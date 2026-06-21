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

#include <cuda_runtime.h>

#include "./vec.cuh"

// A voxel density field for ray marching: a block of densities in VRAM,
// sampled in world space through a borrowed 3D texture. Positive density is
// solid, negative is air, and the surface is the zero crossing, so the
// geometry can be edited in place by writing the underlying voxels.

namespace corvid::cuda {

#pragma region density_field

// A density field sampled in world space: an `extent` of voxels at
// `voxel_size` spacing with voxel (0, 0, 0)'s center at world `origin`, read
// through a borrowed texture. Positive density is solid, negative is
// air, and the surface is the zero crossing.
struct density_field {
#pragma region Data members

  cudaExtent extent;
  pos3 origin;
  float voxel_size;
  cudaTextureObject_t tex;

#pragma endregion
#pragma region Coordinates

  // Continuous voxel-space coordinates of world point `p` (voxel indices, not
  // rounded): the inverse of `voxel_center`.
  [[nodiscard]] __device__ vec3 to_voxel(pos3 p) const {
    return (p - origin) / voxel_size;
  }

  // World-space center of the voxel at index `idx`.
  [[nodiscard]] __device__ pos3 voxel_center(int3 idx) const {
    return origin +
           vec3{static_cast<float>(idx.x) * voxel_size,
               static_cast<float>(idx.y) * voxel_size,
               static_cast<float>(idx.z) * voxel_size};
  }

  // Whether the voxel at index `idx` is inside the field.
  [[nodiscard]] __device__ bool contains(int3 idx) const {
    return idx.x >= 0 && idx.x < static_cast<int>(extent.width) &&
           idx.y >= 0 && idx.y < static_cast<int>(extent.height) &&
           idx.z >= 0 && idx.z < static_cast<int>(extent.depth);
  }

#pragma endregion
#pragma region Sampling

  // Density at world position `p`, trilinearly sampled. The texture reads
  // voxel `v` at coordinate `v + 0.5` and clamps past the edges.
  [[nodiscard]] __device__ float sample_density(pos3 p) const {
    const vec3 v = to_voxel(p);
    return tex3D<float>(tex, v.x + 0.5F, v.y + 0.5F, v.z + 0.5F);
  }

  // Outward surface normal at `p`, from the density gradient by central
  // differences. Density rises into the solid, so the outward normal opposes
  // the gradient.
  [[nodiscard]] __device__ vec3 normal(pos3 p) const {
    const float e = voxel_size;
    const vec3 dx{e, 0.0F, 0.0F};
    const vec3 dy{0.0F, e, 0.0F};
    const vec3 dz{0.0F, 0.0F, e};
    const float gx = sample_density(p + dx) - sample_density(p - dx);
    const float gy = sample_density(p + dy) - sample_density(p - dy);
    const float gz = sample_density(p + dz) - sample_density(p - dz);
    return normalize(vec3{-gx, -gy, -gz});
  }

#pragma endregion
#pragma region Marching

  // March from `eye` along unit `dir` to the first solid surface, returning
  // the distance to the hit, or a negative value on a miss. Fixed-step until
  // the density crosses from air into solid, then bisect that interval for a
  // crisp surface.
  [[nodiscard]] __device__ float raymarch(pos3 eye, vec3 dir) const {
    constexpr float far_dist = 160.0F;
    constexpr int max_steps = 512;
    constexpr int refine_steps = 6;
    const float step_size = voxel_size * 0.5F;
    float dist = 0.0F;
    for (int step = 0; step < max_steps; ++step) {
      dist += step_size;
      if (dist > far_dist) break;
      if (sample_density(eye + (dir * dist)) >= 0.0F) {
        float lo = dist - step_size;
        float hi = dist;
        for (int refine = 0; refine < refine_steps; ++refine) {
          const float mid = 0.5F * (lo + hi);
          if (sample_density(eye + (dir * mid)) >= 0.0F)
            hi = mid;
          else
            lo = mid;
        }
        return hi;
      }
    }
    return -1.0F;
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
