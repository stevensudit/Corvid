// Corvid: A general-purpose C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022 Steven Sudit
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
#include "pch.h"
#include <LibCorvid/includes/Containers.h>

using namespace std::literals;
using namespace corvid;

TEST(FindPtrTest, Maps) {
  const auto key = "key"s;
  const auto value = "value"s;
  std::map<std::string, std::string> m{{key, value}};
  EXPECT_EQ(*find_opt(m, key), value);
  EXPECT_EQ(find_opt(m, value).value_or_ptr(&key), key);
}

TEST(FindPtrTest, Sets) {
  const auto value = "value"s;
  using C = std::set<std::string>;
  using K = std::string;
  C s{value};
  EXPECT_EQ(*find_opt(s, value), value);
  EXPECT_EQ(find_opt(s, "").value_or("nope"), "nope");
  EXPECT_TRUE((has_find_v<C, K>));
  EXPECT_FALSE((has_find_v<C, C>));
}

TEST(FindPtrTest, Vectors) {
  const auto value = "value"s;
  using C = std::vector<std::string>;
  using K = std::string;
  C s{value};
  EXPECT_EQ(*find_opt(s, value), value);
  EXPECT_EQ(find_opt(s, "").value_or("nope"), "nope");
  EXPECT_FALSE((has_find_v<C, K>));
}

TEST(FindPtrTest, Arrays) {
  using C = int;
  using K = int;
  int s[]{1, 2, 3, 4};
  EXPECT_EQ(*find_opt(s, 3), 3);
  EXPECT_EQ(find_opt(s, 5).value_or(-1), -1);
  EXPECT_FALSE((has_find_v<C, K>));
}

TEST(FindPtrTest, Strings) {
  if (true) {
    using C = std::string;
    using K = char;
    C s{"value"};
    EXPECT_TRUE((has_find_v<C, K>));
    EXPECT_TRUE((has_find_v<C, C>));
    EXPECT_FALSE((has_find_v<C, std::vector<int>>));
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
  }
  if (true) {
    using C = std::string;
    using K = std::string;
    C s{"value"};
    EXPECT_TRUE((has_find_v<C, K>));
    EXPECT_TRUE((has_find_v<C, C>));
    EXPECT_FALSE((has_find_v<C, std::vector<int>>));
    auto z = find_opt(s, "al"s);
    // EXPECT_EQ("no", type_name<decltype(z)>());
    EXPECT_EQ(*find_opt(s, "al"s), 'a');
    // EXPECT_FALSE(contains(s, "al"s));
  }
  if (true) {
    using C = std::string_view;
    using K = char;
    C s{"value"};
    EXPECT_TRUE((has_find_v<C, K>));
    EXPECT_TRUE((has_find_v<C, C>));
    EXPECT_FALSE((has_find_v<C, std::vector<int>>));
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
  }
  if (true) {
    using C = std::vector<char>;
    using K = char;
    C s{'v', 'a', 'l', 'u', 'e'};
    EXPECT_FALSE((has_find_v<C, K>));
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
  }
}

TEST(FindPtrTest, Reversed) {
  if (true) {
    using C = std::vector<char>;
    C s{'v', 'a', 'l', 'u', 'e'};
    char c{};
    for (auto e : s) {
      c = e;
    }
    EXPECT_EQ(c, 'e');
  }
  if (true) {
    using C = std::vector<char>;
    C s{'v', 'a', 'l', 'u', 'e'};
    char c{};
    for (auto e : reversed_range(s)) {
      c = e;
    }
    EXPECT_EQ(c, 'v');
  }
}
