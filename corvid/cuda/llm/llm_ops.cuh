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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <cuda_runtime.h>

#include "../../linalg/linear_algebra.h"
#include "../../llm/llm_ops.h"
#include "../../llm/token_id.h"
#include "../../meta/containers.h"
#include "../bfloat16.cuh"
#include "../cuda_buffer.cuh"
#include "../cuda_kernel.cuh"
#include "../cuda_matrix.cuh"
#include "../cuda_reduce.cuh"
#include "../cuda_status.cuh"
#include "../cuda_std.cuh"
#include "../linalg/linear_algebra.cuh"

// The transformer ops on the device, one free function per op, over `float`,
// `double`, or `bfloat16_t`.
//
// Every op writes into a caller-owned output (a `cuda_matrix` or a
// `cuda_matrix_lens`), launches on the default stream, and returns whether its
// launches were accepted, so a fault inside a kernel surfaces at the next
// synchronizing call, such as a `store`. The scalar formulas are the CPU ops'
// own, shared through `CUDA_HOST_DEVICE`, and a kernel computes them in
// `compute_t` of the element type it reads, rounding each result once, at
// its store, to the element type it writes.
namespace corvid::cuda::llm {

using namespace corvid::cuda::linalg;
using corvid::linalg::scale_shift;
using corvid::linalg::standardize;
using corvid::llm::gelu_new; // Overloaded.
using corvid::llm::token_id;
using matrix_types::col_ndx;
using matrix_types::row_ndx;

#pragma region layer_norm

// What standardizes one row of a layer norm's input, which is its mean and
// the reciprocal of its standard deviation.
template<DeviceFloating T>
struct row_statistics {
  T mean;
  T inv_std;
};

namespace details {

// `compute_row_statistics` computes the `row_statistics` of row `row` of `in`,
// over `columns`, with `eps` added to the variance inside the square root.
//
// The mean and the variance are each a block-wide sum, so every thread of the
// block must call this, even one past the last column, which sums nothing.
template<DeviceFloating From>
__device__ row_statistics<compute_t<From>>
compute_row_statistics(kernel_matrix_view<From> in, size_t row,
    kernel_col_range columns, compute_t<From> eps) {
  const auto count = static_cast<compute_t<From>>(columns.cols);

  compute_t<From> total{};
  for (const auto col : columns) total += widen(in[row, col]);
  const auto mean = cuda_reduce::block_sum(total) / count;

  // The variance is calculated as the mean of the squared deviations from the
  // mean, as the CPU op computes it, rather than the mean of the squares minus
  // the square of the mean.
  compute_t<From> squares{};
  for (const auto col : columns) {
    const auto deviation = widen(in[row, col]) - mean;
    squares += deviation * deviation;
  }
  const auto variance = cuda_reduce::block_sum(squares) / count;

  // The inverse standard deviation is calculated as an actual reciprocal of
  // the square root, instead of using `rsqrt`. The latter would be faster, but
  // yield slightly different results.
  return {.mean = mean,
      .inv_std = compute_t<From>{1} / std::sqrt(variance + eps)};
}

// `apply_layer_norm` normalizes one row of `in`, `cols` wide, to a mean of 0
// and a variance of 1, then scale by `weight` and shift by `bias`, writing
// into the matching row of `out`.
//
// The block index picks the row, and each thread takes the columns at its
// index and at every `blockDim.x` after it, so with 256 threads and 768
// columns each thread holds 3. A thread past the last column sums nothing and
// still joins both block sums, which every thread must.
//
// In-place is safe. Each element is read only by the thread that writes it,
// and the writes come after both sums, so no thread reads a column another
// has already overwritten.
template<DeviceFloating To, DeviceFloating From>
__global__ void
apply_layer_norm(kernel_matrix_lens<To> out, kernel_matrix_view<From> in,
    size_t cols, const From* weight, const From* bias, compute_t<From> eps) {
  const auto row = cuda_kernel::x_block<size_t>();
  const kernel_col_range columns{cols};
  const auto [mean, inv_std] = compute_row_statistics(in, row, columns, eps);

  for (const auto col : columns)
    out[row, col] =
        narrow<To>(scale_shift(standardize(widen(in[row, col]), mean, inv_std),
            widen(weight[col]), widen(bias[col])));
}

} // namespace details

// `layer_norm` normalizes each row of `in` to a mean of 0 and a variance of 1,
// then scales by `weight` and shifts by `bias`, elementwise, writing into
// `out`.
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
// otherwise overlap it. `in`, `weight`, and `bias` share an element type,
// and `out` may hold another, each result rounded once, to it. `eps` is
// added to each row's variance inside the square root. Returns false when
// the launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out, DeviceMatrixViewable In>
requires DeviceFloating<device_element_t<Out>> &&
         DeviceFloating<device_value_t<In>>
[[nodiscard]] bool layer_norm(Out&& out, const In& in,
    const cuda_buffer<device_value_t<In>>& weight,
    const cuda_buffer<device_value_t<In>>& bias,
    compute_t<device_value_t<In>> eps) {
  const auto& out_lens = out.as_lens();
  const cuda_matrix_view<device_value_t<In>> in_view = in;
  [[maybe_unused]] const auto width = in_view.col_extent();
  assert(out_lens.extent() == in_view.extent());
  assert((weight.size() == width) && (bias.size() == width));
  assert(is_same_or_disjoint(out_lens.as_span(), in_view.as_span()));
  assert(is_disjoint(out_lens.as_span(), weight.as_span()));
  assert(is_disjoint(out_lens.as_span(), bias.as_span()));

  details::apply_layer_norm<<<out_lens.row_extent(), threads_per_block>>>(
      kernel_matrix_lens{out_lens}, kernel_matrix_view{in_view}, width,
      weight.get(), bias.get(), eps);
  return cuda_last_status{}.ok();
}

#pragma endregion
#pragma region layer_norm_backward

namespace details {

// `apply_layer_norm_backward` computes how much the loss moves per unit of
// each value in one row of a layer norm's input, `cols` wide, from how much it
// moves per unit of each value in the matching row of its output,
// `out_gradient`, writing into the row of `in_gradient`, and recording the
// row's `row_statistics` in `statistics`.
//
// Note that `out_gradient` is the input and `in_gradient` is the output,
// because these names refer to the forward-pass value, not the direction data
// flows through during backpropagation.
//
// The forward pass standardized each row of `in`, then scaled the row by
// `weight` and shifted it by `bias`. Its backward step has three gradients to
// produce: with respect to `weight`, to `bias`, and to `in`; the last of
// which is what propagates further back.
//
// This kernel produces that last one, reaching back through the scale and then
// through the standardization, so it reads the forward input `in` (to
// recompute the row's statistics and standardized values) and `weight`, but
// has no use for `bias`. The `weight` and `bias` gradients are sums over every
// row, which a block holding one row cannot form, so
// `add_layer_norm_parameter_gradients` produces them afterward from the
// statistics this kernel records.
//
// For T tokens of C features:
//
//   in_gradient   [T, C]  written, this block's row
//   statistics    [T]     written, this block's row
//   out_gradient  [T, C]  read, this block's row
//   in            [T, C]  read, this block's row
//   weight        [C]
//
// The block index picks the row, and each thread takes the columns at its
// index and at every `blockDim.x` after it. The two means the formula needs
// are each a block-wide sum.
template<DeviceFloating T, DeviceFloating G>
__global__ void apply_layer_norm_backward(kernel_matrix_lens<T> in_gradient,
    row_statistics<compute_t<T>>* statistics,
    kernel_matrix_view<G> out_gradient, kernel_matrix_view<T> in, size_t cols,
    const T* weight, compute_t<T> eps) {
  const auto row = cuda_kernel::x_block<size_t>();
  const kernel_col_range columns{cols};
  const auto count = static_cast<compute_t<T>>(cols);
  const auto [mean, inv_std] = compute_row_statistics(in, row, columns, eps);

  // The loss's movement per unit of each standardized value is its movement
  // per unit of the output times the weight. The row's mean of that, and of
  // its product with the standardized value, are what the row's mean and
  // standard deviation feed back.
  compute_t<T> scaled_total{};
  compute_t<T> projected_total{};
  for (const auto col : columns) {
    const auto scaled = widen(out_gradient[row, col]) * widen(weight[col]);
    scaled_total += scaled;
    projected_total +=
        scaled * standardize(widen(in[row, col]), mean, inv_std);
  }
  const auto scaled_mean = cuda_reduce::block_sum(scaled_total) / count;
  const auto projected_mean = cuda_reduce::block_sum(projected_total) / count;

  for (const auto col : columns) {
    const auto scaled = widen(out_gradient[row, col]) * widen(weight[col]);
    const auto standardized = standardize(widen(in[row, col]), mean, inv_std);
    in_gradient[row, col] = narrow<T>(
        inv_std * (scaled - scaled_mean - (standardized * projected_mean)));
  }

  if (cuda_kernel::x_thread() == 0) statistics[row] = {mean, inv_std};
}

// `add_layer_norm_parameter_gradients` adds how much the loss moves per unit
// of a layer norm's weight and bias, summed over every row of `extent`, to
// `weight_gradient` and `bias_gradient`, one thread per column.
//
// These are the two gradients `apply_layer_norm_backward` leaves to this
// kernel. A column's bias gradient is the plain sum of that column of
// `out_gradient`, since the forward pass added the bias to every row as-is.
// Its weight gradient is the same sum with each term multiplied by the
// standardized value the weight scaled, recomputed from `in` and the row's
// `statistics`. For T tokens of C features:
//
//   weight_gradient  [C]     added to
//   bias_gradient    [C]     added to
//   out_gradient     [T, C]
//   in               [T, C]
//   statistics       [T]     as `apply_layer_norm_backward` recorded them
//
// A column's two sums run over the rows in order, so the result does not
// depend on how the launch is scheduled.
template<DeviceFloating T, DeviceFloating G>
__global__ void
add_layer_norm_parameter_gradients(T* weight_gradient, T* bias_gradient,
    kernel_matrix_view<G> out_gradient, kernel_matrix_view<T> in,
    const row_statistics<compute_t<T>>* statistics, matrix_extent extent) {
  const auto col = cuda_kernel::x_index<size_t>();
  if (col >= extent.col_count) return;

  compute_t<T> weight_total{};
  compute_t<T> bias_total{};
  for (auto row = 0UZ; row < extent.row_count; ++row) {
    const auto gradient = widen(out_gradient[row, col]);
    bias_total += gradient;
    weight_total +=
        gradient *
        standardize(widen(in[row, col]), statistics[row].mean,
            statistics[row].inv_std);
  }

  weight_gradient[col] = narrow<T>(widen(weight_gradient[col]) + weight_total);
  bias_gradient[col] = narrow<T>(widen(bias_gradient[col]) + bias_total);
}

} // namespace details

// `layer_norm_backward` computes how much the loss moves per unit of each
// value of a layer norm's input from how much it moves per unit of each value
// of its output, writing into `in_gradient`, and adds how much it moves per
// unit of each weight and bias to `weight_gradient` and `bias_gradient`.
//
// This is the backward step of `layer_norm`, whose forward step standardizes
// each row of `in` (subtracts the row's mean and multiplies by the reciprocal
// of its standard deviation), then scales each standardized value by `weight`
// and shifts it by `bias`:
//
//   out = standardized * weight + bias
//
// A gradient is how much the loss moves per unit of a value, as
// `cross_entropy` describes. For T tokens of C features:
//
//   in_gradient      [T, C]  the gradient with respect to `in`, written
//   weight_gradient  [C]     the gradient with respect to `weight`, added to
//   bias_gradient    [C]     the gradient with respect to `bias`, added to
//   out_gradient     [T, C]  the gradient with respect to the forward `out`
//   in               [T, C]  the forward `in`
//   weight           [C]     the forward `weight`
//   statistics       [T]     scratch for each row's `row_statistics`
//
// Nudging `bias[c]` by a small `step` moves `out[t, c]` by `step` in every
// row, and the loss by `out_gradient[t, c] * step` for each, so
// `bias_gradient[c]` is the sum of column c of `out_gradient`.
//
// Nudging `weight[c]` moves `out[t, c]` by `standardized[t, c] * step`, so
// `weight_gradient[c]` is the sum over the rows of `out_gradient[t, c]` times
// `standardized[t, c]`.
//
// Nudging `in[t, c]` moves its own row in three ways. With `s` as the
// standardized row, the loss feels a move in `s[k]` through `g[k]`, the
// product of `out_gradient[t, k]` and `weight[k]`. Directly, `s[c]` moves by
// `inv_std * step`. Through the row's mean, which rises by `step / C`, every
// `s[k]` falls by `inv_std * step / C`. And through the row's standard
// deviation, which grows when `in[t, c]` is above the mean and shrinks when it
// is below, every `s[k]` moves by `-s[k] * s[c] * inv_std * step / C`. The
// three summed over the row, per unit of `step`, are
//
//   in_gradient[t, c] = inv_std * (g[c] - mean(g) - s[c] * mean(g * s))
//
// with both means over the row's C columns.
//
// A caller that spreads one batch over several calls accumulates one batch
// gradient, since the weight and bias gradients are added to and never
// overwritten. Zero them before the first call.
//
// `in_gradient`, `out_gradient`, and `in` must have the same extent, with at
// least one column, and `statistics` an entry per row. `in`, `weight`, and
// the three gradients share an element type, and `out_gradient` may hold
// another (the type the forward step wrote). Each row's mean and reciprocal
// standard deviation are recomputed from `in`, with `eps` as the forward
// step had it. `in_gradient` must not overlap `out_gradient` or `in`.
// Returns false when a launch is refused, leaving `in_gradient`,
// `weight_gradient`, `bias_gradient`, and `statistics` unspecified.
template<DeviceMatrixLike Out, DeviceMatrixViewable OutGradient>
requires DeviceFloating<device_element_t<Out>> &&
         DeviceFloating<device_value_t<OutGradient>>
[[nodiscard]] bool layer_norm_backward(Out&& in_gradient,
    input_buffer_t<Out>& weight_gradient, input_buffer_t<Out>& bias_gradient,
    const OutGradient& out_gradient, input_view_t<Out> in,
    const input_buffer_t<Out>& weight, compute_t<device_element_t<Out>> eps,
    cuda_buffer<row_statistics<compute_t<device_element_t<Out>>>>&
        statistics) {
  const auto& in_gradient_lens = in_gradient.as_lens();
  const cuda_matrix_view<device_value_t<OutGradient>> out_gradient_view =
      out_gradient;
  const auto extent = in.extent();
  const auto width = extent.col_count;
  assert(in_gradient_lens.extent() == extent);
  assert(out_gradient_view.extent() == extent);
  assert(width > 0);
  assert((weight_gradient.size() == width) && (bias_gradient.size() == width));
  assert(weight.size() == width);
  assert(statistics.size() == extent.row_count);
  assert(is_disjoint(in_gradient_lens.as_span(), out_gradient_view.as_span()));
  assert(is_disjoint(in_gradient_lens.as_span(), in.as_span()));

  details::apply_layer_norm_backward<<<extent.row_count, threads_per_block>>>(
      kernel_matrix_lens{in_gradient_lens}, statistics.get(),
      kernel_matrix_view{out_gradient_view}, kernel_matrix_view{in}, width,
      weight.get(), eps);
  if (!cuda_last_status{}) return false;

  details::add_layer_norm_parameter_gradients<<<blocks_for(width),
      threads_per_block>>>(weight_gradient.get(), bias_gradient.get(),
      kernel_matrix_view{out_gradient_view}, kernel_matrix_view{in},
      statistics.get(), extent);
  return cuda_last_status{}.ok();
}

#pragma endregion
#pragma region gelu_new

namespace details {

// Apply the scalar `gelu_new` to the elements of `in`, writing into `out`,
// both `extent` in size, one thread per element.
template<DeviceFloating T>
__global__ void apply_gelu_new(kernel_matrix_lens<T> out,
    kernel_matrix_view<T> in, matrix_extent extent) {
  if (const kernel_coord at; at.is_within(extent))
    out[at] = T{gelu_new(widen(in[at]))};
}

} // namespace details

// Apply `gelu_new` to every element of `in`, writing into `out`.
//
// `out` and `in` must have the same extent. `out` can be `in`, applying it in
// place. Returns false when the launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out>
requires DeviceFloating<device_element_t<Out>>
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
template<DeviceFloating To, DeviceFloating From>
__global__ void gather_rows(kernel_matrix_lens<To> out, const token_id* ids,
    kernel_matrix_view<From> table, matrix_extent extent) {
  if (const kernel_coord at; at.is_within(extent))
    out[at] = narrow<To>(widen(table[*ids[at.row], at.col]));
}

} // namespace details

