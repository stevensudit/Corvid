// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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

#include "../corvid/strings/fixed_string.h"
#include "../corvid/strings/fixed_string_utils.h"
#include "AccutestShim.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;

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

void FixedStringTest_General() {
  std::string_view s;
  s = GetFixedString<"abc">();
  EXPECT_EQ(s, "abc"sv);
  constinit static auto ceval = test_ceval();
  EXPECT_EQ(ceval, "abc"sv);
  constinit static auto csplit = test_split();
  EXPECT_EQ(csplit, "def"sv);

  EXPECT_EQ((strings::fixed_split<"abc,def">()),
      (std::array{"abc"sv, "def"sv}));
  EXPECT_EQ((strings::fixed_split<"abc , def">()),
      (std::array{"abc "sv, " def"sv}));
  EXPECT_EQ((strings::fixed_split_trim<"   abc   ,   def   ">()),
      (std::array{"abc"sv, "def"sv}));
  EXPECT_EQ((strings::fixed_split_trim<"   abc   ,    ,  def   ">()),
      (std::array{"abc"sv, ""sv, "def"sv}));
  EXPECT_EQ(
      (strings::fixed_split_trim<"- -- abc  - ,  --  ,  def  -- ", " -">()),
      (std::array{"abc"sv, ""sv, "def"sv}));

  auto cs = test_cstr();
  EXPECT_EQ(cs, "abc"sv);
  EXPECT_EQ(ceval, "abc"_csv);
}

MAKE_TEST_LIST(FixedStringTest_General);
