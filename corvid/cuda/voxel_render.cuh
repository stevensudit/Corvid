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
#include "./render_config.cuh"
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

// Shade the saucer head at surface point `hit_point`: plain steel on the domed
// top, and a distinctive spinning belly on the underside, painted with rings
// and spokes and carrying the central flashlight and a ring of amber rim
// lights. The camera rides inside the head, so this is only ever reached by
// the ball's reflection ray, never by a primary ray.
[[nodiscard]] __device__ inline vec3 shade_head(const saucer_head& head,
    const render_config& cfg, pos3 hit_point, vec3 ray_dir) {
  const render_config::head_params& hp = cfg.head;
  const vec3 normal = head.normal(hit_point);
  const vec3 light_dir = normalize(cfg.sun_direction);
  const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);

  const float facing_up = dot(normal, head.up);
  const float upside = fmaxf(facing_up, 0.0F);     // the dome
  const float underside = fmaxf(-facing_up, 0.0F); // the belly

  const vec3 ql = head.to_local(hit_point);
  const float rr = sqrtf((ql.x * ql.x) + (ql.z * ql.z)) / head.radius;
  const float ang = atan2f(ql.z, ql.x) + head.spin;

  vec3 albedo = hp.base_albedo;
  vec3 emissive{0.0F, 0.0F, 0.0F};

  // The dome: a darker, cooler canopy with faint concentric panel ridges, so
  // the top reads as a hard-tech shell rather than flat gray. A sharper
  // highlight (below) gives it a polished-glass glint.
  if (upside > 0.001F) {
    const float panels =
        (1.0F - hp.panel_amplitude) +
        (hp.panel_amplitude * cosf(rr * hp.panel_frequency));
    albedo = ((albedo * (1.0F - upside)) + (hp.canopy * upside)) * panels;
  }

  // The belly: a painted disc (concentric rings * spinning spokes), the
  // central flashlight hub, a ring of amber rim lights, and a propulsion glow
  // that swells with motion.
  if (underside > 0.001F) {
    const float rings = 0.5F + (0.5F * cosf(rr * hp.ring_frequency));
    const float spokes = 0.5F + (0.5F * cosf(ang * hp.spoke_frequency));
    albedo = albedo * (hp.paint_base + (hp.paint_range * rings * spokes));

    const float hub = __saturatef((hp.hub_radius - rr) / hp.hub_softness);
    emissive = emissive + (hp.hub_color * (hp.hub_strength * hub));

    const float ring = expf(-powf((rr - hp.rim_center) * hp.rim_width, 2.0F));
    const float dots =
        powf(0.5F + (0.5F * cosf(ang * hp.rim_dot_frequency)), 8.0F);
    emissive = emissive + (hp.rim_color * (hp.rim_strength * ring * dots));

    // Propulsion: a blue-white engine wash over the belly, strongest at the
    // rim, that brightens as the saucer moves or zooms.
    const float jets = hp.jet_base + (hp.jet_slope * ring);
    emissive = emissive +
               (hp.thrust_color * (head.thrust * hp.thrust_strength * jets));

    emissive = emissive * underside;
  }

  const vec3 half_v = normalize(light_dir - ray_dir);
  const float spec_power =
      (upside > underside) ? hp.dome_specular_power : hp.belly_specular_power;
  const float spec = powf(fmaxf(dot(normal, half_v), 0.0F), spec_power);
  return (albedo * (hp.ambient + (hp.sun * diffuse))) +
         (vec3{1.0F, 1.0F, 1.0F} * (spec * hp.specular_strength)) + emissive;
}

// March one reflection ray through the scene the ball mirrors and return its
// linear color: the nearer of the terrain and the saucer head, or the sky on a
// miss. The head is present here even though it is absent from primary rays,
// so looking at the ball shows the saucer reflected overhead. One bounce: the
// ball does not reflect itself.
[[nodiscard]] __device__ inline vec3
shade_scene_ray(const density_field& field, cudaTextureObject_t color,
    const saucer_head& head, const render_config& cfg, pos3 eye,
    vec3 ray_dir) {
  const float t_terrain = field.raymarch(eye, ray_dir);
  const float t_head = head.raymarch(eye, ray_dir);
  const bool head_nearer =
      t_head >= 0.0F && (t_terrain < 0.0F || t_head < t_terrain);
  if (head_nearer)
    return shade_head(head, cfg, eye + (ray_dir * t_head), ray_dir);
  if (t_terrain < 0.0F) return sky_color(cfg, ray_dir);
  return shade_terrain_hit(field, color, cfg, eye + (ray_dir * t_terrain));
}

// Shade the metallic ball at surface point `hit_point`: bounce one reflection
// ray into the scene (terrain, the saucer head, sky), then dim and tint it so
// the ball reads as dark liquid metal rather than a blown-out chrome mirror,
// with a small ambient floor so the darkest reflections do not crush to black.
[[nodiscard]] __device__ inline vec3 shade_ball(const density_field& field,
    cudaTextureObject_t color, const metal_ball& ball, const saucer_head& head,
    const render_config& cfg, pos3 hit_point, vec3 ray_dir) {
  const vec3 normal = ball.normal(hit_point);
  const vec3 env = shade_scene_ray(field, color, head, cfg, hit_point,
      reflect(ray_dir, normal));
  return (env * cfg.ball.dim * cfg.ball.tint) + cfg.ball.ambient_floor;
}

// Composite the primary ray: whichever of the ball and the terrain is nearer,
// or the sky if it escapes both. The saucer head is not tested here: the
// camera rides inside it, so it never appears in the view itself, only in the
// ball's reflection (which `shade_ball` casts).
[[nodiscard]] __device__ inline vec3
shade_primary_ray(const density_field& field, cudaTextureObject_t color,
    const metal_ball& ball, const saucer_head& head, const render_config& cfg,
    pos3 eye, vec3 ray_dir) {
  const float t_terrain = field.raymarch(eye, ray_dir);
  const float t_ball = ball.intersect(eye, ray_dir);
  const bool ball_nearer =
      t_ball >= 0.0F && (t_terrain < 0.0F || t_ball < t_terrain);
  if (ball_nearer)
    return shade_ball(field, color, ball, head, cfg, eye + (ray_dir * t_ball),
        ray_dir);
  if (t_terrain < 0.0F) return sky_color(cfg, ray_dir);
  return shade_terrain_hit(field, color, cfg, eye + (ray_dir * t_terrain));
}

#pragma endregion

} // namespace corvid::cuda
