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

#include "../../../math/arithmetic.h"
#include "../../density_field.cuh"
#include "../../mirror.cuh"
#include "../../vec.cuh"
#include "./avatar.cuh"
#include "./render_config.cuh"
#include "./voxel_render.cuh"

// The voxel viewer's game scene compositing: the player's avatar (the metallic
// ball and the saucer head) and the flat mirror, composited per ray against
// the terrain and sky from voxel_render.cuh. `shade_primary_ray` is the
// per-ray entry the render kernels call. The ball and the mirror each cast a
// one-bounce reflection ray that reveals the head, which the camera rides
// inside and so never appears in the view directly. Game-specific, so it lives
// here rather than in the generic voxel renderer.

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
  // Night gates the sun (diffuse and specular) off and dims the ambient, so
  // the head goes dark except its emissives and the flashlight source, instead
  // of staying sun-lit in the mirror and the ball's reflection. The sun gate
  // is baked into `diffuse`, so every consumer (the antenna rod, the hull)
  // follows.
  const float sun_on = cfg.night ? 0.0F : 1.0F;
  const float ambient_scale = cfg.night ? 0.1F : 1.0F;
  const float diffuse = fmaxf(dot(normal, light_dir), 0.0F) * sun_on;

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
        const auto spokes = static_cast<float>(hp.eye_spokes);
        float spoke = 0.0F;
        if (hp.eye_spokes > 0 && er > hp.eye_hub) {
          const float spoke_phase = (atan2f(ey, ex) * spokes) / two_pi_v<>;
          const float to_spoke =
              fabsf(spoke_phase - rintf(spoke_phase)) * (two_pi_v<> / spokes);
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

        // The pupil charges up while the dig tool is projecting, so the ball
        // reflection shows the eye as the beam source. Gated branchlessly on
        // the uniform `enabled` flag to add no divergence.
        const float glow =
            hp.eye_glow_strength * static_cast<float>(cfg.reticle.enabled);
        emissive = emissive + (hp.eye_glow_color * (glow * pupil));

        // The iris glass segments are the flashlight's emitter: the cells
        // between the spokes light up into a ring around the pupil while the
        // headlamp is on, so the head reads as the source and the ball's
        // reflection of it becomes the glint. The frame and spokes stay as
        // dark dividers (`1 - structure`) and the pupil stays the reticle's
        // (`1 - pupil`); a solid lit ring reads and reflects far better than
        // bright wires.
        const float segment = inside * (1.0F - pupil) * (1.0F - structure);
        const float lamp =
            cfg.flashlight.source_strength *
            static_cast<float>(cfg.flashlight.enabled);
        emissive = emissive + (cfg.flashlight.color * (lamp * segment));
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
      const float sector = two_pi_v<> / static_cast<float>(hp.port_count);
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
      const float phase = ((top_ang - hp.panel_phase) * panels) / two_pi_v<>;
      const float to_seam =
          fabsf(phase - rintf(phase)) * (two_pi_v<> / panels);
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

    const float ring_d = (rr - hp.spoke_center) * hp.spoke_width;
    const float ring = expf(-(ring_d * ring_d));
    const float wave = 0.5F + (0.5F * cosf(ang * hp.spoke_dot_frequency));
    const float wave2 = wave * wave;
    const float wave4 = wave2 * wave2;
    const float dots = wave4 * wave4; // wave^8
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
      powf(fmaxf(dot(normal, half_v), 0.0F), spec_power) * (1.0F - eye_cover) *
      sun_on;
  return (albedo * ((hp.ambient * ambient_scale) + (hp.sun * diffuse))) +
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

// The ball's motion-grid emissive at the surface point with outward `normal`.
//
// The rolling hex wireframe is wrapped on by the conveyor projection
// (`grid_uv`), motion-blurred along the roll so a fast scroll streaks instead
// of strobing, and faded toward the roll-axis poles where the cells shrink.
// Scaled by the grid's glow, so it is dark at rest and flares while moving.
// Shared by the outer mirror (`shade_ball`) and the inner porthole frame the
// merged glass view shows from inside the ball (`shade_merged_glass`).
//
// `hex_freq` (cells per radian) scales the coordinates, so the line and aa
// widths convert back to radians by the same factor. The flat-top axis is `u`
// (the rolling direction), so the cell edges run along `v`, parallel to the
// ground. The motion blur averages the line over the phase the grid sweeps
// this frame (`roll_blur`), clamped to one cell period past which the average
// is the steady cell mean (an even glow, not a flicker); the tap count scales
// with the sweep, uniform across the ball so there is no warp divergence.
[[nodiscard]] __device__ inline vec3 ball_grid_emissive(const metal_ball& ball,
    const render_config& cfg, vec3 normal) {
  if (ball.glow <= 0.001F) return vec3{};
  constexpr float aa = 0.02F;
  constexpr float feather = 0.2F; // axle-fade softness
  constexpr int max_taps = 8;
  const auto scale = static_cast<float>(cfg.ball.hex_freq);
  const metal_ball::grid_sample uv = ball.grid_uv(normal);
  const float fade = __saturatef((cfg.ball.grid_extent - uv.axle) / feather);
  const float period = 1.0F / scale; // grid period along the roll (u)
  const float sweep = fminf(ball.roll_blur, period);
  const int want = static_cast<int>(lroundf(sweep / aa));
  const int taps = want < 1 ? 1 : (want > max_taps ? max_taps : want);
  float line = 0.0F;
  for (int i = 0; i < taps; ++i) {
    const float s =
        sweep *
        (((static_cast<float>(i) + 0.5F) / static_cast<float>(taps)) - 0.5F);
    const float edge = hex_grid_edge(uv.v * scale, (uv.u + s) * scale) / scale;
    line += __saturatef((cfg.ball.hex_line - edge) / aa);
  }
  line /= static_cast<float>(taps);
  return cfg.ball.hex_color *
         (line * fade * cfg.ball.hex_strength * ball.glow);
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
  // the rolling-conveyor projection, flaring up only while moving, added over
  // the mirror as glowing lines (see `ball_grid_emissive`).
  col = col + ball_grid_emissive(ball, cfg, normal);

  // The flashlight's glossy highlight: a broad, bright view-facing lobe where
  // the beam strikes the ball, so the chrome catches the headlamp and blows
  // out through bloom, steady across poses. The emitter's reflection in `env`
  // above is the sharp sparkle on top of it.
  col = col + flashlight_gloss(cfg.flashlight, hit_point, normal, ray_dir);
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
  float best = big_value;
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
    return shade_terrain_hit(field, color, cfg, eye + (ray_dir * best),
        shadow_sphere{ball.center, ball.radius});
  if (kind == 2)
    return shade_ball(field, color, ball, head, cfg, eye + (ray_dir * best),
        ray_dir);
  if (kind == 3) return shade_head(head, cfg, eye + (ray_dir * best), ray_dir);
  return sky_color(cfg, ray_dir);
}

// Distance from a hexagon's center to its boundary at azimuth `angle`, for a
// hexagon of apothem `apothem` (center to edge midpoint). The boundary radius
// is `apothem / cos` of the angle folded into one 60-degree sector, running
// from the apothem at an edge midpoint to `apothem / cos(30)` at a vertex.
[[nodiscard]] __device__ inline float
hex_edge_radius(float angle, float apothem) {
  constexpr float sector = (60_deg).value;
  const float a = angle - (sector * rintf(angle / sector));
  return apothem / cosf(a);
}

// Lay the target reticle at the surface point `hit`: a laser-projected hexagon
// (a bold outer ring sized to the dig footprint and a fine inner crosshair hex
// with optional spokes, counter-rotating) marking the aim at `r.center`.
//
// The caller passes `hit` already snapped to the surface, the real terrain hit
// from outside the ball or a low-passed one from inside (see
// `apply_lensed_reticle`).
//
// Both rings use the pick point's true 3D distance as their radius, so they
// hug a dug bowl instead of smearing the way a flat tangent-plane decal does;
// the azimuth is measured against the camera's screen axes (`r.view_right` /
// `r.view_up`), so the roll holds steady where the surface normal would jump.
// Painted as additive glow on terrain, so the ball and head occlude it for
// free. The inner crosshair drops out (and the outer dims) when the ball
// blocks the aim (`show_inner`). A no-op when the tool is off (`enabled`).
//
// `aa_world` is the reticle's edge softness in world units, the screen
// footprint of one pixel at the hit, so the ring holds a steady ~1px edge
// instead of razor-sharp staircasing up close or sub-pixel line shimmer far
// off. The caller measures it for the ray that actually reaches the hit: a
// straight `eye`->`hit` estimate outside the ball, but the lensed footprint of
// the refracted ray once the eye has merged inside, where the straight
// estimate ignores the glass and the ring would otherwise staircase through
// it.
//
// `on_edge` reports whether this pixel carries any reticle coverage, so the
// adaptive-AA resolve pass can supersample it: the reticle's thin edges are
// painted on flat, same-depth terrain, so the geometry kind/depth test never
// sees them, and its own analytic edge softens only a fronto-parallel surface
// (it collapses to a hard, staircased edge where the ring grazes a curved cave
// wall). Letting the resolve fan those pixels out gives them true multisample
// edges at any viewing angle.
[[nodiscard]] __device__ inline vec3
apply_reticle(const render_config::reticle_params& r, pos3 hit, float aa_world,
    vec3 color, bool& on_edge) {
  on_edge = false;
  if (!r.enabled) return color;
  const vec3 d = hit - r.center;
  const float er = length(d); // 3D distance: the ring radius hugs the bowl

  // A regular hexagon's vertex sits at apothem / cos(30) = 2 / sqrt(3) times
  // its apothem; the rings are sized by apothem, so scale to reach a vertex.
  constexpr float apothem_to_vertex = 1.0F / cos_30_v<>;
  // Floor the antialiasing width so it stays strictly positive (the divisions
  // below would blow up at a zero footprint).
  constexpr float min_aa = 1.0e-4F;
  const float aa = fmaxf(aa_world, min_aa);
  const float oline = fmaxf(r.outer_line, aa); // outer ring, bold
  // Cheap reject beyond the outer hexagon's farthest reach (a vertex); the
  // caller already snapped `hit` to the surface, so `er` is final.
  if (er > (r.outer_radius * apothem_to_vertex) + oline + aa) return color;

  // Azimuth measured against the camera's screen axes, not a surface tangent
  // frame: the hit positions `d` stay continuous as the aim crosses a terrain
  // crease, but the surface normal does not, so keying the roll on the normal
  // snapped the pattern there. Screen-axis azimuth has no such discontinuity.
  // Each ring spins the opposite way (`spin` added for the outer, subtracted
  // for the inner).
  const float ang = atan2f(dot(d, r.view_up), dot(d, r.view_right));

  // The outer hexagon, clipped to a circle: fade it out past `outer_clip`
  // (times the apothem), cutting the hexagon's corners so it reads as a beam
  // projected through a round aperture (the eye), not a bare hexagon.
  float mask = __saturatef(
      (oline - fabsf(er - hex_edge_radius(ang + r.spin, r.outer_radius))) /
      aa);
  mask *= __saturatef(((r.outer_clip * r.outer_radius) - er) / aa);
  if (r.show_inner) {
    const float iline = fmaxf(r.inner_line, aa); // inner crosshair, fine
    const float ia = ang - r.spin;               // inner counter-rotates
    const float edge = hex_edge_radius(ia, r.inner_radius);
    mask = fmaxf(mask, __saturatef((iline - fabsf(er - edge)) / aa));

    // Crosshair spokes from the center to the inner hex: a thin radial line
    // wherever the azimuth lands near one of `inner_spokes` evenly spaced
    // spokes, clipped to the hexagon boundary in that direction (`edge`, not
    // the constant vertex reach) so the spoke stops flush with the hex instead
    // of poking out past its flat sides.
    if (r.inner_spokes > 0) {
      constexpr float two_pi = two_pi_v<float>;
      const auto spokes = static_cast<float>(r.inner_spokes);
      const float phase = (ia * spokes) / two_pi;
      const float to_spoke = fabsf(phase - rintf(phase)) * (two_pi / spokes);
      const float spoke =
          __saturatef((iline - (er * sinf(to_spoke))) / aa) *
          __saturatef((edge - er) / aa);
      mask = fmaxf(mask, spoke);
    }
  }

  // Any nonzero coverage marks this pixel for the resolve pass to supersample;
  // the reticle is mostly thin lines, so this is a small pixel count.
  constexpr float coverage_eps = 1.0e-3F;
  on_edge = mask > coverage_eps;

  // The outer ring holds full strength even when the inner crosshair is
  // dropped (ball-blocked or out of dig range): the half-dim no-dig cue
  // clashed with the eye glow, and the missing crosshair already reads as
  // no-dig.
  return color + (r.color * (mask * r.strength));
}

// A primary ray's shaded color plus the geometry the adaptive-AA pass keys on:
// the nearest-hit `kind` and its `depth` (the ray parameter at the hit, or
// `big_value` on a sky miss). The resolve pass compares these against the
// neighbors to find the silhouettes worth supersampling. `reticle_edge` forces
// that pixel to supersample regardless of the geometry, since the reticle's
// edges sit on flat same-kind terrain the kind/depth test cannot see. (`kind`:
// 0 sky, 1 terrain, 2 ball, 3 mirror, 4 head.)
struct ray_sample {
  vec3 color;
  float depth;
  int kind;
  bool reticle_edge;
};

// Unpolarized Fresnel reflectance at an interface, for incidence cosine `cosi`
// (>= 0, the angle to the normal on the incident side) and relative index
// `eta` = n_incident / n_transmitted. The average of the s- and p-polarized
// terms. Returns 1 on total internal reflection (`eta` > 1 past the critical
// angle), to which it rises smoothly, so blending reflection against
// refraction by this weight has no hard seam where the transmitted ray cuts
// out.
[[nodiscard]] __device__ inline float
fresnel_reflectance(float cosi, float eta) {
  const float sin_t2 = eta * eta * (1.0F - (cosi * cosi));
  if (sin_t2 >= 1.0F) return 1.0F; // total internal reflection
  const float cost = sqrtf(1.0F - sin_t2);
  const float rs = ((eta * cosi) - cost) / ((eta * cosi) + cost);
  const float rp = (cosi - (eta * cost)) / (cosi + (eta * cost));
  return 0.5F * ((rs * rs) + (rp * rp));
}

// Lay the dig reticle onto the lensed terrain the merged glass view shows,
// along the base refracted ray `dg` that left `exit` and struck terrain at
// distance `twg`. Returns `base` (the caller's color at this pixel) with the
// reticle's additive glow composited in, and sets `reticle_edge` where the
// ring has coverage so the adaptive-AA resolve supersamples it. Split out of
// `shade_merged_glass` to keep its branching in check.
//
// The reticle bends through the lens with the bowl it sits in, the same
// additive glow the outside view lays on a direct terrain hit. The centered
// aim exits at normal incidence (the eye rides the look axis), so it stays put
// on the pick point while the periphery bends.
//
// The ring radius comes from a smooth surface fitted to the terrain near the
// aim, not the per-pixel terrain hit: that hit rides the trilinear voxel
// facets, so its radius frizzes at voxel scale and crawls with any lens motion
// (the spinning ring then sweeps that fixed noise into a shimmer). The fit
// (`reticle_surface_fit`) is a tangent frame plus a quadric height, so the
// ring follows a tunnel or bowl while ignoring the bumps and degrades to a
// flat plane on open ground.
//
// The edge softness is the lensed footprint: the refracted bundle's spread
// perpendicular to the ray (finite-difference the hit for a one-pixel
// primary-ray step along two screen tangents, the wider wins, reusing `twg` so
// it costs two ball intersects and no march, carrying the lens magnification)
// divided by the surface-grazing cosine, since a ring raking a sloped bowl
// covers `1/cos` more surface per pixel. Clamped to the ring radius so a
// near-tangent rake blurs the ring away, and gated to the ring's vicinity so
// the rest of the lensed terrain skips the work.
[[nodiscard]] __device__ inline vec3
apply_lensed_reticle(const metal_ball& ball, const render_config& cfg,
    pos3 eye, vec3 ray_dir, pos3 exit, vec3 dg, float twg, float px_scale,
    vec3 base, bool& reticle_edge) {
  constexpr float reticle_reach = (1.0F / cos_30_v<>)+2.0F;
  if (!cfg.reticle.enabled ||
      length((exit + (dg * twg)) - cfg.reticle.center) >=
          cfg.reticle.outer_radius * reticle_reach)
    return base;
  const pos3 hit_g = exit + (dg * twg);
  // Take the ring radius from a smooth surface fitted to the terrain near the
  // aim, not the per-pixel terrain hit: that hit rides the trilinear voxel
  // facets, so its radius frizzes at voxel scale and crawls with any lens
  // motion. The fit (`reticle_surface_fit`, computed once per frame) is a
  // tangent frame at the pick plus a quadric height, so it follows a tunnel or
  // bowl while ignoring the bumps, and degrades to the bare plane on flat
  // ground.
  const reticle_surface_fit& fit = cfg.reticle.fit;
  const float denom = dot(dg, fit.n);
  if (fabsf(denom) < 1.0e-3F) return base; // ray skims the plane: no radius
  // Intersect the tangent plane through the pick, then lift that hit onto the
  // fitted quadric. No "plane behind the exit" guard: a ball resting with
  // slight terrain penetration exits its buried bottom cap below the plane
  // when looking straight down, so `t_plane` goes negative; that backward
  // intersection still resolves to ~the center (a tiny radius), filling the
  // buried-cap disc instead of leaving a hole, and a genuinely distant one
  // self-rejects via the radius reach in `apply_reticle`.
  const float t_plane = dot(cfg.reticle.center - exit, fit.n) / denom;
  const pos3 hit_p = exit + (dg * t_plane); // on the tangent plane at center
  // Lift onto the quadric: take the in-plane offset's (u, v), add the surface
  // height there, and report the conforming 3D radius while keeping the
  // in-plane direction (so the screen-axis azimuth is unchanged). On a curve
  // the radius grows faster than the planar distance, so the ring pulls in to
  // hug the bowl.
  const vec3 dp = hit_p - cfg.reticle.center;
  const float pu = dot(dp, fit.u);
  const float pv = dot(dp, fit.v);
  const float wq =
      0.5F *
      ((fit.a * pu * pu) + (2.0F * fit.b * pu * pv) + (fit.c * pv * pv));
  const float r_planar = sqrtf((pu * pu) + (pv * pv));
  const float er = sqrtf((r_planar * r_planar) + (wq * wq));
  const float scale = (r_planar > 1.0e-4F) ? (er / r_planar) : 0.0F;
  const pos3 hit_s = cfg.reticle.center + (dp * scale);
  const auto refr_hit = [&](vec3 rd) {
    const float te = ball.intersect(eye, rd);
    const pos3 ex = eye + (rd * te);
    return ex + (refract(rd, -ball.normal(ex), cfg.glass.ior) * twg);
  };
  const vec3 ref_ax =
      (fabsf(ray_dir.x) < 0.9F)
          ? vec3{1.0F, 0.0F, 0.0F}
          : vec3{0.0F, 1.0F, 0.0F};
  const vec3 ta = normalize(cross(ray_dir, ref_ax));
  const vec3 tb = cross(ray_dir, ta);
  const float spread = fmaxf(
      length(refr_hit(normalize(ray_dir + (ta * px_scale))) - hit_g),
      length(refr_hit(normalize(ray_dir + (tb * px_scale))) - hit_g));
  const float graze = fmaxf(fabsf(denom), 0.1F); // grazing on the smooth plane
  const float fp = fminf(cfg.reticle.outer_radius, spread / graze);
  return apply_reticle(cfg.reticle, hit_s, fp, base, reticle_edge);
}

// Shade a primary ray whose eye has dollied inside the ball, where the ball is
// a glass lens rather than the opaque one-way mirror it stays from outside.
// The ray refracts at the exit surface and continues into the world; the
// forward (aim) ray meets the sphere at normal incidence so the center stays
// clean while off-axis rays bend. Fresnel blends in the reflected internal
// bounce, rising to a full mirror at the grazing rim (so there is no hard
// refract/reflect seam to shimmer). The transmitted side shows only terrain
// and sky; the reflected bounce shows the player's own saucer, the faint ghost
// in the glass. Returns the world-hit depth (not the ball exit), so the
// adaptive-AA resolve still supersamples silhouettes seen through the lens.
[[nodiscard]] __device__ inline ray_sample
shade_merged_glass(const density_field& field, cudaTextureObject_t color,
    const metal_ball& ball, const saucer_head& head, const render_config& cfg,
    pos3 eye, vec3 ray_dir, float px_scale) {
  const float t_exit = ball.intersect(eye, ray_dir);
  const pos3 exit = eye + (ray_dir * t_exit);
  const vec3 n_out = ball.normal(exit);                // outward at the exit
  const float cosi = fmaxf(dot(ray_dir, n_out), 0.0F); // exit incidence
  const float refl_w = fresnel_reflectance(cosi, cfg.glass.ior);

  // Shade the world (terrain or sky) along a ray leaving the glass at `exit`.
  const auto march_world = [&](vec3 dir) {
    const float tw = field.raymarch(exit, dir);
    return (tw >= 0.0F)
               ? shade_terrain_hit(field, color, cfg, exit + (dir * tw),
                     shadow_sphere{ball.center, ball.radius})
               : sky_color(cfg, dir);
  };

  // Transmitted (refracted) view. With dispersion on, each channel refracts at
  // a slightly different index, so the off-axis bend splits R/G/B into lateral
  // chromatic fringing; the base (green) ray sets the depth, and a channel
  // that hits its own total internal reflection falls back to the green sample
  // rather than going black.
  vec3 trans{};
  float world_depth = big_value;
  bool reticle_edge = false;
  vec3 dg{};
  float twg = -1.0F;
  if (refl_w < 1.0F) {
    dg = refract(ray_dir, -n_out, cfg.glass.ior);
    twg = field.raymarch(exit, dg);
    const vec3 green =
        (twg >= 0.0F)
            ? shade_terrain_hit(field, color, cfg, exit + (dg * twg),
                  shadow_sphere{ball.center, ball.radius})
            : sky_color(cfg, dg);
    world_depth = (twg >= 0.0F) ? t_exit + twg : big_value;
    if (const float disp = cfg.glass.dispersion; disp > 0.0F) {
      const vec3 dr = refract(ray_dir, -n_out, cfg.glass.ior * (1.0F - disp));
      const vec3 db = refract(ray_dir, -n_out, cfg.glass.ior * (1.0F + disp));
      const float r = (dot(dr, dr) > 1.0e-8F) ? march_world(dr).x : green.x;
      const float b = (dot(db, db) > 1.0e-8F) ? march_world(db).z : green.z;
      trans = vec3{r, green.y, b};
    } else {
      trans = green;
    }
  }

  // Reflected internal bounce: the player's own saucer, faintly mirrored in
  // the glass. A miss falls to a dim interior, which doubles as the grazing
  // rim going dark.
  vec3 ghost{};
  if (refl_w > 0.0F) {
    const vec3 rd = reflect(ray_dir, n_out);
    const float th = head.raymarch(exit, rd);
    ghost = (th >= 0.0F) ? shade_head(head, cfg, exit + (rd * th), rd)
                         : cfg.terrain.ambient;
  }

  vec3 col = (trans * (1.0F - refl_w)) + (ghost * (refl_w * cfg.glass.ghost));
  // Composite the in-world dig reticle on top of the Fresnel blend, not into
  // the transmitted side: the reflected head ghost grows over the center as
  // the pitch nears straight down (the reflected ray points back up the look
  // axis, at the head sitting atop the ball), and folding the reticle into
  // `trans` let that ghost wash out the aim marker. Painted where the base
  // refracted ray struck terrain (a sky miss has nothing to project onto), and
  // before the vignette so the grazing rim still dims it with the rest of the
  // lensed view. See `apply_lensed_reticle`.
  if (twg >= 0.0F)
    col = apply_lensed_reticle(ball, cfg, eye, ray_dir, exit, dg, twg,
        px_scale, col, reticle_edge);
  // Faked corner vignette on top of the Fresnel falloff: darken toward the
  // grazing rim, where the exit incidence cosine is small.
  col = col * (1.0F - (cfg.glass.vignette * (1.0F - cosi)));
  // The propulsion hex shell, seen from inside: the clean in-focus porthole
  // frame on the surface the ray exits, scrolling as you move. Added crisp
  // over the warped world beyond (after the vignette, so the frame stays
  // bright at the rim), the same emissive grid the outer mirror shows.
  col = col + ball_grid_emissive(ball, cfg, n_out);
  return ray_sample{col, world_depth, 2, reticle_edge};
}

// Composite the primary ray: the nearest of the ball, the terrain, and the
// flat mirror, or the sky if it escapes them all. The saucer head is tested
// only when `cfg.show_head` is set (the observer freeze): normally the camera
// rides inside it, so it appears only reflected in the ball (`shade_ball`) or
// the mirror (`shade_world_ray`), but the freeze pins the camera outside it
// and reveals it here. Returns the shaded color alongside the hit kind and
// depth, so the adaptive-AA prepass can classify the pixel.
[[nodiscard]] __device__ inline ray_sample
shade_primary_ray(const density_field& field, cudaTextureObject_t color,
    const metal_ball& ball, const saucer_head& head, const flat_mirror& mirror,
    const render_config& cfg, pos3 eye, vec3 ray_dir, float px_scale) {
  // Merged glass view: when the eye has dollied inside the ball, it is a glass
  // lens, not the opaque one-way mirror it stays from outside (see
  // `shade_merged_glass`).
  if (const vec3 oc = eye - ball.center;
      dot(oc, oc) < ball.radius * ball.radius)
    return shade_merged_glass(field, color, ball, head, cfg, eye, ray_dir,
        px_scale);

  float t_terrain = field.raymarch(eye, ray_dir);
  const float t_ball = ball.intersect(eye, ray_dir);
  // Tunnel-view sanity: at the jockey, let the ball draw through any terrain
  // nearer than it, so a wall between the close camera and the ball does not
  // bury it. Only the ball's own silhouette is punched through (terrain the
  // ray would miss the ball stays put), so nothing reveals a cleared tunnel
  // and the only edge is the ball's smooth outline. Render-only, so collision
  // is unaffected.
  if (cfg.jockey_clear && t_ball >= 0.0F && t_terrain >= 0.0F &&
      t_terrain < t_ball)
    t_terrain = -1.0F;
  const float t_mirror =
      cfg.show_mirror ? mirror.intersect(eye, ray_dir) : -1.0F;
  const float t_head = cfg.show_head ? head.raymarch(eye, ray_dir) : -1.0F;
  float best = big_value;
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
  vec3 col;
  bool reticle_edge = false;
  if (kind == 2)
    col = shade_ball(field, color, ball, head, cfg, eye + (ray_dir * best),
        ray_dir);
  else if (kind == 3) {
    const pos3 hit = eye + (ray_dir * best);
    const vec3 refl = reflect(ray_dir, mirror.normal);
    col = shade_world_ray(field, color, ball, head, cfg, hit, refl) * 0.9F;
  } else if (kind == 4)
    col = shade_head(head, cfg, eye + (ray_dir * best), ray_dir);
  else if (kind == 1) {
    const pos3 hit = eye + (ray_dir * best);
    // Snap onto the true surface for the reticle radius (outside the lens the
    // real terrain hit is stable enough; only the merged view low-passes it).
    const pos3 snapped = field.refine_hit(hit, ray_dir);
    col = apply_reticle(cfg.reticle, snapped, length(snapped - eye) * px_scale,
        shade_terrain_hit(field, color, cfg, hit,
            shadow_sphere{ball.center, ball.radius}),
        reticle_edge);
  } else
    col = sky_color(cfg, ray_dir);
  return ray_sample{col, best, kind, reticle_edge};
}

#pragma endregion

} // namespace corvid::cuda
