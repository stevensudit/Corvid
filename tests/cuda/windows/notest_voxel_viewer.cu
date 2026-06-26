// CUDA-driven voxel ray marcher (Windows-only CUDA cell, crossplatform.md
// section 11), the second 3D rung after the SDF raymarch viewer. A density
// field lives in VRAM as a 3D texture; a kernel fixed-step marches it per
// pixel to the first solid voxel, shades it, and writes the result into the
// interop texture through `cudaGraphicsD3D11*`; D3D copies it to the
// backbuffer and presents. The frame stays on the GPU. Unlike the SDF viewer's
// analytic scene, the geometry is sampled from the field, so it can be edited
// in place (the next rung). The player is an avatar of a metallic ball and a
// saucer head: WASD drives the ball (Space/Ctrl raise and lower it, Shift goes
// faster), holding the right mouse button aims the eye (Look, or Steer while
// driving), and the mouse wheel dollies the head between its trailing distance
// and the jockey (close above and behind the ball). Escape quits.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <numbers>
#include <print>
#include <stdexcept>
#include <string_view>

#include <cuda_runtime.h>

#include "corvid/cuda/camera.cuh"
#include "corvid/cuda/cuda_event.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_surface.cuh"
#include "corvid/cuda/cuda_volume.cuh"
#include "corvid/cuda/density_field.cuh"
#include "corvid/cuda/material_volume.cuh"
#include "corvid/cuda/mirror.cuh"
#include "corvid/cuda/radians.cuh"
#include "corvid/cuda/raycast.cuh"
#include "corvid/cuda/strata.cuh"
#include "corvid/cuda/terrain.cuh"
#include "corvid/cuda/vec.cuh"
#include "corvid/cuda/windows/cuda_d3d11_presenter.cuh"
#include "corvid/cuda/windows/game/avatar.cuh"
#include "corvid/cuda/windows/game/avatar_tuning.cuh"
#include "corvid/cuda/windows/game/config_panel.cuh"
#include "corvid/cuda/windows/game/render_config.cuh"
#include "corvid/cuda/windows/game/scene_render.cuh"
#include "corvid/cuda/windows/imgui_overlay.h"
#include "corvid/infra/scope_exit.h"
#include "corvid/sdl/fly_input.h"
#include "corvid/sdl/frame_loop.h"
#include "corvid/sdl/frame_stats.h"
#include "corvid/sdl/sdl_event.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "corvid/sdl/sdl_window.h"

using namespace corvid;
using namespace corvid::sdl;
using namespace corvid::cuda;

