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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>

#include "../../containers/utils/matrix_view.h"
#include "../../meta/containers.h"

// The GPT-2 forward pass on the CPU, in fp32, one free function per op.
//
// Every op writes into a caller-owned output view. Activations are
// `matrix_view` rows of features, one row per token, and parameters are
// spans over the weight file. Shape mismatches are contract violations
// (asserted).
namespace corvid::llm {

#pragma region Reductions

// Reductions over a row of features, kept here until a second consumer earns
// them a home in `corvid/math`.

// The sum of `values`.
[[nodiscard]] constexpr float sum(const_float_span values) noexcept {
  float total{};
  for (const auto x : values) total += x;
  return total;
}

// The arithmetic mean of `values`, NaN when empty.
[[nodiscard]] constexpr float mean(const_float_span values) noexcept {
  return sum(values) / static_cast<float>(values.size());
}

// The sum of the squared deviations of `values` from `center`.
[[nodiscard]] constexpr float
squared_deviation_sum(const_float_span values, float center) noexcept {
  float total{};
  for (const auto x : values) {
    const auto deviation = x - center;
    total += deviation * deviation;
  }
  return total;
}

// The biased variance of `values` around their `mean`. It's biased because the
// squared deviations are divided by the count, not the count minus one (in
// other words, Bessel's correction is not applied). NaN when empty.
[[nodiscard]] constexpr float
variance(const_float_span values, float mean) noexcept {
  return squared_deviation_sum(values, mean) /
         static_cast<float>(values.size());
}

// The reciprocal of the standard deviation of `values` around their `mean`,
// with `eps` added to the variance inside the square root.
[[nodiscard]] inline float
inverse_std_dev(const_float_span values, float mean, float eps) noexcept {
  return 1.0F / std::sqrt(variance(values, mean) + eps);
}

#pragma endregion
#pragma region Elementwise

// The z-score of `x`, which is its distance from `mean` in units of the
// standard deviation (which is the reciprocal of `inv_std`).
[[nodiscard]] constexpr float
standardize(float x, float mean, float inv_std) noexcept {
  return (x - mean) * inv_std;
}

// `x` scaled by `weight`, then shifted by `bias`.
[[nodiscard]] constexpr float
scale_shift(float x, float weight, float bias) noexcept {
  return (x * weight) + bias;
}

// Add `scale` times each of `values` to the matching element of `acc`.
// Conceptually: `acc += scale * values`
constexpr void
add_scaled(float_span acc, float scale, const_float_span values) noexcept {
  assert(acc.size() == values.size());

  for (size_t ndx = 0; ndx < acc.size(); ++ndx)
    acc[ndx] += scale * values[ndx];
}

#pragma endregion
#pragma region layer_norm

// The epsilon GPT-2 adds to the variance before the square root.
inline constexpr float layer_norm_eps = 1e-5F;

// Normalize `row_in` to a mean of 0 and a variance of 1, into `row_out`.
//
// The variance is biased (divided by the size, not the size minus one), with
// `eps` added inside the square root. `row_out` and `row_in` must be the same
// size, and can refer to the same memory.
inline void standardize_row(float_row_span row_out,
    const_float_row_span row_in, float eps) noexcept {
  assert(row_out.size() == row_in.size());
  assert(is_same_or_disjoint(row_out, row_in));

  const auto row_mean = mean(row_in);
  const auto inv_std = inverse_std_dev(row_in, row_mean, eps);
  for (const auto c : row_in.range_interval())
    row_out[c] = standardize(row_in[c], row_mean, inv_std);
}

// Scale `row_in` by `weight` and shift by `bias`, elementwise, into
// `row_out`.
//
// All four spans must be the same size, and `row_out` and `row_in` can refer
// to the same memory.
constexpr void scale_shift_row(float_row_span row_out,
    const_float_row_span row_in, const_float_row_span weight,
    const_float_row_span bias) noexcept {
  assert(row_out.size() == row_in.size());
  assert(is_same_or_disjoint(row_out, row_in));
  assert((weight.size() == row_in.size()) && (bias.size() == row_in.size()));

  for (const auto c : row_in.range_interval())
    row_out[c] = scale_shift(row_in[c], weight[c], bias[c]);
}

// Normalize each row of `in` to a mean of 0 and a variance of 1, then scale by
// `weight` and shift by `bias`, elementwise, into `out`.
//
// This is `standardize_row` followed by `scale_shift_row`, one row at a time
// so that the row is still in cache for the second step.
//
// `out` and `in` must have the same extent, and its width must be the size of
// `weight` and `bias`. `out` can be the same view as `in`, normalizing in
// place, but must not otherwise overlap it.
inline void layer_norm(float_matrix_view out, const_float_matrix_view in,
    const_float_row_span weight, const_float_row_span bias,
    float eps = layer_norm_eps) noexcept {
  [[maybe_unused]] const auto width = in.col_extent();
  assert((out.row_extent() == in.row_extent()) && (out.col_extent() == width));
  assert((weight.size() == width) && (bias.size() == width));
  assert(is_same_or_disjoint(out.as_span(), in.as_span()));
  assert(
      is_disjoint(out.as_span(), weight) && is_disjoint(out.as_span(), bias));

  for (const auto r : in.row_interval()) {
    const auto out_row = out.row_as_span(r);
    standardize_row(out_row, in.row_as_span(r), eps);
    scale_shift_row(out_row, out_row, weight, bias);
  }
}

#pragma endregion
#pragma region linear

// Project each row of `in` through `weight` and add `bias`, into `out`, so
// that `out = in * weight + bias`.
//
// `weight` is laid out as GPT-2 stores its projections: one row per input
// feature and one column per output feature. So `in` is rows by in_features,
// `weight` is in_features by out_features, and `out` and `bias` have
// out_features columns. `out` must not overlap `in`, `weight`, or `bias`
// (all asserted).
inline void linear(float_matrix_view out, const_float_matrix_view in,
    const_float_matrix_view weight, const_float_row_span bias) noexcept {
  assert(weight.row_extent() == in.col_extent());
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == weight.col_extent()));
  assert(bias.size() == weight.col_extent());
  assert(is_disjoint(out.as_span(), in.as_span()));
  assert(is_disjoint(out.as_span(), weight.as_span()) &&
         is_disjoint(out.as_span(), bias));

  for (const auto r : in.row_interval()) {
    const auto in_row = const_float_col_span(in.row_as_span(r));
    const auto out_row = out.row_as_span(r);
    std::ranges::copy(bias, out_row.begin());

    // Each input feature is a column of `in` and a row of `weight`.
    for (const auto feature : weight.row_interval())
      add_scaled(out_row, in_row[feature], weight[feature]);
  }
}

#pragma endregion
} // namespace corvid::llm
