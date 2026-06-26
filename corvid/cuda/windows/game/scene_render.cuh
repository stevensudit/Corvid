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
  const float glow = 0.4F + (0.9F * (facing * facing)) + (0.6F * rim);

  // The two-tone beacon. While moving the color is direction-driven: the
  // tip-color running light, reddening toward the alt color as the Head backs
  // up. At rest it eases to the belly idle alternation (`idle_mix`), so the
  // beacon alternates the two colors in tune with the idle belly spin;
  // `motion` blends between the two regimes. The on/off portion dims the
  // beacon toward dark on the blink wave by `blink_depth`, gated by `motion`
  // so the resting alternation is a smooth color change with no pulsing.
  const auto mix = [](vec3 a, vec3 b, float t) { return a + ((b - a) * t); };
  const float select =
      (head.idle_mix * (1.0F - head.motion)) + (head.reversing * head.motion);
  const vec3 color = mix(hp.antenna_tip_color, hp.antenna_alt_color, select);
  const float on = 1.0F - (hp.blink_depth * head.motion * (1.0F - head.blink));
  out = color * (glow * on);
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

  // Classify the hit as the dome sphere or the disc body, by which of the two
  // unioned components is nearer. The dome decal (canopy, grid, eye, seam) is
  // drawn only on the sphere, tiled over the whole of it, so a dome rotation
  // can never bare it or slide the eye onto the disc; the disc occludes the
  // sphere's lower part for free.
  const saucer_head::parts sp = head.parts_at(hit_point);
  const bool is_dome = sp.dome < sp.disc;

  // The dome takes its own cap albedo, the saucer body its own elsewhere.
  vec3 albedo = is_dome ? hp.dome_albedo : hp.base_albedo;
  vec3 emissive{0.0F, 0.0F, 0.0F};
  float eye_cover = 0.0F; // how much an eye decal covers this point

  // The dome: a fixed cockpit, mechanical but calmer than the belly. A
  // canopy-tinted metal shell tiled with a geodesic hex grid over its whole
  // sphere, carrying a single hexagonal porthole eye on its front: an opaque
  // iris with a pupil hub and radial spokes inside a hex frame. The eye is
  // placed in the dome's own (counter-rotated) frame, so it holds still on the
  // dome and can never leave it. Its colors render as emissive (and the lit
  // metal under it removed), so a set color reads true rather than darkening
  // to gray; the edges stay crisp so it holds up small.
  if (is_dome) {
    albedo = (albedo * (1.0F - upside)) + (hp.canopy * upside);
    constexpr float aa = 0.012F;

    // A hard black groove where the dome meets the disc: a band the dome
    // surface enters as it nears the disc (`sp.disc - sp.dome` shrinking to
    // zero at the join), pulled `seam_offset` in from the very edge and
    // `seam_width` thick. Drawn like the eye, emissive with the lit metal and
    // specular removed (via `eye_cover`), so it reads true black, a groove the
    // dome sits in.
    const float seam_t = (sp.disc - sp.dome) / head.radius;
    const float seam_d = seam_t - hp.seam_offset;
    const float band =
        __saturatef(seam_d / aa) * __saturatef((hp.seam_width - seam_d) / aa);
    albedo = albedo * (1.0F - band);
    emissive = (emissive * (1.0F - band)) + (hp.seam_color * band);
    eye_cover = fmaxf(eye_cover, band);

    // The eye and grid ride `dome_up`, the dome's own frame (it counter-
    // rotates against the disc's motion bank). `c` is the eye center, passed
    // from the host so the antenna shares its exact angle; the geodesic cell
    // the eye nests on sizes the iris (its apothem) and, via its flat-top
    // phase plus `dome_hex_phase`, rotates the grid about the eye.
    const vec3 e_up = head.dome_up;
    const vec3 c = head.eye_dir;
    const geodesic_eye_cell eye_cell = geodesic_eye_cell_of(hp.dome_hex_freq);
    const vec3 th = normalize(cross(e_up, c));
    const vec3 tv = cross(c, th);
    const float grid_phase = eye_cell.phase + hp.dome_hex_phase;
    const vec3 eye_tan = (th * cosf(grid_phase)) + (tv * sinf(grid_phase));

    // Locate the point by its direction from the dome's center, unique on the
    // sphere (a normal-placed decal would stamp twice, on the dome and on the
    // body, which repeat normals).
    const pos3 dome_c =
        head.center + (e_up * (head.radius * head.dome_offset));
    const vec3 dd = normalize(hit_point - dome_c);

    // The geodesic (Goldberg) hex grid over the whole dome sphere: a
    // frequency-`dome_hex_freq` icosahedron reoriented so one cell sits on the
    // eye, the cell's flat-top phase lining its hexagons up with the iris.
    const float edge = geodesic_grid_edge(dd, hp.dome_hex_freq, c, eye_tan);
    const float facet = __saturatef((hp.dome_hex_line - edge) / aa);
    albedo = albedo * (1.0F - (facet * hp.dome_hex_strength));

    // The eye, on the front of the dome (toward the eye center `c`): an opaque
    // glass iris with a pupil hub and radial spokes at its center, ringed by a
    // bright hex frame, drawn over the grid. Its apothem is three
    // cell-apothems (two cells from center to flat) minus the frame
    // half-width, so the frame's outer edge, not its centerline, lands flush
    // on the ring-cell seam instead of overrunning onto the neighbors.
    if (dot(dd, c) > 0.0F) {
      // The iris shares the grid's roll: rotate its tangent frame by
      // `dome_hex_phase` so it tracks the cells. The grid carries the phase
      // through `eye_tan`, so without this the iris lags the cells whenever
      // the phase is nonzero.
      const float cd = cosf(hp.dome_hex_phase);
      const float sd = sinf(hp.dome_hex_phase);
      const vec3 iris_th = (th * cd) + (tv * sd);
      const vec3 iris_tv = (tv * cd) - (th * sd);
      const float ex = dot(dd, iris_th);
      const float ey = dot(dd, iris_tv);
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

  // The saucer's upper cone, dressed to a classic hull: a fixed ring of dark
  // portholes and radial panel grooves. Both ride the saucer's local frame
  // (no belly spin), and the hull is radially symmetric, so their fixed angle
  // is invisible under the bank. Drawn on the up-facing disc only, never the
  // dome (which carries its own grid) nor the belly.
  if (!is_dome && upside > 0.001F) {
    constexpr float aa = 0.01F;
    constexpr float two_pi = 6.2831853F;
    // Azimuth bolted to the hull's `front`, not `to_local`'s world-up frame,
    // so these fixed decals hold still as the disc banks under dolly motion.
    // Each decal subtracts its own phase to rotate where it rings the hull.
    const float top_ang = head.hull_azimuth(hit_point);

    // Dark portholes evenly spaced on a ring, each centered in its panel: snap
    // to the nearest ring slot, then test the in-plane distance to it. `rr` is
    // frame-independent, so reconstruct the in-plane coordinates from it and
    // the phased azimuth.
    if (hp.port_count > 0) {
      const float pa = top_ang - hp.port_phase;
      const float lx = rr * cosf(pa);
      const float lz = rr * sinf(pa);
      const float sector = two_pi / static_cast<float>(hp.port_count);
      const float a0 = (rintf((pa / sector) - 0.5F) + 0.5F) * sector;
      const float dx = lx - (hp.port_center * cosf(a0));
      const float dz = lz - (hp.port_center * sinf(a0));
      const float pd = sqrtf((dx * dx) + (dz * dz));
      const float port = __saturatef((hp.port_radius - pd) / aa);
      albedo = (albedo * (1.0F - port)) + (hp.port_color * port);
    }

    // Radial panel grooves: thin darkened seams at evenly spaced angles, the
    // arc scaled by `rr` so each holds a constant width out to the rim. They
    // stop at `rim_top` (fading out below it) so they cover only the flat top
    // and leave the rounded shoulder to the rim running lights.
    if (hp.panel_count > 0) {
      const auto panels = static_cast<float>(hp.panel_count);
      const float phase = ((top_ang - hp.panel_phase) * panels) / two_pi;
      const float to_seam = fabsf(phase - rintf(phase)) * (two_pi / panels);
      const float groove =
          __saturatef((hp.panel_line - (to_seam * rr)) / aa) *
          __saturatef((facing_up - hp.rim_top) / 0.05F);
      albedo = albedo * (1.0F - (groove * hp.panel_strength));
    }
  }

  // The belly: a painted disc (concentric rings * spinning spokes), the
  // central flashlight hub, and a ring of amber rim lights.
  if (underside > 0.001F) {
    const float rings = 0.5F + (0.5F * cosf(rr * hp.ring_paint_frequency));
    const float spokes = 0.5F + (0.5F * cosf(ang * hp.spoke_paint_frequency));
    albedo = albedo * (hp.paint_base + (hp.paint_range * rings * spokes));

    const float hub = __saturatef((hp.hub_radius - rr) / hp.hub_softness);
    emissive = emissive + (hp.hub_color * (hp.hub_strength * hub));

    const float ring =
        expf(-powf((rr - hp.spoke_center) * hp.spoke_width, 2.0F));
    const float dots =
        powf(0.5F + (0.5F * cosf(ang * hp.spoke_dot_frequency)), 8.0F);
    emissive = emissive + (hp.spoke_color * (hp.spoke_strength * ring * dots));

    emissive = emissive * underside;
  }

  // Rim running lights: a ring of emissive segments set into the rounded
  // shoulder where the top curves down to the brim (the darker edge above the
  // brim), crisp at the top of that shoulder and fading downward, so they read
  // as lights projecting down whose diffuse lower half still shows from above.
  // `facing_up` (the normal's vertical component) is ~1 on the flat top, 0 at
  // the brim boundary, negative on the belly: the band cuts hard just below
  // `rim_top` (the crisp top edge, set where the flat top ends) and fades over
  // `rim_width` down across the shoulder and brim onto the upper belly. The
  // panel grooves stop at the same `rim_top` so the rim light owns this edge.
  // Keying on the geometric normal (not the view angle) keeps a consistent
  // width and covers the gap a thin equator ring leaves under shallow views.
  // On the disc body only (not the dome). `seg` is the smooth cosine of the
  // azimuth, so the `rim_count` segments fade between dark and lit, scaled
  // into
  // [`rim_floor`, 1]; `rim_spin_scale` turns them slower than the belly
  // (subtracting part of `head.spin` from `ang`).
  if (!is_dome) {
    constexpr float aa = 0.03F; // top-edge crispness, in normal units
    const float f = facing_up - hp.rim_top;
    const float band =
        __saturatef(-f / aa) * __saturatef(1.0F + (f / hp.rim_width));
    const float rim_ang = ang - (head.spin * (1.0F - hp.rim_spin_scale));
    const float wave =
        0.5F + (0.5F * cosf(rim_ang * static_cast<float>(hp.rim_count)));
    const float seg = hp.rim_floor + ((1.0F - hp.rim_floor) * wave);
    emissive = emissive + (hp.rim_color * (hp.rim_strength * band * seg));
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
  if (cfg.debug_ball_raw) return env; // diagnose the black blob undimmed
  vec3 col = (env * cfg.ball.dim * cfg.ball.tint) + cfg.ball.ambient_floor;

  // The motion grid: an emissive flat hex wireframe wrapped onto the ball by
  // the rolling-conveyor projection (`grid_uv`), so it stays flat and flows at
  // the roll rate instead of wobbling like a whole-sphere geodesic grid. It
  // flares up only while moving (`glow`) and fades toward the roll-axis poles,
  // where the cells shrink: `grid_extent` is how far (in `axle`, the latitude
  // sine) the grid reaches before that fade. The coordinates are scaled by
  // `hex_freq` (cells per radian), so the line and aa widths convert back to
  // radians by the same factor. The grid's flat-top axis is `u` (the rolling
  // direction), so its edges run along `v`, parallel to the ground. Added over
  // the mirror as glowing lines.
  if (ball.glow > 0.001F) {
    constexpr float aa = 0.02F;
    constexpr float feather = 0.2F; // axle-fade softness
    const auto scale = static_cast<float>(cfg.ball.hex_freq);
    const metal_ball::grid_sample uv = ball.grid_uv(normal);
    const float fade = __saturatef((cfg.ball.grid_extent - uv.axle) / feather);
    const float edge = hex_grid_edge(uv.v * scale, uv.u * scale) / scale;
    const float line = __saturatef((cfg.ball.hex_line - edge) / aa);
    col = col + (cfg.ball.hex_color *
                    (line * fade * cfg.ball.hex_strength * ball.glow));
  }
  return col;
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