namespace {

// A filtered color grid: a uchar4 linear-unorm albedo per voxel, read back as
// a normalized float through a linearly filtered texture (a quarter the VRAM
// of float4, and the filtering still blends in float).
using color_volume = cuda_volume<uchar4, cudaReadModeNormalizedFloat>;

#pragma region Terrain

// Seed the heightfield from the terrain noise: one world-space surface height
// per (x, z) column, laid out row-major in z.
__global__ void init_height_kernel(float* height, int width, int depth,
    pos3 origin, float voxel_size) {
  const int ix = cuda_kernel::x_index();
  const int iz = cuda_kernel::y_index();
  if (ix >= width || iz >= depth) return;
  const float wx = origin.v.x + (static_cast<float>(ix) * voxel_size);
  const float wz = origin.v.z + (static_cast<float>(iz) * voxel_size);
  height[(iz * width) + ix] = terrain::height(wx, wz);
}

// One thermal-erosion pass: each column sheds material to its four neighbors
// wherever it stands steeper than the repose slope. Repeated, this settles
// every slope to at most `max_step` per cell, so no sharp faces survive. Reads
// `src`, writes `dst`; the caller ping-pongs them.
__global__ void erode_kernel(const float* src, float* dst, int width,
    int depth, float max_step, float rate) {
  const int ix = cuda_kernel::x_index();
  const int iz = cuda_kernel::y_index();
  if (ix >= width || iz >= depth) return;
  const int xm = ix > 0 ? ix - 1 : 0;
  const int xp = ix < width - 1 ? ix + 1 : width - 1;
  const int zm = iz > 0 ? iz - 1 : 0;
  const int zp = iz < depth - 1 ? iz + 1 : depth - 1;
  const float h = src[(iz * width) + ix];
  float delta = 0.0F;
  delta += terrain::talus_flow(h, src[(iz * width) + xm], max_step, rate);
  delta += terrain::talus_flow(h, src[(iz * width) + xp], max_step, rate);
  delta += terrain::talus_flow(h, src[(zm * width) + ix], max_step, rate);
  delta += terrain::talus_flow(h, src[(zp * width) + ix], max_step, rate);
  dst[(iz * width) + ix] = h + delta;
}

// Fill the grids from the (eroded) heightfield: the geometry density (solid
// below the surface, air above), the material tier by depth, and the color
// seeded from that tier with per-cell brightness and tint variation, so a band
// is not one flat color.
__global__ void fill_kernel(cudaSurfaceObject_t density_surface,
    cudaSurfaceObject_t material_surface, cudaSurfaceObject_t color_surface,
    const float* height, int height_width, density_field field) {
  const int ix = cuda_kernel::x_index();
  const int iy = cuda_kernel::y_index();
  const int iz = cuda_kernel::z_index();
  const int3 voxel = make_int3(ix, iy, iz);
  if (!field.contains(voxel)) return;

  const vec3 w = field.voxel_center(voxel).v;
  const float density = height[(iz * height_width) + ix] - w.y;
  surf3Dwrite(density, density_surface, ix * static_cast<int>(sizeof(float)),
      iy, iz);
  const uint16_t tier = strata::tier_for_depth(density);
  surf3Dwrite(tier, material_surface, ix * static_cast<int>(sizeof(uint16_t)),
      iy, iz);

  // Vary brightness and tint with 3D fractal noise in world space, so the
  // filtered color mottles organically instead of revealing a grid.
  const vec3 base = strata::tier_color(tier);
  constexpr float noise_scale = 0.15F;
  const float n =
      terrain::fbm_3d(w.x * noise_scale, w.y * noise_scale, w.z * noise_scale);
  const float warm = terrain::fbm_3d((w.x + 100.0F) * noise_scale,
      w.y * noise_scale, w.z * noise_scale);
  const float shade = 0.78F + (0.50F * n);
  const vec3 tint{0.94F + (0.12F * warm), 1.0F, 1.06F - (0.12F * warm)};
  const vec3 c = base * shade * tint;
  surf3Dwrite(make_uchar4(to_unorm8(c.x), to_unorm8(c.y), to_unorm8(c.z), 255),
      color_surface, ix * static_cast<int>(sizeof(uchar4)), iy, iz);
}

#pragma endregion
#pragma region Rendering

// Shade each pixel by supersampling and write the result. The texture is
// `R8G8B8A8_UNORM`, so a `uchar4` of (r, g, b, a) maps straight to its bytes.
//
// `__launch_bounds__` caps registers so more 256-thread blocks stay resident:
// uncapped, the march wants 136 registers, leaving only one block per SM
// (~17% occupancy) to hide texture-fetch latency, which roughly halves the
// frame rate. Capping trades a little register spill for more resident blocks;
// measured on the terrain view, 3 blocks (~80 registers, ~50% occupancy) is
// the peak, past which the added spill outweighs the occupancy.
//
// The large scene params (`field`, `ball`, `head`, `mirror`, `cfg`) are
// `__grid_constant__`: the shaders take them by const reference, and without
// this the compiler copies each whole struct (`render_config` alone is ~520
// bytes) into a per-thread local stack frame just to pass a pointer.
// `__grid_constant__` binds the reference straight to the parameter in
// constant memory, with no copy. `res` and `cam` are read directly here, not
// passed by reference, so they need no annotation.
__global__ void __launch_bounds__(256, 3) voxel_kernel(cudaSurfaceObject_t out,
    resolution res, camera_rays cam,
    __grid_constant__ const density_field field, cudaTextureObject_t color_tex,
    __grid_constant__ const metal_ball ball,
    __grid_constant__ const saucer_head head,
    __grid_constant__ const flat_mirror mirror,
    __grid_constant__ const render_config cfg) {
  const int px = cuda_kernel::x_index();
  const int py = cuda_kernel::y_index();
  const auto fx = static_cast<float>(px);
  const auto fy = static_cast<float>(py);
  if (fx >= res.width || fy >= res.height) return;

  // Average an `aa_samples` x `aa_samples` grid of sub-pixel rays to
  // anti-alias the silhouettes and soften the strata seams. 1 disables it.
  //
  // Adaptive AA (future): shade one center sample first, then fan out to the
  // full grid only on pixels whose nearest-hit id or depth disagrees with a
  // neighbor, the silhouettes that actually alias, leaving flat interiors at
  // one sample. That spends the AA budget on edges and buys back most of the
  // supersampling cost.
  const int aa_samples = cfg.aa_samples;
  const float inv = 1.0F / static_cast<float>(aa_samples);
  vec3 color{};
  for (int sy = 0; sy < aa_samples; ++sy)
    for (int sx = 0; sx < aa_samples; ++sx) {
      const float ox = (static_cast<float>(sx) + 0.5F) * inv;
      const float oy = (static_cast<float>(sy) + 0.5F) * inv;
      const vec3 ray_dir =
          cam.ray_direction(pos2{vec2{fx + ox, fy + oy}}, res);
      color = color + shade_primary_ray(field, color_tex, ball, head, mirror,
                          cfg, cam.eye, ray_dir);
    }
  color = color * (inv * inv);

  // Reinhard tonemap so highlights roll off.
  color = vec3{color.x / (1.0F + color.x), color.y / (1.0F + color.y),
      color.z / (1.0F + color.z)};

  // A small crosshair at the screen center marks where a dig lands.
  const float center_x = res.width * 0.5F;
  const float center_y = res.height * 0.5F;
  if ((fabsf(fx - center_x) < 6.0F && fabsf(fy - center_y) < 1.0F) ||
      (fabsf(fy - center_y) < 6.0F && fabsf(fx - center_x) < 1.0F))
    color = vec3{1.0F, 1.0F, 1.0F};

  const uchar4 pixel =
      make_uchar4(to_byte(color.x), to_byte(color.y), to_byte(color.z), 255);
  surf2Dwrite(pixel, out, px * static_cast<int>(sizeof(uchar4)), py);
}

#pragma endregion
#pragma region Editing

// Where a dig will land: the world point the center ray hit, and whether it
// hit anything at all.
struct dig_probe {
  pos3 point;
  bool hit;
};

// Cast the center ray and record where it meets the surface, for the brush.
__global__ void
pick_kernel(density_field field, pos3 eye, vec3 dir, dig_probe* out) {
  const float dist = field.raymarch(eye, dir);
  out->hit = (dist >= 0.0F);
  // No need to set on miss, because we check for hit before reading the point.
  if (out->hit) out->point = eye + (dir * dist);
}

// Carve a spherical brush of `radius` around the picked point, subtracting
// `strength` from each voxel's density with a linear falloff so the center
// digs most; lowering density turns solid into air. A no-op if the pick
// missed. Reads and writes the field surface, so the next frame's march shows
// the hole.
__global__ void dig_kernel(cudaSurfaceObject_t surface, density_field field,
    const dig_probe* probe, float radius, float strength) {
  if (!probe->hit) return;

  // Skip voxels outside the brush's bounding cube.
  const int radius_voxels = static_cast<int>(ceilf(radius / field.voxel_size));
  const int span = (2 * radius_voxels) + 1;
  const int sx = cuda_kernel::x_index();
  const int sy = cuda_kernel::y_index();
  const int sz = cuda_kernel::z_index();
  if (sx >= span || sy >= span || sz >= span) return;

  // The picked point in voxel space, and the voxel this thread edits.
  const vec3 rel = field.to_voxel(probe->point);
  const int vx = static_cast<int>(lroundf(rel.x)) + (sx - radius_voxels);
  const int vy = static_cast<int>(lroundf(rel.y)) + (sy - radius_voxels);
  const int vz = static_cast<int>(lroundf(rel.z)) + (sz - radius_voxels);
  const int3 voxel = make_int3(vx, vy, vz);
  if (!field.contains(voxel)) return;

  // Skip voxels outside the brush sphere.
  const pos3 voxel_point = field.voxel_center(voxel);
  const float d = distance(voxel_point, probe->point);
  if (d > radius) return;

  float density = 0.0F;
  surf3Dread(&density, surface, vx * static_cast<int>(sizeof(float)), vy, vz);
  density -= strength * (1.0F - (d / radius));
  surf3Dwrite(density, surface, vx * static_cast<int>(sizeof(float)), vy, vz);
}

#pragma endregion
#pragma region Input

// Whether an event carries mouse or keyboard input, so an open config panel
// can swallow the input ImGui captured instead of letting the game act on it.
[[nodiscard]] bool is_mouse_event(const sdl_event& ev) {
  switch (ev.data_type()) {
  case sdl_event_data_type::motion:
  case sdl_event_data_type::button:
  case sdl_event_data_type::wheel: return true;
  default: return false;
  }
}
[[nodiscard]] bool is_keyboard_event(const sdl_event& ev) {
  switch (ev.data_type()) {
  case sdl_event_data_type::key:
  case sdl_event_data_type::text:
  case sdl_event_data_type::edit: return true;
  default: return false;
  }
}

// Fold the left mouse button into the `digging` flag, for composing after
// `handle_fly`. Returns whether it consumed the event.
[[nodiscard]] bool handle_dig(const sdl_event& ev, bool& digging) {
  switch (ev.type()) {
  case sdl_event_type::mouse_button_down:
  case sdl_event_type::mouse_button_up: {
    const auto button = ev.get_button();
    if (button.button == sdl_mouse_button::left) {
      digging = button.down;
      return true;
    }
    return false;
  }

  default: return false;
  }
}

// Route one frame event. ImGui always sees it, so its capture state stays
// current. While the config panel is open, ImGui swallows the input it
// captured (a slider drag, a focused field) so the game does not also react;
// other events still reach the game handlers. Returns whether it consumed the
// event, leaving Escape for the pump to toggle the panel.
[[nodiscard]] bool handle_viewer_event(const sdl_event& ev,
    imgui_overlay& imgui, bool show_config, fly_input& input, sdl_window& win,
    bool& digging) {
  imgui.process_event(ev);
  if (show_config) {
    if (is_mouse_event(ev) && imgui.wants_mouse()) return true;
    if (is_keyboard_event(ev) && imgui.wants_keyboard()) return true;
  }
  return input.handle(ev, win) || handle_dig(ev, digging);
}

#pragma endregion
#pragma region Avatar

// Rotate `v` about unit `axis` by `angle` (Rodrigues' rotation formula). Local
// to the rig for now; promote to the vec math if a second caller appears.
[[nodiscard]] vec3 rotate_about(vec3 v, vec3 axis, radians angle) {
  const float c = cos(angle);
  const float s = sin(angle);
  return (v * c) + (cross(axis, v) * s) + (axis * (dot(axis, v) * (1.0F - c)));
}

// The player's avatar rig: the ball anchor the player drives and the saucer
// head the camera rides inside. The camera is always the head, so the head is
// never drawn in the view; you see it only reflected in the ball. A Warcraft-
// style zoom slides the head along the yaw-forward axis relative to the ball:
// pulled back and raised for an over-the-shoulder trailing view, or drawn in
// to the jockey, close above and behind the ball, so a level look sees past it
// and looking down gives its profile. The head never goes in front. WASD
// drives the ball along its heading and the mouse wheel zooms. Holding the
// right button aims the eye: with no movement
// keys it is free Look; while driving it is Steer, the heading chasing the eye
// so the ball arcs toward where you look. Releasing it Follows: the ball keeps
// its heading and the view rotates to frame the travel. The head flies to its
// seat at a bounded speed, so it never snaps around the boom. No physics yet:
// the anchor moves directly and the ball floats where it is left.
struct avatar_rig {
  pos3 anchor;        // ball center, what the player drives
  orientation facing; // yaw/pitch look direction
  radians heading;    // yaw the head sits along; tracks the look while moving
  float boom;         // head distance behind the ball, jockey to trailing
  float
      boom_target; // where the wheel is taking the boom; `update` eases to it
  float spin = 0.0F;  // saucer belly rotation, advanced by `update`
  float drive = 0.0F; // smoothed signed forward head speed, clamped (-1..1)
  float slide = 0.0F; // smoothed signed strafe head speed, clamped (-1..1)
  float drive_raw = 0.0F;   // forward head speed, unclamped (sprint past 1)
  float slide_raw = 0.0F;   // strafe head speed, unclamped (sprint past 1)
  float spin_clock = 0.0F;  // time accumulator for the idle spin reversal
  float blink_phase = 0.0F; // antenna beacon blink phase, in cycles [0..1)
  float idle_dir = 1.0F;    // smoothed idle spin direction (+/-1)
  float moving = 0.0F;      // this frame's planar ball movement, set by `move`
  pos3 prev_eye{};     // last frame's head position, for the head velocity
  bool primed = false; // whether `prev_eye` holds a real previous frame
  vec3 head_offset{};  // eased head offset from the ball, flown to the seat
  bool head_primed = false; // whether `head_offset` holds a real seated offset
  bool frozen = false;      // observer freeze: camera pinned, head shown
  pos3 frozen_cam{};        // the pinned camera position while `frozen`
  basis frozen_frame{};     // the pinned camera look while `frozen`
  bool locked = false;      // treadmill: hold the body, animate as if moving
  vec3 locked_step{};       // the step `move` withheld this frame while locked
  vec3 ball_roll_axis{0.0F, 0.0F,
      1.0F};                     // motion grid roll axis, set by `move`
  float ball_roll_phase = 0.0F;  // accumulated roll angle, scrolls the grid
  float ball_steer_phase = 0.0F; // accumulated steer fake, drifts it sideways
  float ball_glow = 0.0F;        // eased motion-grid intensity (0 at rest)
  avatar_tuning
      tune{}; // live feel constants, read through by the methods below

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
  // boom and raised by the rise. The boom runs from the jockey (`boom_min`,
  // above and slightly behind) to the trailing distance (`boom_max`) and never
  // goes in front. The heading, not the live look, seats the head, so
  // free-look turns the camera without orbiting the head around the ball. The
  // head's actual offset eases toward this (`update`), so the head translates
  // with the ball one-to-one (never lagging the drive, however fast), while a
  // heading swing or boom dolly glides the offset rather than snapping it
  // around the long boom.
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
  // offset. The offset carries all the smoothing, so the head tracks the
  // ball's translation exactly and only a heading swing or boom dolly glides.
  [[nodiscard]] pos3 eye() const { return anchor + head_offset; }

