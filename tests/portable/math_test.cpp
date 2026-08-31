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

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

using namespace corvid;

// Tolerance for the double-precision constants, a few ulps at their
// magnitudes.
constexpr double eps = 1e-15;

#pragma region TwoPi

TEST_CASE("TwoPi", "[MathTest]") {
  // Defaults to `float` and follows the argument otherwise, in the same
  // variable-template form as <numbers>.
  static_assert(
      std::is_same_v<std::remove_const_t<decltype(two_pi_v<>)>, float>);
  static_assert(
      std::is_same_v<std::remove_const_t<decltype(two_pi_v<double>)>, double>);
  static_assert(two_pi_v<double> == std::numbers::pi_v<double> * 2.0);

  // It is a full turn, checked against its decimal expansion and against the
  // trig identity rather than against the expression that produces it.
  CHECK(std::abs(two_pi_v<double> - 6.283185307179586) <= eps);
  CHECK(std::abs(std::sin(two_pi_v<double>)) <= eps);
}

#pragma endregion
#pragma region Cos30

TEST_CASE("Cos30", "[MathTest]") {
  // Defaults to `float` and follows the argument otherwise.
  static_assert(
      std::is_same_v<std::remove_const_t<decltype(cos_30_v<>)>, float>);
  static_assert(
      std::is_same_v<std::remove_const_t<decltype(cos_30_v<double>)>, double>);
  // Halving is exact in binary, so doubling it back is too.
  static_assert(cos_30_v<double> * 2.0 == std::numbers::sqrt3_v<double>);

  // It really is the cosine of 30 degrees.
  CHECK(std::abs(cos_30_v<double> - 0.8660254037844386) <= eps);
  CHECK(std::abs(cos_30_v<double> -
                 std::cos(std::numbers::pi_v<double> / 6.0)) <= eps);
}

#pragma endregion
#pragma region CeilDivExact

TEST_CASE("CeilDivExact", "[MathTest]") {
  // Exact division has no remainder to round up.
  CHECK(ceil_div(0, 3) == 0);
  CHECK(ceil_div(6, 3) == 2);
  CHECK(ceil_div(4096, 16) == 256);
}

#pragma endregion
#pragma region CeilDivRoundsUp

TEST_CASE("CeilDivRoundsUp", "[MathTest]") {
  // Any remainder rounds up to the next bucket.
  CHECK(ceil_div(1, 3) == 1);
  CHECK(ceil_div(7, 3) == 3);
  CHECK(ceil_div(4090, 16) == 256);
}

#pragma endregion
#pragma region CeilDivByOne

TEST_CASE("CeilDivByOne", "[MathTest]") {
  // A divisor of one returns the dividend unchanged.
  CHECK(ceil_div(0, 1) == 0);
  CHECK(ceil_div(7, 1) == 7);
}

#pragma endregion
#pragma region CeilDivMixedSign

TEST_CASE("CeilDivMixedSign", "[MathTest]") {
  // Mixed operands resolve to the common type. The matmul grid case is an
  // int dividend with an unsigned (dim3) divisor.
  CHECK(ceil_div(4090, 16U) == 256U);
  CHECK(ceil_div(size_t{1000}, 256) == 4U);
}

#pragma endregion
#pragma region CeilDivNoOverflow

TEST_CASE("CeilDivNoOverflow", "[MathTest]") {
  // The `(n + d - 1)` idiom would wrap uint32_t here; ceil_div must not.
  constexpr auto max32 = std::numeric_limits<uint32_t>::max();
  CHECK(ceil_div(max32, uint32_t{2}) == (max32 / 2) + 1);
}

#pragma endregion
#pragma region CeilDivConstexpr

TEST_CASE("CeilDivConstexpr", "[MathTest]") {
  // Usable in constant expressions.
  static_assert(ceil_div(7, 3) == 3);
  static_assert(ceil_div(16'777'216, 4096) == 4096);
}

#pragma endregion
#pragma region RoundUpAlreadyMultiple

TEST_CASE("RoundUpAlreadyMultiple", "[MathTest]") {
  // A value already on a boundary is returned unchanged.
  CHECK(round_up_to_multiple(0, 256) == 0);
  CHECK(round_up_to_multiple(256, 256) == 256);
  CHECK(round_up_to_multiple(512, 256) == 512);
}

#pragma endregion
#pragma region RoundUpRoundsUp

TEST_CASE("RoundUpRoundsUp", "[MathTest]") {
  // A value off a boundary rounds up to the next multiple.
  CHECK(round_up_to_multiple(1, 256) == 256);
  CHECK(round_up_to_multiple(255, 256) == 256);
  CHECK(round_up_to_multiple(257, 256) == 512);
}

