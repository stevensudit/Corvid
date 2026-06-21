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
// y, z)`. Linear filtering requires a floating-point array.
//
// The array is borrowed, not owned, and must outlive the texture.
class cuda_texture
    : public cuda_handle<cudaTextureObject_t, cudaDestroyTextureObject> {
public:
#pragma region Construction

  cuda_texture() = default;

  explicit cuda_texture(cudaArray_t array) : cuda_handle{create(array)} {}

#pragma endregion
#pragma region Helpers
private:
  // Create a texture object over `array`, returning the handle for the base to
  // adopt.
  static cudaTextureObject_t create(cudaArray_t array) {
    const cudaResourceDesc resource_desc{
        .resType = cudaResourceTypeArray,
        .res = {.array = {.array = array}},
    };
    const cudaTextureDesc texture_desc{
        .addressMode = {cudaAddressModeClamp, cudaAddressModeClamp,
            cudaAddressModeClamp},
        .filterMode = cudaFilterModeLinear,
        .readMode = cudaReadModeElementType,
        .normalizedCoords = 0,
    };
    cudaTextureObject_t texture{};
    cuda_last_status{
        cudaCreateTextureObject(&texture, &resource_desc, &texture_desc,
            nullptr)}
        .or_throw();
    return texture;
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