  // Drive the anchor along the ground in its own heading, so the ball keeps
  // its course; steering swings the heading toward the look (see `update`), so
  // the ball arcs that way. The heading is a flat ground bearing, so looking
  // up never lifts the ball. Space/Ctrl still raise and lower it directly,
  // since there is no terrain following yet. Records the planar movement so
  // `update` knows the body is moving; the tilt and spin read the head's own
  // velocity, not this input.
  void move(float forward, float strafe, float lift, bool fast, float dt) {
    const vec3 fwd{cos(heading), 0.0F, sin(heading)};
    const vec3 right{-sin(heading), 0.0F, cos(heading)};
    const vec3 step =
        (fwd * forward) + (right * strafe) + (camera::world_up * lift);
    // The locked treadmill holds the body in place (so the distance to the
    // mirror stays fixed for testing) but withholds the step for `update` to
    // feed into the velocity, so the saucer still tilts and spins as if
    // moving.
    if (locked) {
      locked_step = step;
    } else {
      anchor = anchor + step;
      locked_step = vec3{};
    }
    moving = fabsf(forward) + fabsf(strafe);

    // Advance the rolling motion grid: aim its conveyor across this frame's
    // travel and scroll it by the rolling-without-slipping angle (arc /
    // radius). Driven by the planar step whether locked or not, so the
    // treadmill rolls the grid in place like the saucer spins. The phase wraps
    // to one grid period (an integer roll, so invisible at integer `hex_freq`)
    // to stay precise over a long session.
    const vec3 ground{step.x, 0.0F, step.z};
    const float dist = length(ground);
    if (dist > 1.0e-6F) {
      // Ease the roll axis toward the new travel direction rather than
      // snapping it, so changing the motion direction (adding a strafe,
      // reversing) turns the grid quickly but smoothly instead of jumping. The
      // rotation is about world up (both axes are horizontal); the sign turns
      // the short way, and an exact reversal, where the cross degenerates,
      // just picks a side.
      const vec3 target = normalize(cross(camera::world_up, ground));
      const float cosang =
          fminf(fmaxf(dot(ball_roll_axis, target), -1.0F), 1.0F);
      const float ang = acosf(cosang);
      if (ang > 1.0e-5F) {
        const float frac = 1.0F - expf(-tune.ball_grid_turn_rate * dt);
        const float sy = cross(ball_roll_axis, target).y;
        const float sgn = sy < 0.0F ? -1.0F : 1.0F;
        ball_roll_axis = rotate_about(ball_roll_axis, camera::world_up,
            radians{sgn * ang * frac});
      }
      // Sprinting travels three times as fast, so the scroll has its own gain
      // to keep the faster roll readable rather than strobing.
      const float gain =
          fast ? tune.ball_grid_roll_gain_fast : tune.ball_grid_roll_gain;
      ball_roll_phase =
          fmodf(ball_roll_phase + ((dist / tune.ball_radius) * gain), 1.0F);
    }
  }

