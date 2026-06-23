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
// and the frame passes it by value into `voxel_kernel`, which threads it
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

  // Metal ball reflection look (see `shade_ball`).
  struct ball_params {
    float dim = 0.65F; // darkens the mirrored scene
    vec3 tint{0.82F, 0.86F, 0.95F};
    vec3 ambient_floor{0.0F, 0.0F, 0.0F}; // keeps the darkest reflections up
  } ball;

  // Saucer head shading (see `shade_head`).
  struct head_params {
    vec3 ambient{0.10F, 0.11F, 0.13F};
    vec3 sun{1.0F, 0.96F, 0.88F};
    vec3 base_albedo{1.0F, 1.0F, 1.0F}; // bare steel (the cone and belly)

    // Dome canopy tint, plus the dome cap's own steel albedo, kept separate
    // from `base_albedo` so the dome can be darkened to pop without dimming
    // the rest of the hull. Split at the same `rr` boundary as the hex grid
    // (`dome_hex_extent`).
    vec3 canopy{0.16F, 0.20F, 0.28F};
    vec3 dome_albedo{0.0F, 0.0F, 0.0F};

    // Geodesic (Goldberg) hex grid on the dome cap: an icosahedron subdivided
    // to `dome_hex_freq` (near-uniform hexagons plus twelve pentagons),
    // reoriented so a cell lands on the eye, `dome_hex_phase` rotating the
    // grid about the eye to align its hexagons with the porthole.
    int dome_hex_freq = 9;           // icosahedron subdivision frequency
    float dome_hex_line = 0.01F;     // seam half-width, radians
    float dome_hex_strength = 3.25F; // how much the seams darken the dome
    float dome_hex_extent = 0.463F;  // rr out to which the grid covers
    float dome_hex_phase = 0.0F;     // grid rotation about the eye, radians

    // A hard black band masking the dome/cone joint. A solid, hard-edged band
    // whose inner edge is pulled `seam_offset` inside the hex grid's
    // `dome_hex_extent` cutoff (to cover the specular-washed dome edge just
    // inside it) and which spans `seam_width`. Drawn like the eye (emissive,
    // with the lit metal and specular removed) so the whole band reads true
    // black instead of washing out under the lighting; reads as a groove the
    // dome sits in, hinting it could separate.
    float seam_offset = 0.017F;        // inner edge of seam
    float seam_width = 0.024F;         // band width (solid, hard-edged)
    vec3 seam_color{0.0F, 0.0F, 0.0F}; // band color (hard black)

    // Cockpit eye: a pupil hub and radial spokes inside an opaque hexagonal
    // iris on the front of the fixed dome, placed relative to the saucer's
    // forward so the dome reads as a single eye. The iris spans two grid
    // cells, so its edge nests flush with the surrounding hex cells. Albedo
    // only, crisp, so it holds up small in the ball reflection.
    float eye_forward = 1.5F; // lean toward the front (0 = at the apex)
    float eye_lean = 1.0F;    // extra forward lean per unit of look-down
    float eye_aim = 0.0F;    // 0..1 blend of the eye toward the look direction
    float eye_hub = 0.09F;   // central hub-circle radius
    int eye_spokes = 6;      // radial spokes inside the iris (try 3)
    float eye_line = 0.018F; // frame, spoke, and hub line width
    vec3 eye_glass{0.003F, 0.016F, 0.049F};    // iris
    vec3 eye_pupil{0.0F, 0.0F, 0.0F};          // hub center (the beam source)
    vec3 eye_frame_color{0.62F, 0.62F, 0.62F}; // frame, spokes, hub ring

    // Belly paint: concentric rings times spinning spokes.
    float ring_frequency = 26.0F;
    float spoke_frequency = 12.0F;
    float paint_base = 0.35F;  // darkest the paint dims the albedo to
    float paint_range = 0.65F; // added back where rings and spokes peak

    // Central flashlight hub.
    float hub_radius = 0.15F;
    float hub_softness = 0.06F;
    vec3 hub_color{1.0F, 0.95F, 0.80F};
    float hub_strength = 2.5F;

    // Amber rim lights: a ring of glowing dots near the belly edge.
    float rim_center = 0.80F; // radius of the ring (fraction of disc radius)
    float rim_width = 14.0F;  // higher = thinner ring
    vec3 rim_color{1.0F, 0.50F, 0.12F};
    float rim_strength = 2.2F;
    float rim_dot_frequency = 16.0F;

    // Propulsion glow: a blue-white engine wash that swells with motion.
    float jet_base = 0.4F;
    float jet_slope = 0.6F;
    vec3 thrust_color{0.308F, 0.358F, 1.06F};
    float thrust_strength = 1.6F;

    // Specular highlight.
    float dome_specular_power = 150.0F; // sharper glint on the dome
    float belly_specular_power = 48.0F; // softer on the belly
    float specular_strength = 0.5F;
  } head;

  // Anti-alias samples per axis in `voxel_kernel`: 1 disables it, 2 to 3 is
  // the useful range. Runtime (not a `constexpr`) so the panel can change it;
  // the cost is the square of this.
  int aa_samples = 2;
};

#pragma endregion

} // namespace corvid::cuda
