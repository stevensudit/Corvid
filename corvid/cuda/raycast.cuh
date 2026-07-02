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
#include <concepts>

#include "./vec.cuh"

// Scene-independent building blocks for an SDF ray marcher: shading terms that
// march a caller-supplied distance field, plus color-to-byte conversions. The
// field is a template parameter `Sdf`, a `float(pos3)` callable, so these work
// against any scene.

namespace corvid::cuda {

#pragma region Shading

// Surface normal at `p`, estimated from the gradient of the scene field `Sdf`
// by central differences.
template<float Sdf(pos3)>
[[nodiscard]] __device__ vec3 surface_normal(pos3 p) {
  static constexpr float eps = 0.0005F;
  static constexpr vec3 dx{eps, 0.0F, 0.0F};
  static constexpr vec3 dy{0.0F, eps, 0.0F};
  static constexpr vec3 dz{0.0F, 0.0F, eps};
  return normalize(vec3{Sdf(p + dx) - Sdf(p - dx), Sdf(p + dy) - Sdf(p - dy),
      Sdf(p + dz) - Sdf(p - dz)});
}

// Penumbra factor in [0, 1] for the ray from `p` toward `light_dir` through
// the scene field `Sdf`: 1 is fully lit, 0 fully shadowed. `hardness` sets how
// sharply the penumbra falls off.
template<float Sdf(pos3)>
[[nodiscard]] __device__ float
soft_shadow(pos3 p, vec3 light_dir, float hardness) {
  // Heuristic max steps to reach the light; more is softer but slower.
  static constexpr int limit = 48;

  float result = 1.0F;
  float dist = 0.02F;
  for (int step = 0; step < limit; ++step) {
    const float surf_dist = Sdf(p + (light_dir * dist));
    if (surf_dist < 0.001F) return 0.0F;

    result = fminf(result, (hardness * surf_dist) / dist);
    dist += surf_dist;
    if (dist > 20.0F) break;
  }
  return result;
}

// Ambient occlusion in [0, 1] at `p` with the given `normal`, sampling the
// scene field `Sdf`: 1 is unoccluded.
template<float Sdf(pos3)>
[[nodiscard]] __device__ float ambient_occlusion(pos3 p, vec3 normal) {
  // Heuristic number of ambient occlusion samples; more is darker but slower.
  constexpr int ao_samples = 5;

  // Samples march outward from the surface, starting at `min_offset` and
  // stepping by `offset_step` (together spanning `offset_span`); nearer ones
  // weigh more, and `strength` scales the result.
  constexpr float min_offset = 0.01F;
  constexpr float offset_span = 0.12F;
  constexpr float offset_step =
      offset_span / static_cast<float>(ao_samples - 1);
  constexpr float weight_falloff = 0.7F;
  constexpr float strength = 3.0F;

  float occlusion = 0.0F;
  float weight = 1.0F;
  float offset = min_offset;
  for (int step = 0; step < ao_samples; ++step) {
    const float surf_dist = Sdf(p + (normal * offset));
    occlusion += (offset - surf_dist) * weight;
    weight *= weight_falloff;
    offset += offset_step;
  }
  return fminf(fmaxf(1.0F - (strength * occlusion), 0.0F), 1.0F);
}

#pragma endregion
#pragma region Rendering

// What a scene policy must supply to `shade_ray`: static `__device__` members
// for the distance field, the surface albedo, and the background sky.
//
// A `scene_policy` type supplies three static `__device__` members:
// `sdf(pos3)` (the signed-distance field), `albedo(pos3)` (base color at a
// surface point), and `sky(vec3)` (background for a ray that hits nothing).
template<typename S>
concept scene_policy = requires(pos3 p, vec3 dir) {
  { S::sdf(p) } -> std::same_as<float>;
  { S::albedo(p) } -> std::same_as<vec3>;
  { S::sky(dir) } -> std::same_as<vec3>;
};

// Trace `ray_dir` from `eye` through a `Scene` policy and return its
// tonemapped color.
//
// A hit is shaded by one directional light (`light_dir`) with a soft shadow
// and ambient occlusion; `pixel_eps` is the marcher's distance-scaled hit
// threshold (about one pixel's cone).
template<scene_policy Scene>
[[nodiscard]] __device__ vec3 shade_ray(pos3 eye, vec3 ray_dir,
    float pixel_eps, vec3 light_dir) {
  // Sphere-trace to the nearest surface or the far limit. The threshold
  // `pixel_eps * dist` grows with distance (about one pixel's cone) so grazing
  // rays toward the horizon, and rays slipping just past a silhouette,
  // converge within the step budget instead of exhausting it and falling
  // through to sky (which bent the horizon near silhouettes and left a
  // sky-colored fringe). Near surfaces keep the tight `min_eps`, so
  // silhouettes stay sharp.
  constexpr int max_steps = 256;
  constexpr float far_dist = 50.0F;
  constexpr float min_eps = 0.001F;
  float dist = 0.0F;
  bool hit = false;
  for (int step = 0; step < max_steps; ++step) {
    const float surf_dist = Scene::sdf(eye + (ray_dir * dist));
    if (surf_dist < fmaxf(min_eps, pixel_eps * dist)) {
      hit = true;
      break;
    }
    dist += surf_dist;
    if (dist > far_dist) break;
  }

  vec3 color;
  if (hit) {
    const pos3 hit_point = eye + (ray_dir * dist);
    const vec3 normal = surface_normal<Scene::sdf>(hit_point);
    const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);
    const float shadow = soft_shadow<Scene::sdf>(hit_point, light_dir, 16.0F);
    const float occlusion = ambient_occlusion<Scene::sdf>(hit_point, normal);

    const vec3 ambient{0.12F, 0.14F, 0.18F};
    const vec3 sun{1.0F, 0.97F, 0.90F};
    color = Scene::albedo(hit_point) *
            ((ambient * occlusion) + (sun * (diffuse * shadow)));
  } else {
    color = Scene::sky(ray_dir);
  }

  // Reinhard tonemap so highlights roll off.
  return vec3{color.x / (1.0F + color.x), color.y / (1.0F + color.y),
      color.z / (1.0F + color.z)};
}

#pragma endregion
#pragma region Output

// Convert a linear color channel in [0, 1] to a gamma-encoded byte.
[[nodiscard]] __device__ inline unsigned char to_byte(float c) {
  constexpr float gamma = 2.2F; // display gamma (sRGB approximation)
  const float g = powf(fminf(fmaxf(c, 0.0F), 1.0F), 1.0F / gamma);
  return static_cast<unsigned char>(lroundf(g * 255.0F));
}

// Convert a linear color channel in [0, 1] to an 8-bit unorm (no gamma), for
// packing into a normalized-unorm grid that is filtered back as a float.
[[nodiscard]] __device__ inline unsigned char to_unorm8(float c) {
  return static_cast<unsigned char>(
      lroundf(fminf(fmaxf(c, 0.0F), 1.0F) * 255.0F));
}

#pragma endregion

} // namespace corvid::cuda