#pragma endregion
#pragma region RoundUpByOne

TEST_CASE("RoundUpByOne", "[MathTest]") {
  // Every integer is a multiple of one, so the value is unchanged.
  CHECK(round_up_to_multiple(7, 1) == 7);
}

#pragma endregion
#pragma region RoundUpMixedSign

TEST_CASE("RoundUpMixedSign", "[MathTest]") {
  // Mixed operands resolve to the common type. The viewer rounds a pixel
  // width up to a 256-pixel quantum with an unsigned multiple.
  CHECK(round_up_to_multiple(100, 256U) == 256U);
  CHECK(round_up_to_multiple(513U, 256) == 768U);
}

#pragma endregion
#pragma region RoundUpNoDivisionOverflow

TEST_CASE("RoundUpNoDivisionOverflow", "[MathTest]") {
  // The underlying ceil_div avoids the (n + m - 1) wraparound, so rounding the
  // maximum up to a multiple of one stays exact (the multiply by one cannot
  // overflow).
  constexpr auto max32 = std::numeric_limits<uint32_t>::max();
  CHECK(round_up_to_multiple(max32, uint32_t{1}) == max32);
}

#pragma endregion
#pragma region RoundUpConstexpr

TEST_CASE("RoundUpConstexpr", "[MathTest]") {
  // Usable in constant expressions.
  static_assert(round_up_to_multiple(1, 256) == 256);
  static_assert(round_up_to_multiple(4096, 256) == 4096);
}

#pragma endregion
#pragma region SaturateCast

TEST_CASE("SaturateCast", "[MathTest]") {
  // In-range values pass through; out-of-range values clamp to the
  // destination maximum instead of truncating. Widening never clamps.
  CHECK(saturate_cast<uint8_t>(200UZ) == 200);
  CHECK(saturate_cast<uint8_t>(255UZ) == 255);
  CHECK(saturate_cast<uint8_t>(256UZ) == 255);
  CHECK(saturate_cast<uint8_t>(std::numeric_limits<size_t>::max()) == 255);
  CHECK(saturate_cast<size_t>(uint8_t{255}) == 255UZ);
  static_assert(saturate_cast<uint8_t>(1000U) == 255);
  static_assert(saturate_cast<uint16_t>(1000U) == 1000);
}

#pragma endregion
#pragma region ExtractByte

TEST_CASE("ExtractByte", "[MathTest]") {
  // Index 0 is the low byte, counting up from there.
  CHECK(extract_byte<0>(uint16_t{0x2001}) == 0x01);
  CHECK(extract_byte<1>(uint16_t{0x2001}) == 0x20);
  CHECK(extract_byte<0>(uint16_t{}) == 0);
  CHECK(extract_byte<1>(uint16_t{}) == 0);
}

#pragma endregion
#pragma region ExtractByteAddressesEveryByte

