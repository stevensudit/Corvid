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

#include <algorithm>
#include <cmath>
#include <numbers>

#include "../../../math/arithmetic.h"
#include "../../camera.cuh"
#include "../../radians.cuh"
#include "../../vec.cuh"
#include "./avatar.cuh"
#include "./avatar_body.cuh"
#include "./avatar_tuning.cuh"
#include "./field_ops.cuh"
#include "./render_config.cuh"
#include "./voxel_render.cuh"

// The player's avatar rig: the host-side state that drives the ball and seats
// the saucer head each frame, and builds the `metal_ball`, `saucer_head`, and
// `camera_rays` the renderer consumes.
//
// The drawn avatar SDFs live in avatar.cuh; this is the simulation that poses
// them.

namespace corvid::cuda {

#pragma region Avatar

// The player's avatar rig: the ball anchor the player drives and the saucer
// head the camera rides inside.
//
// The camera is always the head, so the head is never drawn in the view; you
// see it only reflected in the ball. A Warcraft- style zoom slides the head
// along the yaw-forward axis relative to the ball: pulled back and raised for
// an over-the-shoulder trailing view, or drawn in to the jockey, close above
// and behind the ball, so a level look sees past it and looking down gives its
// profile.
//
// The head never goes in front. WASD drives the ball along its heading and the
// mouse wheel zooms. Holding the right button aims the eye: with no movement
// keys it is free Look; while driving it is Steer, the heading chasing the eye
// so the ball arcs toward where you look.
//
// Releasing it Follows: the ball keeps its heading and the view rotates to
// frame the travel. The head flies to its seat at a bounded speed, so it never
// snaps around the boom.
//
// The ball falls under gravity and rests on the terrain. The rigid-body
// simulation (`avatar_body`, stepped by the engine) owns that motion; the rig
// reads its result through `drive_from_body` for rendering, the head, and the
// camera.
struct avatar_rig {
#pragma region State

  pos3 anchor{};        // ball center, what the player drives
  orientation facing{}; // yaw/pitch look direction
  radians heading{};   // yaw the head sits along; tracks the look while moving
  float boom{};        // head distance behind the ball, jockey to trailing
  float boom_target{}; // where the wheel's taking the boom; `update` eases
  float terrain_clear = big_value; // clear distance along the boom axis
  vec3 ground_vel{};               // horizontal velocity, synced from the body
  float vel_y{};                   // vertical velocity, synced from the body
  bool grounded{}; // on floor (can jump, has traction), synced from body
  bool walled{};   // a wall at the equator (buried to the waist)
  bool running{};  // sprinting (Run) this frame, synced from the body
  bool confined{}; // a ceiling overhead: in a too-short tunnel (for the dolly)
  pos3 wall_contact{};  // where the ball pressed a wall this frame, for stain
  float wall_press{};   // lateral travel a wall blocked this frame (0 if none)
  float spin{};         // saucer belly rotation, advanced by `update`
  float drive{};        // smoothed signed forward head speed, clamped (-1..1)
  float slide{};        // smoothed signed strafe head speed, clamped (-1..1)
  float drive_raw{};    // forward head speed, unclamped (sprint past 1)
  float slide_raw{};    // strafe head speed, unclamped (sprint past 1)
  float spin_clock{};   // time accumulator for the idle spin reversal
  float blink_phase{};  // antenna beacon blink phase, in cycles [0..1)
  float idle_dir{1.0F}; // smoothed idle spin direction (+/-1)
  float moving{}; // this frame's planar ball travel, set by `drive_from_body`
  float wheel_spin{};   // this frame's wheel roll, set by `drive_from_body`
  bool driving{};       // whether a movement key commanded the ball this frame
  pos3 prev_eye{};      // last frame's head position, for the head velocity
  bool primed{};        // whether `prev_eye` holds a real previous frame
  vec3 head_offset{};   // eased head offset from the ball, flown to the seat
  bool head_primed{};   // whether `head_offset` holds a real seated offset
  bool frozen{};        // observer freeze: camera pinned, head shown
  pos3 frozen_cam{};    // the pinned camera position while `frozen`
  basis frozen_frame{}; // the pinned camera look while `frozen`
  bool locked{};        // treadmill: hold the body, animate as if moving
  vec3 locked_step{}; // the step withheld this frame while locked (treadmill)
  vec3 ball_roll_axis{0.0F, 0.0F, 1.0F}; // set by `drive_from_body`
  float ball_roll_phase{};  // accumulated roll angle, scrolls the grid
  float ball_roll_blur{};   // this frame's roll-phase sweep, the blur length
  float ball_steer_phase{}; // accumulated steer fake, drifts it sideways
  float ball_glow{};        // eased motion-grid intensity (0 at rest)
  avatar_tuning tune{};     // live feel constants

#pragma endregion
#pragma region Geometry

