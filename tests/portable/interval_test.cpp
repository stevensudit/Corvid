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
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <ranges>
#include <type_traits>
#include <vector>

#include "corvid/containers.h"
#include "corvid/enums.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::sequence;

// A registered sequence enum, to check that an interval of enums prints names
// in regular mode and the underlying numbers in debug.
enum class hue : std::uint8_t { red, green, blue };
consteval auto corvid_enum_spec(hue*) {
  return corvid::enums::sequence::make_sequence_enum_spec<hue,
      "red,green,blue">();
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(readability-function-size)

#pragma region Ctors

TEST_CASE("Ctors", "[Intervals]") {
  if (true) {
    interval i;
    CHECK(i.empty());
    CHECK(i.size() == 0U);
  }
  if (true) {
    interval i{42};
    CHECK_FALSE(i.empty());
    CHECK(i.size() == 1U);
    CHECK(i.front() == 42);
    CHECK(i.back() == 42);
  }
  if (true) {
    interval i{40, 42};
    CHECK_FALSE(i.empty());
    CHECK(i.size() == 3U);
    CHECK(i.front() == 40);
    CHECK(i.back() == 42);
  }
  if (true) {
    // Reversed bounds are legal and read as empty, whether constructed that
    // way or reversed through the raw setter.
    CHECK(interval{42, 40}.empty());
    interval i{40};
    i.min(42);
    CHECK(i.empty());
  }
  if (true) {
    // Mixed signedness (or a narrower `U`) is rejected at compile time:
    // * interval<int8_t, uint8_t> bad;
    // * interval<int16_t, int8_t> bad;
    static_assert(std::is_same_v<decltype(interval{1, 4}), interval<int>>);
  }
  if (true) {
    // The closed storage reaches edge to edge; the old half-open storage
    // could not represent an interval ending at the top of `U`.
    constexpr auto top = std::numeric_limits<int64_t>::max();
    constexpr auto bottom = std::numeric_limits<int64_t>::min();
    interval full{bottom, top};
    CHECK_FALSE(full.empty());
    CHECK(full.front() == bottom);
    CHECK(full.back() == top);
    // One short of the full span is the largest representable size. The
    // arithmetic is exact even in constant evaluation, where any signed
    // overflow would refuse to compile.
    static_assert(
        interval{bottom + 1, top}.size() ==
        std::numeric_limits<size_t>::max());
    static_assert(
        interval{bottom, top - 1}.size() ==
        std::numeric_limits<size_t>::max());
    // The full span's count is one past `size_type`, so `size` wraps to 0 by
    // documented exception while `empty` stays false.
    static_assert(interval{bottom, top}.size() == 0UZ);
    static_assert(!interval{bottom, top}.empty());
    CHECK(interval<>::max_size() == std::numeric_limits<size_t>::max());
    // For a narrower `U`, `max_size` is the exact full-span count.
    static_assert(interval<hue>::max_size() == 256UZ);
  }
}
#pragma endregion

#pragma region Insert

TEST_CASE("Insert", "[IntervalTest]") {
  if (true) {
    interval i;
    CHECK(i.empty());
    CHECK(i.insert(0));
    CHECK_FALSE(i.empty());
    CHECK(i.size() == 1U);
    CHECK(i.front() == 0);
    CHECK(i.back() == 0);

    CHECK(i.insert(5));
    CHECK_FALSE(i.empty());
    CHECK(i.size() == 6U);
    CHECK(i.front() == 0);
    CHECK(i.back() == 5);

    CHECK(i.insert(-5));
    CHECK_FALSE(i.empty());
    CHECK(i.size() == 11U);
    CHECK(i.front() == -5);
    CHECK(i.back() == 5);

    CHECK_FALSE(i.insert(-5));
    CHECK_FALSE(i.insert(0));
    CHECK_FALSE(i.insert(5));
  }
  if (true) {
    // The extremes insert cleanly; these were the old corruption cases.
    interval i;
    CHECK(i.insert(std::numeric_limits<int64_t>::max()));
    CHECK(i.size() == 1U);
    CHECK_FALSE(i.insert(std::numeric_limits<int64_t>::max()));
    CHECK(i.insert(std::numeric_limits<int64_t>::min()));
    CHECK(i.front() == std::numeric_limits<int64_t>::min());
    CHECK(i.back() == std::numeric_limits<int64_t>::max());
    CHECK_FALSE(i.insert(0));

    interval<int64_t> j{5};
    CHECK(j.push_back(std::numeric_limits<int64_t>::max()));
    CHECK(j.back() == std::numeric_limits<int64_t>::max());
    CHECK(j.push_front(std::numeric_limits<int64_t>::min()));
    CHECK(j.front() == std::numeric_limits<int64_t>::min());

    // The whole expansion path is exact in constant evaluation too.
    static_assert([] {
      interval<int64_t> acc;
      acc.insert(std::numeric_limits<int64_t>::max());
      acc.insert(std::numeric_limits<int64_t>::min());
      return (acc.front() == std::numeric_limits<int64_t>::min()) &&
             (acc.back() == std::numeric_limits<int64_t>::max());
    }());
  }
  if (true) {
    interval i{5};
    CHECK_FALSE(i.empty());
    CHECK(i.size() == 1U);

    CHECK_FALSE(i.push_back(0));
    CHECK_FALSE(i.push_back(5));
    CHECK(i.push_back(6));
    CHECK(i.push_back(7));
    CHECK_FALSE(i.push_back(6));
    CHECK(i.size() == 3U);
    CHECK(i.front() == 5);
    CHECK(i.back() == 7);

    i.pop_back();
    CHECK(i.size() == 2U);
    CHECK(i.front() == 5);
    CHECK(i.back() == 6);
    i.pop_back(2);
    CHECK(i.empty());
  }
  if (true) {
    interval i{5};
    CHECK_FALSE(i.empty());
    CHECK(i.size() == 1U);

    CHECK_FALSE(i.push_front(7));
    CHECK_FALSE(i.push_front(6));
    CHECK_FALSE(i.push_front(5));
    CHECK(i.push_front(4));
    CHECK(i.push_front(3));
    CHECK_FALSE(i.push_front(6));
    CHECK(i.size() == 3U);
    CHECK(i.front() == 3);
    CHECK(i.back() == 5);

    i.pop_front();
    CHECK(i.size() == 2U);
    CHECK(i.front() == 4);
    CHECK(i.back() == 5);
    i.pop_front(2);
    CHECK(i.empty());
  }
}
#pragma endregion

#pragma region ForEach

TEST_CASE("ForEach", "[IntervalTest]") {
  auto i = interval{1, 4};

  int64_t c{};
  int64_t s{};
  for (auto e : i) {
    ++c;
    s += e;
  }

  CHECK(c == 4);
  CHECK(s == (1 + 2 + 3 + 4));

  // An empty interval iterates as an empty range.
  int64_t n{};
  for ([[maybe_unused]] auto e : interval{}) ++n;
  CHECK(n == 0);
}
#pragma endregion

#pragma region Reverse

TEST_CASE("Reverse", "[IntervalTest]") {
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

    CHECK(c == 4);
    CHECK(s == (1 + 2 + 3 + 4));
    CHECK(l == 4);
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

    CHECK(c == 4);
    CHECK(s == (1 + 2 + 3 + 4));
    CHECK(l == 1);
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

    CHECK(c == 4);
    CHECK(s == (1 + 2 + 3 + 4));
    CHECK(l == 1);
  }
  // The iterator models the C++20 concepts (default-constructible, honest
  // traits), so std::ranges views and algorithms work over an interval.
  if (true) {
    using it_t = decltype(std::begin(interval{1, 4}));
    static_assert(std::bidirectional_iterator<it_t>);
    static_assert(std::ranges::bidirectional_range<interval<int>>);

    auto i = interval{1, 4};
    int64_t s{};
    int64_t l{};
    for (auto v : std::ranges::reverse_view{i}) {
      s += v;
      l = v;
    }
    CHECK(s == (1 + 2 + 3 + 4));
    CHECK(l == 1);
  }
}
#pragma endregion

