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

#include <cstdint>
#include <map>
#include <set>
#include <type_traits>

#include "../corvid/strings.h"
std::ostream&
operator<<(std::ostream& os, const corvid::strings::location& l) {
  return os << "location{" << l.pos << ", " << l.pos_value << "}";
}

#include "minitest.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;
using namespace corvid::enums::sequence;
using namespace corvid::enums::bitmask;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

// Test extract_piece.
void StringUtilsTest_ExtractPiece() {
  std::string_view sv;
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  sv = "1,2";
  EXPECT_EQ(strings::extract_piece(sv, ","), "1");
  EXPECT_EQ(strings::extract_piece(sv, ","), "2");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  sv = ",";
  EXPECT_EQ(sv.size(), 1U);
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");

  sv = "1,2,3,4";
  EXPECT_EQ(strings::extract_piece<std::string>(sv, ","), "1");
}

// Test more_pieces.
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

// Test split.
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

// Test split_gen.
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
    strings::piece_generator pg{""sv,
        [](std::string_view s) {
          auto loc = strings::locate(s, {' ', '\t', '\n', '\r'});
          return std::pair{loc.pos, loc.pos + 1};
        },
        [](std::string_view s) { return s; }};
  }
}

// Test as_lower, as_upper.
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

// Test locate.
void StringUtilsTest_Locate() {
  using location = corvid::strings::location;
  if (true) {
    constexpr auto s = "abcdefghij"sv;
    constexpr auto l = s.size();
    // locate(psz).
    EXPECT_EQ(strings::locate(s, "def"), 3U);
    //  Locate(sv).
    EXPECT_EQ(strings::locate(s, "def"sv), 3U);
    // Locate(ch).
    EXPECT_EQ(strings::locate(s, 'd'), 3U);
    // Locate(init<ch>).
    EXPECT_EQ(strings::locate(s, {'x', 'i', 'y'}), (location{8U, 1U}));
    // Locate(span<ch>).
    EXPECT_EQ(strings::locate(s, std::span{"xfz", 3}), (location{5U, 1U}));

    // locate(array<ch>).
    // So this is supposed to return the location, which is a pos of 8 and a
    // value of 1, meaning 'i'. Instead, it's treating the array as a string.
    // Or, rather, as a SingleLocateValue = StringViewConvertible<T> ||
    // is_char_v<T>. So lets's sniff it out.
    EXPECT_EQ(strings::locate(s, std::array{'x', 'i', 'y'}),
        (location{8U, 1U}));
    // Locate(init<sv>).
    EXPECT_EQ(strings::locate(s, {"a0c"sv, "def"s, "g0i"}),
        (location{3U, 1U}));
    // Locate(vector<ch>).
    EXPECT_EQ(strings::locate(s, std::vector{'x', 'i', 'y'}),
        (location{8U, 1U}));

    // Edge cases.
    EXPECT_EQ(strings::locate(s, "def", l), npos);
    EXPECT_EQ(strings::locate(s, "def", npos), npos);
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
    EXPECT_EQ(strings::locate(s, ""), 0U);
    EXPECT_EQ(strings::locate(s, "", l), l);
    EXPECT_EQ(strings::locate(s, "", l + 1), npos);
    //
    EXPECT_EQ(strings::locate(s, {"x", ""}), (location{0U, 1U}));
    EXPECT_EQ(strings::locate(s, {"x", ""}, l), (location{l, 1U}));
    EXPECT_EQ(strings::locate(s, {"x", ""}, l + 1), nloc);
  }
  if (true) {
    //                  01234567
    constexpr auto s = "aaaabaaa"sv;
    EXPECT_EQ(strings::locate_not(s, 'a'), 4U);
    EXPECT_EQ(strings::locate_not(s, 'b'), 0U);
    EXPECT_EQ(strings::locate_not("aaaaaa"sv, 'a'), npos);
    EXPECT_EQ(strings::locate_not(s, "a"), 4U);
    EXPECT_EQ(strings::locate_not(s, "aaaa"), 4U);
    EXPECT_EQ(strings::locate_not(s, "aaaab"), 5U);
    EXPECT_EQ(strings::locate_not(s, "b"), 0U);
    EXPECT_EQ(strings::locate_not("aaaaaa"sv, "a"), npos);
    EXPECT_EQ(strings::locate_not("aaaaaa"sv, "aa"), npos);
    size_t pos{};
    EXPECT_EQ(strings::located_not(pos, s, 'a'), true);
    EXPECT_EQ(pos, 4U);
    ++pos;
    EXPECT_EQ(strings::located_not(pos, s, 'a'), false);
    EXPECT_EQ(pos, npos);

    EXPECT_EQ(strings::rlocate_not(s, 'a'), 4U);
    EXPECT_EQ(strings::rlocate_not(s, 'b'), 7U);
    EXPECT_EQ(strings::rlocate_not("aaaaaa"sv, 'a'), npos);
    EXPECT_EQ(strings::rlocate_not(s, "a"), 4U);
    EXPECT_EQ(strings::rlocate_not(s, "aaaa"), 4U);
    EXPECT_EQ(strings::rlocate_not(s, "baaa"), 0U);
    EXPECT_EQ(strings::rlocate_not(s, "aabaaa"), 0U);
    EXPECT_EQ(strings::rlocate_not(s, "b"), 7U);
    EXPECT_EQ(strings::rlocate_not("aaaaaa"sv, "a"), npos);
    EXPECT_EQ(strings::rlocate_not("aaaaaa"sv, "aa"), npos);
    EXPECT_EQ(strings::rlocate_not("abcde"sv, "de"), 1U);
    EXPECT_EQ(strings::rlocate_not("abc"sv, "abcdef"sv), 0U);
    EXPECT_EQ(strings::rlocate_not(""sv, "abcdef"sv), 0U);
    pos = s.size();
    EXPECT_EQ(strings::rlocated_not(pos, s, 'a'), true);
    EXPECT_EQ(pos, 4U);
    --pos;
    EXPECT_EQ(strings::rlocated_not(pos, s, 'a'), false);
    EXPECT_EQ(pos, npos);
  }
  if (true) {
    using location = corvid::strings::location;
    constexpr auto s1 = "abaac"sv;
    const auto ab = {'a', 'b'};
    EXPECT_EQ(strings::rlocate_not(s1, ab), (location{4U, 2U}));
    EXPECT_EQ(strings::rlocate_not("abab"sv, ab), nloc);

    constexpr auto s2 = "abxcdef"sv;
    const auto abcd = {"ab"sv, "cd"sv};
    EXPECT_EQ(strings::locate_not(s2, abcd), (location{1U, 2U}));
    EXPECT_EQ(strings::rlocate_not(s2, abcd), (location{6U, 2U}));
    EXPECT_EQ(strings::locate_not(s2, {"", "ab"sv}), nloc);
  }
  if (true) {
    // located(ch).
    constexpr auto t = "abcabcabc"sv;
    size_t pos{};
    const auto a = 'a';
    EXPECT_EQ(strings::located(pos, t, a), true);
    EXPECT_EQ(pos, 0U);
    ++pos;
    EXPECT_EQ(strings::located(pos, t, a), true);
    EXPECT_EQ(pos, 3U);
    ++pos;
    EXPECT_EQ(strings::located(pos, t, a), true);
    EXPECT_EQ(pos, 6U);
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
    EXPECT_EQ(pos, 6U);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, a), true);
    EXPECT_EQ(pos, 3U);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, a), true);
    EXPECT_EQ(pos, 0U);
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
    EXPECT_EQ(pos, 0U);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 3U);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 6U);
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
    EXPECT_EQ(pos, 6U);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 3U);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 0U);
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
    EXPECT_EQ(pos, 0U);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 3U);
    strings::point_past(pos, abc);
    EXPECT_EQ(strings::located(pos, t, abc), true);
    EXPECT_EQ(pos, 6U);
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
    EXPECT_EQ(pos, 6U);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 3U);
    --pos;
    EXPECT_EQ(strings::rlocated(pos, t, abc), true);
    EXPECT_EQ(pos, 0U);
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
    EXPECT_EQ(loc.pos, 2U);
    ++loc.pos;
    EXPECT_EQ(strings::located(loc, s, xy), true);
    EXPECT_EQ(loc.pos, 6U);
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
    EXPECT_EQ(loc.pos, 6U);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, xy), true);
    EXPECT_EQ(loc.pos, 2U);
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
    EXPECT_EQ(loc.pos, 0U);
    EXPECT_EQ(loc.pos_value, 0U);
    strings::point_past(loc, abcbc);
    EXPECT_EQ(strings::located(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 3U);
    EXPECT_EQ(loc.pos_value, 0U);
    strings::point_past(loc, abcbc);
    EXPECT_EQ(strings::located(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 5U);
    EXPECT_EQ(loc.pos_value, 1U);
    strings::point_past(loc, abcbc);
    EXPECT_EQ(strings::located(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 8U);
    EXPECT_EQ(loc.pos_value, 0U);
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
    EXPECT_EQ(loc.pos, 8U);
    EXPECT_EQ(loc.pos_value, 0U);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 5U);
    EXPECT_EQ(loc.pos_value, 1U);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 3U);
    EXPECT_EQ(loc.pos_value, 0U);
    --loc.pos;
    EXPECT_EQ(strings::rlocated(loc, s, abcbc), true);
    EXPECT_EQ(loc.pos, 0U);
    EXPECT_EQ(loc.pos_value, 0U);
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
    EXPECT_EQ(strings::count_located(s, 'a'), 5U);
    EXPECT_EQ(strings::count_located(s, 'b'), 2U);
    EXPECT_EQ(strings::count_located(s, "def"), 2U);
    EXPECT_EQ(strings::count_located(s, "aa"), 1U);

    EXPECT_EQ(strings::count_located(s, "def"sv), 2U);
    EXPECT_EQ(strings::count_located(s, "def"s), 2U);
    const auto axy = std::array<const std::string_view, 2>{"x"sv, "y"sv};
    EXPECT_EQ(strings::count_located(s, axy), 1U);
    const auto sxy = std::span<const std::string_view>{axy};
    EXPECT_EQ(strings::count_located(s, sxy), 1U);
    EXPECT_EQ(strings::count_located(s, ""), 24U);
    EXPECT_EQ(strings::count_located("aaaaaaaa"sv, "a"sv), 8U);
    EXPECT_EQ(strings::count_located("aaaaaaaa"sv, "aa"sv), 4U);
    const auto a0 = std::array<const std::string_view, 0>{};
    EXPECT_EQ(strings::count_located(s, a0), 0U);
    const auto s0 = std::span<const std::string_view>{a0};
    EXPECT_EQ(strings::count_located(s, s0), 0U);
    EXPECT_EQ(strings::count_located(s, {""sv}), 24U);
    EXPECT_EQ(strings::count_located(s, {""}), 24U);
  }
}