  // The orthonormal view basis for the current facing.
  [[nodiscard]] basis frame() const {
    const float cos_pitch = cos(facing.pitch);
    const vec3 forward{cos(facing.yaw) * cos_pitch, sin(facing.pitch),
        sin(facing.yaw) * cos_pitch};
    const vec3 right = normalize(cross(forward, camera::world_up));
    return {forward, right, cross(right, forward)};
  }

  // The yaw-only forward, flattened to the ground plane, for movement and head
  // placement that ignore pitch.
  [[nodiscard]] vec3 ground_forward() const {
    const basis b = frame();
    return normalize(vec3{b.forward.x, 0.0F, b.forward.z});
  }

  // The head's seat offset from the ball: behind it along the heading by the
  // boom and raised by the rise.
  //
  // The boom runs from the jockey (`boom_min`, above and slightly behind) to
  // the trailing distance (`boom_max`) and never goes in front. The heading,
  // not the live look, seats the head, so free-look turns the camera without
  // orbiting the head around the ball.
  //
  // The head's actual offset eases toward this (`update`), so the head
  // translates with the ball one-to-one (never lagging the drive, however
  // fast), while a heading swing or boom dolly glides the offset rather than
  // snapping it around the long boom.
  //
  // Physics seam (deferred): a low-ceiling tunnel will be the one exception
  // that shifts the head in front, and only at the jockey. That waits on a
  // physics pass; until then the boom is always behind.
  [[nodiscard]] vec3 head_seat_offset() const {
    const float rise = tune.head_height + (boom * tune.boom_rise);
    const vec3 heading_fwd{cos(heading), 0.0F, sin(heading)};
    return (camera::world_up * rise) - (heading_fwd * boom);
  }

  // The head position, which is also the eye: the ball plus the eased head
  // offset.
  //
  // The offset carries all the smoothing, so the head tracks the ball's
  // translation exactly and only a heading swing or boom dolly glides.
  [[nodiscard]] pos3 eye() const { return anchor + head_offset; }

  // The camera boom probe axis: a ray from the head's seat base (above the
  // ball) out along the dolly direction, up by `boom_rise` and back along the
  // heading.
  //
  // The engine raymarches the terrain along it once a frame; `terrain_clear`
  // is how far it stays clear, which `boom_axis_limit` turns into the boom
  // clamp that auto-merges the camera when the seat will not fit.
  struct boom_axis {
    pos3 origin;
    vec3 dir; // unit
  };
  [[nodiscard]] boom_axis boom_probe_ray() const {
    const vec3 heading_fwd{cos(heading), 0.0F, sin(heading)};
    const vec3 d = (camera::world_up * tune.boom_rise) - heading_fwd;
    return {anchor + (camera::world_up * tune.head_height), normalize(d)};
  }

  // The largest boom the terrain allows before the head seat enters dirt, in
  // boom units, from the last `terrain_clear` probe (one frame stale).
  //
  // The seat advances `sqrt(1 + boom_rise^2)` world units per boom unit along
  // the probe axis (the `boom_probe_ray` direction's length); back off by the
  // head radius so the saucer keeps clear of the wall. 0 forces a full merge.
  // `update` eases the boom toward the smaller of this and the player's zoom
  // target, so the dolly clamps short in a tunnel and springs back out in the
  // open.
  [[nodiscard]] float boom_axis_limit() const {
    const float dlen = sqrtf(1.0F + (tune.boom_rise * tune.boom_rise));
    return fmaxf(0.0F, (terrain_clear - tune.head_radius) / dlen);
  }

#pragma endregion
#pragma region Body sync

