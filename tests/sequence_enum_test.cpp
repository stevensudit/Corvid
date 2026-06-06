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

#include <cstdint>
#include <map>
#include <set>

#include "../corvid/meta.h"
#include "../corvid/enums.h"
#include "../corvid/strings/utils/enum_conversion.h"
#include "../corvid/containers.h"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::enums;
using namespace corvid::enums::sequence;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

enum class tiger_pick : std::int8_t { eeny, meany, miny, moe };
consteval auto corvid_enum_spec(tiger_pick*) {
  return make_sequence_enum_spec<tiger_pick, "eeny,meany,miny,moe">();
}

enum old_enum : std::uint8_t { old_zero, old_one, old_two, old_three };
enum class new_enum : std::uint8_t { new_zero, new_one, new_two, new_three };

#pragma region Registry

TEST_CASE("Registry", "[SequentialEnumTest]") {
  if (true) { CHECK(strings::enum_as_string(tiger_pick::eeny) == "eeny"); }
}

#pragma endregion
#pragma region Ops

TEST_CASE("Ops", "[SequentialEnumTest]") {
  if (true) {
    CHECK(!tiger_pick{});

    auto e = tiger_pick::eeny;
    auto i = *e;
    CHECK(i == 0);

    // The next line correctly fails because std::byte isn't a registered enum.
    // auto bad = make<std::byte>(23);

    // The next line correctly fails to compile because int isn't an enum.
    // * auto worse = make<int>(23);

    e = e + 1;
    CHECK(e == tiger_pick::meany);

    // Commutative.
    e = 0 + e;

    e += 1;
    CHECK(e == tiger_pick::miny);
    e = tiger_pick::eeny;
    CHECK(++e == tiger_pick::meany);
    CHECK(e == tiger_pick::meany);
    CHECK(e++ == tiger_pick::meany);
    CHECK(e == tiger_pick::miny);

    e = tiger_pick::moe;
    e = e - 1;
    CHECK(e == tiger_pick::miny);

    // Does not compile because subtraction is not commutative.
    // * auto bad = 1 - e;

    e -= 1;
    CHECK(e == tiger_pick::meany);
    e = tiger_pick::moe;
    CHECK(--e == tiger_pick::miny);
    CHECK(e == tiger_pick::miny);
    CHECK(e-- == tiger_pick::miny);
    CHECK(e == tiger_pick::meany);

    // The following shows what happens when wrapping isn't enabled.
    e = tiger_pick::eeny;
    --e;
    CHECK(*e == -1);
  }
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(tiger_pick::eeny) == "eeny");
    CHECK(enum_as_string(tiger_pick::meany) == "meany");
    CHECK(enum_as_string(tiger_pick::miny) == "miny");
    CHECK(enum_as_string(tiger_pick::moe) == "moe");
    CHECK(enum_as_string(tiger_pick(-1)) == "-1");
    CHECK(enum_as_string(tiger_pick(4)) == "4");
  }
}

#pragma endregion

// Range of 0 to 3.
enum class e0_3 : int8_t {};
consteval auto corvid_enum_spec(e0_3*) {
  return make_sequence_enum_spec<e0_3, "a,,c,", wrapclip::limit>();
}

// Range of 10 to 13. Tests non-zero minimums.
enum class e10_13 : int8_t {};
consteval auto corvid_enum_spec(e10_13*) {
  return make_sequence_enum_spec<e10_13, "ten,eleven,twelve,thirteen",
      wrapclip::limit, e10_13{10}>();
}

// Range of -3 to 3. Tests negative minimums.
enum class eneg3_3 : int8_t {};
consteval auto corvid_enum_spec(eneg3_3*) {
  return make_sequence_enum_spec<eneg3_3,
      "neg-three,neg-two,neg-one,zero,one,two,three", wrapclip::limit,
      eneg3_3{-3}>();
}

// Range of 0 to 255. Tests exact fit, unsigned. Enabling wrap has no effect.
enum class e0_255 : uint8_t {};
consteval auto corvid_enum_spec(e0_255*) {
  return make_sequence_enum_spec<e0_255, e0_255{255}>();
}

// Range of -128 to 127. Tests exact fit, signed.
enum class eneg128_127 : int8_t {};
consteval auto corvid_enum_spec(eneg128_127*) {
  return make_sequence_enum_spec<eneg128_127, eneg128_127{127},
      eneg128_127{-128}>();
}

// Range of 0 to 3. Tests int64_t underlying type.
enum class e64_0_3 : int64_t {};
consteval auto corvid_enum_spec(e64_0_3*) {
  return make_sequence_enum_spec<e64_0_3, "alpha,beta,gamma,delta",
      wrapclip::limit>();
}

// Range with large int64_t values near the limits.
enum class e64_large : int64_t {};
consteval auto corvid_enum_spec(e64_large*) {
  return make_sequence_enum_spec<e64_large, "low,mid,high", wrapclip::limit,
      e64_large{1000000000000}>();
}

// Range of 0 to 3. Tests uint64_t underlying type.
enum class eu64_0_3 : uint64_t {};
consteval auto corvid_enum_spec(eu64_0_3*) {
  return make_sequence_enum_spec<eu64_0_3, "one,two,three,four",
      wrapclip::limit>();
}

// Range with large uint64_t values.
enum class eu64_large : uint64_t {};
consteval auto corvid_enum_spec(eu64_large*) {
  return make_sequence_enum_spec<eu64_large, "first,second,third",
      wrapclip::limit, eu64_large{UINT64_C(10000000000000000000)}>();
}