void StringUtilsTest_RLocate() {
  using location = corvid::strings::location;
  // These tests are abbreviated because we only want to confirm algorithmic
  // correctness, not test for all those tricky overloads.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    EXPECT_EQ(strings::locate(s, "def"sv), 3U);
    EXPECT_EQ(strings::rlocate(s, "def"sv), 13U);
    EXPECT_EQ(strings::locate(s, 'd'), 3U);
    EXPECT_EQ(strings::rlocate(s, 'd'), 13U);
    EXPECT_EQ(s[13], 'd');
    location loc;
    EXPECT_EQ(loc.pos, 0U);
  }
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    EXPECT_EQ(strings::rlocate(s, 'j'), 19U);
    EXPECT_EQ(strings::rlocate(s, 'j', npos), 19U);
    EXPECT_EQ(strings::rlocate(s, 'j', 0U), npos);
    EXPECT_EQ(strings::rlocate(s, 'j', 25U), 19U);
    EXPECT_EQ(strings::rlocate(s, 'j', 18U), 9U);
    EXPECT_EQ(strings::rlocate(s, 'a'), 10U);
    EXPECT_EQ(strings::rlocate(s, 'a', 10U), 10U);
    EXPECT_EQ(strings::rlocate(s, 'a', 1U), 0U);
    EXPECT_EQ(s.rfind('a', 0U), 0U);
    EXPECT_EQ(strings::rlocate(s, 'a', 0U), 0U);
    EXPECT_EQ(strings::rlocate(s, "j"), 19U);
    EXPECT_EQ(strings::rlocate(s, "j", npos), 19U);
    EXPECT_EQ(strings::rlocate(s, "j", 0U), npos);
    EXPECT_EQ(strings::rlocate(s, "j", 25U), 19U);
    EXPECT_EQ(strings::rlocate(s, "j", 18U), 9U);
    EXPECT_EQ(strings::rlocate(s, "a"), 10U);
    EXPECT_EQ(strings::rlocate(s, "a", 10U), 10U);
    EXPECT_EQ(strings::rlocate(s, "a", 1U), 0U);
    EXPECT_EQ(s.rfind("a", 0U), 0U);
    EXPECT_EQ(strings::rlocate(s, "a", 0U), 0U);
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}), (location{19U, 1U}));
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}, npos), (location{19U, 1U}));
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}, 0U), (location{npos, npos}));
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}, 25U), (location{19U, 1U}));
    EXPECT_EQ(s.rfind('i', 18U), 18U);
    EXPECT_EQ(s.rfind('j', 18U), 9U);
    EXPECT_EQ(strings::rlocate(s, {'i', 'j'}, 18U), (location{18U, 0U}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}), (location{11U, 1U}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 13), (location{11U, 1U}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 12), (location{11U, 1U}));
    EXPECT_EQ(s.rfind('b', 12U), 11U);
    EXPECT_EQ(s.rfind('b', 11U), 11U);
    EXPECT_EQ(s.rfind('b', 10U), 1U);
    EXPECT_EQ(s.rfind('b', 9U), 1U);
    EXPECT_EQ(s.rfind('a', 0U), 0U);
    EXPECT_EQ(s.rfind('b', 0U), npos);
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 11U), (location{11U, 1U}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 10U), (location{10U, 0U}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 1U), (location{1U, 1U}));
    EXPECT_EQ(strings::rlocate(s, {'a', 'b'}, 0U), (location{0U, 0U}));
  }
}