  // Pose the rig from the physical body for rendering.
  //
  // The `avatar_body` simulation owns the position, velocity, and spin; this
  // reads them into the rig's state so the head, camera, and the rolling
  // motion grid follow it, and `update` then animates from them. The wireframe
  // scrolls at the body's true roll rate, its axis along the body's spin, so
  // the spin the body carries drives the grid rather than a travel-derived
  // fake.
  void drive_from_body(const avatar_body& body, bool driving, bool running,
      float dt) {
    anchor = body.center;
    ground_vel = vec3{body.velocity.x, 0.0F, body.velocity.z};
    vel_y = body.velocity.y;
    grounded = body.grounded;
    this->driving = driving;
    this->running = running;
    // The body actually moves, so there is no withheld treadmill step; the
    // locked path (`advance_body_locked`) overrides this right after, so a
    // held step never lingers into a normal drive.
    locked_step = vec3{};
    const vec3 ground{ground_vel.x * dt, 0.0F, ground_vel.z * dt};
    moving = length(ground);
    // The wheel's own surface roll, not the ground travel: it over-spins past
    // `moving` when the body slips (revving stuck), so the grid lights and
    // spins even when the ball is barely moving.
    wheel_spin = length(body.angular_velocity) * body.params.radius * dt;
    if (const float omega = length(body.angular_velocity); omega > 1.0e-6F) {
      // Ease the grid's flow axis toward the spin direction rather than
      // snapping to it, so the small frame-to-frame wander of the spin at low
      // speed (course corrections, the stale probe) is low-passed into a
      // steady flow instead of a visible wobble. `ball_grid_turn_rate` is the
      // cutoff: higher follows a real turn faster, lower suppresses more of
      // the jitter. Only the axis is eased; the scroll rate and phase still
      // track the true spin. The length guard keeps the prior axis through a
      // near-reversal, where easing across the flip would cross zero.
      const vec3 target = body.angular_velocity * (1.0F / omega);
      const float ease = 1.0F - expf(-tune.ball_grid_turn_rate * dt);
      if (const vec3 axis =
              ball_roll_axis + ((target - ball_roll_axis) * ease);
          length(axis) > 1.0e-4F)
        ball_roll_axis = normalize(axis);
      ball_roll_blur = omega * dt * tune.ball_grid_roll_gain;
      ball_roll_phase = fmodf(ball_roll_phase + ball_roll_blur, 1.0F);
    } else {
      ball_roll_blur = 0.0F;
    }
  }

#pragma endregion
#pragma region Control

  // Orbit the look by yaw/pitch deltas, clamping pitch shy of vertical.
  void look(float yaw, float pitch) {
    facing.yaw += radians{yaw};
    facing.pitch = std::clamp(facing.pitch + radians{pitch},
        -camera::max_pitch, camera::max_pitch);
  }

  // Aim the zoom: a positive delta (wheel up) targets a smaller boom, toward
  // the jockey and, past it, into the ball (a merge).
  //
  // The target floors at 0, the fully merged eye, so wheeling in past the
  // jockey (`boom_min`) glides the camera into the ball's glass-lens viewpoint
  // (see `cam_pos`). The head only glides there in `update`, so it reads as
  // the saucer moving rather than snapping.
  void zoom(float delta) {
    boom_target = std::clamp(boom_target - delta, 0.0F, tune.boom_max);
  }

#pragma endregion
#pragma region Animation

