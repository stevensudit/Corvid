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

#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>

#include <cuda_runtime.h>

#include "../../../math/arithmetic.h"
#include "../../camera.cuh"
#include "../../cuda_event.cuh"
#include "../../cuda_kernel.cuh"
#include "../../cuda_ptr.cuh"
#include "../../cuda_status.cuh"
#include "../../cuda_surface.cuh"
#include "../../cuda_volume.cuh"
#include "../../density_field.cuh"
#include "../../material_volume.cuh"
#include "../../mirror.cuh"
#include "../../vec.cuh"
#include "../cuda_d3d11_presenter.cuh"
#include "../imgui_overlay.h"
#include "./avatar.cuh"
#include "./avatar_body.cuh"
#include "./avatar_rig.cuh"
#include "./avatar_tuning.cuh"
#include "./config_panel.cuh"
#include "./field_ops.cuh"
#include "./render_config.cuh"
#include "./render_kernel.cuh"
#include "./viewer_input.h"
#include "./world_gen.cuh"
#include "../../../sdl/drive_input.h"
#include "../../../sdl/frame_loop.h"
#include "../../../sdl/frame_stats.h"
#include "../../../sdl/sdl_event.h"
#include "../../../sdl/sdl_subsystem.h"
#include "../../../sdl/sdl_window.h"

// The voxel viewer game engine.
//
// Owns the window, the GPU world and presentation pipeline, the avatar, and
// the live config panel, and drives the whole thing one frame at a time.
// `main` constructs one, calls `init` once, then loops on `tick` until it
// returns false. Everything the old `main` held as a local lives here as a
// data member.

namespace corvid::cuda {

#pragma region engine

class engine {
public:
  engine() { init(); }

  engine(const engine&) = delete;
  engine& operator=(const engine&) = delete;

  ~engine() { save_window_geometry(); }

#pragma region Tick

  // Run one frame: pump input, advance the avatar, edit and probe the field,
  // render, and present. Returns false once the window is closing.
  [[nodiscard]] bool tick() {
    if (!handle_events()) return false;
    const float dt = frame_dt();

    // Freeze the simulation while another window holds focus (alt-tab): the dt
    // cap alone still let gravity drift the buried ball downward across the
    // gap. `frame_dt` already advanced the clock, so refocusing resumes at a
    // normal step rather than one huge catch-up. The scene still renders
    // (throttled) so the window does not go stale.
    const bool active = focused();
    if (active) advance_avatar(dt);

    // Skip all GPU work when the window has no client area (minimized, or
    // collapsed by the OS during a mixed-DPI cross-monitor drag).
    if (!presenter_.window_width() || !presenter_.window_height()) {
      SDL_Delay(16);
      return true;
    }

    update_title(dt);
    sync_settings();

    // The per-frame scene inputs, shared by the dig and the render.
    const camera_rays rays = rig_.rays();
    const metal_ball ball = rig_.ball();
    const saucer_head head = rig_.head(render_cfg_.head);

    // The flashlight is a headlamp: it rides the eye and points along the
    // view.
    render_cfg_.flashlight.enabled = flashlight_on_;
    render_cfg_.flashlight.origin = rays.eye;
    render_cfg_.flashlight.direction = rays.frame.forward;

    // Tunnel-view sanity: at the jockey, clear a bubble around the ball in the
    // primary march so dirt between the close camera and the ball does not
    // bury the view (`shade_primary_ray`). Render-only.
    render_cfg_.jockey_clear = rig_.boom <= (rig_.tune.boom_min + 0.05F);

    if (active) {
      if (flatten_requested_) {
        flatten_terrain();
        flatten_requested_ = false;
      }
      if (tunnels_requested_) {
        dig_test_tunnels();
        tunnels_requested_ = false;
      }
      update_reticle(rays, ball, dt);
      dig(dt);
      crush_track();
      probe_ground();
      probe_boom();
    }
    render_frame(rays, ball, head);
    return true;
  }

  // Whether the window currently holds input focus. False while alt-tabbed
  // away or minimized; the tick freezes the simulation when it is.
  [[nodiscard]] bool focused() const {
    return (SDL_GetWindowFlags(win_) & SDL_WINDOW_INPUT_FOCUS) != 0;
  }

#pragma endregion
#pragma region Frame steps
private:
  // Pump and route this frame's input, returning false when the window is
  // closing. A resize grows the swapchain; Escape toggles the config panel
  // (freeing the OS cursor, which the next right-button press re-grabs).
  [[nodiscard]] bool handle_events() {
    const auto action = sdl::pump_events([&](const sdl::sdl_event& ev) {
      return handle_viewer_event(ev, imgui_, show_config_, input_, win_,
          digging_, active_tool_, flashlight_on_);
    });
    if (action == sdl::frame_action::quit) return false;
    if (action == sdl::frame_action::resize) presenter_.resize().or_throw();
    if (action == sdl::frame_action::menu) {
      show_config_ = !show_config_;
      if (show_config_) win_.set_relative_mouse_mode(false).or_throw();
    }
    return true;
  }