void StringUtilsTest_LocateEdges() {
  using location = corvid::strings::location;
  // Test for using size as npos.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    EXPECT_EQ(strings::locate(s, 'a'), 0U);
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
  // Test that passing an initializer list of string literals (without sv
  // suffix) works correctly via implicit conversion to string_view.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    // locate with psz (string literals) should match sv behavior.
    EXPECT_EQ(strings::locate(s, {"ab", "cd"}), (location{0U, 0U}));
    EXPECT_EQ(strings::locate(s, {"cd", "ab"}), (location{0U, 1U}));
    EXPECT_EQ(strings::locate(s, {"xy", "zz"}), nloc);
    // rlocate with psz.
    EXPECT_EQ(strings::rlocate(s, {"ab", "cd"}), (location{12U, 1U}));
    EXPECT_EQ(strings::rlocate(s, {"cd", "ab"}), (location{12U, 0U}));
    EXPECT_EQ(strings::rlocate(s, {"xy", "zz"}), nloc);
  }
  // Confirm the correctness of infinite loops.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    EXPECT_EQ(s.find("a"), 0U);
    EXPECT_EQ(strings::locate(s, "a"), 0U);
    EXPECT_EQ(s.find(""), 0U);
    EXPECT_EQ(strings::locate(s, ""), 0U);
    EXPECT_EQ(strings::locate(s, {""sv, ""sv}), (location{0U, 0U}));
    EXPECT_EQ(strings::locate(s, std::array<std::string_view, 0>{}), nloc);
  }
}

