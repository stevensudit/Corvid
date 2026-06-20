// CUDA-driven SDF ray marcher (Windows-only CUDA cell, crossplatform.md
// section 11), the first 3D rung after the fractal viewer. A CUDA kernel
// sphere-traces a small signed-distance scene per pixel and shades it (a
// directional light with soft shadows and ambient occlusion), writing the
// result into a VRAM texture through `cudaGraphicsD3D11*` interop; D3D copies
// it to the swapchain backbuffer and presents. The frame stays on the GPU (the
// copy is VRAM-to-VRAM, no PCIe round-trip). The kernel targets a separate
// texture rather than the backbuffer because CUDA cannot register the primary
// render target. A free-fly camera moves with WASD (Space/Ctrl up and down,
// Shift to go faster) or the mouse wheel (forward/back), and looks with the
// mouse while the right button is held; Escape quits.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>

#include <cuda_runtime.h>

#include "corvid/cuda/camera.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_surface.cuh"
#include "corvid/cuda/radians.cuh"
#include "corvid/cuda/raycast.cuh"
#include "corvid/cuda/sdf.cuh"
#include "corvid/cuda/vec.cuh"
#include "corvid/cuda/windows/cuda_d3d11_presenter.cuh"
#include "corvid/math/one_euro_filter.h"
#include "corvid/sdl/sdl_event.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "corvid/sdl/sdl_window.h"

using namespace corvid::sdl;
using namespace corvid::cuda;

namespace {

// The demo scene as a `shade_ray` policy: a signed-distance field, a
// per-object albedo, and a sky. A ground plane at `y = -1` with a few
// primitives resting on it, two of them smoothly blended.
struct scene {
  // Signed distance to the scene at `point`.
  static __device__ float sdf(pos3 point) {
    // Inside the field, work in vector space (offsets from the origin).
    const vec3 p = point.v;
    float dist = sd_plane(p, vec3{0.0F, 1.0F, 0.0F}, 1.0F);
    dist = op_union(dist, sd_sphere(p - vec3{-1.3F, 0.0F, 0.0F}, 1.0F));
    dist = op_smooth_union(dist, sd_sphere(p - vec3{1.1F, -0.3F, 0.6F}, 0.7F),
        0.5F);
    dist = op_union(dist,
        sd_box(p - vec3{0.2F, -0.5F, -2.0F}, vec3{0.8F, 0.5F, 0.8F}));
    return dist;
  }

  // Base color of the surface at `point`: a distinct color per object so each
  // reads clearly against the others and the sky.
  static __device__ vec3 albedo(pos3 point) {
    switch (material(point)) {
    case 1: return vec3{0.80F, 0.25F, 0.20F};  // red sphere
    case 2: return vec3{0.20F, 0.40F, 0.80F};  // blue sphere
    case 3: return vec3{0.85F, 0.65F, 0.18F};  // amber box
    default: return vec3{0.32F, 0.40F, 0.26F}; // green ground
    }
  }

