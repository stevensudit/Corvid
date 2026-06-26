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
  // Body: the metal ball you drive.
  float ball_radius = 0.6F;

  // Head: the overall saucer head size (the camera rides inside it).
  float head_radius = 0.45F;

  // Saucer shape, as fractions of the head radius (see `saucer_head`).
  float disc_height = 0.32F; // disc half-height / radius (smaller = flatter)
  float top_height = 0.47F;  // top-cone apex height / radius (< disc = a cone)
  float rim_round = 0.03F;   // brim smooth-intersection rounding / radius
  float dome_blend = 0.005F; // dome/disc smooth-union width / radius

  // Caps the head's silhouette hit tolerance (fraction of radius): lower
  // sharpens the far-mirror edge, too low reopens the dome/disc seam.
  float head_hit_cap = 0.002F;

  // Saucer belly spin: the idle rate, the gain from forward travel, and the
  // idle-reversal period.
  float spin_rate = -1.5F;       // idle belly spin, radians per second
  float spin_move_gain = -3.0F;  // spin gain from forward travel, signed
  float spin_idle_period = 6.0F; // seconds between idle spin reversals

  // Saucer tilt: the look gimbal's max nose-down dip, plus the helicopter
  // motion tilt (the saucer banks with its own travel), one angle per
  // direction at full speed.
  float dip_max_deg = 65.0F;       // max nose-down dip on a look down, degrees
  float forward_tilt_deg = 28.0F;  // nose-down tilt at full forward travel
  float backward_tilt_deg = 62.0F; // tail-down tilt at full reverse travel
  float strafe_tilt_deg = 55.0F;   // bank toward the strafe at full strafe

  // Dome shape, as fractions of the head radius (see `saucer_head`).
  float dome_offset = 0.2F;  // dome center height / radius (lower = buried)
  float dome_radius = 0.46F; // dome sphere radius / radius

  // Eye: the look-gimbal lift (the eye aims this far above the look for eye
  // contact) and the steadycam counter-tilt that holds the dome level against
  // the motion bank.
  float eye_lift_deg = 15.0F; // degrees the eye aims above the look
  float stabilize = 1.0F; // dome cancel of the motion bank (1 = hold level)
  float overcomp = 0.15F; // extra dome tilt past level, opposite the bank

  // Antenna standing off the dome top (fractions of the head radius). It wags
  // with the eye's gimbal as an exaggerated tilt signal; `antenna_length` 0
  // disables it.
  float antenna_length = 0.5F;      // rod length / radius
  float antenna_thickness = 0.004F; // rod radius / radius
  float antenna_ball = 0.044F;      // tip ball radius / radius
  float antenna_collar = 0.006F;    // base collar (the metal disc) / radius

  // Movement: how the rig follows the body, dollies, zooms, and frames it.
  float move_speed = 8.0F;       // planar move speed, units per second
  float head_height = 0.9F;      // head hover height over the ball
  float camera_height = 0.5F;    // eye height above head center, of the radius
  float boom_min = 0.485F;       // jockey: this far behind, above the ball
  float boom_max = 14.0F;        // trailing: pulled this far back (wide)
  float boom_rise = 0.35F;       // head rise per unit pulled back
  float zoom_approach = 8.0F;    // boom easing rate per second
  float zoom_step = 1.0F;        // boom change per mouse-wheel notch
  float heading_approach = 8.0F; // steer heading-chase rate while moving
  float follow_approach = 3.0F;  // follow look-recenter rate (gentler)
  float motion_approach = 5.0F;  // tilt/spin motion-signal ramp/fade rate

  // Animation rigging: rotate the head's front (and the cockpit eye) off the
  // camera heading, in degrees. Mainly to animate the UFO shaking its head;
  // also handy when debugging to bring the back of the dome into the mirror.
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
