// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2025 Steven Sudit
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

#include "../corvid/strings.h"
std::ostream&
operator<<(std::ostream& os, const corvid::strings::location& l) {
  return os << "location{" << l.pos << ", " << l.pos_value << "}";
}

#include "AccutestShim.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;
using namespace corvid::enums::sequence;
using namespace corvid::enums::bitmask;

void StringUtilsTest_ExtractPiece() {
  std::string_view sv;
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  sv = "1,2";
  EXPECT_EQ(strings::extract_piece(sv, ","), "1");
  EXPECT_EQ(strings::extract_piece(sv, ","), "2");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  sv = ",";
  EXPECT_EQ(sv.size(), 1u);
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");

  sv = "1,2,3,4";
  EXPECT_EQ(strings::extract_piece<std::string>(sv, ","), "1");
}

void StringUtilsTest_MorePieces() {
  std::string_view w, part;
  w = "1,2";
  EXPECT_FALSE(w.empty());
  EXPECT_TRUE(strings::more_pieces(part, w, ","));
  EXPECT_EQ(part, "1");
  EXPECT_FALSE(w.empty());
  EXPECT_FALSE(strings::more_pieces(part, w, ","));
  EXPECT_EQ(part, "2");
  EXPECT_TRUE(w.empty());
  EXPECT_FALSE(strings::more_pieces(part, w, ","));

  w = "1,";
  EXPECT_FALSE(w.empty());
  EXPECT_TRUE(strings::more_pieces(part, w, ","));
  EXPECT_EQ(part, "1");
  EXPECT_TRUE(w.empty());
  EXPECT_FALSE(strings::more_pieces(part, w, ","));
  EXPECT_EQ(part, "");
}

void StringUtilsTest_Split() {
  if (true) {
    using V = std::vector<std::string_view>;

    EXPECT_EQ(strings::split(""sv, ","), (V{}));
    EXPECT_EQ(strings::split("1"sv, ","), (V{"1"}));
    EXPECT_EQ(strings::split("1,"sv, ","), (V{"1", ""}));
    EXPECT_EQ(strings::split(",1"sv, ","), (V{"", "1"}));
    EXPECT_EQ(strings::split(",,"sv, ","), (V{"", "", ""}));
    EXPECT_EQ(strings::split("1,2"sv, ","), (V{"1", "2"}));
    EXPECT_EQ(strings::split("1,2,3"sv, ","), (V{"1", "2", "3"}));
    EXPECT_EQ(strings::split("11"sv, ","), (V{"11"}));
    EXPECT_EQ(strings::split("11,"sv, ","), (V{"11", ""}));
    EXPECT_EQ(strings::split(",11"sv, ","), (V{"", "11"}));
    EXPECT_EQ(strings::split("11,22"sv, ","), (V{"11", "22"}));
    EXPECT_EQ(strings::split("11,22,33"sv, ","), (V{"11", "22", "33"}));
  }
  if (true) {
    using V = std::vector<std::string>;
    using R = std::string;

    EXPECT_EQ(strings::split<R>("", ","), (V{}));
    EXPECT_EQ(strings::split<R>("1", ","), (V{"1"}));
    EXPECT_EQ(strings::split<R>("1,", ","), (V{"1", ""}));
    EXPECT_EQ(strings::split<R>(",1", ","), (V{"", "1"}));
    EXPECT_EQ(strings::split<R>(",,", ","), (V{"", "", ""}));
    EXPECT_EQ(strings::split<R>("1,2", ","), (V{"1", "2"}));
    EXPECT_EQ(strings::split<R>("1,2,3", ","), (V{"1", "2", "3"}));
    EXPECT_EQ(strings::split<R>("11", ","), (V{"11"}));
    EXPECT_EQ(strings::split<R>("11,", ","), (V{"11", ""}));
    EXPECT_EQ(strings::split<R>(",11", ","), (V{"", "11"}));
    EXPECT_EQ(strings::split<R>("11,22", ","), (V{"11", "22"}));
    EXPECT_EQ(strings::split<R>("11,22,33", ","), (V{"11", "22", "33"}));
  }
  if (true) {
    using V = std::vector<std::string_view>;
    using S = std::vector<std::string>;
    auto w = "1,2,3,4"sv;
    std::string s{w};
    EXPECT_EQ(strings::split(w, ","), (V{"1", "2", "3", "4"}));
    EXPECT_EQ(strings::split(s, ","), (V{"1", "2", "3", "4"}));
    EXPECT_EQ(strings::split(std::move(s), ","), (S{"1", "2", "3", "4"}));
  }
  if (true) {
    using R = std::string;
    using V = std::vector<R>;

    EXPECT_EQ(strings::split<R>("11,22,33", ","), (V{"11", "22", "33"}));
    EXPECT_EQ(strings::split<R>(R{"11,22,33"}, ","), (V{"11", "22", "33"}));
  }
}

void StringUtilsTest_SplitPg() {
  using PG = strings::piece_generator;
  if (true) {
    using V = std::vector<std::string_view>;

    EXPECT_EQ(strings::split_gen(std::string_view{}), (V{}));
    EXPECT_EQ(strings::split_gen(opt_string_view{std::nullopt}), (V{}));
    EXPECT_EQ(strings::split_gen(0_osv), (V{}));
    EXPECT_EQ(strings::split_gen(""sv), (V{""}));
    EXPECT_EQ(strings::split_gen(""_osv), (V{""}));
    EXPECT_EQ(strings::split_gen("1"sv), (V{"1"}));
    EXPECT_EQ(strings::split_gen("1 "sv), (V{"1", ""}));
    EXPECT_EQ(strings::split_gen(" 1"sv), (V{"", "1"}));
    EXPECT_EQ(strings::split_gen("  1"sv), (V{"", "", "1"}));
    EXPECT_EQ(strings::split_gen("1 2"sv), (V{"1", "2"}));
    EXPECT_EQ(strings::split_gen("1 2 3"sv), (V{"1", "2", "3"}));
    EXPECT_EQ(strings::split_gen("11"sv), (V{"11"}));
    EXPECT_EQ(strings::split_gen("11 "sv), (V{"11", ""}));
    EXPECT_EQ(strings::split_gen(" 11"sv), (V{"", "11"}));
    EXPECT_EQ(strings::split_gen("11 22"sv), (V{"11", "22"}));
    EXPECT_EQ(strings::split_gen("11 22 33"sv), (V{"11", "22", "33"}));
  }
  if (true) {
    using V = std::vector<std::string>;
    using R = std::string;
    using namespace strings;

    EXPECT_EQ((split_gen<PG, R>(std::string_view{})), (V{}));
    EXPECT_EQ((split_gen<PG, R>((opt_string_view{std::nullopt}))), (V{}));
    EXPECT_EQ((split_gen<PG, R>((opt_string_view{std::nullopt}))), (V{}));
    EXPECT_EQ((split_gen<PG, R>((0_osv))), (V{}));
    EXPECT_EQ((split_gen<PG, R>((""sv))), (V{""}));
    EXPECT_EQ((split_gen<PG, R>((""_osv))), (V{""}));
    EXPECT_EQ((split_gen<PG, R>(("1"sv))), (V{"1"}));
    EXPECT_EQ((split_gen<PG, R>(("1 "sv))), (V{"1", ""}));
    EXPECT_EQ((split_gen<PG, R>((" 1"sv))), (V{"", "1"}));
    EXPECT_EQ((split_gen<PG, R>(("  1"sv))), (V{"", "", "1"}));
    EXPECT_EQ((split_gen<PG, R>(("1 2"sv))), (V{"1", "2"}));
    EXPECT_EQ((split_gen<PG, R>(("1 2 3"sv))), (V{"1", "2", "3"}));
    EXPECT_EQ((split_gen<PG, R>(("11"sv))), (V{"11"}));
    EXPECT_EQ((split_gen<PG, R>(("11 "sv))), (V{"11", ""}));
    EXPECT_EQ((split_gen<PG, R>((" 11"sv))), (V{"", "11"}));
    EXPECT_EQ((split_gen<PG, R>(("11 22"sv))), (V{"11", "22"}));
    EXPECT_EQ((split_gen<PG, R>(("11 22 33"sv))), (V{"11", "22", "33"}));
  }
  if (true) {
// using V = std::vector<std::string_view>;
// using namespace strings;
#if 1
    strings::piece_generator pg{""sv,
        [](std::string_view s) {
          auto loc = strings::locate(s, {' ', '\t', '\n', '\r'});
          return std::pair{loc.pos, loc.pos + 1};
        },
        [](std::string_view s) { return s; }};
#endif
  }
}

void StringUtilsTest_Case() {
  auto s = "abcdefghij"s;
  strings::to_upper(s);
  EXPECT_EQ(s, "ABCDEFGHIJ");
  strings::to_lower(s);
  EXPECT_EQ(s, "abcdefghij");
  EXPECT_EQ(strings::as_lower("ABCDEFGHIJ"), "abcdefghij");
  EXPECT_EQ(strings::as_upper("abcdefghij"), "ABCDEFGHIJ");
  char a[] = "abcdefghij";
  strings::to_upper(a);
  EXPECT_EQ(a, "ABCDEFGHIJ"sv);
}

