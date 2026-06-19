// CUDA-driven fractal viewer (Windows-only CUDA cell, crossplatform.md section
// 11), step B2. A CUDA kernel computes a Mandelbrot set into a VRAM texture
// through `cudaGraphicsD3D11*` interop; D3D copies it to the swapchain
// backbuffer and presents. The frame stays on the GPU (the copy is
// VRAM-to-VRAM, no PCIe round-trip). The kernel targets a separate texture
// rather than the backbuffer because CUDA cannot register the primary render
// target. The view is fixed for now; pan/zoom is a later step.

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
__global__ void
mandelbrot_kernel(cudaSurfaceObject_t surface, int width, int height) {
  const int px = cuda_kernel::x_index();
  const int py = cuda_kernel::y_index();
  if (px >= width || py >= height) return;

  // Map the pixel to the complex plane: centered at -0.5 + 0i, a fixed height
  // of 2.5, and a width that preserves the pixel aspect ratio.
  const float aspect = static_cast<float>(width) / static_cast<float>(height);
  const float view_height = 2.5F;
  const float view_width = view_height * aspect;
  const float nx = (static_cast<float>(px) / static_cast<float>(width)) - 0.5F;
  const float ny =
      (static_cast<float>(py) / static_cast<float>(height)) - 0.5F;
  const float cx = -0.5F + (nx * view_width);
  const float cy = ny * view_height;

  constexpr int max_iter = 256;
  float zx = 0.0F;
  float zy = 0.0F;
  int iter = 0;
  for (; iter < max_iter; ++iter) {
    const float zx2 = zx * zx;
    const float zy2 = zy * zy;
    if (zx2 + zy2 > 4.0F) break;
    zy = (2.0F * zx * zy) + cy;
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
      while (auto ev = sdl_event::poll())
        if (ev.type() == sdl_event_type::quit) running = false;

      if (cuda_d3d11_mapping map{cuda_target})
        mandelbrot_kernel<<<grid, block>>>(cuda_surface{map.array()},
            static_cast<int>(w), static_cast<int>(h));

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
