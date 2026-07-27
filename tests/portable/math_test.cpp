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

TEST_CASE("ExtractByte", "[MathTest]") {
  // Index 0 is the low byte, counting up from there.
  CHECK(extract_byte<0>(std::uint16_t{0x2001}) == 0x01);
  CHECK(extract_byte<1>(std::uint16_t{0x2001}) == 0x20);
  CHECK(extract_byte<0>(std::uint16_t{}) == 0);
  CHECK(extract_byte<1>(std::uint16_t{}) == 0);
}

TEST_CASE("ExtractByteAddressesEveryByte", "[MathTest]") {
  // Every byte of a wider value is reachable by its own index, most
  // significant last.
  constexpr auto addr = std::uint32_t{0xc0a80101};
  CHECK(extract_byte<3>(addr) == 0xc0);
  CHECK(extract_byte<2>(addr) == 0xa8);
  CHECK(extract_byte<1>(addr) == 0x01);
  CHECK(extract_byte<0>(addr) == 0x01);
  CHECK(extract_byte<7>(std::uint64_t{0xfedc'ba98'7654'3210}) == 0xfe);
  CHECK(extract_byte<0>(std::uint8_t{0x42}) == 0x42);
}

TEST_CASE("ExtractByteConstexpr", "[MathTest]") {
  // Usable in constant expressions.
  static_assert(extract_byte<1>(std::uint16_t{0xbeef}) == 0xbe);
  static_assert(extract_byte<0>(std::uint16_t{0xbeef}) == 0xef);
}

#ifdef NOT_SUPPOSED_TO_COMPILE
TEST_CASE("ExtractByteOutOfRange", "[MathTest]") {
  // A byte the type does not have is a compile error, not a zero.
  CHECK(extract_byte<2>(std::uint16_t{0x2001}) == 0);
}
#endif

TEST_CASE("CombineBytes", "[MathTest]") {
  // Least significant first, so the first argument is the low byte.
  CHECK(combine_bytes<std::uint16_t>(std::uint8_t{0x01}, std::uint8_t{0x20}) ==
        0x2001);
  CHECK(combine_bytes<std::uint32_t>(std::uint8_t{0x01}, std::uint8_t{0x01},
            std::uint8_t{0xa8}, std::uint8_t{0xc0}) == 0xc0a80101);
}

TEST_CASE("CombineBytesRoundTrips", "[MathTest]") {
  // The inverse of `extract_byte`.
  constexpr auto addr = std::uint32_t{0xc0a80101};
  CHECK(combine_bytes<std::uint32_t>(extract_byte<0>(addr),
            extract_byte<1>(addr), extract_byte<2>(addr),
            extract_byte<3>(addr)) == addr);
}

TEST_CASE("CombineBytesSpellsItsZeros", "[MathTest]") {
  // Every byte is supplied, so a mostly-empty value states the zeros rather
  // than leaving them off.
  constexpr std::uint8_t z{};
  CHECK(combine_bytes<std::uint32_t>(std::uint8_t{0xff}, z, z, z) == 0xff);
  CHECK(
      combine_bytes<std::uint32_t>(z, z, z, std::uint8_t{0xff}) == 0xff000000);
}

TEST_CASE("CombineBytesUsesOnlyTheLowByte", "[MathTest]") {
  // A wider argument contributes its low byte and nothing else.
  CHECK(combine_bytes<std::uint16_t>(std::uint16_t{0xbeef}, std::uint8_t{}) ==
        0x00ef);
}

TEST_CASE("CombineBytesDeducesItsWidth", "[MathTest]") {
  // With no type named, the byte count picks the result type.
  constexpr std::uint8_t z{};
  static_assert(std::is_same_v<decltype(combine_bytes(z)), std::uint8_t>);
  static_assert(std::is_same_v<decltype(combine_bytes(z, z)), std::uint16_t>);
  static_assert(
      std::is_same_v<decltype(combine_bytes(z, z, z, z)), std::uint32_t>);
  static_assert(std::is_same_v<decltype(combine_bytes(z, z, z, z, z, z, z, z)),
      std::uint64_t>);

  CHECK(combine_bytes(std::uint8_t{0x01}, std::uint8_t{0x20}) == 0x2001);
  CHECK(combine_bytes(std::uint8_t{0x10}, z, z, z, z, z, z,
            std::uint8_t{0xfe}) == 0xfe00'0000'0000'0010);
}

#ifdef __SIZEOF_INT128__
TEST_CASE("ExtractAndCombine128", "[MathTest]") {
  // Where the compiler has a 128-bit type, the pair reaches it too. Note that
  // libstdc++ outside GNU mode does not report `__uint128_t` as integral, so
  // this rests on the library-independent `UnsignedWord`.
  constexpr std::uint8_t z{};
  const auto v = combine_bytes(std::uint8_t{0x10}, z, z, z, z, z, z, z, z, z,
      z, z, z, z, z, std::uint8_t{0xfe});
  static_assert(std::is_same_v<decltype(v), const __uint128_t>);
  CHECK(extract_byte<0>(v) == 0x10);
  CHECK(extract_byte<15>(v) == 0xfe);
  CHECK(extract_byte<7>(v) == 0);
}
#endif

TEST_CASE("CombineBytesDeducedMatchesSpelled", "[MathTest]") {
  // Naming the type is allowed and must agree with the count.
  constexpr std::uint8_t lo{0xef};
  constexpr std::uint8_t hi{0xbe};
  CHECK(combine_bytes<std::uint16_t>(lo, hi) == combine_bytes(lo, hi));
}

TEST_CASE("CombineBytesConstexpr", "[MathTest]") {
  // Usable in constant expressions.
  static_assert(
      combine_bytes<std::uint16_t>(std::uint8_t{0xef}, std::uint8_t{0xbe}) ==
      0xbeef);
}

#ifdef NOT_SUPPOSED_TO_COMPILE
TEST_CASE("CombineBytesWrongCount", "[MathTest]") {
  // A named type must match the count exactly, in both directions.
  CHECK(combine_bytes<std::uint16_t>(std::uint8_t{1}, std::uint8_t{2},
            std::uint8_t{3}) == 0);
  CHECK(combine_bytes<std::uint32_t>(std::uint8_t{1}) == 0);
  // Deducing needs a count some standard width matches.
  CHECK(combine_bytes(std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}) == 0);
}
#endif