template<typename T>
concept SingleLocateValue =
    (StringViewConvertible<T> || is_char_v<T>) && !std::is_array_v<T>;

void StringUtilsTest_Locate() {
  using location = corvid::strings::location;
  if (true) {
    constexpr auto s = "abcdefghij"sv;
    constexpr auto l = s.size();
    // locate(psz).
    using T = decltype("def");
    auto f = SingleLocateValue<T>;
    EXPECT_TRUE(f);
    auto x = strings::locate(s, "def");

    // TODO: Thanks to range-based constructors, a string literal converts to
    // all sorts of things at once, including spans. Change the span overloads
    // to use concepts.

    //!!!!!! EXPECT_EQ(strings::locate(s, "def"), 3u);
    //  Locate(sv).
    EXPECT_EQ(strings::locate(s, "def"sv), 3u);
    // Locate(ch).
    EXPECT_EQ(strings::locate(s, 'd'), 3u);
    // Locate(init<ch>).
    EXPECT_EQ(strings::locate(s, {'x', 'i', 'y'}), (location{8u, 1u}));
    // Locate(span<ch>).
    EXPECT_EQ(strings::locate(s, std::span{"xfz", 3}), (location{5u, 1u}));

#ifdef I_FIXED_THIS_BUG
    // locate(array<ch>).
    // So this is supposed to return the location, which is a pos of 8 and a
    // value of 1, meaning 'i'. Instead, it's treating the array as a string.
    // Or, rather, as a SingleLocateValue = StringViewConvertible<T> ||
    // is_char_v<T>. So lets's sniff it out.
    EXPECT_EQ(strings::locate(s, std::array{'x', 'i', 'y'}),
        (location{8u, 1u}));
    // Locate(init<sv>).
    EXPECT_EQ(strings::locate(s, {"a0c"sv, "def"s, "g0i"}),
        (location{3u, 1u}));
    // Locate(vector<ch>).
    EXPECT_EQ(strings::locate(s, std::vector{'x', 'i', 'y'}),
        (location{8u, 1u}));
#endif

// Edge cases.
#ifdef I_FIXED_THIS_BUG
    EXPECT_EQ(strings::locate(s, "def", l), npos);
    EXPECT_EQ(strings::locate(s, "def", npos), npos);
#endif
    //
    EXPECT_EQ(strings::locate(s, 'd', l), npos);
    EXPECT_EQ(strings::locate(s, 'd', npos), npos);
    //
    EXPECT_EQ(strings::locate(s, {'x', 'i', 'y'}, l), nloc);
    EXPECT_EQ(strings::locate(s, {'x', 'i', 'y'}, npos), nloc);
    //
    EXPECT_EQ(strings::locate(s, {"a0c"sv, "def"s, "g0i"}, l), nloc);
    EXPECT_EQ(strings::locate(s, {"a0c"sv, "def"s, "g0i"}, npos), nloc);
//
#ifdef I_FIXED_THIS_BUG
    EXPECT_EQ(strings::locate(s, ""), 0u);
    EXPECT_EQ(strings::locate(s, "", l), l);
    EXPECT_EQ(strings::locate(s, "", l + 1), npos);
#endif
    //
    EXPECT_EQ(strings::locate(s, {"x", ""}), (location{0u, 1u}));
    EXPECT_EQ(strings::locate(s, {"x", ""}, l), (location{l, 1u}));
    EXPECT_EQ(strings::locate(s, {"x", ""}, l + 1), nloc);
  }
  if (true) {
    //                  01234567
    constexpr auto s = "aaaabaaa"sv;
    EXPECT_EQ(strings::locate_not(s, 'a'), 4u);
    EXPECT_EQ(strings::locate_not(s, 'b'), 0u);
    EXPECT_EQ(strings::locate_not("aaaaaa"sv, 'a'), npos);
#ifdef I_FIXED_THIS_BUG
    EXPECT_EQ(strings::locate_not(s, "a"), 4u);
    EXPECT_EQ(strings::locate_not(s, "aaaa"), 4u);
    EXPECT_EQ(strings::locate_not(s, "aaaab"), 5u);
    EXPECT_EQ(strings::locate_not(s, "b"), 0u);
    EXPECT_EQ(strings::locate_not("aaaaaa"sv, "a"), npos);
    EXPECT_EQ(strings::locate_not("aaaaaa"sv, "aa"), npos);
#endif
    size_t pos{};
    EXPECT_EQ(strings::located_not(pos, s, 'a'), true);
    EXPECT_EQ(pos, 4u);
    ++pos;
    EXPECT_EQ(strings::located_not(pos, s, 'a'), false);
    EXPECT_EQ(pos, npos);

    EXPECT_EQ(strings::rlocate_not(s, 'a'), 4u);
    EXPECT_EQ(strings::rlocate_not(s, 'b'), 7u);
    EXPECT_EQ(strings::rlocate_not("aaaaaa"sv, 'a'), npos);
#ifdef I_FIXED_THIS_BUG
    EXPECT_EQ(strings::rlocate_not(s, "a"), 4u);
    EXPECT_EQ(strings::rlocate_not(s, "aaaa"), 4u);
    EXPECT_EQ(strings::rlocate_not(s, "baaa"), 0u);
    EXPECT_EQ(strings::rlocate_not(s, "aabaaa"), 0u);
    EXPECT_EQ(strings::rlocate_not(s, "b"), 7u);
    EXPECT_EQ(strings::rlocate_not("aaaaaa"sv, "a"), npos);
    EXPECT_EQ(strings::rlocate_not("aaaaaa"sv, "aa"), npos);
    EXPECT_EQ(strings::rlocate_not("abcde"sv, "de"), 1u);
#endif
    pos = s.size();
    EXPECT_EQ(strings::rlocated_not(pos, s, 'a'), true);
    EXPECT_EQ(pos, 4u);
    --pos;
    EXPECT_EQ(strings::rlocated_not(pos, s, 'a'), false);
    EXPECT_EQ(pos, npos);
  }
  if (true) {
    // located(ch).
    constexpr auto t = "abcabcabc"sv;
    size_t pos{};
    const auto a = 'a';
    EXPECT_EQ(strings::located(pos, t, a), true);
    EXPECT_EQ(pos, 0u);
    ++pos;
    EXPECT_EQ(strings::located(pos, t, a), true);
    EXPECT_EQ(pos, 3u);
    ++pos;
    EXPECT_EQ(strings::located(pos, t, a), true);
    EXPECT_EQ(pos, 6u);
    ++pos;
    EXPECT_EQ(strings::located(pos, t, a), false);
    EXPECT_EQ(pos, npos);
  }
  if (true) {
    // rlocated(ch).
    constexpr auto t = "abcabcabc"sv;
    size_t pos = t.size();
    const auto a = 'a';
    EXPECT_EQ(strings::rlocated(pos, t, a), true);
    EXPECT_EQ(pos, 6u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, a), true);
    EXPECT_EQ(pos, 3u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, a), true);
    EXPECT_EQ(pos, 0u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, a), false);
    EXPECT_EQ(pos, npos);
  }
  if (true) {
    // located(psz).
    constexpr auto t = "abcabcabc"sv;
    size_t pos{};
    const auto abc = "abc";
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 0u);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 3u);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 6u);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), false);
    EXPECT_EQ(pos, npos);
  }
  if (true) {
    // rlocated(psz).
    constexpr auto t = "abcabcabc"sv;
    size_t pos = t.size();
    const auto abc = "abc";
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 6u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 3u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 0u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), false);
    EXPECT_EQ(pos, npos);
  }
  if (true) {
    // located(sv).
    constexpr auto t = "abcabcabc"sv;
    size_t pos{};
    const auto abc = "abc"sv;
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 0u);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 3u);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 6u);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), false);
    EXPECT_EQ(pos, npos);
  }
  if (true) {
    // rlocated(sv).
    constexpr auto t = "abcabcabc"sv;
    size_t pos = t.size();
    const auto abc = "abc"sv;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 6u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 3u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 0u);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), false);
    EXPECT_EQ(pos, npos);
  }
  if (true) {
    // located(init<ch>).
    constexpr auto s = "abxabcybc"sv;
    location loc;
    const auto xy = {'x', 'y'};
    EXPECT_EQ(strings::located(loc, s, xy), true);
    EXPECT_EQ(loc.pos, 2u);
    ++loc.pos;
    EXPECT_EQ(strings::located(loc, s, xy), true);
    EXPECT_EQ(loc.pos, 6u);
    ++loc.pos;
    EXPECT_EQ(strings::located(loc, s, xy), false);
    EXPECT_EQ(loc.pos, npos);
    loc.pos = 0;
    // located(array<ch>).
    const auto axy = std::array<const char, 2>{'x', 'y'};
    EXPECT_EQ(strings::located(loc, s, axy), true);
    // located(span<ch>).
    const auto sxy = std::span<const char>{axy};
    EXPECT_EQ(strings::located(loc, s, sxy), true);
  }
  if (true) {
    // rlocated(init<ch>).
    constexpr auto s = "abxabcybc"sv;
    location loc = {s.size(), npos};
    const auto xy = {'x', 'y'};
    EXPECT_EQ(strings::rlocated(loc, s, xy), true);
    EXPECT_EQ(loc.pos, 6u);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, xy), true);
    EXPECT_EQ(loc.pos, 2u);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, xy), false);
    EXPECT_EQ(loc.pos, npos);
    loc.pos = s.size();
    // located(array<ch>).
    const auto axy = std::array<const char, 2>{'x', 'y'};
    EXPECT_EQ(strings::rlocated(loc, s, axy), true);
    // located(span<ch>).
    const auto sxy = std::span<const char>{axy};
    EXPECT_EQ(strings::rlocated(loc, s, sxy), true);
  }
  if (true) {
    // located(init<sv>).
    constexpr auto s = "abxabcbcab"sv;
    location loc;
    // If the next line used regular string literals, we'd get a compiler
    // error. That's because, while we can promote a single `const char*` to
    // a `std::string_view`, we can't do that for a whole bunch of them. We
    // also need to ensure that the `std::span<const std::string_view>` does
    // not use `StringViewConvertible`, because that would break conversion
    // from `std::array`.
    const auto abcbc = {"ab"sv, "cbc"sv};
    EXPECT_EQ(strings::located(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 0u);
    EXPECT_EQ(loc.pos_value, 0u);
    strings::point_past(loc, abcbc);
    EXPECT_EQ(strings::located(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 3u);
    EXPECT_EQ(loc.pos_value, 0u);
    strings::point_past(loc, abcbc);
    EXPECT_EQ(strings::located(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 5u);
    EXPECT_EQ(loc.pos_value, 1u);
    strings::point_past(loc, abcbc);
    EXPECT_EQ(strings::located(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 8u);
    EXPECT_EQ(loc.pos_value, 0u);
    strings::point_past(loc, abcbc);
    EXPECT_EQ(strings::located(loc, s, abcbc), false);
    loc.pos = 0;
    // located(array<sv>).
    const auto axy = std::array<const std::string_view, 2>{"x"sv, "y"sv};
    EXPECT_EQ(strings::located(loc, s, axy), true);
    // located(span<sv>).
    const auto sxy = std::span<const std::string_view>{axy};
    EXPECT_EQ(strings::located(loc, s, sxy), true);
  }
  if (true) {
    // rlocated(init<sv>).
    constexpr auto s = "abxabcbcab"sv;
    location loc{s.size(), npos};
    const auto abcbc = {"ab"sv, "cbc"sv};
    EXPECT_EQ(strings::rlocated(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 8u);
    EXPECT_EQ(loc.pos_value, 0u);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 5u);
    EXPECT_EQ(loc.pos_value, 1u);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 3u);
    EXPECT_EQ(loc.pos_value, 0u);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 0u);
    EXPECT_EQ(loc.pos_value, 0u);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, abcbc), false);
    loc.pos = s.size();
    // located(array<sv>).
    const auto axy = std::array<const std::string_view, 2>{"x"sv, "y"sv};
    EXPECT_EQ(strings::rlocated(loc, s, axy), true);
    // located(span<sv>).
    const auto sxy = std::span<const std::string_view>{axy};
    EXPECT_EQ(strings::rlocated(loc, s, sxy), true);
  }
  if (true) {
    // count_located: ch, psz, sv, s, array<sv>, span<sv>.
    constexpr auto s = "abcdefghijabxdefghijaaa"sv;
    EXPECT_EQ(strings::count_located(s, 'a'), 5u);
    EXPECT_EQ(strings::count_located(s, 'b'), 2u);
    EXPECT_EQ(strings::count_located(s, "def"), 2u);
    EXPECT_EQ(strings::count_located(s, "aa"), 1u);

    EXPECT_EQ(strings::count_located(s, "def"sv), 2u);
    EXPECT_EQ(strings::count_located(s, "def"s), 2u);
    const auto axy = std::array<const std::string_view, 2>{"x"sv, "y"sv};
    EXPECT_EQ(strings::count_located(s, axy), 1u);
    const auto sxy = std::span<const std::string_view>{axy};
    EXPECT_EQ(strings::count_located(s, sxy), 1u);
    EXPECT_EQ(strings::count_located(s, ""), 24u);
    EXPECT_EQ(strings::count_located("aaaaaaaa"sv, "a"sv), 8u);
    EXPECT_EQ(strings::count_located("aaaaaaaa"sv, "aa"sv), 4u);
    const auto a0 = std::array<const std::string_view, 0>{};
    EXPECT_EQ(strings::count_located(s, a0), 0u);
    const auto s0 = std::span<const std::string_view>{a0};
    EXPECT_EQ(strings::count_located(s, s0), 0u);
    EXPECT_EQ(strings::count_located(s, {""sv}), 24u);
    EXPECT_EQ(strings::count_located(s, {""}), 24u);
  }
}

