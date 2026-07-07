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
#include <cstdint>
#include <exception>

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "corvid/cuda/cuda_surface.cuh"
#include "corvid/cuda/windows/cuda_d3d11_presenter.cuh"
#include "corvid/sdl/frame_loop.h"
#include "corvid/sdl/frame_stats.h"
#include "corvid/sdl/sdl_event.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "corvid/sdl/sdl_window.h"

using namespace corvid::sdl;
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

  // Map the pixel to the complex plane: centered at (`center_x`, `center_y`),
  // the given view height, and a width that preserves the pixel aspect ratio.
  // The math is double precision so deep zooms stay smooth past the ~30,000x
  // where float pixels would coalesce.
  const double aspect =
      static_cast<double>(width) / static_cast<double>(height);
  const double view_width = view_height * aspect;
  const double nx =
      (static_cast<double>(px) / static_cast<double>(width)) - 0.5;
  const double ny =
      (static_cast<double>(py) / static_cast<double>(height)) - 0.5;
  const double cx = center_x + (nx * view_width);
  const double cy = center_y + (ny * view_height);

  constexpr double escape_radius_sq = 4.0; // bailout |z| > 2
  double zx = 0.0;
  double zy = 0.0;
  int iter = 0;
  for (; iter < max_iter; ++iter) {
    const double zx2 = zx * zx;
    const double zy2 = zy * zy;
    if (zx2 + zy2 > escape_radius_sq) break;
    zy = (2.0 * zx * zy) + cy;
    zx = (zx2 - zy2) + cx;
  }

  uchar4 pixel;
  if (iter == max_iter) {
    pixel = make_uchar4(0, 0, 0, 255); // inside the set
  } else {
    // Smooth Bernstein-polynomial palette over the escape fraction. The
    // coefficients are tuned so R/G/B peak at different escape depths.
    const float t = static_cast<float>(iter) / static_cast<float>(max_iter);
    constexpr float palette_r = 9.0F;
    constexpr float palette_g = 15.0F;
    constexpr float palette_b = 8.5F;
    const auto r = static_cast<unsigned char>(
        palette_r * (1.0F - t) * t * t * t * 255.0F);
    const auto g = static_cast<unsigned char>(
        palette_g * (1.0F - t) * (1.0F - t) * t * t * 255.0F);
    const auto b = static_cast<unsigned char>(
        palette_b * (1.0F - t) * (1.0F - t) * (1.0F - t) * t * 255.0F);
    pixel = make_uchar4(r, g, b, 255);
  }
  surf2Dwrite(pixel, surface, px * static_cast<int>(sizeof(uchar4)), py);
}

// Mutable view into the complex plane: its center and vertical extent.
struct fractal_view {
  double center_x = -0.5;
  double center_y = 0.0;
  double view_height = 2.5;
};

// Fold one event into the view: zoom toward the cursor (wheel), pan with a
// left-button drag (motion), or pan by a fixed fraction of the view (arrow
// keys). `fwidth`/`fheight` are the pixel size, converting cursor positions to
// complex-plane offsets. Returns whether it consumed the event.
[[nodiscard]] bool handle_pan_zoom(const sdl_event& ev, fractal_view& view,
    double fwidth, double fheight) {
  switch (ev.type()) {
  case sdl_event_type::mouse_wheel: {
    // Zoom toward the cursor: scale `view_height` while keeping the complex
    // point under the cursor fixed.
    const auto wheel = ev.get_wheel();
    const double upp = view.view_height / fheight;
    constexpr double zoom_step = 0.9; // view height per wheel notch (10% in)
    const double factor = std::pow(zoom_step, wheel.y);
    view.center_x += (wheel.mouse_x - (fwidth / 2.0)) * upp * (1.0 - factor);
    view.center_y += (wheel.mouse_y - (fheight / 2.0)) * upp * (1.0 - factor);
    view.view_height *= factor;
    return true;
  }

  case sdl_event_type::mouse_motion: {
    // Left-button drag pans the view by the cursor movement.
    const auto motion = ev.get_motion();
    if (motion.left_held) {
      const double upp = view.view_height / fheight;
      view.center_x -= motion.xrel * upp;
      view.center_y -= motion.yrel * upp;
    }
    return true;
  }

  case sdl_event_type::key_down: {
    // Arrow keys pan by a fixed fraction of the view (so the step scales with
    // zoom). Repeats fire while a key is held.
    const double step = 0.05 * view.view_height;
    switch (ev.get_key().key) {
    case sdl_keycode::left: view.center_x -= step; return true;
    case sdl_keycode::right: view.center_x += step; return true;
    case sdl_keycode::up: view.center_y -= step; return true;
    case sdl_keycode::down: view.center_y += step; return true;
    default: return false;
    }
  }

  default: return false;
  }
}
} // namespace