  // Orbit the look by yaw/pitch deltas, clamping pitch shy of vertical.
  void look(float yaw, float pitch) {
    facing.yaw = facing.yaw + radians{yaw};
    facing.pitch = std::clamp(facing.pitch + radians{pitch},
        -camera::max_pitch, camera::max_pitch);
  }

  // Aim the zoom: a positive delta (wheel up) targets a smaller boom, toward
  // the jockey. The head only glides there in `update`, so it reads as the
  // saucer moving rather than snapping.
  void zoom(float delta) {
    boom_target =
        std::clamp(boom_target - delta, tune.boom_min, tune.boom_max);
  }

  // Advance the frame: ease the boom toward its zoom target, turn the heading
  // and look while the ball moves (steering swings the heading toward the eye
  // so the ball arcs that way; following eases the look onto the heading and
  // the pitch to level so the view frames the travel), fly the head toward its
  // seat at a bounded speed, derive the head's own velocity (so a zoom dolly
  // tilts the head too) into the eased drive/slide that lean the saucer, and
  // spin the belly: travel-driven while moving, alternating while idle.
  void update(float dt, bool looking) {
    boom = boom +
           ((boom_target - boom) * (1.0F - expf(-tune.zoom_approach * dt)));

    if (moving > 0.0F) {
      if (looking) {
        // Steer: the heading chases the eye, so the ball arcs around the head
        // toward where you look, then drives that way once they line up.
        const float rate = 1.0F - expf(-tune.heading_approach * dt);
        const radians delta = radians_atan2(sin(facing.yaw - heading),
            cos(facing.yaw - heading));
        heading = heading + (delta * rate);
        // Fake the turn on the motion grid: the conveyor stays motion-aligned,
        // so the arc is invisible under the tracking camera; drift the grid
        // sideways by this frame's heading change. Wrap to the grid's sideways
        // period (sqrt3 cells, an integer roll at integer `hex_freq`) so it
        // stays precise without a visible jump.
        ball_steer_phase = fmodf(
            ball_steer_phase +
                ((delta * rate).value * tune.ball_grid_steer_gain),
            std::numbers::sqrt3_v<float>);
      } else {
        // Follow: the heading holds, so the ball keeps its course; the look
        // eases gently onto the heading and the pitch to level, rotating the
        // view to frame the travel without whipping.
        const float rate = 1.0F - expf(-tune.follow_approach * dt);
        const radians delta = radians_atan2(sin(heading - facing.yaw),
            cos(heading - facing.yaw));
        facing.yaw = facing.yaw + (delta * rate);
        facing.pitch = facing.pitch * (1.0F - rate);
      }
    }

    // Ease the head offset toward its seat, capped at the ball's own move
    // speed: the head can never reposition around the ball faster than the
    // ball itself travels, so a heading swing or boom dolly glides rather than
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
      drive = drive + ((drive_target - drive) * ramp);
      slide = slide + ((slide_target - slide) * ramp);
      drive_raw = drive_raw + ((fwd_v - drive_raw) * ramp);
      slide_raw = slide_raw + ((side_v - slide_raw) * ramp);
    }
    prev_eye = here;
    primed = true;

    // The Head's planar speed (forward and strafe), saturated to 1 at full
    // move speed. It quiets the idle spin and, in `head`, gates the beacon's
    // on/off blink off at rest and blends its moving versus idle color. The
    // unclamped `move_mag` (which a sprint pushes past 1) sets the blink rate.
    const float speed = fminf(1.0F, fabsf(drive) + fabsf(slide));

    // The belly spin: while moving it follows travel (faster ahead, reversing
    // when backing up; strafing spins it opposite ways left versus right) on
    // the unclamped speed, so a sprint spins it faster too; while stationary
    // it alternates direction every `spin_idle_period`, easing through each
    // reversal, which reads livelier than a constant idle spin.
    spin_clock = spin_clock + dt;
    const float idle_target =
        fmodf(spin_clock, 2.0F * tune.spin_idle_period) < tune.spin_idle_period
            ? 1.0F
            : -1.0F;
    idle_dir = idle_dir + ((idle_target - idle_dir) * ramp);
    const float idle = tune.spin_rate * idle_dir * (1.0F - speed);
    spin =
        spin +
        ((idle + (tune.spin_move_gain * drive_raw) +
             (tune.spin_strafe_gain * slide_raw)) *
            dt);

    // The antenna beacon's on/off blink phase, in cycles: it advances at a
    // rate scaled by the unclamped planar speed, so it does not pulse at rest
    // and runs faster the quicker the Head travels, sprint included. `head`
    // turns the phase into the 0..1 waveform; the wrap keeps the accumulator
    // bounded.
    const float move_mag =
        sqrtf((drive_raw * drive_raw) + (slide_raw * slide_raw));
    blink_phase =
        fmodf(blink_phase + ((tune.blink_move_gain * move_mag) * dt), 1.0F);

    // The ball's motion grid glow: gated on the direction keys (`moving` is
    // this frame's drive/strafe step, so a head dolly or steer with no keys
    // shows nothing) and scaled by the ball's own planar speed. It flares up
    // at `motion_approach` and fades back to dark at its own `ball_grid_fade`
    // rate when the keys release, so the hex wireframe shows only while
    // rolling.
    const float ball_speed = (dt > 0.0F) ? moving / dt : 0.0F;
    const float glow_target =
        fminf(1.0F, (ball_speed / tune.move_speed) * tune.ball_grid_move_gain);
    const float glow_rate =
        (glow_target > ball_glow) ? tune.motion_approach : tune.ball_grid_fade;
    ball_glow =
        ball_glow +
        ((glow_target - ball_glow) * (1.0F - expf(-glow_rate * dt)));
  }