#pragma region MakeSafely

TEST_CASE("MakeSafely", "[SequentialEnumTest]") {
  if (true) {
    e0_3 e;
    e = make_safely<e0_3>(0);
    CHECK(*e == 0);
    e = make_safely<e0_3>(1);
    CHECK(*e == 1);
    e = make_safely<e0_3>(3);
    CHECK(*e == 3);

    e = make_safely<e0_3>(3 + 1);
    CHECK(*e == 0);
    e = make_safely<e0_3>(3 + 2);
    CHECK(*e == 1);
    e = make_safely<e0_3>(3 + 3);
    CHECK(*e == 2);
    e = make_safely<e0_3>(3 + 4);
    CHECK(*e == 3);
    e = make_safely<e0_3>(3 + 5);
    CHECK(*e == 0);
    e = make_safely<e0_3>(3 + 3 * 4);
    CHECK(*e == 3);

    // Note: All of these casts are strictly unnecessary. They're just to
    // suppress spurious warnings of precision lost due to the implicit
    // cast.
    e = make_safely<e0_3>(int8_t(0 - 1));
    CHECK(*e == 3);
    e = make_safely<e0_3>(int8_t(0 - 2));
    CHECK(*e == 2);
    e = make_safely<e0_3>(int8_t(0 - 3));
    CHECK(*e == 1);
    e = make_safely<e0_3>(int8_t(0 - 4));
    CHECK(*e == 0);
    e = make_safely<e0_3>(int8_t(0 - 5));
    CHECK(*e == 3);
    e = make_safely<e0_3>(int8_t(0 - 4 * 4));
    CHECK(*e == 0);
  }
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(e0_3(-1)) == "-1");
    CHECK(enum_as_string(e0_3(0)) == "a");
    CHECK(enum_as_string(e0_3(1)) == "1");
    CHECK(enum_as_string(e0_3(2)) == "c");
    CHECK(enum_as_string(e0_3(3)) == "3");
    CHECK(enum_as_string(e0_3(4)) == "4");
  }
  if (true) {
    e10_13 e;
    e = make_safely<e10_13>(10);
    CHECK(*e == 10);
    e = make_safely<e10_13>(11);
    CHECK(*e == 11);
    e = make_safely<e10_13>(13);
    CHECK(*e == 13);

    e = make_safely<e10_13>(13 + 1);
    CHECK(*e == 10);
    e = make_safely<e10_13>(13 + 2);
    CHECK(*e == 11);
    e = make_safely<e10_13>(13 + 3);
    CHECK(*e == 12);
    e = make_safely<e10_13>(13 + 4);
    CHECK(*e == 13);
    e = make_safely<e10_13>(13 + 5);
    CHECK(*e == 10);
    e = make_safely<e10_13>(13 + 3 * 4);
    CHECK(*e == 13);

    e = make_safely<e10_13>(10 - 1);
    CHECK(*e == 13);
    e = make_safely<e10_13>(10 - 2);
    CHECK(*e == 12);
    e = make_safely<e10_13>(10 - 3);
    CHECK(*e == 11);
    e = make_safely<e10_13>(10 - 4);
    CHECK(*e == 10);
    e = make_safely<e10_13>(10 - 5);
    CHECK(*e == 13);
    e = make_safely<e10_13>(int8_t(10 - 4 * 4));
    CHECK(*e == 10);
  }
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(e10_13(-1)) == "-1");
    CHECK(enum_as_string(e10_13(9)) == "9");
    CHECK(enum_as_string(e10_13(10)) == "ten");
    CHECK(enum_as_string(e10_13(11)) == "eleven");
    CHECK(enum_as_string(e10_13(12)) == "twelve");
    CHECK(enum_as_string(e10_13(13)) == "thirteen");
    CHECK(enum_as_string(e10_13(14)) == "14");
  }
  if (true) {
    eneg3_3 e;
    e = make_safely<eneg3_3>(0);
    CHECK(*e == 0);
    e = make_safely<eneg3_3>(-3);
    CHECK(*e == -3);
    e = make_safely<eneg3_3>(3);
    CHECK(*e == 3);

    e = make_safely<eneg3_3>(3 + 1);
    CHECK(*e == -3);
    e = make_safely<eneg3_3>(3 + 2);
    CHECK(*e == -2);
    e = make_safely<eneg3_3>(3 + 3);
    CHECK(*e == -1);
    e = make_safely<eneg3_3>(3 + 4);
    CHECK(*e == 0);
    e = make_safely<eneg3_3>(3 + 5);
    CHECK(*e == 1);
    e = make_safely<eneg3_3>(3 + 6);
    CHECK(*e == 2);
    e = make_safely<eneg3_3>(3 + 7);
    CHECK(*e == 3);
    e = make_safely<eneg3_3>(3 + 8);
    CHECK(*e == -3);
    e = make_safely<eneg3_3>(3 + 7 * 4);
    CHECK(*e == 3);

    e = make_safely<eneg3_3>(-3 - 1);
    CHECK(*e == 3);
    e = make_safely<eneg3_3>(-3 - 2);
    CHECK(*e == 2);
    e = make_safely<eneg3_3>(-3 - 3);
    CHECK(*e == 1);
    e = make_safely<eneg3_3>(-3 - 4);
    CHECK(*e == 0);
    e = make_safely<eneg3_3>(-3 - 5);
    CHECK(*e == -1);
    e = make_safely<eneg3_3>(-3 - 6);
    CHECK(*e == -2);
    e = make_safely<eneg3_3>(-3 - 7);
    CHECK(*e == -3);
    e = make_safely<eneg3_3>(-3 - 8);
    CHECK(*e == 3);
    e = make_safely<eneg3_3>(-3 - 7 * 4);
    CHECK(*e == -3);
  }
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(eneg3_3(-4)) == "-4");
    CHECK(enum_as_string(eneg3_3(-3)) == "neg-three");
    CHECK(enum_as_string(eneg3_3(-2)) == "neg-two");
    CHECK(enum_as_string(eneg3_3(-1)) == "neg-one");
    CHECK(enum_as_string(eneg3_3(0)) == "zero");
    CHECK(enum_as_string(eneg3_3(1)) == "one");
    CHECK(enum_as_string(eneg3_3(2)) == "two");
    CHECK(enum_as_string(eneg3_3(3)) == "three");
    CHECK(enum_as_string(eneg3_3(4)) == "4");
  }
  if (true) {
    e0_255 e;

    e = make_safely<e0_255>(0);
    CHECK(*e == 0);
    e = make_safely<e0_255>(uint8_t(256));
    CHECK(*e == 0);
    e = make_safely<e0_255>(uint8_t(257));
    CHECK(*e == 1);

    e = make_safely<e0_255>(uint8_t(0 - 2));
    CHECK(*e == 254);
    e = make_safely<e0_255>(uint8_t(255 + 3));
    CHECK(*e == 2);
    e = make_safely<e0_255>(uint8_t(255 + 3 * 256));
    CHECK(*e == 255);

    // Safety is meaningless in this case.
    e = make<e0_255>(0);
    CHECK(*e == 0);
    e = make<e0_255>(uint8_t(256));
    CHECK(*e == 0);
    e = make<e0_255>(uint8_t(257));
    CHECK(*e == 1);

    e = make<e0_255>(uint8_t(0 - 2));
    CHECK(*e == 254);
    e = make<e0_255>(uint8_t(255 + 3));
    CHECK(*e == 2);
    e = make<e0_255>(uint8_t(255 + 3 * 256));
    CHECK(*e == 255);
  }
  if (true) {
    eneg128_127 e;

    e = make_safely<eneg128_127>(0);
    CHECK(*e == 0);
    e = make_safely<eneg128_127>(int8_t(128));
    CHECK(*e == -128);
    e = make_safely<eneg128_127>(int8_t(-129));
    CHECK(*e == 127);

    e = make_safely<eneg128_127>(int8_t(-128 - 2));
    CHECK(*e == 126);
    e = make_safely<eneg128_127>(int8_t(127 + 3));
    CHECK(*e == -126);
    e = make_safely<eneg128_127>(int8_t(127 + 3 * 256));
    CHECK(*e == 127);

    // Safety is meaningless in this case.
    e = make<eneg128_127>(0);
    CHECK(*e == 0);
    e = make<eneg128_127>(int8_t(128));
    CHECK(*e == -128);
    e = make<eneg128_127>(int8_t(-129));
    CHECK(*e == 127);

    e = make<eneg128_127>(int8_t(-128 - 2));
    CHECK(*e == 126);
    e = make<eneg128_127>(int8_t(127 + 3));
    CHECK(*e == -126);
    e = make<eneg128_127>(int8_t(127 + 3 * 256));
    CHECK(*e == 127);
  }
}