  // Advance the frame.
  //
  // Ease the boom toward its zoom target, turn the heading and look while the
  // ball moves (steering swings the heading toward the eye so the ball arcs
  // that way; following eases the look onto the heading and the pitch to level
  // so the view frames the travel), fly the head toward its seat at a bounded
  // speed, derive the head's own velocity (so a zoom dolly tilts the head too)
  // into the eased drive/slide that lean the saucer, and spin the belly:
  // travel-driven while moving, alternating while idle.
  void update(float dt, bool looking) {
    // Auto-merge: the terrain ratchets the zoom target IN (a tunnel pulls the
    // head into the ball when the seat will not fit) but never back out, so
    // leaving the tunnel leaves you merged until you choose to dolly out, the
    // distortion your cue to. A manual wheel-in always works (it only lowers
    // the same target); a wheel-out in a tunnel is undone next frame, which is
    // fine since there is no room to dolly into. The probe only ever lowers
    // the target, so it cannot fight the head back out as the view angle
    // shifts the one-frame-stale reading.
    //
    // The limit is floored to the wheel-notch grid, so crowding only ever
    // lands the head on a slot the mouse wheel itself reaches, never a partial
    // spot between them. The sole sub-jockey slot is 0 (full merge), so there
    // is no resting partial merge: you are either at a trailing slot (outside)
    // or all the way inside, and a hole tight enough to crowd past the closest
    // slot merges you fully rather than parking the eye half in the glass.
    const bool merged_before = merged();
    const float step = fmaxf(tune.zoom_step, 0.01F);
    boom_target =
        std::min(boom_target, floorf(boom_axis_limit() / step) * step);
    boom += (boom_target - boom) * (1.0F - expf(-tune.zoom_approach * dt));

    // Dollying back out of the body points the view down at the ball, so it
    // reads as backing out of it and naturally looking right at it.
    if (merged_before && !merged())
      facing.pitch = radians{tune.merge_exit_pitch_deg * radians::per_degree};

    if (moving > 0.0F) {
      if (looking) {
        // Steer: the heading chases the eye, so the ball arcs around the head
        // toward where you look, then drives that way once they line up.
        const float rate = 1.0F - expf(-tune.heading_approach * dt);
        const radians delta = radians_atan2(sin(facing.yaw - heading),
            cos(facing.yaw - heading));
        heading += delta * rate;
        // Fake the turn on the motion grid.
        //
        // The conveyor stays motion-aligned, so the arc is invisible under the
        // tracking camera; drift the grid sideways by this frame's heading
        // change.
        //
        // Wrap to the grid's sideways period (sqrt3 cells, an integer roll at
        // integer `hex_freq`) so it stays precise without a visible jump. The
        // drift is capped to `ball_grid_steer_cap` (steer-phase per second): a
        // tight donut whips the heading fast enough that the raw drift would
        // slide the grid faster than the eye reads it (a wagon-wheel strobe),
        // so it saturates to a readable rate there while passing normal
        // steering through untouched.
        const float steer_step =
            (delta * rate).value * tune.ball_grid_steer_gain;
        const float steer_cap = tune.ball_grid_steer_cap * dt;
        ball_steer_phase = fmodf(
            ball_steer_phase + fmaxf(-steer_cap, fminf(steer_step, steer_cap)),
            std::numbers::sqrt3_v<float>);
      } else {
        // Follow: the heading holds, so the ball keeps its course; the look
        // eases gently onto the heading and the pitch to level, rotating the
        // view to frame the travel without whipping.
        const float rate = 1.0F - expf(-tune.follow_approach * dt);
        const radians delta = radians_atan2(sin(heading - facing.yaw),
            cos(heading - facing.yaw));
        facing.yaw += delta * rate;
        facing.pitch *= (1.0F - rate);
      }
    }

    // Ease the head offset toward its seat, capped at the ball's own move
    // speed.
    //
    // The head can never reposition around the ball faster than the ball
    // itself travels, so a heading swing or boom dolly glides rather than
    // whipping the camera around the boom. This caps the head's speed relative
    // to the ball only; the ball's translation carries through `eye`
    // untouched, so driving never lags the head out of range. First frame just
    // seats it.
    const vec3 seat = head_seat_offset();
    if (!head_primed) {
      head_offset = seat;
      head_primed = true;
    } else {
      const vec3 to = seat - head_offset;
      const float dist = length(to);
      const float step = tune.move_speed * dt;
      head_offset = (dist <= step) ? seat : head_offset + (to * (step / dist));
    }

    // The tilt and spin read the head's own velocity (the ball-follow, the
    // zoom dolly, and turning all move the head), measured in the heading
    // frame: forward/back drives the nose tilt and the spin, strafe banks it.
    // Eased, and normalized so full move speed reads as 1.
    const float ramp = 1.0F - expf(-tune.motion_approach * dt);
    const pos3 here = eye();
    if (primed && dt > 0.0F) {
      // Add the step the locked treadmill withheld, so a held body still reads
      // as moving; `locked_step` is zero when not locked.
      const vec3 vel = ((here - prev_eye) + locked_step) * (1.0F / dt);
      const vec3 heading_fwd{cos(heading), 0.0F, sin(heading)};
      const vec3 heading_right{-sin(heading), 0.0F, cos(heading)};
      const float fwd_v = dot(vel, heading_fwd) / tune.move_speed;
      const float side_v = dot(vel, heading_right) / tune.move_speed;
      // `drive`/`slide` clamp to +/-1 because the tilt and color saturate at
      // full move speed; `drive_raw`/`slide_raw` keep the unclamped signed
      // speed (a sprint pushes them past 1) so the belly spin and the beacon
      // blink track true speed.
      const float drive_target = fmaxf(-1.0F, fminf(fwd_v, 1.0F));
      const float slide_target = fmaxf(-1.0F, fminf(side_v, 1.0F));
      drive += (drive_target - drive) * ramp;
      slide += (slide_target - slide) * ramp;
      drive_raw += (fwd_v - drive_raw) * ramp;
      slide_raw += (side_v - slide_raw) * ramp;
    }
    prev_eye = here;
    primed = true;

    // The Head's planar speed (forward and strafe), saturated to 1 at full
    // move speed.
    //
    // It quiets the idle spin and, in `head`, gates the beacon's on/off blink
    // off at rest and blends its moving versus idle color. The unclamped
    // `move_mag` (which a sprint pushes past 1) sets the blink rate.
    const float speed = fminf(1.0F, fabsf(drive) + fabsf(slide));

    // The belly spin.
    //
    // While moving it follows travel (faster ahead, reversing when backing up;
    // strafing spins it opposite ways left versus right) on the unclamped
    // speed, so a sprint spins it faster too; while stationary it alternates
    // direction every `spin_idle_period`, easing through each reversal, which
    // reads livelier than a constant idle spin.
    spin_clock += dt;
    const float idle_target =
        fmodf(spin_clock, 2.0F * tune.spin_idle_period) < tune.spin_idle_period
            ? 1.0F
            : -1.0F;
    idle_dir += (idle_target - idle_dir) * ramp;
    const float idle = tune.spin_rate * idle_dir * (1.0F - speed);
    spin +=
        (idle + (tune.spin_move_gain * drive_raw) +
            (tune.spin_strafe_gain * slide_raw)) *
        dt;

    // The antenna beacon's on/off blink phase, in cycles.
    //
    // It advances at a rate scaled by the unclamped planar speed, so it does
    // not pulse at rest and runs faster the quicker the Head travels, sprint
    // included. `head` turns the phase into the 0..1 waveform; the wrap keeps
    // the accumulator bounded.
    const float move_mag =
        sqrtf((drive_raw * drive_raw) + (slide_raw * slide_raw));
    blink_phase =
        fmodf(blink_phase + ((tune.blink_move_gain * move_mag) * dt), 1.0F);

    // The ball's motion grid glow.
    //
    // Gated on `driving` (a movement key held this frame), so a head dolly or
    // steer with no keys shows nothing, and releasing the keys drops the
    // target to dark at once rather than trailing the ball's momentum coast.
    // While driving it scales with `wheel_spin`, the wheel roll (not the
    // actual travel), so a ball revving stuck in a pit still lights and spins.
    // It flares up at `motion_approach` and fades back at `ball_grid_fade`, so
    // the two rates can be matched; the hex wireframe shows only while a key
    // is held.
    const float ball_speed = (dt > 0.0F) ? wheel_spin / dt : 0.0F;
    const float glow_target =
        driving
            ? fminf(1.0F,
                  (ball_speed / tune.move_speed) * tune.ball_grid_move_gain)
            : 0.0F;
    const float glow_rate =
        (glow_target > ball_glow) ? tune.motion_approach : tune.ball_grid_fade;
    ball_glow += (glow_target - ball_glow) * (1.0F - expf(-glow_rate * dt));
  }

#pragma endregion
#pragma region Camera