void StringUtilsTest_RLocate() {
  using location = corvid::strings::location;
  // These tests are abbreviated because we only want to confirm algorithmic
  // correctness, not test for all those tricky overloads.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    EXPECT_EQ(strings::locate(s, "def"sv), 3u);
    EXPECT_EQ(strings::rlocate(s, "def"sv), 13u);
    EXPECT_EQ(strings::locate(s, 'd'), 3u);
    EXPECT_EQ(strings::rlocate(s, 'd'), 13u);
    EXPECT_EQ(s[13], 'd');
    location loc;
    EXPECT_EQ(loc.pos, 0u);
  }
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    EXPECT_EQ(strings::rlocate(s, 'j'), 19u);
    EXPECT_EQ(strings::rlocate(s, 'j', npos), 19u);
    EXPECT_EQ(strings::rlocate(s, 'j', 0u), npos);
    EXPECT_EQ(strings::rlocate(s, 'j', 25u), 19u);
    EXPECT_EQ(strings::rlocate(s, 'j', 18u), 9u);
    EXPECT_EQ(strings::rlocate(s, 'a'), 10u);
    EXPECT_EQ(strings::rlocate(s, 'a', 10u), 10u);
    EXPECT_EQ(strings::rlocate(s, 'a', 1u), 0u);
    EXPECT_EQ(s.rfind('a', 0u), 0u);
    EXPECT_EQ(strings::rlocate(s, 'a', 0u), 0u);
#ifdef I_FIXED_THIS_BUG
    EXPECT_EQ(strings::rlocate(s, "j"), 19u);
    EXPECT_EQ(strings::rlocate(s, "j", npos), 19u);
    EXPECT_EQ(strings::rlocate(s, "j", 0u), npos);
    EXPECT_EQ(strings::rlocate(s, "j", 25u), 19u);
    EXPECT_EQ(strings::rlocate(s, "j", 18u), 9u);
    EXPECT_EQ(strings::rlocate(s, "a"), 10u);
    EXPECT_EQ(strings::rlocate(s, "a", 10u), 10u);
    EXPECT_EQ(strings::rlocate(s, "a", 1u), 0u);
    EXPECT_EQ(s.rfind("a", 0u), 0u);
    EXPECT_EQ(strings::rlocate(s, "a", 0u), 0u);
#endif
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}), (location{19u, 1u}));
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}, npos), (location{19u, 1u}));
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}, 0u), (location{npos, npos}));
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}, 25u), (location{19u, 1u}));
    EXPECT_EQ(s.rfind('i', 18u), 18u);
    EXPECT_EQ(s.rfind('j', 18u), 9u);
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}, 18u), (location{18u, 0u}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}), (location{11u, 1u}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 13), (location{11u, 1u}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 12), (location{11u, 1u}));
    EXPECT_EQ(s.rfind('b', 12u), 11u);
    EXPECT_EQ(s.rfind('b', 11u), 11u);
    EXPECT_EQ(s.rfind('b', 10u), 1u);
    EXPECT_EQ(s.rfind('b', 9u), 1u);
    EXPECT_EQ(s.rfind('a', 0u), 0u);
    EXPECT_EQ(s.rfind('b', 0u), npos);
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 11u), (location{11u, 1u}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 10u), (location{10u, 0u}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 1u), (location{1u, 1u}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 0u), (location{0u, 0u}));
  }
  // TODO: Maybe add rlocate multi-string tests.
}