#pragma region MinMax

TEST_CASE("MinMax", "[IntervalTest]") {
  auto i = interval{1, 4};

  CHECK(i.min() == 1);
  CHECK(i.max() == 4);
  i.min(42);
  CHECK(i.min() == 42);
  // Reversed via the raw setter reads as empty.
  CHECK(i.empty());
  i.max(64);
  CHECK(i.max() == 64);
  CHECK_FALSE(i.empty());
  // The setter accepts the top of `U`, which the old half-open representation
  // had to forbid. (This is an `interval<int>` by deduction.)
  i.max(std::numeric_limits<int>::max());
  CHECK(i.back() == std::numeric_limits<int>::max());
}
#pragma endregion

#pragma region CompareAndSwap

TEST_CASE("CompareAndSwap", "[IntervalTest]") {
  auto i = interval{1, 4};
  auto j = interval{2, 3};
  CHECK(i == i);
  CHECK(j == j);
  CHECK(i != j);
  CHECK(i.back() == 4);
  using std::swap;
  swap(i, j);
  CHECK(j.back() == 4);
  i.swap(j);
  CHECK(i.back() == 4);
}
#pragma endregion
#pragma region Formatting

TEST_CASE("Formatting", "[Intervals]") {
  if (true) {
    // Regular shows the closed interval; debug shows the raw closed storage
    // pair.
    CHECK(std::format("{}", interval{7, 9}) == "[7, 9]");
    CHECK(std::format("{:?}", interval{7, 9}) == "{7, 9}");

    // Empty in regular mode; debug shows the raw reversed bounds, which for
    // the canonical empty are the extremes.
    CHECK(std::format("{}", interval{}) == "[]");
    CHECK(std::format("{:?}", interval{}) ==
          "{9223372036854775807, -9223372036854775808}");
    interval bad{5, 9};
    bad.max(3);
    REQUIRE(bad.empty());
    CHECK(std::format("{}", bad) == "[]");
    CHECK(std::format("{:?}", bad) == "{5, 3}");

    // Not enumerated: format_kind is disabled, and a range of intervals shows
    // each as its closed form rather than a flattened list of values.
    CHECK(std::format("{}", std::vector{interval{1, 2}, interval{3, 4}}) ==
          "[[1, 2], [3, 4]]");

    // Enum interval: names in regular mode, underlying numbers in debug.
    interval<hue> h{hue::red, hue::blue};
    CHECK(std::format("{}", h) == "[red, blue]");
    CHECK(std::format("{:?}", h) == "{0, 2}");
  }
}

