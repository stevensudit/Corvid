// CUDA-driven voxel ray marcher (Windows-only CUDA cell, crossplatform.md
// section 11), the second 3D rung after the SDF raymarch viewer. A density
// field lives in VRAM as a 3D texture; a kernel fixed-step marches it per
// pixel to the first solid voxel, shades it, and writes the result into the
// interop texture through `cudaGraphicsD3D11*`; D3D copies it to the
// backbuffer and presents. The frame stays on the GPU. Unlike the SDF viewer's
// analytic scene, the geometry is sampled from the field, so it can be edited
// in place (the next rung). The player is an avatar of a metallic ball and a
// saucer head: WASD drives the ball (Space/Ctrl raise and lower it, Shift goes
// faster), the right mouse button orbits the look, and the mouse wheel zooms
// the view from third person in to first person through the head. Escape
// quits.

#include <cmath>
#include <cstdint>
#include <exception>
#include <stdexcept>

#include <cuda_runtime.h>

#include "corvid/cuda/avatar.cuh"
#include "corvid/cuda/camera.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_surface.cuh"
#include "corvid/cuda/cuda_volume.cuh"
#include "corvid/cuda/density_field.cuh"
#include "corvid/cuda/material_volume.cuh"
#include "corvid/cuda/radians.cuh"
#include "corvid/cuda/raycast.cuh"
#include "corvid/cuda/render_config.cuh"
#include "corvid/cuda/strata.cuh"
#include "corvid/cuda/terrain.cuh"
#include "corvid/cuda/vec.cuh"
#include "corvid/cuda/voxel_render.cuh"
#include "corvid/cuda/windows/cuda_d3d11_presenter.cuh"
#include "corvid/cuda/windows/game/avatar_tuning.cuh"
#include "corvid/cuda/windows/game/config_panel.cuh"
#include "corvid/cuda/windows/imgui_overlay.h"
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
__global__ void voxel_kernel(cudaSurfaceObject_t out, resolution res,
    camera_rays cam, density_field field, cudaTextureObject_t color_tex,
    metal_ball ball, saucer_head head, render_config cfg) {
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
      color = color + shade_primary_ray(field, color_tex, ball, head, cfg,
                          cam.eye, ray_dir);
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

// The player's avatar rig: the ball anchor the player drives and the saucer
// head the camera rides inside. The camera is always the head, so the head is
// never drawn in the view; you see it only reflected in the ball. A Warcraft-
// style zoom slides the head along the yaw-forward axis relative to the ball:
// pulled back and raised for an over-the-shoulder third-person view, or pushed
// in front of the ball for first person (the ball then behind you, visible
// only on a free-look turn). WASD moves the anchor in the yaw-horizontal
// frame, the right-drag orbits the look, the mouse wheel zooms. No physics
// yet: the anchor moves directly and the ball floats where it is left.
struct avatar_rig {
  pos3 anchor;        // ball center, what the player drives
  orientation facing; // yaw/pitch look direction
  radians heading;    // yaw the head sits along; tracks the look while moving
  float boom;         // head distance behind the ball; negative is in front
  float
      boom_target; // where the wheel is taking the boom; `update` eases to it
  float spin = 0.0F;   // saucer belly rotation, advanced by `update`
  float thrust = 0.0F; // propulsion glow, from motion, advanced by `update`
  float moving = 0.0F; // this frame's planar movement amount, set by `move`
  avatar_tuning tune; // live feel constants, read through by the methods below

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

  // The head position, which is also the eye: offset from the ball along the
  // heading (behind by the boom, in front when negative) and hovering above
  // it, rising as it pulls back. The heading, not the live look, places the
  // head, so free-look turns the camera without orbiting the head around the
  // ball.
  [[nodiscard]] pos3 eye() const {
    const float rise =
        tune.head_height + ((boom > 0.0F ? boom : 0.0F) * tune.boom_rise);
    const vec3 heading_fwd{cos(heading), 0.0F, sin(heading)};
    return anchor - (heading_fwd * boom) + (camera::world_up * rise);
  }

  // The saucer's tilted up axis (disc normal). The saucer is rigidly mounted
  // to the view and tilts as a body to aim its fixed camera: the disc normal
  // is the view up leaned back along the look, so the belly faces outward.
  // Pitch the look down and the whole saucer noses down with it, showing its
  // profile.
  [[nodiscard]] vec3 saucer_up() const {
    const basis b = frame();
    return normalize(b.up - (b.forward * tune.saucer_lean));
  }

  // Drive the anchor in the yaw-horizontal frame. Space/Ctrl still raise and
  // lower it directly, since there is no terrain following yet. Records the
  // planar movement so `update` can swing the heading and drive the thrust.
  void move(float forward, float strafe, float lift) {
    const basis b = frame();
    const vec3 fwd = normalize(vec3{b.forward.x, 0.0F, b.forward.z});
    const vec3 right = normalize(vec3{b.right.x, 0.0F, b.right.z});
    anchor = anchor + (fwd * forward) + (right * strafe) +
             (camera::world_up * lift);
    moving = fabsf(forward) + fabsf(strafe);
  }

  // Orbit the look by yaw/pitch deltas, clamping pitch shy of vertical.
  void look(float yaw, float pitch) {
    facing.yaw = facing.yaw + radians{yaw};
    facing.pitch = std::clamp(facing.pitch + radians{pitch},
        -camera::max_pitch, camera::max_pitch);
  }

  // Aim the zoom: a positive delta (wheel up) targets a smaller boom, toward
  // first person. The head only glides there in `update`, so it reads as the
  // saucer moving rather than snapping.
  void zoom(float delta) {
    boom_target =
        std::clamp(boom_target - delta, tune.boom_min, tune.boom_max);
  }

  // Advance the frame: ease the boom toward its zoom target, turn the belly
  // spin, swing the heading toward the look while moving (so the head leads
  // travel but holds still for free-look), and drive the thrust glow from how
  // fast the saucer is moving and zooming.
  void update(float dt) {
    const float old_boom = boom;
    boom = boom +
           ((boom_target - boom) * (1.0F - expf(-tune.zoom_approach * dt)));
    spin = spin + (tune.spin_rate * dt);

    if (moving > 0.0F) {
      const radians delta =
          radians_atan2(sin(facing.yaw - heading), cos(facing.yaw - heading));
      heading = heading + (delta * (1.0F - expf(-tune.heading_approach * dt)));
    }

    const float planar = dt > 0.0F ? moving / dt : 0.0F;
    const float zoom_speed = dt > 0.0F ? fabsf(boom - old_boom) / dt : 0.0F;
    const float target = fminf((planar + zoom_speed) / tune.thrust_full, 1.0F);
    thrust = thrust +
             ((target - thrust) * (1.0F - expf(-tune.thrust_approach * dt)));
  }

  // The view rays for the current pose, looking out from inside the head. The
  // field of view's `tan(fov/2)` is cached in `tune` and recomputed only when
  // the field of view is edited, so this stays a plain read.
  [[nodiscard]] camera_rays rays() const {
    return {eye(), frame(), tune.tan_half_fov()};
  }

  [[nodiscard]] metal_ball ball() const { return {anchor, tune.ball_radius}; }
  [[nodiscard]] saucer_head head() const {
    return {eye(), saucer_up(), tune.head_radius, spin, thrust,
        tune.body_height, tune.dome_offset, tune.dome_radius, tune.dome_blend};
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

#pragma endregion

} // namespace

#pragma region Main loop

int main() {
  try {
    sdl_subsystem sdl;
    sdl_window win{"Corvid Voxel Viewer", 1280, 720,
        sdl_window_flags::resizable};
    win.set_minimum_size(640, 480).or_throw();

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
    const density_field field{vol_extent, pos3{vec3{ox, oy, oz}}, voxel_size,
        volume.texture()};

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

    // The avatar starts floating above the origin (no gravity yet), looking
    // toward -z and slightly down, in a third-person view pulled back from the
    // head. The mouse wheel zooms in toward first person; the right-drag
    // orbits. The feel constants (field of view and the rest) default from
    // `avatar_tuning`, edited live by the config panel.
    avatar_rig rig{pos3{vec3{0.0F, 10.0F, 0.0F}},
        orientation{-90.0_deg, -20.0_deg}, -90.0_deg, 7.0F, 7.0F};
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

    // Frame timing in nanoseconds: millisecond ticks quantize dt enough to
    // judder movement at high refresh rates (a 6.94 ms frame reads as 6 or 7).
    auto last_ns = SDL_GetTicksNS();

    // Frame-time stats over a one-second window, shown in the title bar to
    // tell steady pacing apart from periodic spikes.
    frame_stats stats;

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

      // Smooth the look before moving so a strafe uses this frame's facing.
      const auto [yaw, pitch] = input.look(dt);
      rig.look(yaw, pitch);

      const auto [fwd, strafe, lift] = input.movement(dt, rig.tune.move_speed);
      rig.move(fwd, strafe, lift);

      // The mouse wheel aims the zoom between third and first person: an
      // impulse, not a held velocity, so it is not scaled by frame time. The
      // head then glides toward that target in `update`, so a zoom slides the
      // saucer in or out rather than snapping.
      if (input.wheel != 0.0F) rig.zoom(input.dolly());

      // Ease the boom toward its zoom target and turn the saucer's belly spin.
      rig.update(dt);

      // Skip all GPU work when the window has no client area (minimized, or
      // collapsed by the OS during a mixed-DPI cross-monitor drag).
      if (!presenter.window_width() || !presenter.window_height()) {
        SDL_Delay(16);
        continue;
      }

      // Fold this frame's wall-clock time into the stats; refresh the title
      // once a window passes a second.
      if (const auto report = stats.record(dt)) {
        char title[128];
        SDL_snprintf(title, sizeof(title),
            "Corvid Voxel Viewer - %.0f fps  %.1f/%.1f/%.1f ms (min/avg/max)",
            report->fps, report->min_ms, report->avg_ms, report->max_ms);
        SDL_SetWindowTitle(win, title);
      }

      const camera_rays rays = rig.rays();
      const metal_ball ball = rig.ball();
      const saucer_head head = rig.head();

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
            render_defaults);

      presenter
          .render(
              [&](cudaArray_t array, int w, int h) {
                const dim3 grid_dim{cuda_kernel::ceil_div(w, block.x),
                    cuda_kernel::ceil_div(h, block.y)};
                voxel_kernel<<<grid_dim, block>>>(cuda_surface{array},
                    resolution{static_cast<float>(w), static_cast<float>(h)},
                    rays, field, colors.texture(), ball, head, render_cfg);
              },
              [&] { imgui.render(presenter.back_buffer()); })
          .or_throw();
    }
    return 0;
  }
  catch (const std::exception& e) {
    // `main` is the boundary: report and exit rather than letting it escape.
    SDL_Log("fatal: %s", e.what());
    return 1;
  }
}

#pragma endregion