int main() {
  try {
    sdl_subsystem sdl;
    sdl_window win{"Corvid Fractal Viewer", 1280, 720,
        sdl_window_flags::resizable};
    win.set_minimum_size(640, 480).or_throw();

    const auto hwnd = static_cast<HWND>(win.native_handle());

    // The live view into the complex plane, mutated by input in `pump_events`.
    // The per-axis scale is isotropic: complex units per pixel is
    // `view_height / h`.
    fractal_view view;

    const dim3 block{16, 16};

    // The GPU presentation pipeline: a CUDA-written render target copied to
    // the swapchain backbuffer and presented. It owns resize growth and
    // lost-device recovery (crossplatform.md section 11).
    cuda_d3d11_presenter presenter{hwnd};

    // Frame timing in nanoseconds, folded into the title-bar stats below.
    auto last_ns = SDL_GetTicksNS();

    // Frame-time stats over a one-second window, shown in the title bar to
    // tell steady pacing apart from periodic spikes.
    frame_stats stats;

    while (true) {
      const auto fwidth = presenter.buffer_width<double>();
      const auto fheight = presenter.buffer_height<double>();
      const auto action = pump_events([&](const sdl_event& ev) {
        return handle_pan_zoom(ev, view, fwidth, fheight);
      });
      if (action == frame_action::quit) break;

      // Apply a coalesced resize: rebuild the swapchain buffers and grow the
      // render target when the new size exceeds its capacity.
      if (action == frame_action::resize) presenter.resize().or_throw();

      const auto now_ns = SDL_GetTicksNS();
      const auto dt = static_cast<float>(now_ns - last_ns) / 1.0e9F;
      last_ns = now_ns;

      // Skip all GPU work when the window has no client area (minimized, or
      // collapsed by the OS during a mixed-DPI cross-monitor drag): rendering
      // to a zero-size window wastes the kernel and may fail to present. The
      // size is current as of the resize handled just above.
      if (presenter.window_width() == 0 || presenter.window_height() == 0) {
        SDL_Delay(16);
        continue;
      }

      // Fold this frame's wall-clock time into the stats; refresh the title
      // once a window passes a second.
      if (const auto report = stats.record(dt)) {
        char title[128];
        SDL_snprintf(title, sizeof(title),
            "Corvid Fractal Viewer - %.0f fps  %.1f/%.1f/%.1f ms "
            "(min/avg/max)",
            report->fps, report->min_ms, report->avg_ms, report->max_ms);
        SDL_SetWindowTitle(win, title);
      }

      // Scale the iteration cap with zoom depth (~200 more per 2x), so detail
      // tracks the zoom instead of saturating at a fixed count. Clamped to
      // keep a deep zoom from stalling the fp64 kernel.
      constexpr double base_iter = 256.0;
      constexpr double iter_per_octave = 200.0;
      constexpr double max_iter_cap = 8000.0;
      const auto max_iter = static_cast<int>(std::clamp(
          base_iter + (iter_per_octave * std::log2(2.5 / view.view_height)),
          base_iter, max_iter_cap));

      presenter
          .render([&](cudaArray_t array, int w, int h) {
            const dim3 grid{cuda_kernel::ceil_div(w, block.x),
                cuda_kernel::ceil_div(h, block.y)};
            mandelbrot_kernel<<<grid, block>>>(cuda_surface{array}, w, h,
                view.center_x, view.center_y, view.view_height, max_iter);
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
