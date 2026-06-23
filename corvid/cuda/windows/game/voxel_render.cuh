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
#include <numbers>

#include <cuda_runtime.h>

#include "../../density_field.cuh"
#include "../../vec.cuh"
#include "./render_config.cuh"

// Per-ray rendering for the voxel viewer: the sky backdrop, the lit terrain
// sampled from the density field and the filtered color grid, and a 2D hexagon
// SDF. These read the viewer's `render_config`, so they live with the rest of
// the game viewer; the avatar and mirror compositing that builds on them is in
// scene_render.cuh.

namespace corvid::cuda {

#pragma region Rendering

// Sky color along `ray_dir`: a deep zenith easing to a bright, pale horizon,
// plus a warm sun glow toward the light direction (a tight core in a softer
// halo). The sun direction matches the terrain light, so the brightest sky
// sits where the ground is lit.
[[nodiscard]] __device__ inline vec3
sky_color(const render_config& cfg, vec3 ray_dir) {
  const render_config::sky_params& sky = cfg.sky;
  const float up = fmaxf(ray_dir.y, 0.0F);
  const float t = powf(up, sky.gradient_bias);
  vec3 col = (sky.zenith * t) + (sky.horizon * (1.0F - t));

  const vec3 sun_dir = normalize(cfg.sun_direction);
  const float s = fmaxf(dot(ray_dir, sun_dir), 0.0F);
  col = col +
        (sky.halo_color * (sky.halo_strength * powf(s, sky.halo_exponent)));
  col = col + (sky.core_color * powf(s, sky.core_exponent));
  return col;
}

// Lit terrain color at surface point `hit_point`, tinted by the smoothly
// filtered color grid.
[[nodiscard]] __device__ inline vec3
shade_terrain_hit(const density_field& field, cudaTextureObject_t color,
    const render_config& cfg, pos3 hit_point) {
  const vec3 normal = field.normal(hit_point);
  const vec3 light_dir = normalize(cfg.sun_direction);
  const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);
  // Albedo from the linearly filtered color grid: one hardware-filtered fetch
  // in the field's coordinates, so strata seams stay smooth like the density.
  const vec3 vf = field.to_voxel(hit_point);
  const auto c = tex3D<float4>(color, vf.x + 0.5F, vf.y + 0.5F, vf.z + 0.5F);
  return vec3{c.x, c.y, c.z} *
         (cfg.terrain.ambient + (cfg.terrain.sun * diffuse));
}

// Signed distance from 2D point (`x`, `y`) to a regular hexagon of radius `r`
// (the apothem, its flat-to-flat half-width), negative inside. Inigo Quilez's
// hexagon SDF, used to stamp the cockpit eyes as crisp shapes.
[[nodiscard]] __device__ inline float hexagon_sd(float x, float y, float r) {
  constexpr float kx = -std::numbers::sqrt3_v<float> / 2.0F; // -cos(30 deg)
  constexpr float ky = 0.5F;                                 // +sin(30 deg)
  constexpr float kz = std::numbers::inv_sqrt3_v<float>;     // +tan(30 deg)

  x = fabsf(x);
  y = fabsf(y);
  const float m = 2.0F * fminf((kx * x) + (ky * y), 0.0F);
  x -= m * kx;
  y -= m * ky;
  x -= fminf(fmaxf(x, -kz * r), kz * r);
  y -= r;
  return copysignf(sqrtf((x * x) + (y * y)), y);
}

#pragma endregion

} // namespace corvid::cuda
