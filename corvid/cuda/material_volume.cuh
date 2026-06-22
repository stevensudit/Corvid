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

#include <cstdint>

#include <cuda_runtime.h>

#include "./cuda_array_3d.cuh"
#include "./cuda_surface.cuh"

// A 3D material grid resident in VRAM: one `uint16` per voxel, point-accessed.

namespace corvid::cuda {

#pragma region material_volume

// A 3D grid of `uint16` material values in device memory, exposed for exact
// reads and writes through a surface.
//
// The sibling of `cuda_volume`. Where that is the smooth, trilinearly filtered
// geometry field, this is the sharp material channel, read once per ray at the
// hit and per voxel at a dig, always exact. So it carries no texture and no
// filtering, since an interpolated ID is meaningless. See "voxel_world.md".
//
// Composes the array (storage) and the surface (a view that borrows it), so
// the array is declared first and thus destroyed last, after the surface. The
// array does not need to be exposed, so it is not.
class material_volume {
public:
#pragma region Construction

  material_volume() = default;

  explicit material_volume(cudaExtent extent)
      : array_{extent}, surface_{array_} {}

#pragma endregion
#pragma region Accessors

  // The surface object for exact reads and writes, passed to a kernel.
  [[nodiscard]] cudaSurfaceObject_t surface() const noexcept {
    return surface_;
  }
  [[nodiscard]] cudaExtent extent() const noexcept { return array_.extent(); }

#pragma endregion
#pragma region Data members
private:
  cuda_array_3d<uint16_t> array_;
  cuda_surface surface_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
