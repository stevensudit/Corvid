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

// Shade an antenna hit: report whether `hit_point` lands on the dome's antenna
// and, if so, write its color to `out`. The ball tip is an emissive beacon;
// the rod is bare steel lit by `diffuse`.
[[nodiscard]] __device__ inline bool shade_antenna(const saucer_head& head,
    const render_config::head_params& hp, pos3 hit_point, vec3 ray_dir,
    float diffuse, vec3& out) {
  if (head.antenna_length <= 0.0F) return false;
  const vec3 pc = hit_point - head.center;
  const vec3 tip = head.antenna_tip();
  const float ball_d = sd_sphere(pc - tip, head.radius * head.antenna_ball);
  const float rod_d = sd_capsule(pc, head.antenna_base(), tip,
      head.radius * head.antenna_thickness);
  if (fminf(rod_d, ball_d) >= head.radius * head.antenna_collar) return false;
  if (ball_d > rod_d) {
    out = (hp.ambient + (hp.sun * diffuse)) * hp.base_albedo; // lit steel rod
    return true;
  }
  // The tip is the emissive green beacon, so only draw it where it is actually
  // the nearest surface. A tip that has tilted behind or into the saucer would
  // otherwise bleed its green onto the body skin in front of it, since the
  // collar test alone does not know which surface the hit belongs to.
  if (ball_d > head.saucer_sdf(hit_point)) return false;
  // The tip reads as a fast-spinning dish blurred into a beacon: an
  // axially-symmetric emissive glow about the antenna, with a hot core toward
  // the viewer and a bright band where the spinning rim sweeps.
  const vec3 n = normalize(pc - tip);
  const float facing = __saturatef(-dot(n, ray_dir));
  const float axial = dot(n, head.antenna_dir);
  const float rim = expf(-(axial * axial) * 8.0F);
  out = hp.antenna_tip_color *
        (0.4F + (0.9F * (facing * facing)) + (0.6F * rim));
  return true;
}

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

  // The antenna stands proud of the dome (its tip wags with the eye's gimbal),
  // so classify and shade it before the dome/belly split.
  vec3 antenna_color{};
  if (shade_antenna(head, hp, hit_point, ray_dir, diffuse, antenna_color))
    return antenna_color;

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
  // front: an opaque iris with a pupil hub and radial spokes at its center,
  // inside a hex frame. The eye is placed relative to the saucer's forward,
  // not the spinning belly frame, so the dome reads as an eye that holds
  // still. Its colors are rendered as emissive (and the lit metal under it is
  // removed), so a set color reads true rather than darkening to gray through
  // the lighting and the dimmed reflection; the edges stay crisp so it holds
  // up small.
  if (upside > 0.001F) {
    albedo = (albedo * (1.0F - upside)) + (hp.canopy * upside);

    // A hard black band masking the dome/cone joint: a solid top-hat with hard
    // edges, its inner edge pulled `seam_offset` inside the hex grid's
    // `dome_hex_extent` cutoff so it covers the specular-washed dome edge as
    // well as the joint, spanning `seam_width`. Drawn like the eye, emissive
    // with the lit metal and specular removed (via `eye_cover`), so the whole
    // band reads true black; a soft-edged or partial band would let the
    // specular leak back through wherever it is not fully opaque. Reads as a
    // groove the dome sits in.
    constexpr float seam_aa = 0.01F;
    const float band_d = rr - (hp.dome_hex_extent - hp.seam_offset);
    const float band =
        __saturatef(band_d / seam_aa) *
        __saturatef((hp.seam_width - band_d) / seam_aa);
    albedo = albedo * (1.0F - band);
    emissive = (emissive * (1.0F - band)) + (hp.seam_color * band);
    eye_cover = fmaxf(eye_cover, band);

    // The cockpit eye, leaned toward the front off the apex: a pupil hub and
    // radial spokes inside an opaque hexagonal iris that together read as an
    // eye. The iris is two cells across, its flats flush with the surrounding
    // ring's outer edge, so it nests cleanly in the dome grid. `aa` is a
    // narrow ramp that keeps the edges crisp without aliasing.
    constexpr float aa = 0.012F;
    const vec3 e_up = head.up;
    // The eye's placement direction is computed once on the host (so the
    // antenna can share its exact angle) and passed in; see
    // `camera_rig::head`.
    const vec3 c = head.eye_dir;

    // The geodesic cell the eye nests on, fixing the iris size and the grid
    // orientation together: its apothem sizes the iris, and its phase rotates
    // the grid about the eye so the cells sit flat-top under the iris.
    // `dome_hex_phase` adds a manual nudge on top.
    const geodesic_eye_cell eye_cell = geodesic_eye_cell_of(hp.dome_hex_freq);

    // Tangent frame at the eye center, shared by the grid and the iris so the
    // two stay concentric.
    const vec3 th = normalize(cross(e_up, c));
    const vec3 tv = cross(c, th);
    const float grid_phase = eye_cell.phase + hp.dome_hex_phase;
    const vec3 eye_tan = (th * cosf(grid_phase)) + (tv * sinf(grid_phase));

    // Locate the point by its direction from the dome's center, a position
    // that is unique on the dome, rather than by its surface normal: the
    // flattened body repeats the dome's up-and-forward normals, so a
    // normal-placed decal would stamp twice (once on the dome, once on the
    // body).
    const pos3 dome_c =
        head.center + (e_up * (head.radius * head.dome_offset));
    const vec3 dd = normalize(hit_point - dome_c);

    // A geodesic (Goldberg) hex grid over the whole dome cap: a
    // frequency-`dome_hex_freq` icosahedron reoriented so one cell sits on the
    // eye, with the cell's own flat-top phase (plus the `dome_hex_phase`
    // nudge) rotating the grid about the eye to line its hexagons up with the
    // iris. Read from `dd` in the head frame so it holds still, and only
    // inside the sharp cutoff so the cone skips the icosahedron search.
    const float grid_fade = __saturatef((hp.dome_hex_extent - rr) / aa);
    if (grid_fade > 0.0F) {
      const float edge = geodesic_grid_edge(dd, hp.dome_hex_freq, c, eye_tan);
      const float facet = __saturatef((hp.dome_hex_line - edge) / aa);
      albedo = albedo * (1.0F - (facet * grid_fade * hp.dome_hex_strength));
    }

    // The eye, on the front of the dome (toward the eye center `c`): an opaque
    // glass iris with a pupil hub and radial spokes at its center, ringed by a
    // bright hex frame, drawn over the grid. Its apothem is three
    // cell-apothems (two cells from center to flat) minus the frame
    // half-width, so the frame's outer edge, not its centerline, lands flush
    // on the ring-cell seam instead of overrunning onto the neighbors.
    if (dot(dd, c) > 0.0F) {
      const float ex = dot(dd, th);
      const float ey = dot(dd, tv);
      const float hexd =
          hexagon_sd(ex, ey, (3.0F * eye_cell.apothem) - hp.eye_line);
      if (hexd <= hp.eye_line + aa) { // inside the iris
        const float er = sqrtf((ex * ex) + (ey * ey));
        const float inside = __saturatef(-hexd / aa);
        const float frame = __saturatef((hp.eye_line - fabsf(hexd)) / aa);
        const float hub =
            __saturatef((hp.eye_line - fabsf(er - hp.eye_hub)) / aa);
        const float pupil = inside * __saturatef((hp.eye_hub - er) / aa);

        // Radial spokes from the hub out to the frame. At `eye_spokes` = 6
        // they run to the hexagon's vertices; halving to 3 leaves alternating
        // spokes, an animation hook.
        constexpr float two_pi = 6.2831853F;
        const auto spokes = static_cast<float>(hp.eye_spokes);
        float spoke = 0.0F;
        if (hp.eye_spokes > 0 && er > hp.eye_hub) {
          const float spoke_phase = (atan2f(ey, ex) * spokes) / two_pi;
          const float to_spoke =
              fabsf(spoke_phase - rintf(spoke_phase)) * (two_pi / spokes);
          spoke = __saturatef((hp.eye_line - (er * sinf(to_spoke))) / aa);
        }

        // Opaque glass iris, then the pupil hub, then the bright frame,
        // spokes, and hub ring over it.
        vec3 col = hp.eye_glass;
        col = (col * (1.0F - pupil)) + (hp.eye_pupil * pupil);
        const float structure = fmaxf(frame, inside * fmaxf(hub, spoke));
        col = (col * (1.0F - structure)) + (hp.eye_frame_color * structure);

        // The eye reads at its set color (white stays white): drop the lit
        // metal under it and add the color as emissive rather than albedo.
        const float cov = fmaxf(inside, frame);
        albedo = albedo * (1.0F - cov);
        emissive = (emissive * (1.0F - cov)) + (col * cov);
        eye_cover = fmaxf(eye_cover, cov);
      }
    }
  }

  // The belly: a painted disc (concentric rings * spinning spokes), the
  // central flashlight hub, and a ring of amber rim lights.
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
// flat mirror, or the sky if it escapes them all. The saucer head is tested
// only when `cfg.show_head` is set (the observer freeze): normally the camera
// rides inside it, so it appears only reflected in the ball (`shade_ball`) or
// the mirror (`shade_world_ray`), but the freeze pins the camera outside it
// and reveals it here.
[[nodiscard]] __device__ inline vec3
shade_primary_ray(const density_field& field, cudaTextureObject_t color,
    const metal_ball& ball, const saucer_head& head, const flat_mirror& mirror,
    const render_config& cfg, pos3 eye, vec3 ray_dir) {
  const float t_terrain = field.raymarch(eye, ray_dir);
  const float t_ball = ball.intersect(eye, ray_dir);
  const float t_mirror = mirror.intersect(eye, ray_dir);
  const float t_head = cfg.show_head ? head.raymarch(eye, ray_dir) : -1.0F;
  float best = 1.0e30F;
  int kind = 0; // 0 sky, 1 terrain, 2 ball, 3 mirror, 4 head
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
  if (t_head >= 0.0F && t_head < best) {
    best = t_head;
    kind = 4;
  }
  if (kind == 2)
    return shade_ball(field, color, ball, head, cfg, eye + (ray_dir * best),
        ray_dir);
  if (kind == 3) {
    const pos3 hit = eye + (ray_dir * best);
    const vec3 refl = reflect(ray_dir, mirror.normal);
    return shade_world_ray(field, color, ball, head, cfg, hit, refl) * 0.9F;
  }
  if (kind == 4) return shade_head(head, cfg, eye + (ray_dir * best), ray_dir);
  if (kind == 1)
    return shade_terrain_hit(field, color, cfg, eye + (ray_dir * best));
  return sky_color(cfg, ray_dir);
}

#pragma endregion

} // namespace corvid::cuda
