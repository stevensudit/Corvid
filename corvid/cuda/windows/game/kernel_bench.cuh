// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <print>

#include <cuda_runtime.h>

#include "../../camera.cuh"
#include "../../cuda_event.cuh"
#include "../../cuda_kernel.cuh"
#include "../../cuda_status.cuh"
#include "../../cuda_surface.cuh"
#include "../../cuda_volume.cuh"
#include "../../density_field.cuh"
#include "../../material_volume.cuh"
#include "../../mirror.cuh"
#include "../../radians.cuh"
#include "../../vec.cuh"
#include "../../../infra/scope_exit.h"
#include "./avatar.cuh"
#include "./avatar_rig.cuh"
#include "./render_config.cuh"
#include "./render_kernel.cuh"
#include "./world_gen.cuh"

// The voxel viewer's offline kernel benchmark: render `voxel_kernel` at a
// fixed pose with no window or vsync, for the pure GPU-time signal the live
// viewer cannot resolve. Selected by the viewer's `bench` argument.

namespace corvid::cuda {

#pragma region Benchmark

// Offline kernel benchmark: render `voxel_kernel` repeatedly into an
// off-screen surface at a fixed pose and resolution, with no window, present,
// or vsync, and report the per-launch GPU time (min/avg/max). The pure,
// isolated signal for the small kernel changes the live, vsync-capped viewer
// cannot resolve; the viewer's "uncap fps" toggle is the complementary in-situ
// measurement. Selected by the `bench` argument.
[[nodiscard]] inline int run_kernel_bench() {
  constexpr int width = 2560; // a fixed 1440p frame, so runs compare directly
  constexpr int height = 1440;
  constexpr int warmup = 32; // settle clocks and caches before timing
  constexpr int iters = 200;

  // The same world the viewer builds (see `engine`): the three grids filled
  // once from the eroded terrain, and the -z mirror.
  constexpr cudaExtent vol_extent{512, 128, 512};
  constexpr float voxel_size = 0.5F;
  cuda_volume<float> volume{vol_extent};
  material_volume materials{vol_extent};
  color_volume colors{vol_extent};
  const float ox =
      -0.5F * static_cast<float>(vol_extent.width - 1) * voxel_size;
  const float oy =
      -0.5F * static_cast<float>(vol_extent.height - 1) * voxel_size;
  const float oz =
      -0.5F * static_cast<float>(vol_extent.depth - 1) * voxel_size;
  density_field field{vol_extent, pos3{vec3{ox, oy, oz}}, voxel_size,
      volume.texture()};
  const float world_x1 =
      ox + (static_cast<float>(vol_extent.width - 1) * voxel_size);
  const flat_mirror mirror{.plane_z = oz,
      .lo = vec2{ox, oy},
      .hi = vec2{world_x1, oy + 80.0F},
      .normal = vec3{0.0F, 0.0F, 1.0F}};
  generate_world(field, volume, materials, colors);

  // A fixed pose, the viewer's default spawn (terrain toward the distant -z
  // mirror). The march tunables come from the shipped defaults.
  render_config cfg;
  field.march_lipschitz = cfg.march.lipschitz;
  field.march_max_step_voxels = cfg.march.max_step_voxels;
  field.march_max_steps = cfg.march.max_steps;
  avatar_rig rig{pos3{vec3{0.0F, 10.0F, 0.0F}},
      orientation{90.0_deg, -20.0_deg}, 90.0_deg, 7.0F, 7.0F};
  rig.update(0.016F, false); // seat the head offset off its first frame
  const camera_rays rays = rig.rays();
  const metal_ball ball = rig.ball();
  const saucer_head head = rig.head(cfg.head);

  // The off-screen target the kernel writes through: an owned surface array,
  // so the bench needs no D3D interop. Freed after the surface that borrows
  // it.
  cudaArray_t array{};
  const cudaChannelFormatDesc fmt = cudaCreateChannelDesc<uchar4>();
  cuda_last_status{
      cudaMallocArray(&array, &fmt, width, height, cudaArraySurfaceLoadStore)}
      .or_throw();
  const scope_exit free_array{[&] { (void)cudaFreeArray(array); }};
  cuda_surface surf{array};

  const dim3 block{16, 16};
  const dim3 grid{cuda_kernel::ceil_div(width, block.x),
      cuda_kernel::ceil_div(height, block.y)};
  const resolution res{static_cast<float>(width), static_cast<float>(height)};
  const auto launch = [&] {
    voxel_kernel<<<grid, block>>>(surf, res, rays, field, colors.texture(),
        ball, head, mirror, cfg);
  };

  for (int i = 0; i < warmup; ++i) launch();
  cuda_last_status{cudaDeviceSynchronize()}.or_throw();
  cuda_last_status{cudaGetLastError()}.or_throw();

  cuda_event start;
  cuda_event stop;
  float total = 0.0F;
  float lo = 1.0e30F;
  float hi = 0.0F;
  for (int i = 0; i < iters; ++i) {
    start.record().or_throw();
    launch();
    stop.record().or_throw();
    stop.synchronize().or_throw();
    float ms = 0.0F;
    cuda_event::elapsed_ms(start, stop, ms).or_throw();
    total += ms;
    lo = fminf(lo, ms);
    hi = fmaxf(hi, ms);
  }

  std::println("voxel_kernel  {}x{}  aa={}  {} iters", width, height,
      cfg.aa_samples, iters);
  std::println("  GPU ms/frame  min {:.3f}  avg {:.3f}  max {:.3f}", lo,
      total / static_cast<float>(iters), hi);
  return 0;
}

#pragma endregion

} // namespace corvid::cuda
