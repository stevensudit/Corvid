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
#include <ranges>
#include <span>

#include "../containers/utils/matrix_view.h"
#include "../meta/containers.h"
#include "../meta/crossplatform.h"

// Arithmetic over rows and matrices, one free function per op.
//
// The row ops take spans, while the matrix ops take `matrix_view`.
//
// Every op writes into a caller-owned span or view and honors the stride of
// every view it is given. Shape mismatches are contract violations. The set
// is what the consumers need, not a statistics library, so an op arrives
// with its first caller.
namespace corvid::linalg {

// The loop idiom of this file.
using std::views::zip;

#pragma region Reductions

// Reductions over a row.

// The sum of `values`.
[[nodiscard]] constexpr float sum(const_float_span values) noexcept {
  float total{};
  for (const auto x : values) total += x;
  return total;
}

// The arithmetic mean of `values`, NaN when empty.
//
// cl C4723: Division by zero is intentionally allowed.
PRAGMA_DIAG(push)
PRAGMA_MSVC_IGNORED(4723)
[[nodiscard]] constexpr float mean(const_float_span values) noexcept {
  return sum(values) / static_cast<float>(values.size());
}
PRAGMA_DIAG(pop)

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

// The population variance of `values` around their `mean`, which is the
// squared deviations divided by the count. NaN when empty.
//
// cl C4723: as for `mean`.
PRAGMA_DIAG(push)
PRAGMA_MSVC_IGNORED(4723)
[[nodiscard]] constexpr float
population_variance(const_float_span values, float mean) noexcept {
  const auto size = static_cast<float>(values.size());
  return squared_deviation_sum(values, mean) / size;
}
PRAGMA_DIAG(pop)

// The sample variance of `values` around their `mean`, which is the squared
// deviations divided by the count minus one (Bessel's correction). NaN for a
// single value, and not meaningful for none.
//
// cl C4723: as for `mean`.
PRAGMA_DIAG(push)
PRAGMA_MSVC_IGNORED(4723)
[[nodiscard]] constexpr float
sample_variance(const_float_span values, float mean) noexcept {
  const auto size = static_cast<float>(values.size());
  return squared_deviation_sum(values, mean) / (size - 1.0F);
}
PRAGMA_DIAG(pop)

// The population standard deviation of `values` around their `mean`, with
// `eps` added to the variance inside the square root.
[[nodiscard]] inline float
std_dev(const_float_span values, float mean, float eps) noexcept {
  return std::sqrt(population_variance(values, mean) + eps);
}

// The reciprocal of `std_dev`.
[[nodiscard]] inline float
inverse_std_dev(const_float_span values, float mean, float eps) noexcept {
  return 1.0F / std_dev(values, mean, eps);
}

// The dot product of `a` and `b`, which must be the same size.
[[nodiscard]] constexpr float
dot_product(const_float_span a, const_float_span b) noexcept {
  // In IEEE order the sum is one serial chain of fused multiply-adds, 8x
  // slower on a 768-wide row than the vectorized reduction this permits.
  PRAGMA_FP_REASSOCIATE
  assert(a.size() == b.size());

  float total{};
  for (const auto [x, y] : zip(a, b)) total += x * y;
  return total;
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
// BLAS calls this operation "axpy", because it's "a times x plus y".
constexpr void
add_scaled(float_span acc, float scale, const_float_span values) noexcept {
  assert(acc.size() == values.size());

  for (auto [acc_value, value] : zip(acc, values)) acc_value += scale * value;
}

#pragma endregion
#pragma region linear_projection

// Project each row of `in` through `weight` and add `bias`, writing to `out`.
//
// This is an affine map applied row by row: `out = in * weight + bias` with
// `bias` added to every row, so the output width is `weight`'s column count
// and need not match the input width. `weight` has a row per input value
// and a column per output value:
//
//   out[r][j] = bias[j] + sum over i of in[r][i] * weight[i][j]
//
// The three indices each play one part:
//
//   r  row             row of `in`, row of `out`
//   i  input value     column of `in`, row of `weight`
//   j  output value    column of `weight`, column of `out`, index of `bias`
//
// The summed index, `i`, is the one the operands must agree on, so `in` has
// as many columns as `weight` has rows. The surviving indices label `out`,
// which has a row per row of `in` and a column per output value.
//
// `out` must not overlap `in`, `weight`, or `bias`.
inline void linear_projection(float_matrix_view out,
    const_float_matrix_view in, const_float_matrix_view weight,
    const_float_row_span bias) noexcept {
  assert(weight.row_extent() == in.col_extent());
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == weight.col_extent()));
  assert(bias.size() == weight.col_extent());
  assert(is_disjoint(out.as_span(), in.as_span()));
  assert(is_disjoint(out.as_span(), weight.as_span()) &&
         is_disjoint(out.as_span(), bias));

