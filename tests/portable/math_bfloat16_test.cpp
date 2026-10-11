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

#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include "corvid/math/bfloat16.h"
#include "catch2_main.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

static_assert(sizeof(bfloat16_t) == 2);
static_assert(std::is_trivially_copyable_v<bfloat16_t>);
static_assert(std::is_trivially_default_constructible_v<bfloat16_t>);

TEST_CASE("bfloat16_t is the top half of a float", "[Bfloat16Test]") {
  struct pattern {
    float value;
    uint16_t bits;
  };
  // Exactly representable values keep their top half, and the rest round to
  // nearest, ties to even: 1 + 2^-8 sits between 0x3F80 and 0x3F81 and takes
  // the even one, 1 + 3 * 2^-8 sits between 0x3F81 and 0x3F82 and takes
  // 0x3F82. The largest float rounds up to infinity, and the smallest
  // denormal rounds down to zero.
  const std::vector<pattern> patterns{
      {0.0F, 0x0000},
      {-0.0F, 0x8000},
      {1.0F, 0x3F80},
      {2.0F, 0x4000},
      {-1.5F, 0xBFC0},
      {1.0078125F, 0x3F81},
      {1.00390625F, 0x3F80},
      {1.01171875F, 0x3F82},
      {1.005F, 0x3F81},
      {std::numeric_limits<float>::max(), 0x7F80},
      {std::numeric_limits<float>::infinity(), 0x7F80},
      {-std::numeric_limits<float>::infinity(), 0xFF80},
      {std::numeric_limits<float>::denorm_min(), 0x0000},
  };
  for (const auto& [value, bits] : patterns) {
    CAPTURE(value);
    CHECK(bfloat16_t{value}.bits() == bits);
    CHECK(static_cast<float>(bfloat16_t::from_bits(bits)) ==
          static_cast<float>(bfloat16_t{value}));
  }

  // The exactly representable ones survive the round trip untouched.
  for (const auto value : {0.0F, 1.0F, 2.0F, -1.5F, 1.0078125F,
           std::numeric_limits<float>::infinity()})
    CHECK(static_cast<float>(bfloat16_t{value}) == value);

  // Every NaN, whatever its sign or payload, becomes the one quiet NaN.
  for (const auto value : {std::numeric_limits<float>::quiet_NaN(),
           -std::numeric_limits<float>::quiet_NaN(),
           std::numeric_limits<float>::signaling_NaN()})
  {
    CHECK(bfloat16_t{value}.bits() == 0x7FFF);
    CHECK(std::isnan(static_cast<float>(bfloat16_t{value})));
  }
  CHECK(std::isnan(static_cast<float>(bfloat16_t::from_bits(0x7F81))));

  // The conversions are constant expressions.
  static_assert(bfloat16_t{1.0F}.bits() == 0x3F80);
  static_assert(static_cast<float>(bfloat16_t::from_bits(0x4000)) == 2.0F);
}

TEST_CASE("bfloat16_t compares as its float value", "[Bfloat16Test]") {
  const bfloat16_t one{1.0F};
  const bfloat16_t two{2.0F};
  CHECK(one == bfloat16_t::from_bits(0x3F80));
  CHECK(one < two);
  CHECK(one <= two);
  CHECK(two > one);
  CHECK(two >= one);
  CHECK(!(two < one));
  CHECK(one != two);
  CHECK(bfloat16_t{0.0F} == bfloat16_t{-0.0F});

  // The order is partial. A NaN is unordered against everything, itself
  // included, so every comparison with one is false.
  const auto nan = bfloat16_t{std::numeric_limits<float>::quiet_NaN()};
  CHECK((nan <=> one) == std::partial_ordering::unordered);
  CHECK(!(nan < one));
  CHECK(!(nan >= one));
  CHECK(!(nan == nan));
  CHECK(nan != nan);
  static_assert(bfloat16_t{1.0F} < bfloat16_t{2.0F});
}

// NOLINTEND(readability-function-cognitive-complexity)