  // The merge fraction: 0 when the boom sits at the jockey (`boom_min`) or
  // further out (not merged), rising to 1 when the boom reaches 0 (the eye
  // fully merged at the ball center). It runs opposite to `boom` itself, which
  // is 0 at the merge and `boom_min` at the jockey; this is the normalized
  // amount merged, not a position. `cam_pos` uses it to glide the eye from the
  // jockey seat into the ball, the always-valid glass-lens viewpoint.
  [[nodiscard]] float merge_t() const {
    if (boom >= tune.boom_min || tune.boom_min <= 0.0F) return 0.0F;
    return (tune.boom_min - boom) / tune.boom_min;
  }

  // Whether the eye has dollied inside the ball (the merged glass-lens view).
  [[nodiscard]] bool merged() const { return merge_t() > 0.0F; }

  // The camera position.
  //
  // The eye raised above the head's center by `camera_height` (of the head
  // radius) so the camera looks out from up in the dome rather than the
  // middle; otherwise the dome-heavy saucer reflects into the top of the
  // frame. World up, not the tilted disc normal, so the viewpoint does not
  // swim as the saucer banks.
  //
  // Merged (the boom dollied past the jockey), the eye instead glides to the
  // off-center opening on the look axis, so the forward ray meets the glass at
  // normal incidence and stays clean while the periphery refracts.
  // `frame().forward` carries the pitch, so looking down sinks the eye toward
  // the ball bottom while it stays inside. There is no stable partial merge
  // (crowding snaps the boom to a wheel slot, see `update`), so the only
  // resting merged state is fully inside, where the eye orbits well within the
  // ball as you look; the blend below is only crossed transiently while
  // dollying.
  [[nodiscard]] pos3 cam_pos() const {
    const pos3 trailing =
        eye() + (camera::world_up * (tune.camera_height * tune.head_radius));
    const float m = merge_t();
    if (m <= 0.0F) return trailing;
    const pos3 inside =
        anchor - (frame().forward * (tune.merge_eye_back * tune.ball_radius));
    const float s = m * m * (3.0F - (2.0F * m)); // smoothstep the blend
    return trailing + ((inside - trailing) * s);
  }