void StringUtilsTest_Substitute() {
  if (true) {
    // substitute: ch, psz, s, sv.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, 'a', 'b'), 2U);
    EXPECT_EQ(s, "bbcdefghijbbcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def", "abc"), 2U);
    EXPECT_EQ(s, "abcabcghijabcabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"s, "abc"s), 2U);
    EXPECT_EQ(s, "abcabcghijabcabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, "abc"sv), 2U);
    EXPECT_EQ(s, "abcabcghijabcabcghij");
  }
  if (true) {
    // substitute: init<ch>, array<ch>, span<ch>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {'a'}, {'b'}), 2U);
    EXPECT_EQ(s, "bbcdefghijbbcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {'a', 'b'}, {'b', 'a'}), 4U);
    EXPECT_EQ(s, "bacdefghijbacdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {'a', 'y', 'c'}, {'y', 'a', 'z'}), 4U);
    EXPECT_EQ(s, "ybzdefghijybzdefghij");
    const auto axy = std::array<const char, 2>{'x', 'y'};
    const auto ayz = std::array<const char, 2>{'y', 'z'};
    s = "abcdefghijabxdefghijaaa";
    EXPECT_EQ(strings::substitute(s, axy, ayz), 1U);
    EXPECT_EQ(s, "abcdefghijabydefghijaaa");
    s = "abcdefghijabxdefghijaaa";
    const auto sxy = std::span<const char>{axy};
    const auto syz = std::span<const char>{ayz};
    EXPECT_EQ(strings::substitute(s, sxy, syz), 1U);
    EXPECT_EQ(s, "abcdefghijabydefghijaaa");
  }
  if (true) {
    // substitute: init<sv>, array<psz>, array<s>, array<sv>, span<sv>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {"ab"sv, "xz"sv, "cd"sv},
                  {"cd"sv, "za"sv, "ab"sv}),
        4U);
    EXPECT_EQ(s, "cdabefghijcdabefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {"ab", "xz", "cd"}, {"cd", "za", "ab"}),
        4U);
    EXPECT_EQ(s, "cdabefghijcdabefghij");
    s = std::string{sv};
    EXPECT_EQ(
        strings::substitute(s, {"ab"s, "xz"s, "cd"s}, {"cd"s, "za"s, "ab"s}),
        4U);
    EXPECT_EQ(s, "cdabefghijcdabefghij");

    // We can't support vector<s>:
    // * strings::substitute(s, f, t),
    s = std::string{sv};
    const auto f = std::vector{"ab"s, "xz"s, "cd"s};
    const auto t = std::vector{"cd"s, "za"s, "ab"s};
    // But we can allow explicit conversion to vector<sv>.
    EXPECT_EQ(
        strings::substitute(s, strings::as_views(f), strings::as_views(t)),
        4U);
    EXPECT_EQ(s, "cdabefghijcdabefghij");

    s = std::string{sv};
    const auto aabcd = std::array<const std::string_view, 2>{"ab"sv, "cd"sv};
    const auto acdab = std::array<const std::string_view, 2>{"cd"sv, "ab"sv};
    EXPECT_EQ(strings::substitute(s, aabcd, acdab), 4U);
    EXPECT_EQ(s, "cdabefghijcdabefghij");
    s = std::string{sv};
    const auto sabcd = std::span<const std::string_view>{aabcd};
    const auto scdab = std::span<const std::string_view>{acdab};
    EXPECT_EQ(strings::substitute(s, sabcd, scdab), 4U);
    EXPECT_EQ(s, "cdabefghijcdabefghij");
  }
  if (true) {
    std::string s;
    s = "abcdefghij";
    EXPECT_EQ(0U, strings::substitute(s, "bac", "yyy"));
    EXPECT_EQ(s, "abcdefghij");
    EXPECT_EQ(1U, strings::substitute(s, "abc", "yyy"));
    EXPECT_EQ(s, "yyydefghij");
    EXPECT_EQ(3U, strings::substitute(s, "y", "z"));
    EXPECT_EQ(s, "zzzdefghij");
    EXPECT_EQ(3U, strings::substitute(s, 'z', 'x'));
    EXPECT_EQ(s, "xxxdefghij");
    EXPECT_EQ(strings::substituted("abcdef", "abc", "yyy"), "yyydef");
    EXPECT_EQ(strings::substituted("abba", {'a', 'b'}, {'b', 'a'}), "baab");
    EXPECT_EQ(strings::substituted("abcd", {"ab"sv, "cd"sv}, {"xy"sv, "zz"sv}),
        "xyzz");
  }
  if (true) {
    constexpr auto sv = "aaaaaaaaaa"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "a"sv, "b"sv), 10U);
    EXPECT_EQ(s, "bbbbbbbbbb");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "a"sv, ""sv), 10U);
    EXPECT_EQ(s, "");
  }
  if (true) {
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, "ab"sv), 2U);
    EXPECT_EQ(s, "abcabghijabcabghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, "a"sv), 2U);
    EXPECT_EQ(s, "abcaghijabcaghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, ""sv), 2U);
    EXPECT_EQ(s, "abcghijabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "def"sv, "abcd"sv), 2U);
    EXPECT_EQ(s, "abcabcdghijabcabcdghij");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "de"sv, "abcd"sv), 2U);
    EXPECT_EQ(s, "abcabcdfghijabcabcdfghij");
  }
  if (true) {
    // Test of Pythonic behavior.
    constexpr auto sv = "abcdef"sv;
    auto s = std::string{sv};
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, ""sv, "x"sv), 7U);
    EXPECT_EQ(s, "xaxbxcxdxexfx");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, ""sv, "xy"sv), 7U);
    EXPECT_EQ(s, "xyaxybxycxydxyexyfxy");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, "c"sv, ""sv), 1U);
    EXPECT_EQ(s, "abdef");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, ""sv, ""sv), 7U);
    EXPECT_EQ(s, "abcdef");
    //
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {""sv}, {"x"sv}), 7U);
    EXPECT_EQ(s, "xaxbxcxdxexfx");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {""sv}, {"xy"sv}), 7U);
    EXPECT_EQ(s, "xyaxybxycxydxyexyfxy");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {"c"sv}, {""sv}), 1U);
    EXPECT_EQ(s, "abdef");
    s = std::string{sv};
    EXPECT_EQ(strings::substitute(s, {""sv}, {""sv}), 7U);
    EXPECT_EQ(s, "abcdef");
  }
}