#pragma endregion

#pragma region Int128

#if defined(__SIZEOF_INT128__)
// Probe `U = __int128`, which buys iteration headroom over the full range of
// `int64_t`. The compiler extension alone is not enough: the standard library
// must also treat `__int128` as integral (libc++ does; Microsoft's STL does
// not, and libstdc++ only in GNU mode), so the probe is a template whose
// `if constexpr` keeps `interval` uninstantiated where support is missing.
template<typename I128>
void probe_int128_interval() {
  if constexpr (std::is_integral_v<I128>) {
    using iv_t = interval<int64_t, I128>;
    constexpr auto top = std::numeric_limits<int64_t>::max();

    // An interval ending at the top of `int64_t` is iterable with the wider
    // `U`, where `U = int64_t` could store but not iterate it.
    iv_t iv{top - 2, top};
    CHECK(iv.size() == 3U);
    int64_t cnt{};
    int64_t last{};
    for (auto v : iv) {
      ++cnt;
      last = v;
    }
    CHECK(cnt == 3);
    CHECK(last == top);

    CHECK(iv_t::max_size() == std::numeric_limits<size_t>::max());
    CHECK(iv_t{}.empty());
  } else {
    SUCCEED(
        "__int128 exists but the standard library does not treat it as "
        "integral, so interval cannot use it for U");
  }
}
#endif

TEST_CASE("Int128", "[IntervalTest]") {
#if defined(__SIZEOF_INT128__)
  probe_int128_interval<__int128>();
#else
  SUCCEED("__int128 is not available on this compiler");
#endif
}
#pragma endregion

// NOLINTEND(readability-function-size)
// NOLINTEND(readability-function-cognitive-complexity)