#pragma endregion
#pragma region SafeOps

TEST_CASE("SafeOps", "[SequentialEnumTest]") {
  if (true) {
    e0_3 e;
    e = make<e0_3>(0);
    CHECK(*e == 0);
    --e;
    CHECK(*e == 3);
    ++e;
    CHECK(*e == 0);
    e += 4;
    CHECK(*e == 0);
    e += 4 * 4;
    CHECK(*e == 0);
    e -= 4;
    CHECK(*e == 0);
    e -= 4 * 4;
    CHECK(*e == 0);
  }
}

#pragma endregion

enum class tiger_nochoice : std::uint8_t { tiger };
consteval auto corvid_enum_spec(tiger_nochoice*) {
  return make_sequence_enum_spec<tiger_nochoice, tiger_nochoice{}>();
}

#pragma region NoChoice

TEST_CASE("NoChoice", "[SequentialEnumTest]") {
  if (true) {
    auto e = tiger_nochoice::tiger;
    auto n = *e;
    CHECK(n == 0);
  }
}

#pragma endregion

// Range of 0 to 3, but without wrapping.
enum class e0_3unsafe : int8_t {};
consteval auto corvid_enum_spec(e0_3unsafe*) {
  return make_sequence_enum_spec<e0_3unsafe, e0_3unsafe{3}>();
}

#pragma region SubtleBugRepro

TEST_CASE("SubtleBugRepro", "[SequentialEnumTest]") {
  e0_3unsafe e;

  // This is a regression test now.
  e = make_safely<e0_3unsafe>(36);
  CHECK(*e == 0);
  CHECK(*e != 36);
}

#pragma endregion
#pragma region StreamingOut

TEST_CASE("StreamingOut", "[SequentialEnumTest]") {
  CHECK(OStreamable<tiger_pick>);
  if (true) {
    std::stringstream ss;
    int i = *tiger_pick::moe;
    ss << i << std::flush;
    CHECK(ss.str() == "3");
  }

  if (true) {
    std::stringstream ss;
    ss << tiger_pick::moe << std::flush;
    CHECK(ss.str() == "moe");
  }

  if (true) {
    std::stringstream ss;
    ss << extract_field::value << ", " << extract_field::key_value
       << std::flush;
    CHECK(ss.str() == "value, key_value");
  }
}