// `embed_tokens` looks up each ID's row of `table`, writing into `out`.
//
// The contract is that of the CPU `corvid::llm::embed_tokens`. With T tokens,
// V vocabulary entries, and width C:
//
//   ids    [T]     one token ID per row of `out`
//   table  [V, C]  a row per vocabulary entry
//   out    [T, C]  a row per ID, written
//
// `out` must have one row per ID and the width of `table`, every ID must
// index a row of `table`, and `out` must not overlap `table`. `out` may hold
// another element type than `table`, each entry rounded once, to it.
// Returns false when the launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out, DeviceMatrixViewable Table>
requires DeviceFloating<device_element_t<Out>> &&
         DeviceFloating<device_value_t<Table>>
[[nodiscard]] bool
embed_tokens(Out&& out, const cuda_buffer<token_id>& ids, const Table& table) {
  const auto& out_lens = out.as_lens();
  const cuda_matrix_view<device_value_t<Table>> table_view = table;
  assert(out_lens.row_extent() == ids.size());
  assert(out_lens.col_extent() == table_view.col_extent());
  assert(is_disjoint(out_lens.as_span(), table_view.as_span()));

  details::gather_rows<<<grid_for(out_lens), threads_per_block>>>(
      kernel_matrix_lens{out_lens}, ids.get(), kernel_matrix_view{table_view},
      out_lens.extent());
  return cuda_last_status{}.ok();
}

