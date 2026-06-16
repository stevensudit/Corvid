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

#include "../corvid/math.h"
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
