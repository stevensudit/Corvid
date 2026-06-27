// CUDA-driven voxel ray marcher (Windows-only CUDA cell, crossplatform.md
// section 11), the second 3D rung after the SDF raymarch viewer. A density
// field lives in VRAM as a 3D texture; a kernel fixed-step marches it per
// pixel to the first solid voxel, shades it, and writes the result into the
// interop texture through `cudaGraphicsD3D11*`; D3D copies it to the
// backbuffer and presents. The frame stays on the GPU. Unlike the SDF viewer's
// analytic scene, the geometry is sampled from the field, so it can be edited
// in place (the next rung). The player is an avatar of a metallic ball and a
// saucer head: WASD drives the ball (Shift sprints, Space jumps), gravity
// holds it on the terrain, holding the right mouse button aims the eye (Look,
// or Steer while driving), and the mouse wheel dollies the head between its
// trailing distance and the jockey (close above and behind the ball). Escape
// quits.
//
// This file is just the entry point: `main` parses the command line and runs
// either the engine (game/engine.cuh) or the offline benchmark
// (game/kernel_bench.cuh). The kernels and the avatar rig live in their own
// game/ headers (render_kernel, field_ops, world_gen, avatar_rig); see those
// for the scene work.

#include <exception>
#include <string_view>

#include "corvid/cuda/windows/game/engine.cuh"
#include "corvid/cuda/windows/game/kernel_bench.cuh"

using namespace corvid::cuda;

#pragma region Main

int main(int argc, char** argv) {
  try {
    // `bench`: run the offline kernel benchmark (no window) and exit. The pure
    // signal for small kernel changes; the live "uncap fps" toggle is the
    // in-situ measure for a chosen view.
    if (argc > 1 && std::string_view{argv[1]} == "bench")
      return run_kernel_bench();

    engine viewer;
    while (viewer.tick()) {}
    return 0;
  }
  catch (const std::exception& e) {
    // `main` is the boundary: report and exit rather than letting it escape.
    SDL_Log("fatal: %s", e.what());
    return 1;
  }
}

#pragma endregion