#pragma endregion
#pragma region embed_positions

// `embed_positions` adds each row's position embedding from `table`, writing
// into `out`.
//
// The contract is that of the CPU `corvid::llm::embed_positions`. With T
// tokens, a context of P positions, and width C:
//
//   table  [P, C]  a row per position
//   out    [T, C]  a row per token, added to in place
//
// `out` must have the width of `table`, its last row's position must be in
// `table`, and it must not overlap `table`. `out` may hold another element
// type than `table`, each sum rounded once, to it. Returns false when the
// launch is refused, leaving `out` unspecified.
template<DeviceMatrixLike Out, DeviceMatrixViewable Table>
requires DeviceFloating<device_element_t<Out>> &&
         DeviceFloating<device_value_t<Table>>
[[nodiscard]] bool
embed_positions(Out&& out, const Table& table, size_t first_position = 0) {
  const auto& out_lens = out.as_lens();
  const cuda_matrix_view<device_value_t<Table>> table_view = table;
  assert(first_position + out_lens.row_extent() <= table_view.row_extent());
  assert(out_lens.col_extent() == table_view.col_extent());

  return add(out, out_lens,
      table_view[{row_ndx{first_position}, col_ndx{0}}, out_lens.extent()]);
}

#pragma endregion
#pragma region attend

