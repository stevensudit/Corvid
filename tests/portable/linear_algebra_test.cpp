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
#include "catch2/matchers/catch_matchers_floating_point.hpp"

using namespace corvid;
using namespace corvid::linalg;
using Catch::Matchers::WithinAbs;

using row_ndx = float_matrix_view::row_ndx;
using col_ndx = float_matrix_view::col_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Row reductions", "[LinearAlgebraTest]") {
  constexpr std::array values{1.0F, 2.0F, 3.0F, 4.0F};
  static_assert(sum(values) == 10.0F);
  static_assert(mean(values) == 2.5F);
  static_assert(squared_deviation_sum(values, 2.5F) == 5.0F);
  static_assert(population_variance(values, 2.5F) == 1.25F);
  static_assert(sample_variance(values, 2.5F) == 5.0F / 3.0F);
  constexpr std::array flat{5.0F, 5.0F};
  static_assert(population_variance(flat, 5.0F) == 0.0F);
  static_assert(sum(std::span<const float>{}) == 0.0F);
  CHECK(std::isnan(mean(std::span<const float>{})));
  CHECK(std::isnan(population_variance(std::span<const float>{}, 0.0F)));
  constexpr std::array one{5.0F};
  CHECK(std::isnan(sample_variance(one, 5.0F)));
  CHECK_THAT(std_dev(values, 2.5F, 0.0F), WithinAbs(std::sqrt(1.25), 1e-6));
  CHECK_THAT(std_dev(flat, 5.0F, 0.25F), WithinAbs(0.5, 1e-6));
  CHECK_THAT(inverse_std_dev(values, 2.5F, 0.0F),
      WithinAbs(1.0 / std::sqrt(1.25), 1e-6));
  CHECK(std::isinf(inverse_std_dev(flat, 5.0F, 0.0F)));
  CHECK_THAT(inverse_std_dev(flat, 5.0F, 0.25F), WithinAbs(2.0, 1e-6));
}

TEST_CASE("Elementwise steps", "[LinearAlgebraTest]") {
  static_assert(standardize(4.0F, 2.5F, 2.0F) == 3.0F);
  static_assert(standardize(2.5F, 2.5F, 2.0F) == 0.0F);
  static_assert(scale_shift(3.0F, 2.0F, 0.5F) == 6.5F);
}

TEST_CASE("Add scaled", "[LinearAlgebraTest]") {
  std::array acc{1.0F, 2.0F, 3.0F};
  constexpr std::array values{10.0F, 20.0F, 30.0F};
  add_scaled(acc, 0.5F, values);
  CHECK(acc == std::array{6.0F, 12.0F, 18.0F});
}

TEST_CASE("Linear projection on hand-computed rows", "[LinearAlgebraTest]") {
  // Two rows of three features through a three-by-two weight: the first
  // output column sums features 0 and 2, the second sums features 1 and 2,
  // and each gets its bias.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
  const std::vector<float> weight_storage{1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
  constexpr std::array bias{10.0F, 20.0F};
  const const_float_matrix_view in(in_storage,
      {.row_count = 2, .col_count = 3});
  const const_float_matrix_view weight(weight_storage,
      {.row_count = 3, .col_count = 2});

  // The output lands in the middle two columns of a wider buffer, the way a
  // projection fills one block of columns and leaves the rest alone.
  std::vector<float> out_storage(2UZ * 4, -1.0F);
  const auto out =
      float_matrix_view(out_storage, {.row_count = 2, .col_count = 4})
          .subview({row_ndx{0}, col_ndx{1}}, {.row_count = 2, .col_count = 2});

  linear_projection(out, in, weight, bias);

  CHECK(out_storage ==
        std::vector<float>{-1.0F, 14.0F, 25.0F, -1.0F, -1.0F, 20.0F, 31.0F,
            -1.0F});
}

TEST_CASE("Dot product", "[LinearAlgebraTest]") {
  constexpr std::array a{1.0F, 2.0F, 3.0F};
  constexpr std::array b{4.0F, 5.0F, 6.0F};
  static_assert(dot_product(a, b) == 32.0F);
  static_assert(
      dot_product(std::span<const float>{}, std::span<const float>{}) == 0.0F);
}

TEST_CASE("Softmax row", "[LinearAlgebraTest]") {
  constexpr auto tolerance = 1e-5;

  // Scores 0 and 0.707 differ by 0.707, so the second gets twice the weight:
  // exp(0.707) is 2.028 times exp(0).
  std::array scores{0.0F, 0.707F};
  softmax(scores, scores);
  CHECK_THAT(scores[0], WithinAbs(0.330262, tolerance));
  CHECK_THAT(scores[1], WithinAbs(0.669738, tolerance));

  // Equal scores share equally, even when exp of the raw score would
  // overflow a float.
  std::array huge{1000.0F, 1000.0F, 1000.0F, 1000.0F};
  std::array weights{0.0F, 0.0F, 0.0F, 0.0F};
  softmax(weights, huge);
  CHECK(weights == std::array{0.25F, 0.25F, 0.25F, 0.25F});

  // A single score gets all the weight.
  std::array one{-3.0F};
  softmax(one, one);
  CHECK(one[0] == 1.0F);
}

TEST_CASE("Add on hand-computed rows", "[LinearAlgebraTest]") {
  std::vector<float> a_storage{1.0F, 2.0F, 3.0F, 4.0F};
  std::vector<float> b_storage{10.0F, 20.0F, 30.0F, 40.0F};
  const float_matrix_view a(a_storage, {.row_count = 2, .col_count = 2});
  const float_matrix_view b(b_storage, {.row_count = 2, .col_count = 2});
  const std::vector<float> expected{11.0F, 22.0F, 33.0F, 44.0F};

  SECTION("into a separate matrix") {
    std::vector<float> storage(a.size());
    const float_matrix_view out(storage, a.extent());
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

TEST_CASE("Subtract on hand-computed rows", "[LinearAlgebraTest]") {
  std::vector<float> a_storage{11.0F, 22.0F, 33.0F, 44.0F};
  std::vector<float> b_storage{1.0F, 2.0F, 3.0F, 4.0F};
  const float_matrix_view a(a_storage, {.row_count = 2, .col_count = 2});
  const float_matrix_view b(b_storage, {.row_count = 2, .col_count = 2});
  const std::vector<float> expected{10.0F, 20.0F, 30.0F, 40.0F};

  SECTION("into a separate matrix") {
    std::vector<float> storage(a.size());
    const float_matrix_view out(storage, a.extent());
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
