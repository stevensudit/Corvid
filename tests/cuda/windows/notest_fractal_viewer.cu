// First target of the Windows-only CUDA cell (see crossplatform.md section
// 11): a CUDA-driven fractal viewer. This milestone proves the SDL3 dependency
// and the corvid::sdl wrappers: it opens a window through the RAII
// subsystem/window types and pumps events until closed, with no CUDA kernel
// and no Direct3D yet. The event loop is still raw SDL pending the typed event
// wrapper; the D3D11 device, flip-model swapchain, and CUDA-D3D interop arrive
// in later milestones.

#include <exception>

#include "corvid/sdl/sdl_subsystem.h"
#include "corvid/sdl/sdl_window.h"

using namespace corvid::sdl;

int main() {
  try {
    sdl_subsystem sdl;
    sdl_window win{"Corvid Fractal Viewer", 1280, 720};

    bool running = true;
    while (running) {
      // Raw poll until the typed event wrapper lands; only QUIT handled here.
      SDL_Event event;
      while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT) running = false;
    }
    return 0;
  }
  catch (const std::exception& e) {
    // main is the boundary: report and exit rather than letting it escape.
    SDL_Log("fatal: %s", e.what());
    return 1;
  }
}