  // The camera position: the eye raised above the head's center by
  // `camera_height` (of the head radius) so the camera looks out from up in
  // the dome rather than the middle; otherwise the dome-heavy saucer reflects
  // into the top of the frame. World up, not the tilted disc normal, so the
  // viewpoint does not swim as the saucer banks.
  [[nodiscard]] pos3 cam_pos() const {
    return eye() +
           (camera::world_up * (tune.camera_height * tune.head_radius));
  }

  // Enter or leave the observer freeze. On the off-to-on edge it pins the
  // camera position and look; while on, `rays` holds both fixed, so the mouse
  // and movement keys drive the avatar (turning and moving the ship in front
  // of a fixed camera) rather than steering the view. The head is also drawn
  // in the primary view; see `render_config::show_head`.
  void set_frozen(bool on) {
    if (on && !frozen) {
      frozen_cam = cam_pos();
      frozen_frame = frame();
    }
    frozen = on;
  }

  // The view rays for the current pose, looking out from inside the head, or
  // from the pinned camera while the observer freeze holds (position and look
  // both fixed, so the mouse turns the ship instead of the view). The field of
  // view's `tan(fov/2)` is cached in `tune` and recomputed only when the field
  // of view is edited, so this stays a plain read.
  [[nodiscard]] camera_rays rays() const {
    if (frozen) return {frozen_cam, frozen_frame, tune.tan_half_fov()};
    return {cam_pos(), frame(), tune.tan_half_fov()};
  }

  [[nodiscard]] metal_ball ball() const {
    return {anchor, tune.ball_radius, ball_roll_axis, ball_roll_phase,
        ball_steer_phase, ball_glow};
  }
  [[nodiscard]] saucer_head head(const render_config::head_params& hp) const {
    // The articulated look gimbal. The camera looks freely along `facing`; the
    // eye, dome, and saucer tilt computed here are only drawn (seen reflected
    // in the ball), never the view itself. Everything is built in the look's
    // vertical meridian: `f_h` is the look heading on the ground, `world_up`
    // the vertical. `front_offset_deg` (animation rigging) swings the meridian
    // off the look heading about the vertical, to bring the back of the dome
    // into the mirror.
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

    // The eye's travel on the dome, edge to edge. The dome sphere's lower part
    // is submerged in the disc, so the visible cap ends where the sphere
    // emerges: the circle at the disc's top height on the sphere, whose dome
    // elevation (asin of that height over the dome radius) is the seam. The
    // lower limit keeps the eye's edge above it so the eye never dips out of
    // sight behind the disc; the upper keeps its edge below the pole. Geometry
    // only (disc_height, dome_offset, dome_radius), independent of the
    // shader's seam decal, so retuning the seam never moves the eye limit.
    const float seam_h = fmaxf(
        fminf((tune.disc_height - tune.dome_offset) / tune.dome_radius, 1.0F),
        -1.0F);
    const radians dome_max = pole - eye_ang;
    const radians dome_min =
        std::min(radians_asin(seam_h) + eye_ang, dome_max);

    // The eye aims `lift` above the look: the camera rides above the eye, so a
    // level aim reads as looking low, and the lift restores eye contact in the
    // mirror. It sits `rest` higher than its aim on the dome, so a level look
    // rests the saucer dipped by `rest` (nosed down), showing the dome in
    // profile from the jockey. The dome rotates the eye within its travel;
    // past that the saucer takes over the dip, nosing up until the eye reaches
    // straight up and down only as far as `dip_max`.
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

    // Helicopter motion tilt: the disc banks with travel (the eased
    // `drive`/`slide`, measured in the heading frame), on top of the look tilt
    // and independent of it. Forward leans the top toward the heading (front
    // dips), reverse leans it back, strafe banks it into the strafe, like a
    // helicopter. The pitch is negated because a positive rotation about
    // `h_right` tilts the normal away from the heading.
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
    // dome rather than the tilt being capped to keep it there. Holding the
    // steadycam eye level lets a hard bank carry it past the dome's edge,
    // where the disc rim would hide it, so clamp its elevation over the banked
    // disc to [dome_min, dome_max] and rotate the whole dome (grid and antenna
    // with it) back onto the cap. The eye then dips to the seam on reverse and
    // rides to the pole on forward while the disc tilts fully under it.
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

    // The beacon animation handed to the shader: the blink waveform from the
    // phase, how much the Head is backing up (reddens the beacon while
    // moving), the planar speed (gates the on/off blink off at rest and blends
    // the moving versus idle color), and the idle color selector tied to the
    // belly idle spin so the beacon alternates in tune with it at rest.
    // Strafing does not redden the beacon: only backing up is special.
    constexpr float two_pi = 2.0F * std::numbers::pi_v<float>;
    const float blink = 0.5F + (0.5F * sin(radians{blink_phase * two_pi}));
    const float reversing = fmaxf(0.0F, -drive);
    const float speed = fminf(1.0F, fabsf(drive) + fabsf(slide));

    // The idle color selector: a cosine over the beacon color cycle, the belly
    // reversal cycle divided by `color_spin_ratio`, so the resting beacon
    // shifts color in tune with the spin but at a chosen multiple of its rate
    // (1 locks them identical, which reads wrong). `color_phase` shifts the
    // color against the spin (0 in phase, 1 opposite); `fmodf` keeps the
    // cosine's argument bounded.
    const float cycle = 2.0F * tune.spin_idle_period;
    const float color_cycle = cycle / fmaxf(tune.color_spin_ratio, 0.01F);
    const float frac = fmodf(spin_clock, color_cycle) / color_cycle;
    const float idle_smooth =
        cos(radians{(frac + (tune.color_phase * 0.5F)) * two_pi});
    const float idle_mix = 0.5F - (0.5F * idle_smooth);

    return {eye(), saucer_up, dome_up, disc_nose, tune.head_radius, spin,
        tune.disc_height, tune.dome_offset, tune.dome_radius, tune.dome_blend,
        tune.top_height, tune.rim_round, eye_banked, antenna_banked,
        tune.antenna_length, tune.antenna_thickness, tune.antenna_ball,
        tune.antenna_collar, blink, reversing, speed, idle_mix,
        tune.head_hit_cap};
  }
};

#pragma endregion
#pragma region World generation

