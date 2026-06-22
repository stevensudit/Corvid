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

#include "./vec.cuh"

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
    vec3 base_albedo{0.55F, 0.58F, 0.62F}; // bare steel

    // Dome canopy: a cooler tint with faint concentric panel ridges.
    vec3 canopy{0.16F, 0.20F, 0.28F};
    float panel_frequency = 18.0F;
    float panel_amplitude = 0.15F;

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
    float dome_specular_power = 96.0F;  // sharper glint on the dome
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