#pragma endregion

enum class tiger_missing : std::uint8_t { eeny, miny = 2, moe };
consteval auto corvid_enum_spec(tiger_missing*) {
  return make_sequence_enum_spec<tiger_missing, "eeny,,miny,moe">();
}

// Tests empty-string placeholder (whitespace-only element between commas).
enum class tiger_gapped : std::uint8_t { ga, gb, gc, gd };

consteval auto corvid_enum_spec(tiger_gapped*) {
  return make_sequence_enum_spec<tiger_gapped, "ga,,gc,gd">();
}

#pragma region Missing

TEST_CASE("Missing", "[SequentialEnumTest]") {
  if (true) {
    using namespace strings;
    // Hyphen placeholder: numeric value is printed.
    CHECK(enum_as_string(tiger_missing::eeny) == "eeny");
    CHECK(enum_as_string(tiger_missing(1)) == "1");
    CHECK(enum_as_string(tiger_missing::miny) == "miny");
    CHECK(enum_as_string(tiger_missing::moe) == "moe");
    CHECK(enum_as_string(tiger_missing(-1)) == "255");
    CHECK(enum_as_string(tiger_missing(4)) == "4");
  }
  if (true) {
    using namespace strings;
    // Empty-element placeholder: numeric value is printed.
    CHECK(enum_as_string(tiger_gapped(0)) == "ga");
    CHECK(enum_as_string(tiger_gapped(1)) == "1");
    CHECK(enum_as_string(tiger_gapped(2)) == "gc");
    CHECK(enum_as_string(tiger_gapped(3)) == "gd");
    // Asterisk placeholder (e0_3 index 1), question mark placeholder (index
    // 3).
    CHECK(enum_as_string(e0_3(1)) == "1");
    CHECK(enum_as_string(e0_3(3)) == "3");
  }
}

#pragma endregion
#pragma region Intervals

TEST_CASE("Intervals", "[SequentialEnumTest]") {
  if (true) {
    int c{}, s{};
    for (auto e : make_interval<e0_3>()) {
      ++c;
      s += *e;
    }
    CHECK(c == 4);
    CHECK(s == (0 + 1 + 2 + 3));
  }
  if (true) {
    int c{}, s{};
    for (auto e : make_interval<e10_13>()) {
      ++c;
      s += *e;
    }
    CHECK(c == 4);
    CHECK(s == (10 + 11 + 12 + 13));
  }
  if (true) {
    int c{}, s{};
    // This fails with an assertion.
    // * auto bad = make_interval<e0_255>();
    for (auto e : make_interval<e0_255, uint16_t>()) {
      ++c;
      s += *e;
    }
    CHECK(c == 256);
    CHECK(s == 32640);
  }
  if (true) {
    int c{}, s{};
    // This fails with an assertion.
    // * auto bad = make_interval<eneg128_127>();
    for (auto e : make_interval<eneg128_127, int16_t>()) {
      ++c;
      s += *e;
    }
    CHECK(c == 256);
    CHECK(s == -128);
  }
}

#pragma endregion
#pragma region ExtractEnum

TEST_CASE("ExtractEnum", "[SequentialEnumTest]") {
  using namespace corvid::strings;
  if (true) {
    tiger_pick e{};
    std::string_view sv;

    sv = "0";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == tiger_pick::eeny);

    sv = "eeny";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == tiger_pick::eeny);

    sv = "meany";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == tiger_pick::meany);

    sv = "miny";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == tiger_pick::miny);

    sv = "miny";
    e = extract_enum<tiger_pick>(sv).value_or(tiger_pick{-1});
    CHECK(e == tiger_pick::miny);
    CHECK(sv.empty());

    sv = "miny";
    auto opte = parse_enum<tiger_pick>(sv);
    CHECK(opte.value_or(tiger_pick{-1}) == tiger_pick::miny);

    sv = "miny  ";
    opte = parse_enum<tiger_pick>(sv);
    CHECK(opte.value_or(tiger_pick{-1}) == tiger_pick::miny);

    sv = "miny ; ";
    opte = parse_enum<tiger_pick>(sv);
    CHECK_FALSE(opte.has_value());

    sv = "miny";
    e = parse_enum(sv, tiger_pick::moe);
    CHECK(e == tiger_pick::miny);

    sv = "miny ; ";
    e = parse_enum(sv, tiger_pick::moe);
    CHECK(e == tiger_pick::moe);
  }
  if (true) {
    e10_13 e{};
    std::string_view sv;

    sv = "10";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == e10_13{10});

    sv = "13";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == e10_13{13});

    sv = "ten";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == e10_13{10});

    sv = "thirteen";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == e10_13{13});
  }
  if (true) {
    eneg3_3 e{};
    std::string_view sv;

    sv = "-3";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == eneg3_3{-3});

    sv = "neg-three";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == eneg3_3{-3});

    sv = "0";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == eneg3_3{0});

    sv = "zero";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == eneg3_3{0});

    sv = "3";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == eneg3_3{3});

    sv = "three";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == eneg3_3{3});

    sv = "4";
    CHECK_FALSE(extract_enum(e, sv));

    sv = "four";
    CHECK_FALSE(extract_enum(e, sv));
  }
  if (true) {
    old_enum e{};
    std::string_view sv;

    sv = "1";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == old_one);
  }
  if (true) {
    new_enum e{};
    std::string_view sv;

    sv = "1";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == new_enum::new_one);
  }
}