  // Advance the frame clock and return the seconds since the last frame.
  // Nanoseconds, because millisecond ticks quantize dt enough to judder
  // movement at high refresh rates (a 6.94 ms frame reads as 6 or 7).
  [[nodiscard]] float frame_dt() {
    const auto now_ns = SDL_GetTicksNS();
    const auto dt = static_cast<float>(now_ns - last_ns_) / 1.0e9F;
    last_ns_ = now_ns;
    // Cap the step so a stalled frame does not integrate a huge dt. Dragging
    // the title bar (Windows enters a modal move loop), a breakpoint, or an
    // alt-tab freezes the loop for a long beat; without this, gravity over
    // that gap flings the avatar far below the world in one step, where
    // collision can never recover (the camera rides it down, so the screen
    // goes to sky). The sim hitches by at most this instead.
    constexpr float max_dt = 0.05F; // 50 ms; a long stall advances only this
    return fminf(dt, max_dt);
  }

  // Advance the avatar from this frame's input: smooth the look, drive the
  // ball, settle it on the terrain, aim the zoom, and animate.
  void advance_avatar(float dt) {
    // While a panel widget owns the keyboard (a Ctrl+click value entry), its
    // key-up goes to ImGui, not the game, so a movement key held into it would
    // stick. Release them so the avatar stops when the UI takes over; moving
    // with the panel open otherwise still works.
    if (imgui_.wants_keyboard()) input_.release_keys();

    // Smooth the look before moving so a strafe uses this frame's facing. The
    // filter tuning is live, so sync it from the rig before consuming the
    // look.
    input_.look_filter.set_params(rig_.tune.look_rest_ms, rig_.tune.look_beta);
    const auto [yaw, pitch] = input_.look(dt);
    rig_.look(yaw, pitch);

    // The panel below toggles `lock_position_`, read a frame late (harmless
    // for a debug control); `move` then holds the body but still feeds the
    // step to the tilt.
    rig_.locked = lock_position_;

    // Read back the ground probe the previous frame launched under the ball:
    // one frame stale, but issued after that frame's dig so it already
    // reflects the edit, and ready with no stall since the present has since
    // synced. The body settles against it.
    if (ground_primed_) ground_target_.store(ground_state_).or_throw();

    // Read back the boom probe the previous frame launched along the camera
    // dolly axis, into the rig's `terrain_clear`. The rig clamps the boom to
    // it, auto-merging the camera into the ball where the seat will not fit.
    if (boom_primed_) boom_clear_.store(rig_.terrain_clear).or_throw();

    // Drive the avatar with the rigid body: it owns its position and velocity
    // and feeds the rig for rendering, the head, and the motion grid.
    advance_body(dt);
    log_collision();

    // The mouse wheel aims the zoom between the trailing distance and the
    // jockey: an impulse, not a held velocity, so it is not scaled by frame
    // time. The head then glides toward that target in `update`, so a zoom
    // slides the saucer in or out rather than snapping. The per-notch step is
    // live-tuned, so sync it from the rig before consuming the wheel.
    input_.scroll_step = rig_.tune.zoom_step;
    if (input_.wheel != 0.0F) rig_.zoom(input_.dolly());

    // Ease the boom toward its zoom target and turn the saucer's belly spin.
    rig_.update(dt, input_.looking);
  }

  // Drive the avatar with the rigid body.
  //
  // The body owns its position and velocity; this builds its contact from the
  // same one-frame-stale ground probe the rig uses, drives it from the input,
  // applies the probe's wall and ceiling push (the single floor contact alone
  // misses those), fences it in the world box, and reads its state back into
  // the rig for the camera, head, and motion grid. The treadmill (lock
  // position) holds the body in place while still animating; see
  // `advance_body_locked`.
  void advance_body(float dt) {
    // Seed the body from the rig's spawn pose once at startup; the rig is then
    // kept in sync from the body every frame after.
    if (!body_primed_) {
      body_.center = rig_.anchor;
      body_.velocity = rig_.ground_vel + (body_up * rig_.vel_y);
      body_.angular_velocity = vec3{};
      body_.grounded = rig_.grounded;
      body_primed_ = true;
    }
    // Keep the collision radius matched to the drawn ball; the body's other
    // constants are its own (tuned in the panel), not mirrored from the rig.
    body_.params.radius = rig_.tune.ball_radius;

    // The floor contact, from the stale probe (the same penetration band as
    // the rig's grounded test).
    const float pen = rig_.tune.ball_radius - ground_state_.surface_dist;
    const body_contact contact{.touching = pen > -rig_.tune.ground_tol,
        .normal = ground_state_.normal,
        .penetration = pen};

    // The drive command in world space: the input's heading-frame target as a
    // fraction of the body's drive force (1 walking, up to 3 running).
    const auto [fwd, strafe] = input_.movement();
    const vec3 hfwd{cos(rig_.heading), 0.0F, sin(rig_.heading)};
    const vec3 hright{-sin(rig_.heading), 0.0F, cos(rig_.heading)};
    const vec3 drive = (hfwd * fwd) + (hright * strafe);
    const bool driving = (fabsf(fwd) + fabsf(strafe)) > 1.0e-4F;

    // Treadmill (lock position): hold the body in front of the mirror, but let
    // its velocity and spin keep evolving under the drive so every
    // motion-driven visual animates as the live physics would.
    if (rig_.locked) {
      advance_body_locked(drive, driving, dt);
      return;
    }

    body_.advance(contact, drive, input_.jump, dt);

    // Lift out of a wall or ceiling the center floor contact missed, damped
    // like the rig (the probe is stale and sparse), and stop the drive into a
    // wall, recording it for the stain. `confined` is left for the camera.
    body_.center += ground_state_.push * rig_.tune.collision_damp;
    rig_.walled = length(ground_state_.wall_normal) > 0.5F;
    rig_.confined = ground_state_.overhead;
    rig_.wall_press = 0.0F;
    if (rig_.walled)
      if (const float into = dot(body_.velocity, ground_state_.wall_normal);
          into < 0.0F)
      {
        body_.velocity -= ground_state_.wall_normal * into;
        rig_.wall_press = -into * dt;
        rig_.wall_contact =
            body_.center - (ground_state_.wall_normal * rig_.tune.ball_radius);
      }

    fence_body();
    rig_.drive_from_body(body_, driving, input_.fast, dt);
  }

