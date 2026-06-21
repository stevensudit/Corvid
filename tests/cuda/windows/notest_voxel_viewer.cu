// CUDA-driven voxel ray marcher (Windows-only CUDA cell, crossplatform.md
// section 11), the second 3D rung after the SDF raymarch viewer. A density
// field lives in VRAM as a 3D texture; a kernel fixed-step marches it per
// pixel to the first solid voxel, shades it, and writes the result into the
// interop texture through `cudaGraphicsD3D11*`; D3D copies it to the
// backbuffer and presents. The frame stays on the GPU. Unlike the SDF viewer's
// analytic scene, the geometry is sampled from the field, so it can be edited
// in place (the next rung). A free-fly camera moves with WASD (Space/Ctrl up
// and down, Shift to go faster) or the mouse wheel (forward/back), and looks
// with the mouse while the right button is held; Escape quits.

#include <cmath>
#include <cstdint>
#include <exception>
#include <stdexcept>

#include <cuda_runtime.h>

#include "corvid/cuda/camera.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_surface.cuh"
#include "corvid/cuda/cuda_volume.cuh"
#include "corvid/cuda/density_field.cuh"
#include "corvid/cuda/radians.cuh"
#include "corvid/cuda/raycast.cuh"
#include "corvid/cuda/terrain.cuh"
#include "corvid/cuda/vec.cuh"
#include "corvid/cuda/windows/cuda_d3d11_presenter.cuh"
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

#pragma region Terrain

// Fill the field once from the terrain heightfield: solid below the surface,
// air above.
__global__ void fill_kernel(cudaSurfaceObject_t surface, density_field field) {
  const int ix = cuda_kernel::x_index();
  const int iy = cuda_kernel::y_index();
  const int iz = cuda_kernel::z_index();
  const int3 voxel = make_int3(ix, iy, iz);
  if (!field.contains(voxel)) return;

  const vec3 w = field.voxel_center(voxel).v;
  const float density = terrain::height(w.x, w.z) - w.y;
  surf3Dwrite(density, surface, ix * static_cast<int>(sizeof(float)), iy, iz);
}

#pragma endregion
#pragma region Rendering

// Fixed-step march the density field for every pixel and write the shaded
// color. The texture is `R8G8B8A8_UNORM`, so a `uchar4` of (r, g, b, a) maps
// straight to its bytes.
__global__ void voxel_kernel(cudaSurfaceObject_t out, resolution res,
    camera_rays cam, density_field field) {
  const int px = cuda_kernel::x_index();
  const int py = cuda_kernel::y_index();
  const auto fx = static_cast<float>(px);
  const auto fy = static_cast<float>(py);
  if (fx >= res.width || fy >= res.height) return;

  const vec3 ray_dir = cam.ray_direction(pos2{vec2{fx, fy}}, res);

  const float dist = field.raymarch(cam.eye, ray_dir);
  const bool hit = dist >= 0.0F;

  vec3 color;
  if (hit) {
    const pos3 hit_point = cam.eye + (ray_dir * dist);
    const vec3 normal = field.normal(hit_point);
    const vec3 light_dir = normalize(vec3{0.5F, 0.8F, 0.3F});
    const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);
    const vec3 ambient{0.18F, 0.20F, 0.24F};
    const vec3 sun{1.0F, 0.96F, 0.88F};
    const vec3 albedo{0.46F, 0.42F, 0.34F};
    color = albedo * (ambient + (sun * diffuse));
  } else {
    // Sky gradient by ray height.
    const float blend = (0.5F * ray_dir.y) + 0.5F;
    color = (vec3{0.50F, 0.70F, 1.00F} * blend) +
            (vec3{0.90F, 0.95F, 1.00F} * (1.0F - blend));
  }

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

#pragma endregion

} // namespace

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
    cuda_volume volume{vol_extent};
    const float ox =
        -0.5F * static_cast<float>(vol_extent.width - 1) * voxel_size;
    const float oy =
        -0.5F * static_cast<float>(vol_extent.height - 1) * voxel_size;
    const float oz =
        -0.5F * static_cast<float>(vol_extent.depth - 1) * voxel_size;
    const density_field field{vol_extent, pos3{vec3{ox, oy, oz}}, voxel_size,
        volume.texture()};

    const dim3 fill_block{8, 8, 8};
    const dim3 fill_grid{cuda_kernel::ceil_div(vol_extent.width, fill_block.x),
        cuda_kernel::ceil_div(vol_extent.height, fill_block.y),
        cuda_kernel::ceil_div(vol_extent.depth, fill_block.z)};
    fill_kernel<<<fill_grid, fill_block>>>(volume.surface(), field);
    cuda_last_status{cudaDeviceSynchronize()}.or_throw();

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

    // Start high and back, looking toward -z and down at the terrain, with a
    // 60-degree vertical field of view.
    camera cam{pos3{vec3{0.0F, 16.0F, 28.0F}},
        orientation{-90.0_deg, -25.0_deg}, 60.0_deg};
    fly_input input;

    const dim3 block{16, 16};

    // The GPU presentation pipeline: a CUDA-written render target copied to
    // the swapchain backbuffer and presented. It owns resize growth and
    // lost-device recovery (crossplatform.md section 11).
    cuda_d3d11_presenter presenter{hwnd};

    // Frame timing in nanoseconds: millisecond ticks quantize dt enough to
    // judder movement at high refresh rates (a 6.94 ms frame reads as 6 or 7).
    auto last_ns = SDL_GetTicksNS();

    // Frame-time stats over a one-second window, shown in the title bar to
    // tell steady pacing apart from periodic spikes.
    frame_stats stats;

    while (true) {
      const auto action = pump_events([&](const sdl_event& ev) {
        return input.handle(ev, win) || handle_dig(ev, digging);
      });
      if (action == frame_action::quit) break;
      if (action == frame_action::resize) presenter.resize().or_throw();

      const auto now_ns = SDL_GetTicksNS();
      const auto dt = static_cast<float>(now_ns - last_ns) / 1.0e9F;
      last_ns = now_ns;

      // Smooth the look before moving so a strafe uses this frame's facing.
      const auto [yaw, pitch] = input.look(dt);
      cam.look({radians{yaw}, radians{pitch}});

      const auto [fwd, strafe, lift] = input.movement(dt, 8.0F);
      cam.move(fwd, strafe, lift);

      // The mouse wheel dollies forward/back in fixed steps: an impulse, not a
      // held velocity, so it is not scaled by frame time.
      if (input.wheel != 0.0F) cam.move(input.dolly(), 0.0F, 0.0F);

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

      const camera_rays rays = cam.rays();

      // Carve the field at the crosshair while the left button is held. The
      // pick records the hit in device memory and the brush reads it there, so
      // the dig stays on the GPU; the next frame's march shows the hole.
      if (digging) {
        pick_kernel<<<1, 1>>>(field, rays.eye, rays.frame.forward, dig_target);
        dig_kernel<<<dig_grid, dig_block>>>(volume.surface(), field,
            dig_target, dig_radius, dig_rate * dt);
      }

      presenter
          .render([&](cudaArray_t array, int w, int h) {
            const dim3 grid_dim{cuda_kernel::ceil_div(w, block.x),
                cuda_kernel::ceil_div(h, block.y)};
            voxel_kernel<<<grid_dim, block>>>(cuda_surface{array},
                resolution{static_cast<float>(w), static_cast<float>(h)}, rays,
                field);
          })
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