void StringUtilsTest_Excise() {
  if (true) {
    // excise: ch, psz, s, sv.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, 'a'), 2U);
    EXPECT_EQ(s, "bcdefghijbcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "def"), 2U);
    EXPECT_EQ(s, "abcghijabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "def"s), 2U);
    EXPECT_EQ(s, "abcghijabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "def"sv), 2U);
    EXPECT_EQ(s, "abcghijabcghij");
  }
  if (true) {
    // excise: init<ch>, array<ch>, span<ch>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {'a'}), 2U);
    EXPECT_EQ(s, "bcdefghijbcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {'a', 'b'}), 4U);
    EXPECT_EQ(s, "cdefghijcdefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {'a', 'y', 'c'}), 4U);
    EXPECT_EQ(s, "bdefghijbdefghij");
    const auto axy = std::array<const char, 2>{'x', 'y'};
    s = "abcdefghijabxdefghijaaa";
    EXPECT_EQ(strings::excise(s, axy), 1U);
    EXPECT_EQ(s, "abcdefghijabdefghijaaa");
    s = "abcdefghijabxdefghijaaa";
    const auto sxy = std::span<const char>{axy};
    EXPECT_EQ(strings::excise(s, sxy), 1U);
    EXPECT_EQ(s, "abcdefghijabdefghijaaa");
    EXPECT_EQ(strings::excised(s, 'x'), "abcdefghijabdefghijaaa");
    EXPECT_EQ(strings::excised("abba", {'a', 'b'}), "");
    EXPECT_EQ(strings::excised("abcdabcd", {"ab"sv, "cd"sv}), "");
  }
  if (true) {
    // excise: init<sv>, array<sv>, span<sv>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {"ab"sv, "xz"sv, "cd"sv}), 4U);
    EXPECT_EQ(s, "efghijefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {"ab", "xz", "cd"}), 4U);
    EXPECT_EQ(s, "efghijefghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {"ab"s, "xz"s, "cd"s}), 4U);
    EXPECT_EQ(s, "efghijefghij");

    // We can't support vector<s>:
    // strings::excise(s, f, t),
    s = std::string{sv};
    const auto f = std::vector{"ab"s, "xz"s, "cd"s};
    // But we can allow explicit conversion to vector<sv>.
    EXPECT_EQ(strings::excise(s, strings::as_views(f)), 4U);
    EXPECT_EQ(s, "efghijefghij");
    s = std::string{sv};
    const auto aabcd = std::array<const std::string_view, 2>{"ab"sv, "cd"sv};
    EXPECT_EQ(strings::excise(s, aabcd), 4U);
    EXPECT_EQ(s, "efghijefghij");
    s = std::string{sv};
    const auto sabcd = std::span<const std::string_view>{aabcd};
    EXPECT_EQ(strings::excise(s, sabcd), 4U);
    EXPECT_EQ(s, "efghijefghij");
  }
  if (true) {
    std::string s;
    s = "abcdefghij";
    EXPECT_EQ(strings::excise(s, "bac"), 0U);
    EXPECT_EQ(s, "abcdefghij");
    EXPECT_EQ(strings::excise(s, "abc"), 1U);
    EXPECT_EQ(s, "defghij");
    EXPECT_EQ(strings::excise(s, 'e'), 1U);
    EXPECT_EQ(s, "dfghij");
    EXPECT_EQ(strings::substituted("abcdef", "abc", "yyy"), "yyydef");
  }
  if (true) {
    constexpr auto sv = "aaaaaaaaaa"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "a"sv), 10U);
    EXPECT_EQ(s, "");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, ""sv), 10U);
    EXPECT_EQ(s, "");
  }
  if (true) {
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "def"sv), 2U);
    EXPECT_EQ(s, "abcghijabcghij");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, "de"sv), 2U);
    EXPECT_EQ(s, "abcfghijabcfghij");
  }
  if (true) {
    // Test of Pythonic behavior.
    constexpr auto sv = "abcdef"sv;
    auto s = std::string{sv};
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, ""sv), 6U);
    EXPECT_EQ(s, "");
    s = std::string{sv};
    EXPECT_EQ(strings::excise(s, {""sv, "c"sv}), 6U);
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
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::print_with(", ", 42);
    EXPECT_EQ(ss.str(), "42");
  }
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::println_with(", ", 42);
    EXPECT_EQ(ss.str(), "42\n");
  }
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cerr_to_ss(std::cerr, ss);
    strings::report_with(", ", 42);
    EXPECT_EQ(ss.str(), "42\n");
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

