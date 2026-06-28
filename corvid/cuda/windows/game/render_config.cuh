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

#include "../../vec.cuh"

// Runtime shading parameters for the voxel viewer, gathered so the tuning
// panel can edit them live with no recompile.

namespace corvid::cuda {

#pragma region render_config

// The shading "constants" the `shade_*` functions used to bake in as literals,
// as a plain aggregate usable from host and device. The host keeps one live
// instance plus a default-constructed baseline, the panel edits the live one,
// and the frame passes it by value into the render kernels, which thread it
// through the shaders. The default member initializers are the values the
// shaders shipped with.
struct render_config {
  // The scene sun direction: the source of the sky's sun glow and the diffuse
  // light on the terrain and the saucer. Normalized in the shaders. This is
  // the single value to animate if the sun ever moves across the sky.
  vec3 sun_direction{0.5F, 0.8F, 0.3F};

  // Sky gradient and sun glow (see `sky_color`).
  struct sky_params {
    vec3 zenith{0.18F, 0.42F, 0.82F};    // color straight up
    vec3 horizon{0.82F, 0.91F, 0.98F};   // color at the horizon
    float gradient_bias = 0.45F;         // < 1 biases toward the horizon band
    vec3 halo_color{1.0F, 0.85F, 0.55F}; // wide warm glow around the sun
    float halo_strength = 0.30F;
    float halo_exponent = 8.0F;          // higher = tighter halo
    vec3 core_color{1.0F, 0.96F, 0.88F}; // the bright sun disc itself
    float core_exponent = 350.0F;        // higher = smaller, sharper disc
  } sky;

  // Terrain lighting (see `shade_terrain_hit`); lit by the shared
  // `sun_direction`.
  struct terrain_params {
    vec3 ambient{0.18F, 0.20F, 0.24F};
    vec3 sun{1.0F, 0.96F, 0.88F};
  } terrain;

  // Terrain march tunables, copied onto the `density_field` each frame so the
  // panel can tune the sphere-trace live. Defaults match `density_field`'s.
  struct march_params {
    float lipschitz = 2.0F;       // assumed max field slope; lower = faster
    float max_step_voxels = 8.0F; // single-step cap, in voxels
    int max_steps = 1024;         // work cap per ray
  } march;

  // Metal ball reflection look (see `shade_ball`).
  struct ball_params {
    float dim = 0.65F; // darkens the mirrored scene
    vec3 tint{0.82F, 0.86F, 0.95F};
    vec3 ambient_floor{0.0F, 0.0F, 0.0F}; // keeps the darkest reflections up

    // Motion grid: an emissive flat hex wireframe wrapped onto the ball by the
    // rolling-conveyor projection to show its rotation, flaring up only while
    // it moves. `hex_freq` sets the cell density (cells per radian, low for
    // large tiles), `hex_line` the seam half-width in radians, `hex_strength`
    // the glow brightness. `grid_extent` is how far toward the axle (the
    // ball's sides) the grid reaches before fading out the cells that shrink
    // there: higher shows more, lower hides the shrink sooner.
    int hex_freq = 2;
    float hex_line = 0.006F;
    float hex_strength = 1.0F;
    vec3 hex_color{0.20F, 1.0F, 0.45F};
    float grid_extent = 1.05F;
  } ball;

  // Saucer head shading (see `shade_head`).
  struct head_params {
    // Surface: the head-wide lighting, the bare-steel hull albedo, and the
    // specular highlight.
    vec3 ambient{0.10F, 0.11F, 0.13F};
    vec3 sun{1.0F, 0.96F, 0.88F};
    vec3 base_albedo{1.0F, 1.0F, 1.0F}; // bare steel (the cone and belly)
    float dome_specular_power = 150.0F; // sharper glint on the dome
    float belly_specular_power = 48.0F; // softer on the belly
    float specular_strength = 0.5F;

    // Belly paint: concentric rings times spinning spokes.
    float ring_paint_frequency = 26.0F;
    float spoke_paint_frequency = 12.0F;
    float paint_base = 0.35F;  // darkest the paint dims the albedo to
    float paint_range = 0.65F; // added back where rings and spokes peak

    // Belly central flashlight hub.
    float hub_radius = 0.15F;
    float hub_softness = 0.06F;
    vec3 hub_color{1.0F, 0.95F, 0.80F};
    float hub_strength = 2.5F;

    // Belly amber spoke lights: a ring of glowing dots on the underside near
    // the edge, turning with the belly paint.
    float spoke_center = 0.80F; // radius of the ring (fraction of disc radius)
    float spoke_width = 14.0F;  // higher = thinner ring
    vec3 spoke_color{1.0F, 0.50F, 0.12F};
    float spoke_strength = 2.2F;
    float spoke_dot_frequency = 16.0F;

