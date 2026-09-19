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

#include <cassert>
#include <cstddef>

#include <cuda_runtime.h>

#include "../containers/utils/matrix_view.h"
#include "./cuda_buffer.cuh"
#include "./cuda_status.cuh"

// A matrix in device memory.
//
// `cuda_matrix` is a `cuda_buffer` that knows its row and column counts, so
// device ops can check shapes on the host before launching, and a packed
// `matrix_view` is what it loads from and stores to:
//
//   cuda_matrix in(host_view);
//   cuda_matrix out(in.extent());
//   ... launch over out.get() ...
//   out.store(host_out).or_throw();
namespace corvid::cuda {

using matrix_types::matrix_extent;

#pragma region cuda_matrix

// A row-major matrix of `float` in device memory, packed with no gap between
// rows.
//
// It owns its allocation and carries its extent, so an op can check shapes on
// the host before launching. Rows are the first index, as in `matrix_view`,
// and a packed host view is the shape data moves in and out through.
//
// TODO: Must be templated so it's not just `float`.
class cuda_matrix {
public:
  using extent_t = matrix_extent;

#pragma region Construction

  // Allocate `extent` elements, uninitialized, or throw.
  explicit cuda_matrix(extent_t extent)
      : buffer_(extent.row_count * extent.col_count), extent_{extent} {}

  // Allocate and upload `host`, which must be packed, or throw.
  explicit cuda_matrix(const_float_matrix_view host)
      : cuda_matrix{host.extent()} {
    assert(host.stride() == host.col_extent());
    load(host).or_throw();
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] float* get() noexcept { return buffer_.get(); }
  [[nodiscard]] const float* get() const noexcept { return buffer_.get(); }

  [[nodiscard]] cuda_buffer<float>& buffer() noexcept { return buffer_; }
  [[nodiscard]] const cuda_buffer<float>& buffer() const noexcept {
    return buffer_;
  }

  [[nodiscard]] extent_t extent() const noexcept { return extent_; }
  [[nodiscard]] size_t row_extent() const noexcept {
    return extent_.row_count;
  }
  [[nodiscard]] size_t col_extent() const noexcept {
    return extent_.col_count;
  }
  [[nodiscard]] size_t size() const noexcept {
    return extent_.row_count * extent_.col_count;
  }

#pragma endregion
#pragma region Transfer

  // Upload `host`, which must be packed and of the same extent.
  [[nodiscard]] cuda_last_status load(const_float_matrix_view host) {
    assert(is_packed_match(host));
    return buffer_.load(host.as_span());
  }

  // Download into `host`, which must be packed and of the same extent.
  [[nodiscard]] cuda_last_status store(float_matrix_view host) const {
    assert(is_packed_match(host));
    return buffer_.store(host.as_span());
  }

#pragma endregion
#pragma region Helpers
private:
  [[nodiscard]] bool is_packed_match(const_float_matrix_view host) const {
    return (host.row_extent() == extent_.row_count) &&
           (host.col_extent() == extent_.col_count) &&
           (host.stride() == extent_.col_count);
  }

#pragma endregion
#pragma region Data members
private:
  cuda_buffer<float> buffer_;
  extent_t extent_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
