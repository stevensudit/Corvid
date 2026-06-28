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

// The voxel viewer's primary render kernel: supersample each pixel against the
// terrain, avatar, mirror, and sky, tonemap, and write the result into the
// interop surface.

namespace corvid::cuda {

#pragma region Rendering

// Shade each pixel by supersampling and write the result. The texture is
// `R8G8B8A8_UNORM`, so a `uchar4` of (r, g, b, a) maps straight to its bytes.
//
// `__launch_bounds__` caps registers so more 256-thread blocks stay resident:
// uncapped, the march wants 136 registers, leaving only one block per SM
// (~17% occupancy) to hide texture-fetch latency, which roughly halves the
// frame rate. Capping trades a little register spill for more resident blocks;
// measured on the terrain view, 3 blocks (~80 registers, ~50% occupancy) is
// the peak, past which the added spill outweighs the occupancy.
//
// The large scene params (`field`, `ball`, `head`, `mirror`, `cfg`) are
// `__grid_constant__`: the shaders take them by const reference, and without
// this the compiler copies each whole struct (`render_config` alone is ~520
// bytes) into a per-thread local stack frame just to pass a pointer.
// `__grid_constant__` binds the reference straight to the parameter in
// constant memory, with no copy. `res` and `cam` are read directly here, not
// passed by reference, so they need no annotation.
__global__ void __launch_bounds__(256, 3) voxel_kernel(cudaSurfaceObject_t out,
    resolution res, camera_rays cam,
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

  // Average an `aa_samples` x `aa_samples` grid of sub-pixel rays to
  // anti-alias the silhouettes and soften the strata seams. 1 disables it.
  //
  // Adaptive AA (future): shade one center sample first, then fan out to the
  // full grid only on pixels whose nearest-hit id or depth disagrees with a
  // neighbor, the silhouettes that actually alias, leaving flat interiors at
  // one sample. That spends the AA budget on edges and buys back most of the
  // supersampling cost.
  const int aa_samples = cfg.aa_samples;
  const float inv = 1.0F / static_cast<float>(aa_samples);
  // World size of one pixel per unit distance, for the reticle's distance-
  // aware anti-aliasing (it floors its edge softness to about a pixel).
  const float px_scale = (cam.tan_half_fov * 2.0F) / res.height;
  vec3 color{};
  for (int sy = 0; sy < aa_samples; ++sy)
    for (int sx = 0; sx < aa_samples; ++sx) {
      const float ox = (static_cast<float>(sx) + 0.5F) * inv;
      const float oy = (static_cast<float>(sy) + 0.5F) * inv;
      const vec3 ray_dir =
          cam.ray_direction(pos2{vec2{fx + ox, fy + oy}}, res);
      color = color + shade_primary_ray(field, color_tex, ball, head, mirror,
                          cfg, cam.eye, ray_dir, px_scale);
    }
  color = color * (inv * inv);

  // Reinhard tonemap so highlights roll off.
  color = vec3{color.x / (1.0F + color.x), color.y / (1.0F + color.y),
      color.z / (1.0F + color.z)};

  const uchar4 pixel =
      make_uchar4(to_byte(color.x), to_byte(color.y), to_byte(color.z), 255);
  surf2Dwrite(pixel, out, px * static_cast<int>(sizeof(uchar4)), py);
}

#pragma endregion

} // namespace corvid::cuda
