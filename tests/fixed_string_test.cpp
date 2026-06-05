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

#include <sstream>

#include "../corvid/strings/core/fixed_string.h"
#include "../corvid/strings/core/fixed_string_utils.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

template<strings::fixed_string W>
constexpr std::string_view GetFixedString() {
  return W;
}

template<strings::fixed_string W>
constexpr std::string_view GetSecondString() {
  return strings::fixed_split<W>()[1];
}

template<strings::fixed_string W>
constexpr cstring_view GetFixedCString() {
  return W.cview();
}

consteval auto test_ceval() { return GetFixedString<"abc">(); }

consteval auto test_split() { return GetSecondString<"abc,def">(); }

consteval auto test_cstr() { return GetFixedCString<"abc">(); }

template<strings::fixed_string D>
consteval bool CanSplit() {
  if constexpr (D.view().size() == 0) {
    return false;
  } else {
    static_cast<void>(strings::fixed_split<"a,b", D>());
    return true;
  }
}

static_assert(CanSplit<",">());
static_assert(!CanSplit<"">());

#pragma region General

TEST_CASE("General", "[FixedStringTest]") {
  std::string_view s;
  s = GetFixedString<"abc">();
  CHECK(s == "abc"sv);
  constinit static auto ceval = test_ceval();
  CHECK(ceval == "abc"sv);
  constinit static auto csplit = test_split();
  CHECK(csplit == "def"sv);

  CHECK(((strings::fixed_split<"abc,def">())) ==
        ((std::array{"abc"sv, "def"sv})));
  CHECK(((strings::fixed_split<"abc , def">())) ==
        ((std::array{"abc "sv, " def"sv})));
  CHECK(((strings::fixed_split_trim<"   abc   ,   def   ">())) ==
        ((std::array{"abc"sv, "def"sv})));
  CHECK(((strings::fixed_split_trim<"   abc   ,    ,  def   ">())) ==
        ((std::array{"abc"sv, ""sv, "def"sv})));
  CHECK(
      (strings::fixed_split_trim<"- -- abc  - ,  --  ,  def  -- ", " -">()) ==
      std::array{"abc"sv, ""sv, "def"sv});

  auto cs = test_cstr();
  CHECK(cs == "abc"sv);
  CHECK(ceval == "abc"_csv);
}

#pragma endregion
#pragma region Constructors

TEST_CASE("Constructors", "[FixedStringTest]") {
  SECTION("fixed_string is the char alias") {
    STATIC_REQUIRE(std::is_same_v<strings::fixed_string<3>,
        strings::basic_fixed_string<char, 3>>);
  }

  SECTION("literal ctor deduces CharT and length") {
    constexpr strings::basic_fixed_string fs{"abc"};
    STATIC_REQUIRE(std::is_same_v<std::remove_const_t<decltype(fs)>,
        strings::basic_fixed_string<char, 3>>);
    STATIC_REQUIRE(fs.view() == "abc"sv);
    CHECK(fs.view() == "abc"sv);
    CHECK(fs.c_str() == "abc"sv);
  }

  SECTION("literal ctor generalizes over CharT") {
    constexpr strings::basic_fixed_string fs{L"hello"};
    STATIC_REQUIRE(std::is_same_v<std::remove_const_t<decltype(fs)>,
        strings::basic_fixed_string<wchar_t, 5>>);
    CHECK(fs.view() == L"hello"sv);
  }

  SECTION("pointer ctor with size tag") {
    constexpr const char* p = "abcdef";
    constexpr strings::basic_fixed_string<char, 3> fs{p,
        std::integral_constant<std::size_t, 3>{}};
    STATIC_REQUIRE(fs.view() == "abc"sv);
    CHECK(fs.view() == "abc"sv);
  }

  SECTION("pointer ctor deduces CharT and length") {
    constexpr const char* p = "abcdef";
    constexpr strings::basic_fixed_string fs{p,
        std::integral_constant<std::size_t, 3>{}};
    STATIC_REQUIRE(std::is_same_v<std::remove_const_t<decltype(fs)>,
        strings::basic_fixed_string<char, 3>>);
    STATIC_REQUIRE(fs.view() == "abc"sv);
  }

  SECTION("character pack ctor") {
    constexpr strings::basic_fixed_string<char, 3> fs{'a', 'b', 'c'};
    STATIC_REQUIRE(fs.view() == "abc"sv);
    CHECK(fs.view() == "abc"sv);
  }

  SECTION("character pack ctor deduces CharT and length") {
    constexpr strings::basic_fixed_string fs{'a', 'b', 'c'};
    STATIC_REQUIRE(std::is_same_v<std::remove_const_t<decltype(fs)>,
        strings::basic_fixed_string<char, 3>>);
    STATIC_REQUIRE(fs.view() == "abc"sv);
  }
}