#pragma endregion
#pragma region Int64

TEST_CASE("Int64", "[SequentialEnumTest]") {
  // Test basic operations with int64_t underlying type.
  if (true) {
    e64_0_3 e;
    e = make<e64_0_3>(0);
    CHECK(*e == 0);
    e = make<e64_0_3>(1);
    CHECK(*e == 1);
    e = make<e64_0_3>(3);
    CHECK(*e == 3);

    // Test arithmetic.
    e = make<e64_0_3>(0);
    ++e;
    CHECK(*e == 1);
    --e;
    CHECK(*e == 0);
    e += 2;
    CHECK(*e == 2);
    e -= 1;
    CHECK(*e == 1);
  }
  // Test make_safely wrapping with int64_t.
  if (true) {
    e64_0_3 e;
    e = make_safely<e64_0_3>(0);
    CHECK(*e == 0);
    e = make_safely<e64_0_3>(3);
    CHECK(*e == 3);

    // Wrap around.
    e = make_safely<e64_0_3>(4);
    CHECK(*e == 0);
    e = make_safely<e64_0_3>(5);
    CHECK(*e == 1);
    e = make_safely<e64_0_3>(-1);
    CHECK(*e == 3);
    e = make_safely<e64_0_3>(-2);
    CHECK(*e == 2);
  }
  // Test enum_as_string with int64_t.
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(e64_0_3(0)) == "alpha");
    CHECK(enum_as_string(e64_0_3(1)) == "beta");
    CHECK(enum_as_string(e64_0_3(2)) == "gamma");
    CHECK(enum_as_string(e64_0_3(3)) == "delta");
    CHECK(enum_as_string(e64_0_3(-1)) == "-1");
    CHECK(enum_as_string(e64_0_3(4)) == "4");
  }
  // Test large int64_t values.
  if (true) {
    e64_large e;
    e = make<e64_large>(1000000000000);
    CHECK(*e == 1000000000000);
    e = make<e64_large>(1000000000001);
    CHECK(*e == 1000000000001);
    e = make<e64_large>(1000000000002);
    CHECK(*e == 1000000000002);

    // Test arithmetic with large values.
    e = make<e64_large>(1000000000000);
    ++e;
    CHECK(*e == 1000000000001);
    ++e;
    CHECK(*e == 1000000000002);
  }
  // Test enum_as_string with large int64_t values.
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(e64_large(1000000000000)) == "low");
    CHECK(enum_as_string(e64_large(1000000000001)) == "mid");
    CHECK(enum_as_string(e64_large(1000000000002)) == "high");
    CHECK(enum_as_string(e64_large(999999999999)) == "999999999999");
    CHECK(enum_as_string(e64_large(1000000000003)) == "1000000000003");
  }
  // Test basic operations with uint64_t underlying type.
  if (true) {
    eu64_0_3 e;
    e = make<eu64_0_3>(0);
    CHECK(*e == 0U);
    e = make<eu64_0_3>(1);
    CHECK(*e == 1U);
    e = make<eu64_0_3>(3);
    CHECK(*e == 3U);

    // Test arithmetic.
    e = make<eu64_0_3>(0);
    ++e;
    CHECK(*e == 1U);
    e += 2;
    CHECK(*e == 3U);
  }
  // Test make_safely wrapping with uint64_t.
  if (true) {
    eu64_0_3 e;
    e = make_safely<eu64_0_3>(0);
    CHECK(*e == 0U);
    e = make_safely<eu64_0_3>(3);
    CHECK(*e == 3U);

    // Wrap around.
    e = make_safely<eu64_0_3>(4);
    CHECK(*e == 0U);
    e = make_safely<eu64_0_3>(5);
    CHECK(*e == 1U);
  }
  // Test enum_as_string with uint64_t.
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(eu64_0_3(0)) == "one");
    CHECK(enum_as_string(eu64_0_3(1)) == "two");
    CHECK(enum_as_string(eu64_0_3(2)) == "three");
    CHECK(enum_as_string(eu64_0_3(3)) == "four");
    CHECK(enum_as_string(eu64_0_3(4)) == "4");
  }
  // Test large uint64_t values.
  if (true) {
    eu64_large e;
    e = make<eu64_large>(UINT64_C(10000000000000000000));
    CHECK(*e == UINT64_C(10000000000000000000));
    e = make<eu64_large>(UINT64_C(10000000000000000001));
    CHECK(*e == UINT64_C(10000000000000000001));
    e = make<eu64_large>(UINT64_C(10000000000000000002));
    CHECK(*e == UINT64_C(10000000000000000002));

    // Test arithmetic with large values.
    e = make<eu64_large>(UINT64_C(10000000000000000000));
    ++e;
    CHECK(*e == UINT64_C(10000000000000000001));
    ++e;
    CHECK(*e == UINT64_C(10000000000000000002));
  }
  // Test enum_as_string with large uint64_t values.
  if (true) {
    using namespace strings;
    CHECK((enum_as_string(eu64_large(UINT64_C(10000000000000000000)))) ==
          ("first"));
    CHECK((enum_as_string(eu64_large(UINT64_C(10000000000000000001)))) ==
          ("second"));
    CHECK((enum_as_string(eu64_large(UINT64_C(10000000000000000002)))) ==
          ("third"));
  }
  // Test intervals with int64_t.
  if (true) {
    int c{};
    int64_t s{};
    for (auto e : make_interval<e64_0_3>()) {
      ++c;
      s += *e;
    }
    CHECK(c == 4);
    CHECK(s == (0 + 1 + 2 + 3));
  }
  // Test intervals with uint64_t.
  if (true) {
    int c{};
    uint64_t s{};
    for (auto e : make_interval<eu64_0_3>()) {
      ++c;
      s += *e;
    }
    CHECK(c == 4);
    CHECK(s == (0U + 1U + 2U + 3U));
  }
  // Test extract_enum with int64_t.
  if (true) {
    using namespace corvid::strings;
    e64_0_3 e{};
    std::string_view sv;

    sv = "0";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(*e == 0);

    sv = "alpha";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(*e == 0);

    sv = "delta";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(*e == 3);
  }
  // Test extract_enum with uint64_t.
  if (true) {
    using namespace corvid::strings;
    eu64_0_3 e{};
    std::string_view sv;

    sv = "0";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(*e == 0U);

    sv = "one";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(*e == 0U);

    sv = "four";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(*e == 3U);
  }
  // Test streaming with int64_t.
  if (true) {
    std::stringstream ss;
    ss << e64_0_3(1) << std::flush;
    CHECK(ss.str() == "beta");
  }
  // Test streaming with uint64_t.
  if (true) {
    std::stringstream ss;
    ss << eu64_0_3(2) << std::flush;
    CHECK(ss.str() == "three");
  }
}