namespace details {

// Set every element of `scores` after its row's token to negative infinity,
// over `extent`, one thread per element.
//
// `scores` is a stack of matrices of `new_count` rows each, and a row's token
// is `cached_count` plus its index within its own matrix.
template<DeviceFloating T>
__global__ void apply_causal_mask(kernel_matrix_lens<T> scores,
    matrix_extent extent, size_t cached_count, size_t new_count) {
  if (const kernel_coord at;
      at.is_within(extent) && (at.col > cached_count + (at.row % new_count)))
    scores[at] = T{-::cuda::std::numeric_limits<compute_t<T>>::infinity()};
}

} // namespace details

// `causal_mask` sets every element of `scores` whose column exceeds its row's
// token to negative infinity, so a softmax over the row gives the tokens after
// it no weight.
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
requires DeviceFloating<device_element_t<Out>>
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

// `attend` lets each token read from the tokens at or before it, across all
// attention heads, writing into `out`.
//
// The contract is that of the CPU `corvid::llm::attend`, which also holds the
// worked explanation, with `qkv` split into its thirds. For M cached tokens,
// N new ones, T = M + N, width C, and H heads of width D = C / H:
//
//   out      N x C     a row per new token, each head writing its D columns
//   queries  N x C     the new tokens' queries
//   keys     T x C     every token's keys, the cached tokens first
//   values   T x C     every token's values, the cached tokens first
//   scores   HN x T    scratch, every head's N x T scores, stacked
//   weights  HN x T    scratch, the same rows after the softmax
//
// The queries, keys, and scores are held in `compute_t` of `out`'s element
// type, so the scores of a `bfloat16_t` model are exact products, and only
// the weights, which the values product averages, are narrowed.
//
// Where the CPU op walks each token's causal prefix one head at a time, this
// one takes every head's whole N x T score matrix at once, in four launches: a
// batched GEMM of the queries against the transposed keys (scaled by
// 1 / sqrt(D)), the causal mask, the row softmax into `weights`, and a batched
// GEMM of the weights against the values. With nothing cached, N is T.
//
// `queries` must have the extent of `out`, `keys` and `values` the width of
// `out` and at least as many rows, `out`'s width must divide evenly by
// `head_count`, and `scores` and `weights` must have a column per row of
// `keys` and a row per row of `out` per head. `out` must not overlap an
// input or a scratch, no scratch may overlap an input, and `weights` may be
// `scores` only when their element types agree. Returns false when a launch
// is refused, leaving `out` and both scratches unspecified.
template<DeviceMatrixLike Out>
requires GemmElement<device_element_t<Out>>
[[nodiscard]] bool attend(const cublas_handle& blas, Out&& out,
    cuda_matrix_view<compute_t<device_element_t<Out>>> queries,
    cuda_matrix_view<compute_t<device_element_t<Out>>> keys,
    input_view_t<Out> values, size_t head_count,
    cuda_matrix_lens<compute_t<device_element_t<Out>>> scores,
    cuda_matrix_lens<device_element_t<Out>> weights) {
  using wide_t = compute_t<device_element_t<Out>>;

  const auto& out_lens = out.as_lens();
  const auto new_count = out_lens.row_extent();
  const auto total_count = keys.row_extent();
  const auto width = out_lens.col_extent();
  assert(queries.extent() == out_lens.extent());
  assert((keys.col_extent() == width) && (values.extent() == keys.extent()));
  assert(total_count >= new_count);
  assert(head_count && (width % head_count == 0));
  assert((scores.row_extent() == head_count * new_count) &&
         (scores.col_extent() == total_count));
  assert(weights.extent() == scores.extent());
  assert(is_disjoint(out_lens.as_span(), queries.as_span()));
  assert(is_disjoint(out_lens.as_span(), keys.as_span()));
  assert(is_disjoint(out_lens.as_span(), values.as_span()));
  assert(is_disjoint(out_lens.as_span(), scores.as_span()));
  assert(is_disjoint(out_lens.as_span(), weights.as_span()));
  assert(is_disjoint(scores.as_span(), queries.as_span()));
  assert(is_disjoint(scores.as_span(), keys.as_span()));
  assert(is_disjoint(scores.as_span(), values.as_span()));
  assert(is_disjoint(weights.as_span(), queries.as_span()));
  assert(is_disjoint(weights.as_span(), keys.as_span()));
  assert(is_disjoint(weights.as_span(), values.as_span()));
  assert(is_same_or_disjoint(scores.as_span(), weights.as_span()));
  const auto cached_count = total_count - new_count;
  const auto head_width = width / head_count;
  const auto scale =
      gemm_scalar_t<wide_t>{1} /
      std::sqrt(static_cast<gemm_scalar_t<wide_t>>(head_width));

  // A head is a block of columns in the queries, keys, values, and `out`, and
  // a block of rows in the scores and weights.
  if (!gemm_batched(blas, scores, queries, keys,
          {.count = head_count,
              .a = matrix_axis::cols,
              .b = matrix_axis::cols,
              .out = matrix_axis::rows},
          {.scale = scale, .op_b = cublas_operation::transpose}))
    return false;
  if (!causal_mask(scores, cached_count)) return false;
  if (!softmax(weights, scores)) return false;
  return gemm_batched(blas, out, weights, values,
      {.count = head_count,
          .a = matrix_axis::rows,
          .b = matrix_axis::cols,
          .out = matrix_axis::cols});
}

