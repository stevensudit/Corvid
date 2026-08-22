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

#include "./cuda_handle.cuh"
#include "./cuda_status.cuh"

// CUDA texture objects.

namespace corvid::cuda {

#pragma region cuda_texture

// RAII handle to a `cudaTextureObject_t` over a `cudaArray`, the handle a
// kernel reads through with `tex3D` (or the lower-dimensional fetches).
//
// The texture filters linearly, clamps at the edges, and uses unnormalized
// coordinates, so a fetch at `(x + 0.5, y + 0.5, z + 0.5)` reads element `(x,
// y, z)`. Linear filtering returns floats, so an integer-element array must be
// read as `cudaReadModeNormalizedFloat` to filter; a floating-point array uses
// the default `cudaReadModeElementType`.
//
// The array is borrowed, not owned, and must outlive the texture.
class cuda_texture
    : public cuda_handle<cudaTextureObject_t, cudaDestroyTextureObject> {
public:
#pragma region Construction

  cuda_texture() = default;
  explicit cuda_texture(std::nullptr_t) noexcept : cuda_handle{nullptr} {}

  // Create a texture object over `array`, or throw.
  explicit cuda_texture(cudaArray_t array,
      cudaTextureReadMode read_mode = cudaReadModeElementType)
      : cuda_texture{make(array, read_mode, on_failure::raise)} {}

  // Create a texture object over `array`, or return a failed instance.
  // Check with `operator bool`, and follow up with `cuda_last_status{}`.
  [[nodiscard]] static cuda_texture try_create(cudaArray_t array,
      cudaTextureReadMode read_mode = cudaReadModeElementType) {
    return cuda_texture{make(array, read_mode, on_failure::ignore)};
  }

#pragma endregion
#pragma region Helpers
private:
  explicit cuda_texture(cudaTextureObject_t texture) noexcept
      : cuda_handle{texture} {}

  static cudaTextureObject_t
  make(cudaArray_t array, cudaTextureReadMode read_mode, on_failure policy) {
    const cudaResourceDesc resource_desc{.resType = cudaResourceTypeArray,
        .res = {.array = {.array = array}}};
    const cudaTextureDesc texture_desc{
        .addressMode = {cudaAddressModeClamp, cudaAddressModeClamp,
            cudaAddressModeClamp},
        .filterMode = cudaFilterModeLinear,
        .readMode = read_mode,
        .normalizedCoords = 0,
    };
    return create<cudaCreateTextureObject>(policy, &resource_desc,
        &texture_desc, nullptr);
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
