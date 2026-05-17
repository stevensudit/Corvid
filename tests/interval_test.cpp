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
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::sequence;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(readability-function-size)

#pragma region Ctors

TEST_CASE("Intervals_Ctors", "[Intervals]") {
  if (true) {
    interval i;
    CHECK((i.empty()));
    CHECK_FALSE((i.invalid()));
  }
  if (true) {
    interval i{42};
    CHECK_FALSE((i.empty()));
    CHECK_FALSE((i.invalid()));
    CHECK((i.size()) == (1U));
    CHECK((i.front()) == (42));
    CHECK((i.back()) == (42));
  }
  if (true) {
    interval i{40, 42};
    CHECK_FALSE((i.empty()));
    CHECK_FALSE((i.invalid()));
    CHECK((i.size()) == (3U));
    CHECK((i.front()) == (40));
    CHECK((i.back()) == (42));
  }
  if (true) {
    // Next line asserts.
    // * interval i{42, 40};
    interval i{40};
    i.min(42);
    CHECK((i.empty()));
    CHECK((i.invalid()));
  }
}
#pragma endregion

#pragma region Insert

TEST_CASE("IntervalTest_Insert", "[IntervalTest]") {
  if (true) {
    interval i;
    CHECK((i.empty()));
    CHECK_FALSE((i.invalid()));
    CHECK((i.insert(0)));
    CHECK_FALSE((i.empty()));
    CHECK_FALSE((i.invalid()));
    CHECK((i.size()) == (1U));
    CHECK((i.front()) == (0));
    CHECK((i.back()) == (0));

    CHECK((i.insert(5)));
    CHECK_FALSE((i.empty()));
    CHECK_FALSE((i.invalid()));
    CHECK((i.size()) == (6U));
    CHECK((i.front()) == (0));
    CHECK((i.back()) == (5));

    CHECK((i.insert(-5)));
    CHECK_FALSE((i.empty()));
    CHECK_FALSE((i.invalid()));
    CHECK((i.size()) == (11U));
    CHECK((i.front()) == (-5));
    CHECK((i.back()) == (5));

    CHECK_FALSE((i.insert(-5)));
    CHECK_FALSE((i.insert(0)));
    CHECK_FALSE((i.insert(5)));
  }
  if (true) {
    interval i{5};
    CHECK_FALSE((i.empty()));
    CHECK_FALSE((i.invalid()));
    CHECK((i.size()) == (1U));

    CHECK_FALSE((i.push_back(0)));
    CHECK_FALSE((i.push_back(5)));
    CHECK((i.push_back(6)));
    CHECK((i.push_back(7)));
    CHECK_FALSE((i.push_back(6)));
    CHECK((i.size()) == (3U));
    CHECK((i.front()) == (5));
    CHECK((i.back()) == (7));

    i.pop_back();
    CHECK((i.size()) == (2U));
    CHECK((i.front()) == (5));
    CHECK((i.back()) == (6));
    i.pop_back(2);
    CHECK((i.empty()));
  }
  if (true) {
    interval i{5};
    CHECK_FALSE((i.empty()));
    CHECK_FALSE((i.invalid()));
    CHECK((i.size()) == (1U));

    CHECK_FALSE((i.push_front(7)));
    CHECK_FALSE((i.push_front(6)));
    CHECK_FALSE((i.push_front(5)));
    CHECK((i.push_front(4)));
    CHECK((i.push_front(3)));
    CHECK_FALSE((i.push_front(6)));
    CHECK((i.size()) == (3U));
    CHECK((i.front()) == (3));
    CHECK((i.back()) == (5));

    i.pop_front();
    CHECK((i.size()) == (2U));
    CHECK((i.front()) == (4));
    CHECK((i.back()) == (5));
    i.pop_front(2);
    CHECK((i.empty()));
  }
}
#pragma endregion

#pragma region ForEach

TEST_CASE("IntervalTest_ForEach", "[IntervalTest]") {
  auto i = interval{1, 4};

  int64_t c{};
  int64_t s{};
  for (auto e : i) {
    ++c;
    s += e;
  }

  CHECK((c) == (4));
  CHECK((s) == (1 + 2 + 3 + 4));
}
#pragma endregion

#pragma region Reverse

TEST_CASE("IntervalTest_Reverse", "[IntervalTest]") {
  if (true) {
    auto i = interval{1, 4};

    int64_t c{};
    int64_t s{};
    int64_t l{};
    auto b = std::begin(i);
    auto e = std::end(i);
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    CHECK((c) == (4));
    CHECK((s) == (1 + 2 + 3 + 4));
    CHECK((l) == (4));
  }
  if (true) {
    auto i = interval{1, 4};

    int64_t c{};
    int64_t s{};
    int64_t l{};
    auto b = std::reverse_iterator(std::end(i));
    auto e = std::reverse_iterator(std::begin(i));
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    CHECK((c) == (4));
    CHECK((s) == (1 + 2 + 3 + 4));
    CHECK((l) == (1));
  }
  if (true) {
    auto i = interval{1, 4};

    int64_t c{};
    int64_t s{};
    int64_t l{};
    auto b = std::rbegin(i);
    auto e = std::rend(i);
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    CHECK((c) == (4));
    CHECK((s) == (1 + 2 + 3 + 4));
    CHECK((l) == (1));
  }
}
#pragma endregion

#pragma region MinMax

TEST_CASE("IntervalTest_MinMax", "[IntervalTest]") {
  auto i = interval{1, 4};

  CHECK((i.min()) == (1));
  CHECK((i.max()) == (4));
  i.min(42);
  CHECK((i.min()) == (42));
  CHECK((i.invalid()));
  i.max(64);
  CHECK((i.max()) == (64));
  CHECK_FALSE((i.invalid()));
}
#pragma endregion

#pragma region CompareAndSwap

TEST_CASE("IntervalTest_CompareAndSwap", "[IntervalTest]") {
  auto i = interval{1, 4};
  auto j = interval{2, 3};
  CHECK((i == i));
  CHECK((j == j));
  CHECK((i != j));
  CHECK((i.back()) == (4));
  using std::swap;
  swap(i, j);
  CHECK((j.back()) == (4));
  i.swap(j);
  CHECK((i.back()) == (4));
}
#pragma endregion

#pragma region Append

TEST_CASE("IntervalTest_Append", "[IntervalTest]") {
  if (true) {
    auto i = interval{1, 4};
    using I = decltype(i);
    CHECK_FALSE((is_pair_v<decltype(i)>));
    CHECK((is_pair_convertible_v<decltype(i)>));

    auto s = ""s;
    I::append_fn(s, i);
    CHECK((s) == ("1, 4"));
    s = strings::concat(i);
    CHECK((s) == ("1, 4"));

    s = strings::join<strings::join_opt::json>(i);
    CHECK((s) == ("[1, 4]"));

    i.clear();
    s = strings::join<strings::join_opt::json>(i);
    CHECK((s) == ("[]"));

    // Note: make_interval is tested in bitmask_enum_test.cpp and
    // sequence_enum_test.cpp.
  }
}
#pragma endregion

// NOLINTEND(readability-function-size)
// NOLINTEND(readability-function-cognitive-complexity)
