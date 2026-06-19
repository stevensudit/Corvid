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

// CUDA surface objects.

namespace corvid::cuda {

#pragma region cuda_surface

// RAII handle to a `cudaSurfaceObject_t` over a `cudaArray`, the handle a
// kernel writes through with `surf2Dwrite`.
//
// The array is borrowed, not owned, and must outlive the surface.
class cuda_surface {
public:
#pragma region Construction

  explicit cuda_surface(cudaArray_t array) {
    cudaResourceDesc desc{};
    desc.resType = cudaResourceTypeArray;
    desc.res.array.array = array;
    cuda_last_status{cudaCreateSurfaceObject(&surface_, &desc)}.or_throw();
  }

  cuda_surface(const cuda_surface&) = delete;
  cuda_surface& operator=(const cuda_surface&) = delete;

  cuda_surface(cuda_surface&& other) noexcept
      : surface_{std::exchange(other.surface_, 0)} {}
  cuda_surface& operator=(cuda_surface&& other) noexcept {
    if (this != &other) {
      destroy();
      surface_ = std::exchange(other.surface_, 0);
    }
    return *this;
  }
  ~cuda_surface() { destroy(); }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] cudaSurfaceObject_t get() const noexcept { return surface_; }

#pragma endregion
#pragma region Helpers
private:
  void destroy() const {
    if (surface_) cudaDestroySurfaceObject(surface_);
  }

#pragma endregion
#pragma region Data members
private:
  cudaSurfaceObject_t surface_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