void StringUtilsTest_LocateEdges() {
  using location = corvid::strings::location;
  // Test for using size as npos.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    EXPECT_EQ(strings::locate(s, 'a'), 0u);
    // npos, as pos, is just a placeholder for size.
    EXPECT_EQ(strings::locate(s, 'a', npos), npos);
    EXPECT_EQ(strings::locate(s, 'a', s.size()), npos);
    // We can choose to use the size as npos for returns.
    EXPECT_EQ(strings::locate(s, 'z'), npos);
    EXPECT_EQ(strings::locate<npos_choice::size>(s, 'z'), s.size());
    EXPECT_EQ(strings::rlocate(s, 'z'), npos);
    EXPECT_EQ(strings::rlocate<npos_choice::size>(s, 'z'), s.size());
    EXPECT_EQ(strings::locate(s, "xyz"sv), npos);
    EXPECT_EQ(strings::locate<npos_choice::size>(s, "xyz"sv), s.size());
    EXPECT_EQ(strings::rlocate(s, "xyz"sv), npos);
    EXPECT_EQ(strings::rlocate<npos_choice::size>(s, "xyz"sv), s.size());
    //
    EXPECT_EQ(strings::locate(s, {'y', 'z'}), (location{npos, npos}));
    EXPECT_EQ(strings::locate<npos_choice::size>(s, {'y', 'z'}),
        (location{s.size(), 2}));
    EXPECT_EQ(strings::rlocate(s, {'y', 'z'}), (location{npos, npos}));
    EXPECT_EQ(strings::rlocate<npos_choice::size>(s, {'y', 'z'}),
        (location{s.size(), 2}));
    EXPECT_EQ(strings::locate(s, {"uvw"sv, "xyz"sv}), (location{npos, npos}));
    EXPECT_EQ(strings::locate<npos_choice::size>(s, {"uvw"sv, "xyz"sv}),
        (location{s.size(), 2}));
    EXPECT_EQ(strings::rlocate(s, {"uvw"sv, "xyz"sv}), (location{npos, npos}));
    EXPECT_EQ(strings::rlocate<npos_choice::size>(s, {"uvw"sv, "xyz"sv}),
        (location{s.size(), 2}));
  }
  // Test for catching the subtle error of passing an initializer list of
  // string literals.
  if (true) {
  }
  // Confirm the correctness of infinite loops.
  if (true) {
#ifdef I_FIXED_THIS_BUG
    constexpr auto s = "abcdefghijabcdefghij"sv;
    EXPECT_EQ(s.find("a"), 0u);
    EXPECT_EQ(strings::locate(s, "a"), 0u);
    EXPECT_EQ(s.find(""), 0u);
    EXPECT_EQ(strings::locate(s, ""), 0u);
    EXPECT_EQ(strings::locate(s, {""sv, ""sv}), (location{0u, 0u}));
    EXPECT_EQ(strings::locate(s, std::array<std::string_view, 0>{}), nloc);
#endif
  }
}

void StringUtilsTest_Substitute() {
  if (true) {
    // substitute: ch, psz, s, sv.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, 'a', 'b'), 2u);
    EXPECT_EQ(s, "bbcdefghijbbcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def", "abc"), 2u);
    EXPECT_EQ(s, "abcabcghijabcabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"s, "abc"s), 2u);
    EXPECT_EQ(s, "abcabcghijabcabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, "abc"sv), 2u);
    EXPECT_EQ(s, "abcabcghijabcabcghij");
  }
  if (true) {
    // substitute: init<ch>, array<ch>, span<ch>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {'a'}, {'b'}), 2u);
    EXPECT_EQ(s, "bbcdefghijbbcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {'a', 'b'}, {'b', 'a'}), 4u);
    EXPECT_EQ(s, "bacdefghijbacdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {'a', 'y', 'c'}, {'y', 'a', 'z'}), 4u);
    EXPECT_EQ(s, "ybzdefghijybzdefghij");
    const auto axy = std::array<const char, 2>{'x', 'y'};
    const auto ayz = std::array<const char, 2>{'y', 'z'};
#ifdef I_FIXED_THIS_BUG
    s = "abcdefghijabxdefghijaaa";
    EXPECT_EQ(strings::substitute(s, axy, ayz), 1u);
    EXPECT_EQ(s, "abcdefghijabydefghijaaa");
    s = "abcdefghijabxdefghijaaa";
    const auto sxy = std::span<const char>{axy};
    const auto syz = std::span<const char>{ayz};
    EXPECT_EQ(strings::substitute(s, sxy, syz), 1u);
    EXPECT_EQ(s, "abcdefghijabydefghijaaa");
#else
    (void)axy;
    (void)ayz;
#endif
  }
  if (true) {
    // substitute: init<sv>, array<psz>, array<s>, array<sv>, span<sv>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {"ab"sv, "xz"sv, "cd"sv},
                  {"cd"sv, "za"sv, "ab"sv}),
        4u);
    EXPECT_EQ(s, "cdabefghijcdabefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {"ab", "xz", "cd"}, {"cd", "za", "ab"}),
        4u);
    EXPECT_EQ(s, "cdabefghijcdabefghij");
    s = std::string{sv};
    EXPECT_EQ(
        strings::substitute(s, {"ab"s, "xz"s, "cd"s}, {"cd"s, "za"s, "ab"s}),
        4u);
    EXPECT_EQ(s, "cdabefghijcdabefghij");

    // We can't support vector<s>:
    // * strings::substitute(s, f, t),
    s = std::string{sv};
    const auto f = std::vector{"ab"s, "xz"s, "cd"s};
    const auto t = std::vector{"cd"s, "za"s, "ab"s};
    // But we can allow explicit conversion to vector<sv>.
    EXPECT_EQ(
        strings::substitute(s, strings::as_views(f), strings::as_views(t)),
        4u);
    EXPECT_EQ(s, "cdabefghijcdabefghij");

    s = std::string{sv};
    const auto aabcd = std::array<const std::string_view, 2>{"ab"sv, "cd"sv};
    const auto acdab = std::array<const std::string_view, 2>{"cd"sv, "ab"sv};
    EXPECT_EQ(strings::substitute(s, aabcd, acdab), 4u);
    EXPECT_EQ(s, "cdabefghijcdabefghij");
    s = std::string{sv};
    const auto sabcd = std::span<const std::string_view>{aabcd};
    const auto scdab = std::span<const std::string_view>{acdab};
    EXPECT_EQ(strings::substitute(s, sabcd, scdab), 4u);
    EXPECT_EQ(s, "cdabefghijcdabefghij");
  }
  if (true) {
    std::string s;
    s = "abcdefghij";
    EXPECT_EQ(0u, strings::substitute(s, "bac", "yyy"));
    EXPECT_EQ(s, "abcdefghij");
    EXPECT_EQ(1u, strings::substitute(s, "abc", "yyy"));
    EXPECT_EQ(s, "yyydefghij");
    EXPECT_EQ(3u, strings::substitute(s, "y", "z"));
    EXPECT_EQ(s, "zzzdefghij");
    EXPECT_EQ(3u, strings::substitute(s, 'z', 'x'));
    EXPECT_EQ(s, "xxxdefghij");
    EXPECT_EQ(strings::substituted("abcdef", "abc", "yyy"), "yyydef");
  }
  if (true) {
    constexpr auto sv = "aaaaaaaaaa"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "a"sv, "b"sv), 10u);
    EXPECT_EQ(s, "bbbbbbbbbb");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "a"sv, ""sv), 10u);
    EXPECT_EQ(s, "");
  }
  if (true) {
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, "ab"sv), 2u);
    EXPECT_EQ(s, "abcabghijabcabghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, "a"sv), 2u);
    EXPECT_EQ(s, "abcaghijabcaghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, ""sv), 2u);
    EXPECT_EQ(s, "abcghijabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, "abcd"sv), 2u);
    EXPECT_EQ(s, "abcabcdghijabcabcdghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "de"sv, "abcd"sv), 2u);
    EXPECT_EQ(s, "abcabcdfghijabcabcdfghij");
  }
  if (true) {
    // Test of Pythonic behavior.
    constexpr auto sv = "abcdef"sv;
    auto s = std::string{sv};
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, ""sv, "x"sv), 7u);
    EXPECT_EQ(s, "xaxbxcxdxexfx");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, ""sv, "xy"sv), 7u);
    EXPECT_EQ(s, "xyaxybxycxydxyexyfxy");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "c"sv, ""sv), 1u);
    EXPECT_EQ(s, "abdef");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, ""sv, ""sv), 7u);
    EXPECT_EQ(s, "abcdef");
    //
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {""sv}, {"x"sv}), 7u);
    EXPECT_EQ(s, "xaxbxcxdxexfx");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {""sv}, {"xy"sv}), 7u);
    EXPECT_EQ(s, "xyaxybxycxydxyexyfxy");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {"c"sv}, {""sv}), 1u);
    EXPECT_EQ(s, "abdef");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {""sv}, {""sv}), 7u);
    EXPECT_EQ(s, "abcdef");
  }
}