#pragma endregion
#pragma region AsView

TEST_CASE("AsView", "[SequentialEnumTest]") {
  if (true) {
    CHECK(enum_as_view(e0_3(0)) == "a");
    CHECK(enum_as_view(e0_3(1)) == "");
    CHECK(enum_as_view(e0_3(2)) == "c");
    CHECK(enum_as_view(e0_3(3)) == "");
    CHECK(enum_as_view(e0_3(4)) == "");
  }

  SECTION("views are null-terminated") {
    // `enum_as_view` returns a `cstring_view`: c_str() stops at this name's
    // own terminator in the packed buffer, not at the next name.
    CHECK(std::string_view{enum_as_view(tiger_pick::eeny).c_str()} == "eeny");
    CHECK(std::string_view{enum_as_view(tiger_pick::moe).c_str()} == "moe");
  }
}

#pragma endregion
#pragma region EnumFindNamed

TEST_CASE("EnumFindNamed", "[SequentialEnumTest]") {
  // Names-only lookup: the matching name view, or empty on a miss. Never
  // interprets numeric text, and is usable in a constant expression.
  static_assert(enum_intern_name<tiger_pick>("eeny") == "eeny");
  static_assert(enum_intern_name<tiger_pick>("moe") == "moe");
  static_assert(enum_intern_name<tiger_pick>("nope").empty());
  static_assert(enum_intern_name<tiger_pick>("").empty());
  static_assert(
      enum_intern_name<tiger_pick>("0").empty()); // not numeric-aware
  CHECK(enum_intern_name<tiger_pick>("meany") == "meany");
  CHECK(enum_intern_name<tiger_pick>("nope").empty());
}

#pragma endregion
#pragma region EnumStringView

// Shows that a bare literal or enum value is validated by the parameter type,
// with no ceremony at the call site.
constexpr std::string_view take_tiger(enum_name<tiger_pick> s) { return s; }

TEST_CASE("EnumStringView", "[SequentialEnumTest]") {
  using tiger_sv = enum_name<tiger_pick>;

  // Bare string literal, validated at compile time (array-ref ctor). Compares
  // equal to the matching view and converts to it implicitly.
  if (true) {
    constexpr tiger_sv s{"meany"};
    static_assert(s == "meany");
    static_assert(std::string_view{s} == "meany");
    CHECK(s == "meany");
    CHECK(std::string_view{s} == "meany");
  }
  // Pointer-and-length ctor: what a string-literal UDL feeds it.
  if (true) {
    constexpr tiger_sv s{"moe", 3};
    static_assert(s == "moe");
    CHECK(s == "moe");
  }
  // From the enum value itself, with its name looked up at compile time.
  if (true) {
    constexpr tiger_sv s{tiger_pick::miny};
    static_assert(s == "miny");
    CHECK(s == "miny");
  }
  // Bare literal and enum value both convert implicitly at the parameter.
  if (true) {
    static_assert(take_tiger("eeny") == "eeny");
    static_assert(take_tiger(tiger_pick::moe) == "moe");
    CHECK(take_tiger("miny") == "miny");
    CHECK(take_tiger(tiger_pick::eeny) == "eeny");
  }
  // `intern` / `try_intern` / `force`: runtime escape hatches that build a
  // view without the `consteval` path. `intern` and `try_intern` keep the
  // registry's canonical copy; `force` keeps the caller's view (which must
  // outlive it).
  if (true) {
    std::string_view name = "meany"; // A runtime value.
    CHECK(tiger_sv::intern(name) == "meany");
    CHECK(tiger_sv::try_intern(name)->as_view() == name);
    CHECK(tiger_sv::force(name) == "meany");

    // `intern`/`try_intern` point into the name table; `force` points at the
    // caller's buffer.
    CHECK(tiger_sv::intern(name).data() ==
          enum_intern_name<tiger_pick>("meany").data());
    CHECK(tiger_sv::try_intern(name)->as_view().data() ==
          enum_intern_name<tiger_pick>("meany").data());
    CHECK(tiger_sv::force(name).data() == name.data());
  }
  // `try_intern` is empty on an unregistered name; `intern` throws; `force`
  // keeps the unregistered bytes verbatim.
  if (true) {
    std::string_view bogus = "notaname";
    CHECK(!tiger_sv::try_intern(bogus));
    CHECK_THROWS(tiger_sv::intern(bogus));
    auto s = tiger_sv::force(bogus);
    CHECK(s == "notaname");
    CHECK(enum_intern_name<tiger_pick>(s).empty());
  }

  // The following correctly fail to compile. The `consteval` constructors'
  // throw makes the construction a non-constant expression when the name is
  // not registered or the enum value has no name:
  // * constexpr tiger_sv bad_name{"mooe"};
  // * constexpr tiger_sv bad_value{tiger_pick(7)};
  // * (void)take_tiger("notaname");
}