    // Rim running lights: a ring of emissive segments set into the rounded
    // shoulder where the top curves down to the brim, crisp at the top of that
    // shoulder and fading downward, so they read as lights projecting down
    // whose diffuse lower half still shows from above. `rim_top` is the crisp
    // top edge, in normal units (the surface normal's vertical component: ~1
    // on the flat top, 0 at the brim boundary, negative on the belly), set
    // where the flat top ends; the panel grooves stop there too, leaving this
    // edge to the rim light. `rim_width` is the downward fade distance from
    // `rim_top`. `rim_count` segments fade smoothly between dark and lit
    // around the ring
    // (`rim_floor` raises the dark end, scaling the fade into [rim_floor, 1]
    // without clipping it), turning with the belly at `rim_spin_scale` of its
    // rate (slower reads calmer and avoids the fast-spin strobe).
    float rim_top = 0.75F;  // the normal's vertical value there
    float rim_width = 1.0F; // downward fade distance from `rim_top` (normal)
    vec3 rim_color{0.465F, 0.475F, 0.630F};
    float rim_strength = 0.4F;
    int rim_count = 24;     // number of running lights around the ring
    float rim_floor = 0.3F; // dark end of the segment fade (0 = full 0..1)
    float rim_spin_scale = 0.5F; // rim spin rate / belly spin rate

    // Saucer top: fixed decals dressing the bare upper cone for a classic
    // hull look. A ring of dark portholes and a set of radial panel grooves,
    // both placed in the saucer's local frame so they hold still on the hull
    // (they do not spin with the belly). The hull is radially symmetric, so
    // their fixed angle is invisible under the bank.
    int port_count = 14;                  // portholes round the ring (0 off)
    float port_center = 0.775F;           // ring radius / disc radius
    float port_radius = 0.075F;           // porthole radius / disc radius
    vec3 port_color{0.02F, 0.02F, 0.05F}; // dark porthole glass
    float port_phase = 0.224F;            // ring rotation about the axis, rad
    int panel_count = 14;                 // radial panel grooves (0 off)
    float panel_line = 0.01F;             // groove half-width (arc) / radius
    float panel_strength = 0.4F;          // how much the grooves darken
    float panel_phase = 0.224F; // groove rotation about the axis, rad

    // Dome canopy tint, plus the dome cap's own steel albedo, kept separate
    // from `base_albedo` so the dome can be darkened to pop without dimming
    // the rest of the hull. Split at the same `rr` boundary as the hex grid
    // (`dome_hex_extent`).
    vec3 canopy{0.16F, 0.20F, 0.28F};
    vec3 dome_albedo{0.0F, 0.0F, 0.0F};

    // Geodesic (Goldberg) hex grid on the dome cap: an icosahedron subdivided
    // to `dome_hex_freq` (near-uniform hexagons plus twelve pentagons),
    // reoriented so a cell lands on the eye. `dome_hex_phase` adds a manual
    // roll of the grid about the eye; it stays 0 so the grid reads level (its
    // hexagons parallel to the ground) and the eye cell sits flush, the
    // antenna instead being seated on a grid edge midpoint by the off-round
    // `lead` constant in the rig.
    int dome_hex_freq = 9;           // icosahedron subdivision frequency
    float dome_hex_line = 0.01F;     // seam half-width, radians
    float dome_hex_strength = 3.25F; // how much the seams darken the dome
    float dome_hex_extent = 0.463F;  // rr out to which the grid covers
    float dome_hex_phase = 0.0F;     // manual grid roll about the eye, radians

    // A hard black band masking the dome/cone joint. A solid, hard-edged band
    // whose inner edge is pulled `seam_offset` inside the hex grid's
    // `dome_hex_extent` cutoff (to cover the specular-washed dome edge just
    // inside it) and which spans `seam_width`. Drawn like the eye (emissive,
    // with the lit metal and specular removed) so the whole band reads true
    // black instead of washing out under the lighting; reads as a groove the
    // dome sits in, hinting it could separate.
    float seam_offset = -0.008F;       // inner edge of seam
    float seam_width = 0.024F;         // band width (solid, hard-edged)
    vec3 seam_color{0.0F, 0.0F, 0.0F}; // band color (hard black)

    // Cockpit eye: a pupil hub and radial spokes inside an opaque hexagonal
    // iris on the dome front, placed by the rig's look gimbal. The iris spans
    // two grid cells, so its edge nests flush with the surrounding hex cells.
    // Albedo only, crisp, so it holds up small in the ball reflection.
    float eye_hub = 0.09F;   // central hub-circle radius
    int eye_spokes = 6;      // radial spokes inside the iris (try 3)
    float eye_line = 0.018F; // frame, spoke, and hub line width
    vec3 eye_glass{0.003F, 0.016F, 0.049F};    // iris
    vec3 eye_pupil{0.0F, 0.0F, 0.0F};          // hub center (the beam source)
    vec3 eye_frame_color{0.62F, 0.62F, 0.62F}; // frame, spokes, hub ring

