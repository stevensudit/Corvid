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

// Reinhard-tonemap a linear color (so highlights roll off) and write it to the
// interop surface. The texture is `R8G8B8A8_UNORM`, so a `uchar4` of
// (r, g, b, a) maps straight to its bytes.
__device__ inline void
write_pixel(cudaSurfaceObject_t out, int px, int py, vec3 color) {
  color = vec3{color.x / (1.0F + color.x), color.y / (1.0F + color.y),
      color.z / (1.0F + color.z)};
  const uchar4 pixel =
      make_uchar4(to_byte(color.x), to_byte(color.y), to_byte(color.z), 255);
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

// Prepass: shade one ray through each pixel's center, write that color to the
// surface, and record the hit kind and depth into `gbuf` for the resolve pass.
// On its own (`aa_samples` <= 1) this is the whole render at one sample per
// pixel.
__global__ void __launch_bounds__(256, 3) aa_prepass_kernel(
    cudaSurfaceObject_t out, aa_texel* gbuf, resolution res, camera_rays cam,
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
  const vec3 ray_dir =
      cam.ray_direction(pos2{vec2{fx + 0.5F, fy + 0.5F}}, res);
  const ray_sample s = shade_primary_ray(field, color_tex, ball, head, mirror,
      cfg, cam.eye, ray_dir, px_scale);

  const int w = static_cast<int>(res.width);
  const int packed = s.kind | (s.reticle_edge ? aa_reticle_edge_bit : 0);
  gbuf[(py * w) + px] = aa_texel{s.depth, packed};
  write_pixel(out, px, py, s.color);
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
__global__ void __launch_bounds__(256, 3) aa_resolve_kernel(
    cudaSurfaceObject_t out, const aa_texel* gbuf, resolution res,
    camera_rays cam, __grid_constant__ const density_field field,
    cudaTextureObject_t color_tex, __grid_constant__ const metal_ball ball,
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
    write_pixel(out, px, py,
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
      const vec3 ray_dir =
          cam.ray_direction(pos2{vec2{fx + ox, fy + oy}}, res);
      color =
          color +
          shade_primary_ray(field, color_tex, ball, head, mirror, cfg, cam.eye,
              ray_dir, px_scale)
              .color;
    }
  write_pixel(out, px, py, color * (inv * inv));
}

// Render one frame: the prepass over every pixel, then the resolve over the
// silhouettes. The resolve launch is skipped when AA is off (`aa_samples` <=
// 1), since the prepass already wrote the final single-sample image. Both
// passes share the launch grid and the scene; `gbuf` must hold at least
// `res.width` x `res.height` `aa_texel`s.
inline void render_scene(cudaSurfaceObject_t out, aa_texel* gbuf, dim3 grid,
    dim3 block, resolution res, const camera_rays& cam,
    const density_field& field, cudaTextureObject_t color_tex,
    const metal_ball& ball, const saucer_head& head, const flat_mirror& mirror,
    const render_config& cfg) {
  aa_prepass_kernel<<<grid, block>>>(out, gbuf, res, cam, field, color_tex,
      ball, head, mirror, cfg);
  if (cfg.aa_samples > 1)
    aa_resolve_kernel<<<grid, block>>>(out, gbuf, res, cam, field, color_tex,
        ball, head, mirror, cfg);
}

#pragma endregion

} // namespace corvid::cuda
