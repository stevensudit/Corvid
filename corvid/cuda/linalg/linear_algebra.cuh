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
#include "../cuda_matrix.cuh"
#include "../cuda_status.cuh"

// Row and matrix arithmetic on the device, one free function per op.
//
// Every op writes into a caller-owned `cuda_matrix`, launches on the default
// stream, and returns whether its launches were accepted, so a fault inside a
// kernel surfaces at the next synchronizing call, such as a `store`. Shape
// mismatches are contract violations.
namespace corvid::cuda::linalg {

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
template<typename T>
__global__ void fill_rows(T* out, size_t size, const T* bias, size_t cols) {
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
// Returns false when a launch is refused, leaving `out` unspecified.
//
// TODO: Handle non-packed views over matrices.
template<GemmElement T>
[[nodiscard]] bool linear_projection(const cublas_handle& blas,
    cuda_matrix<T>& out, const cuda_matrix<T>& in,
    const cuda_matrix<T>& weight, const cuda_buffer<T>& bias) {
  assert(weight.row_extent() == in.col_extent());
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == weight.col_extent()));
  assert(bias.size() == weight.col_extent());
  assert((&out != &in) && (&out != &weight));

  // The bias is the starting value of every output row, and the product then
  // accumulates onto it through `beta`, which is the shape of the CPU op's
  // copy followed by `add_scaled`.
  details::fill_rows<T><<<blocks_for(out.size()), threads_per_block>>>(
      out.get(), out.size(), bias.get(), out.col_extent());
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