TEST_CASE("ExtractByteAddressesEveryByte", "[MathTest]") {
  // Every byte of a wider value is reachable by its own index, most
  // significant last.
  constexpr auto addr = uint32_t{0xc0a80101};
  CHECK(extract_byte<3>(addr) == 0xc0);
  CHECK(extract_byte<2>(addr) == 0xa8);
  CHECK(extract_byte<1>(addr) == 0x01);
  CHECK(extract_byte<0>(addr) == 0x01);
  CHECK(extract_byte<7>(uint64_t{0xfedc'ba98'7654'3210}) == 0xfe);
  CHECK(extract_byte<0>(uint8_t{0x42}) == 0x42);
}

#pragma endregion
#pragma region ExtractByteConstexpr

TEST_CASE("ExtractByteConstexpr", "[MathTest]") {
  // Usable in constant expressions.
  static_assert(extract_byte<1>(uint16_t{0xbeef}) == 0xbe);
  static_assert(extract_byte<0>(uint16_t{0xbeef}) == 0xef);
}

#pragma endregion

#ifdef NOT_SUPPOSED_TO_COMPILE
#pragma region ExtractByteOutOfRange

TEST_CASE("ExtractByteOutOfRange", "[MathTest]") {
  // A byte the type does not have is a compile error, not a zero.
  CHECK(extract_byte<2>(uint16_t{0x2001}) == 0);
}

#pragma endregion
#endif

#pragma region CombineBytes

TEST_CASE("CombineBytes", "[MathTest]") {
  // Least significant first, so the first argument is the low byte.
  CHECK(combine_bytes<uint16_t>(uint8_t{0x01}, uint8_t{0x20}) == 0x2001);
  CHECK(combine_bytes<uint32_t>(uint8_t{0x01}, uint8_t{0x01}, uint8_t{0xa8},
            uint8_t{0xc0}) == 0xc0a80101);
}

#pragma endregion
#pragma region CombineBytesRoundTrips

TEST_CASE("CombineBytesRoundTrips", "[MathTest]") {
  // The inverse of `extract_byte`.
  constexpr auto addr = uint32_t{0xc0a80101};
  CHECK(combine_bytes<uint32_t>(extract_byte<0>(addr), extract_byte<1>(addr),
            extract_byte<2>(addr), extract_byte<3>(addr)) == addr);
}

#pragma endregion
#pragma region CombineBytesSpellsItsZeros

TEST_CASE("CombineBytesSpellsItsZeros", "[MathTest]") {
  // Every byte is supplied, so a mostly-empty value states the zeros rather
  // than leaving them off.
  constexpr uint8_t z{};
  CHECK(combine_bytes<uint32_t>(uint8_t{0xff}, z, z, z) == 0xff);
  CHECK(combine_bytes<uint32_t>(z, z, z, uint8_t{0xff}) == 0xff000000);
}

#pragma endregion
#pragma region CombineBytesUsesOnlyTheLowByte

TEST_CASE("CombineBytesUsesOnlyTheLowByte", "[MathTest]") {
  // A wider argument contributes its low byte and nothing else.
  CHECK(combine_bytes<uint16_t>(uint16_t{0xbeef}, uint8_t{}) == 0x00ef);
}

#pragma endregion
#pragma region CombineBytesDeducesItsWidth

TEST_CASE("CombineBytesDeducesItsWidth", "[MathTest]") {
  // With no type named, the byte count picks the result type.
  constexpr uint8_t z{};
  static_assert(std::is_same_v<decltype(combine_bytes(z)), uint8_t>);
  static_assert(std::is_same_v<decltype(combine_bytes(z, z)), uint16_t>);
  static_assert(std::is_same_v<decltype(combine_bytes(z, z, z, z)), uint32_t>);
  static_assert(std::is_same_v<decltype(combine_bytes(z, z, z, z, z, z, z, z)),
      uint64_t>);

  CHECK(combine_bytes(uint8_t{0x01}, uint8_t{0x20}) == 0x2001);
  CHECK(combine_bytes(uint8_t{0x10}, z, z, z, z, z, z, uint8_t{0xfe}) ==
        0xfe00'0000'0000'0010);
}

#pragma endregion

#ifdef __SIZEOF_INT128__
#pragma region ExtractAndCombine128

TEST_CASE("ExtractAndCombine128", "[MathTest]") {
  // Where the compiler has a 128-bit type, the pair reaches it too. Note that
  // libstdc++ outside GNU mode does not report `__uint128_t` as integral, so
  // this rests on the library-independent `UnsignedWord`.
  constexpr uint8_t z{};
  const auto v = combine_bytes(uint8_t{0x10}, z, z, z, z, z, z, z, z, z, z, z,
      z, z, z, uint8_t{0xfe});
  static_assert(std::is_same_v<decltype(v), const __uint128_t>);
  CHECK(extract_byte<0>(v) == 0x10);
  CHECK(extract_byte<15>(v) == 0xfe);
  CHECK(extract_byte<7>(v) == 0);
}

#pragma endregion
#endif

#pragma region CombineBytesDeducedMatchesSpelled

TEST_CASE("CombineBytesDeducedMatchesSpelled", "[MathTest]") {
  // Naming the type is allowed and must agree with the count.
  constexpr uint8_t lo{0xef};
  constexpr uint8_t hi{0xbe};
  CHECK(combine_bytes<uint16_t>(lo, hi) == combine_bytes(lo, hi));
}

#pragma endregion
#pragma region CombineBytesConstexpr

TEST_CASE("CombineBytesConstexpr", "[MathTest]") {
  // Usable in constant expressions.
  static_assert(
      combine_bytes<uint16_t>(uint8_t{0xef}, uint8_t{0xbe}) == 0xbeef);
}

#pragma endregion

#ifdef NOT_SUPPOSED_TO_COMPILE
#pragma region CombineBytesWrongCount

TEST_CASE("CombineBytesWrongCount", "[MathTest]") {
  // A named type must match the count exactly, in both directions.
  CHECK(combine_bytes<uint16_t>(uint8_t{1}, uint8_t{2}, uint8_t{3}) == 0);
  CHECK(combine_bytes<uint32_t>(uint8_t{1}) == 0);
  // Deducing needs a count some standard width matches.
  CHECK(combine_bytes(uint8_t{1}, uint8_t{2}, uint8_t{3}) == 0);
}

#pragma endregion
#endif