#pragma endregion
#pragma region logits

// `compute_logits` scores every vocabulary entry after every token of `in`,
// writing into `out`.
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
// A one-row `in` scores one token. `in` and `vocab` share an element type,
// and `out` holds that type or, for `bfloat16_t`, `float`. `out` must not be
// `in` or `vocab`. Returns false when the launch is refused, leaving `out`
// unspecified.
template<DeviceMatrixLike Out, DeviceMatrixViewable In,
    DeviceMatrixViewable Vocab>
requires std::same_as<device_value_t<In>, device_value_t<Vocab>> &&
         GemmOutput<device_value_t<In>, device_element_t<Out>>
[[nodiscard]] bool compute_logits(const cublas_handle& blas, Out&& out,
    const In& in, const Vocab& vocab) {
  return gemm(blas, out, in, vocab, {.op_b = cublas_operation::transpose});
}

#pragma endregion
#pragma region pick_greedy

namespace details {

// Write the column of each row's largest element of `in`, `cols` wide, into
// `out`, the first on a tie, one block per row.
//
// Each thread keeps the largest of its columns, the block reduces those to
// the row's largest, and thread 0 writes its column.
template<Arithmetic T>
__global__ void
pick_largest_column(token_id* out, kernel_matrix_view<T> in, size_t cols) {
  const auto row = cuda_kernel::x_block<size_t>();
  const kernel_col_range columns{cols};

  auto best = cuda_reduce::lowest<cuda_reduce::element<T>>;
  for (const auto col : columns)
    if (in[row, col] > best.value) best = {in[row, col], col};

  best = cuda_reduce::block_max(best);

  if (cuda_kernel::x_thread() == 0)
    out[row] = token_id{static_cast<uint32_t>(best.index)};
}

} // namespace details