#pragma endregion
#pragma region Accessors

TEST_CASE("Accessors", "[FixedStringTest]") {
  SECTION("size, data, and indexing") {
    constexpr strings::basic_fixed_string fs{"abc"};
    STATIC_REQUIRE(!fs.empty());
    STATIC_REQUIRE(fs.size() == 3);
    STATIC_REQUIRE(fs.data() == fs.c_str());
    STATIC_REQUIRE(fs[0] == 'a');
    STATIC_REQUIRE(fs[2] == 'c');
    CHECK(std::string_view{fs.data(), fs.size()} == "abc"sv);
  }

  SECTION("empty") {
    constexpr strings::basic_fixed_string fs{""};
    STATIC_REQUIRE(fs.empty());
    STATIC_REQUIRE(fs.size() == 0);
  }

  SECTION("iteration spans the value, not the terminator") {
    constexpr strings::basic_fixed_string fs{"abc"};
    STATIC_REQUIRE(fs.end() - fs.begin() == 3);
    STATIC_REQUIRE(fs.begin() == fs.cbegin());
    STATIC_REQUIRE(fs.end() == fs.cend());
    CHECK(std::string(fs.begin(), fs.end()) == "abc");
  }
}

#pragma endregion
#pragma region Operations

TEST_CASE("Operations", "[FixedStringTest]") {
  SECTION("concatenation grows the length") {
    constexpr auto fs =
        strings::basic_fixed_string{"ab"} + strings::basic_fixed_string{"cde"};
    STATIC_REQUIRE(std::is_same_v<std::remove_const_t<decltype(fs)>,
        strings::basic_fixed_string<char, 5>>);
    STATIC_REQUIRE(fs.view() == "abcde"sv);
    CHECK(fs.view() == "abcde"sv);
  }

  SECTION("equality, same length") {
    STATIC_REQUIRE(
        strings::basic_fixed_string{"abc"} ==
        strings::basic_fixed_string{"abc"});
    STATIC_REQUIRE(
        strings::basic_fixed_string{"abc"} !=
        strings::basic_fixed_string{"abd"});
  }

  SECTION("equality, different length is never equal") {
    STATIC_REQUIRE(
        strings::basic_fixed_string{"abc"} !=
        strings::basic_fixed_string{"abcd"});
  }

  SECTION("ordering") {
    STATIC_REQUIRE(
        (strings::basic_fixed_string{"abc"} <=>
            strings::basic_fixed_string{"abd"}) < 0);
    STATIC_REQUIRE(
        (strings::basic_fixed_string{"abc"} <=>
            strings::basic_fixed_string{"ab"}) > 0);
  }

  SECTION("stream insertion") {
    std::ostringstream os;
    os << strings::basic_fixed_string{"abc"};
    CHECK(os.str() == "abc");
  }
}

#pragma endregion
#pragma region fixed_replaced

TEST_CASE("fixed_replaced", "[FixedStringTest]") {
  SECTION("swaps every occurrence, preserving length") {
    constexpr auto r = strings::fixed_replaced<"a,b,c", ',', '|'>();
    STATIC_REQUIRE(std::is_same_v<std::remove_const_t<decltype(r)>,
        strings::basic_fixed_string<char, 5>>);
    STATIC_REQUIRE(r.view() == "a|b|c"sv);
  }

  SECTION("delimiter to null gives null-delimited storage") {
    constexpr auto r = strings::fixed_replaced<"ab,cd", ',', '\0'>();
    STATIC_REQUIRE(r.view() == std::string_view{"ab\0cd", 5});
    // A const char* read of the storage stops at the embedded null.
    STATIC_REQUIRE(std::string_view{r.c_str()} == "ab"sv);
  }

  SECTION("no match leaves the string unchanged") {
    constexpr auto r = strings::fixed_replaced<"abc", 'z', 'y'>();
    STATIC_REQUIRE(r.view() == "abc"sv);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