void StringUtilsTest_Excise() {
  if (true) {
    // excise: ch, psz, s, sv.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, 'a'), 2u);
    EXPECT_EQ(s, "bcdefghijbcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "def"), 2u);
    EXPECT_EQ(s, "abcghijabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "def"s), 2u);
    EXPECT_EQ(s, "abcghijabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "def"sv), 2u);
    EXPECT_EQ(s, "abcghijabcghij");
  }
  if (true) {
    // excise: init<ch>, array<ch>, span<ch>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {'a'}), 2u);
    EXPECT_EQ(s, "bcdefghijbcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {'a', 'b'}), 4u);
    EXPECT_EQ(s, "cdefghijcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {'a', 'y', 'c'}), 4u);
    EXPECT_EQ(s, "bdefghijbdefghij");
    const auto axy = std::array<const char, 2>{'x', 'y'};
#ifdef I_FIXED_THIS_BUG
    s = "abcdefghijabxdefghijaaa";
    EXPECT_EQ(strings::excise(s, axy), 1u);
    EXPECT_EQ(s, "abcdefghijabdefghijaaa");
    s = "abcdefghijabxdefghijaaa";
    const auto sxy = std::span<const char>{axy};
    EXPECT_EQ(strings::excise(s, sxy), 1u);
    EXPECT_EQ(s, "abcdefghijabdefghijaaa");
    EXPECT_EQ(strings::excised(s, 'x'), "abcdefghijabdefghijaaa");
#else
    (void)axy;
#endif
  }
  if (true) {
    // excise: init<sv>, array<sv>, span<sv>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {"ab"sv, "xz"sv, "cd"sv}), 4u);
    EXPECT_EQ(s, "efghijefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {"ab", "xz", "cd"}), 4u);
    EXPECT_EQ(s, "efghijefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {"ab"s, "xz"s, "cd"s}), 4u);
    EXPECT_EQ(s, "efghijefghij");

    // We can't support vector<s>:
    // strings::excise(s, f, t),
    s = std::string{sv};
    const auto f = std::vector{"ab"s, "xz"s, "cd"s};
    // But we can allow explicit conversion to vector<sv>.
    EXPECT_EQ(strings::excise(s, strings::as_views(f)), 4u);
    EXPECT_EQ(s, "efghijefghij");
    s = std::string{sv};
    const auto aabcd = std::array<const std::string_view, 2>{"ab"sv, "cd"sv};
    EXPECT_EQ(strings::excise(s, aabcd), 4u);
    EXPECT_EQ(s, "efghijefghij");
    s = std::string{sv};
    const auto sabcd = std::span<const std::string_view>{aabcd};
    EXPECT_EQ(strings::excise(s, sabcd), 4u);
    EXPECT_EQ(s, "efghijefghij");
  }
  if (true) {
    std::string s;
    s = "abcdefghij";
    EXPECT_EQ(strings::excise(s, "bac"), 0u);
    EXPECT_EQ(s, "abcdefghij");
    EXPECT_EQ(strings::excise(s, "abc"), 1u);
    EXPECT_EQ(s, "defghij");
    EXPECT_EQ(strings::excise(s, 'e'), 1u);
    EXPECT_EQ(s, "dfghij");
    EXPECT_EQ(strings::substituted("abcdef", "abc", "yyy"), "yyydef");
  }
  if (true) {
    constexpr auto sv = "aaaaaaaaaa"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "a"sv), 10u);
    EXPECT_EQ(s, "");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, ""sv), 10u);
    EXPECT_EQ(s, "");
  }
  if (true) {
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "def"sv), 2u);
    EXPECT_EQ(s, "abcghijabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "de"sv), 2u);
    EXPECT_EQ(s, "abcfghijabcfghij");
  }
  if (true) {
    // Test of Pythonic behavior.
    constexpr auto sv = "abcdef"sv;
    auto s = std::string{sv};
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, ""sv), 6u);
    EXPECT_EQ(s, "");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {""sv, "c"sv}), 6u);
    EXPECT_EQ(s, "");
  }
}

template<AppendTarget T>
auto& test_append(T& target, std::string_view sv) {
  return *strings::appender{target}
              .append(sv)
              .append(sv[0])
              .append(sv.data(), sv.size())
              .append(4, sv[0]);
}

void StringUtilsTest_Target() {
  if (true) {
    std::ostringstream oss;
    EXPECT_EQ(test_append(oss, "abc").str(), "abcaabcaaaa");
  }
  if (true) {
    std::string s;
    EXPECT_EQ(test_append(s, "abc"), "abcaabcaaaa");
    strings::appender(s).reserve(500);
  }
#ifdef NOT_SUPPOSED_TO_COMPILE
  if (true) {
    int i;
    EXPECT_EQ(test_append(i, "abc").str(), "abcaabcaaaa");
    strings::appender(i).append("abc");
  }
#endif
}

struct NotStreamable {};

void StringUtilsTest_Print() {
  if (true) {
    std::stringstream ss;
    strings::stream_out(ss, "abc=", 5, ';');
    EXPECT_EQ(ss.str(), "abc=5;");
    strings::stream_out(ss);
    EXPECT_EQ(ss.str(), "abc=5;");
    strings::stream_out(ss, '\n');
    EXPECT_EQ(ss.str(), "abc=5;\n");
  }
  if (true) {
    std::stringstream ss;
    strings::stream_out_with(ss, ",", "abc", 5, "def", true);
    EXPECT_EQ(ss.str(), "abc,5,def,1");

    ss.str("");
    strings::ostream_redirector cerr_to_ss(std::cerr, ss);
    strings::report("a=", 5);
    EXPECT_EQ(ss.str(), "a=5\n");
  }
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::println("a=", 5);
    EXPECT_EQ(ss.str(), "a=5\n");
    ss.str("");
    strings::println_with(", ", 'a', 5, "bc", 5.5);
    EXPECT_EQ(ss.str(), "a, 5, bc, 5.5\n");
  }
#ifdef NOT_SUPPOSED_TO_COMPILE
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::print("a=", NotStreamable{});
    EXPECT_EQ(ss.str(), "a=5");
  }
#endif
}

void StringUtilsTest_Trim() {
  if (true) {
    EXPECT_EQ(strings::trim_left(""), "");
    EXPECT_EQ(strings::trim_left("1"), "1");
    EXPECT_EQ(strings::trim_left("12"), "12");
    EXPECT_EQ(strings::trim_left("123"), "123");
    EXPECT_EQ(strings::trim_left(" "), "");
    EXPECT_EQ(strings::trim_left(" 1"), "1");
    EXPECT_EQ(strings::trim_left(" 12"), "12");
    EXPECT_EQ(strings::trim_left(" 123"), "123");
    EXPECT_EQ(strings::trim_left("  "), "");
    EXPECT_EQ(strings::trim_left("  1"), "1");
    EXPECT_EQ(strings::trim_left("  12"), "12");
    EXPECT_EQ(strings::trim_left("  123"), "123");
    EXPECT_EQ(strings::trim_left("  1  "), "1  ");

    EXPECT_EQ(strings::trim_right(""), "");
    EXPECT_EQ(strings::trim_right("1"), "1");
    EXPECT_EQ(strings::trim_right("12"), "12");
    EXPECT_EQ(strings::trim_right("123"), "123");
    EXPECT_EQ(strings::trim_right(" "), "");
    EXPECT_EQ(strings::trim_right("1 "), "1");
    EXPECT_EQ(strings::trim_right("12 "), "12");
    EXPECT_EQ(strings::trim_right("123 "), "123");
    EXPECT_EQ(strings::trim_right("  "), "");
    EXPECT_EQ(strings::trim_right("1  "), "1");
    EXPECT_EQ(strings::trim_right("12  "), "12");
    EXPECT_EQ(strings::trim_right("123  "), "123");
    EXPECT_EQ(strings::trim_right("  1  "), "  1");

    EXPECT_EQ(strings::trim(""), "");
    EXPECT_EQ(strings::trim("1"), "1");
    EXPECT_EQ(strings::trim("12"), "12");
    EXPECT_EQ(strings::trim("123"), "123");
    EXPECT_EQ(strings::trim("  "), "");
    EXPECT_EQ(strings::trim(" 1 "), "1");
    EXPECT_EQ(strings::trim(" 12 "), "12");
    EXPECT_EQ(strings::trim(" 123 "), "123");
    EXPECT_EQ(strings::trim("    "), "");
    EXPECT_EQ(strings::trim("  1  "), "1");
    EXPECT_EQ(strings::trim("  12  "), "12");
    EXPECT_EQ(strings::trim("  123  "), "123");
    EXPECT_EQ(strings::trim("  1  "), "1");

    EXPECT_EQ(strings::trim_braces("[]"), "");
    EXPECT_EQ(strings::trim_braces("[1]"), "1");
    EXPECT_EQ(strings::trim_braces("[12]"), "12");
    EXPECT_EQ(strings::trim_braces("12]"), "12]");
    EXPECT_EQ(strings::trim_braces("[12]"), "12");
    EXPECT_EQ(strings::trim_braces("'12'", "'"), "12");
  }
  if (true) {
    auto w = " 1, 2, 3  , 4 "s;

    auto vsv = strings::split(w, ",");
    EXPECT_EQ(vsv[0], " 1");
    //* auto x = string::trim(v);
    strings::trim(vsv);
    EXPECT_EQ(vsv[0], "1");

    auto vs = strings::split<std::string>(w, ",");
    strings::trim(vs);
    EXPECT_EQ(vsv[0], "1");

    vsv = strings::split(w, ",");
    std::map<int, std::string> mss;
    for (size_t i = 0; i < vsv.size(); ++i) {
      mss[i] = vsv[i];
    }
    EXPECT_EQ(mss[0], " 1");
    strings::trim(mss);
    EXPECT_EQ(mss[0], "1");
  }
}

