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
#include <array>
#include <cmath>
#include <span>
#include <vector>

#include "corvid/linalg/linear_algebra.h"
#include "catch2_main.h"
#include "catch2/catch_template_test_macros.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"

using namespace corvid;
using namespace corvid::linalg;
using Catch::Matchers::WithinAbs;

using row_ndx = float_matrix_view::row_ndx;
using col_ndx = float_matrix_view::col_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEMPLATE_TEST_CASE("Row reductions", "[LinearAlgebraTest]", float, double) {
  using T = TestType;
  constexpr std::array values{T{1}, T{2}, T{3}, T{4}};
  static_assert(sum(values) == T{10});
  static_assert(mean(values) == T{2.5});
  static_assert(squared_deviation_sum(values, T{2.5}) == T{5});
  static_assert(population_variance(values, T{2.5}) == T{1.25});
  static_assert(sample_variance(values, T{2.5}) == T{5} / T{3});
  constexpr std::array flat{T{5}, T{5}};
  static_assert(population_variance(flat, T{5}) == T{0});
  static_assert(sum(std::span<const T>{}) == T{0});
  CHECK(std::isnan(mean(std::span<const T>{})));
  CHECK(std::isnan(population_variance(std::span<const T>{}, T{0})));
  constexpr std::array one{T{5}};
  CHECK(std::isnan(sample_variance(one, T{5})));
  CHECK_THAT(std_dev(values, T{2.5}, T{0}), WithinAbs(std::sqrt(1.25), 1e-6));
  CHECK_THAT(std_dev(flat, T{5}, T{0.25}), WithinAbs(0.5, 1e-6));
  CHECK_THAT(inverse_std_dev(values, T{2.5}, T{0}),
      WithinAbs(1.0 / std::sqrt(1.25), 1e-6));
  CHECK(std::isinf(inverse_std_dev(flat, T{5}, T{0})));
  CHECK_THAT(inverse_std_dev(flat, T{5}, T{0.25}), WithinAbs(2.0, 1e-6));
}

TEMPLATE_TEST_CASE("Elementwise steps", "[LinearAlgebraTest]", float, double) {
  using T = TestType;
  static_assert(standardize(T{4}, T{2.5}, T{2}) == T{3});
  static_assert(standardize(T{2.5}, T{2.5}, T{2}) == T{0});
  static_assert(scale_shift(T{3}, T{2}, T{0.5}) == T{6.5});
}

TEMPLATE_TEST_CASE("Add scaled", "[LinearAlgebraTest]", float, double) {
  using T = TestType;
  std::array acc{T{1}, T{2}, T{3}};
  constexpr std::array values{T{10}, T{20}, T{30}};
  add_scaled(acc, T{0.5}, values);
  CHECK(acc == std::array{T{6}, T{12}, T{18}});
}

TEMPLATE_TEST_CASE("Linear projection on hand-computed rows",
    "[LinearAlgebraTest]", float, double) {
  using T = TestType;
  // Two rows of three features through a three-by-two weight: the first
  // output column sums features 0 and 2, the second sums features 1 and 2,
  // and each gets its bias.
  const std::vector<T> in_storage{T{1}, T{2}, T{3}, T{4}, T{5}, T{6}};
  const std::vector<T> weight_storage{T{1}, T{0}, T{0}, T{1}, T{1}, T{1}};
  constexpr std::array bias{T{10}, T{20}};
  const matrix_view<const T> in(in_storage, {.row_count = 2, .col_count = 3});
  const matrix_view<const T> weight(weight_storage,
      {.row_count = 3, .col_count = 2});

  // The output lands in the middle two columns of a wider buffer, the way a
  // projection fills one block of columns and leaves the rest alone.
  std::vector<T> out_storage(2UZ * 4, T{-1});
  const auto out =
      matrix_view<T>(out_storage, {.row_count = 2, .col_count = 4})
          .subview({row_ndx{0}, col_ndx{1}}, {.row_count = 2, .col_count = 2});

  linear_projection(out, in, weight, bias);

  CHECK(
      out_storage ==
      std::vector<T>{T{-1}, T{14}, T{25}, T{-1}, T{-1}, T{20}, T{31}, T{-1}});
}

TEMPLATE_TEST_CASE("Dot product", "[LinearAlgebraTest]", float, double) {
  using T = TestType;
  constexpr std::array a{T{1}, T{2}, T{3}};
  constexpr std::array b{T{4}, T{5}, T{6}};
  static_assert(dot_product(a, b) == T{32});
  static_assert(
      dot_product(std::span<const T>{}, std::span<const T>{}) == T{0});
}

TEMPLATE_TEST_CASE("Softmax", "[LinearAlgebraTest]", float, double) {
  using T = TestType;
  constexpr auto tolerance = 1e-5;

  // Scores 0 and 0.707 differ by 0.707, so the second gets twice the weight:
  // exp(0.707) is 2.028 times exp(0).
  std::array scores{T{0}, static_cast<T>(0.707)};
  softmax(scores, scores);
  CHECK_THAT(scores[0], WithinAbs(0.330262, tolerance));
  CHECK_THAT(scores[1], WithinAbs(0.669738, tolerance));

  // Equal scores share equally, even when exp of the raw score would
  // overflow a float.
  std::array huge{T{1000}, T{1000}, T{1000}, T{1000}};
  std::array weights{T{0}, T{0}, T{0}, T{0}};
  softmax(weights, huge);
  CHECK(weights == std::array{T{0.25}, T{0.25}, T{0.25}, T{0.25}});

  // A single score gets all the weight.
  std::array one{T{-3}};
  softmax(one, one);
  CHECK(one[0] == T{1});
}

TEMPLATE_TEST_CASE("Add on hand-computed rows", "[LinearAlgebraTest]", float,
    double) {
  using T = TestType;
  std::vector<T> a_storage{T{1}, T{2}, T{3}, T{4}};
  std::vector<T> b_storage{T{10}, T{20}, T{30}, T{40}};
  const matrix_view<T> a(a_storage, {.row_count = 2, .col_count = 2});
  const matrix_view<T> b(b_storage, {.row_count = 2, .col_count = 2});
  const std::vector<T> expected{T{11}, T{22}, T{33}, T{44}};

  SECTION("into a separate matrix") {
    std::vector<T> storage(a.size());
    const matrix_view<T> out(storage, a.extent());
    add(out, a, b);
    CHECK(storage == expected);
  }

  SECTION("in place on the left") {
    add(a, a, b);
    CHECK(a_storage == expected);
  }

  SECTION("in place on the right") {
    add(b, a, b);
    CHECK(b_storage == expected);
  }
}

TEMPLATE_TEST_CASE("Subtract on hand-computed rows", "[LinearAlgebraTest]",
    float, double) {
  using T = TestType;
  std::vector<T> a_storage{T{11}, T{22}, T{33}, T{44}};
  std::vector<T> b_storage{T{1}, T{2}, T{3}, T{4}};
  const matrix_view<T> a(a_storage, {.row_count = 2, .col_count = 2});
  const matrix_view<T> b(b_storage, {.row_count = 2, .col_count = 2});
  const std::vector<T> expected{T{10}, T{20}, T{30}, T{40}};

  SECTION("into a separate matrix") {
    std::vector<T> storage(a.size());
    const matrix_view<T> out(storage, a.extent());
    subtract(out, a, b);
    CHECK(storage == expected);
  }

  SECTION("in place on the left") {
    subtract(a, a, b);
    CHECK(a_storage == expected);
  }

  SECTION("in place on the right") {
    subtract(b, a, b);
    CHECK(b_storage == expected);
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