  // Background for a ray that leaves the scene: a gradient by ray height.
  static __device__ vec3 sky(vec3 dir) {
    const float blend = (0.5F * dir.y) + 0.5F;
    return (vec3{0.50F, 0.70F, 1.00F} * blend) +
           (vec3{0.90F, 0.95F, 1.00F} * (1.0F - blend));
  }

private:
  // Material id of the nearest primitive at `point`. Mirrors `sdf`; only the
  // final hit needs it, so the march and lighting stay distance-only.
  static __device__ int material(pos3 point) {
    const vec3 p = point.v;
    float best = sd_plane(p, vec3{0.0F, 1.0F, 0.0F}, 1.0F);
    int id = 0; // ground
    float dist = sd_sphere(p - vec3{-1.3F, 0.0F, 0.0F}, 1.0F);
    if (dist < best) {
      best = dist;
      id = 1;
    }
    dist = sd_sphere(p - vec3{1.1F, -0.3F, 0.6F}, 0.7F);
    if (dist < best) {
      best = dist;
      id = 2;
    }
    dist = sd_box(p - vec3{0.2F, -0.5F, -2.0F}, vec3{0.8F, 0.5F, 0.8F});
    if (dist < best) id = 3;
    return id;
  }
};

// Sphere-trace the `scene` for every pixel and write the shaded color. The
// texture is `R8G8B8A8_UNORM`, so a `uchar4` of (r, g, b, a) maps straight to
// its bytes.
__global__ void
raymarch_kernel(cudaSurfaceObject_t surface, resolution res, camera_rays cam) {
  const int px = cuda_kernel::x_index();
  const int py = cuda_kernel::y_index();
  const auto fx = static_cast<float>(px);
  const auto fy = static_cast<float>(py);
  if (fx >= res.width || fy >= res.height) return;

  const vec3 ray_dir = cam.ray_direction(pos2{vec2{fx, fy}}, res);
  // One pixel's cone in tangent space: the marcher's distance-scaled hit
  // threshold.
  const float pixel_eps = cam.tan_half_fov / (0.5F * res.height);
  const vec3 light_dir = normalize(vec3{0.5F, 0.8F, 0.3F});

  const vec3 color = shade_ray<scene>(cam.eye, ray_dir, pixel_eps, light_dir);

  const uchar4 pixel =
      make_uchar4(to_byte(color.x), to_byte(color.y), to_byte(color.z), 255);
  surf2Dwrite(pixel, surface, px * static_cast<int>(sizeof(uchar4)), py);
}

// Which movement keys are held this frame.
struct fly_keys {
  bool forward = false;
  bool back = false;
  bool left = false;
  bool right = false;
  bool up = false;
  bool down = false;
  bool fast = false;
};

// What the event pump asks the frame loop to do next.
enum class frame_action : std::uint8_t { proceed, resize, quit };

// Fold one batch of input into the held-key set, `look_dx`/`look_dy` (the
// frame's accumulated mouse-look delta in raw counts), and `wheel` (the
// frame's accumulated wheel scroll), and report any `quit` or `resize`
// request. Holding the right mouse button captures the cursor for mouse-look
// and gates the look delta; the caller smooths and applies it. Escape quits.
frame_action pump_events(fly_keys& keys, bool& looking, sdl_window& win,
    float& look_dx, float& look_dy, float& wheel) {
  look_dx = 0.0F;
  look_dy = 0.0F;
  wheel = 0.0F;
  auto action = frame_action::proceed;
  while (auto ev = sdl_event::poll()) {
    switch (ev.type()) {
    case sdl_event_type::quit:
    case sdl_event_type::window_close_requested: return frame_action::quit;

    case sdl_event_type::window_pixel_size_changed:
      // Coalesce a drag's event storm into one rebuild before the frame.
      action = frame_action::resize;
      break;

    case sdl_event_type::mouse_button_down:
    case sdl_event_type::mouse_button_up: {
      const auto button = ev.get_button();
      if (button.button == sdl_mouse_button::right) {
        looking = button.down;
        win.set_relative_mouse_mode(looking).or_throw();
      }
      break;
    }

    case sdl_event_type::mouse_motion:
      if (looking) {
        const auto motion = ev.get_motion();
        look_dx += motion.xrel;
        look_dy += motion.yrel;
      }
      break;

    case sdl_event_type::mouse_wheel: wheel += ev.get_wheel().y; break;

    case sdl_event_type::key_down:
    case sdl_event_type::key_up: {
      const auto key = ev.get_key();
      const bool down = key.down;
      switch (key.key) {
      case sdl_keycode::w: keys.forward = down; break;
      case sdl_keycode::s: keys.back = down; break;
      case sdl_keycode::a: keys.left = down; break;
      case sdl_keycode::d: keys.right = down; break;
      case sdl_keycode::space: keys.up = down; break;
      case sdl_keycode::lctrl: keys.down = down; break;
      case sdl_keycode::lshift: keys.fast = down; break;
      case sdl_keycode::escape:
        if (down) return frame_action::quit;
        break;
      default: break;
      }
      break;
    }

    default: break;
    }
  }
  return action;
}

// Translate the held-key set into a camera move scaled by frame time.
void apply_movement(camera& cam, const fly_keys& keys, float dt) {
  const float speed = (keys.fast ? 12.0F : 4.0F) * dt;
  const float forward =
      (keys.forward ? 1.0F : 0.0F) - (keys.back ? 1.0F : 0.0F);
  const float strafe = (keys.right ? 1.0F : 0.0F) - (keys.left ? 1.0F : 0.0F);
  const float lift = (keys.up ? 1.0F : 0.0F) - (keys.down ? 1.0F : 0.0F);
  if (forward != 0.0F || strafe != 0.0F || lift != 0.0F)
    cam.move(forward * speed, strafe * speed, lift * speed);
}

} // namespace

