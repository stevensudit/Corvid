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
#include <functional>
#include <type_traits>

#include <cuda_runtime.h>

#include "../../containers/utils/matrix_view.h"
#include "../../meta/concepts.h"
#include "../../meta/containers.h"
#include "../cuda_buffer.cuh"
#include "../cuda_cublas.cuh"
#include "../cuda_kernel.cuh"
#include "../cuda_matrix.cuh"
#include "../cuda_status.cuh"

// Row and matrix arithmetic on the device, one free function per op.
//
// Every op writes into a caller-owned output (a `cuda_matrix` or a
// `cuda_matrix_view`), launches on the default stream, and returns whether its
// launches were accepted, so a fault inside a kernel surfaces at the next
// synchronizing call, such as a `store`. Shape mismatches are contract
// violations.
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
#pragma region Views

// A read-only device view of `T`, the element type of an op's output, which
// an owning `cuda_matrix` or a mutable view converts to at the call.
template<typename T>
using const_view_t = cuda_matrix_view<const T>;

// The read-only view an op with the output `Out` takes as an input.
template<DeviceMatrixLike Out>
using input_view_t = const_view_t<device_element_t<Out>>;

// The buffer an op with the output `Out` takes as a per-column input, such as
// a bias.
template<DeviceMatrixLike Out>
using input_buffer_t = cuda_buffer<device_element_t<Out>>;

#pragma endregion
#pragma region gemm

namespace details {

// Write `bias` across every row of the `size` elements of a view `cols` wide
// whose rows start `stride` apart, one thread per element.
template<typename T>
__global__ void
fill_rows(T* out, size_t size, size_t cols, size_t stride, const T* bias) {
  const auto i = cuda_kernel::x_index<size_t>();
  if (i < size)
    out[cuda_kernel::strided_offset(i, cols, stride)] = bias[i % cols];
}

} // namespace details

// The scalars and transpose flags of a `gemm`, named at the call site.
//
// `scale` multiplies the product and `addend_scale` the addend, whether that
// is a matrix or a bias row. `op_a` and `op_b` take each operand as stored or
// transposed.
template<GemmElement T>
struct gemm_options {
  T scale = 1;
  T addend_scale = 1;
  cublas_operation op_a = cublas_operation::none;
  cublas_operation op_b = cublas_operation::none;
};

// General Matrix Multiply (GEMM) over row-major matrices, with a matrix
// addend. The formula is:
//
// `out = scale * op(a) * op(b) + addend_scale * addend`
//
// where `op(x)` is `x` or its transpose, as the options select.
//
// The shapes must agree: `op(a)` has a row per row of `out`, `op(b)` has a
// column per column of `out`, and `op(a)` has as many columns as `op(b)` has
// rows.
//
// `addend` picks what the product is added to:
//
//   empty (the default)  out = scale * op(a) * op(b)
//   `out` itself         out = scale * op(a) * op(b) + addend_scale * out
//   another matrix       out = scale * op(a) * op(b) + addend_scale * addend
//
// Another matrix must have the extent of `out`.
//
// The distinction between `addend` and `out` is this layer's. Down in
// `cublas_handle::multiply_row_major` and below, `C` is one in-out matrix that
// is read scaled by `beta` and overwritten with the result. So `out` is always
// `C`, another matrix is copied into `out` before the call, and an empty
// addend or a zero `addend_scale` passes `beta` as zero, under which cuBLAS
// never reads `out`.
//
// `out` must not be `a` or `b`. Returns false when a launch or copy is
// refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
requires GemmElement<device_element_t<Out>>
[[nodiscard]] bool
gemm(const cublas_handle& blas, Out&& out, input_view_t<Out> a,
    input_view_t<Out> b, gemm_options<device_element_t<Out>> options = {},
    input_view_t<Out> addend = {}) {
  const auto& out_view = out.as_view();
  // The extent of `op(x)`.
  const auto op_extent = [](const auto& x, cublas_operation op) {
    return (op == cublas_operation::none)
               ? x.extent()
               : x.extent().transposed();
  };
  const auto op_a_extent = op_extent(a, options.op_a);
  [[maybe_unused]] const auto op_b_extent = op_extent(b, options.op_b);
  assert((out_view.row_extent() == op_a_extent.row_count) &&
         (out_view.col_extent() == op_b_extent.col_count));
  assert(op_a_extent.col_count == op_b_extent.row_count);
  assert((out_view.get() != a.get()) && (out_view.get() != b.get()));

  // To create the `out`/`addend` distinction, we need to initialize `C` with a
  // copy of the `addend`, if there's anything there for us.
  const auto has_addend = !addend.empty() && (options.addend_scale != 0);
  if (has_addend && (addend.get() != out_view.get())) {
    assert((addend.row_extent() == out_view.row_extent()) &&
           (addend.col_extent() == out_view.col_extent()));
    // Copy `addend` into `out`, as part of the same default stream as `blas`.
    if (!out_view.load(addend)) return false;
  }
  const auto beta =
      has_addend ? options.addend_scale : device_element_t<Out>{};

  // Each leading dimension is its view's stride, the row length as stored.
  const auto m = static_cast<int>(out_view.row_extent());
  const auto n = static_cast<int>(out_view.col_extent());
  const auto k = static_cast<int>(op_a_extent.col_count);
  const auto lda = static_cast<int>(a.stride());
  const auto ldb = static_cast<int>(b.stride());
  const auto ldc = static_cast<int>(out_view.stride());
  return blas
      .multiply_row_major(m, n, k, options.scale, a.get(), lda, b.get(), ldb,
          beta, out_view.get(), ldc, options.op_a, options.op_b)
      .ok();
}

