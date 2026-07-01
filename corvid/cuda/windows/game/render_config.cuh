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

// A smooth local fit of the terrain around the dig reticle's aim point.
//
// An orthonormal tangent frame at the pick (`u`, `v` along the surface, `n`
// the normal) plus the quadric `w(u, v) = 0.5*(a*u^2 + 2*b*u*v + c*v^2)`, the
// surface's height above that tangent plane. Fitted once per frame from a few
// terrain samples spanning the reticle footprint (`fit_kernel`), so the
// reticle conforms to a tunnel or bowl without riding the per-pixel voxel
// facets; flat ground leaves `a = b = c = 0` (the bare plane). See
// `apply_lensed_reticle`.
struct reticle_surface_fit {
  vec3 u{1.0F, 0.0F, 0.0F};
  vec3 v{0.0F, 0.0F, 1.0F};
  vec3 n{0.0F, 1.0F, 0.0F};
  float a = 0.0F;
  float b = 0.0F;
  float c = 0.0F;
};

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

  // Night: turn off the sun (its diffuse on the terrain and its glow in the
  // sky) and dim the ambient to near dark, so there is darkness to test the
  // flashlight against. The emissive lights and the flashlight are unaffected.
  bool night = false;

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

  // Flashlight: a headlamp riding the camera eye and pointing along the view,
  // adding a soft cone of light to the terrain (toggled by the F key, handy at
  // night or down a deep pit).
  //
  //  `origin` and `direction` are written each frame by the engine from the
  //  camera; the rest are look tunables. The cone has a soft edge
  //  (`cone_degrees` outer half-angle, `softness` the inner fraction)
  // and a quadratic distance falloff out to `range`.
  struct flashlight_params {
    bool enabled = false;             // the F-key toggle (engine-driven)
    pos3 origin{};                    // eye position, written per frame
    vec3 direction{0.0F, 0.0F, 1.0F}; // view forward, written per frame
    vec3 color{1.0F, 0.93F, 0.80F};
    float intensity = 4.0F;
    float range = 45.0F;          // reach in world units
    float cone_degrees = 23.333F; // outer half-angle of the cone
    float softness = 1.5F;        // inner cone = outer x (1 - softness)
    // Penumbra of the ball's shadow on the terrain, as a fraction of the ball
    // radius: the soft band the shadow fades across, so its edge reads (and
    // peeks out from behind the ball that hides the hard umbra). 0 is a hard
    // shadow.
    float shadow_softness = 0.55F;
    // Brightness of the emitter on the head (the eye's iris segments light up
    // as the lamp source). HDR, so the ball's reflection of it blows out
    // through bloom instead of reading as flat white.
    float source_strength = 30.0F;
    // The ball's glossy response to the beam: an HDR view-facing highlight so
    // the chrome lights up and blows out (stable across poses, unlike the tiny
    // reflection of the emitter). `gloss_power` sets the lobe breadth near the
    // ball: keep it tight, a broad lobe saturates into a flat white disc.
    float gloss_strength = 3.0F;
    float gloss_power = 9.0F;
    // How much the glossy spot spreads with distance, like the cone footprint:
    // higher broadens the highlight (and the `fade` dims it) as the ball gets
    // farther from the lamp. 0 holds a fixed-breadth specular spot.
    float gloss_grow = 0.017F;
  } flashlight;

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

    // Eye-cone glow: while the dig tool is projecting (`reticle.enabled`) the
    // eye casts a speckled green haze cone into the air along the aim, a
    // volume that floats in front of the dome rather than a decal glued to it
    // (see `eye_cone_glow`). Shown on the freeze view's primary ray and in the
    // ball's reflection of the saucer.
    //
    // The laser is green everywhere; white is only what the HDR tonemap reads
    // at the very brightest peak (each channel saturating to one), so the air
    // and any reflection stay green while the source blows out to white. The
    // air cone is faint scatter only, no bright core: the beam's brightness
    // lives at its source, the pupil, whose locked center runs at
    // `eye_glow_peak_gain` (intense green, peak reads white; see
    // `pupil_emitter`).
    //
    // The cone's apex is the eye. It is sized to the live aim geometry so it
    // stays a natural cone at any range: `eye_glow_length` is its reach as a
    // fraction of the eye-to-target distance (1 reaches the target), and
    // `eye_glow_radius` is the tip radius as a fraction of the outer reticle's
    // world size at the target (`reticle.outer_radius`). Brightest near the
    // apex, tinted `eye_glow_color` x `eye_glow_strength`. The speckle
    // (`eye_glow_speckle` amount, `eye_glow_speckle_freq` cell count) swirls
    // and drifts in sync with the reticle's spin. A down-beam dead zone
    // suppresses the cone where you look along it (there it degenerates into a
    // flat end-on disc); it reads only in profile and reflection.
    // `eye_glow_backscatter` lifts that dead-zone floor if any down-beam haze
    // is wanted.
    vec3 eye_glow_color{0.20F, 1.0F, 0.55F};
    float eye_glow_strength = 4.0F; // green haze brightness (0 disables)
    float eye_glow_length = 1.2F;   // reach, fraction of eye->target distance
    float eye_glow_radius = 0.65F;  // tip radius, fraction of reticle size
    float eye_glow_peak_gain = 18.0F; // pupil center peak (green, blows white)
    float eye_glow_speckle = 0.5F;    // smoke-texture depth (0 smooth)
    int eye_glow_speckle_freq = 16;   // smoke grain scale (higher = finer)
    float eye_glow_spin = 5.0F;       // swirl rate, radians per second
    float eye_glow_backscatter = 0.1F; // down-beam floor (faint but visible)
    // Speckle boil rate: keep the cone's speckle alive when the camera holds
    // still. The visible speckle is the jittered march (a per-ray offset of
    // the sample depths, decorrelated pixel to pixel), which freezes when the
    // camera stops and the ray directions stop changing; the smoke texture is
    // only the slower wisp under it. `eye_glow_boil` advances that jitter
    // phase over time (cycles per second) so it keeps boiling in place; 0
    // freezes it between camera moves (see `eye_cone_glow`).
    float eye_glow_boil = 4.0F;
    // Dust extinction: optional Beer-Lambert dimming with the target's
    // distance
    // (`exp(-extinction * target_dist)`), on top of the always-on geometric
    // falloff that already makes a farther aim dimmer (see `eye_cone_glow`,
    // the distance response). 0 is clear air (geometric falloff only).
    // Monotonic for any value, so aiming farther never brightens the glow.
    float eye_glow_extinction = 0.0F;
    // Merged view only: brightness of the pupil's near-field veiling glow,
    // seen from inside looking out along your own aim (green, peak reads
    // white; see `shade_merged_glass`). 0 disables. Separate from the outside
    // air cone, whose down-aim backscatter would dim to a ring at the source.
    float eye_glow_merged_gain = 1.5F;
    // Radial edge feather: fades the cone's shell to zero as it nears the
    // `rn == 2` radial cull, over a band that widens inward as this grows, so
    // the disc's outer edge is soft instead of the hard circle the plain cull
    // makes. Ease-out shaped (`1 - q^3`), so it holds the middle bright and
    // fades only near the rim rather than dimming the whole disc. Stays inside
    // the cull, so the march bounds are unchanged. 0 keeps the hard edge.
    float eye_glow_edge_soft = 2.0F;
    // Counter-rotating inner core, shown only while the aim is locked
    // (`show_inner`, the inner crosshair on). The inner reticle counter-spins
    // the outer, so full mode gives the cone a faint inner swirl turning the
    // other way, textured by a second smoke field at the opposite azimuth. It
    // busies the too-even outer swirl and makes locked read distinct from the
    // outer-only rim state. 0 keeps the plain hollow shell (rim and locked
    // look the same); higher makes the counter-core brighter and busier.
    float eye_glow_counter = 1.0F;
    // Debug: render only the eye-cone glow (the rest of the scene black), so
    // its shape and edge can be read in isolation from the terrain, ball, and
    // reticle.
    bool eye_glow_solo = false;

    // Reticle glare: while the dig tool projects (`reticle.enabled`), the
    // pupil's ring of laser light blooms outward, a soft green glow sourced at
    // the pupil (never the iris, which is the white flashlight source), so
    // looking at the eye -- directly, in the ball, or in the flat mirror --
    // reads as laser glare (see `eye_glare_halo`). A real surface emissive
    // every ray path catches. The glow rises from a dark center to the hub rim
    // (so the pupil stays a dark hole until locked) and fades outward, an
    // extension of the pupil ring into the air. Green, so the HDR peak reads
    // white while the skirts stay green: crank the gain for a blinding bloom,
    // drop to 0 to disable. `eye_glare_gain` is the glow while merely
    // projecting (ring only, not locked); `eye_glare_lock_gain` while locked
    // on a target
    // (`show_inner`), so the lit and locked glare tune apart.
    // `eye_glare_spread` is how far it reaches out past the pupil hub rim
    // (same units as `eye_hub`).
    float eye_glare_gain = 3.0F;      // lit (not locked) glow brightness
    float eye_glare_lock_gain = 8.0F; // locked glow brightness
    float eye_glare_spread = 0.15F;   // reach past the hub rim (eye_hub units)
    // Dark pupil center while merely projecting (not locked): a crisp dark
    // hexagon punched out of the pupil center (and a matching round core out
    // of the glare, which has no hard edges to need a hex), so the unlit inner
    // reads as a distinct hole instead of a soft dip the bloom washes over.
    // Its apothem is this fraction of `eye_hub`. Once locked, the white-hot
    // center fills it. 0 disables the hole (the plain radial pupil).
    float eye_pupil_hex = 1.0F;

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

  // The in-world target reticle: a slowly spinning hexagon laser-projected
  // onto the terrain where the active tool aims (an outer ring marking the
  // footprint and a smaller inner crosshair hex). It takes its radius from the
  // pick point's 3D distance, so it hugs a dug bowl, and its azimuth from the
  // camera's screen axes, so its roll holds steady across slopes and creases.
  // The engine fills the per-frame state each frame from the aim pick; the
  // rest are look tunables. See `apply_reticle`.
  struct reticle_params {
    // Per-frame state, written by the engine (not panel-tuned): whether the
    // dig tool is on and the aim hit, the world hit point, and the rotation
    // phase.
    //
    // `view_right` / `view_up` are the camera's screen axes: the reticle
    // measures its azimuth against them (not the surface normal), so its roll
    // is steady as the aim sweeps across a terrain crease, where the normal
    // jumps discontinuously and a normal-derived frame would snap the pattern.
    // The radius still uses the 3D hit distance, so the ring keeps hugging the
    // dug bowl.
    bool enabled = false;
    pos3 center{};
    vec3 view_right{1.0F, 0.0F, 0.0F};
    vec3 view_up{0.0F, 1.0F, 0.0F};
    float spin = 0.0F;
    // The eye-cone glow's own swirl phase, advanced by the engine at
    // `head_params::eye_glow_spin` (decoupled from this reticle's spin so the
    // cone can swirl visibly while the ground reticle turns slowly).
    float eye_glow_phase = 0.0F;
    // A monotonic clock (seconds since start) for the cone's speckle drift,
    // advanced by the engine. Separate from the wrapping swirl phase because
    // the drift scrolls the noise linearly: a wrap would jump the pattern (a
    // lighthouse flash), so this is not wrapped. Float precision holds over
    // any real session.
    float eye_glow_time = 0.0F;
    // The local terrain curvature around `center`, fit each frame so the
    // reticle conforms to a tunnel or bowl (see `reticle_surface_fit`).
    reticle_surface_fit fit;
    // Whether `center` is a real terrain pick (so `fit` is valid and on the
    // ground), as opposed to an in-air aim (sky miss, or the force-beam debug
    // pointing ahead). The eye-cone glow clips to the ground plane only when
    // this is set (see `eye_cone_glow`); otherwise there is no ground to clip
    // to. The engine sets it from the pick.
    bool grounded = false;
    // Hide the inner crosshair when the ball blocks the aim (the dig beam
    // leaves the ball, so it cannot fire through itself); the engine sets it.
    bool show_inner = true;

    // Look tunables.
    float spin_rate = 0.6F;     // reticle turn rate, radians per second
    float outer_radius = 1.0F;  // outer hexagon apothem, world units
    float outer_clip = 1.06F;   // circular clip on the outer hex, x apothem
    float inner_radius = 0.11F; // inner crosshair hexagon apothem
    float outer_line = 0.06F;   // outer ring half-thickness, world units
    float inner_line = 0.015F;  // inner hex + spoke half-thickness (finer)
    int inner_spokes = 3;       // crosshair spokes in the inner hex (0..6)
    vec3 color{0.20F, 1.0F, 0.55F}; // reticle glow color
    float strength = 1.0F;          // reticle glow brightness
    // Extra brightness on the inner crosshair only (multiplies `strength` for
    // the inner hex and its spokes, not the outer ring). Raise it to blow the
    // locked crosshair toward white through the HDR tonemap, so it reads over
    // the eye-cone's counter-rotating core (see `eye_glow_counter`) instead of
    // washing out. 1 keeps the inner at the outer's brightness.
    float inner_gain = 6.0F;
    // Max dig reach: when the aim hit is farther than this from the ball, drop
    // the inner crosshair and block the dig, the same as when the ball blocks
    // the aim, so digging stays a close-range action.
    float max_dig_distance = 10.0F; // world units from the ball

    // One Euro aim smoothing for the reticle center.
    //
    // `pick_rest_rate` is the at-rest ease rate floor (lower eases harder:
    // steadier when the aim is still, but laggier); `pick_beta` lifts that
    // rate with the pick's speed, so a deliberate sweep relaxes the smoothing
    // and the marker tracks instead of lagging (0 leaves it the fixed
    // distance-scaled low-pass).
    float pick_rest_rate = 3.0F; // at-rest ease rate floor, per second
    float pick_beta = 8.0F;      // speed-relax coefficient, per world unit
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

  // Tunnel-view sanity: when the camera is at the jockey, let the ball draw
  // through any terrain nearer than it, across its own silhouette only, so a
  // wall between the close camera and the ball does not bury the view
  // (`shade_primary_ray`). Render-only: the collision world is untouched. Set
  // per frame by the viewer when the boom is at the jockey.
  bool jockey_clear = false;

  // Debug: flat-tint the pixels the adaptive-AA resolve pass supersamples,
  // instead of shading them, to verify edge detection. Geometry silhouettes
  // show red, dig-reticle pixels blue. A pixel left at its prepass color was
  // judged a flat interior and not supersampled.
  bool debug_aa_edges = false;

  // Barrel distortion: how far the projection bends from the rectilinear
  // pinhole (0) toward a full equidistant fisheye (1), see
  // `camera_rays::ray_direction`. The center and vertical field of view are
  // unchanged; the amount only bends the periphery in, so a small value gives
  // a mild barrel without the full-fisheye pitch nausea.
  float fisheye_amount = 0.0F;

  // Glass ball lens: when the camera dollies inside the ball (merged), the
  // ball stops being an opaque mirror for the primary view and becomes a solid
  // glass lens the ray refracts through to see the world
  // (`shade_primary_ray`).
  //
  // The forward (aim) ray meets the sphere at normal incidence and stays
  // clean; off-axis rays bend, the coma the merged viewpoint trades for always
  // having somewhere valid to stand. From outside the ball is unchanged (still
  // the opaque one-way mirror).
  //
  // The geometric tier (one ray per pixel): `ior` sets how hard the surface
  // bends the view (and where the grazing rim hits total internal reflection).
  // `dispersion` spreads the index per channel for lateral chromatic fringing
  // (0 off; the three RGB rays then march separately, ~3x the cost). Fresnel
  // (derived from `ior`) blends in the reflected internal bounce, rising from
  // ~4% head-on to a full mirror at the grazing rim, which also removes the
  // hard refract/reflect seam. `ghost` scales that reflected bounce (the faint
  // image of the player's own saucer in the glass; 0 hides it). `vignette`
  // darkens the corners toward the rim on top of the Fresnel falloff (0 off).
  struct glass_params {
    float ior = 1.347F;       // index of refraction of the ball glass
    float dispersion = 0.04F; // per-channel index spread for chromatic (0 off)
    float ghost = 2.0F;       // strength of the internal-bounce saucer ghost
    float vignette = 3.0F;    // extra corner darkening toward the grazing rim
  } glass;

  // Merge ripple: a one-shot force-field shockwave played as the camera eye
  // crosses the ball surface (merging in or backing out), so the hard switch
  // between the opaque mirror and the glass lens reads as breaching the shell
  // rather than a cut.
  //
  // A radial wave warps the primary-ray sample about the screen center
  // (`ripple_warp` in render_kernel.cuh): concentric rings that expand as
  // `phase` advances and fade as `amplitude` decays to 0. The exact center has
  // no offset, so the normal-incidence aim point holds still while the
  // surround ripples.
  //
  // `amplitude` and `phase` are written each frame by the engine, which
  // triggers the decaying shockwave on the crossing edge; the rest are look
  // tunables.
  struct ripple_params {
    float amplitude = 0.0F;   // live radial warp strength this frame (0 = off)
    float phase = 0.0F;       // live ring phase, advances as the rings expand
    float peak = 0.05F;       // max radial displacement at the crossing
    float frequency = 2.5F;   // rings across the half-screen
    float duration = 0.75F;   // effect length, seconds
    float ring_speed = 18.0F; // ring expansion rate, phase radians per second
  } ripple;

  // Bloom: the render writes linear HDR into an off-screen buffer, and the
  // post pass blooms the brights (a soft-thresholded, blurred copy added back)
  // before the Reinhard tonemap that lands the LDR frame. With `enabled` off,
  // the post pass is a plain tonemap, identical to the per-pixel tonemap the
  // render used to do inline.
  struct bloom_params {
    bool enabled = true;
    float threshold = 0.37F; // luma at which a pixel starts to bloom
    float knee = 0.6F;       // soft-threshold width below `threshold`
    float intensity = 1.0F;  // how much of the bloom is added back
    float sigma = 2.5F;      // Gaussian blur radius, half-res texels
  } bloom;
};

#pragma endregion

} // namespace corvid::cuda