void StringUtilsTest_ParseNum() {
  if (true) {
    std::string_view sv;
    sv = "123";
    int64_t t;
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 123);
    EXPECT_TRUE(sv.empty());

    sv = "123 456";
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 123);
    EXPECT_FALSE(sv.empty());
    EXPECT_EQ(sv, " 456");
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 456);
    EXPECT_TRUE(sv.empty());

    sv = "abc";
    EXPECT_FALSE(strings::extract_num(t, sv));
    EXPECT_EQ(sv, "abc");
    EXPECT_TRUE(strings::extract_num<16>(t, sv));
    EXPECT_EQ(t, 0xabc);
    EXPECT_TRUE(sv.empty());

    sv = "123";
    auto r = strings::extract_num(sv);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 123);
    EXPECT_TRUE(sv.empty());
    r = strings::extract_num(sv);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(42, r.value_or(42));

    sv = "123";
    t = strings::parse_num(sv, -1);
    EXPECT_EQ(t, 123);
    sv = "abc";
    t = strings::parse_num(sv, -1);
    EXPECT_EQ(t, -1);
    t = strings::parse_num<int64_t, 16>(sv, -1);
    EXPECT_EQ(t, 0xabc);

    sv = "123 ";
    t = strings::parse_num(sv, -1);
    EXPECT_EQ(t, -1);

    std::optional<int64_t> ot;

    sv = "123 ";
    ot = strings::parse_num(sv);
    EXPECT_FALSE(ot.has_value());

    sv = "123";
    ot = strings::parse_num(sv);
    EXPECT_TRUE(ot.has_value());
    EXPECT_EQ(ot.value(), 123);
  }
  if (true) {
    std::string_view sv;
    double t;
#ifdef I_FIXED_THIS_BUG
    sv = "12.3";

    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 12.3);
    EXPECT_TRUE(sv.empty());

    sv = "12.3 45.6";
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 12.3);
    EXPECT_FALSE(sv.empty());
    EXPECT_EQ(sv, " 45.6");
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 45.6);
    EXPECT_TRUE(sv.empty());
#endif
#ifdef ONLY_WORKED_ON_MSVC
    std::string s;
    // strings::append<std::chars_format::hex>(s, 12.3l);
    s = "1.899999999999ap+3";
    sv = s;
    EXPECT_EQ(sv, "1.899999999999ap+3");
    // Succeed with totally wrong answer.
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(sv, "ap+3");
    sv = s;
    EXPECT_EQ(t, 1.8999999999990000l);
    EXPECT_TRUE(strings::extract_num<std::chars_format::hex>(t, sv));
    EXPECT_EQ(t, 12.3l);
    EXPECT_TRUE(sv.empty());
#endif

    sv = "12.3";
    auto r = strings::extract_num<double>(sv);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 12.3);
    EXPECT_TRUE(sv.empty());
    r = strings::extract_num<double>(sv);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(4.2, r.value_or(4.2));

    sv = "12.3";
    t = strings::parse_num<double>(sv, -1.2);
    EXPECT_EQ(t, 12.3);
    sv = "xyz";
    t = strings::parse_num<double>(sv, -1.2);
    EXPECT_EQ(t, -1.2);

    sv = "12.3 ";
    t = strings::parse_num<double>(sv, -1.2);
    EXPECT_EQ(t, -1.2);

    std::optional<double> ot;

    sv = "12.3 ";
    ot = strings::parse_num<double>(sv);
    EXPECT_FALSE(ot.has_value());

    sv = "12.3";
    ot = strings::parse_num<double>(sv);
    EXPECT_TRUE(ot.has_value());
    EXPECT_EQ(ot.value(), 12.3);
  }
}

void StringUtilsTest_AppendNum() {
  if (true) {
    EXPECT_EQ(strings::num_as_string(1), "1");
    EXPECT_EQ(strings::num_as_string(0), "0");
    EXPECT_EQ((strings::num_as_string<10, 5>(0)), "    0");
    EXPECT_EQ((strings::num_as_string<16>(uint8_t(0))), "0x00");
    EXPECT_EQ((strings::num_as_string<16>(uint16_t(0))), "0x0000");
    EXPECT_EQ((strings::num_as_string<16>(uint32_t(0))), "0x00000000");
    EXPECT_EQ((strings::num_as_string<16>(uint64_t(0))), "0x0000000000000000");
    EXPECT_EQ((strings::num_as_string(float(0.25f))), "0.25");
    EXPECT_EQ((strings::num_as_string(double(0.25f))), "0.25");
    EXPECT_EQ((strings::num_as_string<std::chars_format::hex>(double(0.25))),
        "1p-2");
    EXPECT_EQ(
        (strings::num_as_string<std::chars_format::fixed>(double(65536.25))),
        "65536.25");
    EXPECT_EQ((strings::num_as_string<std::chars_format::scientific>(
                  double(65536.25))),
        "6.553625e+04");
    EXPECT_EQ(
        (strings::num_as_string<std::chars_format::hex>(double(65536.25))),
        "1.00004p+16");
    EXPECT_EQ(
        (strings::num_as_string<std::chars_format::general>(double(65536.25))),
        "65536.25");
  }
}

