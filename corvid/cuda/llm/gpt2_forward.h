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
#include <numbers>
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
//
// BLAS calls this operation BLAS axpy, because it's "a times x plus y".
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

// Project each row of `in` through `weight` and add `bias`, into `out`.
//
// Each row of `in` holds the input features of one token, and the same row of
// `out` receives that token's output features. However, the number of features
// for a token may differ on the `in` and `out` sides, with the actual values
// depending on which projection is being applied:
//
// call         |  in features (i)  |  out features (j)  |  factor
// -------------+-------------------+--------------------+---------
// c_attn       |       768         |      2304          |    3
// attn c_proj  |       768         |       768          |    1
// c_fc         |       768         |      3072          |    4
// mlp c_proj   |       3072        |       768          |    1/4
//
// Every output value is a weighted sum of the token's input features, plus a
// bias, with the weights for output feature `j` in column `j` of `weight`:
//
//   out[t][j] = bias[j] + sum over i of in[t][i] * weight[i][j]
//
// The three indices each play one part:
//
//   t  token           row of `in`, row of `out`
//   i  input feature   column of `in`, row of `weight`
//   j  output feature  column of `weight`, column of `out`, index of `bias`
//
// The summed index, `i`, is the one the operands must agree on, so `in` has as
// many columns as `weight` has rows. The surviving indices label `out`, which
// has a row per token and a column per output feature. In matrix terms, this
// is: `out = in * weight + bias`, with `bias` added to every row.
//
// Consider a column `j` of `weight` as a direction. The value `out[t][j]` is
// the dot product of the token's feature vector with that direction, plus a
// bias. When the two vectors point the same way, the dot product is large and
// positive. So each output feature answers one question: How much does this
// token resemble learned pattern `j`?
//
// `weight` is laid out as GPT-2 stores its projections (Conv1D): one row per
// input feature and one column per output feature. That is the transpose of
// nn.Linear, which keeps one row per output feature.
//
// `out` must not overlap `in`, `weight`, or `bias`.
inline void linear(float_matrix_view out, const_float_matrix_view in,
    const_float_matrix_view weight, const_float_row_span bias) noexcept {
  assert(weight.row_extent() == in.col_extent());
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == weight.col_extent()));
  assert(bias.size() == weight.col_extent());
  assert(is_disjoint(out.as_span(), in.as_span()));
  assert(is_disjoint(out.as_span(), weight.as_span()) &&
         is_disjoint(out.as_span(), bias));

  // Loop over each row of the input matrix, which corresponds to a token, and
  // in which each column contains a feature.
  for (const auto token_row_ndx : in.row_interval()) {
    // The input features for a given token.
    const auto in_row = in[token_row_ndx];
    // The same input features but viewed as a column vector, so that it can
    // use the same index as the weight.
    const auto in_as_col = const_float_col_span(in_row);
    // Will hold the projected features for the same token.
    const auto out_row = out[token_row_ndx];

    // We start by adding in the biases for this token.
    std::ranges::copy(bias, out_row.begin());

    // We then sum up the scaled contributions of each input feature to the
    // output features.
    for (const auto feature_row_ndx : weight.row_interval())
      add_scaled(out_row, in_as_col[feature_row_ndx], weight[feature_row_ndx]);
  }
}

#pragma endregion
#pragma region gelu_new

// The `sqrt(2 / pi)` that scales the argument of the tanh.
inline constexpr float gelu_tanh_scale =
    std::numbers::sqrt2_v<float> * std::numbers::inv_sqrtpi_v<float>;

// The coefficient of the cubic term inside the tanh, from the original
// paper's fit of the tanh form to the exact one.
inline constexpr float gelu_cubic_coeff = 0.044715F;

// The GELU (Gaussian Error Linear Unit) of `x`, in the tanh form that GPT-2
// was trained with.
//
// GELU is `x` times the probability that a standard normal draw is below `x`.
// So it passes large positive inputs through unchanged, squashes large
// negative ones to zero, and bends smoothly between the two, dipping a little
// below zero on the way. The exact probability needs `erf`. This form
// replaces it with a tanh of a cubic, which is close but not identical:
//
//   0.5 * x * (1 + tanh(sqrt(2 / pi) * (x + 0.044715 * x^3)))
//
// The weights were fit to this curve, so this is the form that matches the
// oracle, and the `erf` form would not. Not `constexpr` because it uses
// `std::tanh`.
[[nodiscard]] inline float gelu_new(float x) noexcept {
  const auto inner = gelu_tanh_scale * (x + (gelu_cubic_coeff * x * x * x));
  return 0.5F * x * (1.0F + std::tanh(inner));
}

// Apply `gelu_new` to every element of `in`, into `out`.
//
// GPT-2 applies it once per block, to the 3072-wide rows that `c_fc`
// produces, before `mlp c_proj` projects them back down to 768. It is the
// only nonlinearity in the MLP, and without it the two projections would
// collapse into one.
//
// `out` and `in` must have the same extent. `out` can be the same view as
// `in`, applying it in place, but must not otherwise overlap it.
inline void
gelu_new(float_matrix_view out, const_float_matrix_view in) noexcept {
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == in.col_extent()));
  assert(is_same_or_disjoint(out.as_span(), in.as_span()));

  for (const auto r : in.row_interval()) {
    const auto out_row = out.row_as_span(r);
    const auto in_row = in.row_as_span(r);
    for (const auto c : in.col_interval()) out_row[c] = gelu_new(in_row[c]);
  }
}

#pragma endregion
} // namespace corvid::llm
