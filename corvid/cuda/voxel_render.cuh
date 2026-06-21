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

#include "./density_field.cuh"
#include "./vec.cuh"

// Per-ray shading for the voxel viewer: march the density field to the first
// solid surface and light it, tinting the hit with the smoothly filtered color
// grid, or return the sky on a miss. The kernel tonemaps the result.

namespace corvid::cuda {

#pragma region Rendering

// March one ray and return its linear (pre-tonemap) color: the lit terrain at
// the hit, tinted by the smoothly filtered color grid, or the sky on a miss.
[[nodiscard]] __device__ inline vec3
shade_terrain_ray(const density_field& field, cudaTextureObject_t color,
    pos3 eye, vec3 ray_dir) {
  const float dist = field.raymarch(eye, ray_dir);
  if (dist < 0.0F) {
    // Sky gradient by ray height.
    const float blend = (0.5F * ray_dir.y) + 0.5F;
    return (vec3{0.50F, 0.70F, 1.00F} * blend) +
           (vec3{0.90F, 0.95F, 1.00F} * (1.0F - blend));
  }
  const pos3 hit_point = eye + (ray_dir * dist);
  const vec3 normal = field.normal(hit_point);
  const vec3 light_dir = normalize(vec3{0.5F, 0.8F, 0.3F});
  const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);
  const vec3 ambient{0.18F, 0.20F, 0.24F};
  const vec3 sun{1.0F, 0.96F, 0.88F};
  // Albedo from the linearly filtered color grid: one hardware-filtered fetch
  // in the field's coordinates, so strata seams stay smooth like the density.
  const vec3 vf = field.to_voxel(hit_point);
  const auto c = tex3D<float4>(color, vf.x + 0.5F, vf.y + 0.5F, vf.z + 0.5F);
  return vec3{c.x, c.y, c.z} * (ambient + (sun * diffuse));
}

#pragma endregion

} // namespace corvid::cuda