  // Run the body for the treadmill: evolve velocity and spin under the drive
  // so the motion visuals animate, but hold the position (the distance to the
  // mirror stays fixed for inspection), defeat gravity, and keep it grounded.
  // The withheld horizontal step feeds the rig's tilt and spin through
  // `locked_step`, the channel `update` reads for them; the jump is suppressed
  // (a treadmill does not launch). No wall or fence resolve, since the body
  // does not move.
  //
  // The contact is a synthetic level floor, not the real probe: the treadmill
  // must always give the drive traction (so it responds to the keys and to
  // Run, drags toward a cruise, and brakes to rest when released) no matter
  // where the avatar is posed or what the stale probe reports beneath it. A
  // treadmill is a flat surface, and the point is to watch the body's own
  // response to the drive, not the terrain under it.
  void advance_body_locked(vec3 drive, bool driving, float dt) {
    const body_contact flat{.touching = true,
        .normal = body_up,
        .penetration = 0.0F};
    const pos3 held = body_.center;
    body_.advance(flat, drive, false, dt);
    body_.center = held;
    body_.velocity.y = 0.0F;
    body_.grounded = true;
    rig_.drive_from_body(body_, driving, input_.fast, dt);
    rig_.locked_step =
        vec3{body_.velocity.x * dt, 0.0F, body_.velocity.z * dt};
  }

  // Clamp the body one radius inside the world box, killing the velocity into
  // a face it hits (the body has no world bounds of its own).
  void fence_body() {
    const float r = body_.params.radius;
    const float cx =
        fminf(fmaxf(body_.center.v.x, world_min_.x + r), world_max_.x - r);
    const float cy =
        fminf(fmaxf(body_.center.v.y, world_min_.y + r), world_max_.y - r);
    const float cz =
        fminf(fmaxf(body_.center.v.z, world_min_.z + r), world_max_.z - r);
    if (cx != body_.center.v.x) body_.velocity.x = 0.0F;
    if (cy != body_.center.v.y) body_.velocity.y = 0.0F;
    if (cz != body_.center.v.z) body_.velocity.z = 0.0F;
    body_.center.v = vec3{cx, cy, cz};
  }

  // Refresh the title bar's fps/ms readout once a stats window passes a
  // second.
  void update_title(float dt) {
    if (const auto report = stats_.record(dt)) {
      // Planar speed of the avatar (world units per second), the speedometer.
      const float speed = sqrtf(
          (rig_.ground_vel.x * rig_.ground_vel.x) +
          (rig_.ground_vel.z * rig_.ground_vel.z));
      // The aim's elevation above horizontal, so sighting up a tunnel reads
      // its grade off the title bar.
      const float aim_deg = rig_.facing.pitch.value / radians::per_degree;
      std::array<char, 224> title;
      SDL_snprintf(title.data(), title.size(),
          "Corvid Voxel Viewer - %.0f fps  %.1f/%.1f/%.1f ms (min/avg/max)  "
          "GPU %.1f ms  spd %.1f  aim %+.0fdeg",
          report->fps, report->min_ms, report->avg_ms, report->max_ms, gpu_ms_,
          speed, aim_deg);
      SDL_SetWindowTitle(win_, title.data());
    }
  }

  // Apply the panel's per-frame settings before the scene is built: the
  // observer freeze (pins the camera and draws the head, otherwise hidden from
  // primary rays) and the live march tunables the kernel reads. Both panel
  // toggles are read a frame late, imperceptible for debug controls.
  void sync_settings() {
    rig_.set_frozen(freeze_camera_);
    render_cfg_.show_head = freeze_camera_;
    field_.march_lipschitz = render_cfg_.march.lipschitz;
    field_.march_max_step_voxels = render_cfg_.march.max_step_voxels;
    field_.march_max_steps = render_cfg_.march.max_steps;
  }