#pragma endregion
#pragma region StringViewAndValue

// Shows that a bare literal or enum value carries both name and enum to the
// parameter, validated at compile time.
constexpr tiger_pick take_pair(enum_named_value<tiger_pick> ne) {
  return ne.as_enum();
}

TEST_CASE("StringViewAndValue", "[SequentialEnumTest]") {
  using tiger_nv = enum_named_value<tiger_pick>;

  // From a literal: carries both the validated name and its enum, resolved at
  // compile time.
  if (true) {
    constexpr tiger_nv nv{"meany"};
    static_assert(std::string_view{nv} == "meany");
    static_assert(nv.as_enum() == tiger_pick::meany);
    CHECK(std::string_view{nv} == "meany");
    CHECK(nv.as_enum() == tiger_pick::meany);
  }
  // Pointer-and-length ctor: what a string-literal UDL feeds it.
  if (true) {
    constexpr tiger_nv nv{"moe", 3};
    static_assert(nv.as_enum() == tiger_pick::moe);
    CHECK(std::string_view{nv} == "moe");
  }
  // From the enum value (constexpr ctor, so it also works at runtime).
  if (true) {
    constexpr tiger_nv nv{tiger_pick::miny};
    static_assert(std::string_view{nv} == "miny");
    static_assert(nv.as_enum() == tiger_pick::miny);
    CHECK(nv.as_enum() == tiger_pick::miny);
  }
  // A bare literal and an enum value both convert implicitly at the parameter.
  if (true) {
    static_assert(take_pair("eeny") == tiger_pick::eeny);
    static_assert(take_pair(tiger_pick::moe) == tiger_pick::moe);
    CHECK(take_pair("miny") == tiger_pick::miny);
    CHECK(take_pair(tiger_pick::eeny) == tiger_pick::eeny);
  }
  // `intern` / `try_intern` / `force` at runtime. `intern`/`try_intern` keep
  // the registry's canonical name; `force` keeps the caller's view.
  // `try_intern` returns an optional, empty when it cannot intern.
  if (true) {
    std::string_view name = "meany"; // A runtime value.

    // By name and enum, by name alone, and by enum alone all intern to the
    // same canonical pair.
    for (auto in : {tiger_nv::intern(name, tiger_pick::meany),
             tiger_nv::intern(name), tiger_nv::intern(tiger_pick::meany)})
    {
      CHECK(std::string_view{in} == "meany");
      CHECK(in.as_enum() == tiger_pick::meany);
      CHECK(std::string_view{in}.data() ==
            enum_intern_name<tiger_pick>("meany").data());
    }

    // The `try_` forms succeed for a known name and/or enum.
    CHECK(tiger_nv::try_intern(name, tiger_pick::meany)->as_enum() ==
          tiger_pick::meany);
    CHECK(tiger_nv::try_intern(name)->as_enum() == tiger_pick::meany);
    CHECK(tiger_nv::try_intern(tiger_pick::meany)->as_enum() ==
          tiger_pick::meany);

    // `force` keeps the caller's buffer, not the interned copy.
    auto fo = tiger_nv::force(name, tiger_pick::meany);
    CHECK(std::string_view{fo}.data() == name.data());
  }
  // What cannot be interned: an unnamed enum, a mismatched name/enum pair, or
  // an unregistered name. `try_intern` is empty; `intern` throws.
  if (true) {
    constexpr auto unnamed = tiger_pick(7);
    CHECK(!tiger_nv::try_intern("", unnamed));
    CHECK(!tiger_nv::try_intern("notaname"));
    CHECK(!tiger_nv::try_intern("moe", tiger_pick::meany)); // name != enum
    CHECK_THROWS(tiger_nv::intern("", unnamed));
    CHECK_THROWS(tiger_nv::intern("notaname"));
  }

  // The following correctly fail to compile, with the same validation as the
  // underlying view (unregistered name, or enum value with no name):
  // * constexpr tiger_nv bad_name{"mooe"};
  // * constexpr tiger_nv bad_value{tiger_pick(7)};
}

#pragma endregion
#pragma region Segmented

// Sparse enum registered with the segmented syntax: two runs separated by a
// gap. '|' delimits segments; each segment's first comma-field is its absolute
// start value, the rest are names.
enum class seg_basic : int { a = 0, b = 1, x = 10, y = 11, z = 12 };
consteval auto corvid_enum_spec(seg_basic*) {
  return make_sequence_enum_spec<seg_basic, "0,a,b|10,x,y,z">();
}