// `pick_greedy` picks the vocabulary entry with the largest logit in each row
// of `logits`, the first on a tie, writing into `out`.
//
// The contract is that of the CPU `corvid::llm::pick_greedy`, applied to each
// row of logits. With T tokens and V vocabulary entries:
//
//   out     [T]     the ID picked for each row, written
//   logits  [T, V]  a row per token
//
// `out` must have one ID per row of `logits`, which must have at least one
// column. Returns false when the launch is refused, leaving `out` unspecified.
template<DeviceMatrixViewable Logits>
requires Arithmetic<device_value_t<Logits>>
[[nodiscard]] bool
pick_greedy(cuda_buffer<token_id>& out, const Logits& logits) {
  const cuda_matrix_view<device_value_t<Logits>> logits_view = logits;
  assert(out.size() == logits_view.row_extent());
  assert(logits_view.col_extent() > 0);

  details::
      pick_largest_column<<<logits_view.row_extent(), threads_per_block>>>(
          out.get(), kernel_matrix_view{logits_view},
          logits_view.col_extent());
  return cuda_last_status{}.ok();
}

#pragma endregion
#pragma region cross_entropy

namespace details {

// Compute the loss of one row of `logits`, `cols` wide, against its label (the
// correct `token_id`), writing it into `losses`, and the gradient of that loss
// divided by `label_count`, writing into the matching row of `gradient`.
//
// The block index picks the row, and each thread takes the columns at its
// index and every `blockDim.x` after it.
//
// In-place is safe. Every thread reads the label's logit before
// `exponentiate_row`, which is itself safe in place, and the last pass reads
// only what the thread itself wrote.
template<DeviceFloating T>
__global__ void apply_cross_entropy(kernel_matrix_lens<T> gradient, T* losses,
    kernel_matrix_view<T> logits, const token_id* labels, size_t cols,
    compute_t<T> label_count) {
  const auto row = cuda_kernel::x_block<size_t>();
  const kernel_col_range columns{cols};
  const auto label = static_cast<size_t>(*labels[row]);
  const auto label_logit = widen(logits[row, label]);

  // The row's softmax, with `gradient` holding the exponentials until `total`
  // is known.
  const auto [peak, total] = exponentiate_row(gradient, logits, row, columns);

  for (const auto col : columns) {
    const auto probability = widen(gradient[row, col]) / total;
    const auto target = (col == label) ? compute_t<T>{1} : compute_t<T>{0};
    gradient[row, col] = narrow<T>((probability - target) / label_count);
  }

  // The label's probability is `exp(label_logit - peak) / total`, so its
  // negated logarithm is taken without forming it. In other words, we
  // deliberately do not calculate the probability and then take its logarithm,
  // because that would round the probability.
  if (cuda_kernel::x_thread() == 0)
    losses[row] = narrow<T>(std::log(total) - (label_logit - peak));
}

} // namespace details