  // Pick the aim point and update the in-world target reticle.
  //
  // The reticle (and digging) live only in look/steer mode, where the aim is
  // the centered camera ray: targeting is centered, the free cursor never
  // fights the view, and a centered target keeps the coming dig beam simple.
  // Running (Shift) also hides it, to keep it out of the way at speed and make
  // the player slow down to dig. When shown, cast the center ray into the
  // field, record the hit point into `dig_target_` for both the reticle and
  // the brush, and read it back to drive the reticle. Also advances its slow
  // spin.
  void
  update_reticle(const camera_rays& rays, const metal_ball& ball, float dt) {
    render_cfg_.reticle.spin = fmodf(
        render_cfg_.reticle.spin + (render_cfg_.reticle.spin_rate * dt),
        two_pi_v<>);

    if (active_tool_ != active_tool::dig || !input_.looking || input_.fast) {
      render_cfg_.reticle.enabled = false;
      pick_state_.hit = false;
      dig_blocked_ = false;
      return;
    }

    const vec3 aim = rays.frame.forward;
    pick_kernel<<<1, 1>>>(field_, rays.eye, aim, dig_target_);
    dig_target_.store(pick_state_).or_throw();
    render_cfg_.reticle.enabled = pick_state_.hit;

    // The dig is blocked when the ball occludes the aim or the target is out
    // of range, both of which drop the inner crosshair and disable the brush;
    // the outer ring still shows the footprint, nudging the player to dolly to
    // the jockey or close in. The dig beam leaves the ball, so it cannot fire
    // through the ball when the aim ray meets it nearer than the terrain; and
    // digging is a close-range action, so a hit past `max_dig_distance` (from
    // the eye) is also refused.
    const float t_ball = ball.intersect(rays.eye, aim);
    const float t_hit =
        pick_state_.hit ? length(pick_state_.point - rays.eye) : big_value;
    const bool aim_through_ball = (t_ball >= 0.0F) && (t_ball < t_hit);
    // Range is measured from the ball (the digger), not the eye (the trailing
    // camera), so the reach does not change as the camera dollies in and out.
    const bool out_of_range =
        pick_state_.hit &&
        length(pick_state_.point - ball.center) >
            render_cfg_.reticle.max_dig_distance;
    dig_blocked_ = aim_through_ball || out_of_range;
    render_cfg_.reticle.show_inner = !dig_blocked_;
    if (pick_state_.hit) {
      render_cfg_.reticle.center = pick_state_.point;
      // The reticle measures its azimuth against the camera's screen axes, so
      // its roll stays steady across terrain creases (where the pick normal
      // jumps); the pick normal is no longer used for the reticle.
      render_cfg_.reticle.view_right = rays.frame.right;
      render_cfg_.reticle.view_up = rays.frame.up;
    }
  }

  // Carve the field at the reticle while the dig tool is on and the left
  // button is held. `update_reticle` already picked the aim point into
  // `dig_target_` this frame, so the brush reads it straight from device
  // memory; the next frame's march shows the hole. A no-op when the tool is
  // off, the button is up, or the aim missed.
  void dig(float dt) {
    if (active_tool_ != active_tool::dig || !digging_ || !pick_state_.hit ||
        dig_blocked_)
      return;
    dig_kernel<<<dig_grid_, dig_block_>>>(volume_.surface(), field_,
        dig_target_, dig_radius_, dig_rate_ * dt);
  }

  // Wear a track into the dirt under the rolling ball.
  //
  // A shallow groove plus a dark stain at the ball's contact, both scaled by
  // this frame's lateral roll (`moving`) so a parked or vertically settling
  // ball leaves nothing and the single-pass depth does not depend on speed.
  // Issued after the dig and before the ground probe, so the probe samples the
  // groove and the ball settles into the track it just wore. A no-op when both
  // halves are off, the ball is airborne, or it barely rolled.
  void crush_track() {
    const avatar_tuning& t = rig_.tune;
    const bool carve = t.track_crush_strength > 0.0F;
    const bool stain = t.track_darken_strength > 0.0F;
    if (!carve && !stain) return;

    // The rolling groove and stain on the floor, scaled by the lateral roll so
    // a parked or vertically settling ball leaves nothing. The brush falloff
    // peaks at the contact under the ball, so it bites a bowl down.
    //
    // Only the geometry groove gates on `driving` (a movement key held). The
    // collision's own micro-jitter and any passive drift set `moving` too, and
    // carving on that would deepen a bowl the ball then slides into, a
    // feedback that drifts and shivers it. The stain has no geometry feedback,
    // so it still follows any roll.
    if (rig_.grounded && rig_.moving > track_move_epsilon_) {
      const pos3 contact = rig_.anchor - (camera::world_up * t.ball_radius);
      // Stop carving once the ball is buried to its equator (`walled`): the
      // weight-dig sinks it only to the waist, then it can rev or hop but not
      // bore itself a shaft. The stain still follows the roll.
      const float carve_amt =
          (rig_.driving && !rig_.walled)
              ? t.track_crush_strength * rig_.moving
              : 0.0F;
      launch_crush(contact, t.track_crush_radius, carve_amt,
          t.track_darken_strength * rig_.moving, t.track_darken_floor);
    }

    // The stain where the ball pushes into a wall, scaled by the travel the
    // wall blocked (collision kills that travel, so `moving` cannot stand in
    // for it). No carve: pushing into a wall stains it but does not dig, since
    // the beam is the tool for boring in.
    if (stain && rig_.wall_press > track_move_epsilon_) {
      launch_crush(rig_.wall_contact, t.track_crush_radius, 0.0F,
          t.track_darken_strength * rig_.wall_press, t.track_darken_floor);
    }
  }

  // Launch the crush brush at `contact`: carve `crush` from the density and
  // stain the color by `darken` over a `radius` footprint (see
  // `crush_kernel`).
  void launch_crush(pos3 contact, float radius, float crush, float darken,
      float darken_floor) {
    const int span =
        (2 * static_cast<int>(std::ceil(radius / voxel_size_))) + 1;
    const dim3 grid{cuda_kernel::ceil_div(span, dig_block_.x),
        cuda_kernel::ceil_div(span, dig_block_.y),
        cuda_kernel::ceil_div(span, dig_block_.z)};
    crush_kernel<<<grid, dig_block_>>>(volume_.surface(), colors_.surface(),
        field_, contact, radius, crush, darken, darken_floor);
  }

