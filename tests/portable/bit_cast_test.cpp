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

#include "corvid/meta/bit_cast.h"
#include "catch2_main.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;
using namespace corvid;

namespace {

// A trivially copyable aggregate, to show the cast is not limited to
// integers.
struct point {
  uint16_t x;
  uint16_t y;
  friend bool operator==(const point&, const point&) = default;
};

using bytes8 = std::array<std::byte, 8>;
using bytes4 = std::array<std::byte, 4>;

const bytes8 one_to_eight{std::byte{1}, std::byte{2}, std::byte{3},
    std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
const auto first_word = std::bit_cast<uint32_t>(
    bytes4{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
const auto second_word = std::bit_cast<uint32_t>(
    bytes4{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}});

} // namespace

// Concept checks.
static_assert(ByteLike<std::byte>);
static_assert(ByteLike<char>);
static_assert(ByteLike<unsigned char>);
static_assert(ByteLike<signed char>);
static_assert(ByteLike<char8_t>);
static_assert(ByteLike<const std::byte>);
static_assert(!ByteLike<bool>);
static_assert(!ByteLike<char16_t>);
static_assert(!ByteLike<int>);

static_assert(ByteRange<std::span<const std::byte>>);
static_assert(ByteRange<std::span<uint8_t>>);
static_assert(ByteRange<std::string_view>);
static_assert(ByteRange<std::string>);
static_assert(ByteRange<const std::array<char, 4>&>);
static_assert(ByteRange<char[4]>);
static_assert(!ByteRange<std::vector<int>>);
static_assert(!ByteRange<std::span<const uint32_t>>);

static_assert(MutableByteRange<std::span<std::byte>>);
static_assert(MutableByteRange<std::vector<uint8_t>>);
static_assert(MutableByteRange<std::array<char, 4>&>);
static_assert(MutableByteRange<char[4]>);
static_assert(!MutableByteRange<std::span<const std::byte>>);
static_assert(!MutableByteRange<std::string_view>);
static_assert(!MutableByteRange<const std::array<char, 4>&>);

static_assert(TriviallyCopyableRange<std::span<const float>>);
static_assert(TriviallyCopyableRange<std::vector<point>>);
static_assert(TriviallyCopyableRange<std::string_view>);
static_assert(!TriviallyCopyableRange<std::vector<std::string>>);

static_assert(static_size_v<char[4]> == 4);
static_assert(static_size_v<const std::array<std::byte, 8>&> == 8);
static_assert(static_size_v<std::span<uint8_t, 2>> == 2);
static_assert(static_size_v<std::span<uint8_t>> == std::dynamic_extent);
static_assert(static_size_v<std::vector<uint8_t>> == std::dynamic_extent);
static_assert(static_size_v<std::string_view> == std::dynamic_extent);

// A range whose size is static and too short fails a `static_assert`; a
// dynamic size is left to the runtime check.
#ifdef NOT_SUPPOSED_TO_COMPILE
[[maybe_unused]] auto too_short_array = [](std::array<std::byte, 4>& r) {
  return bit_cast_from<uint64_t>(r);
};
[[maybe_unused]] auto too_short_span = [](std::span<const std::byte, 7> r) {
  return bit_cast_from<uint64_t>(r);
};
[[maybe_unused]] auto too_short_c_array = [](const char (&r)[7]) {
  return bit_cast_from<uint64_t>(r);
};
#endif

// The destination range must be mutable; the source may be const. A value
// that is itself a range is rejected by the single-value overloads.
template<typename R>
concept CanCastToRange = requires(R& r) {
  corvid::try_bit_cast_to(r, uint32_t{});
  corvid::bit_cast_to(r, uint32_t{});
};
static_assert(CanCastToRange<std::array<std::byte, 4>>);
static_assert(!CanCastToRange<const std::array<std::byte, 4>>);
static_assert(!CanCastToRange<std::span<const std::byte>>);
template<typename R>
concept CanCastFromRange = requires(R& r, uint32_t v) {
  corvid::try_bit_cast_to(v, r);
  corvid::bit_cast_to(v, r);
};
static_assert(CanCastFromRange<std::array<std::byte, 4>>);
static_assert(CanCastFromRange<const std::array<std::byte, 4>>);
static_assert(CanCastFromRange<std::span<const std::byte>>);
template<typename V>
concept CanCastValue = requires(V& v, std::array<std::byte, 16>& r) {
  corvid::try_bit_cast_to(r, v);
  corvid::try_bit_cast_to(v, r);
};
static_assert(CanCastValue<uint32_t>);
static_assert(CanCastValue<point>);
static_assert(!CanCastValue<std::array<uint8_t, 4>>);
static_assert(!CanCastValue<std::span<std::byte>>);
static_assert(!CanCastValue<std::string_view>);
template<typename S>
concept CanCastToSpan = requires(S& s, const std::vector<float>& from) {
  corvid::try_bit_cast_to(s, from);
};
static_assert(CanCastToSpan<std::span<uint32_t>>);
static_assert(CanCastToSpan<std::span<std::byte, 8>>);
static_assert(CanCastToSpan<std::span<float>>);
static_assert(!CanCastToSpan<std::span<const uint32_t>>);
static_assert(!CanCastToSpan<const std::span<uint32_t>>);
// The span destination has no non-try form: its output size varies with the
// inputs, so the result is the only way to report the fit.
template<typename S>
concept HasAssertingSpanCast = requires(S& s, const std::vector<float>& f) {
  corvid::bit_cast_to(s, f);
};
static_assert(!HasAssertingSpanCast<std::span<uint32_t>>);
static_assert(!HasAssertingSpanCast<std::span<std::byte, 8>>);

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("bit_cast_from", "[BitCast]") {
  // The expected values come from `std::bit_cast` on a same-sized object, so
  // the checks hold in either byte order.
  const auto expected64 = std::bit_cast<uint64_t>(one_to_eight);

  SECTION("exact fit") {
    CHECK(bit_cast_from<uint64_t>(one_to_eight) == expected64);
    CHECK(bit_cast_from<uint64_t>(std::span<const std::byte>{one_to_eight}) ==
          expected64);
  }
  SECTION("leading bytes of a longer range") {
    CHECK(bit_cast_from<uint32_t>(one_to_eight) == first_word);
  }
  SECTION("from inside a buffer") {
    const std::span<const std::byte> view{one_to_eight};
    CHECK(bit_cast_from<uint32_t>(view.subspan(4)) == second_word);
  }
  SECTION("other byte types") {
    const std::array<uint8_t, 4> unsigned_bytes{1, 2, 3, 4};
    CHECK(bit_cast_from<uint32_t>(unsigned_bytes) == first_word);
    const auto sv = "\x01\x02\x03\x04"sv;
    CHECK(bit_cast_from<uint32_t>(sv) == first_word);
    const std::vector<char> chars{1, 2, 3, 4};
    CHECK(bit_cast_from<uint32_t>(chars) == first_word);
    const char raw[4]{1, 2, 3, 4};
    CHECK(bit_cast_from<uint32_t>(raw) == first_word);
  }
  SECTION("aggregate") {
    const auto expected = std::bit_cast<point>(
        bytes4{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    CHECK(bit_cast_from<point>(one_to_eight) == expected);
  }
  SECTION("prvalue range") {
    CHECK(bit_cast_from<uint32_t>(std::span{one_to_eight}.first(4)) ==
          first_word);
  }
  SECTION("static sizes that fit") {
    CHECK(bit_cast_from<uint64_t>(
              std::span<const std::byte, 8>{one_to_eight}) == expected64);
    const char nine[9]{1, 2, 3, 4, 5, 6, 7, 8, 9};
    CHECK(bit_cast_from<uint64_t>(nine) == expected64);
  }
}

TEST_CASE("try_bit_cast_from", "[BitCast]") {
  SECTION("fits") {
    const auto value = try_bit_cast_from<uint32_t>(one_to_eight);
    REQUIRE(value);
    CHECK(*value == first_word);
    const auto tail =
        try_bit_cast_from<uint32_t>(std::span{one_to_eight}.subspan(4));
    REQUIRE(tail);
    CHECK(*tail == second_word);
    CHECK(try_bit_cast_from<point>(""sv) ==
          std::bit_cast<point>(
              bytes4{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}));
  }
  SECTION("too short") {
    CHECK_FALSE(try_bit_cast_from<uint32_t>(std::span{one_to_eight}.first(3)));
    CHECK_FALSE(try_bit_cast_from<uint32_t>(std::span<const std::byte>{}));
    CHECK_FALSE(try_bit_cast_from<uint64_t>(""sv));
  }
}

TEST_CASE("bit_cast_to value into range", "[BitCast]") {
  SECTION("exact fit") {
    bytes4 out{};
    CHECK(bit_cast_to(out, first_word));
    CHECK(
        out == bytes4{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
  }
  SECTION("leaves the tail alone") {
    bytes8 out{};
    out.fill(std::byte{0xFF});
    CHECK(try_bit_cast_to(out, first_word));
    CHECK(out ==
          bytes8{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
              std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
              std::byte{0xFF}});
  }
  SECTION("into a buffer through a subspan") {
    bytes8 out{};
    CHECK(try_bit_cast_to(std::span{out}.subspan(4), first_word));
    CHECK(out == bytes8{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                     std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
  }
  SECTION("other byte types") {
    std::string s(4, '\0');
    CHECK(try_bit_cast_to(s, first_word));
    CHECK(s == "\x01\x02\x03\x04"sv);
    std::vector<uint8_t> v(4);
    CHECK(try_bit_cast_to(v, first_word));
    CHECK(v == std::vector<uint8_t>{1, 2, 3, 4});
    char raw[4]{};
    CHECK(try_bit_cast_to(raw, first_word));
    CHECK(std::string_view{raw, 4} == "\x01\x02\x03\x04"sv);
  }
  SECTION("round trip") {
    const point p{0x1234, 0x5678};
    bytes8 out{};
    CHECK(try_bit_cast_to(out, p));
    CHECK(bit_cast_from<point>(out) == p);
    const auto x = 0x0123456789ABCDEFULL;
    CHECK(try_bit_cast_to(out, x));
    CHECK(bit_cast_from<uint64_t>(out) == x);
  }
  SECTION("too short") {
    bytes4 out{};
    out.fill(std::byte{0xFF});
    CHECK_FALSE(try_bit_cast_to(std::span{out}.first(3), first_word));
    CHECK(out == bytes4{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                     std::byte{0xFF}});
    CHECK_FALSE(try_bit_cast_to(std::span<std::byte>{}, first_word));
  }
}

TEST_CASE("bit_cast_to range into value", "[BitCast]") {
  SECTION("exact fit and leading bytes") {
    uint32_t value{};
    CHECK(bit_cast_to(value, std::span{one_to_eight}.first(4)));
    CHECK(value == first_word);
    value = 0;
    CHECK(try_bit_cast_to(value, one_to_eight));
    CHECK(value == first_word);
  }
  SECTION("other byte types") {
    uint32_t value{};
    CHECK(try_bit_cast_to(value, "\x01\x02\x03\x04"sv));
    CHECK(value == first_word);
    const char raw[4]{1, 2, 3, 4};
    value = 0;
    CHECK(try_bit_cast_to(value, raw));
    CHECK(value == first_word);
  }
  SECTION("aggregate") {
    point p{};
    CHECK(try_bit_cast_to(p, one_to_eight));
    CHECK(p == std::bit_cast<point>(bytes4{std::byte{1}, std::byte{2},
                   std::byte{3}, std::byte{4}}));
  }
  SECTION("too short leaves the value alone") {
    auto value = 0xDEADBEEFU;
    CHECK_FALSE(try_bit_cast_to(value, std::span{one_to_eight}.first(3)));
    CHECK(value == 0xDEADBEEFU);
    CHECK_FALSE(try_bit_cast_to(value, std::span<const std::byte>{}));
    CHECK(value == 0xDEADBEEFU);
  }
}

TEST_CASE("bit_cast_to span, dynamic extent", "[BitCast]") {
  SECTION("exact fit") {
    std::array<uint32_t, 2> words{};
    auto out = std::span<uint32_t>{words};
    CHECK(try_bit_cast_to(out, one_to_eight));
    CHECK(out.size() == 2);
    CHECK(words == std::array{first_word, second_word});
  }
  SECTION("destination shorter than source") {
    std::array<uint32_t, 4> words{};
    auto out = std::span<uint32_t>{words}.first(1);
    CHECK(try_bit_cast_to(out, one_to_eight));
    CHECK(out.size() == 1);
    CHECK(words == std::array{first_word, 0U, 0U, 0U});
  }
  SECTION("source shorter than destination shrinks it") {
    std::array<uint32_t, 4> words{};
    words.fill(0xFFFFFFFFU);
    auto out = std::span<uint32_t>{words};
    CHECK(try_bit_cast_to(out, one_to_eight));
    CHECK(out.size() == 2);
    CHECK(words ==
          std::array{first_word, second_word, 0xFFFFFFFFU, 0xFFFFFFFFU});
  }
  SECTION("partial trailing element is dropped") {
    std::array<uint32_t, 4> words{};
    auto out = std::span<uint32_t>{words};
    CHECK(try_bit_cast_to(out, std::span{one_to_eight}.first(7)));
    CHECK(out.size() == 1);
    CHECK(words[0] == first_word);
    CHECK(words[1] == 0);
  }
  SECTION("non-byte source") {
    const std::array<float, 2> floats{1.5F, -2.0F};
    std::array<uint32_t, 2> words{};
    auto out = std::span<uint32_t>{words};
    CHECK(try_bit_cast_to(out, floats));
    CHECK(out.size() == 2);
    CHECK(words[0] == std::bit_cast<uint32_t>(1.5F));
    CHECK(words[1] == std::bit_cast<uint32_t>(-2.0F));
  }
  SECTION("same element type") {
    std::array<uint32_t, 3> words{};
    auto out = std::span<uint32_t>{words};
    CHECK(try_bit_cast_to(out, std::array{7U, 8U}));
    CHECK(out.size() == 2);
    CHECK(words == std::array{7U, 8U, 0U});
  }
  SECTION("value to bytes") {
    const std::array<uint32_t, 2> words{first_word, second_word};
    bytes8 out_bytes{};
    auto out = std::span<std::byte>{out_bytes};
    CHECK(try_bit_cast_to(out, words));
    CHECK(out.size() == 8);
    CHECK(out_bytes == one_to_eight);
  }
  SECTION("nothing to copy") {
    std::array<uint32_t, 2> words{};
    auto out = std::span<uint32_t>{words};
    CHECK_FALSE(try_bit_cast_to(out, std::span<const std::byte>{}));
    CHECK(out.size() == 2);
    CHECK_FALSE(try_bit_cast_to(out, std::span{one_to_eight}.first(3)));
    CHECK(out.size() == 2);
    auto empty = std::span<uint32_t>{};
    CHECK_FALSE(try_bit_cast_to(empty, one_to_eight));
    CHECK(words == std::array{0U, 0U});
  }
}

TEST_CASE("bit_cast_to span, static extent", "[BitCast]") {
  SECTION("exact fit") {
    std::array<uint32_t, 2> words{};
    auto out = std::span<uint32_t, 2>{words};
    CHECK(try_bit_cast_to(out, one_to_eight));
    CHECK(words == std::array{first_word, second_word});
  }
  SECTION("longer source is fine") {
    uint32_t pair[2]{};
    auto out = std::span{pair};
    static_assert(out.extent == 2);
    CHECK(try_bit_cast_to(out, one_to_eight));
    CHECK(pair[0] == first_word);
    CHECK(pair[1] == second_word);
  }
  SECTION("short source is refused") {
    std::array<uint32_t, 2> words{};
    words.fill(0xFFFFFFFFU);
    auto out = std::span<uint32_t, 2>{words};
    CHECK_FALSE(try_bit_cast_to(out, std::span{one_to_eight}.first(7)));
    CHECK(words == std::array{0xFFFFFFFFU, 0xFFFFFFFFU});
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
