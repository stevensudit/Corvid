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
#include <cmath>
#include <cstddef>
#include <type_traits>

#include <cuda_runtime.h>

#include "../../linalg/linear_algebra.h"
#include "../../llm/llm_ops.h"
#include "../../meta/containers.h"
#include "../cuda_block.cuh"
#include "../cuda_buffer.cuh"
#include "../cuda_kernel.cuh"
#include "../cuda_matrix.cuh"
#include "../cuda_status.cuh"
#include "../linalg/linear_algebra.cuh"

// The transformer ops on the device, in fp32, one free function per op.
//
// Every op writes into a caller-owned `cuda_matrix_view`, launches on the
// default stream, and returns whether its launches were accepted, so a fault
// inside a kernel surfaces at the next synchronizing call, such as a `store`.
// The scalar formulas are the CPU ops' own, shared through `CUDA_HOST_DEVICE`.
namespace corvid::cuda::llm {

using namespace corvid::cuda::linalg;

#pragma region layer_norm

namespace details {

// Normalize one row of `in`, `cols` wide, to a mean of 0 and a variance of
// 1, then scale by `weight` and shift by `bias`, into the same row of `out`.
//
// The block index picks the row, and each thread takes the columns at its
// index and every `blockDim.x` after it, so with 256 threads and 768 columns
// each thread holds 3.
//
// The mean and the variance are each a block-wide sum, and the variance is
// the mean of the squared deviations from the mean, as the CPU op computes
// it, rather than the mean of the squares minus the square of the mean.
//
// In-place is safe. Each element is read only by the thread that writes it,
// and the writes come after both sums, so no thread reads a column another
// has already overwritten.
template<Floating T>
__global__ void apply_layer_norm(T* out, size_t out_stride, const T* in,
    size_t in_stride, size_t cols, const T* weight, const T* bias, T eps) {
  const auto row = cuda_kernel::x_block<size_t>();
  const auto* in_row = in + (row * in_stride);
  auto* out_row = out + (row * out_stride);
  const auto first = cuda_kernel::x_thread<size_t>();
  const auto step = cuda_kernel::x_block_dim<size_t>();
  const auto count = static_cast<T>(cols);

  T total{};
  for (auto c = first; c < cols; c += step) total += in_row[c];
  const auto mean = cuda_block::sum(total) / count;

  T squares{};
  for (auto c = first; c < cols; c += step) {
    const auto deviation = in_row[c] - mean;
    squares += deviation * deviation;
  }
  const auto variance = cuda_block::sum(squares) / count;
  // Note that we could have used `rsqrt(variance + eps)` instead of `1 /
  // std::sqrt(variance + eps)`, which is faster but yields slightly different
  // results. We still might, but we'd need to tolerance it.
  const auto inv_std = T{1} / std::sqrt(variance + eps);

  for (auto c = first; c < cols; c += step)
    out_row[c] = corvid::linalg::scale_shift(
        corvid::linalg::standardize(in_row[c], mean, inv_std), weight[c],
        bias[c]);
}

} // namespace details

// Normalize each row of `in` to a mean of 0 and a variance of 1, then scale by
// `weight` and shift by `bias`, elementwise, into `out`.
//
// The contract is that of the CPU `corvid::llm::layer_norm`. For T tokens of
// C features:
//
//   out     T x C
//   in      T x C
//   weight  C
//   bias    C
//
// `out` can be the same view as `in`, normalizing in place, but must not
// otherwise overlap it. `eps` is added to each row's variance inside the
// square root. Returns false when the launch is refused, leaving `out`
// unspecified.
template<Floating T>
[[nodiscard]] bool layer_norm(cuda_matrix_view<T> out, const_view_t<T> in,
    const cuda_buffer<T>& weight, const cuda_buffer<T>& bias,
    std::type_identity_t<T> eps) {
  [[maybe_unused]] const auto width = in.col_extent();
  assert((out.row_extent() == in.row_extent()) && (out.col_extent() == width));
  assert((weight.size() == width) && (bias.size() == width));
  assert(is_same_or_disjoint(out.as_span(), in.as_span()));
  assert(is_disjoint(out.as_span(), weight.as_span()));
  assert(is_disjoint(out.as_span(), bias.as_span()));

  details::apply_layer_norm<T>
      <<<static_cast<unsigned>(out.row_extent()), threads_per_block>>>(
          out.get(), out.stride(), in.get(), in.stride(), width, weight.get(),
          bias.get(), eps);
  return cuda_last_status{}.ok();
}

// `layer_norm` over an owning `out`, so the call needs no `view()`.
template<Floating T>
[[nodiscard]] bool layer_norm(cuda_matrix<T>& out, const_view_t<T> in,
    const cuda_buffer<T>& weight, const cuda_buffer<T>& bias,
    std::type_identity_t<T> eps) {
  return layer_norm(out.view(), in, weight, bias, eps);
}

#pragma endregion
#pragma region gelu_new

namespace details {

// Apply the scalar `gelu_new` to the `size` elements of a view `cols` wide,
// from rows `in_stride` apart into rows `out_stride` apart, one thread per
// element.
template<Floating T>
__global__ void apply_gelu_new(T* out, size_t out_stride, const T* in,
    size_t in_stride, size_t size, size_t cols) {
  const auto i = cuda_kernel::x_index<size_t>();
  if (i < size)
    out[cuda_kernel::strided_offset(i, cols, out_stride)] =
        corvid::llm::gelu_new(
            in[cuda_kernel::strided_offset(i, cols, in_stride)]);
}

} // namespace details

// Apply `gelu_new` to every element of `in`, into `out`.
//
// `out` and `in` must have the same extent. `out` can be `in`, applying it in
// place. Returns false when the launch is refused, leaving `out` unspecified.
template<Floating T>
[[nodiscard]] bool gelu_new(cuda_matrix_view<T> out, const_view_t<T> in) {
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == in.col_extent()));

  details::apply_gelu_new<T><<<blocks_for(out.size()), threads_per_block>>>(
      out.get(), out.stride(), in.get(), in.stride(), out.size(),
      out.col_extent());
  return cuda_last_status{}.ok();
}

// `gelu_new` over an owning `out`, so the call needs no `view()`.
template<Floating T>
[[nodiscard]] bool gelu_new(cuda_matrix<T>& out, const_view_t<T> in) {
  return gelu_new(out.view(), in);
}

#pragma endregion

} // namespace corvid::cuda::llm