    // Pupil glow: the beam source lights up while the dig tool is projecting
    // (`reticle.enabled`), so the ball reflection shows the eye charging.
    // Added emissive over the pupil hub.
    vec3 eye_glow_color{0.20F, 1.0F, 0.55F};
    float eye_glow_strength = 2.5F; // 0 disables the pupil glow

    // Antenna tip beacon: the ball atop the dome's antenna, drawn emissive so
    // it reads as a light. The rod uses the bare-steel `base_albedo`. The
    // beacon shows `antenna_tip_color` while moving forward or strafing and
    // `antenna_alt_color` while backing up; at rest it alternates the two in
    // tune with the belly's idle spin. `blink_depth` is the on/off portion
    // while moving (see `avatar_tuning` for the rate): 0 holds it lit, 1
    // blinks it fully dark; it is gated off at rest, so the resting
    // alternation is a smooth color change with no pulsing.
    vec3 antenna_tip_color{0.469F, 1.0F, 0.1F}; // glowing bead (green beacon)
    vec3 antenna_alt_color{1.0F, 0.08F, 0.04F}; // backing-up color (red)
    float blink_depth = 0.7F;                   // on/off blink while moving
  } head;

  // The in-world dig reticle: a slowly spinning hexagon hologram laid on the
  // terrain where the dig tool aims, oriented to the surface normal so it
  // conforms to slopes (an outer ring marking the dig footprint and a smaller
  // inner crosshair hex). The engine fills the per-frame state each frame from
  // the aim pick; the rest are look tunables. See `apply_reticle`.
  struct reticle_params {
    // Per-frame state, written by the engine (not panel-tuned): whether the
    // dig tool is on and the aim hit, the world hit point and its surface
    // normal (the azimuth frame the hexagons spin in), and the rotation phase.
    bool enabled = false;
    pos3 center{};
    vec3 normal{0.0F, 1.0F, 0.0F};
    float spin = 0.0F;
    // Hide the inner crosshair when the ball blocks the aim (the dig beam
    // leaves the ball, so it cannot fire through itself); the engine sets it.
    bool show_inner = true;

    // Look tunables.
    float spin_rate = 0.6F;     // hologram turn rate, radians per second
    float outer_radius = 1.0F;  // outer hexagon apothem, world units
    float outer_clip = 1.06F;   // circular clip on the outer hex, x apothem
    float inner_radius = 0.11F; // inner crosshair hexagon apothem
    float outer_line = 0.06F;   // outer ring half-thickness, world units
    float inner_line = 0.015F;  // inner hex + spoke half-thickness (finer)
    int inner_spokes = 3;       // crosshair spokes in the inner hex (0..6)
    vec3 color{0.20F, 1.0F, 0.55F}; // hologram glow color
    float strength = 1.0F;          // hologram glow brightness
  } reticle;

  // Anti-alias samples per axis: 1 disables it, 2 to 3 is the useful range.
  // Runtime (not a `constexpr`) so the panel can change it. The AA is
  // adaptive: a cheap prepass shades one center sample per pixel, and only
  // pixels on a silhouette fan out to the full `aa_samples` x `aa_samples`
  // grid, so the squared cost is paid on edges, not flat interiors.
  int aa_samples = 2;

  // Adaptive-AA edge threshold. A pixel is treated as a silhouette (and
  // supersampled) when its nearest-hit kind differs from a 4-neighbor, or its
  // depth bends by more than this fraction of its own depth across the
  // neighbors (a second difference, so a smooth grazing ramp does not trip it,
  // only a crease or a depth jump). Larger spends less on edges (faster, more
  // residual aliasing on shallow creases); 0 supersamples every depth wrinkle.
  float aa_edge_depth = 0.05F;

  // Observer freeze (debug): draw the saucer head in the primary view. Off by
  // default, since the camera normally rides inside the head and so never sees
  // it directly. The viewer sets this from the freeze-camera toggle, which
  // also pins the camera so the avatar can be driven out in front and watched.
  bool show_head = false;

  // Debug: show the ball's reflection undimmed (`shade_ball`), to tell a real
  // black artifact from the dark belly merely crushed by the dim factor.
  bool debug_ball_raw = false;

  // Whether the flat mirror wall is in the scene (`shade_primary_ray`). Off by
  // default; the panel can show it for debugging.
  bool show_mirror = false;
};

#pragma endregion

} // namespace corvid::cuda