// GEMM over row-major matrices, with a bias row added to every row.
//
// `out = scale * op(a) * op(b) + addend_scale * bias`, where `bias` has an
// element per column of `out` and is the addend of every row. The operands
// and options are those of the matrix-addend `gemm`.
//
// `bias` must not be empty. `out` must not be `a` or `b`. Returns false when a
// launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
requires GemmElement<device_element_t<Out>>
[[nodiscard]] bool gemm(const cublas_handle& blas, Out&& out,
    input_view_t<Out> a, input_view_t<Out> b, const input_buffer_t<Out>& bias,
    gemm_options<device_element_t<Out>> options = {}) {
  const auto& out_view = out.as_view();
  assert(bias);
  assert(bias.size() == out_view.col_extent());

  // We can't directly add `bias` as part of the GEMM operation because `bias`
  // is a row vector and GEMM expects a matrix for `addend`. In principle, we
  // could pass an `addend_scale` of 0 and then do the `bias` add in a separate
  // kernel, but we can get the same effect cheaper by broadcasting `bias` into
  // `out`, turning it into a matrix that we then use as the `addend`.
  // Essentially: Ctrl+C, Ctrl+V FTW.
  details::fill_rows<<<blocks_for(out_view.size()), threads_per_block>>>(
      out_view.get(), out_view.size(), out_view.col_extent(),
      out_view.stride(), bias.get());
  if (!cuda_last_status{}) return false;

  return gemm(blas, out_view, a, b, options, out_view);
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
template<DeviceMatrixLike Out>
requires GemmElement<device_element_t<Out>>
[[nodiscard]] bool
linear_projection(const cublas_handle& blas, Out&& out, input_view_t<Out> in,
    input_view_t<Out> weight, const input_buffer_t<Out>& bias) {
  return gemm(blas, out, in, weight, bias);
}

#pragma endregion
#pragma region add and subtract

namespace details {

// Combine the `size` elements of two views `cols` wide through `op`, with
// rows `a_stride` and `b_stride` apart, into rows `out_stride` apart, one
// thread per element.
//
// Thread `i` takes element `i` in row-major order, so the lanes of a warp
// read 32 consecutive columns, and `strided_offset` steps over the gap
// between the rows of a strided view.
template<typename T, typename Op>
__global__ void
combine_elements(T* out, size_t out_stride, const T* a, size_t a_stride,
    const T* b, size_t b_stride, size_t size, size_t cols, Op op) {
  const auto i = cuda_kernel::x_index<size_t>();
  if (i < size)
    out[cuda_kernel::strided_offset(i, cols, out_stride)] = op(
        a[cuda_kernel::strided_offset(i, cols, a_stride)],
        b[cuda_kernel::strided_offset(i, cols, b_stride)]);
}

// Combine `a` and `b` elementwise through `op`, into `out`.
//
// All three views must have the same extent. `out` can be the same view as
// `a` or as `b`, but must not otherwise overlap either. Returns false when
// the launch is refused, leaving `out` unspecified.
template<typename T, typename Op>
[[nodiscard]] bool
combine(cuda_matrix_view<T> out, const_view_t<T> a, const_view_t<T> b, Op op) {
  assert((a.row_extent() == b.row_extent()) &&
         (a.col_extent() == b.col_extent()));
  assert((out.row_extent() == a.row_extent()) &&
         (out.col_extent() == a.col_extent()));
  assert(is_same_or_disjoint(out.as_span(), a.as_span()));
  assert(is_same_or_disjoint(out.as_span(), b.as_span()));

  combine_elements<<<blocks_for(out.size()), threads_per_block>>>(out.get(),
      out.stride(), a.get(), a.stride(), b.get(), b.stride(), out.size(),
      out.col_extent(), op);
  return cuda_last_status{}.ok();
}

} // namespace details

// Add `a` and `b` elementwise, into `out`.
//
// All three views must have the same extent. `out` can be the same view as
// `a` or as `b`, adding in place, but must not otherwise overlap either.
// Returns false when the launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
requires Arithmetic<device_element_t<Out>>
[[nodiscard]] bool add(Out&& out, input_view_t<Out> a, input_view_t<Out> b) {
  return details::combine(out.as_view(), a, b, std::plus<>{});
}

// Subtract `b` from `a` elementwise, into `out`.
//
// All three views must have the same extent. `out` can be the same view as
// `a` or as `b`, subtracting in place, but must not otherwise overlap either.
// Returns false when the launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
requires Arithmetic<device_element_t<Out>>
[[nodiscard]] bool
subtract(Out&& out, input_view_t<Out> a, input_view_t<Out> b) {
  return details::combine(out.as_view(), a, b, std::minus<>{});
}

#pragma endregion

} // namespace corvid::cuda::linalg
