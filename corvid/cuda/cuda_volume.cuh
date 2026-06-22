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

#include <cuda_runtime.h>

#include "./cuda_array_3d.cuh"
#include "./cuda_surface.cuh"
#include "./cuda_texture.cuh"

// A 3D scalar field resident in VRAM.

namespace corvid::cuda {

#pragma region cuda_volume

// A 3D field of `T` in device memory, exposed for filtered reads (the marcher
// samples through the texture) and for writes (a fill or edit overwrites
// through the surface).
//
// T` is a texture-filterable type: `float` (read as its element type) for the
// density field, or an integer type like `uchar4` read as
// `cudaReadModeNormalizedFloat` (`Read`) for a smoothly interpolated color
// field.
//
// Composes the three RAII handles: the array is the storage, the texture and
// surface are views that borrow it, so the array is declared first and thus
// destroyed last, after the views. The array does not need to be exposed, so
// it's not.
template<typename T = float,
    cudaTextureReadMode Read = cudaReadModeElementType>
class cuda_volume {
public:
#pragma region Construction

  cuda_volume() = default;

  explicit cuda_volume(cudaExtent extent)
      : array_{extent}, texture_{array_, Read}, surface_{array_} {}

#pragma endregion
#pragma region Accessors

  // The texture object for filtered reads, passed to a marching kernel.
  [[nodiscard]] cudaTextureObject_t texture() const noexcept {
    return texture_;
  }
  // The surface object for writes, passed to a fill or edit kernel.
  [[nodiscard]] cudaSurfaceObject_t surface() const noexcept {
    return surface_;
  }
  [[nodiscard]] cudaExtent extent() const noexcept { return array_.extent(); }

#pragma endregion
#pragma region Data members
private:
  cuda_array_3d<T> array_;
  cuda_texture texture_;
  cuda_surface surface_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
