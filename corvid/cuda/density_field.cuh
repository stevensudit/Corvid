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
    const int w = static_cast<int>(extent.width);
    const int h = static_cast<int>(extent.height);
    const int d = static_cast<int>(extent.depth);
    return idx.x >= 0 && idx.x < w && idx.y >= 0 && idx.y < h && idx.z >= 0 &&
           idx.z < d;
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
  // the distance to the hit, or a negative value on a miss.
  //
  // The ray is first clipped to the field's world box, so the march never
  // samples the clamped extrusion outside the volume: outside the box is sky.
  // Within it the march sphere-traces: in air the surface is at least
  // `|density| / lipschitz` away, since the field changes by at most
  // `lipschitz` per unit, so a step that long cannot skip it. Steps shrink
  // toward the surface. A head-on ray crosses into solid and the interval is
  // bisected for a crisp hit; a grazing ray, which nears the surface without
  // crossing, is accepted once within `hit_epsilon`. `lipschitz` must exceed
  // the field's steepest slope, or a dig wall steeper than it can be overshot.
  [[nodiscard]] __device__ float raymarch(pos3 eye, vec3 dir) const {
    constexpr int max_steps = 1024;
    constexpr int refine_steps = 6;
    constexpr float lipschitz = 4.0F;
    constexpr float hit_epsilon = 0.02F;
    const float max_step = voxel_size * 8.0F;

    // Clip to the world box (slab test). Voxel (0, 0, 0) is centered at
    // `origin`, so the box runs half a voxel past the first and last centers.
    const float half = 0.5F * voxel_size;
    const vec3 bmin{origin.v.x - half, origin.v.y - half, origin.v.z - half};
    const vec3 bmax{
        origin.v.x + ((static_cast<float>(extent.width) - 0.5F) * voxel_size),
        origin.v.y + ((static_cast<float>(extent.height) - 0.5F) * voxel_size),
        origin.v.z + ((static_cast<float>(extent.depth) - 0.5F) * voxel_size)};
    const vec3 inv{1.0F / dir.x, 1.0F / dir.y, 1.0F / dir.z};
    const float tx0 = (bmin.x - eye.v.x) * inv.x;
    const float tx1 = (bmax.x - eye.v.x) * inv.x;
    const float ty0 = (bmin.y - eye.v.y) * inv.y;
    const float ty1 = (bmax.y - eye.v.y) * inv.y;
    const float tz0 = (bmin.z - eye.v.z) * inv.z;
    const float tz1 = (bmax.z - eye.v.z) * inv.z;
    const float enter = fmaxf(fmaxf(fminf(tx0, tx1), fminf(ty0, ty1)),
        fmaxf(fminf(tz0, tz1), 0.0F));
    const float exit =
        fminf(fminf(fmaxf(tx0, tx1), fmaxf(ty0, ty1)), fmaxf(tz0, tz1));
    if (exit < enter) return -1.0F;

    float prev = enter;
    float dist = enter;
    for (int step = 0; step < max_steps; ++step) {
      const float density = sample_density(eye + (dir * dist));
      if (density >= 0.0F) {
        float lo = prev;
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
      const float safe = -density / lipschitz;
      if (safe < hit_epsilon) return dist;
      prev = dist;
      dist += fminf(safe, max_step);
      if (dist > exit) break;
    }
    return -1.0F;
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
