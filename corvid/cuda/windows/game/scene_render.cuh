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

#include "../../density_field.cuh"
#include "../../mirror.cuh"
#include "../../vec.cuh"
#include "./avatar.cuh"
#include "./render_config.cuh"
#include "./voxel_render.cuh"

// The voxel viewer's game scene compositing: the player's avatar (the metallic
// ball and the saucer head) and the flat mirror, composited per ray against
// the terrain and sky from voxel_render.cuh. `shade_primary_ray` is the kernel
// entry point. The ball and the mirror each cast a one-bounce reflection ray
// that reveals the head, which the camera rides inside and so never appears in
// the view directly. Game-specific, so it lives here rather than in the
// generic voxel renderer.

namespace corvid::cuda {

#pragma region Avatar and scene compositing

// Shade the saucer head at surface point `hit_point`: a fixed cockpit dome
// carrying a single hexagonal porthole eye on its front, and a distinctive
// spinning belly on the underside, painted with rings and spokes and carrying
// the central flashlight and a ring of amber rim lights. The camera rides
// inside the head, so this is only ever reached by the ball's reflection ray,
// never by a primary ray.
[[nodiscard]] __device__ inline vec3 shade_head(const saucer_head& head,
    const render_config& cfg, pos3 hit_point, vec3 ray_dir) {
  const render_config::head_params& hp = cfg.head;

  // Snap the marched hit onto the true surface before deriving anything from
  // its position.
  //
  // The raymarch accepts a hit within a distance-growing tolerance (to close
  // the dome/disc seam), so the returned point sits off the true ray crossing
  // by an amount that varies pixel to pixel; that slack jitters the
  // dome-direction `dd` that places the eye and staircases its thin frame and
  // spokes (the dome shading, taken from the gradient, is insensitive and
  // stays smooth).
  //
  // A Newton step along the ray converges to the true crossing, fixing the
  // tangential slide a normal projection leaves when the eye is viewed
  // grazing. The denominator's magnitude is floored (a front-facing hit has
  // `ndotd < 0`) so the step stays bounded and continuous as the ray nears
  // tangent: an unbounded grazing step, or a hard branch to the normal
  // projection, slides the point across the surface and echoes the decal just
  // outside itself at angles. A tiny residual at the extreme grazing sliver is
  // invisible next to that echo.
  vec3 normal = head.normal(hit_point);
  const float ndotd = dot(normal, ray_dir);
  hit_point =
      hit_point - (ray_dir * (head.sdf(hit_point) / fminf(ndotd, -0.4F)));
  normal = head.normal(hit_point);

  const vec3 light_dir = normalize(cfg.sun_direction);
  const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);

  const float facing_up = dot(normal, head.up);
  const float upside = fmaxf(facing_up, 0.0F);     // the dome
  const float underside = fmaxf(-facing_up, 0.0F); // the belly

  const vec3 ql = head.to_local(hit_point);
  const float rr = sqrtf((ql.x * ql.x) + (ql.z * ql.z)) / head.radius;
  const float ang = atan2f(ql.z, ql.x) + head.spin;

  // The dome cap takes its own base albedo, fully replacing `base_albedo` out
  // to the hex grid's `rr` boundary, then fading to it just past, onto the
  // cone, so the dome can be darkened to pop without dimming the saucer body.
  const float dome_cap =
      (facing_up > 0.0F)
          ? __saturatef((hp.dome_hex_extent + 0.08F - rr) / 0.08F)
          : 0.0F;
  vec3 albedo =
      (hp.base_albedo * (1.0F - dome_cap)) + (hp.dome_albedo * dome_cap);
  vec3 emissive{0.0F, 0.0F, 0.0F};
  float eye_cover = 0.0F; // how much an eye decal covers this point