// Generate the terrain into the three grids: build the surface heightfield,
// slump it to the soil's angle of repose, then fill the geometry density, the
// material tier, and the tier-seeded color from it. One-time GPU world-gen,
// run before the first frame.
void generate_world(const density_field& field,
    const cuda_volume<float>& volume, const material_volume& materials,
    const color_volume& colors) {
  const cudaExtent extent = field.extent;
  const int height_w = static_cast<int>(extent.width);
  const int height_d = static_cast<int>(extent.depth);

  // Build the heightfield and slump it to the soil's angle of repose, so
  // world-gen leaves no face steeper than `repose_slope` (no sharp corners to
  // alias). Each thermal-erosion pass sheds material off columns standing too
  // tall over a neighbor; a few dozen passes settle every slope.
  constexpr float repose_slope = 0.7F; // tangent, about 35 degrees
  constexpr float erode_rate = 0.15F;
  constexpr int erode_passes = 80;
  cuda_ptr<float> height_a{static_cast<size_t>(height_w) * height_d};
  cuda_ptr<float> height_b{static_cast<size_t>(height_w) * height_d};
  if (!height_a || !height_b)
    throw std::runtime_error{"failed to allocate heightfield"};
  const dim3 height_block{16, 16};
  const dim3 height_grid{cuda_kernel::ceil_div(extent.width, height_block.x),
      cuda_kernel::ceil_div(extent.depth, height_block.y)};
  init_height_kernel<<<height_grid, height_block>>>(height_a.get(), height_w,
      height_d, field.origin, field.voxel_size);
  float* height_src = height_a.get();
  float* height_dst = height_b.get();
  for (int pass = 0; pass < erode_passes; ++pass) {
    erode_kernel<<<height_grid, height_block>>>(height_src, height_dst,
        height_w, height_d, repose_slope * field.voxel_size, erode_rate);
    float* const tmp = height_src;
    height_src = height_dst;
    height_dst = tmp;
  }

  // Fill the geometry, material, and color grids from the eroded heightfield.
  const dim3 fill_block{8, 8, 8};
  const dim3 fill_grid{cuda_kernel::ceil_div(extent.width, fill_block.x),
      cuda_kernel::ceil_div(extent.height, fill_block.y),
      cuda_kernel::ceil_div(extent.depth, fill_block.z)};
  fill_kernel<<<fill_grid, fill_block>>>(volume.surface(), materials.surface(),
      colors.surface(), height_src, height_w, field);
  cuda_last_status{cudaDeviceSynchronize()}.or_throw();
}

// One present per vsync while the window is the foreground (input-focused)
// one, else one per 4 vsyncs so a backgrounded viewer idles without spinning
// up the GPU fan. `uncap` drops the foreground interval to 0 (present
// uncapped) for benchmarking, effective only on a tearing-capable swapchain;
// backgrounded still throttles so an idle viewer does not spin the GPU fan.
[[nodiscard]] int present_sync_interval(SDL_Window* win, bool uncap) {
  constexpr int background_sync = 4;
  const bool focused = (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0;
  if (!focused) return background_sync;
  return uncap ? 0 : 1;
}

// Remember the window's monitor, size, and maximized state between runs, so it
// reopens where it was left rather than at the default spot on the primary
// display. Stored as one line "x y w h maximized" under the SDL pref path
// (created on demand); `out` is left empty if the pref path is unavailable.
void window_geometry_path(char* out, size_t out_size) {
  out[0] = '\0';
  char* pref = SDL_GetPrefPath("Corvid", "VoxelViewer");
  if (!pref) return;
  SDL_snprintf(out, out_size, "%swindow.txt", pref);
  SDL_free(pref);
}

void restore_window_geometry(SDL_Window* win, const char* path) {
  if (!path[0]) return;
  std::ifstream in{path};
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int maximized = 0;
  if (!(in >> x >> y >> w >> h >> maximized)) return;
  SDL_SetWindowSize(win, w, h);
  SDL_SetWindowPosition(win, x, y);
  if (maximized) SDL_MaximizeWindow(win);
}

void save_window_geometry(SDL_Window* win, const char* path) {
  if (!path[0]) return;
  const bool maximized = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  SDL_GetWindowPosition(win, &x, &y);
  SDL_GetWindowSize(win, &w, &h);
  std::ofstream out{path, std::ios::trunc};
  out << x << ' ' << y << ' ' << w << ' ' << h << ' ' << (maximized ? 1 : 0)
      << '\n';
}

#pragma endregion
#pragma region Benchmark

// Offline kernel benchmark: render `voxel_kernel` repeatedly into an
// off-screen surface at a fixed pose and resolution, with no window, present,
// or vsync, and report the per-launch GPU time (min/avg/max). The pure,
// isolated signal for the small kernel changes the live, vsync-capped viewer
// cannot resolve; the viewer's "uncap fps" toggle is the complementary in-situ
// measurement. Selected by the `bench` argument.
[[nodiscard]] int run_kernel_bench() {
  constexpr int width = 2560; // a fixed 1440p frame, so runs compare directly
  constexpr int height = 1440;
  constexpr int warmup = 32; // settle clocks and caches before timing
  constexpr int iters = 200;

  // The same world the viewer builds (see `main`): the three grids filled once
  // from the eroded terrain, and the -z mirror.
  constexpr cudaExtent vol_extent{512, 128, 512};
  constexpr float voxel_size = 0.5F;
  cuda_volume<float> volume{vol_extent};
  material_volume materials{vol_extent};
  color_volume colors{vol_extent};
  const float ox =
      -0.5F * static_cast<float>(vol_extent.width - 1) * voxel_size;
  const float oy =
      -0.5F * static_cast<float>(vol_extent.height - 1) * voxel_size;
  const float oz =
      -0.5F * static_cast<float>(vol_extent.depth - 1) * voxel_size;
  density_field field{vol_extent, pos3{vec3{ox, oy, oz}}, voxel_size,
      volume.texture()};
  const float world_x1 =
      ox + (static_cast<float>(vol_extent.width - 1) * voxel_size);
  const flat_mirror mirror{.plane_z = oz,
      .lo = vec2{ox, oy},
      .hi = vec2{world_x1, oy + 80.0F},
      .normal = vec3{0.0F, 0.0F, 1.0F}};
  generate_world(field, volume, materials, colors);

  // A fixed pose, the viewer's default spawn (terrain toward the distant -z
  // mirror). The march tunables come from the shipped defaults.
  render_config cfg;
  field.march_lipschitz = cfg.march.lipschitz;
  field.march_max_step_voxels = cfg.march.max_step_voxels;
  field.march_max_steps = cfg.march.max_steps;
  avatar_rig rig{pos3{vec3{0.0F, 10.0F, 0.0F}},
      orientation{90.0_deg, -20.0_deg}, 90.0_deg, 7.0F, 7.0F};
  rig.update(0.016F, false); // seat the head offset off its first frame
  const camera_rays rays = rig.rays();
  const metal_ball ball = rig.ball();
  const saucer_head head = rig.head(cfg.head);

  // The off-screen target the kernel writes through: an owned surface array,
  // so the bench needs no D3D interop. Freed after the surface that borrows
  // it.
  cudaArray_t array{};
  const cudaChannelFormatDesc fmt = cudaCreateChannelDesc<uchar4>();
  cuda_last_status{
      cudaMallocArray(&array, &fmt, width, height, cudaArraySurfaceLoadStore)}
      .or_throw();
  const scope_exit free_array{[&] { (void)cudaFreeArray(array); }};
  cuda_surface surf{array};

  const dim3 block{16, 16};
  const dim3 grid{cuda_kernel::ceil_div(width, block.x),
      cuda_kernel::ceil_div(height, block.y)};
  const resolution res{static_cast<float>(width), static_cast<float>(height)};
  const auto launch = [&] {
    voxel_kernel<<<grid, block>>>(surf, res, rays, field, colors.texture(),
        ball, head, mirror, cfg);
  };

  for (int i = 0; i < warmup; ++i) launch();
  cuda_last_status{cudaDeviceSynchronize()}.or_throw();
  cuda_last_status{cudaGetLastError()}.or_throw();

  cuda_event start;
  cuda_event stop;
  float total = 0.0F;
  float lo = 1.0e30F;
  float hi = 0.0F;
  for (int i = 0; i < iters; ++i) {
    start.record().or_throw();
    launch();
    stop.record().or_throw();
    stop.synchronize().or_throw();
    float ms = 0.0F;
    cuda_event::elapsed_ms(start, stop, ms).or_throw();
    total += ms;
    lo = fminf(lo, ms);
    hi = fmaxf(hi, ms);
  }

  std::println("voxel_kernel  {}x{}  aa={}  {} iters", width, height,
      cfg.aa_samples, iters);
  std::println("  GPU ms/frame  min {:.3f}  avg {:.3f}  max {:.3f}", lo,
      total / static_cast<float>(iters), hi);
  return 0;
}

#pragma endregion

} // namespace

