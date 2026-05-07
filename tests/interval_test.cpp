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

#include <algorithm>

#include "../corvid/containers.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::sequence;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Ctors

void Intervals_Ctors() {
  if (true) {
    interval i;
    EXPECT_TRUE(i.empty());
    EXPECT_FALSE(i.invalid());
  }
  if (true) {
    interval i{42};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1U);
    EXPECT_EQ(i.front(), 42);
    EXPECT_EQ(i.back(), 42);
  }
  if (true) {
    interval i{40, 42};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 3U);
    EXPECT_EQ(i.front(), 40);
    EXPECT_EQ(i.back(), 42);
  }
  if (true) {
    // Next line asserts.
    // * interval i{42, 40};
    interval i{40};
    i.min(42);
    EXPECT_TRUE(i.empty());
    EXPECT_TRUE(i.invalid());
  }
}
#pragma endregion

#pragma region Insert

void IntervalTest_Insert() {
  if (true) {
    interval i;
    EXPECT_TRUE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_TRUE(i.insert(0));
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1U);
    EXPECT_EQ(i.front(), 0);
    EXPECT_EQ(i.back(), 0);

    EXPECT_TRUE(i.insert(5));
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 6U);
    EXPECT_EQ(i.front(), 0);
    EXPECT_EQ(i.back(), 5);

    EXPECT_TRUE(i.insert(-5));
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 11U);
    EXPECT_EQ(i.front(), -5);
    EXPECT_EQ(i.back(), 5);

    EXPECT_FALSE(i.insert(-5));
    EXPECT_FALSE(i.insert(0));
    EXPECT_FALSE(i.insert(5));
  }
  if (true) {
    interval i{5};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1U);

    EXPECT_FALSE(i.push_back(0));
    EXPECT_FALSE(i.push_back(5));
    EXPECT_TRUE(i.push_back(6));
    EXPECT_TRUE(i.push_back(7));
    EXPECT_FALSE(i.push_back(6));
    EXPECT_EQ(i.size(), 3U);
    EXPECT_EQ(i.front(), 5);
    EXPECT_EQ(i.back(), 7);

    i.pop_back();
    EXPECT_EQ(i.size(), 2U);
    EXPECT_EQ(i.front(), 5);
    EXPECT_EQ(i.back(), 6);
    i.pop_back(2);
    EXPECT_TRUE(i.empty());
  }
  if (true) {
    interval i{5};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1U);

    EXPECT_FALSE(i.push_front(7));
    EXPECT_FALSE(i.push_front(6));
    EXPECT_FALSE(i.push_front(5));
    EXPECT_TRUE(i.push_front(4));
    EXPECT_TRUE(i.push_front(3));
    EXPECT_FALSE(i.push_front(6));
    EXPECT_EQ(i.size(), 3U);
    EXPECT_EQ(i.front(), 3);
    EXPECT_EQ(i.back(), 5);

    i.pop_front();
    EXPECT_EQ(i.size(), 2U);
    EXPECT_EQ(i.front(), 4);
    EXPECT_EQ(i.back(), 5);
    i.pop_front(2);
    EXPECT_TRUE(i.empty());
  }
}
#pragma endregion

#pragma region ForEach

void IntervalTest_ForEach() {
  auto i = interval{1, 4};

  int64_t c{}, s{};
  for (auto e : i) {
    ++c;
    s += e;
  }

  EXPECT_EQ(c, 4);
  EXPECT_EQ(s, 1 + 2 + 3 + 4);
}
#pragma endregion

#pragma region Reverse

void IntervalTest_Reverse() {
  if (true) {
    auto i = interval{1, 4};

    int64_t c{}, s{}, l{};
    auto b = std::begin(i), e = std::end(i);
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    EXPECT_EQ(c, 4);
    EXPECT_EQ(s, 1 + 2 + 3 + 4);
    EXPECT_EQ(l, 4);
  }
  if (true) {
    auto i = interval{1, 4};

    int64_t c{}, s{}, l{};
    auto b = std::reverse_iterator(std::end(i)),
         e = std::reverse_iterator(std::begin(i));
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    EXPECT_EQ(c, 4);
    EXPECT_EQ(s, 1 + 2 + 3 + 4);
    EXPECT_EQ(l, 1);
  }
  if (true) {
    auto i = interval{1, 4};

    int64_t c{}, s{}, l{};
    auto b = std::rbegin(i), e = std::rend(i);
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    EXPECT_EQ(c, 4);
    EXPECT_EQ(s, 1 + 2 + 3 + 4);
    EXPECT_EQ(l, 1);
  }
}
#pragma endregion

#pragma region MinMax

void IntervalTest_MinMax() {
  auto i = interval{1, 4};

  EXPECT_EQ(i.min(), 1);
  EXPECT_EQ(i.max(), 4);
  i.min(42);
  EXPECT_EQ(i.min(), 42);
  EXPECT_TRUE(i.invalid());
  i.max(64);
  EXPECT_EQ(i.max(), 64);
  EXPECT_FALSE(i.invalid());
}
#pragma endregion

#pragma region CompareAndSwap

void IntervalTest_CompareAndSwap() {
  auto i = interval{1, 4};
  auto j = interval{2, 3};
  EXPECT_TRUE(i == i);
  EXPECT_TRUE(j == j);
  EXPECT_TRUE(i != j);
  EXPECT_EQ(i.back(), 4);
  using std::swap;
  swap(i, j);
  EXPECT_EQ(j.back(), 4);
  i.swap(j);
  EXPECT_EQ(i.back(), 4);
}
#pragma endregion

#pragma region Append

void IntervalTest_Append() {
  if (true) {
    auto i = interval{1, 4};
    using I = decltype(i);
    EXPECT_FALSE(is_pair_v<decltype(i)>);
    EXPECT_TRUE(is_pair_convertible_v<decltype(i)>);

    auto s = ""s;
    I::append_fn(s, i);
    EXPECT_EQ(s, "1, 4");
    s = strings::concat(i);
    EXPECT_EQ(s, "1, 4");

    s = strings::join<strings::join_opt::json>(i);
    EXPECT_EQ(s, "[1, 4]");

    i.clear();
    s = strings::join<strings::join_opt::json>(i);
    EXPECT_EQ(s, "[]");

    // Note: make_interval is tested in bitmask_enum_test.cpp and
    // sequence_enum_test.cpp.
  }
}
#pragma endregion

MAKE_TEST_LIST(Intervals_Ctors, IntervalTest_Insert, IntervalTest_ForEach,
    IntervalTest_Reverse, IntervalTest_MinMax, IntervalTest_CompareAndSwap,
    IntervalTest_Append);

// NOLINTEND(readability-function-cognitive-complexity)
