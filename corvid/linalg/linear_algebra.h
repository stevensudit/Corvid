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
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>

#include "../containers/utils/matrix_view.h"
#include "../meta/concepts.h"
#include "../meta/containers.h"
#include "../meta/crossplatform.h"

// Arithmetic over rows and matrices, one free function per op.
//
// The row ops take any contiguous range of numbers, while the matrix ops
// take `matrix_lens`. Every op is a template on the element type, and each
// constrains it to what its arithmetic needs: the sums and products accept
// any `Arithmetic` type, while the ops that divide or take a root require a
// `Floating` one.
//
// Every op writes into a caller-owned range or lens and honors the stride of
// every lens or view it is given. Shape mismatches are contract violations.
// The set is what the consumers need, not a comprehensive statistics library,
// so an op arrives with its first caller.
namespace corvid::linalg {

// The loop idiom of this file.
using std::views::zip;

#pragma region Types

// A view of the element type an op deduced from its output.
//
// `std::type_identity_t` keeps it out of deduction, so a lens passed as an
// input converts to it.
template<typename T>
using matrix_view_t = matrix_view<std::type_identity_t<T>>;

#pragma endregion
#pragma region Reductions

// Reductions over a row.

// The sum of `values`.
template<ArithmeticRange R>
[[nodiscard]] constexpr element_of_t<R> sum(const R& values) noexcept {
  element_of_t<R> total{};
  for (const auto x : values) total += x;
  return total;
}

// The arithmetic mean of `values`, NaN when empty.
//
// cl C4723: Division by zero is intentionally allowed.
PRAGMA_DIAG(push)
PRAGMA_MSVC_IGNORED(4723)
template<FloatingRange R>
[[nodiscard]] constexpr element_of_t<R> mean(const R& values) noexcept {
  using T = element_of_t<R>;
  return sum(values) / static_cast<T>(std::ranges::size(values));
}
PRAGMA_DIAG(pop)

// The sum of the squared deviations of `values` from `center`.
template<ArithmeticRange R>
[[nodiscard]] constexpr element_of_t<R>
squared_deviation_sum(const R& values, element_of_t<R> center) noexcept {
  element_of_t<R> total{};
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
template<FloatingRange R>
[[nodiscard]] constexpr element_of_t<R>
population_variance(const R& values, element_of_t<R> mean) noexcept {
  using T = element_of_t<R>;
  const auto size = static_cast<T>(std::ranges::size(values));
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
template<FloatingRange R>
[[nodiscard]] constexpr element_of_t<R>
sample_variance(const R& values, element_of_t<R> mean) noexcept {
  using T = element_of_t<R>;
  const auto size = static_cast<T>(std::ranges::size(values));
  return squared_deviation_sum(values, mean) / (size - T{1});
}
PRAGMA_DIAG(pop)

// The population standard deviation of `values` around their `mean`, with
// `eps` added to the variance inside the square root.
template<FloatingRange R>
[[nodiscard]] element_of_t<R>
std_dev(const R& values, element_of_t<R> mean, element_of_t<R> eps) noexcept {
  return std::sqrt(population_variance(values, mean) + eps);
}

// The reciprocal of `std_dev`.
template<FloatingRange R>
[[nodiscard]] element_of_t<R> inverse_std_dev(const R& values,
    element_of_t<R> mean, element_of_t<R> eps) noexcept {
  using T = element_of_t<R>;
  return T{1} / std_dev(values, mean, eps);
}

// The dot product of `a` and `b`, which must be the same size.
template<ArithmeticRange A, ArithmeticRange B>
requires SameElement<A, B>
[[nodiscard]] constexpr element_of_t<A>
dot_product(const A& a, const B& b) noexcept {
  // In IEEE order the sum is one serial chain of fused multiply-adds, 8x
  // slower on a 768-wide row than the vectorized reduction this permits.
  PRAGMA_FP_REASSOCIATE
  assert(std::ranges::size(a) == std::ranges::size(b));

  element_of_t<A> total{};
  for (const auto [x, y] : zip(a, b)) total += x * y;
  return total;
}

#pragma endregion
#pragma region Elementwise

// The z-score of `x`, which is its distance from `mean` in units of the
// standard deviation (which is the reciprocal of `inv_std`).
template<Floating T>
[[nodiscard]] constexpr CUDA_HOST_DEVICE T standardize(T x, T mean,
    T inv_std) noexcept {
  return (x - mean) * inv_std;
}

// `x` scaled by `weight`, then shifted by `bias`.
template<Arithmetic T>
[[nodiscard]] constexpr CUDA_HOST_DEVICE T scale_shift(T x, T weight,
    T bias) noexcept {
  return (x * weight) + bias;
}

// Add `scale` times each of `values` to the matching element of `acc`.
// Conceptually: `acc += scale * values`
//
// BLAS calls this operation "axpy", because it's "a times x plus y".
template<ArithmeticRange A, ArithmeticRange V>
requires MutableRange<A> && SameElement<A, V>
constexpr void
add_scaled(A& acc, element_of_t<A> scale, const V& values) noexcept {
  assert(std::ranges::size(acc) == std::ranges::size(values));

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
template<Arithmetic T, ArithmeticRange B>
requires std::same_as<element_of_t<B>, T>
void linear_projection(matrix_lens<T> out, matrix_view_t<T> in,
    matrix_view_t<T> weight, const B& bias) noexcept {
  assert(weight.row_extent() == in.col_extent());
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == weight.col_extent()));
  assert(std::ranges::size(bias) == weight.col_extent());
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
template<FloatingRange O, FloatingRange I>
requires MutableRange<O> && SameElement<O, I>
void softmax(O& span_out, const I& span_in) noexcept {
  assert(std::ranges::size(span_out) == std::ranges::size(span_in));
  assert(is_same_or_disjoint(span_out, span_in));
  assert(!std::ranges::empty(span_in));

  // Shifting every value by the same amount leaves the weights unchanged, and
  // shifting by the maximum keeps `exp` at or below 1.
  const auto peak = std::ranges::max(span_in);
  element_of_t<O> total{};
  for (auto [out_value, in_value] : zip(span_out, span_in)) {
    out_value = std::exp(in_value - peak);
    total += out_value;
  }
  for (auto& weight : span_out) weight /= total;
}

#pragma endregion
#pragma region add

// Add `a` and `b` elementwise, writing into `out`.
//
// All three views must have the same extent. `out` can be the same view as
// `a` or as `b`, adding in place, but must not otherwise overlap either.
template<Arithmetic T>
void add(matrix_lens<T> out, matrix_view_t<T> a, matrix_view_t<T> b) noexcept {
  assert(a.extent() == b.extent());
  assert(out.extent() == a.extent());
  assert(is_same_or_disjoint(out.as_span(), a.as_span()));
  assert(is_same_or_disjoint(out.as_span(), b.as_span()));

  for (const auto [out_row, a_row, b_row] :
      zip(out.rows(), a.rows(), b.rows()))
    for (auto [out_value, a_value, b_value] : zip(out_row, a_row, b_row))
      out_value = a_value + b_value;
}

#pragma endregion
#pragma region subtract

// Subtract `b` from `a` elementwise, writing into `out`.
//
// All three views must have the same extent. `out` can be the same view as
// `a` or as `b`, subtracting in place, but must not otherwise overlap either.
template<Arithmetic T>
void subtract(matrix_lens<T> out, matrix_view_t<T> a,
    matrix_view_t<T> b) noexcept {
  assert(a.extent() == b.extent());
  assert(out.extent() == a.extent());
  assert(is_same_or_disjoint(out.as_span(), a.as_span()));
  assert(is_same_or_disjoint(out.as_span(), b.as_span()));

  for (const auto [out_row, a_row, b_row] :
      zip(out.rows(), a.rows(), b.rows()))
    for (auto [out_value, a_value, b_value] : zip(out_row, a_row, b_row))
      out_value = a_value - b_value;
}

#pragma endregion

} // namespace corvid::linalg
