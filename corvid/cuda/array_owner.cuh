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

// Ownership of a CUDA array handle.

namespace corvid::cuda {

#pragma region array_owner

// RAII ownership of a `cudaArray_t`: frees it with `cudaFreeArray` when
// destroyed. It does not allocate; adopt a handle from a `cudaMalloc*Array`
// call (1D, 2D, or 3D), so it serves arrays of any dimensionality. Move-only,
// so a type that holds one is non-copyable by rule of zero.
class array_owner {
public:
#pragma region Construction

  array_owner() noexcept = default;
  explicit array_owner(cudaArray_t array) noexcept : array_{array} {}

  array_owner(const array_owner&) = delete;
  array_owner& operator=(const array_owner&) = delete;

  array_owner(array_owner&& other) noexcept
      : array_{std::exchange(other.array_, nullptr)} {}
  array_owner& operator=(array_owner&& other) noexcept {
    if (this != &other) {
      if (array_) cudaFreeArray(array_);
      array_ = std::exchange(other.array_, nullptr);
    }
    return *this;
  }
  ~array_owner() {
    if (array_) cudaFreeArray(array_);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] cudaArray_t get() const noexcept { return array_; }
  [[nodiscard]] operator cudaArray_t() const noexcept { return array_; }

#pragma endregion
#pragma region Data members
private:
  cudaArray_t array_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
