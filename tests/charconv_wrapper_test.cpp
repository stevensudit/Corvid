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
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include "../corvid/strings/core/charconv_wrapper.h"

#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

// Format `value` to a `std::string` with our `int_to_chars`.
template<std::integral T>
static std::string ours_int(T value, int base = 10) {
  std::array<char, 80> b{};
  auto [ptr, ec] =
      strings::int_to_chars(b.data(), b.data() + b.size(), value, base);
  CHECK(ec == std::errc{});
  return std::string{b.data(), ptr};
}

// Format `value` to a `std::string` with `std::to_chars`.
template<std::integral T>
static std::string theirs_int(T value, int base = 10) {
  std::array<char, 80> b{};
  auto [ptr, ec] = std::to_chars(b.data(), b.data() + b.size(), value, base);
  CHECK(ec == std::errc{});
  return std::string{b.data(), ptr};
}

#pragma region IntToChars

TEST_CASE("IntToChars", "[charconv]") {
  SECTION("matches std across bases and values") {
    const int bases[] = {2, 8, 10, 16, 36};
    const int64_t svals[] = {0, 1, -1, 7, -7, 42, -42, 255, 256, 1000, -1000,
        65535, 2147483647, -2147483648, 9223372036854775807LL,
        (-9223372036854775807LL - 1)};
    const uint64_t uvals[] = {0u, 1u, 255u, 256u, 65535u, 4294967295u,
        18446744073709551615ULL};
    for (int base : bases) {
      for (int64_t v : svals) CHECK(ours_int(v, base) == theirs_int(v, base));
      for (uint64_t v : uvals) CHECK(ours_int(v, base) == theirs_int(v, base));
    }
  }

  SECTION("small types and their extremes") {
    CHECK(ours_int<int8_t>(-128) == "-128");
    CHECK(ours_int<int8_t>(127) == "127");
    CHECK(ours_int<uint8_t>(255) == "255");
    CHECK(ours_int<int16_t>(-32768) == "-32768");
    CHECK(ours_int<uint16_t>(65535, 16) == "ffff");
  }

  SECTION("value_too_large when the buffer is short") {
    std::array<char, 2> b{};
    auto [ptr, ec] =
        strings::int_to_chars(b.data(), b.data() + b.size(), 1000, 10);
    CHECK(ec == std::errc::value_too_large);
    CHECK(ptr == b.data() + b.size());
  }
}

#pragma endregion
#pragma region IntFromChars

TEST_CASE("IntFromChars", "[charconv]") {
  SECTION("round-trips std's output across bases and values") {
    const int bases[] = {2, 8, 10, 16, 36};
    const int64_t svals[] = {0, 1, -1, 7, -7, 42, -42, 255, 256, 1000, -1000,
        9223372036854775807LL, (-9223372036854775807LL - 1)};
    for (int base : bases)
      for (int64_t v : svals) {
        const auto s = theirs_int(v, base);
        int64_t out = 999;
        auto [ptr, ec] =
            strings::int_from_chars(s.data(), s.data() + s.size(), out, base);
        CHECK(ec == std::errc{});
        CHECK(ptr == s.data() + s.size());
        CHECK(out == v);
      }
  }

  SECTION("stops at the first non-digit") {
    auto s = "123abc"sv;
    int out = 0;
    auto [ptr, ec] =
        strings::int_from_chars(s.data(), s.data() + s.size(), out, 10);
    CHECK(ec == std::errc{});
    CHECK(out == 123);
    CHECK(ptr == s.data() + 3);
  }

  SECTION("rejects empty and non-numeric input at first") {
    for (auto s : {""sv, "abc"sv, "-"sv, "-x"sv}) {
      int out = 7;
      auto [ptr, ec] =
          strings::int_from_chars(s.data(), s.data() + s.size(), out, 10);
      CHECK(ec == std::errc::invalid_argument);
      CHECK(ptr == s.data());
      CHECK(out == 7);
    }
  }

  SECTION("reports overflow and leaves value unchanged") {
    for (auto s : {"9223372036854775808"sv, "99999999999999999999"sv}) {
      int64_t out = -5;
      auto [ptr, ec] =
          strings::int_from_chars(s.data(), s.data() + s.size(), out, 10);
      CHECK(ec == std::errc::result_out_of_range);
      CHECK(out == -5);
      CHECK(ptr == s.data() + s.size());
    }
    auto s = "18446744073709551616"sv; // uint64 max + 1
    uint64_t out = 5;
    auto [ptr, ec] =
        strings::int_from_chars(s.data(), s.data() + s.size(), out, 10);
    CHECK(ec == std::errc::result_out_of_range);
    CHECK(out == 5);
  }

  SECTION("accepts upper- and lowercase digits above base 10") {
    int outl = 0, outu = 0;
    auto l = "ff"sv, u = "FF"sv;
    (void)strings::int_from_chars(l.data(), l.data() + l.size(), outl, 16);
    (void)strings::int_from_chars(u.data(), u.data() + u.size(), outu, 16);
    CHECK(outl == 255);
    CHECK(outu == 255);
  }
}

#pragma endregion
#pragma region WideInt