void StringUtilsTest_OstreamRedirectorTraits() {
  using R = strings::ostream_redirector;
  static_assert(!std::is_copy_constructible_v<R>);
  static_assert(!std::is_copy_assignable_v<R>);
  static_assert(!std::is_move_constructible_v<R>);
  static_assert(!std::is_move_assignable_v<R>);
}

void StringUtilsTest_OstreamRedirectorRestore() {
  auto* orig = std::cout.rdbuf();
  {
    std::stringstream ss;
    {
      strings::ostream_redirector r(std::cout, ss);
      std::cout << "abc";
      EXPECT_EQ(ss.str(), "abc");
      EXPECT_FALSE(std::cout.rdbuf() == orig);
    }
    EXPECT_TRUE(std::cout.rdbuf() == orig);
  }
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
    EXPECT_EQ(vs[0], "1");

    vsv = strings::split(w, ",");
    std::map<int, std::string> mss;
    for (size_t i = 0; i < vsv.size(); ++i) {
      mss[static_cast<int>(i)] = vsv[i];
    }
    EXPECT_EQ(mss[0], " 1");
    strings::trim(mss);
    EXPECT_EQ(mss[0], "1");
  }
  if (true) {
    std::string s{"  abc  "};
    strings::trim_left(s);
    EXPECT_EQ(s, "abc  ");
    s = "  abc  ";
    strings::trim_right(s);
    EXPECT_EQ(s, "  abc");
    s = "  abc  ";
    strings::trim(s);
    EXPECT_EQ(s, "abc");
  }
}

