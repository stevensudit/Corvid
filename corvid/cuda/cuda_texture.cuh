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

#include <utility>

#include <cuda_runtime.h>

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
class cuda_texture {
public:
#pragma region Construction

  cuda_texture() = default;

  explicit cuda_texture(cudaArray_t array) {
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
    cuda_last_status{
        cudaCreateTextureObject(&texture_, &resource_desc, &texture_desc,
            nullptr)}
        .or_throw();
  }

  cuda_texture(const cuda_texture&) = delete;
  cuda_texture& operator=(const cuda_texture&) = delete;

  cuda_texture(cuda_texture&& other) noexcept
      : texture_{std::exchange(other.texture_, 0)} {}
  cuda_texture& operator=(cuda_texture&& other) noexcept {
    if (this != &other) {
      destroy();
      texture_ = std::exchange(other.texture_, 0);
    }
    return *this;
  }
  ~cuda_texture() { destroy(); }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] cudaTextureObject_t get() const noexcept { return texture_; }
  [[nodiscard]] operator cudaTextureObject_t() const noexcept {
    return texture_;
  }

#pragma endregion
#pragma region Helpers
private:
  void destroy() const {
    if (texture_) cudaDestroyTextureObject(texture_);
  }

#pragma endregion
#pragma region Data members
private:
  cudaTextureObject_t texture_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