TEST_CASE("WideInt", "[charconv]") {
  // Format wide, and confirm it equals the widened `char` form.
  auto widen_check = [](auto value, int base) {
    using T = decltype(value);
    const std::string narrow = ours_int(value, base);
    std::array<char32_t, 80> b{};
    auto [ptr, ec] = strings::int_to_chars(b.data(), b.data() + b.size(),
        static_cast<T>(value), base);
    CHECK(ec == std::errc{});
    const std::u32string wide{b.data(), ptr};
    CHECK(wide.size() == narrow.size());
    for (size_t i = 0; i < narrow.size(); ++i)
      CHECK(wide[i] == static_cast<char32_t>(narrow[i]));
    // And parse the wide form back.
    T out{};
    auto [p2, ec2] = strings::int_from_chars(wide.data(),
        wide.data() + wide.size(), out, base);
    CHECK(ec2 == std::errc{});
    CHECK(p2 == wide.data() + wide.size());
    CHECK(out == value);
  };
  for (int base : {2, 10, 16, 36}) {
    widen_check(int64_t{-123456789}, base);
    widen_check(uint64_t{9876543210u}, base);
  }

  // char16_t round-trip.
  {
    std::array<char16_t, 16> b{};
    auto [ptr, ec] = strings::int_to_chars(b.data(), b.data() + b.size(),
        int32_t{-4095}, 16);
    CHECK(ec == std::errc{});
    const std::u16string s{b.data(), ptr};
    CHECK(s == u"-fff");
    int32_t out = 0;
    (void)strings::int_from_chars(s.data(), s.data() + s.size(), out, 16);
    CHECK(out == -4095);
  }
}

#pragma endregion
#pragma region Float

TEST_CASE("FloatCharMatchesStd", "[charconv]") {
  const double vals[] = {0.0, 1.0, -1.0, 3.14159, -2.5, 1e10, 1e-10,
      123456.789};
  for (double v : vals) {
    std::array<char, 64> a{}, b{};
    auto [pa, ea] = strings::float_to_chars(a.data(), a.data() + a.size(), v);
    auto [pb, eb] = std::to_chars(b.data(), b.data() + b.size(), v);
    CHECK(ea == std::errc{});
    CHECK(eb == std::errc{});
    CHECK(std::string{a.data(), pa} == std::string{b.data(), pb});
  }
}

TEST_CASE("FloatRoundTrip", "[charconv]") {
  const double vals[] = {0.0, 1.0, -1.0, 3.141592653589793, -2.5, 1e10, 1e-10,
      6.022e23};

  SECTION("char") {
    for (double v : vals) {
      std::array<char, 64> b{};
      auto [pt, et] =
          strings::float_to_chars(b.data(), b.data() + b.size(), v);
      CHECK(et == std::errc{});
      double out = -1;
      auto [pf, ef] = strings::float_from_chars(b.data(), pt, out);
      CHECK(ef == std::errc{});
      CHECK(pf == pt);
      CHECK(out == v);
    }
  }

  SECTION("char32_t") {
    for (double v : vals) {
      std::array<char32_t, 64> b{};
      auto [pt, et] =
          strings::float_to_chars(b.data(), b.data() + b.size(), v);
      CHECK(et == std::errc{});
      double out = -1;
      auto [pf, ef] = strings::float_from_chars(b.data(), pt, out);
      CHECK(ef == std::errc{});
      CHECK(pf == pt);
      CHECK(out == v);
    }
  }

  SECTION("char8_t (single-byte fast path)") {
    for (double v : vals) {
      std::array<char8_t, 64> b{};
      auto [pt, et] =
          strings::float_to_chars(b.data(), b.data() + b.size(), v);
      CHECK(et == std::errc{});
      double out = -1;
      auto [pf, ef] = strings::float_from_chars(b.data(), pt, out);
      CHECK(ef == std::errc{});
      CHECK(pf == pt);
      CHECK(out == v);
    }
  }
}

TEST_CASE("FloatWidePrefix", "[charconv]") {
  // The number sits at the front of a wide range far longer than the scratch
  // buffer; it still parses, consuming only the number.
  std::u32string s = U"3.5";
  s.append(strings::float_buffer_size * 2, U'x');
  double out = -1;
  auto [ptr, ec] =
      strings::float_from_chars(s.data(), s.data() + s.size(), out);
  CHECK(ec == std::errc{});
  CHECK(out == 3.5);
  CHECK(ptr == s.data() + 3);

  // A single-byte code unit has no scratch buffer, so even a long run of
  // digits parses directly.
  const std::u8string u(strings::float_buffer_size + 1, u8'1');
  double out8 = -1;
  auto [ptr8, ec8] =
      strings::float_from_chars(u.data(), u.data() + u.size(), out8);
  CHECK(ec8 == std::errc{});
  CHECK(out8 > 0);
}

TEST_CASE("FloatPrecisionClip", "[charconv]") {
  // An absurd precision must not overflow the buffer; it is clipped and still
  // produces a valid result.
  std::array<char, 256> b{};
  auto [ptr, ec] = strings::float_to_chars(b.data(), b.data() + b.size(), 1.5,
      std::chars_format::fixed, 100000);
  CHECK(ec == std::errc{});
  const std::string_view s{b.data(), static_cast<size_t>(ptr - b.data())};
  CHECK(s.starts_with("1.5"));
  CHECK(s.size() <= strings::float_buffer_size);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
