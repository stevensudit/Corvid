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

// CUDA surface objects.

namespace corvid::cuda {

#pragma region cuda_surface

// RAII handle to a `cudaSurfaceObject_t` over a `cudaArray`, the handle a
// kernel writes through with `surf2Dwrite`.
//
// The array is borrowed, not owned, and must outlive the surface.
class cuda_surface
    : public cuda_handle<cudaSurfaceObject_t, cudaDestroySurfaceObject> {
public:
#pragma region Construction

  cuda_surface() = default;
  explicit cuda_surface(std::nullptr_t) noexcept : cuda_handle{nullptr} {}

  // Create a surface object over `array`, or throw.
  explicit cuda_surface(cudaArray_t array)
      : cuda_surface{make(array, on_failure::raise)} {}

  // Create a surface object over `array`, or return a failed instance.
  // Check with `operator bool`, and follow up with `cuda_last_status{}`.
  [[nodiscard]] static cuda_surface try_create(cudaArray_t array) {
    return cuda_surface{make(array, on_failure::ignore)};
  }

#pragma endregion
#pragma region Helpers
private:
  explicit cuda_surface(cudaSurfaceObject_t surface) noexcept
      : cuda_handle{surface} {}

  static cudaSurfaceObject_t make(cudaArray_t array, on_failure policy) {
    const cudaResourceDesc desc{.resType = cudaResourceTypeArray,
        .res = {.array = {.array = array}},
        .flags = 0};
    return create<cudaCreateSurfaceObject>(policy, &desc);
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
