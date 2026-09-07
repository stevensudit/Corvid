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
#include <cstddef>
#include <vector>

#include "corvid/cuda/llm/gpt2_forward.h"
#include "catch2_main.h"
#include "catch2/matchers/catch_matchers_floating_point.hpp"

using namespace corvid;
using namespace corvid::llm;
using Catch::Matchers::WithinAbs;

using row_ndx = float_matrix_view::row_ndx;
using col_ndx = float_matrix_view::col_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Row reductions", "[Gpt2ForwardTest]") {
  constexpr std::array values{1.0F, 2.0F, 3.0F, 4.0F};
  static_assert(sum(values) == 10.0F);
  static_assert(mean(values) == 2.5F);
  static_assert(squared_deviation_sum(values, 2.5F) == 5.0F);
  static_assert(variance(values, 2.5F) == 1.25F);
  constexpr std::array flat{5.0F, 5.0F};
  static_assert(variance(flat, 5.0F) == 0.0F);
  static_assert(sum(std::span<const float>{}) == 0.0F);
  CHECK(std::isnan(mean(std::span<const float>{})));
  CHECK(std::isnan(variance(std::span<const float>{}, 0.0F)));
  CHECK_THAT(inverse_std_dev(values, 2.5F, 0.0F),
      WithinAbs(1.0 / std::sqrt(1.25), 1e-6));
  CHECK(std::isinf(inverse_std_dev(flat, 5.0F, 0.0F)));
  CHECK_THAT(inverse_std_dev(flat, 5.0F, 0.25F), WithinAbs(2.0, 1e-6));
}

TEST_CASE("Elementwise steps", "[Gpt2ForwardTest]") {
  static_assert(standardize(4.0F, 2.5F, 2.0F) == 3.0F);
  static_assert(standardize(2.5F, 2.5F, 2.0F) == 0.0F);
  static_assert(scale_shift(3.0F, 2.0F, 0.5F) == 6.5F);
}

TEST_CASE("Layer norm on hand-computed rows", "[Gpt2ForwardTest]") {
  // Row 0 has mean 2.5 and biased variance 1.25; row 1 is constant, so it
  // normalizes to zero and the output is the bias alone.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 5.0F, 5.0F,
      5.0F};
  std::vector<float> out_storage(in_storage.size());
  const const_float_matrix_view in(in_storage,
      {.row_count = 2, .col_count = 4});
  const float_matrix_view out(out_storage, {.row_count = 2, .col_count = 4});
  constexpr std::array weight{1.0F, 2.0F, 1.0F, 2.0F};
  constexpr std::array bias{0.0F, 0.0F, 0.5F, 0.5F};

  layer_norm(out, in, weight, bias);

  constexpr auto tolerance = 1e-5;
  const auto inv_std = 1.0F / std::sqrt(1.25F + layer_norm_eps);
  const auto first = out.row_span(row_ndx{0});
  CHECK_THAT(first[0], WithinAbs(-1.5F * inv_std, tolerance));
  CHECK_THAT(first[1], WithinAbs(-0.5F * inv_std * 2.0F, tolerance));
  CHECK_THAT(first[2], WithinAbs((0.5F * inv_std) + 0.5F, tolerance));
  CHECK_THAT(first[3], WithinAbs((1.5F * inv_std * 2.0F) + 0.5F, tolerance));
  const auto second = out.row_span(row_ndx{1});
  CHECK_THAT(second[0], WithinAbs(0.0, tolerance));
  CHECK_THAT(second[1], WithinAbs(0.0, tolerance));
  CHECK_THAT(second[2], WithinAbs(0.5, tolerance));
  CHECK_THAT(second[3], WithinAbs(0.5, tolerance));
}

TEST_CASE("Layer norm in place", "[Gpt2ForwardTest]") {
  std::vector<float> storage{1.0F, 2.0F, 3.0F, 4.0F};
  std::vector<float> expected(storage.size());
  const float_matrix_view m(storage, {.row_count = 1, .col_count = 4});
  const float_matrix_view out(expected, {.row_count = 1, .col_count = 4});
  constexpr std::array weight{1.0F, 2.0F, 1.0F, 2.0F};
  constexpr std::array bias{0.0F, 0.0F, 0.5F, 0.5F};

  layer_norm(out, m, weight, bias);
  layer_norm(m, m, weight, bias);

  CHECK(storage == expected);
}

TEST_CASE("Layer norm honors the stride of both views", "[Gpt2ForwardTest]") {
  // The row lives in the first two columns of a three-column buffer, and
  // writes land in the last two columns of another, leaving the rest alone.
  const std::vector<float> in_storage{2.0F, 4.0F, -1.0F};
  std::vector<float> out_storage{-1.0F, -1.0F, -1.0F};
  const auto in =
      const_float_matrix_view(in_storage, {.row_count = 1, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{0}}, {.row_count = 1, .col_count = 2});
  const auto out =
      float_matrix_view(out_storage, {.row_count = 1, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{1}});
  constexpr std::array weight{1.0F, 1.0F};
  constexpr std::array bias{0.0F, 0.0F};

  layer_norm(out, in, weight, bias);

  constexpr auto tolerance = 1e-5;
  CHECK(out_storage[0] == -1.0F);
  CHECK_THAT(out_storage[1], WithinAbs(-1.0, tolerance));
  CHECK_THAT(out_storage[2], WithinAbs(1.0, tolerance));
}

// NOLINTEND(readability-function-cognitive-complexity)
