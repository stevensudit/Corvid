// CUDA-driven SDF ray marcher (Windows-only CUDA cell, crossplatform.md
// section 11), the first 3D rung after the fractal viewer. A CUDA kernel
// sphere-traces a small signed-distance scene per pixel and shades it (a
// directional light with soft shadows and ambient occlusion), writing the
// result into a VRAM texture through `cudaGraphicsD3D11*` interop; D3D copies
// it to the swapchain backbuffer and presents. The frame stays on the GPU (the
// copy is VRAM-to-VRAM, no PCIe round-trip). The kernel targets a separate
// texture rather than the backbuffer because CUDA cannot register the primary
// render target. A free-fly camera moves with WASD (Space/Ctrl up and down,
// Shift to go faster) and looks with the mouse while the right button is held;
// Escape quits.

#include <cstdint>
#include <exception>

#include <cuda_runtime.h>

#include "corvid/cuda/camera.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_surface.cuh"
#include "corvid/cuda/radians.cuh"
#include "corvid/cuda/sdf.cuh"
#include "corvid/cuda/vec.cuh"
#include "corvid/cuda/windows/cuda_d3d11_presenter.cuh"
#include "corvid/sdl/sdl_event.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "corvid/sdl/sdl_window.h"

using namespace corvid::sdl;
using namespace corvid::cuda;

namespace {

// Signed distance to the demo scene: a ground plane at y = -1 with a few
// primitives resting on it, two of them smoothly blended.
__device__ float scene_sdf(pos3 point) {
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

// Surface normal at `p`, estimated from the gradient of the scene field by
// central differences.
__device__ vec3 scene_normal(pos3 p) {
  static constexpr float eps = 0.0005F;
  static constexpr vec3 dx{eps, 0.0F, 0.0F};
  static constexpr vec3 dy{0.0F, eps, 0.0F};
  static constexpr vec3 dz{0.0F, 0.0F, eps};
  return normalize(vec3{scene_sdf(p + dx) - scene_sdf(p - dx),
      scene_sdf(p + dy) - scene_sdf(p - dy),
      scene_sdf(p + dz) - scene_sdf(p - dz)});
}

// Penumbra factor in [0, 1] for the ray from `p` toward the light: 1 is fully
// lit, 0 fully shadowed. `hardness` sets how sharply the penumbra falls off.
__device__ float soft_shadow(pos3 p, vec3 light_dir, float hardness) {
  // Heuristic max steps to reach the light; more is softer but slower.
  static constexpr int limit = 48;

  float result = 1.0F;
  float dist = 0.02F;
  for (int step = 0; step < limit; ++step) {
    const float surf_dist = scene_sdf(p + (light_dir * dist));
    if (surf_dist < 0.001F) return 0.0F;

    result = fminf(result, (hardness * surf_dist) / dist);
    dist += surf_dist;
    if (dist > 20.0F) break;
  }
  return result;
}

// Ambient occlusion in [0, 1] at `p` with the given `normal`: 1 is unoccluded.
__device__ float ambient_occlusion(pos3 p, vec3 normal) {
  // Heuristic number of ambient occlusion samples; more is darker but slower.
  constexpr int ao_samples = 5;

  // Samples march outward from the surface, starting at `min_offset` and
  // stepping by `offset_step` (together spanning `offset_span`); nearer ones
  // weigh more, and `strength` scales the result.
  constexpr float min_offset = 0.01F;
  constexpr float offset_span = 0.12F;
  constexpr float offset_step =
      offset_span / static_cast<float>(ao_samples - 1);
  constexpr float weight_falloff = 0.7F;
  constexpr float strength = 3.0F;

  float occlusion = 0.0F;
  float weight = 1.0F;
  float offset = min_offset;
  for (int step = 0; step < ao_samples; ++step) {
    const float surf_dist = scene_sdf(p + (normal * offset));
    occlusion += (offset - surf_dist) * weight;
    weight *= weight_falloff;
    offset += offset_step;
  }
  return fminf(fmaxf(1.0F - (strength * occlusion), 0.0F), 1.0F);
}

// Convert a linear color channel in [0, 1] to a gamma-encoded byte.
__device__ unsigned char to_byte(float c) {
  const float g = powf(fminf(fmaxf(c, 0.0F), 1.0F), 1.0F / 2.2F);
  return static_cast<unsigned char>(lroundf(g * 255.0F));
}

// Sphere-trace the scene for every pixel and write the shaded color. The
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

  // March along the ray, stepping by the distance to the nearest surface until
  // we reach one or pass the far limit.
  float dist = 0.0F;
  bool hit = false;
  for (int step = 0; step < 128; ++step) {
    const float surf_dist = scene_sdf(cam.eye + (ray_dir * dist));
    if (surf_dist < 0.001F) {
      hit = true;
      break;
    }
    dist += surf_dist;
    if (dist > 50.0F) break;
  }

  vec3 color;
  if (hit) {
    const pos3 hit_point = cam.eye + (ray_dir * dist);
    const vec3 normal = scene_normal(hit_point);
    const vec3 light_dir = normalize(vec3{0.5F, 0.8F, 0.3F});
    const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);
    const float shadow = soft_shadow(hit_point, light_dir, 16.0F);
    const float occlusion = ambient_occlusion(hit_point, normal);

    // A checkerboard on the ground, flat gray on everything else.
    vec3 albedo{0.80F, 0.80F, 0.82F};
    if (hit_point.v.y < -0.99F) {
      const bool dark =
          (static_cast<int>(floorf(hit_point.v.x) + floorf(hit_point.v.z)) &
              1) != 0;
      albedo = dark ? vec3{0.12F, 0.12F, 0.14F} : vec3{0.80F, 0.80F, 0.82F};
    }

    const vec3 ambient{0.12F, 0.14F, 0.18F};
    const vec3 sun{1.0F, 0.97F, 0.90F};
    color = albedo * ((ambient * occlusion) + (sun * (diffuse * shadow)));
  } else {
    // A simple sky gradient by ray height.
    const float sky_blend = (0.5F * ray_dir.y) + 0.5F;
    color = (vec3{0.50F, 0.70F, 1.00F} * sky_blend) +
            (vec3{0.90F, 0.95F, 1.00F} * (1.0F - sky_blend));
  }

  // Reinhard tonemap so highlights roll off, then write gamma-encoded bytes.
  color = vec3{color.x / (1.0F + color.x), color.y / (1.0F + color.y),
      color.z / (1.0F + color.z)};
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

// Fold one batch of input into the camera and the held-key set, and report any
// `quit` or `resize` request. Holding the right mouse button captures the
// cursor for mouse-look; motion turns the camera only while it is held. Escape
// quits.
frame_action
pump_events(camera& cam, fly_keys& keys, bool& looking, sdl_window& win) {
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
        const float sensitivity = 0.0025F;
        cam.look({radians{motion.xrel * sensitivity},
            radians{-motion.yrel * sensitivity}});
      }
      break;

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

    auto last_ticks = SDL_GetTicks();

    while (true) {
      const auto action = pump_events(cam, keys, looking, win);
      if (action == frame_action::quit) break;
      if (action == frame_action::resize) presenter.resize().or_throw();

      const auto now = SDL_GetTicks();
      const auto dt = static_cast<float>(now - last_ticks) / 1000.0F;
      last_ticks = now;
      apply_movement(cam, keys, dt);

      // Skip all GPU work when the window has no client area (minimized, or
      // collapsed by the OS during a mixed-DPI cross-monitor drag).
      if (!presenter.window_width() || !presenter.window_height()) {
        SDL_Delay(16);
        continue;
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