  // Enter or leave the observer freeze.
  //
  // On the off-to-on edge it pins the camera position and look; while on,
  // `rays` holds both fixed, so the mouse and movement keys drive the avatar
  // (turning and moving the ship in front of a fixed camera) rather than
  // steering the view. The head is also drawn in the primary view; see
  // `render_config::show_head`.
  void set_frozen(bool on) {
    if (on && !frozen) {
      frozen_cam = cam_pos();
      frozen_frame = frame();
    }
    frozen = on;
  }

  // The view rays for the current pose, looking out from inside the head, or
  // from the pinned camera while the observer freeze holds (position and look
  // both fixed, so the mouse turns the ship instead of the view).
  //
  // The field of view's `tan(fov/2)` is cached in `tune` and recomputed only
  // when the field of view is edited, so this stays a plain read.
  [[nodiscard]] camera_rays rays() const {
    if (frozen) return {frozen_cam, frozen_frame, tune.tan_half_fov()};
    return {cam_pos(), frame(), tune.tan_half_fov()};
  }

#pragma endregion
#pragma region Drawables

  [[nodiscard]] metal_ball ball() const {
    return {anchor, tune.ball_radius, ball_roll_axis, ball_roll_phase,
        ball_steer_phase, ball_glow, ball_roll_blur};
  }

