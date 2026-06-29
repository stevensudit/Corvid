// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <cmath>

#include <cuda_runtime.h>

#include "../../camera.cuh"
#include "../../cuda_kernel.cuh"
#include "../../density_field.cuh"
#include "../../mirror.cuh"
#include "../../raycast.cuh"
#include "../../vec.cuh"
#include "./avatar.cuh"
#include "./render_config.cuh"
#include "./scene_render.cuh"

// The voxel viewer's primary render: an adaptive two-pass anti-alias against
// the terrain, avatar, mirror, and sky. A prepass shades one center sample per
// pixel and records the hit geometry; a resolve pass supersamples only the
// pixels on a silhouette, leaving flat interiors at the single sample.

namespace corvid::cuda {

#pragma region Rendering

// The adaptive-AA prepass output, one per pixel: the nearest-hit `kind` and
// its `depth`. The resolve pass reads a pixel's own texel and its four
// neighbors to decide whether the pixel straddles a silhouette and so needs
// supersampling. Kept compact (8 bytes) so the neighbor gather is cheap.
//
// `kind` carries the hit kind in its low bits with a high "reticle edge" flag
// bit: the prepass sets it on pixels the target reticle covers so the resolve
// supersamples them too (the reticle's edges sit on flat same-kind terrain the
// kind/depth test cannot see). The flag is masked off before the kind
// comparison, so it never reads as a geometry edge by itself.
inline constexpr int aa_reticle_edge_bit = 1 << 8;
inline constexpr int aa_kind_mask = aa_reticle_edge_bit - 1;

struct aa_texel {
  float depth;
  int kind;
};

// Reinhard tone map: roll each linear channel off into [0, 1) so highlights
// compress smoothly instead of clipping. Applied once in the composite, after
// bloom is added, since bloom works in the linear HDR domain.
[[nodiscard]] __device__ inline vec3 reinhard_tonemap(vec3 c) {
  return vec3{c.x / (1.0F + c.x), c.y / (1.0F + c.y), c.z / (1.0F + c.z)};
}

// Store a linear HDR color into the off-screen render buffer at `idx`
// (`py * width + px`). The render kernels write here; the post pass reads it,
// blooms the brights, tone maps, and lands the LDR surface.
__device__ inline void store_hdr(float4* hdr, int idx, vec3 color) {
  hdr[idx] = make_float4(color.x, color.y, color.z, 1.0F);
}

// Pack a tonemapped [0, 1] color and write it to the interop surface. The
// texture is `R8G8B8A8_UNORM`, so a `uchar4` of (r, g, b, a) maps straight to
// its bytes.
__device__ inline void
write_surface(cudaSurfaceObject_t out, int px, int py, vec3 c) {
  const uchar4 pixel =
      make_uchar4(to_byte(c.x), to_byte(c.y), to_byte(c.z), 255);
  surf2Dwrite(pixel, out, px * static_cast<int>(sizeof(uchar4)), py);
}

// World size of one pixel per unit distance (2 tan(fov/2) / height), for the
// reticle's distance-aware anti-aliasing (it floors its edge softness to about
// a pixel).
[[nodiscard]] __device__ inline float
pixel_world_scale(const camera_rays& cam, resolution res) {
  return (cam.tan_half_fov * 2.0F) / res.height;
}

// `__launch_bounds__` caps registers on both render kernels so more 256-thread
// blocks stay resident: uncapped, the march wants 136 registers, leaving only
// one block per SM (~17% occupancy) to hide texture-fetch latency, which
// roughly halves the frame rate. Capping trades a little register spill for
// more resident blocks; measured on the terrain view, 3 blocks (~80 registers,
// ~50% occupancy) is the peak, past which the added spill outweighs the
// occupancy.
//
// The large scene params (`field`, `ball`, `head`, `mirror`, `cfg`) are
// `__grid_constant__`: the shaders take them by const reference, and without
// this the compiler copies each whole struct (`render_config` alone is ~520
// bytes) into a per-thread local stack frame just to pass a pointer.
// `__grid_constant__` binds the reference straight to the parameter in
// constant memory, with no copy. `res` and `cam` are read directly here, not
// passed by reference, so they need no annotation.

// Prepass: shade one ray through each pixel's center, write that linear HDR
// color into `hdr`, and record the hit kind and depth into `gbuf` for the
// resolve pass. On its own (`aa_samples` <= 1) this is the whole render at one
// sample per pixel.
__global__ void __launch_bounds__(256, 3) aa_prepass_kernel(float4* hdr,
    aa_texel* gbuf, resolution res, camera_rays cam,
    __grid_constant__ const density_field field, cudaTextureObject_t color_tex,
    __grid_constant__ const metal_ball ball,
    __grid_constant__ const saucer_head head,
    __grid_constant__ const flat_mirror mirror,
    __grid_constant__ const render_config cfg) {
  const int px = cuda_kernel::x_index();
  const int py = cuda_kernel::y_index();
  const auto fx = static_cast<float>(px);
  const auto fy = static_cast<float>(py);
  if (fx >= res.width || fy >= res.height) return;

  const float px_scale = pixel_world_scale(cam, res);
  const vec3 ray_dir = cam.ray_direction(pos2{vec2{fx + 0.5F, fy + 0.5F}}, res,
      cfg.fisheye_amount);
  const ray_sample s = shade_primary_ray(field, color_tex, ball, head, mirror,
      cfg, cam.eye, ray_dir, px_scale);

  const int w = static_cast<int>(res.width);
  const int packed = s.kind | (s.reticle_edge ? aa_reticle_edge_bit : 0);
  gbuf[(py * w) + px] = aa_texel{s.depth, packed};
  store_hdr(hdr, (py * w) + px, s.color);
}

// Resolve: supersample the pixels the prepass marked as silhouettes, leaving
// the rest at their prepass center sample. A pixel is a silhouette when its
// nearest-hit kind differs from a 4-neighbor, or its depth bends sharply
// across the neighbors. The bend is a second difference (`2*center - lo -
// hi`), so a smoothly receding grazing surface, whose depth ramps almost
// linearly, does not count: only a crease or a true depth jump does. Border
// neighbors clamp to the center. Non-edge pixels return immediately, leaving
// the prepass write intact; edge pixels overwrite it with the full
// `aa_samples` x `aa_samples` average.
__global__ void __launch_bounds__(256, 3) aa_resolve_kernel(float4* hdr,
    const aa_texel* gbuf, resolution res, camera_rays cam,
    __grid_constant__ const density_field field, cudaTextureObject_t color_tex,
    __grid_constant__ const metal_ball ball,
    __grid_constant__ const saucer_head head,
    __grid_constant__ const flat_mirror mirror,
    __grid_constant__ const render_config cfg) {
  const int px = cuda_kernel::x_index();
  const int py = cuda_kernel::y_index();
  const auto fx = static_cast<float>(px);
  const auto fy = static_cast<float>(py);
  if (fx >= res.width || fy >= res.height) return;

  const int w = static_cast<int>(res.width);
  const int h = static_cast<int>(res.height);
  const int xm = (px > 0) ? px - 1 : 0;
  const int xp = (px < w - 1) ? px + 1 : w - 1;
  const int ym = (py > 0) ? py - 1 : 0;
  const int yp = (py < h - 1) ? py + 1 : h - 1;
  const aa_texel me = gbuf[(py * w) + px];
  const aa_texel left = gbuf[(py * w) + xm];
  const aa_texel right = gbuf[(py * w) + xp];
  const aa_texel up = gbuf[(ym * w) + px];
  const aa_texel down = gbuf[(yp * w) + px];

  // The target reticle forces its own pixels to supersample (the kind/depth
  // test is blind to it, since it paints flat same-kind terrain).
  const int my_kind = me.kind & aa_kind_mask;
  // The reticle flag (bit 8) plus any 4-neighbor kind change (bits 0-7) folded
  // into one OR-of-XORs: the two bit ranges do not overlap, so a nonzero
  // result means "force supersample" (reticle pixel or geometry silhouette).
  // Masking the reticle flag out of `my_kind` keeps it from reading as a kind
  // change by itself.
  const int edge_bits =
      (me.kind & aa_reticle_edge_bit) |
      (my_kind ^ (left.kind & aa_kind_mask)) |
      (my_kind ^ (right.kind & aa_kind_mask)) |
      (my_kind ^ (up.kind & aa_kind_mask)) |
      (my_kind ^ (down.kind & aa_kind_mask));
  bool edge = edge_bits != 0;
  if (!edge) {
    // All four neighbors share this pixel's kind, so the depths are comparable
    // (a sky miss would have changed the kind). `me.depth` scales the
    // threshold so a distant ramp is judged in proportion to its range.
    //
    // The bend is grouped as a difference of slopes, `(me - lo) - (hi - me)`,
    // not `2*me - lo - hi`: on a sky pixel the depth is the `big_value`
    // sentinel, and `2 * big_value` overflows to inf, which trips the test and
    // supersamples the whole sky. Grouped this way the sentinel cancels
    // (`big - big = 0`) before any doubling.
    const float tol = cfg.aa_edge_depth * me.depth;
    edge = fabsf((me.depth - left.depth) - (right.depth - me.depth)) > tol ||
           fabsf((me.depth - up.depth) - (down.depth - me.depth)) > tol;
  }
  if (!edge) return; // keep the prepass center sample

  // Edge-detection debug: flat-tint the supersampled pixels (blue = reticle,
  // red = geometry) instead of shading them, so the marked pixels are visible.
  // Blue, not green, so the reticle flag does not blend into the reticle's own
  // green glow.
  if (cfg.debug_aa_edges) {
    const bool reticle = (me.kind & aa_reticle_edge_bit) != 0;
    store_hdr(hdr, (py * w) + px,
        reticle ? vec3{0.0F, 0.0F, 8.0F} : vec3{8.0F, 0.0F, 0.0F});
    return;
  }

  const int aa_samples = cfg.aa_samples;
  const float inv = 1.0F / static_cast<float>(aa_samples);
  const float px_scale = pixel_world_scale(cam, res);
  vec3 color{};
  for (int sy = 0; sy < aa_samples; ++sy)
    for (int sx = 0; sx < aa_samples; ++sx) {
      const float ox = (static_cast<float>(sx) + 0.5F) * inv;
      const float oy = (static_cast<float>(sy) + 0.5F) * inv;
      const vec3 ray_dir = cam.ray_direction(pos2{vec2{fx + ox, fy + oy}}, res,
          cfg.fisheye_amount);
      color =
          color +
          shade_primary_ray(field, color_tex, ball, head, mirror, cfg, cam.eye,
              ray_dir, px_scale)
              .color;
    }
  store_hdr(hdr, (py * w) + px, color * (inv * inv));
}

// Render one frame into the linear HDR buffer: the prepass over every pixel,
// then the resolve over the silhouettes. The resolve launch is skipped when AA
// is off (`aa_samples` <= 1), since the prepass already wrote the final
// single-sample image. Both passes share the launch grid and the scene; `gbuf`
// must hold at least `res.width` x `res.height` `aa_texel`s, and `hdr` the
// same count of `float4`s. The post pass turns `hdr` into the LDR surface.
inline void render_scene(float4* hdr, aa_texel* gbuf, dim3 grid, dim3 block,
    resolution res, const camera_rays& cam, const density_field& field,
    cudaTextureObject_t color_tex, const metal_ball& ball,
    const saucer_head& head, const flat_mirror& mirror,
    const render_config& cfg) {
  aa_prepass_kernel<<<grid, block>>>(hdr, gbuf, res, cam, field, color_tex,
      ball, head, mirror, cfg);
  if (cfg.aa_samples > 1)
    aa_resolve_kernel<<<grid, block>>>(hdr, gbuf, res, cam, field, color_tex,
        ball, head, mirror, cfg);
}

#pragma endregion
#pragma region Post-process

// Width and height of the half-resolution bloom buffers for a `w` x `h` frame.
// Rounded up, so the half buffers cover the frame when a dimension is odd.
[[nodiscard]] inline int bloom_dim(int full) { return (full + 1) / 2; }

// Half-resolution luma of a 2x2 HDR block, soft-thresholded so only the
// brights bloom. Reads the four full-res texels under output texel (`bx`,
// `by`), averages them (a box downsample that also damps single-pixel
// fireflies), and scales the average by a soft knee: full weight well above
// `threshold`, a quadratic ramp through the `knee`-wide shoulder below it, and
// zero further down. The bloom buffer is half the frame on each axis.
__global__ void bloom_prefilter_kernel(const float4* hdr, float4* bloom,
    resolution full, resolution half,
    __grid_constant__ const render_config cfg) {
  const int bx = cuda_kernel::x_index();
  const int by = cuda_kernel::y_index();
  const int hw = static_cast<int>(half.width);
  const int hh = static_cast<int>(half.height);
  if (bx >= hw || by >= hh) return;

  const int fw = static_cast<int>(full.width);
  const int fh = static_cast<int>(full.height);
  const int x0 = bx * 2;
  const int y0 = by * 2;
  const int x1 = min(x0 + 1, fw - 1);
  const int y1 = min(y0 + 1, fh - 1);
  const float4 a = hdr[(y0 * fw) + x0];
  const float4 b = hdr[(y0 * fw) + x1];
  const float4 c = hdr[(y1 * fw) + x0];
  const float4 d = hdr[(y1 * fw) + x1];
  vec3 avg{0.25F * (a.x + b.x + c.x + d.x), 0.25F * (a.y + b.y + c.y + d.y),
      0.25F * (a.z + b.z + c.z + d.z)};

  // Soft-threshold knee (the Call of Duty / Unity curve): isolate the energy
  // above `threshold` without a hard cutoff, so a pixel hovering at the
  // threshold fades in instead of popping.
  const float luma = dot(avg, vec3{0.2126F, 0.7152F, 0.0722F});
  const float knee = fmaxf(cfg.bloom.threshold * cfg.bloom.knee, 1.0e-4F);
  float soft =
      __saturatef((luma - cfg.bloom.threshold + knee) / (2.0F * knee));
  soft = soft * soft * knee * 2.0F; // quadratic ramp across the knee
  const float over = fmaxf(soft, luma - cfg.bloom.threshold);
  const float weight = over / fmaxf(luma, 1.0e-4F);

  bloom[(by * hw) + bx] =
      make_float4(avg.x * weight, avg.y * weight, avg.z * weight, 1.0F);
}

// One axis of a separable Gaussian blur over the half-res bloom buffer
// (`dx`, `dy` selects horizontal or vertical), ping-ponging `src` into `dst`.
// The radius is sized from `bloom.sigma` and capped, so a wider blur spends a
// few more taps rather than reallocating. Samples clamp at the buffer edges.
__global__ void bloom_blur_kernel(const float4* src, float4* dst,
    resolution half, int dx, int dy,
    __grid_constant__ const render_config cfg) {
  const int x = cuda_kernel::x_index();
  const int y = cuda_kernel::y_index();
  const int hw = static_cast<int>(half.width);
  const int hh = static_cast<int>(half.height);
  if (x >= hw || y >= hh) return;

  const float sigma = fmaxf(cfg.bloom.sigma, 1.0e-3F);
  const float inv_two_sigma_sq = 1.0F / (2.0F * sigma * sigma);
  constexpr int max_radius = 8;
  int radius = static_cast<int>(ceilf(2.0F * sigma));
  radius = min(radius, max_radius);

  vec3 sum{};
  float wsum = 0.0F;
  for (int k = -radius; k <= radius; ++k) {
    const float w = expf(-static_cast<float>(k * k) * inv_two_sigma_sq);
    const int sx = min(max(x + (k * dx), 0), hw - 1);
    const int sy = min(max(y + (k * dy), 0), hh - 1);
    const float4 s = src[(sy * hw) + sx];
    sum = sum + (vec3{s.x, s.y, s.z} * w);
    wsum += w;
  }
  const float inv = 1.0F / wsum;
  dst[(y * hw) + x] = make_float4(sum.x * inv, sum.y * inv, sum.z * inv, 1.0F);
}

// Bilinearly sample the half-res bloom buffer at the full-res pixel center
// (`px`, `py`), so the upsample does not show the half-res grid.
[[nodiscard]] __device__ inline vec3
sample_bloom(const float4* bloom, int hw, int hh, int px, int py) {
  const float fx = (static_cast<float>(px) * 0.5F) - 0.25F;
  const float fy = (static_cast<float>(py) * 0.5F) - 0.25F;
  const int x0 = max(static_cast<int>(floorf(fx)), 0);
  const int y0 = max(static_cast<int>(floorf(fy)), 0);
  const int x1 = min(x0 + 1, hw - 1);
  const int y1 = min(y0 + 1, hh - 1);
  const float tx = __saturatef(fx - static_cast<float>(x0));
  const float ty = __saturatef(fy - static_cast<float>(y0));
  const float4 a = bloom[(y0 * hw) + x0];
  const float4 b = bloom[(y0 * hw) + x1];
  const float4 c = bloom[(y1 * hw) + x0];
  const float4 d = bloom[(y1 * hw) + x1];
  const vec3 top =
      vec3{a.x, a.y, a.z} + ((vec3{b.x, b.y, b.z} - vec3{a.x, a.y, a.z}) * tx);
  const vec3 bot =
      vec3{c.x, c.y, c.z} + ((vec3{d.x, d.y, d.z} - vec3{c.x, c.y, c.z}) * tx);
  return top + ((bot - top) * ty);
}

// Composite: add the upsampled bloom to the linear HDR color, Reinhard tone
// map, and write the LDR interop surface. With `use_bloom` false the bloom add
// is skipped (and `bloom` may be null), leaving a plain tone map identical to
// the render's old inline one.
__global__ void composite_kernel(const float4* hdr, const float4* bloom,
    cudaSurfaceObject_t out, resolution full, resolution half, bool use_bloom,
    __grid_constant__ const render_config cfg) {
  const int px = cuda_kernel::x_index();
  const int py = cuda_kernel::y_index();
  const int fw = static_cast<int>(full.width);
  const int fh = static_cast<int>(full.height);
  if (px >= fw || py >= fh) return;

  const float4 h = hdr[(py * fw) + px];
  vec3 color{h.x, h.y, h.z};
  if (use_bloom) {
    const vec3 b = sample_bloom(bloom, static_cast<int>(half.width),
        static_cast<int>(half.height), px, py);
    color = color + (b * cfg.bloom.intensity);
  }
  write_surface(out, px, py, reinhard_tonemap(color));
}

// Post-process the linear HDR render into the LDR surface: bloom the brights
// (prefilter to half-res, separable Gaussian blur, add back) then tone map.
// When bloom is off it is a single tone-map pass. `bloom_a` and `bloom_b` are
// half-res ping-pong buffers, each at least `bloom_dim(w)` x `bloom_dim(h)`
// `float4`s; they are untouched when bloom is off.
inline void post_process(cudaSurfaceObject_t out, const float4* hdr,
    float4* bloom_a, float4* bloom_b, dim3 block, resolution res,
    const render_config& cfg) {
  const int w = static_cast<int>(res.width);
  const int h = static_cast<int>(res.height);
  const dim3 full_grid{cuda_kernel::ceil_div(w, block.x),
      cuda_kernel::ceil_div(h, block.y)};
  if (!cfg.bloom.enabled) {
    composite_kernel<<<full_grid, block>>>(hdr, nullptr, out, res, res, false,
        cfg);
    return;
  }
  const int hw = bloom_dim(w);
  const int hh = bloom_dim(h);
  const resolution half{static_cast<float>(hw), static_cast<float>(hh)};
  const dim3 half_grid{cuda_kernel::ceil_div(hw, block.x),
      cuda_kernel::ceil_div(hh, block.y)};
  bloom_prefilter_kernel<<<half_grid, block>>>(hdr, bloom_a, res, half, cfg);
  bloom_blur_kernel<<<half_grid, block>>>(bloom_a, bloom_b, half, 1, 0, cfg);
  bloom_blur_kernel<<<half_grid, block>>>(bloom_b, bloom_a, half, 0, 1, cfg);
  composite_kernel<<<full_grid, block>>>(hdr, bloom_a, out, res, half, true,
      cfg);
}

#pragma endregion

} // namespace corvid::cuda