  // Append this frame's settle state to the debug CSV while logging is on:
  // position, vertical and ground velocity, the probe's normal, signed
  // distance, push and wall normal, the penetration, and the grounded,
  // overhead, moving, and driving flags. The stream opens (truncating) on the
  // first logged frame and closes when logging is turned off, and each line is
  // flushed so a capture survives a hard quit. Temp instrumentation for
  // diagnosing settle jitter.
  void log_collision() {
    if (!log_collision_) {
      if (collision_log_.is_open()) collision_log_.close();
      return;
    }
    if (!collision_log_.is_open()) {
      if (log_path_.empty()) return;
      collision_log_.open(log_path_, std::ios::trunc);
      collision_log_ << "t,ax,ay,az,vy,gvx,gvz,sdist,nx,ny,nz,pen,pushx,pushy,"
                        "pushz,wnx,wny,wnz,over,gnd,mv,drv\n";
    }
    const ground_probe& g = ground_state_;
    const float pen = rig_.tune.ball_radius - g.surface_dist;
    collision_log_
        << static_cast<double>(SDL_GetTicksNS()) / 1.0e9 << ','
        << rig_.anchor.v.x << ',' << rig_.anchor.v.y << ',' << rig_.anchor.v.z
        << ',' << rig_.vel_y << ',' << rig_.ground_vel.x << ','
        << rig_.ground_vel.z << ',' << g.surface_dist << ',' << g.normal.x
        << ',' << g.normal.y << ',' << g.normal.z << ',' << pen << ','
        << g.push.x << ',' << g.push.y << ',' << g.push.z << ','
        << g.wall_normal.x << ',' << g.wall_normal.y << ',' << g.wall_normal.z
        << ',' << g.overhead << ',' << rig_.grounded << ',' << rig_.moving
        << ',' << rig_.driving << '\n';
    collision_log_.flush();
  }

  // Probe the ground under the ball for next frame's `settle`. Issued after
  // the dig so it samples the freshly edited field (a hole the ball is
  // standing over drops it in); the host reads it back at the top of the next
  // frame. On the default stream this stays ordered after the dig and before
  // the render, both of which only read the field.
  void probe_ground() {
    ground_probe_kernel<<<1, 1>>>(field_, rig_.anchor, rig_.tune.ball_radius,
        ground_target_.get());
    ground_primed_ = true;
  }

  // Probe the terrain along the camera boom axis for next frame's auto-merge,
  // issued after the dig like the ground probe so it sees the freshly edited
  // field. The host reads it back next frame into the rig's `terrain_clear`.
  void probe_boom() {
    const auto [origin, dir] = rig_.boom_probe_ray();
    boom_probe_kernel<<<1, 1>>>(field_, origin, dir, boom_clear_.get());
    boom_primed_ = true;
  }

  // Flatten the whole world to a level plane at the ball's feet: a test track
  // for measuring speeds. Triggered by the panel button
  // (`flatten_requested_`); the dug shape is lost.
  void flatten_terrain() {
    flatten_world(field_, volume_, materials_, colors_,
        rig_.anchor.v.y - rig_.tune.ball_radius);
  }

  // Carve a row of nine straight test tunnels in front of the ball, each a
  // further 10 degrees steeper (10 to 90), all boring the same heading from
  // openings at the ball's foot level: a reproducible grade fixture for the
  // climb/slip tests. Triggered by the panel button (`tunnels_requested_`);
  // flatten first, since it cuts into whatever terrain is there.
  void dig_test_tunnels() {
    constexpr int count = 9;
    constexpr float step = 10.0F * radians::per_degree;
    constexpr float spacing = 5.0F; // between adjacent bore openings
    constexpr float bore_length = 16.0F;
    const float radius =
        rig_.tune.ball_radius * 2.5F;  // clearance over the ball
    const vec3 bore{1.0F, 0.0F, 0.0F}; // heading all bores share
    const vec3 row{0.0F, 0.0F, 1.0F};  // openings spaced along z
    const float surface_y = rig_.anchor.v.y - rig_.tune.ball_radius;
    // The first opening, so the row centers on the ball and sits a little
    // ahead of it along the bore.
    const vec3 first{rig_.anchor.v.x + 3.0F, surface_y,
        rig_.anchor.v.z - (0.5F * static_cast<float>(count - 1) * spacing)};
    dig_tunnels(field_, volume_, pos3{first}, bore, row, spacing, radius,
        bore_length, count, step);
  }

  // Build the config panel, then render the scene and present. The voxel
  // kernel fills the interop target (timed on its own for the title's GPU ms),
  // and the ImGui overlay draws over the backbuffer before the present.
  void render_frame(const camera_rays& rays, const metal_ball& ball,
      const saucer_head& head) {
    // Open the ImGui frame and build the panel. One `begin_frame` pairs with
    // the one `render` below, which runs even with no panel up, so the pairing
    // always balances.
    imgui_.begin_frame();
    if (show_config_)
      draw_config_panel(rig_.tune, tuning_defaults_, render_cfg_,
          render_defaults_, freeze_camera_, lock_position_, uncap_fps_,
          log_collision_, body_.params, body_defaults_, flatten_requested_,
          tunnels_requested_, input_.run_multiplier);

    const int sync_interval = present_sync_interval(win_, uncap_fps_);

    presenter_
        .render(
            [&](cudaArray_t array, int w, int h) {
              const dim3 grid_dim{cuda_kernel::ceil_div(w, block_.x),
                  cuda_kernel::ceil_div(h, block_.y)};
              cuda_surface surf{array};
              ensure_gbuffer(w, h);
              ensure_post_buffers(w, h);
              const resolution res{static_cast<float>(w),
                  static_cast<float>(h)};
              // Time just the render so the title can show GPU ms against the
              // whole-frame ms (GPU-bound vs CPU-bound).
              {
                cuda_timer gpu_timer{gpu_ms_};
                render_scene(hdr_.get(), aa_gbuf_.get(), grid_dim, block_, res,
                    rays, field_, colors_.texture(), ball, head, mirror_,
                    render_cfg_);
                post_process(surf, hdr_.get(), bloom_a_.get(), bloom_b_.get(),
                    block_, res, render_cfg_);
              }
              cuda_timer::synchronize().or_throw();
            },
            [&] { imgui_.render(presenter_.back_buffer()); }, sync_interval)
        .or_throw();
  }

