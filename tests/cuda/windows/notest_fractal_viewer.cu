// CUDA-driven fractal viewer (Windows-only CUDA cell, crossplatform.md section
// 11), step B2. A CUDA kernel computes a Mandelbrot set into a VRAM texture
// through `cudaGraphicsD3D11*` interop; D3D copies it to the swapchain
// backbuffer and presents. The frame stays on the GPU (the copy is
// VRAM-to-VRAM, no PCIe round-trip). The kernel targets a separate texture
// rather than the backbuffer because CUDA cannot register the primary render
// target. The view pans (left-button drag or arrow keys) and zooms toward the
// cursor (mouse wheel); the kernel re-renders every frame from the live view.

#include <algorithm>
#include <cmath>
#include <exception>

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "corvid/cuda/cuda_surface.cuh"
#include "corvid/cuda/windows/cuda_d3d11_interop.cuh"
#include "corvid/cuda/windows/d3d11_device.h"
#include "corvid/cuda/windows/d3d11_swapchain.h"
#include "corvid/sdl/sdl_event.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "corvid/sdl/sdl_window.h"

using namespace corvid::sdl;
using namespace corvid::win32;
using namespace corvid::win32::d3d;
using namespace corvid::cuda;

namespace {
// Mandelbrot escape-time iteration over the texture grid, written through the
// interop surface. The texture is `R8G8B8A8_UNORM`, so a `uchar4` of
// (r, g, b, a) maps straight to its bytes.
__global__ void mandelbrot_kernel(cudaSurfaceObject_t surface, int width,
    int height, double center_x, double center_y, double view_height,
    int max_iter) {
  const int px = cuda_kernel::x_index();
  const int py = cuda_kernel::y_index();
  if (px >= width || py >= height) return;

  // Map the pixel to the complex plane: centered at (center_x, center_y), the
  // given view height, and a width that preserves the pixel aspect ratio. The
  // math is double precision so deep zooms stay smooth past the ~30,000x where
  // float pixels would coalesce.
  const double aspect =
      static_cast<double>(width) / static_cast<double>(height);
  const double view_width = view_height * aspect;
  const double nx =
      (static_cast<double>(px) / static_cast<double>(width)) - 0.5;
  const double ny =
      (static_cast<double>(py) / static_cast<double>(height)) - 0.5;
  const double cx = center_x + (nx * view_width);
  const double cy = center_y + (ny * view_height);

  double zx = 0.0;
  double zy = 0.0;
  int iter = 0;
  for (; iter < max_iter; ++iter) {
    const double zx2 = zx * zx;
    const double zy2 = zy * zy;
    if (zx2 + zy2 > 4.0) break;
    zy = (2.0 * zx * zy) + cy;
    zx = (zx2 - zy2) + cx;
  }

  uchar4 pixel;
  if (iter == max_iter) {
    pixel = make_uchar4(0, 0, 0, 255); // inside the set
  } else {
    // Smooth Bernstein-polynomial palette over the escape fraction.
    const float t = static_cast<float>(iter) / static_cast<float>(max_iter);
    const auto r =
        static_cast<unsigned char>(9.0F * (1.0F - t) * t * t * t * 255.0F);
    const auto g = static_cast<unsigned char>(
        15.0F * (1.0F - t) * (1.0F - t) * t * t * 255.0F);
    const auto b = static_cast<unsigned char>(
        8.5F * (1.0F - t) * (1.0F - t) * (1.0F - t) * t * 255.0F);
    pixel = make_uchar4(r, g, b, 255);
  }
  surf2Dwrite(pixel, surface, px * static_cast<int>(sizeof(uchar4)), py);
}
} // namespace

int main() {
  try {
    sdl_subsystem sdl;
    sdl_window win{"Corvid Fractal Viewer", 1280, 720};

    d3d11_device device;
    d3d11_swapchain swapchain{device, static_cast<HWND>(win.native_handle())};

    const auto w = swapchain.width();
    const auto h = swapchain.height();
    const auto fwidth = static_cast<double>(w);
    const auto fheight = static_cast<double>(h);

    // The live view into the complex plane, mutated by input below. The
    // per-axis scale is isotropic: complex units per pixel is view_height / h.
    double center_x = -0.5;
    double center_y = 0.0;
    double view_height = 2.5;

    const dim3 block{16, 16};
    const dim3 grid{cuda_kernel::ceil_div(w, block.x),
        cuda_kernel::ceil_div(h, block.y)};

    // Create a texture for the kernel to write to, and for D3D to copy to the
    // backbuffer.
    com_ptr<ID3D11Texture2D> render_texture =
        swapchain.create_matching_texture(d3d11_bind_flag::shader_resource);
    cuda_d3d11_resource cuda_target{render_texture};

    bool running = true;
    while (running) {
      while (auto ev = sdl_event::poll()) {
        switch (ev.type()) {
        case sdl_event_type::quit: running = false; break;

        case sdl_event_type::mouse_wheel: {
          // Zoom toward the cursor: scale view_height while keeping the
          // complex point under the cursor fixed.
          const auto wheel = ev.get_wheel();
          const double upp = view_height / fheight;
          const double factor = std::pow(0.9, wheel.y);
          center_x += (wheel.mouse_x - (fwidth / 2.0)) * upp * (1.0 - factor);
          center_y += (wheel.mouse_y - (fheight / 2.0)) * upp * (1.0 - factor);
          view_height *= factor;
          break;
        }

        case sdl_event_type::mouse_motion: {
          // Left-button drag pans the view by the cursor movement.
          const auto motion = ev.get_motion();
          if (motion.left_held) {
            const double upp = view_height / fheight;
            center_x -= motion.xrel * upp;
            center_y -= motion.yrel * upp;
          }
          break;
        }

        case sdl_event_type::key_down: {
          // Arrow keys pan by a fixed fraction of the view (so the step scales
          // with zoom). Repeats fire while a key is held.
          const double step = 0.05 * view_height;
          switch (ev.get_key().key) {
          case sdl_keycode::left: center_x -= step; break;
          case sdl_keycode::right: center_x += step; break;
          case sdl_keycode::up: center_y -= step; break;
          case sdl_keycode::down: center_y += step; break;
          default: break;
          }
          break;
        }

        default: break;
        }
      }

      // Scale the iteration cap with zoom depth (~200 more per 2x), so detail
      // tracks the zoom instead of saturating at a fixed count. Clamped to
      // keep a deep zoom from stalling the fp64 kernel.
      const auto max_iter = static_cast<int>(std::clamp(
          256.0 + (200.0 * std::log2(2.5 / view_height)), 256.0, 8000.0));

      if (cuda_d3d11_mapping map{cuda_target})
        mandelbrot_kernel<<<grid, block>>>(cuda_surface{map.array()},
            static_cast<int>(w), static_cast<int>(h), center_x, center_y,
            view_height, max_iter);

      swapchain.fill_back_buffer(render_texture);
      swapchain.present().or_throw();
    }
    return 0;
  }
  catch (const std::exception& e) {
    // main is the boundary: report and exit rather than letting it escape.
    SDL_Log("fatal: %s", e.what());
    return 1;
  }
}
