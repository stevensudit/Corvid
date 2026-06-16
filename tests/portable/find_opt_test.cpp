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

#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("FindOptTest_Maps", "[find_opt][maps]") {
  SECTION("std::map of string to string") {
    const auto key = "key"s;
    const auto value = "value"s;
    using C = std::map<std::string, std::string>;
    C m{{key, value}};
    CHECK(*find_opt(m, key) == value);
    CHECK(find_opt(m, value).value_or_ptr(&key) == key);
    CHECK(KeyFindable<C>);
    CHECK_FALSE(RangeWithoutFind<C>);
  }
  SECTION("std::map of string_view to int") {
    using C = std::map<std::string_view, int>;
    const auto key = "key"sv;
    const auto value = 42;
    C m{{key, value}};
    CHECK(*find_opt(m, key) == value);
    CHECK(find_opt(m, "missing"sv).value_or(0) == 0);
    CHECK(KeyFindable<C>);
    CHECK_FALSE(RangeWithoutFind<C>);
  }
  SECTION("arena_map of string_view to int") {
    extensible_arena arena{4096};
    extensible_arena::scope s{arena};
    using C = arena_map<std::string_view, int>;
    const auto key = "key"sv;
    const auto value = 42;
    C m{{key, value}};
    CHECK(*find_opt(m, key) == value);
    CHECK(find_opt(m, "missing"sv).value_or(0) == 0);
    CHECK(KeyFindable<C>);
    CHECK_FALSE(RangeWithoutFind<C>);
  }
}

TEST_CASE("FindOptTest_Sets", "[find_opt][sets]") {
  const auto value = "value"s;
  using C = std::set<std::string>;
  C s{value};
  CHECK(*find_opt(s, value) == value);
  CHECK(find_opt(s, "").value_or("nope") == "nope");
  CHECK(KeyFindable<C>);
  CHECK_FALSE(RangeWithoutFind<C>);
}

TEST_CASE("FindOptTest_Vectors", "[find_opt][vectors]") {
  const auto value = "value"s;
  using C = std::vector<std::string>;
  C s{value};
  CHECK(*find_opt(s, value) == value);
  CHECK(find_opt(s, "").value_or("nope") == "nope");
  CHECK_FALSE(KeyFindable<C>);
  CHECK(RangeWithoutFind<C>);
}

TEST_CASE("FindOptTest_Arrays", "[find_opt][arrays]") {
  int s[]{1, 2, 3, 4};
  using C = decltype(s);
  CHECK(*find_opt(s, 3) == 3);
  CHECK(find_opt(s, 5).value_or(-1) == -1);
  CHECK_FALSE(KeyFindable<C>);
  CHECK(RangeWithoutFind<C>);
}

TEST_CASE("FindOptTest_Strings", "[find_opt][strings]") {
  SECTION("std::string") {
    using C = std::string;
    C s{"value"};
    CHECK(*find_opt(s, 'a') == 'a');
    CHECK_FALSE(contains(s, 'z'));
    CHECK_FALSE(KeyFindable<C>);
    CHECK(RangeWithoutFind<C>);
  }
  SECTION("std::string_view") {
    using C = std::string_view;
    C s{"value"};
    CHECK(*find_opt(s, 'a') == 'a');
    CHECK_FALSE(contains(s, 'z'));
    CHECK_FALSE(KeyFindable<C>);
    CHECK(RangeWithoutFind<C>);
  }
  SECTION("std::vector of char") {
    using C = std::vector<char>;
    C s{'v', 'a', 'l', 'u', 'e'};
    CHECK(*find_opt(s, 'a') == 'a');
    CHECK_FALSE(contains(s, 'z'));
    CHECK_FALSE(KeyFindable<C>);
    CHECK(RangeWithoutFind<C>);
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