  // Each row of `in` pairs with the row of `out` that receives its projection.
  for (const auto [out_row, in_row] : zip(out.rows(), in.rows())) {
    // The bias is the row's starting value.
    std::ranges::copy(bias, out_row.begin());

    // Then each input value adds its scaled row of the weight. Input value `i`
    // pairs with row `i` of the weight.
    for (const auto [in_value, weight_row] : zip(in_row, weight.rows()))
      add_scaled(out_row, in_value, weight_row);
  }
}

#pragma endregion
#pragma region softmax

// Turn the full-range values in `span_in` into weights that sum to 1, written
// to `span_out`.
//
// Each weight is the exponential of its score divided by the sum of all the
// exponentials, so a larger score gets a larger share and the gaps between
// scores are sharpened. Large scores do not overflow.
//
// `span_out` and `span_in` must be the same size, and can refer to the same
// memory. Both must be non-empty.
inline void softmax(float_span span_out, const_float_span span_in) noexcept {
  assert(span_out.size() == span_in.size());
  assert(is_same_or_disjoint(span_out, span_in));
  assert(!span_in.empty());

  // Shifting every value by the same amount leaves the weights unchanged, and
  // shifting by the maximum keeps `exp` at or below 1.
  const auto peak = std::ranges::max(span_in);
  float total{};
  for (auto [out_value, in_value] : zip(span_out, span_in)) {
    out_value = std::exp(in_value - peak);
    total += out_value;
  }
  for (auto& weight : span_out) weight /= total;
}

#pragma endregion
#pragma region add

// Add `a` and `b` elementwise, into `out`.
//
// All three views must have the same extent. `out` can be the same view as
// `a` or as `b`, adding in place, but must not otherwise overlap either.
inline void add(float_matrix_view out, const_float_matrix_view a,
    const_float_matrix_view b) noexcept {
  assert((a.row_extent() == b.row_extent()) &&
         (a.col_extent() == b.col_extent()));
  assert((out.row_extent() == a.row_extent()) &&
         (out.col_extent() == a.col_extent()));
  assert(is_same_or_disjoint(out.as_span(), a.as_span()));
  assert(is_same_or_disjoint(out.as_span(), b.as_span()));

  for (const auto [out_row, a_row, b_row] :
      zip(out.rows(), a.rows(), b.rows()))
    for (auto [out_value, a_value, b_value] : zip(out_row, a_row, b_row))
      out_value = a_value + b_value;
}

#pragma endregion
#pragma region subtract

// Subtract `b` from `a` elementwise, into `out`.
//
// All three views must have the same extent. `out` can be the same view as
// `a` or as `b`, subtracting in place, but must not otherwise overlap either.
inline void subtract(float_matrix_view out, const_float_matrix_view a,
    const_float_matrix_view b) noexcept {
  assert((a.row_extent() == b.row_extent()) &&
         (a.col_extent() == b.col_extent()));
  assert((out.row_extent() == a.row_extent()) &&
         (out.col_extent() == a.col_extent()));
  assert(is_same_or_disjoint(out.as_span(), a.as_span()));
  assert(is_same_or_disjoint(out.as_span(), b.as_span()));

  for (const auto [out_row, a_row, b_row] :
      zip(out.rows(), a.rows(), b.rows()))
    for (auto [out_value, a_value, b_value] : zip(out_row, a_row, b_row))
      out_value = a_value - b_value;
}

#pragma endregion

} // namespace corvid::linalg
