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

#include "../../containers/utils/matrix_view.h"
#include "../cuda_buffer.cuh"
#include "../cuda_cublas.cuh"
#include "../cuda_kernel.cuh"
#include "../cuda_status.cuh"

// Row and matrix arithmetic on the device, one free function per op.
//
// Every op writes into a caller-owned `cuda_matrix`, launches on the default
// stream, and returns whether its launches were accepted, so a fault inside a
// kernel surfaces at the next synchronizing call, such as a `store`. Shape
// mismatches are contract violations.
namespace corvid::cuda::linalg {

using matrix_types::matrix_extent;

#pragma region cuda_matrix

// A row-major matrix of `float` in device memory, packed with no gap between
// rows.
//
// It owns its allocation and carries its extent, so an op can check shapes on
// the host before launching. Rows are the first index, as in `matrix_view`,
// and a packed host view is the shape data moves in and out through.
//
// Note: Before this is moved out of LLM-specific and into general CUDA, it has
// to be templated so it's not just `float`.
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
#pragma region Launch geometry

// One-thread-per-element kernels launch this many threads per block, in
// enough blocks to cover every element.
inline constexpr auto threads_per_block = 256U;

// The block count that covers `size` elements.
[[nodiscard]] inline unsigned blocks_for(size_t size) noexcept {
  return cuda_kernel::blocks_for_threads(static_cast<unsigned>(size),
      threads_per_block);
}

#pragma endregion
#pragma region linear_projection

namespace details {

// Write `bias` across every row of `out`, one thread per element.
__global__ void
fill_rows(float* out, size_t size, const float* bias, size_t cols) {
  const auto i = cuda_kernel::x_index<size_t>();
  if (i < size) out[i] = bias[i % cols];
}

} // namespace details

// Project each row of `in` through `weight` and add `bias`, into `out`.
//
// The contract is that of the CPU `corvid::linalg::linear_projection`, which
// also holds the worked explanation: `out` has a row per row of `in` and a
// column per column of `weight`, `weight` has a row per column of `in`, and
// `bias` has an element per column of `weight`. `out` must not be `in` or
// `weight`.
//
// Note: Before this is moved out of LLM-specific and into general CUDA, it not
// only has to be templated so it's not just `float`, but it also has to handle
// non-packed inputs.
//
// Returns false when a launch is refused, leaving `out` unspecified.
[[nodiscard]] inline bool linear_projection(const cublas_handle& blas,
    cuda_matrix& out, const cuda_matrix& in, const cuda_matrix& weight,
    const cuda_buffer<float>& bias) {
  assert(weight.row_extent() == in.col_extent());
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == weight.col_extent()));
  assert(bias.size() == weight.col_extent());
  assert((&out != &in) && (&out != &weight));

  // The bias is the starting value of every output row, and the product then
  // accumulates onto it through `beta`, which is the shape of the CPU op's
  // copy followed by `add_scaled`.
  details::fill_rows<<<blocks_for(out.size()), threads_per_block>>>(out.get(),
      out.size(), bias.get(), out.col_extent());
  if (!cuda_last_status{}) return false;

  // `out = in * weight` in row-major terms: `m` rows, `n` output features,
  // `k` input features, each leading dimension a packed row length. The
  // handle's stream is the default one, the same the fill ran on, so the
  // order holds.
  const auto m = static_cast<int>(out.row_extent());
  const auto n = static_cast<int>(out.col_extent());
  const auto k = static_cast<int>(in.col_extent());
  return blas
      .multiply_row_major(m, n, k, 1.0F, in.buffer(), k, weight.buffer(), n,
          1.0F, out.buffer(), n)
      .ok();
}

#pragma endregion

} // namespace corvid::cuda::linalg
