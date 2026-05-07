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

#include <map>
#include <set>
#include <vector>

#include "../corvid/containers.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Maps

void FindOptTest_Maps() {
  if (true) {
    const auto key = "key"s;
    const auto value = "value"s;
    using C = std::map<std::string, std::string>;
    C m{{key, value}};
    EXPECT_EQ(*find_opt(m, key), value);
    EXPECT_EQ(find_opt(m, value).value_or_ptr(&key), key);
    EXPECT_TRUE(KeyFindable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::map<std::string_view, int>;
    const auto key = "key"sv;
    const auto value = 42;
    C m{{key, value}};
    EXPECT_EQ(*find_opt(m, key), value);
    EXPECT_EQ(find_opt(m, "missing"sv).value_or(0), 0);
    EXPECT_TRUE(KeyFindable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
  }
  if (true) {
    extensible_arena arena{4096};
    extensible_arena::scope s{arena};
    using C = arena_map<std::string_view, int>;
    const auto key = "key"sv;
    const auto value = 42;
    C m{{key, value}};
    EXPECT_EQ(*find_opt(m, key), value);
    EXPECT_EQ(find_opt(m, "missing"sv).value_or(0), 0);
    EXPECT_TRUE(KeyFindable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
  }
}
#pragma endregion

#pragma region Sets

void FindOptTest_Sets() {
  const auto value = "value"s;
  using C = std::set<std::string>;
  C s{value};
  EXPECT_EQ(*find_opt(s, value), value);
  EXPECT_EQ(find_opt(s, "").value_or("nope"), "nope");
  EXPECT_TRUE(KeyFindable<C>);
  EXPECT_FALSE(RangeWithoutFind<C>);
}
#pragma endregion

#pragma region Vectors

void FindOptTest_Vectors() {
  const auto value = "value"s;
  using C = std::vector<std::string>;
  C s{value};
  EXPECT_EQ(*find_opt(s, value), value);
  EXPECT_EQ(find_opt(s, "").value_or("nope"), "nope");
  EXPECT_FALSE(KeyFindable<C>);
  EXPECT_TRUE(RangeWithoutFind<C>);
}
#pragma endregion

#pragma region Arrays

void FindOptTest_Arrays() {
  int s[]{1, 2, 3, 4};
  using C = decltype(s);
  EXPECT_EQ(*find_opt(s, 3), 3);
  EXPECT_EQ(find_opt(s, 5).value_or(-1), -1);
  EXPECT_FALSE(KeyFindable<C>);
  EXPECT_TRUE(RangeWithoutFind<C>);
}
#pragma endregion

#pragma region Strings

void FindOptTest_Strings() {
  if (true) {
    using C = std::string;
    C s{"value"};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(KeyFindable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::string_view;
    C s{"value"};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(KeyFindable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::vector<char>;
    C s{'v', 'a', 'l', 'u', 'e'};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(KeyFindable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
}
#pragma endregion

MAKE_TEST_LIST(FindOptTest_Maps, FindOptTest_Sets, FindOptTest_Vectors,
    FindOptTest_Arrays, FindOptTest_Strings);

// NOLINTEND(readability-function-cognitive-complexity)