  // Grow the adaptive-AA prepass buffer to cover a `w` x `h` frame (one
  // `aa_texel` per pixel). Grow-only, like the presenter's render target, so a
  // resize down keeps the larger allocation and steady-state frames reuse it
  // without reallocating. The prepass rewrites every texel each frame, so no
  // stale data survives a resize.
  void ensure_gbuffer(int w, int h) {
    const size_t needed = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (needed <= aa_gbuf_count_) return;
    aa_gbuf_ = cuda_ptr<aa_texel>{needed};
    if (!aa_gbuf_)
      throw std::runtime_error{"failed to allocate AA prepass buffer"};
    aa_gbuf_count_ = needed;
  }

  // Grow the post-process buffers to a `w` x `h` frame: the full-res linear
  // HDR target the render writes, and the two half-res bloom ping-pong
  // buffers. Grow-only, like `ensure_gbuffer`; every pixel is rewritten each
  // frame, so a resize down keeps the larger allocation with no stale data.
  void ensure_post_buffers(int w, int h) {
    const size_t needed = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (needed <= post_count_) return;
    const size_t half =
        static_cast<size_t>(bloom_dim(w)) * static_cast<size_t>(bloom_dim(h));
    hdr_ = cuda_ptr<float4>{needed};
    bloom_a_ = cuda_ptr<float4>{half};
    bloom_b_ = cuda_ptr<float4>{half};
    if (!hdr_ || !bloom_a_ || !bloom_b_)
      throw std::runtime_error{"failed to allocate post-process buffers"};
    post_count_ = needed;
  }

#pragma endregion
#pragma region Init
  // Bring up the window geometry, generate the world, and build the GPU
  // presentation pipeline and the ImGui overlay.
  void init() {
    win_.set_minimum_size(640, 480).or_throw();

    // Reopen on the monitor and at the size left last run.
    resolve_geometry_path();
    restore_window_geometry();

    if (!dig_target_) throw std::runtime_error{"failed to allocate dig probe"};
    if (!ground_target_)
      throw std::runtime_error{"failed to allocate ground probe"};
    if (!boom_clear_)
      throw std::runtime_error{"failed to allocate boom probe"};

    // The dig brush's launch dims, from its radius: a cube of voxels around
    // the picked point, the brush sphere clipped out of it in the kernel.
    dig_span_ =
        (2 * static_cast<int>(std::ceil(dig_radius_ / voxel_size_))) + 1;
    dig_grid_ = dim3{cuda_kernel::ceil_div(dig_span_, dig_block_.x),
        cuda_kernel::ceil_div(dig_span_, dig_block_.y),
        cuda_kernel::ceil_div(dig_span_, dig_block_.z)};

    // Generate the world once into the three grids before the first frame.
    generate_world(field_, volume_, materials_, colors_);

    // Bind the presenter to the window, then build the ImGui overlay (which
    // borrows the presenter's device and context, so it is built after).
    presenter_.reset(static_cast<HWND>(win_.native_handle())).or_throw();
    imgui_ = imgui_overlay{win_, presenter_.device(), presenter_.context()};

    last_ns_ = SDL_GetTicksNS();
  }

#pragma endregion
#pragma region World constants

  static constexpr cudaExtent vol_extent_{512, 128, 512};
  static constexpr float voxel_size_ = 0.5F;
  static constexpr float ox_ =
      -0.5F * static_cast<float>(vol_extent_.width - 1) * voxel_size_;
  static constexpr float oy_ =
      -0.5F * static_cast<float>(vol_extent_.height - 1) * voxel_size_;
  static constexpr float oz_ =
      -0.5F * static_cast<float>(vol_extent_.depth - 1) * voxel_size_;
  static constexpr float world_x1_ =
      ox_ + (static_cast<float>(vol_extent_.width - 1) * voxel_size_);
  static constexpr float half_voxel_ = 0.5F * voxel_size_;

  static constexpr float dig_radius_ = 3.0F;
  static constexpr float dig_rate_ = 10.0F;

  // Below this lateral roll (world units in a frame) the ball is treated as
  // not moving, so it wears no track: guards against jitter and the vertical
  // settle, which never enters `moving` anyway.
  static constexpr float track_move_epsilon_ = 1.0e-4F;

#pragma endregion
#pragma region Window and input

  // SDL first so the window (and the rest) can be created against it.
  sdl::sdl_subsystem sdl_;
  sdl::sdl_window win_{"Corvid Voxel Viewer", 1280, 720,
      sdl::sdl_window_flags::resizable};
  std::string geom_path_;
  sdl::drive_input input_;
  sdl::frame_stats stats_;

  // The left mouse button, while held, digs at the reticle.
  bool digging_ = false;

  // The active tool, toggled by the number keys (1 = dig). `none` shows no
  // reticle and disables the dig brush.
  active_tool active_tool_ = active_tool::none;

