// First target of the Windows-only CUDA cell (see crossplatform.md section 11):
// a CUDA-driven fractal viewer. This initial milestone proves the SDL3
// dependency alone -- it opens a window and pumps events until closed, with no
// CUDA kernel and no Direct3D yet. The D3D11 device, the flip-model swapchain,
// and the CUDA-D3D interop that fills the frame arrive in later milestones; SDL3
// stays the window, input, and event layer throughout.

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // SDL_SetMainReady (not pulled in by SDL.h)

int main() {
  SDL_SetMainReady();
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow("Corvid Fractal Viewer", 1280, 720, 0);
  if (!window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  bool running = true;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) running = false;
    }
  }

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
