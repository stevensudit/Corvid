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

#include <cstddef>
#include <cuda_runtime.h>

#include "./cuda_handle.cuh"
#include "./cuda_status.cuh"

// An owned 3D CUDA array.

namespace corvid::cuda {

#pragma region cuda_array_3d

// RAII handle to an owned 3D `cudaArray` of `T` elements, sized by a
// `cudaExtent` and allocated with `cudaArraySurfaceLoadStore` so the same
// storage can back both a texture (for reads) and a surface (for writes).
template<typename T = float>
class cuda_array_3d: public cuda_handle<cudaArray_t, cudaFreeArray> {
public:
#pragma region Construction

  cuda_array_3d() = default;
  explicit cuda_array_3d(std::nullptr_t) noexcept : cuda_handle{nullptr} {}

  // Allocate an array of `extent`, or throw.
  explicit cuda_array_3d(cudaExtent extent)
      : cuda_array_3d{allocate(extent, on_failure::raise), extent} {}

  // Allocate an array of `extent`, or return a failed instance.
  //
  // Check with `operator bool`, and follow up with `cuda_last_status{}`.
  [[nodiscard]] static cuda_array_3d try_create(cudaExtent extent) {
    return cuda_array_3d{allocate(extent, on_failure::ignore), extent};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] const cudaExtent& extent() const noexcept { return extent_; }

#pragma endregion
#pragma region Helpers
private:
  cuda_array_3d(cudaArray_t array, cudaExtent extent) noexcept
      : cuda_handle{array}, extent_{extent} {}

  static cudaArray_t allocate(cudaExtent extent, on_failure policy) {
    const cudaChannelFormatDesc channel = cudaCreateChannelDesc<T>();
    return create<cudaMalloc3DArray>(policy, &channel, extent,
        cudaArraySurfaceLoadStore);
  }

#pragma endregion
#pragma region Data members
private:
  cudaExtent extent_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
