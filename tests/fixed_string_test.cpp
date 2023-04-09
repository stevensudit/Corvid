// Corvid20: A general-purpose C++ 20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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

#include "../includes/strings/fixed_string.h"
#include "AccutestShim.h"

using namespace std::literals;
using namespace corvid;

template<strings::fixed_string W>
constexpr std::string_view GetFixedString() {
  return W;
}

template<strings::fixed_string W>
constexpr std::string_view GetSecondString() {
  return strings::fixed_split<W>()[1];
}

consteval auto test_ceval() { return GetFixedString<"abc">(); }

consteval auto test_split() { return GetSecondString<"abc,def">(); }

void FixedStringTest_General() {
  std::string_view s;
  s = GetFixedString<"abc">();
  EXPECT_EQ(s, "abc"sv);
  constinit static auto ceval = test_ceval();
  EXPECT_EQ(ceval, "abc"sv);
  constinit static auto csplit = test_split();
  EXPECT_EQ(csplit, "def"sv);

  EXPECT_EQ((std::array{"abc"sv, "def"sv}),
      (strings::fixed_split<"abc,def">()));
  EXPECT_EQ((std::array{"abc "sv, " def"sv}),
      (strings::fixed_split<"abc , def">()));
  EXPECT_EQ((std::array{"abc"sv, "def"sv}),
      (strings::fixed_split_trim<"   abc   ,   def   ">()));
  EXPECT_EQ((std::array{"abc"sv, ""sv, "def"sv}),
      (strings::fixed_split_trim<"   abc   ,    ,  def   ">()));
  EXPECT_EQ((std::array{"abc"sv, ""sv, "def"sv}),
      (strings::fixed_split_trim<"- -- abc  - ,  --  ,  def  -- ", " -">()));
}

MAKE_TEST_LIST(FixedStringTest_General);