int main() {
  try {
    sdl_subsystem sdl;
    sdl_window win{"Corvid Raymarch Viewer", 1280, 720,
        sdl_window_flags::resizable};
    win.set_minimum_size(640, 480).or_throw();

    // The cursor stays free until the right mouse button is held, which
    // captures it for mouse-look.
    bool looking = false;

    const auto hwnd = static_cast<HWND>(win.native_handle());

    // Start above the ground a few units back, looking toward -z, with a
    // 60-degree vertical field of view.
    camera cam{pos3{vec3{0.0F, 0.5F, 5.0F}}, orientation{-90.0_deg, 0.0_deg},
        60.0_deg};
    fly_keys keys;

    const dim3 block{16, 16};

    // The GPU presentation pipeline: a CUDA-written render target copied to
    // the swapchain backbuffer and presented. It owns resize growth and
    // lost-device recovery (crossplatform.md section 11).
    cuda_d3d11_presenter presenter{hwnd};

    // Frame timing in nanoseconds: millisecond ticks quantize dt enough to
    // judder movement at high refresh rates (a 6.94 ms frame reads as 6 or 7).
    auto last_ns = SDL_GetTicksNS();

    // Frame-time stats over a one-second window, shown in the title bar to
    // tell steady pacing apart from periodic spikes. The max is what reveals
    // judder that an average frame rate hides.
    int stat_frames = 0;
    float stat_ms_sum = 0.0F;
    float stat_ms_min = 0.0F;
    float stat_ms_max = 0.0F;
    float stat_window = 0.0F;

    // Mouse-look smoothing via a One Euro Filter: heavy at the low, steady
    // speeds where the per-frame mouse jitter shows, easing off as the mouse
    // speeds up so a fast flick stays responsive. The first argument is the
    // at-rest smoothing (a ~60 ms time constant, titrated by hand); `beta`
    // sets how fast it eases off with speed. Tune `beta` up if fast flicks
    // feel laggy, down if they get jittery; 0 makes it a plain fixed low-pass.
    const float look_sensitivity = 0.0025F;
    // Distance the mouse wheel dollies the camera forward per scroll notch.
    const float scroll_step = 0.5F;
    corvid::one_euro_filter look_filter{60.0F, 0.001F};

    while (true) {
      float look_dx = 0.0F;
      float look_dy = 0.0F;
      float wheel = 0.0F;
      const auto action =
          pump_events(keys, looking, win, look_dx, look_dy, wheel);
      if (action == frame_action::quit) break;
      if (action == frame_action::resize) presenter.resize().or_throw();

      const auto now_ns = SDL_GetTicksNS();
      const auto dt = static_cast<float>(now_ns - last_ns) / 1.0e9F;
      last_ns = now_ns;

      // Smooth the look before moving so a strafe uses this frame's facing.
      // Releasing the button forgets the carried state so it neither glides on
      // after release nor fires on the next grab.
      if (looking) {
        look_filter.smooth(dt, look_dx, look_dy);
        cam.look({radians{look_dx * look_sensitivity},
            radians{-look_dy * look_sensitivity}});
      } else {
        look_filter.reset();
      }

      apply_movement(cam, keys, dt);

      // The mouse wheel dollies forward/back in fixed steps: an impulse, not a
      // held velocity, so it is not scaled by frame time.
      if (wheel != 0.0F) cam.move(wheel * scroll_step, 0.0F, 0.0F);

      // Skip all GPU work when the window has no client area (minimized, or
      // collapsed by the OS during a mixed-DPI cross-monitor drag).
      if (!presenter.window_width() || !presenter.window_height()) {
        SDL_Delay(16);
        continue;
      }

      // Fold this frame's wall-clock time into the stats, refreshing the title
      // once the window passes a second. The first frame of a window seeds the
      // min and max.
      const float frame_ms = dt * 1000.0F;
      stat_ms_min =
          (stat_frames == 0) ? frame_ms : std::min(stat_ms_min, frame_ms);
      stat_ms_max =
          (stat_frames == 0) ? frame_ms : std::max(stat_ms_max, frame_ms);
      stat_ms_sum += frame_ms;
      ++stat_frames;
      stat_window += dt;
      if (stat_window >= 1.0F) {
        char title[128];
        SDL_snprintf(title, sizeof(title),
            "Corvid Raymarch Viewer - %.0f fps  %.1f/%.1f/%.1f ms "
            "(min/avg/max)",
            static_cast<float>(stat_frames) / stat_window, stat_ms_min,
            stat_ms_sum / static_cast<float>(stat_frames), stat_ms_max);
        SDL_SetWindowTitle(win, title);
        stat_frames = 0;
        stat_ms_sum = 0.0F;
        stat_window = 0.0F;
      }

      const camera_rays rays = cam.rays();
      presenter
          .render([&](cudaArray_t array, int w, int h) {
            const dim3 grid{cuda_kernel::ceil_div(w, block.x),
                cuda_kernel::ceil_div(h, block.y)};
            raymarch_kernel<<<grid, block>>>(cuda_surface{array},
                resolution{static_cast<float>(w), static_cast<float>(h)},
                rays);
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
