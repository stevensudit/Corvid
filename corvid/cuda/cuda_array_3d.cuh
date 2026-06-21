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

// An owned 3D CUDA array.

namespace corvid::cuda {

#pragma region cuda_array_3d

// RAII handle to an owned 3D `cudaArray` of float elements, sized by a
// `cudaExtent` and allocated with `cudaArraySurfaceLoadStore` so the same
// storage can back both a texture (for reads) and a surface (for writes).
class cuda_array_3d: public cuda_handle<cudaArray_t, cudaFreeArray> {
public:
#pragma region Construction

  cuda_array_3d() = default;

  explicit cuda_array_3d(cudaExtent extent)
      : cuda_handle{allocate(extent)}, extent_{extent} {}

#pragma endregion
#pragma region Accessors

  [[nodiscard]] const cudaExtent& extent() const noexcept { return extent_; }

#pragma endregion
#pragma region Helpers
private:
  // Allocate a 3D float array of `extent`, returning the handle for the base
  // to adopt.
  static cudaArray_t allocate(cudaExtent extent) {
    const cudaChannelFormatDesc channel = cudaCreateChannelDesc<float>();
    cudaArray_t array{};
    cuda_last_status{
        cudaMalloc3DArray(&array, &channel, extent, cudaArraySurfaceLoadStore)}
        .or_throw();
    return array;
  }

#pragma endregion
#pragma region Data members
private:
  cudaExtent extent_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
