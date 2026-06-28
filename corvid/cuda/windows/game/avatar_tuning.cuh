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
  // Body: the metal ball you drive. The motion grid's glow is gated on the
  // direction keys and scaled by the ball's own planar speed:
  // `ball_grid_move_gain` sets how hard it flares up (ramped at
  // `motion_approach`), `ball_grid_fade` how fast it fades back to dark once
  // the keys release. The roll gain scales the literal roll rate (which on a
  // small ball scrolls many cells per second, fast enough to alias into
  // flicker and strobe) down to a readable scroll: `ball_grid_roll_gain` is
  // the gain up to the cruise speed, and it eases toward
  // `ball_grid_roll_gain * ball_grid_roll_gain_fast_mult` as the speed climbs
  // to the sprint top (three times cruise), so the faster roll stays readable
  // without strobing. Easing the gain with the speed instead of switching it
  // on the Shift key keeps a sprint change from jolting the scroll (a 1/3
  // multiple holds the rotation rate level across the sprint).
  // `ball_grid_steer_gain` is a fake: the conveyor stays aligned to
  // the motion, so a steer arc is invisible under the tracking camera, and
  // this drifts the grid sideways by the heading change to sell the turn (0
  // off); `ball_grid_steer_cap` limits that drift (steer-phase per second) so
  // a tight donut, which whips the heading fast, saturates to a readable rate
  // instead of strobing, while normal steering passes through.
  // `ball_grid_turn_rate` is how fast the grid's travel axis eases to a new
  // direction (a strafe, a steer): higher snaps, lower eases.
  float ball_radius = 0.6F;
  float ball_grid_move_gain = 2.0F;
  float ball_grid_fade = 5.0F;
  float ball_grid_roll_gain = 0.125F;
  float ball_grid_roll_gain_fast_mult = 0.75F;
  float ball_grid_steer_gain = 2.0F;
  float ball_grid_steer_cap = 2.0F;
  float ball_grid_turn_rate = 6.0F;

  // Tracks: a shallow groove and dark stain the heavy ball wears into the dirt
  // as it rolls. Only lateral rolling crushes (the edit scales by the distance
  // rolled, not time), so a parked ball settling straight down leaves nothing
  // and speed does not change a single pass; rolling back and forth wears it
  // deeper. `track_crush_strength` is the groove depth (density, world units)
  // per unit rolled and `track_darken_strength` the color fade per unit
  // rolled, each over a `track_crush_radius` footprint; either strength 0
  // disables that half, and `track_darken_floor` is the darkest the stain
  // reaches so it never goes black.
  float track_crush_strength = 0.3F;
  float track_crush_radius = 0.85F;
  float track_darken_strength = 0.666F;
  float track_darken_floor = 0.12F;

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

  // Saucer belly spin: the idle rate, the gains from forward travel and
  // strafing, and the idle-reversal period.
  float spin_rate = -1.5F;       // idle belly spin, radians per second
  float spin_move_gain = -3.0F;  // spin gain from forward travel, signed
  float spin_strafe_gain = 3.0F; // spin gain from strafing, signed
  float spin_idle_period = 4.5F; // seconds between idle spin reversals

  // Saucer tilt: the look gimbal's max nose-down dip, plus the helicopter
  // motion tilt (the saucer banks with its own travel), one angle per
  // direction at full speed.
  float dip_max_deg = 65.0F;       // max nose-down dip on a look down, degrees
  float forward_tilt_deg = 28.0F;  // nose-down tilt at full forward travel
  float backward_tilt_deg = 28.0F; // tail-down tilt at full reverse travel
  float strafe_tilt_deg = 28.0F;   // bank toward the strafe at full strafe

  // Dome shape, as fractions of the head radius (see `saucer_head`).
  float dome_offset = 0.2F;  // dome center height / radius (lower = buried)
  float dome_radius = 0.46F; // dome sphere radius / radius

  // Eye: the look-gimbal lift (the eye aims this far above the look for eye
  // contact) and the steadycam counter-tilt that holds the dome level against
  // the motion bank.
  float eye_lift_deg = 25.0F; // degrees the eye aims above the look
  float stabilize = 1.0F; // dome cancel of the motion bank (1 = hold level)
  float overcomp = 0.15F; // extra dome tilt past level, opposite the bank

  // Antenna standing off the dome top (fractions of the head radius). It wags
  // with the eye's gimbal as an exaggerated tilt signal; `antenna_length` 0
  // disables it.
  float antenna_length = 0.6F;      // rod length / radius
  float antenna_thickness = 0.004F; // rod radius / radius
  float antenna_ball = 0.044F;      // tip ball radius / radius
  float antenna_collar = 0.006F;    // base collar (the metal disc) / radius

  // The antenna's angular lead over the eye toward the dome pole, degrees:
  // sets where its base meets the hex grid (dial it onto a node) and how
  // vertical it stands.
  float antenna_lead_deg = 55.26F;

  // The antenna beacon's blink rate: scales with the Head's planar speed, so
  // it does not pulse at rest and blinks faster the quicker it travels. In
  // cycles (Hz); the colors and the on/off depth live in
  // `render_config::head_params`.
  float blink_move_gain = 2.0F; // blink rate per unit Head speed

  // The resting beacon color smoothly tracks the belly idle spin (a cosine of
  // its period). `color_phase` shifts the color against the spin: 0 in phase
  // (pure color at each reversal), 1 in opposite phase; around 0.5 the color
  // is pure mid-spin and neutral at the reversal.
  float color_phase = 0.5F;

  // Beacon color cycles per belly reversal cycle at rest, so the color tracks
  // the spin at a chosen multiple of its rate instead of locked to it: 1 is
  // identical (which reads wrong), a small whole number above 1 decouples them
  // while staying in tune.
  float color_spin_ratio = 3.0F;

  // Physics: the ball falls under `gravity` and rests on the terrain (see
  // `avatar_rig::settle`). `jump_speed` is the upward launch velocity Space
  // gives when grounded; the jump height is `jump_speed^2 / (2 * gravity)`.
  // Horizontal driving carries momentum (see `avatar_rig::move`): the ground
  // velocity eases toward the input target at `accel_approach` and brakes
  // toward rest at the gentler `brake_approach` when the keys release, so the
  // ball coasts and overshoots a stop; below `coast_min` a coast snaps to rest
  // rather than creeping forever down the exponential tail. `ground_tol` is
  // the contact band: the ball counts as grounded (can jump, has traction)
  // when resting on or skimming within this of the surface, so the flag stays
  // steady and a jump fires reliably even while sprinting over undulating
  // terrain.
  float gravity = 20.0F;   // downward acceleration, units per second squared
  float jump_speed = 8.0F; // upward launch velocity on a grounded jump
  float accel_approach = 0.75F; // ground-velocity ramp toward the input target
  float brake_approach = 4.25F; // ground-velocity decay toward rest (coasting)
  float coast_min = 0.10F;      // speed below which a coast snaps to a stop
  float ground_tol = 0.3F; // contact band counted as grounded, world units

  // The steepest slope the ball drives up: a contact tilted more than this off
  // level is a wall, stopping the ball instead of letting it climb (a vertical
  // face is the limit). Later this may key to the material for per-tier
  // traction.
  float max_climb_deg = 50.0F;

  // How much the climb limit grows while running (Run): the ball can ride up
  // steeper terrain and out of an equator-deep pit when sprinting, where a
  // normal drive is stopped by the wall. 1 disables the boost.
  float run_climb_mult = 1.6F;

  // How much of each frame's collision penetration is corrected: a fraction,
  // not the whole, so the resolve eases to rest instead of overshooting the
  // one-frame-stale probe and limit-cycling (a jitter on a flat floor, a
  // wall-to-wall slam in a slot narrower than the ball). Lower is calmer but
  // sinks deeper into contact; near 1 brings the jitter back. The velocity
  // stops are firm regardless, so a landing or a wall still stops hard.
  float collision_damp = 0.35F;

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

  // Mouse-look de-jitter (the look's One Euro Filter): `look_rest_ms` is the
  // at-rest smoothing time constant in milliseconds (higher is steadier but
  // laggier), `look_beta` how fast that smoothing relaxes as the mouse speeds
  // up (0 leaves it a plain fixed low-pass).
  float look_rest_ms = 150.0F;
  float look_beta = 0.001F;

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
