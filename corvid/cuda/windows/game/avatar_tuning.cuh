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

#include "../../radians.cuh"

// The voxel viewer's avatar tuning: the camera and saucer "feel" constants
// plus the saucer shape, as runtime values so the live config panel can edit
// them with no recompile. Specific to the voxel viewer but kept out of the .cu
// so it stays kernels plus host glue plus main. The panel that edits this is
// in config_panel.cuh; the live shading constants are render_config.cuh.

namespace corvid::cuda {

#pragma region avatar_tuning

// The avatar's "feel" constants as runtime values, so the tuning panel can
// edit them live with no recompile. The defaults are the hand-tuned values the
// rig shipped with; a default-constructed instance is the baseline the panel
// compares each edited field against.
struct avatar_tuning {
  float ball_radius = 0.6F;
  float head_radius = 0.45F;
  float head_height = 0.9F;      // head hover height over the ball
  float camera_height = 0.5F;    // eye height above head center, of the radius
  float boom_min = -1.5F;        // pushed this far in front (FPS)
  float boom_max = 14.0F;        // pulled this far back (wide)
  float boom_rise = 0.35F;       // head rise per unit pulled back
  float zoom_approach = 8.0F;    // boom easing rate per second
  float spin_rate = -1.5F;       // idle belly spin, radians per second
  float spin_move_gain = -3.0F;  // spin gain from forward travel, signed
  float spin_idle_period = 6.0F; // seconds between idle spin reversals
  float saucer_lean = 0.4F;      // belly lean toward the look
  float move_tilt = 1.0F;        // forward nose-down lean into travel at speed
  float back_tilt = 0.5F;        // backward tilt as a fraction of the forward
  float heading_approach = 8.0F; // heading swing rate while moving
  float motion_approach = 5.0F;  // tilt/spin motion-signal ramp/fade rate
  float move_speed = 8.0F;       // planar move speed, units per second

  // Saucer head shape, as fractions of the head radius (see `saucer_head`).
  float body_height = 0.32F; // disc half-height / radius (smaller = flatter)
  float dome_offset =
      0.2F; // dome center height / radius (lower = more buried)
  float dome_radius = 0.46F; // dome sphere radius / radius
  float dome_blend = 0.005F; // dome/disc smooth-union width / radius
  float top_height = 0.47F;  // top-cone apex height / radius (< body = a cone)
  float rim_round = 0.03F;   // brim smooth-intersection rounding / radius

  // Debug: rotate the head's front (and the cockpit eye) off the camera
  // heading, in degrees, to inspect the back of the dome in the mirror without
  // a mirror behind. Also useful for a head-shaking animation.
  float front_offset_deg = 0.0F;

  // Field of view is stored with its derived tan(fov/2), so the per-frame ray
  // setup reads the cached tangent instead of recomputing it. Edit it through
  // `set_fov_deg`, which refreshes the cache.
  [[nodiscard]] float fov_deg() const { return fov_deg_; }
  [[nodiscard]] float tan_half_fov() const { return tan_half_fov_; }
  void set_fov_deg(float deg) {
    fov_deg_ = deg;
    tan_half_fov_ = tanf(deg * radians::per_degree * 0.5F);
  }

private:
  float fov_deg_ = 60.0F; // vertical field of view, degrees
  float tan_half_fov_ = tanf(60.0F * radians::per_degree * 0.5F);
};

#pragma endregion

} // namespace corvid::cuda