#pragma region Main loop

// NOLINTBEGIN(readability-function-cognitive-complexity)
int main(int argc, char** argv) {
  try {
    // `bench`: run the offline kernel benchmark (no window) and exit. The pure
    // signal for small kernel changes; the live "uncap fps" toggle is the
    // in-situ measure for a chosen view.
    if (argc > 1 && std::string_view{argv[1]} == "bench")
      return run_kernel_bench();
    sdl_subsystem sdl;
    sdl_window win{"Corvid Voxel Viewer", 1280, 720,
        sdl_window_flags::resizable};
    win.set_minimum_size(640, 480).or_throw();

    // Reopen on the monitor and at the size left last run (see helpers above).
    char geom_path[1024];
    window_geometry_path(geom_path, sizeof(geom_path));
    restore_window_geometry(win, geom_path);

    // The left mouse button, while held, digs at the crosshair.
    bool digging = false;

    const auto hwnd = static_cast<HWND>(win.native_handle());

    // The density field: a block of voxels centered on the world origin,
    // filled once from the terrain heightfield.
    constexpr cudaExtent vol_extent{512, 128, 512};
    constexpr float voxel_size = 0.5F;
    cuda_volume<float> volume{vol_extent};
    // The material grid: one hardness tier per voxel, point-exact (no
    // texture), the source of truth for digging and (later) SDF object flags.
    material_volume materials{vol_extent};
    // The color grid is seeded from the material tier at fill but kept
    // separate from the material grid, because color wants filtering while the
    // material id must stay exact.
    color_volume colors{vol_extent};
    const float ox =
        -0.5F * static_cast<float>(vol_extent.width - 1) * voxel_size;
    const float oy =
        -0.5F * static_cast<float>(vol_extent.height - 1) * voxel_size;
    const float oz =
        -0.5F * static_cast<float>(vol_extent.depth - 1) * voxel_size;
    // Not const: the live march tunables are copied onto it each frame.
    density_field field{vol_extent, pos3{vec3{ox, oy, oz}}, voxel_size,
        volume.texture()};

    // A flat mirror wall along the -z border (which the avatar faces at
    // start), edge to edge in x and rising from the floor, so the saucer can
    // be seen undistorted, unlike in the convex ball. Fly up to it to preen.
    const float world_x1 =
        ox + (static_cast<float>(vol_extent.width - 1) * voxel_size);
    const flat_mirror mirror{.plane_z = oz,
        .lo = vec2{ox, oy},
        .hi = vec2{world_x1, oy + 80.0F},
        .normal = vec3{0.0F, 0.0F, 1.0F}};

    // Generate the world once into the three grids before the first frame.
    generate_world(field, volume, materials, colors);

    // Digging scratch: a one-element device buffer holding where the center
    // ray last hit, written by pick_kernel and read by dig_kernel, so the dig
    // never round-trips to the host. The brush removes `dig_rate` of density
    // per second at its center, falling off across a `dig_radius` sphere.
    cuda_ptr<dig_probe> dig_target;
    if (!dig_target) throw std::runtime_error{"failed to allocate dig probe"};
    constexpr float dig_radius = 3.0F;
    constexpr float dig_rate = 10.0F;
    const int dig_span =
        (2 * static_cast<int>(std::ceil(dig_radius / voxel_size))) + 1;
    const dim3 dig_block{8, 8, 8};
    const dim3 dig_grid{cuda_kernel::ceil_div(dig_span, dig_block.x),
        cuda_kernel::ceil_div(dig_span, dig_block.y),
        cuda_kernel::ceil_div(dig_span, dig_block.z)};

    // The avatar starts floating (no gravity yet) at the world center, looking
    // toward -z and slightly down, in a trailing view pulled back from the
    // head. The mouse wheel dollies in toward the jockey; the right-drag
    // orbits. The feel constants (field of view and the rest) default from
    // `avatar_tuning`, edited live by the config panel.
    avatar_rig rig{pos3{vec3{0.0F, 10.0F, 0.0F}},
        orientation{90.0_deg, -20.0_deg}, 90.0_deg, 7.0F, 7.0F};
    const avatar_tuning tuning_defaults{};

    // The live shading config (sky, terrain, ball, head, anti-alias), edited
    // by the panel and passed to the kernel each frame; the defaults instance
    // is the baseline the panel compares against.
    render_config render_cfg;
    const render_config render_defaults{};
    fly_input input;

    const dim3 block{16, 16};

    // The GPU presentation pipeline: a CUDA-written render target copied to
    // the swapchain backbuffer and presented. It owns resize growth and
    // lost-device recovery (crossplatform.md section 11).
    cuda_d3d11_presenter presenter{hwnd};

    // The live config panel: a Dear ImGui overlay drawn over the frame,
    // toggled by Escape. Constructed after the presenter so it can borrow its
    // device and context. `show_config` gates both the panel and the input it
    // grabs.
    imgui_overlay imgui{win, presenter.device(), presenter.context()};
    bool show_config = false;

    // Observer freeze (debug): pin the camera and reveal the saucer head,
    // which is otherwise hidden from primary rays since the camera rides
    // inside it. Toggled by a panel checkbox; the avatar stays drivable so it
    // can be moved out in front of the pinned camera and watched.
    bool freeze_camera = false;

    // Treadmill (debug): hold the body in place while it still animates as if
    // moving, so the saucer's motion tilt can be watched at a fixed distance
    // from the mirror. Toggled by a panel checkbox.
    bool lock_position = false;

    // Benchmark: drop the vsync cap (present with tearing) so the frame rate
    // floats above the refresh rate and the title-bar FPS reads true GPU cost
    // for the current view. Toggled by a panel checkbox; needs a tearing-
    // capable swapchain, else it has no effect.
    bool uncap_fps = false;

    // Frame timing in nanoseconds: millisecond ticks quantize dt enough to
    // judder movement at high refresh rates (a 6.94 ms frame reads as 6 or 7).
    auto last_ns = SDL_GetTicksNS();

    // Frame-time stats over a one-second window, shown in the title bar to
    // tell steady pacing apart from periodic spikes.
    frame_stats stats;

    // Last frame's GPU kernel time (ms), measured with a `cuda_timer` and
    // shown next to the whole-frame ms so a slowdown can be pinned to the GPU
    // render or to the CPU-side per-frame work.
    float gpu_ms = 0.0F;

    while (true) {
      const auto action = pump_events([&](const sdl_event& ev) {
        return handle_viewer_event(ev, imgui, show_config, input, win,
            digging);
      });
      if (action == frame_action::quit) break;
      if (action == frame_action::resize) presenter.resize().or_throw();
      // Escape toggles the config panel. Opening it frees the OS cursor for
      // the sliders; the game re-grabs the cursor on the next right-button
      // press.
      if (action == frame_action::menu) {
        show_config = !show_config;
        if (show_config) win.set_relative_mouse_mode(false).or_throw();
      }

      const auto now_ns = SDL_GetTicksNS();
      const auto dt = static_cast<float>(now_ns - last_ns) / 1.0e9F;
      last_ns = now_ns;

      // While a panel widget owns the keyboard (a Ctrl+click value entry), its
      // key-up goes to ImGui, not the game, so a movement key held into it
      // would stick. Release them so the avatar stops when the UI takes over;
      // moving with the panel open otherwise still works.
      if (imgui.wants_keyboard()) input.release_keys();

      // Smooth the look before moving so a strafe uses this frame's facing.
      const auto [yaw, pitch] = input.look(dt);
      rig.look(yaw, pitch);

      // The panel below toggles `lock_position`, read a frame late (harmless
      // for a debug control); `move` then holds the body but still feeds the
      // step to the tilt.
      rig.locked = lock_position;
      const auto [fwd, strafe, lift] = input.movement(dt, rig.tune.move_speed);
      rig.move(fwd, strafe, lift, input.fast, dt);

      // The mouse wheel aims the zoom between the trailing distance and the
      // jockey: an impulse, not a held velocity, so it is not scaled by frame
      // time. The head then glides toward that target in `update`, so a zoom
      // slides the saucer in or out rather than snapping. The per-notch step
      // is live-tuned, so sync it from the rig before consuming the wheel.
      input.scroll_step = rig.tune.zoom_step;
      if (input.wheel != 0.0F) rig.zoom(input.dolly());

      // Ease the boom toward its zoom target and turn the saucer's belly spin.
      rig.update(dt, input.looking);

      // Skip all GPU work when the window has no client area (minimized, or
      // collapsed by the OS during a mixed-DPI cross-monitor drag).
      if (!presenter.window_width() || !presenter.window_height()) {
        SDL_Delay(16);
        continue;
      }

      // Fold this frame's wall-clock time into the stats; refresh the title
      // once a window passes a second.
      if (const auto report = stats.record(dt)) {
        char title[160];
        SDL_snprintf(title, sizeof(title),
            "Corvid Voxel Viewer - %.0f fps  %.1f/%.1f/%.1f ms (min/avg/max)  "
            "GPU %.1f ms",
            report->fps, report->min_ms, report->avg_ms, report->max_ms,
            gpu_ms);
        SDL_SetWindowTitle(win, title);
      }

      // Apply the observer freeze: pin the camera (on the rising edge) and ask
      // the kernel to draw the head, otherwise hidden from primary rays. The
      // panel below toggles `freeze_camera`, so this reads it a frame late,
      // imperceptible for a debug control and consistent within the frame.
      rig.set_frozen(freeze_camera);
      render_cfg.show_head = freeze_camera;

      // Carry the live march tunables onto the field the kernel marches.
      field.march_lipschitz = render_cfg.march.lipschitz;
      field.march_max_step_voxels = render_cfg.march.max_step_voxels;
      field.march_max_steps = render_cfg.march.max_steps;

      const camera_rays rays = rig.rays();
      const metal_ball ball = rig.ball();
      const saucer_head head = rig.head(render_cfg.head);

      // Carve the field at the crosshair while the left button is held. The
      // pick records the hit in device memory and the brush reads it there, so
      // the dig stays on the GPU; the next frame's march shows the hole.
      //
      // The center ray still aims the dig; the cursor-driven in-world reticle
      // that replaces it is the next step.
      if (digging) {
        pick_kernel<<<1, 1>>>(field, rays.eye, rays.frame.forward, dig_target);
        dig_kernel<<<dig_grid, dig_block>>>(volume.surface(), field,
            dig_target, dig_radius, dig_rate * dt);
      }

      // Open the ImGui frame and build the panel. One `begin_frame` pairs with
      // the one `render` below, which runs even with no panel up, so the
      // pairing always balances.
      imgui.begin_frame();
      if (show_config)
        draw_config_panel(rig.tune, tuning_defaults, render_cfg,
            render_defaults, freeze_camera, lock_position, uncap_fps);

      const int sync_interval = present_sync_interval(win, uncap_fps);

      presenter
          .render(
              [&](cudaArray_t array, int w, int h) {
                const dim3 grid_dim{cuda_kernel::ceil_div(w, block.x),
                    cuda_kernel::ceil_div(h, block.y)};
                cuda_surface surf{array};
                // Time just the kernel so the title can show GPU ms against
                // the whole-frame ms (GPU-bound vs CPU-bound).
                {
                  cuda_timer gpu_timer{gpu_ms};
                  voxel_kernel<<<grid_dim, block>>>(surf,
                      resolution{static_cast<float>(w), static_cast<float>(h)},
                      rays, field, colors.texture(), ball, head, mirror,
                      render_cfg);
                }
                cuda_timer::synchronize().or_throw();
              },
              [&] { imgui.render(presenter.back_buffer()); }, sync_interval)
          .or_throw();
    }
    save_window_geometry(win, geom_path);
    return 0;
  }
  catch (const std::exception& e) {
    // `main` is the boundary: report and exit rather than letting it escape.
    SDL_Log("fatal: %s", e.what());
    return 1;
  }
}
// NOLINTEND(readability-function-cognitive-complexity)

#pragma endregion