void StringUtilsTest_Append() {
  using strings::join_opt;
  std::string s;

  s.clear();
  strings::append(s, "1"); // is_string_view_convertible_v
  strings::append(s, "2"s);
  strings::append(s, "3"sv);
  EXPECT_EQ(s, "123");
  s.clear();
  strings::append(s, '1');       // char (digit)
  strings::append(s, "2", "3");  // many
  strings::append(s, (char*){}); // char nullptr
  strings::append(s, (size_t)4); // append size_t
  strings::append(s, nullptr);   // nullptr
  EXPECT_EQ(s, "123null4null");

  s.clear();
  strings::append<2>(s, 0xaa); // append binary
  EXPECT_EQ(s, "10101010");
  s.clear();
  strings::append<16>(s, 0xaa); // append hex
  EXPECT_EQ(s, "0x000000aa");

  s.clear();
  strings::append(s, false);          // append bool
  strings::append(s, true);           // append bool
  strings::append(s, (float)2);       // append float
  strings::append(s, (double)3);      // append float
  strings::append(s, (long double)4); // append float
  EXPECT_EQ(s, "falsetrue234");

  s.clear();
  strings::append(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.456789012345");
  s.clear();
  strings::append<std::chars_format::scientific>(s, (double)123.456789012345);
  EXPECT_EQ(s, "1.23456789012345e+02");
  s.clear();
  strings::append<std::chars_format::fixed, 1>(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.5");
  s.clear();
  strings::append<std::chars_format::fixed, 6>(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.456789");

  const int i = 42;
  s.clear();
  strings::append(s, i);
  EXPECT_EQ(s, "42");
  s.clear();
  strings::append(s, &i);
  EXPECT_EQ(s, "42");
  s.clear();
  strings::append(s, reinterpret_cast<intptr_t>(&i));
  EXPECT_NE(s, "42");
  EXPECT_FALSE(s.starts_with("0x"));
  s.clear();
  strings::append(s, (void*){});
  EXPECT_EQ(s, "0x0000000000000000");
  s.clear();
  strings::append(s, reinterpret_cast<const void*>(&i));
  EXPECT_NE(s, "42");
  EXPECT_TRUE(s.starts_with("0x"));
  auto oi = std::make_optional(i);
  s.clear();
  strings::append(s, oi);
  EXPECT_EQ(s, "42");
  oi.reset();
  s.clear();
  strings::append(s, oi);
  EXPECT_EQ(s, "null");

  // None of these char-like types are char so they're all treated as ints.
  EXPECT_FALSE((std::is_same_v<char, signed char>));
  EXPECT_FALSE((std::is_same_v<char, unsigned char>));
  EXPECT_FALSE((std::is_same_v<char, char8_t>));
  EXPECT_FALSE((std::is_same_v<signed char, unsigned char>));

  s.clear();
  strings::append(s, (signed char)1);
  strings::append(s, (unsigned char)2);
  strings::append(s, (wchar_t)3);
  strings::append(s, (char8_t)4);
  strings::append(s, (char16_t)5);
  strings::append(s, (char32_t)6);
  EXPECT_EQ(s, "123456");

  // Might as well test all the integral types, if only for regression.
  s.clear();
  strings::append(s, (short int)1);
  strings::append(s, (unsigned short int)2);
  strings::append(s, (int)3);
  strings::append(s, (unsigned int)4);
  strings::append(s, (long int)5);
  strings::append(s, (unsigned long int)6);
  strings::append(s, (long long int)7);
  strings::append(s, (unsigned long long int)8);
  EXPECT_EQ(s, "12345678");

  s.clear();
  strings::append(s, (int8_t)1);
  strings::append(s, (int16_t)2);
  strings::append(s, (int32_t)3);
  strings::append(s, (int64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (uint8_t)1);
  strings::append(s, (uint16_t)2);
  strings::append(s, (uint32_t)3);
  strings::append(s, (uint64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (int_fast8_t)1);
  strings::append(s, (int_fast16_t)2);
  strings::append(s, (int_fast32_t)3);
  strings::append(s, (int_fast64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (uint_fast8_t)1);
  strings::append(s, (uint_fast16_t)2);
  strings::append(s, (uint_fast32_t)3);
  strings::append(s, (uint_fast64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (int_least8_t)1);
  strings::append(s, (int_least16_t)2);
  strings::append(s, (int_least32_t)3);
  strings::append(s, (int_least64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (uint_least8_t)1);
  strings::append(s, (uint_least16_t)2);
  strings::append(s, (uint_least32_t)3);
  strings::append(s, (uint_least64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (intmax_t)1);
  strings::append(s, (intptr_t)2);
  strings::append(s, (uintmax_t)3);
  strings::append(s, (uintptr_t)4);
  EXPECT_EQ(s, "1234");

  enum ColorEnum { red, green = 20, blue };
  enum class ColorClass { red, green = 20, blue };

  s.clear();
  strings::append(s, green);
  strings::append(s, ColorClass::blue);
  EXPECT_EQ(s, "2021");
  s.clear();

  strings::append(s, ColorClass::green);
  EXPECT_EQ(s, "20");
  s.clear();
  strings::append(s, ColorClass(20));
  EXPECT_EQ(s, "20"); // not hex!

  std::map<std::string, int> msi{{"a", 1}, {"c", 2}};
  std::set<int> si{3, 4, 5};
  s.clear();
  strings::append(s, msi);
  strings::append(s, si);
  EXPECT_EQ(s, "12345");

  std::variant<std::monostate, int, std::map<std::string, int>> va;
  s.clear();
  strings::append(s, va);
  EXPECT_EQ(s, "null");
  va = 52;
  s.clear();
  strings::append(s, va);
  EXPECT_EQ(s, "52");

  va = msi;
  s.clear();
  strings::append(s, va);
  EXPECT_EQ(s, "12");

  std::map<std::string, std::set<int>> mssi{{"c", {5, 4}}, {"a", {3, 2, 1}}};
  s.clear();
  strings::append(s, mssi);
  EXPECT_EQ(s, "12345");

  EXPECT_FALSE((std::is_same_v<int8_t, char>));
  EXPECT_EQ(strings::num_as_string(42), "42");
  EXPECT_EQ(strings::num_as_string<16>(10), "0x0000000a");
  EXPECT_EQ(strings::num_as_string('a'), "97");
  EXPECT_EQ(strings::num_as_string(123.0l), "123");
  EXPECT_EQ(strings::num_as_string(12.3l), "12.3");

  // TODO: Add nested container torture test.

  EXPECT_EQ(strings::concat("1", "2"sv, "3"s), "123");
  EXPECT_EQ(strings::concat(1, 2.0, 3ull), "123");
  EXPECT_EQ(strings::concat(true, std::byte{2}, 3), "true23");
  EXPECT_EQ((strings::concat("1", "2")), "12");

  auto t = std::make_tuple("1"s, 2, 3.0);
  auto l = {"1"s, "2"s, "3"s};
  auto p = std::make_pair("1"s, "2, 3");
  auto a = std::array<int, 3>{1, 2, 3};
  std::vector<std::string> v = l;

  EXPECT_EQ(strings::concat(l, v), "123123");

  EXPECT_EQ(strings::concat(t), "123");
  EXPECT_EQ(strings::concat(p), "12, 3");
  EXPECT_EQ(strings::concat(a), "123");

  EXPECT_EQ((strings::join<join_opt::quoted>(l)), R"(["1", "2", "3"])");

  s.clear();
  strings::append_join_with(s, ";", "123"); // single
  EXPECT_EQ(s, "123");
  strings::append_join_with(s, ";", std::optional<std::string>{});
  strings::append_join_with(s, ";", "456");
  EXPECT_EQ(s, "123null456");
  strings::append_join_with<join_opt::prefixed, '`', '\''>(s, ";", "789");
  EXPECT_EQ(s, "123null456;`789'");

  s.clear();
  strings::append_join_with(s, ";", v); // container
  EXPECT_EQ(s, "[1;2;3]");

  s = "a";
  strings::append_join_with<join_opt::prefixed>(s, ";", v);
  EXPECT_EQ(s, "a;[1;2;3]");
  s = "v=";
  strings::append_join_with(s, ";", v);
  EXPECT_EQ(s, "v=[1;2;3]");

  s.clear();
  strings::append_join_with(s, ";", t); // tuple
  EXPECT_EQ(s, "{1;2;3}");

  s.clear();
  strings::append_join_with<join_opt::flat>(s, ";", t); // tuple
  EXPECT_EQ(s, "1;2;3");

  s.clear();
  strings::append_join_with(s, ";", intptr_t(42));
  EXPECT_EQ(s, "42");

  s.clear();
  strings::append_join_with(s, ";", reinterpret_cast<const void*>(&i));
  EXPECT_NE(s, "42");

  s.clear();
  strings::append_join_with(s, ";", reinterpret_cast<const void*>(NULL));
  EXPECT_EQ(s, "0x0000000000000000");

  s.clear();
  const char* psz{};
  strings::append_join_with(s, ";", psz);
  EXPECT_EQ(s, "null");

  va = 52;
  s.clear();
  strings::append_join_with(s, ";", va);
  EXPECT_EQ(s, "52");
  EXPECT_EQ(strings::concat(va), "52");

  va = msi;
  s.clear();
  strings::append_join_with(s, ";", va);
  EXPECT_EQ(s, "[1;2]");

  s.clear();
  strings::append_join_with(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, "1");

  s.clear();
  strings::append_join_with<join_opt::flat>(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, "1");

  s.clear();
  strings::append_join_with<join_opt::keyed>(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, "{a,1}");

  s.clear();
  strings::append_join_with<join_opt::flat_keyed>(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, "a,1");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, R"("a": 1)");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ",", std::pair{1, "a"});
  EXPECT_EQ(s, R"("1": "a")");

  s.clear();
  strings::append_join_with(s, ",", mssi);
  EXPECT_EQ(s, "[[1,2,3],[4,5]]");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ", ", mssi);
  EXPECT_EQ(s, R"({"a": [1, 2, 3], "c": [4, 5]})");

  auto om = std::make_optional(mssi);
  s.clear();
  strings::append_join_with(s, ",", om);
  EXPECT_EQ(s, "[[1,2,3],[4,5]]");

  s.clear();
  strings::append_join<join_opt::json>(s, mssi);
  EXPECT_EQ(s, R"({"a": [1, 2, 3], "c": [4, 5]})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<std::string, int>{{"b", 2}, {"a", 1}});
  EXPECT_EQ(s, R"({"a": 1, "b": 2})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<std::string, std::string>{{"b", "2"}, {"a", "1"}});
  EXPECT_EQ(s, R"({"a": "1", "b": "2"})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<int, std::string>{{2, "2"}, {1, "1"}});
  EXPECT_EQ(s, R"({"1": "1", "2": "2"})");

  s.clear();
  auto il = {3, 1, 4};
  strings::append_join_with(s, ";", il); // Initializer list.
  // Unbound initializer lists can't be deduced by templates.
  // * strings::append_join_with(s, ";", {1, 2, 3});
  EXPECT_EQ(s, "[3;1;4]");

  EXPECT_EQ(strings::join(1, 2, 3), "[1, 2, 3]");
  EXPECT_EQ(strings::join("1"s, "2"s, "3"s), "[1, 2, 3]");
  EXPECT_EQ(strings::join(t), "{1, 2, 3}");
  EXPECT_EQ(strings::join<join_opt::keyed>(p), "{1, 2, 3}");
  s.clear();
  strings::append_join_with<join_opt::keyed>(s, ",", std::pair{1, 2});
  EXPECT_EQ(s, "{1,2}");
  EXPECT_EQ(strings::join(a), "[1, 2, 3]");

  EXPECT_EQ(strings::join<join_opt::flat_keyed>(t), "1, 2, 3");
  EXPECT_EQ(strings::join<join_opt::flat_keyed>(p), "1, 2, 3");
  EXPECT_EQ(strings::join<join_opt::flat_keyed>(a), "1, 2, 3");

  EXPECT_EQ(strings::concat('1', '2', '3'), "123");

  EXPECT_EQ(strings::join(mssi), "[[1, 2, 3], [4, 5]]");
  EXPECT_EQ(strings::join<join_opt::flat>(mssi), "1, 2, 3, 4, 5");

  // More JSON A/B tests.
  s.clear();
  strings::append_join_with(s, ", ", std::vector{1, 2, 3});
  EXPECT_EQ(s, "[1, 2, 3]");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ", ", std::vector{1, 2, 3});
  EXPECT_EQ(s, "[1, 2, 3]");

  s.clear();
  strings::append_join_with(s, ", ", std::vector<int>{});
  EXPECT_EQ(s, "[]");

  s = strings::join_json(std::vector<int>{});
  EXPECT_EQ(s, "[]");

  s.clear();
  strings::append_join_with(s, ", ", std::vector{"a", "b", "c"});
  EXPECT_EQ(s, "[a, b, c]");

  s = strings::join_json(std::vector{"a", "b", "c"});
  EXPECT_EQ(s, R"(["a", "b", "c"])");

  s = strings::join_json(std::vector{"a", "b", "c"},
      std::vector{"d", "e", "f"});
  EXPECT_EQ(s, R"([["a", "b", "c"], ["d", "e", "f"]])");

  // TODO: test plain array, std::array, map, set, pair, tuple

  // TODO: Test objects that aren't strings but do have implicit conversion
  // to string or string_view.
}

void StringUtilsTest_Edges() {
  using strings::join_opt;

  std::vector<int> a{1, 2, 3};
  std::vector<int> b{4, 5, 6};
  EXPECT_EQ(strings::join(a, b), "[[1, 2, 3], [4, 5, 6]]");
  EXPECT_EQ(strings::join<join_opt::flat>(a, b), "1, 2, 3, 4, 5, 6");
  EXPECT_EQ((strings::join<join_opt::braced, '(', ')'>(a, b)),
      "([1, 2, 3], [4, 5, 6])");
  EXPECT_EQ((strings::join<join_opt::prefixed, '{', '}'>(a, b)),
      ", {[1, 2, 3], [4, 5, 6]}");
  EXPECT_EQ(strings::join<join_opt::prefixed>(a, b),
      ", [[1, 2, 3], [4, 5, 6]]");
}

void StringUtilsTest_Streams() {
  using strings::join_opt;

  std::vector<int> a{1, 2, 3};
  std::vector<int> b{4, 5, 6};

  if (true) {
    std::stringstream s;
    strings::append_join(s, a, b);
    EXPECT_EQ(s.str(), "[[1, 2, 3], [4, 5, 6]]");
  }
}

enum class rgb {
  black,      // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};

template<>
constexpr auto registry::enum_spec_v<rgb> =
    make_bitmask_enum_spec<rgb, "red,green,blue">();

void StringUtilsTest_AppendEnum() {
  std::string s;
  EXPECT_EQ((strings::concat(rgb::yellow)), "red + green");
  EXPECT_EQ((strings::join(rgb::yellow, rgb::cyan)),
      "[red + green, green + blue]");
  s = (strings::join<strings::join_opt::braced, -1, -1>(rgb::yellow,
      rgb::cyan));
  EXPECT_EQ(s, "red + green, green + blue");
}

// Enlisted Marine Corps ranks.
enum class marine_rank {
  Civilian,
  Private,
  PrivateFirstClass,
  LanceCorporal,
  Sergeant,
  StaffSergeant,
  GunnerySergeant,
  MasterSergeant,
  FirstSergeant,
  MasterGunnerySergeant,
  SergeantMajor,
  SergeantMajorOfTheMarineCorps
};

template<>
constexpr auto registry::enum_spec_v<marine_rank> =
    make_sequence_enum_spec<marine_rank,
        "Civilian, Private, PrivateFirstClass, LanceCorporal, Sergeant, "
        "StaffSergeant, GunnerySergeant, MasterSergeant, FirstSergeant, "
        "MasterGunnerySergeant, SergeantMajor, SergeantMajorOfTheMarineCorps",
        wrapclip::limit>();

struct soldier {
  std::string name;
  marine_rank rank;
  int64_t serial_number;

  friend std::ostream& operator<<(std::ostream& os, const soldier& s) {
    return os << "[" << s.name << ", " << s.rank << ", " << s.serial_number
              << "]";
  }
};

template<>
constexpr bool strings::stream_append_v<soldier> = true;

struct person {
  std::string last;
  std::string first;

  template<AppendTarget A>
  static auto& append(A& target, const person& p) {
    return corvid::strings::append(target, p.last, ", ", p.first);
  }

  template<auto opt = strings::join_opt::braced, char open = 0, char close = 0,
      AppendTarget A>
  static A& append_join_with(A& target, strings::delim d, const person& p) {
    constexpr auto is_json = strings::decode::json_v<opt>;
    constexpr char next_open = open ? open : (is_json ? '[' : 0);
    constexpr char next_close = close ? close : (is_json ? ']' : 0);
    return corvid::strings::append_join_with<opt, next_open, next_close>(
        target, d, p.last, p.first);
  }
};

template<AppendTarget A>
constexpr auto strings::append_override_fn<A, person> = person::append<A>;

template<strings::join_opt opt, char open, char close, AppendTarget A>
constexpr auto strings::append_join_override_fn<opt, open, close, A, person> =
    person::append_join_with<opt, open, close, A>;

void StringUtilsTest_AppendStream() {
  using strings::join_opt;
  soldier pyle{"Gomer", marine_rank::Private, 12345678};
  soldier carter{"Vince", marine_rank::GunnerySergeant, 23456789};
  person doe{"Doe", "John"};
  using tuple0 = std::tuple<>;
  using tuple1 = std::tuple<int>;
  using tuple2 = std::tuple<int, std::string>;
  if (true) {
    std::stringstream os;
    os << pyle;
    EXPECT_EQ(os.str(), "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::stringstream os;
    strings::append_stream(os, pyle);
    EXPECT_EQ(os.str(), "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append_stream(s, pyle);
    EXPECT_EQ(s, "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append(s, pyle);
    EXPECT_EQ(s, "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, pyle);
    EXPECT_EQ(s, "[Gomer, Private, 12345678]");
  }
  if (true) {
    auto v = std::vector{std::make_pair(pyle, 30'000.15),
        std::make_pair(carter, 40'000.21)};
    std::string s;
    strings::append_join<join_opt::keyed>(s, v);
    EXPECT_EQ(s,
        "[{[Gomer, Private, 12345678], 30000.15}, {[Vince, "
        "GunnerySergeant, "
        "23456789], 40000.21}]");

    s.clear();
    strings::append_join<strings::join_opt::json>(s, v);
    EXPECT_EQ(s,
        R"({"[Gomer, Private, 12345678]": 30000.15, "[Vince, GunnerySergeant, 23456789]": 40000.21})");
  }
  if (true) {
    std::string s;
    strings::append(s, doe);
    EXPECT_EQ(s, "Doe, John");

    s.clear();
    strings::append_join(s, doe);
    EXPECT_EQ(s, "[Doe, John]");
  }
  if (true) {
    std::string s;
    strings::append_join_with<strings::join_opt::flat>(s, "; ", doe);
    EXPECT_EQ(s, "Doe; John");
  }
  if (true) {
    std::string s;
    strings::append_join_with<strings::join_opt::quoted>(s, "; ", doe);
    EXPECT_EQ(s, R"(["Doe"; "John"])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, doe);
    EXPECT_EQ(s, R"(["Doe", "John"])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, doe, doe, doe);
    EXPECT_EQ(s, R"([["Doe", "John"], ["Doe", "John"], ["Doe", "John"]])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::flat>(s, doe, doe, doe);
    EXPECT_EQ(s, "Doe, John, Doe, John, Doe, John");
  }
  if (true) {
    tuple0 t0{};
    tuple1 t1{1};
    tuple2 t2{1, "2"};
    std::string s;

    strings::append_join(s, t0);
    EXPECT_EQ(s, "");
    s.clear();
    strings::append_join<strings::join_opt::flat>(s, t0);
    EXPECT_EQ(s, "");
    s.clear();
    strings::append_join<strings::join_opt::quoted>(s, t0);
    EXPECT_EQ(s, "");
    s.clear();
    strings::append_join<strings::join_opt::json>(s, t0);
    EXPECT_EQ(s, "");
    // Test whether we need a special case.
    s.clear();
    strings::append_join_with(s, ";", t0, t1, t2);
    EXPECT_EQ(s, "[;{1};{1;2}]")
    s.clear();
    strings::append_join_with(s, ";", t0);
    EXPECT_EQ(s, "");

    s.clear();
    strings::append_join(s, t1);
    EXPECT_EQ(s, "{1}");
    s.clear();
    strings::append_join<strings::join_opt::flat>(s, t1);
    EXPECT_EQ(s, "1");
    s.clear();
    strings::append_join<strings::join_opt::quoted>(s, t1);
    EXPECT_EQ(s, "{1}");
    s.clear();
    strings::append_join<strings::join_opt::json>(s, t1);
    EXPECT_EQ(s, "{1}");

    s.clear();
    strings::append_join(s, t2);
    EXPECT_EQ(s, "{1, 2}");
    s.clear();
    strings::append_join<strings::join_opt::flat>(s, t2);
    EXPECT_EQ(s, "1, 2");
    s.clear();
    strings::append_join<strings::join_opt::json>(s, t2);
    EXPECT_EQ(s, "{1, \"2\"}");
  }
}

void StringUtilsTest_AppendJson() {
  using strings::join_opt;

  if (true) {
    std::string s;
    strings::append_json(s, marine_rank::Private);
    EXPECT_EQ(s, R"("Private")");
  }
  if (true) {
    std::string s;
    strings::append_escaped(s, R"(he"l"lo)");
    EXPECT_EQ(s, R"(he\"l\"lo)");
  }
  if (true) {
    std::string s;
    strings::append_escaped(s, R"(he"l"lo)");
    EXPECT_EQ(s, R"(he\"l\"lo)");
    s.clear();
    strings::append_escaped(s, "a\tb\\c\"d\n\b\f\r\x1f");
    EXPECT_EQ(s, R"(a\tb\\c\"d\n\b\f\r\u001f)");
  }
  if (true) {
    std::string s;
    const char* p{};
    strings::append_json(s, p);
    EXPECT_EQ(s, R"(null)");
    s.clear();
    strings::append_json(s, cstring_view{});
    EXPECT_EQ(s, R"(null)");
    s.clear();
    strings::append_json(s, cstring_view{""});
    EXPECT_EQ(s, R"("")");
  }
}

MAKE_TEST_LIST(StringUtilsTest_ExtractPiece, StringUtilsTest_MorePieces,
    StringUtilsTest_Split, StringUtilsTest_SplitPg, StringUtilsTest_ParseNum,
    StringUtilsTest_Case, StringUtilsTest_Locate, StringUtilsTest_RLocate,
    StringUtilsTest_LocateEdges, StringUtilsTest_Substitute,
    StringUtilsTest_Excise, StringUtilsTest_Target, StringUtilsTest_Print,
    StringUtilsTest_Trim, StringUtilsTest_AppendNum, StringUtilsTest_Append,
    StringUtilsTest_Edges, StringUtilsTest_Streams, StringUtilsTest_AppendEnum,
    StringUtilsTest_AppendStream, StringUtilsTest_AppendJson);
