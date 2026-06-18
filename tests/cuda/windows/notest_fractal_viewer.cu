// First milestone of the Windows-only CUDA cell (see crossplatform.md section
// 11): a CUDA-driven fractal viewer, built in two steps. This step stands up
// the Direct3D path the later CUDA work presents through. It opens an SDL
// window, pulls the HWND from it, creates an `ID3D11Device` and a flip-model
// swapchain, and each frame clears the backbuffer to a slowly cycling color
// and presents. No CUDA kernel and no D3D interop yet: a Mandelbrot kernel
// writing a D3D texture through `cudaGraphicsD3D11*` interop, presented
// without the frame crossing PCIe, is the next step.

#include <cmath>
#include <exception>

#include "corvid/cuda/windows/d3d11_device.h"
#include "corvid/cuda/windows/d3d11_swapchain.h"
#include "corvid/sdl/sdl_event.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "corvid/sdl/sdl_window.h"

using namespace corvid::sdl;
using corvid::d3d::d3d11_device;
using corvid::d3d::d3d11_swapchain;

int main() {
  try {
    sdl_subsystem sdl;
    sdl_window win{"Corvid Fractal Viewer", 1280, 720};

    d3d11_device device;
    d3d11_swapchain swapchain{device, static_cast<HWND>(win.native_handle())};

    bool running = true;
    while (running) {
      while (auto ev = sdl_event::poll())
        if (ev.type() == sdl_event_type::quit) running = false;

      // Cycle the clear color on wall-clock time, so the frame rate does not
      // set the animation speed and the loop reads as visibly alive.
      const float secs = static_cast<float>(SDL_GetTicks()) * 0.001F;
      const float color[4]{0.5F + (0.5F * std::sin(secs)),
          0.5F + (0.5F * std::sin(secs + 2.0944F)),
          0.5F + (0.5F * std::sin(secs + 4.1888F)), 1.0F};
      device.context()->ClearRenderTargetView(swapchain.render_target(),
          color);
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