  // The flashlight headlamp, toggled by the F key. Copied into the render
  // config each frame along with the camera eye and view direction, so the
  // beam rides the camera.
  bool flashlight_on_ = false;

#pragma endregion
#pragma region World grids

  // The density field: a block of voxels centered on the world origin, filled
  // once from the terrain heightfield.
  cuda_volume<float> volume_{vol_extent_};

  // The material grid: one hardness tier per voxel, point-exact (no texture),
  // the source of truth for digging and (later) SDF object flags.
  material_volume materials_{vol_extent_};

  // The color grid is seeded from the material tier at fill but kept separate
  // from the material grid, because color wants filtering while the material
  // id must stay exact.
  color_volume colors_{vol_extent_};

  // Not const: the live march tunables are copied onto it each frame.
  density_field field_{vol_extent_, pos3{vec3{ox_, oy_, oz_}}, voxel_size_,
      volume_.texture()};

  // A flat mirror wall at the z midplane (cutting the world cube in half),
  // edge to edge in x and rising from the floor, so the saucer can be seen
  // undistorted, unlike in the convex ball. Fly up to it to preen.
  const flat_mirror mirror_{
      .plane_z = oz_ + ((static_cast<float>(vol_extent_.depth) - 1.0F) * 0.5F *
                           voxel_size_),
      .lo = vec2{ox_, oy_},
      .hi = vec2{world_x1_, oy_ + 80.0F},
      .normal = vec3{0.0F, 0.0F, 1.0F}};

  // The world box the ball is fenced inside (the same slab `raymarch` clips
  // to: the field runs half a voxel past the first and last voxel centers).
  // `settle` clamps the ball one radius inside this on the ground plane so it
  // cannot roll off the edge of the soil.
  const vec3 world_min_{ox_ - half_voxel_, oy_ - half_voxel_,
      oz_ - half_voxel_};
  const vec3 world_max_{
      ox_ + ((static_cast<float>(vol_extent_.width) - 0.5F) * voxel_size_),
      oy_ + ((static_cast<float>(vol_extent_.height) - 0.5F) * voxel_size_),
      oz_ + ((static_cast<float>(vol_extent_.depth) - 0.5F) * voxel_size_)};

#pragma endregion
#pragma region Editing and collision scratch

  // Digging scratch.
  //
  // A one-element device buffer holding where the center ray last hit, written
  // by pick_kernel and read by dig_kernel, so the dig never round-trips to the
  // host. The brush removes `dig_rate_` of density per second at its center,
  // falling off across a `dig_radius_` sphere.
  cuda_ptr<dig_probe> dig_target_;
  const dim3 dig_block_{8, 8, 8};
  int dig_span_ = 0; // cube edge in voxels, set in `init`
  dim3 dig_grid_{};  // launch grid for the brush cube, set in `init`

  // The last aim pick read back from `dig_target_`: where the aim ray hit,
  // driving the target reticle and gating the dig brush. `hit` is false when
  // the tool is off or the aim missed.
  dig_probe pick_state_{};

  // True when the dig is refused for this aim: the ball occludes the target
  // (the dig beam leaves the ball, so it cannot fire through it) or the target
  // is past `max_dig_distance`. Blocks the dig and hides the inner crosshair
  // while it holds.
  bool dig_blocked_ = false;

  // Ground-probe scratch.
  //
  // A one-element device buffer the `ground_probe_kernel` writes under the
  // ball each frame (the surface normal and signed distance), read back to the
  // host the next frame to settle the ball onto the terrain. `ground_state_`
  // starts as no-contact (far above), so the first frame just falls;
  // `ground_primed_` gates the readback until the first probe has been issued.
  cuda_ptr<ground_probe> ground_target_;
  ground_probe ground_state_{.normal = vec3{0.0F, 1.0F, 0.0F},
      .surface_dist = no_contact,
      .push = {},
      .wall_normal{},
      .overhead{}};
  bool ground_primed_ = false;

  // Boom-probe scratch (auto-merge).
  //
  // A one-element device buffer the `boom_probe_kernel` writes each frame: how
  // far the camera boom axis stays clear of terrain. Read back to the host the
  // next frame into the rig's `terrain_clear`, which clamps the boom so a
  // tunnel merges the camera into the ball. `boom_primed_` gates the readback
  // until the first probe has been issued.
  cuda_ptr<float> boom_clear_;
  bool boom_primed_ = false;

#pragma endregion
#pragma region Avatar and config

  // The avatar starts above the world center, looking toward -z and slightly
  // down, in a trailing view pulled back from the head; gravity drops it onto
  // the terrain on the first frames. The mouse wheel dollies in toward the
  // jockey; the right-drag orbits. The feel constants default from
  // `avatar_tuning`, edited live by the config panel.
  avatar_rig rig_{pos3{vec3{0.0F, 10.0F, 0.0F}},
      orientation{90.0_deg, -20.0_deg}, 90.0_deg, 7.0F, 7.0F};
  const avatar_tuning tuning_defaults_{};

  // The rigid-body physics: the body owns its position and velocity and feeds
  // the rig for rendering. `body_defaults_` is the panel's baseline, and
  // `body_primed_` seeds the body from the rig's spawn pose once at startup.
  avatar_body body_;
  const body_params body_defaults_{};
  bool body_primed_ = false;

  // One-shot: flatten the world to a level test track at the ball's feet, set
  // by a panel button and consumed in `tick`.
  bool flatten_requested_ = false;
  bool tunnels_requested_ = false;

