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
#include "../../llm/token_id.h"
#include "../../meta/containers.h"
#include "../cuda_buffer.cuh"
#include "../cuda_kernel.cuh"
#include "../cuda_matrix.cuh"
#include "../cuda_reduce.cuh"
#include "../cuda_status.cuh"
#include "../cuda_std.cuh"
#include "../linalg/linear_algebra.cuh"

// The transformer ops on the device, in fp32, one free function per op.
//
// Every op writes into a caller-owned output (a `cuda_matrix` or a
// `cuda_matrix_lens`), launches on the default stream, and returns whether its
// launches were accepted, so a fault inside a kernel surfaces at the next
// synchronizing call, such as a `store`. The scalar formulas are the CPU ops'
// own, shared through `CUDA_HOST_DEVICE`.
namespace corvid::cuda::llm {

using namespace corvid::cuda::linalg;
using corvid::llm::token_id;
using matrix_types::col_ndx;
using matrix_types::row_ndx;

#pragma region layer_norm

namespace details {

// Normalize one row of `in`, `cols` wide, to a mean of 0 and a variance of
// 1, then scale by `weight` and shift by `bias`, writing into the matching row
// of `out`.
//
// The block index picks the row, and each thread takes the columns at its
// index and at every `blockDim.x` after it, so with 256 threads and 768
// columns each thread holds 3. A thread past the last column sums nothing and
// still joins both block sums, which every thread must.
//
// The mean and the variance are each a block-wide sum.
//
// In-place is safe. Each element is read only by the thread that writes it,
// and the writes come after both sums, so no thread reads a column another
// has already overwritten.
template<Floating T>
__global__ void
apply_layer_norm(kernel_matrix_lens<T> out, kernel_matrix_view<T> in,
    size_t cols, const T* weight, const T* bias, T eps) {
  using corvid::linalg::scale_shift;
  using corvid::linalg::standardize;
  const auto row = cuda_kernel::x_block<size_t>();
  const kernel_col_range columns{cols};
  const auto count = static_cast<T>(cols);

  T total{};
  for (const auto c : columns) total += in[row, c];
  const auto mean = cuda_reduce::block_sum(total) / count;

  // The variance is calculated as the mean of the squared deviations from the
  // mean, as the CPU op computes it, rather than the mean of the squares minus
  // the square of the mean.
  T squares{};
  for (const auto c : columns) {
    const auto deviation = in[row, c] - mean;
    squares += deviation * deviation;
  }
  const auto variance = cuda_reduce::block_sum(squares) / count;

  // The inverse standard deviation is calculated as an actual reciprocal of
  // the square root, instead of using `rsqrt`. The latter would be faster, but
  // yield slightly different results.
  const auto inv_std = T{1} / std::sqrt(variance + eps);

  for (const auto c : columns)
    out[row, c] = scale_shift(standardize(in[row, c], mean, inv_std),
        weight[c], bias[c]);
}

} // namespace details

// Normalize each row of `in` to a mean of 0 and a variance of 1, then scale by
// `weight` and shift by `bias`, elementwise, writing into `out`.
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
template<DeviceMatrixLike Out>
requires Floating<device_element_t<Out>>
[[nodiscard]] bool
layer_norm(Out&& out, input_view_t<Out> in, const input_buffer_t<Out>& weight,
    const input_buffer_t<Out>& bias, device_element_t<Out> eps) {
  const auto& out_lens = out.as_lens();
  [[maybe_unused]] const auto width = in.col_extent();
  assert(out_lens.extent() == in.extent());
  assert((weight.size() == width) && (bias.size() == width));
  assert(is_same_or_disjoint(out_lens.as_span(), in.as_span()));
  assert(is_disjoint(out_lens.as_span(), weight.as_span()));
  assert(is_disjoint(out_lens.as_span(), bias.as_span()));

  details::apply_layer_norm<<<out_lens.row_extent(), threads_per_block>>>(
      kernel_matrix_lens{out_lens}, kernel_matrix_view{in}, width,
      weight.get(), bias.get(), eps);
  return cuda_last_status{}.ok();
}

#pragma endregion
#pragma region gelu_new

namespace details {

// Apply the scalar `gelu_new` to the elements of `in`, writing into `out`,
// both `extent` in size, one thread per element.
template<Floating T>
__global__ void apply_gelu_new(kernel_matrix_lens<T> out,
    kernel_matrix_view<T> in, matrix_extent extent) {
  using corvid::llm::gelu_new;
  if (const kernel_coord at; at.is_within(extent)) out[at] = gelu_new(in[at]);
}

} // namespace details

// Apply `gelu_new` to every element of `in`, writing into `out`.
//
// `out` and `in` must have the same extent. `out` can be `in`, applying it in
// place. Returns false when the launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
requires Floating<device_element_t<Out>>
[[nodiscard]] bool gelu_new(Out&& out, input_view_t<Out> in) {
  const auto& out_lens = out.as_lens();
  assert(out_lens.extent() == in.extent());

  details::apply_gelu_new<<<grid_for(out_lens), threads_per_block>>>(
      kernel_matrix_lens{out_lens}, kernel_matrix_view{in}, out_lens.extent());
  return cuda_last_status{}.ok();
}

#pragma endregion
#pragma region embed_tokens

namespace details {

// Copy the row of `table` that each ID names into `out`, `extent` in size, one
// thread per element.
template<typename T>
__global__ void gather_rows(kernel_matrix_lens<T> out, const token_id* ids,
    kernel_matrix_view<T> table, matrix_extent extent) {
  if (const kernel_coord at; at.is_within(extent))
    out[at] = table[*ids[at.row], at.col];
}

} // namespace details

// Look up each ID's row of `table`, writing into `out`.
//
// The contract is that of the CPU `corvid::llm::embed_tokens`. With T tokens,
// V vocabulary entries, and width C:
//
//   ids    [T]     one token ID per row of `out`
//   table  [V, C]  a row per vocabulary entry
//   out    [T, C]  a row per ID, written
//
// `out` must have one row per ID and the width of `table`, every ID must
// index a row of `table`, and `out` must not overlap `table`. Returns false
// when the launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
[[nodiscard]] bool embed_tokens(Out&& out, const cuda_buffer<token_id>& ids,
    input_view_t<Out> table) {
  const auto& out_lens = out.as_lens();
  assert(out_lens.row_extent() == ids.size());
  assert(out_lens.col_extent() == table.col_extent());
  assert(is_disjoint(out_lens.as_span(), table.as_span()));

  details::gather_rows<<<grid_for(out_lens), threads_per_block>>>(
      kernel_matrix_lens{out_lens}, ids.get(), kernel_matrix_view{table},
      out_lens.extent());
  return cuda_last_status{}.ok();
}

#pragma endregion
#pragma region embed_positions

// Add each row's position embedding from `table`, writing into `out`.
//
// The contract is that of the CPU `corvid::llm::embed_positions`. With T
// tokens, a context of P positions, and width C:
//
//   table  [P, C]  a row per position
//   out    [T, C]  a row per token, added to in place
//
// `out` must have the width of `table`, its last row's position must be in
// `table`, and it must not overlap `table`. Returns false when the launch is
// refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
requires Arithmetic<device_element_t<Out>>
[[nodiscard]] bool embed_positions(Out&& out, input_view_t<Out> table,
    size_t first_position = 0) {
  const auto& out_lens = out.as_lens();
  assert(first_position + out_lens.row_extent() <= table.row_extent());
  assert(out_lens.col_extent() == table.col_extent());

  return add(out, out_lens,
      table[{row_ndx{first_position}, col_ndx{0}}, out_lens.extent()]);
}

#pragma endregion
#pragma region attend

namespace details {

// Set every element of `scores` after its row's token to negative infinity,
// over `extent`, one thread per element.
//
// `scores` is a stack of matrices of `new_count` rows each, and a row's token
// is `cached_count` plus its index within its own matrix.
template<Floating T>
__global__ void apply_causal_mask(kernel_matrix_lens<T> scores,
    matrix_extent extent, size_t cached_count, size_t new_count) {
  if (const kernel_coord at;
      at.is_within(extent) && (at.col > cached_count + (at.row % new_count)))
    scores[at] = -::cuda::std::numeric_limits<T>::infinity();
}

} // namespace details

// Set every element of `scores` whose column exceeds its row's token to
// negative infinity, so a softmax over the row gives the tokens after it no
// weight.
//
// `scores` has a row per new token and a column per token (cached and new)
// with the cached ones first. With M cached tokens and N new ones, row `i` is
// token M + `i` and keeps columns 0 through M + `i`. With nothing cached,
// `scores` is square and the mask starts past its diagonal.
//
// `scores` is one such matrix or a stack of them, each masked on its own, so
// its row count must be a multiple of N, which is its column count less
// `cached_count`, and must not be zero. Returns false when the launch is
// refused, leaving `scores` unspecified.
template<DeviceMatrixLike Out>
requires Floating<device_element_t<Out>>
[[nodiscard]] bool causal_mask(Out&& scores, size_t cached_count = 0) {
  const auto& scores_lens = scores.as_lens();
  assert(cached_count < scores_lens.col_extent());
  const auto new_count = scores_lens.col_extent() - cached_count;
  assert(scores_lens.row_extent() % new_count == 0);
  details::apply_causal_mask<<<grid_for(scores_lens), threads_per_block>>>(
      kernel_matrix_lens{scores_lens}, scores_lens.extent(), cached_count,
      new_count);
  return cuda_last_status{}.ok();
}

// Let each token read from the tokens at or before it, across all attention
// heads, writing into `out`.
//
// The contract is that of the CPU `corvid::llm::attend`, which also holds the
// worked explanation. For M cached tokens, N new ones, T = M + N, width C, and
// H heads of width D = C / H:
//
//   out     N x C     a row per new token, each head writing its D columns
//   qkv     T x 3C    each token's queries, then keys, then values, the
//                     cached tokens first
//   scores  HN x T    scratch, holding every head's N x T weights, stacked
//
// Where the CPU op walks each token's causal prefix one head at a time, this
// one takes every head's whole N x T score matrix at once, in four launches: a
// batched GEMM of the new tokens' queries against the transposed keys (scaled
// by 1 / sqrt(D)), the causal mask, the row softmax, and a batched GEMM of the
// weights against the values. With nothing cached, N is T.
//
// `qkv` must be three times as wide as `out` and have at least as many rows,
// `out`'s width must divide evenly by `head_count`, and `scores` must have a
// column per row of `qkv` and a row per row of `out` per head. `out` must not
// overlap `qkv` or `scores`, and `scores` must not overlap `qkv`. Returns
// false when a launch is refused, leaving `out` and `scores` unspecified.
template<DeviceMatrixLike Out>
requires GemmElement<device_element_t<Out>>
[[nodiscard]] bool
attend(const cublas_handle& blas, Out&& out, input_view_t<Out> qkv,
    size_t head_count, cuda_matrix_lens<device_element_t<Out>> scores) {
  using T = device_element_t<Out>;

  const auto& out_lens = out.as_lens();
  const auto new_count = out_lens.row_extent();
  const auto total_count = qkv.row_extent();
  const auto width = out_lens.col_extent();
  assert(total_count >= new_count);
  assert(qkv.col_extent() == 3 * width);
  assert(head_count && (width % head_count == 0));
  assert((scores.row_extent() == head_count * new_count) &&
         (scores.col_extent() == total_count));
  assert(is_disjoint(out_lens.as_span(), qkv.as_span()));
  assert(is_disjoint(out_lens.as_span(), scores.as_span()));
  assert(is_disjoint(scores.as_span(), qkv.as_span()));
  const auto head_width = width / head_count;
  const auto scale = T{1} / std::sqrt(static_cast<T>(head_width));

  const auto one_third = [&](size_t which) {
    return qkv[{row_ndx{0}, col_ndx{which * width}},
        {.row_count = total_count, .col_count = width}];
  };
  // The queries are those of the new tokens, below the cached ones.
  const auto q = one_third(0)[{row_ndx{total_count - new_count}, col_ndx{0}},
      {.row_count = new_count, .col_count = width}];
  const auto k = one_third(1);
  const auto v = one_third(2);

  // A head is a block of columns in `q`, `k`, `v`, and `out`, and a block of
  // rows in `scores`.
  if (!gemm_batched(blas, scores, q, k,
          {.count = head_count,
              .a = matrix_axis::cols,
              .b = matrix_axis::cols,
              .out = matrix_axis::rows},
          {.scale = scale, .op_b = cublas_operation::transpose}))
    return false;
  if (!causal_mask(scores, total_count - new_count)) return false;
  if (!softmax(scores, scores)) return false;
  return gemm_batched(blas, out, scores, v,
      {.count = head_count,
          .a = matrix_axis::rows,
          .b = matrix_axis::cols,
          .out = matrix_axis::cols});
}

#pragma endregion
#pragma region logits

// Score every vocabulary entry after every token of `in`, writing into `out`.
//
// The contract is that of the CPU `corvid::llm::compute_all_logits`. Each
// logit is the dot product of a token's row of `in` with a vocabulary entry's
// row of `vocab`. This is a projection through the transpose of `vocab` with
// no bias, so it is one GEMM. With T tokens, V vocabulary entries, and width
// C:
//
//   out    T x V  one logit per token and vocabulary entry
//   in     T x C  a row per token
//   vocab  V x C  a row per vocabulary entry
//
// A one-row `in` scores one token. `out` must not be `in` or `vocab`. Returns
// false when the launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
requires GemmElement<device_element_t<Out>>
[[nodiscard]] bool compute_logits(const cublas_handle& blas, Out&& out,
    input_view_t<Out> in, input_view_t<Out> vocab) {
  return gemm(blas, out, in, vocab, {.op_b = cublas_operation::transpose});
}

#pragma endregion

} // namespace corvid::cuda::llm