void StringUtilsTest_AddBraces() {
  if (true) {
    EXPECT_EQ(strings::add_braces(""), "[]");
    EXPECT_EQ(strings::add_braces("1"), "[1]");
    EXPECT_EQ(strings::add_braces("12"), "[12]");
    EXPECT_EQ(strings::add_braces("1", "{}"), "{1}");
    EXPECT_EQ(strings::add_braces("12", "{}"), "{12}");
    EXPECT_EQ(strings::add_braces("12", "'"), "'12'");
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
    EXPECT_EQ(r.value_or(-1), 123);
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
    EXPECT_EQ(ot.value_or(-1), 123);

    // Verify default values with various integral types
    sv = "77";
    char c = strings::parse_num<char>(sv, 'x');
    EXPECT_EQ(c, 77);
    sv = "x";
    c = strings::parse_num<char>(sv, 'x');
    EXPECT_EQ(c, 'x');

    sv = "42";
    auto us = strings::parse_num<unsigned short>(sv, 7);
    EXPECT_EQ(us, 42);
    sv = "foo";
    us = strings::parse_num<unsigned short>(sv, 7);
    EXPECT_EQ(us, 7);
  }
  if (true) {
    std::string_view sv;
    double t;
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

#ifdef ONLY_WORKED_ON_MSVC
    std::string s;
    // strings::append<std::chars_format::hex>(s, 12.3L);
    s = "1.899999999999ap+3";
    sv = s;
    EXPECT_EQ(sv, "1.899999999999ap+3");
    // Succeed with totally wrong answer.
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(sv, "ap+3");
    sv = s;
    EXPECT_EQ(t, 1.8999999999990000L);
    EXPECT_TRUE(strings::extract_num<std::chars_format::hex>(t, sv));
    EXPECT_EQ(t, 12.3L);
    EXPECT_TRUE(sv.empty());
#endif

    sv = "12.3";
    auto r = strings::extract_num<double>(sv);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value_or(-1), 12.3);
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
    EXPECT_EQ(ot.value_or(-1), 12.3);
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
    EXPECT_EQ((strings::num_as_string(float(0.25F))), "0.25");
    EXPECT_EQ((strings::num_as_string(double(0.25F))), "0.25");
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

  enum ColorEnum : std::uint8_t { red, green = 20, blue };
  enum class ColorClass : std::uint8_t { red, green = 20, blue };

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
  EXPECT_EQ(strings::num_as_string(123.0L), "123");
  EXPECT_EQ(strings::num_as_string(12.3L), "12.3");

  EXPECT_EQ(strings::concat("1", "2"sv, "3"s), "123");
  EXPECT_EQ(strings::concat(1, 2.0, 3ULL), "123");
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
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
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

enum class rgb : std::uint8_t {
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
  s.clear();
  strings::append_join_with<strings::join_opt::braced, -1, -1>(s, ", ",
      rgb::yellow, rgb::cyan);
  EXPECT_EQ(s, "red + green, green + blue");
}

// Enlisted Marine Corps ranks.
enum class marine_rank : std::uint8_t {
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

// Enum with special characters in names for quote-encoding test.
enum class special_chars : int { normal, has_backslash, has_quote };

template<>
constexpr auto registry::enum_spec_v<special_chars> = make_sequence_enum_spec<
    special_chars, R"(normal, back\slash, has"quote)">();

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
  // Test quote-encoding when non-string wrapped in quotes.
  // Strings are escaped, but enums are not. This documents current behavior.
  if (true) {
    std::string s;

    // String with special chars gets escaped.
    strings::append_json(s, R"(back\slash)");
    EXPECT_EQ(s, R"("back\\slash")");
    s.clear();
    strings::append_json(s, R"(has"quote)");
    EXPECT_EQ(s, R"("has\"quote")");

    // Enum with special chars in name is NOT escaped (current behavior).
    s.clear();
    strings::append_json(s, special_chars::has_backslash);
    EXPECT_EQ(s, R"("back\slash")");
    s.clear();
    strings::append_json(s, special_chars::has_quote);
    EXPECT_EQ(s, R"("has"quote")");
  }
}

void raw_resize(std::string& s, size_t n) {
  s.clear();
  s.resize_and_overwrite(n, [&](char*, size_t n) { return n; });
  s.resize(n);
}