  // The dome: a fixed cockpit, mechanical but calmer than the belly. A
  // canopy-tinted metal shell carrying a single hexagonal porthole eye on its
  // front: an iris glass, a pupil hub at its center, and radial spokes inside
  // a hex frame. The eye is placed relative to the saucer's forward, not the
  // spinning belly frame, so the dome reads as an eye that holds still. Its
  // colors are rendered as emissive (and the lit metal under it is removed),
  // so a set color reads true rather than darkening to gray through the
  // lighting and the dimmed reflection; the edges stay crisp so it holds up
  // small.
  if (upside > 0.001F) {
    albedo = (albedo * (1.0F - upside)) + (hp.canopy * upside);

    // Two seam grooves on the conical top, keyed to `rr` and fixed in the
    // front frame. The inner one butts the dome's hex cutoff: a hard inner
    // edge at `seam_inner` so it does not leak back onto the dome, fading out
    // softly over `seam_inner_width` onto the cone. The outer one is a
    // symmetric ring near the rim to emphasize the brim.
    constexpr float seam_aa = 0.01F;
    const float in_d = rr - hp.seam_inner;
    const float seam_in =
        __saturatef(in_d / seam_aa) *
        __saturatef(1.0F - (in_d / hp.seam_inner_width));
    const float seam_out = __saturatef(
        (hp.seam_outer_width - fabsf(rr - hp.seam_outer)) / seam_aa);
    const float seam = fmaxf(seam_in, seam_out) * hp.seam_strength;
    albedo = (albedo * (1.0F - seam)) + (hp.seam_color * seam);

    // The single cockpit eye, leaned toward the front off the apex: a
    // hexagonal porthole stamped in the tangent plane at its center, a glass
    // iris under a bright hex frame, a central hub ring, and radial spokes.
    // `aa` is a narrow ramp that keeps the edges crisp without aliasing.
    constexpr float aa = 0.012F;
    constexpr float two_pi = 6.2831853F;
    const auto spokes = static_cast<float>(hp.eye_spokes);
    const vec3 e_up = head.up;
    const vec3 e_fwd = normalize(head.front - (e_up * dot(head.front, e_up)));
    const vec3 c = normalize(e_up + (e_fwd * hp.eye_forward));

    // Locate the point by its direction from the dome's center, a position
    // that is unique on the dome, rather than by its surface normal: the
    // flattened body repeats the dome's up-and-forward normals, so a
    // normal-placed decal would stamp twice (once on the dome, once on the
    // body).
    const pos3 dome_c =
        head.center + (e_up * (head.radius * head.dome_offset));
    const vec3 dd = normalize(hit_point - dome_c);

    // A geodesic (Goldberg) hex grid over the whole dome cap: a frequency-
    // `dome_hex_freq` icosahedron reoriented so one cell sits on the eye, with
    // `dome_hex_phase` rotating the grid about the eye to line its hexagons up
    // with the porthole. Read from `dd` in the head frame so it holds still,
    // and only inside the sharp cutoff so the cone skips the icosahedron
    // search.
    const float grid_fade = __saturatef((hp.dome_hex_extent - rr) / aa);
    if (grid_fade > 0.0F) {
      const vec3 th_eye = normalize(cross(e_up, c));
      const vec3 tv_eye = cross(c, th_eye);
      const vec3 eye_tan =
          (th_eye * cosf(hp.dome_hex_phase)) +
          (tv_eye * sinf(hp.dome_hex_phase));
      const float edge = geodesic_grid_edge(dd, hp.dome_hex_freq, c, eye_tan);
      const float grid =
          __saturatef((hp.dome_hex_line - edge) / aa) * grid_fade *
          hp.dome_hex_strength;
      albedo = albedo * (1.0F - grid);
    }

    // The single cockpit eye, on the front of the dome (toward the eye center
    // `c`): a hexagonal porthole stamped in the tangent plane, drawn over the
    // grid.
    if (dot(dd, c) > 0.0F) {
      const vec3 th = normalize(cross(e_up, c));
      const vec3 tv = cross(c, th);
      const float ex = dot(dd, th);
      const float ey = dot(dd, tv);
      const float hexd = hexagon_sd(ex, ey, hp.eye_size);
      if (hexd <= hp.eye_line + aa) { // inside the porthole
        const float er = sqrtf((ex * ex) + (ey * ey));
        const float ea = atan2f(ey, ex);
        const float phase = (ea * spokes) / two_pi;
        const float to_spoke = fabsf(phase - rintf(phase)) * (two_pi / spokes);
        const float spoke_perp = er * sinf(to_spoke);

        const float inside = __saturatef(-hexd / aa);
        const float rim = __saturatef((hp.eye_line - fabsf(hexd)) / aa);
        const float hub =
            __saturatef((hp.eye_line - fabsf(er - hp.eye_hub)) / aa);
        const float spoke =
            (er > hp.eye_hub)
                ? __saturatef((hp.eye_line - spoke_perp) / aa)
                : 0.0F;
        const float structure = fmaxf(rim, inside * fmaxf(hub, spoke));
        const float pupil = inside * __saturatef((hp.eye_hub - er) / aa);

        // Build the eye color: iris, then the pupil center, then the bright
        // frame and spokes over both.
        vec3 col = hp.eye_glass;
        col = (col * (1.0F - pupil)) + (hp.eye_pupil * pupil);
        col = (col * (1.0F - structure)) + (hp.eye_frame_color * structure);

        // The eye reads at its set color (white stays white): drop the lit
        // metal under it and add the color as emissive rather than albedo.
        const float cov = fmaxf(inside, rim);
        albedo = albedo * (1.0F - cov);
        emissive = (emissive * (1.0F - cov)) + (col * cov);
        eye_cover = fmaxf(eye_cover, cov);
      }
    }
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
  const float spec =
      powf(fmaxf(dot(normal, half_v), 0.0F), spec_power) * (1.0F - eye_cover);
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

// March one ray through the full world the flat mirror reflects: the nearest
// of the saucer head, the ball, and the terrain, or the sky on a miss. Unlike
// a primary ray this includes the head, since the mirror shows the avatar the
// camera cannot see directly. One bounce, like the ball's reflection: this
// does not test the mirror again, so there is no mirror-in-mirror recursion.
[[nodiscard]] __device__ inline vec3
shade_world_ray(const density_field& field, cudaTextureObject_t color,
    const metal_ball& ball, const saucer_head& head, const render_config& cfg,
    pos3 eye, vec3 ray_dir) {
  const float t_terrain = field.raymarch(eye, ray_dir);
  const float t_ball = ball.intersect(eye, ray_dir);
  const float t_head = head.raymarch(eye, ray_dir);
  float best = 1.0e30F;
  int kind = 0; // 0 sky, 1 terrain, 2 ball, 3 head
  if (t_terrain >= 0.0F && t_terrain < best) {
    best = t_terrain;
    kind = 1;
  }
  if (t_ball >= 0.0F && t_ball < best) {
    best = t_ball;
    kind = 2;
  }
  if (t_head >= 0.0F && t_head < best) {
    best = t_head;
    kind = 3;
  }
  if (kind == 1)
    return shade_terrain_hit(field, color, cfg, eye + (ray_dir * best));
  if (kind == 2)
    return shade_ball(field, color, ball, head, cfg, eye + (ray_dir * best),
        ray_dir);
  if (kind == 3) return shade_head(head, cfg, eye + (ray_dir * best), ray_dir);
  return sky_color(cfg, ray_dir);
}

// Composite the primary ray: the nearest of the ball, the terrain, and the
// flat mirror, or the sky if it escapes them all. The saucer head is not
// tested here: the camera rides inside it, so it never appears in the view
// itself, only reflected in the ball (`shade_ball`) or the mirror
// (`shade_world_ray`).
[[nodiscard]] __device__ inline vec3
shade_primary_ray(const density_field& field, cudaTextureObject_t color,
    const metal_ball& ball, const saucer_head& head, const flat_mirror& mirror,
    const render_config& cfg, pos3 eye, vec3 ray_dir) {
  const float t_terrain = field.raymarch(eye, ray_dir);
  const float t_ball = ball.intersect(eye, ray_dir);
  const float t_mirror = mirror.intersect(eye, ray_dir);
  float best = 1.0e30F;
  int kind = 0; // 0 sky, 1 terrain, 2 ball, 3 mirror
  if (t_terrain >= 0.0F && t_terrain < best) {
    best = t_terrain;
    kind = 1;
  }
  if (t_ball >= 0.0F && t_ball < best) {
    best = t_ball;
    kind = 2;
  }
  if (t_mirror >= 0.0F && t_mirror < best) {
    best = t_mirror;
    kind = 3;
  }
  if (kind == 2)
    return shade_ball(field, color, ball, head, cfg, eye + (ray_dir * best),
        ray_dir);
  if (kind == 3) {
    const pos3 hit = eye + (ray_dir * best);
    const vec3 refl = reflect(ray_dir, mirror.normal);
    return shade_world_ray(field, color, ball, head, cfg, hit, refl) * 0.9F;
  }
  if (kind == 1)
    return shade_terrain_hit(field, color, cfg, eye + (ray_dir * best));
  return sky_color(cfg, ray_dir);
}

#pragma endregion

} // namespace corvid::cuda
