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

#include "corvid/math.h"
#include "catch2_main.h"

#include <cstdint>
#include <limits>

using namespace corvid;

TEST_CASE("CeilDivExact", "[MathTest]") {
  // Exact division has no remainder to round up.
  CHECK(ceil_div(0, 3) == 0);
  CHECK(ceil_div(6, 3) == 2);
  CHECK(ceil_div(4096, 16) == 256);
}

TEST_CASE("CeilDivRoundsUp", "[MathTest]") {
  // Any remainder rounds up to the next bucket.
  CHECK(ceil_div(1, 3) == 1);
  CHECK(ceil_div(7, 3) == 3);
  CHECK(ceil_div(4090, 16) == 256);
}

TEST_CASE("CeilDivByOne", "[MathTest]") {
  // A divisor of one returns the dividend unchanged.
  CHECK(ceil_div(0, 1) == 0);
  CHECK(ceil_div(7, 1) == 7);
}

TEST_CASE("CeilDivMixedSign", "[MathTest]") {
  // Mixed operands resolve to the common type. The matmul grid case is an
  // int dividend with an unsigned (dim3) divisor.
  CHECK(ceil_div(4090, 16U) == 256U);
  CHECK(ceil_div(size_t{1000}, 256) == 4U);
}

TEST_CASE("CeilDivNoOverflow", "[MathTest]") {
  // The `(n + d - 1)` idiom would wrap uint32_t here; ceil_div must not.
  constexpr auto max32 = std::numeric_limits<std::uint32_t>::max();
  CHECK(ceil_div(max32, std::uint32_t{2}) == (max32 / 2) + 1);
}

TEST_CASE("CeilDivConstexpr", "[MathTest]") {
  // Usable in constant expressions.
  static_assert(ceil_div(7, 3) == 3);
  static_assert(ceil_div(16'777'216, 4096) == 4096);
}

TEST_CASE("RoundUpAlreadyMultiple", "[MathTest]") {
  // A value already on a boundary is returned unchanged.
  CHECK(round_up_to_multiple(0, 256) == 0);
  CHECK(round_up_to_multiple(256, 256) == 256);
  CHECK(round_up_to_multiple(512, 256) == 512);
}

TEST_CASE("RoundUpRoundsUp", "[MathTest]") {
  // A value off a boundary rounds up to the next multiple.
  CHECK(round_up_to_multiple(1, 256) == 256);
  CHECK(round_up_to_multiple(255, 256) == 256);
  CHECK(round_up_to_multiple(257, 256) == 512);
}

TEST_CASE("RoundUpByOne", "[MathTest]") {
  // Every integer is a multiple of one, so the value is unchanged.
  CHECK(round_up_to_multiple(7, 1) == 7);
}

TEST_CASE("RoundUpMixedSign", "[MathTest]") {
  // Mixed operands resolve to the common type. The viewer rounds a pixel
  // width up to a 256-pixel quantum with an unsigned multiple.
  CHECK(round_up_to_multiple(100, 256U) == 256U);
  CHECK(round_up_to_multiple(513U, 256) == 768U);
}

TEST_CASE("RoundUpNoDivisionOverflow", "[MathTest]") {
  // The underlying ceil_div avoids the (n + m - 1) wraparound, so rounding the
  // maximum up to a multiple of one stays exact (the multiply by one cannot
  // overflow).
  constexpr auto max32 = std::numeric_limits<std::uint32_t>::max();
  CHECK(round_up_to_multiple(max32, std::uint32_t{1}) == max32);
}

TEST_CASE("RoundUpConstexpr", "[MathTest]") {
  // Usable in constant expressions.
  static_assert(round_up_to_multiple(1, 256) == 256);
  static_assert(round_up_to_multiple(4096, 256) == 4096);
}