// Segmented enum that keeps a small gap inside a segment as a placeholder
// ('-') and a larger gap between segments.
enum class seg_inner : int { p = 3, r = 5, s = 6, far = 20 };
consteval auto corvid_enum_spec(seg_inner*) {
  return make_sequence_enum_spec<seg_inner, "3,p,,r,s|20,far">();
}

// Segmented enum with a negative start, exercising signed underlying math.
enum class seg_neg : std::int8_t { lo = -5, lo2 = -4, hi = 7, hi2 = 8 };
consteval auto corvid_enum_spec(seg_neg*) {
  return make_sequence_enum_spec<seg_neg, "-5,lo,lo2|7,hi,hi2">();
}

// Three ascending segments exercise the running offset across more than two
// runs, including a middle segment with a single name. Segments must ascend
// and be separated by more than one value; out-of-order, overlapping, or
// too-close runs are compile-time errors, e.g.
// * make_sequence_enum_spec<seg_three, "10,x,y|0,a,b">(); // not ascending
// * make_sequence_enum_spec<seg_three, "0,a,b|3,c">(); // gap of 1; fold into
//   a placeholder instead: "0,a,b,-,c"
enum class seg_three : int { a = 0, b = 1, m = 5, x = 10, y = 11 };
consteval auto corvid_enum_spec(seg_three*) {
  return make_sequence_enum_spec<seg_three, "0,a,b|5,m|10,x,y">();
}

TEST_CASE("Segmented", "[SequentialEnumTest]") {
  using namespace corvid::strings;

  SECTION("Forward, reverse, and derived range") {
    // Min and max span the segments; the gap lies within the range.
    static_assert(*min_value<seg_basic>() == 0);
    static_assert(*max_value<seg_basic>() == 12);
    CHECK(range_length<seg_basic>() == 13); // [0, 12], gap included

    // Forward: named values resolve; values in or past the gap print numbers.
    CHECK(enum_as_string(seg_basic::a) == "a");
    CHECK(enum_as_string(seg_basic::b) == "b");
    CHECK(enum_as_string(seg_basic::x) == "x");
    CHECK(enum_as_string(seg_basic::z) == "z");
    CHECK(enum_as_string(seg_basic(5)) == "5");   // between segments
    CHECK(enum_as_string(seg_basic(13)) == "13"); // past the last segment

    // Reverse (names only): a hit returns the value, a miss is empty, and
    // numeric text is never matched. Usable in a constant expression.
    static_assert(enum_intern_name<seg_basic>("a") == "a");
    static_assert(enum_intern_name<seg_basic>("z") == "z");
    static_assert(enum_intern_name<seg_basic>("q").empty());
    static_assert(enum_intern_name<seg_basic>("5").empty());
    CHECK(*enum_find_by_name<seg_basic>("a") == seg_basic::a);
    CHECK(*enum_find_by_name<seg_basic>("z") == seg_basic::z);
    CHECK(!enum_find_by_name<seg_basic>("q"));

    // Numeric-aware parse still reaches gap values.
    CHECK(parse_enum<seg_basic>("x") == seg_basic::x);
    CHECK(parse_enum<seg_basic>("5") == seg_basic(5));
  }

  SECTION("Placeholder inside a segment") {
    static_assert(*min_value<seg_inner>() == 3);
    static_assert(*max_value<seg_inner>() == 20);

    CHECK(enum_as_string(seg_inner::p) == "p");
    CHECK(enum_as_string(seg_inner(4)) == "4"); // in-segment placeholder
    CHECK(enum_as_string(seg_inner::r) == "r");
    CHECK(enum_as_string(seg_inner::s) == "s");
    CHECK(enum_as_string(seg_inner::far) == "far");
    CHECK(enum_as_string(seg_inner(10)) == "10"); // between segments

    CHECK(*enum_find_by_name<seg_inner>("p") == seg_inner::p);
    CHECK(*enum_find_by_name<seg_inner>("far") == seg_inner::far);
    CHECK(!enum_find_by_name<seg_inner>("-")); // placeholder is not a name
  }

  SECTION("Negative start") {
    static_assert(*min_value<seg_neg>() == -5);
    static_assert(*max_value<seg_neg>() == 8);

    CHECK(enum_as_string(seg_neg::lo) == "lo");
    CHECK(enum_as_string(seg_neg::lo2) == "lo2");
    CHECK(enum_as_string(seg_neg::hi) == "hi");
    CHECK(enum_as_string(seg_neg::hi2) == "hi2");
    CHECK(enum_as_string(seg_neg(0)) == "0"); // between segments

    CHECK(*enum_find_by_name<seg_neg>("lo") == seg_neg::lo);
    CHECK(*enum_find_by_name<seg_neg>("hi2") == seg_neg::hi2);
  }

  SECTION("Three ascending segments") {
    static_assert(*min_value<seg_three>() == 0);
    static_assert(*max_value<seg_three>() == 11);

    CHECK(enum_as_string(seg_three::a) == "a");
    CHECK(enum_as_string(seg_three::m) == "m"); // lone-name middle segment
    CHECK(enum_as_string(seg_three::y) == "y");
    CHECK(enum_as_string(seg_three(3)) == "3"); // between segments 0 and 1
    CHECK(enum_as_string(seg_three(7)) == "7"); // between segments 1 and 2

    CHECK(*enum_find_by_name<seg_three>("a") == seg_three::a);
    CHECK(*enum_find_by_name<seg_three>("m") == seg_three::m);
    CHECK(*enum_find_by_name<seg_three>("y") == seg_three::y);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