// Compute the cross-entropy loss of each row of `logits` against its label,
// writing into `losses`, and the gradient of the mean loss with respect to
// every logit, writing into `gradient`.
//
// A row of logits is a prediction, one score per vocabulary entry, and its
// label is the entry that was in fact right, as a `token_id`. ("Label" alludes
// to labeled data. It's a holdover from ML classification models, where it was
// literally the label hand-written to describe, say, an image input.)
//
// With P predictions and V vocabulary entries:
//
//   gradient  [P, V]  how much the mean loss moves per unit of each logit,
//                     written
//   losses    [P]     each row's loss, written
//   logits    [P, V]  a row per prediction
//   labels    [P]     each row's label
//
// The softmax turns a row's logits into probabilities, and the row's loss is
// the negated logarithm of the probability its label got. A label given a
// probability of 1 costs nothing, and the cost grows without bound as that
// probability nears 0.
//
// For a row of three logits whose label is entry 0:
//
//   token_id       0       1       2
//   logits         2       1       0
//   exp            7.389   2.718   1       totaling 11.107
//   probabilities  0.665   0.245   0.090   each `exp` over the total
//   loss           -log(0.665) = 0.408
//
// The gradient answers one question per logit. Were this logit a little
// larger, by how many times that little would the loss change? The answer is
// the row's probabilities, less 1 at the label:
//
//   gradient       -0.335  0.245   0.090
//
// So raising logit 1 by 0.01 raises the loss by about 0.245 * 0.01, and
// raising the label's logit by 0.01 lowers the loss by about 0.335 * 0.01.
// Every wrong entry pushes the loss up in proportion to the probability it
// took, and the label pulls it down by the probability it did not get.
//
// The reason is in the loss written out, `log(total) - logits[label]`.
// Raising a logit by a small `step` multiplies its `exp` by about `1 + step`,
// which adds `step` times that `exp` to the total, and the logarithm of the
// total rises by that addition over the total, which is `step` times the
// logit's probability. For every logit but the label's, that is the whole
// effect. The label's logit also appears in the second term, which falls by
// `step`.
//
// The loss that training lowers is the mean of the rows' losses, over
// `label_count` rows, so each row's gradient is divided by `label_count`. A
// caller that spreads one batch over several calls passes the batch's count
// to each, so `label_count` may exceed P. A batch of 4 sequences run as 4
// calls of 63 predictions each passes 4 * 63 = 252 to all four. The losses
// are not divided.
//
// For `bfloat16_t`, each gradient is rounded twice, since its exponential is
// stored in `gradient` before the division by the total reads it back. The
// losses are not affected, and `float` and `double` are exact.
//
// `gradient` must have the extent of `logits`, with at least one column, and
// can be `logits` itself, replacing each logit with its gradient, but must
// not otherwise overlap it. `losses` and `labels` must have an element per
// row, every label must index a column, and `label_count` must not be zero.
//
// Returns false when the launch is refused, leaving `gradient` and `losses`
// unspecified.
template<DeviceMatrixLike Out>
requires DeviceFloating<device_element_t<Out>>
[[nodiscard]] bool cross_entropy(Out&& gradient,
    cuda_buffer<device_element_t<Out>>& losses, input_view_t<Out> logits,
    const cuda_buffer<token_id>& labels, size_t label_count) {
  const auto& gradient_lens = gradient.as_lens();
  assert(gradient_lens.extent() == logits.extent());
  assert(logits.col_extent() > 0);
  assert((losses.size() == logits.row_extent()) &&
         (labels.size() == logits.row_extent()));
  assert(label_count);
  assert(is_same_or_disjoint(gradient_lens.as_span(), logits.as_span()));

  details::apply_cross_entropy<<<logits.row_extent(), threads_per_block>>>(
      kernel_matrix_lens{gradient_lens}, losses.get(),
      kernel_matrix_view{logits}, labels.get(), logits.col_extent(),
      static_cast<compute_t<device_element_t<Out>>>(label_count));
  return cuda_last_status{}.ok();
}

#pragma endregion

} // namespace corvid::cuda::llm