void StringUtilsTest_StdFromChars() {
  // Test std_from_chars directly for float.
  if (true) {
    float value{};
    std::string_view sv;

    // Basic positive float.
    sv = "3.14";
    auto result =
        strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value > 3.13f && value < 3.15f);
    EXPECT_EQ(result.ptr, sv.data() + sv.size());

    // Basic negative float.
    sv = "-2.5";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, -2.5f);
    EXPECT_EQ(result.ptr, sv.data() + sv.size());

    // Integer parsed as float.
    sv = "42";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, 42.0f);

    // Zero.
    sv = "0.0";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, 0.0f);

    // Negative zero.
    sv = "-0.0";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    // Negative zero should be equal to positive zero.
    EXPECT_EQ(value, 0.0f);

    // Scientific notation.
    sv = "1.5e3";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, 1500.0f);

    // Negative exponent.
    sv = "1.5e-2";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value > 0.014f && value < 0.016f);

    // Large positive exponent.
    sv = "1e30";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value > 9e29f);

    // Very small positive number.
    sv = "1e-30";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value > 0.0f && value < 1e-29f);

    // Partial parse (stops at non-numeric).
    sv = "3.14abc";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value > 3.13f && value < 3.15f);
    EXPECT_EQ(result.ptr, sv.data() + 4); // Stops at 'a'.
  }
  // Test std_from_chars directly for double.
  if (true) {
    double value{};
    std::string_view sv;

    // Basic positive double.
    sv = "3.141592653589793";
    auto result =
        strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value > 3.14159265358979 && value < 3.14159265358980);
    EXPECT_EQ(result.ptr, sv.data() + sv.size());

    // Basic negative double.
    sv = "-2.718281828";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value < -2.71828182 && value > -2.71828183);

    // Large double.
    sv = "1.7976931348623157e308";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value > 1e307);

    // Small positive double.
    sv = "2.2250738585072014e-308";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_TRUE(value > 0.0 && value < 1e-307);

    // Scientific notation with capital E.
    sv = "1.5E10";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, 1.5e10);

    // Leading decimal point.
    sv = ".5";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, 0.5);

    // Trailing decimal point.
    sv = "5.";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, 5.0);
  }
  // Test error handling.
  if (true) {
    float fvalue{42.0f};
    std::string_view sv;

    // Empty string - properly returns error.
    sv = "";
    auto result =
        strings::std_from_chars(sv.data(), sv.data() + sv.size(), fvalue);
    EXPECT_NE(result.ec, std::errc{});

    // Null first pointer - properly returns error.
    result = strings::std_from_chars(nullptr, sv.data(), fvalue);
    EXPECT_NE(result.ec, std::errc{});

    // first >= last - properly returns error.
    sv = "123";
    result = strings::std_from_chars(sv.data() + 2, sv.data(), fvalue);
    EXPECT_NE(result.ec, std::errc{});

    // Note: Invalid input like "abc" is handled by strtof/strtod which returns
    // 0.0 with endptr at start. The fallback implementation doesn't detect
    // this as an error (it sets value to 0.0 and returns success with ptr at
    // start). This is a known limitation of the fallback implementation.
    sv = "abc";
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), fvalue);
    // Fallback returns success with value 0 and ptr at start (no chars
    // consumed).
    EXPECT_EQ(result.ptr, sv.data());
  }
  // Test extract_num float wrappers (which use std_from_chars internally).
  if (true) {
    std::string_view sv;
    float f{};

    // Basic extraction.
    sv = "  3.14  ";
    EXPECT_TRUE(strings::extract_num(f, sv));
    EXPECT_TRUE(f > 3.13f && f < 3.15f);
    EXPECT_EQ(sv, "  "); // Whitespace trimmed from left, remaining on right.

    // Scientific notation.
    sv = "6.022e23";
    EXPECT_TRUE(strings::extract_num(f, sv));
    EXPECT_TRUE(f > 6e23f);
    EXPECT_TRUE(sv.empty());

    // Note: The fallback std_from_chars implementation has a limitation where
    // invalid input like "xyz" returns success with value 0.0 and ptr at
    // start. The extract_num wrapper checks for character consumption, so it
    // may still fail in some cases.
  }
  // Test parse_num float wrappers.
  if (true) {
    // Successful parse.
    auto opt = strings::parse_num<float>("2.5"sv);
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt.value(), 2.5f);

    // Failure - trailing garbage.
    opt = strings::parse_num<float>("2.5abc"sv);
    EXPECT_FALSE(opt.has_value());

    // Failure - invalid.
    opt = strings::parse_num<float>("invalid"sv);
    EXPECT_FALSE(opt.has_value());

    // With default value.
    float val = strings::parse_num<float>("1.5"sv, -1.0f);
    EXPECT_EQ(val, 1.5f);

    val = strings::parse_num<float>("bad"sv, -1.0f);
    EXPECT_EQ(val, -1.0f);

    val = strings::parse_num<float>("1.5 "sv, -1.0f);
    EXPECT_EQ(val, -1.0f); // Trailing space causes failure.
  }
  // Test parse_num double wrappers.
  if (true) {
    // Successful parse.
    auto opt = strings::parse_num<double>("3.141592653589793"sv);
    EXPECT_TRUE(opt.has_value());
    EXPECT_TRUE(opt.value() > 3.14159265358979);

    // Scientific notation.
    opt = strings::parse_num<double>("1e-100"sv);
    EXPECT_TRUE(opt.has_value());
    EXPECT_TRUE(opt.value() > 0.0 && opt.value() < 1e-99);

    // With default value.
    double val = strings::parse_num<double>("2.718281828"sv, 0.0);
    EXPECT_TRUE(val > 2.71828182 && val < 2.71828183);

    val = strings::parse_num<double>("xyz"sv, -999.0);
    EXPECT_EQ(val, -999.0);
  }
  // Test edge cases for buffer handling in std_from_chars fallback.
  if (true) {
    double value{};

    // String exactly at buffer boundary (127 chars).
    std::string long_num = "1.";
    long_num.append(125, '0');
    std::string_view sv = long_num;
    auto result =
        strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, 1.0);

    // String longer than internal buffer (should still work, truncated).
    long_num = "1.";
    long_num.append(200, '0');
    sv = long_num;
    result = strings::std_from_chars(sv.data(), sv.data() + sv.size(), value);
    EXPECT_EQ(result.ec, std::errc{});
    EXPECT_EQ(value, 1.0);
  }
}

void StringUtilsTest_RawBuffer() {
  std::string buffer;
  raw_resize(buffer, 4096U);
  EXPECT_EQ(buffer.size(), 4096U);
  EXPECT_GE(buffer.capacity(), 4096U);
}

MAKE_TEST_LIST(StringUtilsTest_ExtractPiece, StringUtilsTest_MorePieces,
    StringUtilsTest_Split, StringUtilsTest_SplitPg, StringUtilsTest_ParseNum,
    StringUtilsTest_AddBraces, StringUtilsTest_Case, StringUtilsTest_Locate,
    StringUtilsTest_RLocate, StringUtilsTest_LocateEdges,
    StringUtilsTest_Substitute, StringUtilsTest_Excise, StringUtilsTest_Target,
    StringUtilsTest_Print, StringUtilsTest_OstreamRedirectorTraits,
    StringUtilsTest_OstreamRedirectorRestore, StringUtilsTest_Trim,
    StringUtilsTest_AppendNum, StringUtilsTest_Append, StringUtilsTest_Edges,
    StringUtilsTest_Streams, StringUtilsTest_AppendEnum,
    StringUtilsTest_AppendStream, StringUtilsTest_AppendJson,
    StringUtilsTest_StdFromChars, StringUtilsTest_RawBuffer);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