  // The live shading config (sky, terrain, ball, head, anti-alias), edited by
  // the panel and passed to the kernel each frame; the defaults instance is
  // the baseline the panel compares against.
  render_config render_cfg_;
  const render_config render_defaults_{};

#pragma endregion
#pragma region Presentation and debug toggles

  static constexpr dim3 block_{16, 16};

  // The adaptive-AA prepass buffer: one `aa_texel` (hit kind and depth) per
  // pixel, written by the prepass and read by the resolve pass to find the
  // silhouettes worth supersampling. Grown to the render target's size by
  // `ensure_gbuffer`; `aa_gbuf_count_` is its capacity in texels.
  cuda_ptr<aa_texel> aa_gbuf_;
  size_t aa_gbuf_count_ = 0;

  // The linear HDR render target (`hdr_`) the render kernels write, plus the
  // two half-resolution bloom ping-pong buffers, all grown by
  // `ensure_post_buffers`. The post pass blooms and tone maps `hdr_` into the
  // interop surface. `post_count_` is `hdr_`'s capacity in pixels; the bloom
  // buffers hold a quarter of that (half each axis).
  cuda_ptr<float4> hdr_;
  cuda_ptr<float4> bloom_a_;
  cuda_ptr<float4> bloom_b_;
  size_t post_count_ = 0;

  // The GPU presentation pipeline: default-constructed with the engine (a
  // device, no swapchain), then bound to the window in `init` once its
  // geometry is restored, via `reset`. The ImGui overlay borrows the
  // presenter's device and context, so it is move-assigned into place in
  // `init` after the presenter is up. Both are plain members, no optional.
  cuda_d3d11_presenter presenter_;
  imgui_overlay imgui_;
  bool show_config_ = false;

  // Observer freeze (debug): pin the camera and reveal the saucer head, which
  // is otherwise hidden from primary rays since the camera rides inside it.
  // Toggled by a panel checkbox; the avatar stays drivable so it can be moved
  // out in front of the pinned camera and watched.
  bool freeze_camera_ = false;

  // Treadmill (debug): hold the body in place while it still animates as if
  // moving, so the saucer's motion tilt can be watched at a fixed distance
  // from the mirror. Toggled by a panel checkbox.
  bool lock_position_ = false;

  // Benchmark: drop the vsync cap (present with tearing) so the frame rate
  // floats above the refresh rate and the title-bar FPS reads true GPU cost
  // for the current view. Toggled by a panel checkbox; needs a tearing-capable
  // swapchain, else it has no effect.
  bool uncap_fps_ = false;

  // Collision logging (debug): write a per-frame CSV of the ball's settle
  // state to `log_path_` to diagnose jitter. Toggled by a panel checkbox; the
  // stream opens (truncating) on the first logged frame and closes when
  // toggled off. Temp instrumentation.
  bool log_collision_ = false;
  std::ofstream collision_log_;
  std::string log_path_;

  // Frame timing in nanoseconds: millisecond ticks quantize dt enough to
  // judder movement at high refresh rates (a 6.94 ms frame reads as 6 or 7).
  Uint64 last_ns_ = 0;

  // Last frame's GPU kernel time (ms), measured with a `cuda_timer` and shown
  // next to the whole-frame ms so a slowdown can be pinned to the GPU render
  // or to the CPU-side per-frame work.
  float gpu_ms_ = 0.0F;

#pragma endregion
#pragma region Window geometry

  // One present per vsync while the window is the foreground (input-focused)
  // one, else one per 4 vsyncs so a backgrounded viewer idles without spinning
  // up the GPU fan. `uncap` drops the foreground interval to 0 (present
  // uncapped) for benchmarking, effective only on a tearing-capable swapchain;
  // backgrounded still throttles so an idle viewer does not spin the GPU fan.
  [[nodiscard]] static int present_sync_interval(SDL_Window* win, bool uncap) {
    constexpr int background_sync = 4;
    const bool focused =
        (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (!focused) return background_sync;
    return uncap ? 0 : 1;
  }

  // Remember the window's monitor, size, and maximized state between runs, so
  // it reopens where it was left rather than at the default spot on the
  // primary display. Stored as one line "x y w h maximized" under the SDL pref
  // path (created on demand); `geom_path_` is left empty if the pref path is
  // unavailable.
  void resolve_geometry_path() {
    const std::unique_ptr<char, decltype(&SDL_free)> pref{
        SDL_GetPrefPath("Corvid", "VoxelViewer"), SDL_free};
    if (!pref) return;
    // The pref path already ends in a separator, so just append the file name.
    geom_path_ = std::string{pref.get()} + "window.txt";
    log_path_ = std::string{pref.get()} + "collision_log.csv";
  }

  void restore_window_geometry() {
    if (geom_path_.empty()) return;
    std::ifstream in{geom_path_};
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int maximized = 0;
    if (!(in >> x >> y >> w >> h >> maximized)) return;
    SDL_SetWindowSize(win_, w, h);
    SDL_SetWindowPosition(win_, x, y);
    if (maximized) SDL_MaximizeWindow(win_);
  }

  void save_window_geometry() {
    if (geom_path_.empty()) return;
    const bool maximized =
        (SDL_GetWindowFlags(win_) & SDL_WINDOW_MAXIMIZED) != 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    SDL_GetWindowPosition(win_, &x, &y);
    SDL_GetWindowSize(win_, &w, &h);
    std::ofstream out{geom_path_, std::ios::trunc};
    out << x << ' ' << y << ' ' << w << ' ' << h << ' ' << (maximized ? 1 : 0)
        << '\n';
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
