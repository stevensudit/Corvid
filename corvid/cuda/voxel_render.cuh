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

#include "./avatar.cuh"
#include "./density_field.cuh"
#include "./vec.cuh"

// Per-ray shading for the voxel viewer: composite the player's avatar (the
// metallic ball and the saucer head) against the density field, marching the
// field to the first solid surface and lighting it (tinted by the smoothly
// filtered color grid) or returning the sky on a miss. The ball reflects the
// scene in one bounce. The kernel tonemaps the result.

namespace corvid::cuda {

#pragma region Rendering

// Sky color along `ray_dir`: a deep zenith easing to a bright, pale horizon,
// plus a warm sun glow toward the light direction (a tight core in a softer
// halo). The sun direction matches the terrain light, so the brightest sky
// sits where the ground is lit.
[[nodiscard]] __device__ inline vec3 sky_color(vec3 ray_dir) {
  const float up = fmaxf(ray_dir.y, 0.0F);
  const float t = powf(up, 0.45F); // bias the gradient toward the horizon band
  const vec3 zenith{0.18F, 0.42F, 0.82F};
  const vec3 horizon{0.82F, 0.91F, 0.98F};
  vec3 col = (zenith * t) + (horizon * (1.0F - t));

  const vec3 sun_dir = normalize(vec3{0.5F, 0.8F, 0.3F});
  const float s = fmaxf(dot(ray_dir, sun_dir), 0.0F);
  col = col + (vec3{1.00F, 0.85F, 0.55F} * (0.30F * powf(s, 8.0F))); // halo
  col = col + (vec3{1.00F, 0.96F, 0.88F} * powf(s, 350.0F));         // core
  return col;
}

// Lit terrain color at surface point `hit_point`, tinted by the smoothly
// filtered color grid.
[[nodiscard]] __device__ inline vec3 shade_terrain_hit(
    const density_field& field, cudaTextureObject_t color, pos3 hit_point) {
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

// Shade the saucer head at surface point `hit_point`: plain steel on the domed
// top, and a distinctive spinning belly on the underside, painted with rings
// and spokes and carrying the central flashlight and a ring of amber rim
// lights. The camera rides inside the head, so this is only ever reached by
// the ball's reflection ray, never by a primary ray.
[[nodiscard]] __device__ inline vec3
shade_head(const saucer_head& head, pos3 hit_point, vec3 ray_dir) {
  const vec3 normal = head.normal(hit_point);
  const vec3 light_dir = normalize(vec3{0.5F, 0.8F, 0.3F});
  const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);
  const vec3 ambient{0.10F, 0.11F, 0.13F};
  const vec3 sun{1.0F, 0.96F, 0.88F};

  const float facing_up = dot(normal, head.up);
  const float upside = fmaxf(facing_up, 0.0F);     // the dome
  const float underside = fmaxf(-facing_up, 0.0F); // the belly

  const vec3 ql = head.to_local(hit_point);
  const float rr = sqrtf((ql.x * ql.x) + (ql.z * ql.z)) / head.radius;
  const float ang = atan2f(ql.z, ql.x) + head.spin;

  vec3 albedo{0.55F, 0.58F, 0.62F};
  vec3 emissive{0.0F, 0.0F, 0.0F};

  // The dome: a darker, cooler canopy with faint concentric panel ridges, so
  // the top reads as a hard-tech shell rather than flat gray. A sharper
  // highlight (below) gives it a polished-glass glint.
  if (upside > 0.001F) {
    const vec3 canopy{0.16F, 0.20F, 0.28F};
    const float panels = 0.85F + (0.15F * cosf(rr * 18.0F));
    albedo = ((albedo * (1.0F - upside)) + (canopy * upside)) * panels;
  }

  // The belly: a painted disc (concentric rings * spinning spokes), the
  // central flashlight hub, a ring of amber rim lights, and a propulsion glow
  // that swells with motion.
  if (underside > 0.001F) {
    const float rings = 0.5F + (0.5F * cosf(rr * 26.0F));
    const float spokes = 0.5F + (0.5F * cosf(ang * 12.0F));
    albedo = albedo * (0.35F + (0.65F * rings * spokes));

    const float hub = __saturatef((0.15F - rr) / 0.06F);
    emissive = emissive + (vec3{1.0F, 0.95F, 0.80F} * (2.5F * hub));

    const float ring = expf(-powf((rr - 0.80F) * 14.0F, 2.0F));
    const float dots = powf(0.5F + (0.5F * cosf(ang * 16.0F)), 8.0F);
    emissive = emissive + (vec3{1.0F, 0.50F, 0.12F} * (2.2F * ring * dots));

    // Propulsion: a blue-white engine wash over the belly, strongest at the
    // rim, that brightens as the saucer moves or zooms.
    const float jets = 0.4F + (0.6F * ring);
    emissive =
        emissive + (vec3{0.45F, 0.65F, 1.0F} * (head.thrust * 1.6F * jets));

    emissive = emissive * underside;
  }

  const vec3 half_v = normalize(light_dir - ray_dir);
  const float spec_power = (upside > underside) ? 96.0F : 48.0F;
  const float spec = powf(fmaxf(dot(normal, half_v), 0.0F), spec_power);
  return (albedo * (ambient + (sun * diffuse))) +
         (vec3{1.0F, 1.0F, 1.0F} * (spec * 0.5F)) + emissive;
}

// March one reflection ray through the scene the ball mirrors and return its
// linear color: the nearer of the terrain and the saucer head, or the sky on a
// miss. The head is present here even though it is absent from primary rays,
// so looking at the ball shows the saucer reflected overhead. One bounce: the
// ball does not reflect itself.
[[nodiscard]] __device__ inline vec3
shade_scene_ray(const density_field& field, cudaTextureObject_t color,
    const saucer_head& head, pos3 eye, vec3 ray_dir) {
  const float t_terrain = field.raymarch(eye, ray_dir);
  const float t_head = head.raymarch(eye, ray_dir);
  const bool head_nearer =
      t_head >= 0.0F && (t_terrain < 0.0F || t_head < t_terrain);
  if (head_nearer) return shade_head(head, eye + (ray_dir * t_head), ray_dir);
  if (t_terrain < 0.0F) return sky_color(ray_dir);
  return shade_terrain_hit(field, color, eye + (ray_dir * t_terrain));
}

// Shade the metallic ball at surface point `hit_point`: bounce one reflection
// ray into the scene (terrain, the saucer head, sky), then dim and tint it so
// the ball reads as dark liquid metal rather than a blown-out chrome mirror,
// with a small ambient floor so the darkest reflections do not crush to black.
[[nodiscard]] __device__ inline vec3 shade_ball(const density_field& field,
    cudaTextureObject_t color, const metal_ball& ball, const saucer_head& head,
    pos3 hit_point, vec3 ray_dir) {
  const vec3 normal = ball.normal(hit_point);
  const vec3 env =
      shade_scene_ray(field, color, head, hit_point, reflect(ray_dir, normal));
  constexpr float dim = 0.65F;
  const vec3 tint{0.82F, 0.86F, 0.95F};
  const vec3 ambient_floor{0.05F, 0.055F, 0.07F};
  return (env * dim * tint) + ambient_floor;
}

// Composite the primary ray: whichever of the ball and the terrain is nearer,
// or the sky if it escapes both. The saucer head is not tested here: the
// camera rides inside it, so it never appears in the view itself, only in the
// ball's reflection (which `shade_ball` casts).
[[nodiscard]] __device__ inline vec3
shade_primary_ray(const density_field& field, cudaTextureObject_t color,
    const metal_ball& ball, const saucer_head& head, pos3 eye, vec3 ray_dir) {
  const float t_terrain = field.raymarch(eye, ray_dir);
  const float t_ball = ball.intersect(eye, ray_dir);
  const bool ball_nearer =
      t_ball >= 0.0F && (t_terrain < 0.0F || t_ball < t_terrain);
  if (ball_nearer)
    return shade_ball(field, color, ball, head, eye + (ray_dir * t_ball),
        ray_dir);
  if (t_terrain < 0.0F) return sky_color(ray_dir);
  return shade_terrain_hit(field, color, eye + (ray_dir * t_terrain));
}

#pragma endregion

} // namespace corvid::cuda
