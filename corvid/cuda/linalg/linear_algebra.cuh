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
#include <type_traits>

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
#pragma region gemm

namespace details {

// Write `bias` across every row of `out`, one thread per element.
template<typename T>
__global__ void fill_rows(T* out, size_t size, const T* bias, size_t cols) {
  const auto i = cuda_kernel::x_index<size_t>();
  if (i < size) out[i] = bias[i % cols];
}

} // namespace details

// General Matrix Multiply (GEMM) over row-major matrices.
//
// `out = scale * op(a) * op(b) + bias_scale * bias`, where `op(x)` is `x` or
// its transpose, as `op_a` and `op_b` select. The shapes must agree: `op(a)`
// has a row per row of `out`, `op(b)` has a column per column of `out`, and
// `op(a)` has as many columns as `op(b)` has rows.
//
// `bias` is the term added to the product. When given, it is a row with an
// element per column of `out`, added to every row. When null, it is the prior
// contents of `out`, so the product accumulates onto them. A `bias_scale` of
// zero leaves that term unread, which is how a plain product with no `bias`
// is asked for.
//
// `out` must not be `a` or `b`. Returns false when a launch is refused,
// leaving `out` unspecified.
//
// TODO: Handle non-packed views over matrices.
template<GemmElement T>
[[nodiscard]] bool gemm(const cublas_handle& blas, cuda_matrix<T>& out,
    const cuda_matrix<T>& a, const cuda_matrix<T>& b,
    const cuda_buffer<std::type_identity_t<T>>* bias = nullptr,
    std::type_identity_t<T> scale = 1, std::type_identity_t<T> bias_scale = 1,
    cublas_operation op_a = cublas_operation::none,
    cublas_operation op_b = cublas_operation::none) {
  // The extent of `op(x)`, which is that of `x` with the sides swapped when
  // `x` is transposed.
  const auto op_extent = [](const cuda_matrix<T>& x, cublas_operation op) {
    return (op == cublas_operation::none)
               ? x.extent()
               : matrix_extent{.row_count = x.col_extent(),
                     .col_count = x.row_extent()};
  };
  const auto op_a_extent = op_extent(a, op_a);
  [[maybe_unused]] const auto op_b_extent = op_extent(b, op_b);
  assert((out.row_extent() == op_a_extent.row_count) &&
         (out.col_extent() == op_b_extent.col_count));
  assert(op_a_extent.col_count == op_b_extent.row_count);
  assert((&out != &a) && (&out != &b));

  // The bias goes into `out` before the GEMM, so that cuBLAS sees it as the
  // `C` operand and `bias_scale` reaches it as `beta`. GEMM has no broadcast:
  // its addend is a full matrix, so the one row is written across every row
  // of `out`. That costs no extra memory and one write of `out`, where a
  // product followed by a separate bias add would read and write `out` again.
  if (bias) {
    assert(bias->size() == out.col_extent());
    details::fill_rows<T><<<blocks_for(out.size()), threads_per_block>>>(
        out.get(), out.size(), bias->get(), out.col_extent());
    if (!cuda_last_status{}) return false;
  }

  // Every matrix is packed, so each leading dimension is its stored row
  // length. The handle's stream is the default one, the same the fill ran on,
  // so the order holds.
  const auto m = static_cast<int>(out.row_extent());
  const auto n = static_cast<int>(out.col_extent());
  const auto k = static_cast<int>(op_a_extent.col_count);
  return blas
      .multiply_row_major(m, n, k, scale, a.buffer(),
          static_cast<int>(a.col_extent()), b.buffer(),
          static_cast<int>(b.col_extent()), bias_scale, out.buffer(), n, op_a,
          op_b)
      .ok();
}

#pragma endregion
#pragma region linear_projection

// Project each row of `in` through `weight` and add `bias`, into `out`.
//
// The contract is that of the CPU `corvid::linalg::linear_projection`, which
// also holds the worked explanation: `out` has a row per row of `in` and a
// column per column of `weight`, `weight` has a row per column of `in`, and
// `bias` has an element per column of `weight`. `out` must not be `in` or
// `weight`.
//
// Returns false when a launch is refused, leaving `out` unspecified.
template<GemmElement T>
[[nodiscard]] bool linear_projection(const cublas_handle& blas,
    cuda_matrix<T>& out, const cuda_matrix<T>& in,
    const cuda_matrix<T>& weight, const cuda_buffer<T>& bias) {
  return gemm(blas, out, in, weight, &bias);
}

#pragma endregion

} // namespace corvid::cuda::linalg