  // The articulated look gimbal.
  //
  // The camera looks freely along `facing`; the eye, dome, and saucer tilt
  // computed here are only drawn (seen reflected in the ball), never the view
  // itself. Everything is built in the look's vertical meridian: `f_h` is the
  // look heading on the ground, `world_up` the vertical. `front_offset_deg`
  // (animation rigging) swings the meridian off the look heading about the
  // vertical, to bring the back of the dome into the mirror.
  [[nodiscard]] saucer_head head(const render_config::head_params& hp) const {
    const vec3 up_w = camera::world_up;
    vec3 f_h = ground_forward();
    if (tune.front_offset_deg != 0.0F) {
      const radians a{tune.front_offset_deg * radians::per_degree};
      const vec3 axis = normalize(cross(f_h, up_w)); // horizontal, off heading
      f_h = normalize((f_h * cos(a)) - (axis * sin(a)));
    }

    // The eye's angular half-extent on the dome, from the iris size alone (the
    // iris apothem is three geodesic-cell apothems). The dome's rotation
    // limits and the saucer's tilt limits both derive from it, so there are no
    // knobs.
    const radians eye_ang{
        3.0F * geodesic_eye_cell_of(hp.dome_hex_freq).apothem};
    constexpr radians rest = 30.0_deg; // eye sits this far up the dome at rest
    constexpr radians pole = 90.0_deg; // straight up, the north pole
    const radians lead{tune.antenna_lead_deg * radians::per_degree};
    const radians lift{tune.eye_lift_deg * radians::per_degree};
    const radians dip_max{tune.dip_max_deg * radians::per_degree};

    // The eye's travel on the dome, edge to edge.
    //
    // The dome sphere's lower part is submerged in the disc, so the visible
    // cap ends where the sphere emerges: the circle at the disc's top height
    // on the sphere, whose dome elevation (asin of that height over the dome
    // radius) is the seam. The lower limit keeps the eye's edge above it so
    // the eye never dips out of sight behind the disc; the upper keeps its
    // edge below the pole. Geometry only (disc_height, dome_offset,
    // dome_radius), independent of the shader's seam decal, so retuning the
    // seam never moves the eye limit.
    const float seam_h = fmaxf(
        fminf((tune.disc_height - tune.dome_offset) / tune.dome_radius, 1.0F),
        -1.0F);
    const radians dome_max = pole - eye_ang;
    const radians dome_min =
        std::min(radians_asin(seam_h) + eye_ang, dome_max);

    // The eye aims `lift` above the look.
    //
    // The camera rides above the eye, so a level aim reads as looking low, and
    // the lift restores eye contact in the mirror. It sits `rest` higher than
    // its aim on the dome, so a level look rests the saucer dipped by `rest`
    // (nosed down), showing the dome in profile from the jockey. The dome
    // rotates the eye within its travel; past that the saucer takes over the
    // dip, nosing up until the eye reaches straight up and down only as far as
    // `dip_max`.
    const radians aim = facing.pitch + lift;
    const radians dome = std::clamp(aim + rest, dome_min, dome_max);
    const radians tilt = std::clamp(aim - dome, -dip_max, eye_ang);
    const radians eye_elev = dome + tilt; // the eye's world aim, = look + lift

    // The two drawn directions in the look meridian: the disc normal (the
    // saucer tilt) and the eye direction. The antenna is built from the final
    // eye below, after the motion-tilt cap, so it keeps its fixed lead
    // exactly.
    const vec3 up = normalize((up_w * cos(tilt)) - (f_h * sin(tilt)));
    const vec3 eye_dir = (f_h * cos(eye_elev)) + (up_w * sin(eye_elev));

    // Helicopter motion tilt.
    //
    // The disc banks with travel (the eased `drive`/`slide`, measured in the
    // heading frame), on top of the look tilt and independent of it. Forward
    // leans the top toward the heading (front dips), reverse leans it back,
    // strafe banks it into the strafe, like a helicopter. The pitch is negated
    // because a positive rotation about `h_right` tilts the normal away from
    // the heading.
    //
    // The dome (eye, grid, antenna) counter-rotates against this by a factor
    // of `1 - stabilize - overcomp`: at full stabilize it holds level (a
    // steadycam) while the disc banks under it, and `overcomp` pushes it a
    // touch past level, opposite the bank, for show. A rigid bank (`stabilize`
    // 0, `overcomp` 0) lets the dome ride the disc.
    const vec3 h_fwd{cos(heading), 0.0F, sin(heading)};
    const vec3 h_right{-sin(heading), 0.0F, cos(heading)};
    const radians pitch_m{
        -drive *
        (drive >= 0.0F ? tune.forward_tilt_deg : tune.backward_tilt_deg) *
        radians::per_degree};
    const radians roll_m{slide * tune.strafe_tilt_deg * radians::per_degree};

    // Bank a direction by fraction `k` of the full motion tilt: pitch then
    // roll, in the heading frame.
    const auto bank_by = [&](vec3 v, float k) {
      return rotate_about(rotate_about(v, h_right, pitch_m * k), h_fwd,
          roll_m * k);
    };
    const float dome_factor = 1.0F - tune.stabilize - tune.overcomp;
    const vec3 saucer_up = bank_by(up, 1.0F);
    vec3 dome_up = bank_by(up, dome_factor);
    vec3 eye_banked = bank_by(eye_dir, dome_factor);

    // The saucer takes its full motion tilt, so reverse can raise its front
    // past level and show the belly; the eye is then clamped onto the visible
    // dome rather than the tilt being capped to keep it there.
    //
    // Holding the steadycam eye level lets a hard bank carry it past the
    // dome's edge, where the disc rim would hide it, so clamp its elevation
    // over the banked disc to [dome_min, dome_max] and rotate the whole dome
    // (grid and antenna with it) back onto the cap. The eye then dips to the
    // seam on reverse and rides to the pole on forward while the disc tilts
    // fully under it.
    //
    // Measure the eye's elevation over the banked disc in the look meridian,
    // so it climbs monotonically through the pole. `asin(dot)` instead peaks
    // at 90 degrees and folds back, so a hard forward bank carrying the eye
    // toward the saucer's pole toggled the clamp on and off and jerked the
    // eye. `disc_fwd` is the in-disc forward, taken straight from the bank
    // axis `h_right` so it can never collapse (projecting the heading onto the
    // disc does, once a look-down plus a forward bank stand the disc on edge).
    // `mer` is the meridian normal, the stable axis the clamp rotates the
    // whole dome about.
    const vec3 disc_fwd = normalize(cross(saucer_up, h_right));
    const vec3 mer = normalize(cross(disc_fwd, saucer_up));
    const radians eye_phi =
        radians_atan2(dot(eye_banked, saucer_up), dot(eye_banked, disc_fwd));

    // The motion clamp's lower bound sits below the look gimbal's `dome_min`,
    // nearer the dome/disc seam, so the eye can dip further down the dome on a
    // reverse bank (its iris edge entering the seam) rather than stopping a
    // full iris above it. The look gimbal keeps the higher `dome_min` for
    // framing.
    const radians clamp_floor = radians_asin(seam_h) + (eye_ang * 0.5F);
    const radians eye_clamped = std::clamp(eye_phi, clamp_floor, dome_max);
    if (eye_clamped != eye_phi) {
      const radians fix = eye_clamped - eye_phi;
      dome_up = rotate_about(dome_up, mer, fix);
      eye_banked = rotate_about(eye_banked, mer, fix);
    }

    // The antenna keeps its fixed `lead` over the eye, rotated toward the dome
    // pole along their shared meridian, so the 60-degree offset stays exact
    // through the bank and clamp.
    const vec3 antenna_banked =
        rotate_about(eye_banked, normalize(cross(eye_banked, dome_up)), lead);

    // The disc nose in the banked disc plane: the look heading lifted into the
    // disc plane (perpendicular to the pre-bank `up`) and banked exactly like
    // the disc, so it stays unit and perpendicular to `saucer_up` even at a
    // full nose-down dip, where projecting the horizontal heading onto the
    // disc would collapse. The hull decals anchor to it.
    const vec3 disc_nose =
        bank_by((f_h * cos(tilt)) + (up_w * sin(tilt)), 1.0F);

    // The beacon animation handed to the shader.
    //
    // The blink waveform from the phase, how much the Head is backing up
    // (reddens the beacon while moving), the planar speed (gates the on/off
    // blink off at rest and blends the moving versus idle color), and the idle
    // color selector tied to the belly idle spin so the beacon alternates in
    // tune with it at rest. Strafing does not redden the beacon: only backing
    // up is special.
    const float blink = 0.5F + (0.5F * sin(radians{blink_phase * two_pi_v<>}));
    const float reversing = fmaxf(0.0F, -drive);
    const float speed = fminf(1.0F, fabsf(drive) + fabsf(slide));

    // The idle color selector.
    //
    // A cosine over the beacon color cycle, the belly reversal cycle divided
    // by `color_spin_ratio`, so the resting beacon shifts color in tune with
    // the spin but at a chosen multiple of its rate (1 locks them identical,
    // which reads wrong). `color_phase` shifts the color against the spin (0
    // in phase, 1 opposite); `fmodf` keeps the cosine's argument bounded.
    const float cycle = 2.0F * tune.spin_idle_period;
    const float color_cycle = cycle / fmaxf(tune.color_spin_ratio, 0.01F);
    const float frac = fmodf(spin_clock, color_cycle) / color_cycle;
    const float idle_smooth =
        cos(radians{(frac + (tune.color_phase * 0.5F)) * two_pi_v<>});
    const float idle_mix = 0.5F - (0.5F * idle_smooth);

    return {eye(), saucer_up, dome_up, disc_nose, tune.head_radius, spin,
        tune.disc_height, tune.dome_offset, tune.dome_radius, tune.dome_blend,
        tune.top_height, tune.rim_round, eye_banked, antenna_banked,
        tune.antenna_length, tune.antenna_thickness, tune.antenna_ball,
        tune.antenna_collar, blink, reversing, speed, idle_mix,
        tune.head_hit_cap};
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
