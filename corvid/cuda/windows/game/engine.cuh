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
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <cuda_runtime.h>

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
    advance_avatar(dt);

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

    dig(rays, dt);
    probe_ground();
    render_frame(rays, ball, head);
    return true;
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
          digging_);
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
    return dt;
  }

  // Advance the avatar from this frame's input: smooth the look, drive the
  // ball, settle it on the terrain, aim the zoom, and animate.
  void advance_avatar(float dt) {
    // While a panel widget owns the keyboard (a Ctrl+click value entry), its
    // key-up goes to ImGui, not the game, so a movement key held into it would
    // stick. Release them so the avatar stops when the UI takes over; moving
    // with the panel open otherwise still works.
    if (imgui_.wants_keyboard()) input_.release_keys();

    // Smooth the look before moving so a strafe uses this frame's facing.
    const auto [yaw, pitch] = input_.look(dt);
    rig_.look(yaw, pitch);

    // The panel below toggles `lock_position_`, read a frame late (harmless
    // for a debug control); `move` then holds the body but still feeds the
    // step to the tilt.
    rig_.locked = lock_position_;
    const auto [fwd, strafe] = input_.movement(rig_.tune.move_speed);
    rig_.move(fwd, strafe, input_.fast, dt);

    // Gravity and ground contact. Read back the ground probe the previous
    // frame launched under the ball: one frame stale, but issued after that
    // frame's dig so it already reflects the edit, and ready with no stall
    // since the present has since synced. `settle` then drops the ball, rests
    // it on the surface, applies any jump, and fences it inside the box.
    if (ground_primed_)
      cuda_last_status{
          cudaMemcpy(&ground_state_, ground_target_.get(),
              sizeof(ground_probe), cudaMemcpyDeviceToHost)}
          .or_throw();
    rig_.settle(ground_state_, input_.take_jump(), world_min_, world_max_, dt);

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

  // Refresh the title bar's fps/ms readout once a stats window passes a
  // second.
  void update_title(float dt) {
    if (const auto report = stats_.record(dt)) {
      char title[160];
      SDL_snprintf(title, sizeof(title),
          "Corvid Voxel Viewer - %.0f fps  %.1f/%.1f/%.1f ms (min/avg/max)  "
          "GPU %.1f ms",
          report->fps, report->min_ms, report->avg_ms, report->max_ms,
          gpu_ms_);
      SDL_SetWindowTitle(win_, title);
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

  // Carve the field at the crosshair while the dig button is held. The pick
  // records the hit in device memory and the brush reads it there, so the dig
  // stays on the GPU; the next frame's march shows the hole.
  //
  // The center ray still aims the dig; the cursor-driven in-world reticle that
  // replaces it is the next step.
  void dig(const camera_rays& rays, float dt) {
    if (!digging_) return;
    pick_kernel<<<1, 1>>>(field_, rays.eye, rays.frame.forward, dig_target_);
    dig_kernel<<<dig_grid_, dig_block_>>>(volume_.surface(), field_,
        dig_target_, dig_radius_, dig_rate_ * dt);
  }

  // Probe the ground under the ball for next frame's `settle`. Issued after
  // the dig so it samples the freshly edited field (a hole the ball is
  // standing over drops it in); the host reads it back at the top of the next
  // frame. On the default stream this stays ordered after the dig and before
  // the render, both of which only read the field.
  void probe_ground() {
    ground_probe_kernel<<<1, 1>>>(field_, rig_.anchor, ground_target_.get());
    ground_primed_ = true;
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
          render_defaults_, freeze_camera_, lock_position_, uncap_fps_);

    const int sync_interval = present_sync_interval(win_, uncap_fps_);

    presenter_
        .render(
            [&](cudaArray_t array, int w, int h) {
              const dim3 grid_dim{cuda_kernel::ceil_div(w, block_.x),
                  cuda_kernel::ceil_div(h, block_.y)};
              cuda_surface surf{array};
              // Time just the kernel so the title can show GPU ms against the
              // whole-frame ms (GPU-bound vs CPU-bound).
              {
                cuda_timer gpu_timer{gpu_ms_};
                voxel_kernel<<<grid_dim, block_>>>(surf,
                    resolution{static_cast<float>(w), static_cast<float>(h)},
                    rays, field_, colors_.texture(), ball, head, mirror_,
                    render_cfg_);
              }
              cuda_timer::synchronize().or_throw();
            },
            [&] { imgui_.render(presenter_.back_buffer()); }, sync_interval)
        .or_throw();
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

#pragma endregion
#pragma region Window and input

  // SDL first so the window (and the rest) can be created against it.
  sdl::sdl_subsystem sdl_;
  sdl::sdl_window win_{"Corvid Voxel Viewer", 1280, 720,
      sdl::sdl_window_flags::resizable};
  std::string geom_path_;
  sdl::drive_input input_;
  sdl::frame_stats stats_;

  // The left mouse button, while held, digs at the crosshair.
  bool digging_ = false;

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

  // A flat mirror wall along the -z border (which the avatar faces at start),
  // edge to edge in x and rising from the floor, so the saucer can be seen
  // undistorted, unlike in the convex ball. Fly up to it to preen.
  const flat_mirror mirror_{.plane_z = oz_,
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

  // Ground-probe scratch.
  //
  // A one-element device buffer the `ground_probe_kernel` writes under the
  // ball each frame (the surface normal and signed distance), read back to the
  // host the next frame to settle the ball onto the terrain. `ground_state_`
  // starts as no-contact (far above), so the first frame just falls;
  // `ground_primed_` gates the readback until the first probe has been issued.
  cuda_ptr<ground_probe> ground_target_;
  ground_probe ground_state_{.normal = vec3{0.0F, 1.0F, 0.0F},
      .surface_dist = no_contact};
  bool ground_primed_ = false;

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

  // The live shading config (sky, terrain, ball, head, anti-alias), edited by
  // the panel and passed to the kernel each frame; the defaults instance is
  // the baseline the panel compares against.
  render_config render_cfg_;
  const render_config render_defaults_{};

#pragma endregion
#pragma region Presentation and debug toggles

  static constexpr dim3 block_{16, 16};

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
  // one, else one per 8 vsyncs so a backgrounded viewer idles without spinning
  // up the GPU fan. `uncap` drops the foreground interval to 0 (present
  // uncapped) for benchmarking, effective only on a tearing-capable swapchain;
  // backgrounded still throttles so an idle viewer does not spin the GPU fan.
  [[nodiscard]] static int present_sync_interval(SDL_Window* win, bool uncap) {
    constexpr int background_sync = 8;
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
