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

#include <cstdint>
#include <map>
#include <set>
#include <type_traits>

#include "../corvid/strings.h"
std::ostream&
operator<<(std::ostream& os, const corvid::strings::location& l) {
  return os << "location{" << l.pos << ", " << l.pos_value << "}";
}

#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;
using namespace corvid::enums::sequence;
using namespace corvid::enums::bitmask;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

// Test extract_piece.
#pragma region ExtractPiece

TEST_CASE("ExtractPiece", "[StringUtilsTest]") {
  std::string_view sv;
  CHECK(strings::extract_piece(sv, ",") == "");
  CHECK(strings::extract_piece(sv, ",") == "");
  sv = "1,2";
  CHECK(strings::extract_piece(sv, ",") == "1");
  CHECK(strings::extract_piece(sv, ",") == "2");
  CHECK(strings::extract_piece(sv, ",") == "");
  sv = ",";
  CHECK(sv.size() == 1U);
  CHECK(strings::extract_piece(sv, ",") == "");
  CHECK(strings::extract_piece(sv, ",") == "");

  sv = "1,2,3,4";
  CHECK(strings::extract_piece<std::string>(sv, ",") == "1");
}

#pragma endregion

// Test more_pieces.
#pragma region MorePieces

TEST_CASE("MorePieces", "[StringUtilsTest]") {
  std::string_view w, part;
  w = "1,2";
  CHECK_FALSE(w.empty());
  CHECK(strings::more_pieces(part, w, ","));
  CHECK(part == "1");
  CHECK_FALSE(w.empty());
  CHECK_FALSE(strings::more_pieces(part, w, ","));
  CHECK(part == "2");
  CHECK(w.empty());
  CHECK_FALSE(strings::more_pieces(part, w, ","));

  w = "1,";
  CHECK_FALSE(w.empty());
  CHECK(strings::more_pieces(part, w, ","));
  CHECK(part == "1");
  CHECK(w.empty());
  CHECK_FALSE(strings::more_pieces(part, w, ","));
  CHECK(part == "");
}

#pragma endregion

// Test split.
#pragma region Split

TEST_CASE("Split", "[StringUtilsTest]") {
  if (true) {
    using V = std::vector<std::string_view>;

    CHECK(strings::split(""sv, ",") == V{});
    CHECK(strings::split("1"sv, ",") == V{"1"});
    CHECK(strings::split("1,"sv, ",") == V{"1", ""});
    CHECK(strings::split(",1"sv, ",") == V{"", "1"});
    CHECK(strings::split(",,"sv, ",") == V{"", "", ""});
    CHECK(strings::split("1,2"sv, ",") == V{"1", "2"});
    CHECK(strings::split("1,2,3"sv, ",") == V{"1", "2", "3"});
    CHECK(strings::split("11"sv, ",") == V{"11"});
    CHECK(strings::split("11,"sv, ",") == V{"11", ""});
    CHECK(strings::split(",11"sv, ",") == V{"", "11"});
    CHECK(strings::split("11,22"sv, ",") == V{"11", "22"});
    CHECK(strings::split("11,22,33"sv, ",") == V{"11", "22", "33"});
  }
  if (true) {
    using V = std::vector<std::string>;
    using R = std::string;

    CHECK(strings::split<R>("", ",") == V{});
    CHECK(strings::split<R>("1", ",") == V{"1"});
    CHECK(strings::split<R>("1,", ",") == V{"1", ""});
    CHECK(strings::split<R>(",1", ",") == V{"", "1"});
    CHECK(strings::split<R>(",,", ",") == V{"", "", ""});
    CHECK(strings::split<R>("1,2", ",") == V{"1", "2"});
    CHECK(strings::split<R>("1,2,3", ",") == V{"1", "2", "3"});
    CHECK(strings::split<R>("11", ",") == V{"11"});
    CHECK(strings::split<R>("11,", ",") == V{"11", ""});
    CHECK(strings::split<R>(",11", ",") == V{"", "11"});
    CHECK(strings::split<R>("11,22", ",") == V{"11", "22"});
    CHECK(strings::split<R>("11,22,33", ",") == V{"11", "22", "33"});
  }
  if (true) {
    using V = std::vector<std::string_view>;
    using S = std::vector<std::string>;
    auto w = "1,2,3,4"sv;
    std::string s{w};
    CHECK(strings::split(w, ",") == V{"1", "2", "3", "4"});
    CHECK(strings::split(s, ",") == V{"1", "2", "3", "4"});
    CHECK(strings::split(std::move(s), ",") == S{"1", "2", "3", "4"});
  }
  if (true) {
    using R = std::string;
    using V = std::vector<R>;

    CHECK(strings::split<R>("11,22,33", ",") == V{"11", "22", "33"});
    CHECK(strings::split<R>(R{"11,22,33"}, ",") == V{"11", "22", "33"});
  }
}

#pragma endregion

// Test split_gen.
#pragma region SplitPg

TEST_CASE("SplitPg", "[StringUtilsTest]") {
  using PG = strings::piece_generator;
  if (true) {
    using V = std::vector<std::string_view>;

    CHECK(strings::split_gen(std::string_view{}) == V{});
    CHECK(strings::split_gen(opt_string_view{std::nullopt}) == V{});
    CHECK(strings::split_gen(0_osv) == V{});
    CHECK(strings::split_gen(""sv) == V{""});
    CHECK(strings::split_gen(""_osv) == V{""});
    CHECK(strings::split_gen("1"sv) == V{"1"});
    CHECK(strings::split_gen("1 "sv) == V{"1", ""});
    CHECK(strings::split_gen(" 1"sv) == V{"", "1"});
    CHECK(strings::split_gen("  1"sv) == V{"", "", "1"});
    CHECK(strings::split_gen("1 2"sv) == V{"1", "2"});
    CHECK(strings::split_gen("1 2 3"sv) == V{"1", "2", "3"});
    CHECK(strings::split_gen("11"sv) == V{"11"});
    CHECK(strings::split_gen("11 "sv) == V{"11", ""});
    CHECK(strings::split_gen(" 11"sv) == V{"", "11"});
    CHECK(strings::split_gen("11 22"sv) == V{"11", "22"});
    CHECK(strings::split_gen("11 22 33"sv) == V{"11", "22", "33"});
  }
  if (true) {
    using V = std::vector<std::string>;
    using R = std::string;
    using namespace strings;

    CHECK((split_gen<PG, R>(std::string_view{})) == V{});
    CHECK((split_gen<PG, R>((opt_string_view{std::nullopt}))) == V{});
    CHECK((split_gen<PG, R>((opt_string_view{std::nullopt}))) == V{});
    CHECK((split_gen<PG, R>((0_osv))) == V{});
    CHECK((split_gen<PG, R>((""sv))) == V{""});
    CHECK((split_gen<PG, R>((""_osv))) == V{""});
    CHECK((split_gen<PG, R>(("1"sv))) == V{"1"});
    CHECK((split_gen<PG, R>(("1 "sv))) == V{"1", ""});
    CHECK((split_gen<PG, R>((" 1"sv))) == V{"", "1"});
    CHECK((split_gen<PG, R>(("  1"sv))) == V{"", "", "1"});
    CHECK((split_gen<PG, R>(("1 2"sv))) == V{"1", "2"});
    CHECK((split_gen<PG, R>(("1 2 3"sv))) == V{"1", "2", "3"});
    CHECK((split_gen<PG, R>(("11"sv))) == V{"11"});
    CHECK((split_gen<PG, R>(("11 "sv))) == V{"11", ""});
    CHECK((split_gen<PG, R>((" 11"sv))) == V{"", "11"});
    CHECK((split_gen<PG, R>(("11 22"sv))) == V{"11", "22"});
    CHECK((split_gen<PG, R>(("11 22 33"sv))) == V{"11", "22", "33"});
  }
  if (true) {
    // Custom callables are stored without std::function erasure; CTAD deduces
    // the code unit and both callable types.
    strings::basic_piece_generator pg{"a b\tc"sv,
        [](std::string_view s) {
          auto loc = strings::locate(s, {' ', '\t', '\n', '\r'});
          return std::pair{loc.pos, loc.pos + 1};
        },
        [](std::string_view s) { return s; }};
    CHECK(strings::split(pg) == std::vector<std::string_view>{"a", "b", "c"});
  }
}

#pragma endregion

// Test splitting over a wide code unit.
#pragma region WideSplit

TEST_CASE("WideSplit", "[StringUtilsTest]") {
  // extract_piece defaults its return to a view of `whole`'s code unit.
  std::u16string_view sv = u"1,2";
  CHECK(strings::extract_piece(sv, u",") == u"1");
  CHECK(strings::extract_piece(sv, u",") == u"2");
  CHECK(sv.empty());

  // An owning return type is requested as the first template argument.
  sv = u"1,2";
  CHECK(strings::extract_piece<std::u16string>(sv, u",") == u"1");

  // more_pieces threads the same code unit.
  std::u16string_view w = u"a;b", part;
  CHECK(strings::more_pieces(part, w, u";"));
  CHECK(part == u"a");
  CHECK_FALSE(strings::more_pieces(part, w, u";"));
  CHECK(part == u"b");

  // split defaults to views; an owning element type makes deep copies.
  using V = std::vector<std::u16string_view>;
  using S = std::vector<std::u16string>;
  CHECK(strings::split(u"1,2,3", u",") == V{u"1", u"2", u"3"});
  CHECK(strings::split<std::u16string>(u"1,2,3", u",") == S{u"1", u"2", u"3"});

  // A temporary wide string is split into deep copies.
  CHECK(strings::split(std::u16string{u"1,2,3"}, u",") == S{u"1", u"2", u"3"});

  // split_gen over a wide generator (default whitespace delimiter).
  using PG = strings::basic_piece_generator<char16_t>;
  CHECK(strings::split_gen<PG>(u"1 2 3"sv) == V{u"1", u"2", u"3"});
}

#pragma endregion

// Test as_lower, as_upper.
#pragma region Case

TEST_CASE("Case", "[StringUtilsTest]") {
  auto s = "abcdefghij"s;
  strings::to_upper(s);
  CHECK(s == "ABCDEFGHIJ");
  strings::to_lower(s);
  CHECK(s == "abcdefghij");
  CHECK(strings::as_lower("ABCDEFGHIJ") == "abcdefghij");
  CHECK(strings::as_upper("abcdefghij") == "ABCDEFGHIJ");
  char a[] = "abcdefghij";
  strings::to_upper(a);
  CHECK(a == "ABCDEFGHIJ"sv);

  // Wide code units: same ASCII semantics on any character type.
  CHECK(strings::to_upper(u'a') == u'A');
  CHECK(strings::to_lower(U'Z') == U'z');
  CHECK(strings::is_digit(u'7'));
  CHECK(strings::is_alpha(U'q'));
  CHECK_FALSE(strings::is_upper(char16_t{0xe9})); // U+00E9, not ASCII
  auto w = u"abcXYZ"s;
  strings::to_upper(w);
  CHECK(w == u"ABCXYZ");

  // Deduced string-like helpers across code units.
  CHECK(strings::as_lower(u"MIXEDcase") == u"mixedcase");
  CHECK(strings::as_upper(U"MixedCase") == U"MIXEDCASE");
  CHECK(strings::ci_equal("HeLLo", "hello"s));
  CHECK(strings::ci_equal(u"HeLLo", u"hello"));
  CHECK_FALSE(strings::ci_equal(U"abc", U"abd"));

  // `string_view_wrapper` children flow through `char_type_of`/`as_view`
  // automatically, since they convert to a `std::basic_string_view`.
  CHECK(strings::as_upper("abc"_csv) == "ABC");
  CHECK(strings::ci_equal("Hi"_osv, "hi"));
}

#pragma endregion

// Test basic_delim over a wide code unit.
#pragma region WideDelim

TEST_CASE("WideDelim", "[StringUtilsTest]") {
  strings::basic_delim<char16_t> d{u",;"};
  CHECK(d.find_in(u"ab,cd") == 2);
  CHECK(d.find_not_in(u",,xy") == 2);
  CHECK(d.find_last_not_in(u"xy;,") == 1);

  std::u16string out;
  d.append(out);
  CHECK(out == u",;");

  // The default delimiter is a single space, whatever the code unit.
  CHECK(strings::basic_delim<char16_t>{}.find_in(u"ab cd") == 2);
}

#pragma endregion

// Test trimming over a wide code unit.
#pragma region WideTrim

TEST_CASE("WideTrim", "[StringUtilsTest]") {
  // Value-returning trims default to a view of the haystack's code unit.
  CHECK(strings::trim(u"  hi  ") == u"hi");
  CHECK(strings::trim_left(u"..xy", strings::basic_delim<char16_t>{u"."}) ==
        u"xy");
  CHECK(strings::trim_right(u"xy..", strings::basic_delim<char16_t>{u"."}) ==
        u"xy");

  // An owning return type is requested as the second template argument.
  const std::u16string owned =
      strings::trim<std::u16string_view, std::u16string>(u"  hi  "sv);
  CHECK(owned == u"hi");

  // In-place on a wide string (not misclassified as a container).
  std::u16string s = u"  hi  ";
  strings::trim(s);
  CHECK(s == u"hi");

  // Container of wide strings (explicit wide delimiter).
  std::vector<std::u16string> v{u" a ", u"  b"};
  strings::trim(v, strings::basic_delim<char16_t>{});
  CHECK(v[0] == u"a");
  CHECK(v[1] == u"b");

  // Braces.
  CHECK(strings::trim_braces(u"[x]") == u"x");
  CHECK(strings::add_braces(u"x") == u"[x]");
}

#pragma endregion

// Test locate.
#pragma region Locate

TEST_CASE("Locate", "[StringUtilsTest]") {
  using location = corvid::strings::location;
  if (true) {
    constexpr auto s = "abcdefghij"sv;
    constexpr auto l = s.size();
    // locate(psz).
    CHECK(strings::locate(s, "def") == 3U);
    //  Locate(sv).
    CHECK(strings::locate(s, "def"sv) == 3U);
    // Locate(ch).
    CHECK(strings::locate(s, 'd') == 3U);
    // Locate(init<ch>).
    CHECK(strings::locate(s, {'x', 'i', 'y'}) == location{8U, 1U});
    // Locate(span<ch>).
    CHECK(strings::locate(s, std::span{"xfz", 3}) == location{5U, 1U});

    // locate(array<ch>).
    // So this is supposed to return the location, which is a pos of 8 and a
    // value of 1, meaning 'i'. Instead, it's treating the array as a string.
    // Or, rather, as a SingleLocateValue = StringViewConvertible<T> ||
    // is_char_v<T>. So lets's sniff it out.
    CHECK((strings::locate(s, std::array{'x', 'i', 'y'})) ==
          ((location{8U, 1U})));
    // Locate(init<sv>).
    CHECK((strings::locate(s, {"a0c"sv, "def"s, "g0i"})) ==
          ((location{3U, 1U})));
    // Locate(vector<ch>).
    CHECK((strings::locate(s, std::vector{'x', 'i', 'y'})) ==
          ((location{8U, 1U})));

    // Edge cases.
    CHECK(strings::locate(s, "def", l) == npos);
    CHECK(strings::locate(s, "def", npos) == npos);
    //
    CHECK(strings::locate(s, 'd', l) == npos);
    CHECK(strings::locate(s, 'd', npos) == npos);
    //
    CHECK(strings::locate(s, {'x', 'i', 'y'}, l) == nloc);
    CHECK(strings::locate(s, {'x', 'i', 'y'}, npos) == nloc);
    //
    CHECK(strings::locate(s, {"a0c"sv, "def"s, "g0i"}, l) == nloc);
    CHECK(strings::locate(s, {"a0c"sv, "def"s, "g0i"}, npos) == nloc);
    //
    CHECK(strings::locate(s, "") == 0U);
    CHECK(strings::locate(s, "", l) == l);
    CHECK(strings::locate(s, "", l + 1) == npos);
    //
    CHECK(strings::locate(s, {"x", ""}) == location{0U, 1U});
    CHECK(strings::locate(s, {"x", ""}, l) == location{l, 1U});
    CHECK(strings::locate(s, {"x", ""}, l + 1) == nloc);
  }
  if (true) {
    //                  01234567
    constexpr auto s = "aaaabaaa"sv;
    CHECK(strings::locate_not(s, 'a') == 4U);
    CHECK(strings::locate_not(s, 'b') == 0U);
    CHECK(strings::locate_not("aaaaaa"sv, 'a') == npos);
    CHECK(strings::locate_not(s, "a") == 4U);
    CHECK(strings::locate_not(s, "aaaa") == 4U);
    CHECK(strings::locate_not(s, "aaaab") == 5U);
    CHECK(strings::locate_not(s, "b") == 0U);
    CHECK(strings::locate_not("aaaaaa"sv, "a") == npos);
    CHECK(strings::locate_not("aaaaaa"sv, "aa") == npos);
    size_t pos{};
    CHECK(strings::located_not(pos, s, 'a') == true);
    CHECK(pos == 4U);
    ++pos;
    CHECK(strings::located_not(pos, s, 'a') == false);
    CHECK(pos == npos);

    CHECK(strings::rlocate_not(s, 'a') == 4U);
    CHECK(strings::rlocate_not(s, 'b') == 7U);
    CHECK(strings::rlocate_not("aaaaaa"sv, 'a') == npos);
    CHECK(strings::rlocate_not(s, "a") == 4U);
    CHECK(strings::rlocate_not(s, "aaaa") == 4U);
    CHECK(strings::rlocate_not(s, "baaa") == 0U);
    CHECK(strings::rlocate_not(s, "aabaaa") == 0U);
    CHECK(strings::rlocate_not(s, "b") == 7U);
    CHECK(strings::rlocate_not("aaaaaa"sv, "a") == npos);
    CHECK(strings::rlocate_not("aaaaaa"sv, "aa") == npos);
    CHECK(strings::rlocate_not("abcde"sv, "de") == 1U);
    CHECK(strings::rlocate_not("abc"sv, "abcdef"sv) == 0U);
    CHECK(strings::rlocate_not(""sv, "abcdef"sv) == 0U);
    pos = s.size();
    CHECK(strings::rlocated_not(pos, s, 'a') == true);
    CHECK(pos == 4U);
    --pos;
    CHECK(strings::rlocated_not(pos, s, 'a') == false);
    CHECK(pos == npos);
  }
  if (true) {
    using location = corvid::strings::location;
    constexpr auto s1 = "abaac"sv;
    const auto ab = {'a', 'b'};
    CHECK(strings::rlocate_not(s1, ab) == location{4U, 2U});
    CHECK(strings::rlocate_not("abab"sv, ab) == nloc);

    constexpr auto s2 = "abxcdef"sv;
    const auto abcd = {"ab"sv, "cd"sv};
    CHECK(strings::locate_not(s2, abcd) == location{1U, 2U});
    CHECK(strings::rlocate_not(s2, abcd) == location{6U, 2U});
    CHECK(strings::locate_not(s2, {"", "ab"sv}) == nloc);
  }
  if (true) {
    // located(ch).
    constexpr auto t = "abcabcabc"sv;
    size_t pos{};
    const auto a = 'a';
    CHECK(strings::located(pos, t, a) == true);
    CHECK(pos == 0U);
    ++pos;
    CHECK(strings::located(pos, t, a) == true);
    CHECK(pos == 3U);
    ++pos;
    CHECK(strings::located(pos, t, a) == true);
    CHECK(pos == 6U);
    ++pos;
    CHECK(strings::located(pos, t, a) == false);
    CHECK(pos == npos);
  }
  if (true) {
    // rlocated(ch).
    constexpr auto t = "abcabcabc"sv;
    size_t pos = t.size();
    const auto a = 'a';
    CHECK(strings::rlocated(pos, t, a) == true);
    CHECK(pos == 6U);
    --pos;
    CHECK(strings::rlocated(pos, t, a) == true);
    CHECK(pos == 3U);
    --pos;
    CHECK(strings::rlocated(pos, t, a) == true);
    CHECK(pos == 0U);
    --pos;
    CHECK(strings::rlocated(pos, t, a) == false);
    CHECK(pos == npos);
  }
  if (true) {
    // located(psz).
    constexpr auto t = "abcabcabc"sv;
    size_t pos{};
    const auto abc = "abc";
    CHECK(strings::located(pos, t, abc) == true);
    CHECK(pos == 0U);
    strings::point_past(pos, abc);
    CHECK(strings::located(pos, t, abc) == true);
    CHECK(pos == 3U);
    strings::point_past(pos, abc);
    CHECK(strings::located(pos, t, abc) == true);
    CHECK(pos == 6U);
    strings::point_past(pos, abc);
    CHECK(strings::located(pos, t, abc) == false);
    CHECK(pos == npos);
  }
  if (true) {
    // rlocated(psz).
    constexpr auto t = "abcabcabc"sv;
    size_t pos = t.size();
    const auto abc = "abc";
    CHECK(strings::rlocated(pos, t, abc) == true);
    CHECK(pos == 6U);
    --pos;
    CHECK(strings::rlocated(pos, t, abc) == true);
    CHECK(pos == 3U);
    --pos;
    CHECK(strings::rlocated(pos, t, abc) == true);
    CHECK(pos == 0U);
    --pos;
    CHECK(strings::rlocated(pos, t, abc) == false);
    CHECK(pos == npos);
  }
  if (true) {
    // located(sv).
    constexpr auto t = "abcabcabc"sv;
    size_t pos{};
    const auto abc = "abc"sv;
    CHECK(strings::located(pos, t, abc) == true);
    CHECK(pos == 0U);
    strings::point_past(pos, abc);
    CHECK(strings::located(pos, t, abc) == true);
    CHECK(pos == 3U);
    strings::point_past(pos, abc);
    CHECK(strings::located(pos, t, abc) == true);
    CHECK(pos == 6U);
    strings::point_past(pos, abc);
    CHECK(strings::located(pos, t, abc) == false);
    CHECK(pos == npos);
  }
  if (true) {
    // rlocated(sv).
    constexpr auto t = "abcabcabc"sv;
    size_t pos = t.size();
    const auto abc = "abc"sv;
    CHECK(strings::rlocated(pos, t, abc) == true);
    CHECK(pos == 6U);
    --pos;
    CHECK(strings::rlocated(pos, t, abc) == true);
    CHECK(pos == 3U);
    --pos;
    CHECK(strings::rlocated(pos, t, abc) == true);
    CHECK(pos == 0U);
    --pos;
    CHECK(strings::rlocated(pos, t, abc) == false);
    CHECK(pos == npos);
  }
  if (true) {
    // located(init<ch>).
    constexpr auto s = "abxabcybc"sv;
    location loc;
    const auto xy = {'x', 'y'};
    CHECK(strings::located(loc, s, xy) == true);
    CHECK(loc.pos == 2U);
    ++loc.pos;
    CHECK(strings::located(loc, s, xy) == true);
    CHECK(loc.pos == 6U);
    ++loc.pos;
    CHECK(strings::located(loc, s, xy) == false);
    CHECK(loc.pos == npos);
    loc.pos = 0;
    // located(array<ch>).
    const auto axy = std::array<const char, 2>{'x', 'y'};
    CHECK(strings::located(loc, s, axy) == true);
    // located(span<ch>).
    const auto sxy = std::span<const char>{axy};
    CHECK(strings::located(loc, s, sxy) == true);
  }
  if (true) {
    // rlocated(init<ch>).
    constexpr auto s = "abxabcybc"sv;
    location loc = {s.size(), npos};
    const auto xy = {'x', 'y'};
    CHECK(strings::rlocated(loc, s, xy) == true);
    CHECK(loc.pos == 6U);
    --loc.pos;
    CHECK(strings::rlocated(loc, s, xy) == true);
    CHECK(loc.pos == 2U);
    --loc.pos;
    CHECK(strings::rlocated(loc, s, xy) == false);
    CHECK(loc.pos == npos);
    loc.pos = s.size();
    // located(array<ch>).
    const auto axy = std::array<const char, 2>{'x', 'y'};
    CHECK(strings::rlocated(loc, s, axy) == true);
    // located(span<ch>).
    const auto sxy = std::span<const char>{axy};
    CHECK(strings::rlocated(loc, s, sxy) == true);
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
    CHECK(strings::located(loc, s, abcbc) == true);
    CHECK(loc.pos == 0U);
    CHECK(loc.pos_value == 0U);
    strings::point_past(loc, abcbc);
    CHECK(strings::located(loc, s, abcbc) == true);
    CHECK(loc.pos == 3U);
    CHECK(loc.pos_value == 0U);
    strings::point_past(loc, abcbc);
    CHECK(strings::located(loc, s, abcbc) == true);
    CHECK(loc.pos == 5U);
    CHECK(loc.pos_value == 1U);
    strings::point_past(loc, abcbc);
    CHECK(strings::located(loc, s, abcbc) == true);
    CHECK(loc.pos == 8U);
    CHECK(loc.pos_value == 0U);
    strings::point_past(loc, abcbc);
    CHECK(strings::located(loc, s, abcbc) == false);
    loc.pos = 0;
    // located(array<sv>).
    const auto axy = std::array<const std::string_view, 2>{"x"sv, "y"sv};
    CHECK(strings::located(loc, s, axy) == true);
    // located(span<sv>).
    const auto sxy = std::span<const std::string_view>{axy};
    CHECK(strings::located(loc, s, sxy) == true);
  }
  if (true) {
    // rlocated(init<sv>).
    constexpr auto s = "abxabcbcab"sv;
    location loc{s.size(), npos};
    const auto abcbc = {"ab"sv, "cbc"sv};
    CHECK(strings::rlocated(loc, s, abcbc) == true);
    CHECK(loc.pos == 8U);
    CHECK(loc.pos_value == 0U);
    --loc.pos;
    CHECK(strings::rlocated(loc, s, abcbc) == true);
    CHECK(loc.pos == 5U);
    CHECK(loc.pos_value == 1U);
    --loc.pos;
    CHECK(strings::rlocated(loc, s, abcbc) == true);
    CHECK(loc.pos == 3U);
    CHECK(loc.pos_value == 0U);
    --loc.pos;
    CHECK(strings::rlocated(loc, s, abcbc) == true);
    CHECK(loc.pos == 0U);
    CHECK(loc.pos_value == 0U);
    --loc.pos;
    CHECK(strings::rlocated(loc, s, abcbc) == false);
    loc.pos = s.size();
    // located(array<sv>).
    const auto axy = std::array<const std::string_view, 2>{"x"sv, "y"sv};
    CHECK(strings::rlocated(loc, s, axy) == true);
    // located(span<sv>).
    const auto sxy = std::span<const std::string_view>{axy};
    CHECK(strings::rlocated(loc, s, sxy) == true);
  }
  if (true) {
    // count_located: ch, psz, sv, s, array<sv>, span<sv>.
    constexpr auto s = "abcdefghijabxdefghijaaa"sv;
    CHECK(strings::count_located(s, 'a') == 5U);
    CHECK(strings::count_located(s, 'b') == 2U);
    CHECK(strings::count_located(s, "def") == 2U);
    CHECK(strings::count_located(s, "aa") == 1U);

    CHECK(strings::count_located(s, "def"sv) == 2U);
    CHECK(strings::count_located(s, "def"s) == 2U);
    const auto axy = std::array<const std::string_view, 2>{"x"sv, "y"sv};
    CHECK(strings::count_located(s, axy) == 1U);
    const auto sxy = std::span<const std::string_view>{axy};
    CHECK(strings::count_located(s, sxy) == 1U);
    CHECK(strings::count_located(s, "") == 24U);
    CHECK(strings::count_located("aaaaaaaa"sv, "a"sv) == 8U);
    CHECK(strings::count_located("aaaaaaaa"sv, "aa"sv) == 4U);
    const auto a0 = std::array<const std::string_view, 0>{};
    CHECK(strings::count_located(s, a0) == 0U);
    const auto s0 = std::span<const std::string_view>{a0};
    CHECK(strings::count_located(s, s0) == 0U);
    CHECK(strings::count_located(s, {""sv}) == 24U);
    CHECK(strings::count_located(s, {""}) == 24U);
  }
}

#pragma endregion
#pragma region RLocate

TEST_CASE("RLocate", "[StringUtilsTest]") {
  using location = corvid::strings::location;
  // These tests are abbreviated because we only want to confirm algorithmic
  // correctness, not test for all those tricky overloads.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    CHECK(strings::locate(s, "def"sv) == 3U);
    CHECK(strings::rlocate(s, "def"sv) == 13U);
    CHECK(strings::locate(s, 'd') == 3U);
    CHECK(strings::rlocate(s, 'd') == 13U);
    CHECK(s[13] == 'd');
    location loc;
    CHECK(loc.pos == 0U);
  }
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    CHECK(strings::rlocate(s, 'j') == 19U);
    CHECK(strings::rlocate(s, 'j', npos) == 19U);
    CHECK(strings::rlocate(s, 'j', 0U) == npos);
    CHECK(strings::rlocate(s, 'j', 25U) == 19U);
    CHECK(strings::rlocate(s, 'j', 18U) == 9U);
    CHECK(strings::rlocate(s, 'a') == 10U);
    CHECK(strings::rlocate(s, 'a', 10U) == 10U);
    CHECK(strings::rlocate(s, 'a', 1U) == 0U);
    CHECK(s.rfind('a', 0U) == 0U);
    CHECK(strings::rlocate(s, 'a', 0U) == 0U);
    CHECK(strings::rlocate(s, "j") == 19U);
    CHECK(strings::rlocate(s, "j", npos) == 19U);
    CHECK(strings::rlocate(s, "j", 0U) == npos);
    CHECK(strings::rlocate(s, "j", 25U) == 19U);
    CHECK(strings::rlocate(s, "j", 18U) == 9U);
    CHECK(strings::rlocate(s, "a") == 10U);
    CHECK(strings::rlocate(s, "a", 10U) == 10U);
    CHECK(strings::rlocate(s, "a", 1U) == 0U);
    CHECK(s.rfind("a", 0U) == 0U);
    CHECK(strings::rlocate(s, "a", 0U) == 0U);
    CHECK(strings::rlocate(s, {'i', 'j'}) == location{19U, 1U});
    CHECK(strings::rlocate(s, {'i', 'j'}, npos) == location{19U, 1U});
    CHECK(strings::rlocate(s, {'i', 'j'}, 0U) == location{npos, npos});
    CHECK(strings::rlocate(s, {'i', 'j'}, 25U) == location{19U, 1U});
    CHECK(s.rfind('i', 18U) == 18U);
    CHECK(s.rfind('j', 18U) == 9U);
    CHECK(strings::rlocate(s, {'i', 'j'}, 18U) == location{18U, 0U});
    CHECK(strings::rlocate(s, {'a', 'b'}) == location{11U, 1U});
    CHECK(strings::rlocate(s, {'a', 'b'}, 13) == location{11U, 1U});
    CHECK(strings::rlocate(s, {'a', 'b'}, 12) == location{11U, 1U});
    CHECK(s.rfind('b', 12U) == 11U);
    CHECK(s.rfind('b', 11U) == 11U);
    CHECK(s.rfind('b', 10U) == 1U);
    CHECK(s.rfind('b', 9U) == 1U);
    CHECK(s.rfind('a', 0U) == 0U);
    CHECK(s.rfind('b', 0U) == npos);
    CHECK(strings::rlocate(s, {'a', 'b'}, 11U) == location{11U, 1U});
    CHECK(strings::rlocate(s, {'a', 'b'}, 10U) == location{10U, 0U});
    CHECK(strings::rlocate(s, {'a', 'b'}, 1U) == location{1U, 1U});
    CHECK(strings::rlocate(s, {'a', 'b'}, 0U) == location{0U, 0U});
  }
}

#pragma endregion
#pragma region LocateEdges

TEST_CASE("LocateEdges", "[StringUtilsTest]") {
  using location = corvid::strings::location;
  // Test for using size as npos.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    CHECK(strings::locate(s, 'a') == 0U);
    // npos, as pos, is just a placeholder for size.
    CHECK(strings::locate(s, 'a', npos) == npos);
    CHECK(strings::locate(s, 'a', s.size()) == npos);
    // We can choose to use the size as npos for returns.
    CHECK(strings::locate(s, 'z') == npos);
    CHECK(strings::locate<npos_choice::size>(s, 'z') == s.size());
    CHECK(strings::rlocate(s, 'z') == npos);
    CHECK(strings::rlocate<npos_choice::size>(s, 'z') == s.size());
    CHECK(strings::locate(s, "xyz"sv) == npos);
    CHECK(strings::locate<npos_choice::size>(s, "xyz"sv) == s.size());
    CHECK(strings::rlocate(s, "xyz"sv) == npos);
    CHECK(strings::rlocate<npos_choice::size>(s, "xyz"sv) == s.size());
    //
    CHECK(strings::locate(s, {'y', 'z'}) == location{npos, npos});
    CHECK((strings::locate<npos_choice::size>(s, {'y', 'z'})) ==
          ((location{s.size(), 2})));
    CHECK(strings::rlocate(s, {'y', 'z'}) == location{npos, npos});
    CHECK((strings::rlocate<npos_choice::size>(s, {'y', 'z'})) ==
          ((location{s.size(), 2})));
    CHECK(strings::locate(s, {"uvw"sv, "xyz"sv}) == location{npos, npos});
    CHECK((strings::locate<npos_choice::size>(s, {"uvw"sv, "xyz"sv})) ==
          ((location{s.size(), 2})));
    CHECK(strings::rlocate(s, {"uvw"sv, "xyz"sv}) == location{npos, npos});
    CHECK((strings::rlocate<npos_choice::size>(s, {"uvw"sv, "xyz"sv})) ==
          ((location{s.size(), 2})));
  }
  // Test that passing an initializer list of string literals (without sv
  // suffix) works correctly via implicit conversion to string_view.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    // locate with psz (string literals) should match sv behavior.
    CHECK(strings::locate(s, {"ab", "cd"}) == location{0U, 0U});
    CHECK(strings::locate(s, {"cd", "ab"}) == location{0U, 1U});
    CHECK(strings::locate(s, {"xy", "zz"}) == nloc);
    // rlocate with psz.
    CHECK(strings::rlocate(s, {"ab", "cd"}) == location{12U, 1U});
    CHECK(strings::rlocate(s, {"cd", "ab"}) == location{12U, 0U});
    CHECK(strings::rlocate(s, {"xy", "zz"}) == nloc);
  }
  // Confirm the correctness of infinite loops.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    CHECK(s.find("a") == 0U);
    CHECK(strings::locate(s, "a") == 0U);
    CHECK(s.find("") == 0U);
    CHECK(strings::locate(s, "") == 0U);
    CHECK(strings::locate(s, {""sv, ""sv}) == location{0U, 0U});
    CHECK(strings::locate(s, std::array<std::string_view, 0>{}) == nloc);
  }
}

#pragma endregion
#pragma region Substitute

TEST_CASE("Substitute", "[StringUtilsTest]") {
  if (true) {
    // substitute: ch, psz, s, sv.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    CHECK(strings::substitute(s, 'a', 'b') == 2U);
    CHECK(s == "bbcdefghijbbcdefghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, "def", "abc") == 2U);
    CHECK(s == "abcabcghijabcabcghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, "def"s, "abc"s) == 2U);
    CHECK(s == "abcabcghijabcabcghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, "def"sv, "abc"sv) == 2U);
    CHECK(s == "abcabcghijabcabcghij");
  }
  if (true) {
    // substitute: init<ch>, array<ch>, span<ch>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    CHECK(strings::substitute(s, {'a'}, {'b'}) == 2U);
    CHECK(s == "bbcdefghijbbcdefghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, {'a', 'b'}, {'b', 'a'}) == 4U);
    CHECK(s == "bacdefghijbacdefghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, {'a', 'y', 'c'}, {'y', 'a', 'z'}) == 4U);
    CHECK(s == "ybzdefghijybzdefghij");
    const auto axy = std::array<const char, 2>{'x', 'y'};
    const auto ayz = std::array<const char, 2>{'y', 'z'};
    s = "abcdefghijabxdefghijaaa";
    CHECK(strings::substitute(s, axy, ayz) == 1U);
    CHECK(s == "abcdefghijabydefghijaaa");
    s = "abcdefghijabxdefghijaaa";
    const auto sxy = std::span<const char>{axy};
    const auto syz = std::span<const char>{ayz};
    CHECK(strings::substitute(s, sxy, syz) == 1U);
    CHECK(s == "abcdefghijabydefghijaaa");
  }
  if (true) {
    // substitute: init<sv>, array<psz>, array<s>, array<sv>, span<sv>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    CHECK(strings::substitute(s, {"ab"sv, "xz"sv, "cd"sv},
              {"cd"sv, "za"sv, "ab"sv}) == 4U);
    CHECK(s == "cdabefghijcdabefghij");
    s = std::string{sv};
    CHECK((strings::substitute(s, {"ab", "xz", "cd"}, {"cd", "za", "ab"})) ==
          (4U));
    CHECK(s == "cdabefghijcdabefghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, {"ab"s, "xz"s, "cd"s},
              {"cd"s, "za"s, "ab"s}) == 4U);
    CHECK(s == "cdabefghijcdabefghij");

    // We can't support vector<s>:
    // * strings::substitute(s, f, t),
    s = std::string{sv};
    const auto f = std::vector{"ab"s, "xz"s, "cd"s};
    const auto t = std::vector{"cd"s, "za"s, "ab"s};
    // But we can allow explicit conversion to vector<sv>.
    CHECK(strings::substitute(s, strings::as_views(f), strings::as_views(t)) ==
          4U);
    CHECK(s == "cdabefghijcdabefghij");

    s = std::string{sv};
    const auto aabcd = std::array<const std::string_view, 2>{"ab"sv, "cd"sv};
    const auto acdab = std::array<const std::string_view, 2>{"cd"sv, "ab"sv};
    CHECK(strings::substitute(s, aabcd, acdab) == 4U);
    CHECK(s == "cdabefghijcdabefghij");
    s = std::string{sv};
    const auto sabcd = std::span<const std::string_view>{aabcd};
    const auto scdab = std::span<const std::string_view>{acdab};
    CHECK(strings::substitute(s, sabcd, scdab) == 4U);
    CHECK(s == "cdabefghijcdabefghij");
  }
  if (true) {
    std::string s;
    s = "abcdefghij";
    CHECK(0U == strings::substitute(s, "bac", "yyy"));
    CHECK(s == "abcdefghij");
    CHECK(1U == strings::substitute(s, "abc", "yyy"));
    CHECK(s == "yyydefghij");
    CHECK(3U == strings::substitute(s, "y", "z"));
    CHECK(s == "zzzdefghij");
    CHECK(3U == strings::substitute(s, 'z', 'x'));
    CHECK(s == "xxxdefghij");
    CHECK(strings::substituted("abcdef", "abc", "yyy") == "yyydef");
    CHECK(strings::substituted("abba", {'a', 'b'}, {'b', 'a'}) == "baab");
    CHECK((strings::substituted("abcd", {"ab"sv, "cd"sv}, {"xy"sv, "zz"sv})) ==
          ("xyzz"));
  }
  if (true) {
    constexpr auto sv = "aaaaaaaaaa"sv;
    auto s = std::string{sv};
    CHECK(strings::substitute(s, "a"sv, "b"sv) == 10U);
    CHECK(s == "bbbbbbbbbb");
    s = std::string{sv};
    CHECK(strings::substitute(s, "a"sv, ""sv) == 10U);
    CHECK(s == "");
  }
  if (true) {
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    CHECK(strings::substitute(s, "def"sv, "ab"sv) == 2U);
    CHECK(s == "abcabghijabcabghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, "def"sv, "a"sv) == 2U);
    CHECK(s == "abcaghijabcaghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, "def"sv, ""sv) == 2U);
    CHECK(s == "abcghijabcghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, "def"sv, "abcd"sv) == 2U);
    CHECK(s == "abcabcdghijabcabcdghij");
    s = std::string{sv};
    CHECK(strings::substitute(s, "de"sv, "abcd"sv) == 2U);
    CHECK(s == "abcabcdfghijabcabcdfghij");
  }
  if (true) {
    // Test of Pythonic behavior.
    constexpr auto sv = "abcdef"sv;
    auto s = std::string{sv};
    s = std::string{sv};
    CHECK(strings::substitute(s, ""sv, "x"sv) == 7U);
    CHECK(s == "xaxbxcxdxexfx");
    s = std::string{sv};
    CHECK(strings::substitute(s, ""sv, "xy"sv) == 7U);
    CHECK(s == "xyaxybxycxydxyexyfxy");
    s = std::string{sv};
    CHECK(strings::substitute(s, "c"sv, ""sv) == 1U);
    CHECK(s == "abdef");
    s = std::string{sv};
    CHECK(strings::substitute(s, ""sv, ""sv) == 7U);
    CHECK(s == "abcdef");
    //
    s = std::string{sv};
    CHECK(strings::substitute(s, {""sv}, {"x"sv}) == 7U);
    CHECK(s == "xaxbxcxdxexfx");
    s = std::string{sv};
    CHECK(strings::substitute(s, {""sv}, {"xy"sv}) == 7U);
    CHECK(s == "xyaxybxycxydxyexyfxy");
    s = std::string{sv};
    CHECK(strings::substitute(s, {"c"sv}, {""sv}) == 1U);
    CHECK(s == "abdef");
    s = std::string{sv};
    CHECK(strings::substitute(s, {""sv}, {""sv}) == 7U);
    CHECK(s == "abcdef");
  }
}

#pragma endregion
#pragma region Excise

TEST_CASE("Excise", "[StringUtilsTest]") {
  if (true) {
    // excise: ch, psz, s, sv.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    CHECK(strings::excise(s, 'a') == 2U);
    CHECK(s == "bcdefghijbcdefghij");
    s = std::string{sv};
    CHECK(strings::excise(s, "def") == 2U);
    CHECK(s == "abcghijabcghij");
    s = std::string{sv};
    CHECK(strings::excise(s, "def"s) == 2U);
    CHECK(s == "abcghijabcghij");
    s = std::string{sv};
    CHECK(strings::excise(s, "def"sv) == 2U);
    CHECK(s == "abcghijabcghij");
  }
  if (true) {
    // excise: init<ch>, array<ch>, span<ch>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    std::string s;
    s = std::string{sv};
    CHECK(strings::excise(s, {'a'}) == 2U);
    CHECK(s == "bcdefghijbcdefghij");
    s = std::string{sv};
    CHECK(strings::excise(s, {'a', 'b'}) == 4U);
    CHECK(s == "cdefghijcdefghij");
    s = std::string{sv};
    CHECK(strings::excise(s, {'a', 'y', 'c'}) == 4U);
    CHECK(s == "bdefghijbdefghij");
    const auto axy = std::array<const char, 2>{'x', 'y'};
    s = "abcdefghijabxdefghijaaa";
    CHECK(strings::excise(s, axy) == 1U);
    CHECK(s == "abcdefghijabdefghijaaa");
    s = "abcdefghijabxdefghijaaa";
    const auto sxy = std::span<const char>{axy};
    CHECK(strings::excise(s, sxy) == 1U);
    CHECK(s == "abcdefghijabdefghijaaa");
    CHECK(strings::excised(s, 'x') == "abcdefghijabdefghijaaa");
    CHECK(strings::excised("abba", {'a', 'b'}) == "");
    CHECK(strings::excised("abcdabcd", {"ab"sv, "cd"sv}) == "");
  }
  if (true) {
    // excise: init<sv>, array<sv>, span<sv>.
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    CHECK(strings::excise(s, {"ab"sv, "xz"sv, "cd"sv}) == 4U);
    CHECK(s == "efghijefghij");
    s = std::string{sv};
    CHECK(strings::excise(s, {"ab", "xz", "cd"}) == 4U);
    CHECK(s == "efghijefghij");
    s = std::string{sv};
    CHECK(strings::excise(s, {"ab"s, "xz"s, "cd"s}) == 4U);
    CHECK(s == "efghijefghij");

    // We can't support vector<s>:
    // strings::excise(s, f, t),
    s = std::string{sv};
    const auto f = std::vector{"ab"s, "xz"s, "cd"s};
    // But we can allow explicit conversion to vector<sv>.
    CHECK(strings::excise(s, strings::as_views(f)) == 4U);
    CHECK(s == "efghijefghij");
    s = std::string{sv};
    const auto aabcd = std::array<const std::string_view, 2>{"ab"sv, "cd"sv};
    CHECK(strings::excise(s, aabcd) == 4U);
    CHECK(s == "efghijefghij");
    s = std::string{sv};
    const auto sabcd = std::span<const std::string_view>{aabcd};
    CHECK(strings::excise(s, sabcd) == 4U);
    CHECK(s == "efghijefghij");
  }
  if (true) {
    std::string s;
    s = "abcdefghij";
    CHECK(strings::excise(s, "bac") == 0U);
    CHECK(s == "abcdefghij");
    CHECK(strings::excise(s, "abc") == 1U);
    CHECK(s == "defghij");
    CHECK(strings::excise(s, 'e') == 1U);
    CHECK(s == "dfghij");
    CHECK(strings::substituted("abcdef", "abc", "yyy") == "yyydef");
  }
  if (true) {
    constexpr auto sv = "aaaaaaaaaa"sv;
    auto s = std::string{sv};
    CHECK(strings::excise(s, "a"sv) == 10U);
    CHECK(s == "");
    s = std::string{sv};
    CHECK(strings::excise(s, ""sv) == 10U);
    CHECK(s == "");
  }
  if (true) {
    constexpr auto sv = "abcdefghijabcdefghij"sv;
    auto s = std::string{sv};
    CHECK(strings::excise(s, "def"sv) == 2U);
    CHECK(s == "abcghijabcghij");
    s = std::string{sv};
    CHECK(strings::excise(s, "de"sv) == 2U);
    CHECK(s == "abcfghijabcfghij");
  }
  if (true) {
    // Test of Pythonic behavior.
    constexpr auto sv = "abcdef"sv;
    auto s = std::string{sv};
    s = std::string{sv};
    CHECK(strings::excise(s, ""sv) == 6U);
    CHECK(s == "");
    s = std::string{sv};
    CHECK(strings::excise(s, {""sv, "c"sv}) == 6U);
    CHECK(s == "");
  }
}

#pragma endregion

template<AppendTarget T>
auto& test_append(T& target, std::string_view sv) {
  return *strings::appender{target}
              .append(sv)
              .append(sv[0])
              .append(sv.data(), sv.size())
              .append(4, sv[0]);
}

#pragma region Target

TEST_CASE("Target", "[StringUtilsTest]") {
  if (true) {
    std::ostringstream oss;
    CHECK(test_append(oss, "abc").str() == "abcaabcaaaa");
  }
  if (true) {
    std::string s;
    CHECK(test_append(s, "abc") == "abcaabcaaaa");
    strings::appender(s).reserve(500);
  }
#ifdef NOT_SUPPOSED_TO_COMPILE
  if (true) {
    int i;
    CHECK(test_append(i, "abc").str() == "abcaabcaaaa");
    strings::appender(i).append("abc");
  }
#endif
}

#pragma endregion

struct NotStreamable {};

#pragma region Print

TEST_CASE("Print", "[StringUtilsTest]") {
  if (true) {
    std::stringstream ss;
    strings::stream_out(ss, "abc=", 5, ';');
    CHECK(ss.str() == "abc=5;");
    strings::stream_out(ss);
    CHECK(ss.str() == "abc=5;");
    strings::stream_out(ss, '\n');
    CHECK(ss.str() == "abc=5;\n");
  }
  if (true) {
    std::stringstream ss;
    strings::stream_out_with(ss, ",", "abc", 5, "def", true);
    CHECK(ss.str() == "abc,5,def,1");

    ss.str("");
    strings::ostream_redirector cerr_to_ss(std::cerr, ss);
    strings::report("a=", 5);
    CHECK(ss.str() == "a=5\n");
  }
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::println("a=", 5);
    CHECK(ss.str() == "a=5\n");
    ss.str("");
    strings::println_with(", ", 'a', 5, "bc", 5.5);
    CHECK(ss.str() == "a, 5, bc, 5.5\n");
  }
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::print_with(", ", 42);
    CHECK(ss.str() == "42");
  }
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::println_with(", ", 42);
    CHECK(ss.str() == "42\n");
  }
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cerr_to_ss(std::cerr, ss);
    strings::report_with(", ", 42);
    CHECK(ss.str() == "42\n");
  }
#ifdef NOT_SUPPOSED_TO_COMPILE
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::print("a=", NotStreamable{});
    CHECK(ss.str() == "a=5");
  }
#endif
}

#pragma endregion
#pragma region OstreamRedirectorTraits

TEST_CASE("OstreamRedirectorTraits", "[StringUtilsTest]") {
  using R = strings::ostream_redirector;
  static_assert(!std::is_copy_constructible_v<R>);
  static_assert(!std::is_copy_assignable_v<R>);
  static_assert(!std::is_move_constructible_v<R>);
  static_assert(!std::is_move_assignable_v<R>);
}

#pragma endregion
#pragma region OstreamRedirectorRestore

TEST_CASE("OstreamRedirectorRestore", "[StringUtilsTest]") {
  auto* orig = std::cout.rdbuf();
  {
    std::stringstream ss;
    {
      strings::ostream_redirector r(std::cout, ss);
      std::cout << "abc";
      CHECK(ss.str() == "abc");
      CHECK_FALSE(std::cout.rdbuf() == orig);
    }
    CHECK(std::cout.rdbuf() == orig);
  }
}

#pragma endregion
#pragma region Trim

TEST_CASE("Trim", "[StringUtilsTest]") {
  if (true) {
    CHECK(strings::trim_left("") == "");
    CHECK(strings::trim_left("1") == "1");
    CHECK(strings::trim_left("12") == "12");
    CHECK(strings::trim_left("123") == "123");
    CHECK(strings::trim_left(" ") == "");
    CHECK(strings::trim_left(" 1") == "1");
    CHECK(strings::trim_left(" 12") == "12");
    CHECK(strings::trim_left(" 123") == "123");
    CHECK(strings::trim_left("  ") == "");
    CHECK(strings::trim_left("  1") == "1");
    CHECK(strings::trim_left("  12") == "12");
    CHECK(strings::trim_left("  123") == "123");
    CHECK(strings::trim_left("  1  ") == "1  ");

    CHECK(strings::trim_right("") == "");
    CHECK(strings::trim_right("1") == "1");
    CHECK(strings::trim_right("12") == "12");
    CHECK(strings::trim_right("123") == "123");
    CHECK(strings::trim_right(" ") == "");
    CHECK(strings::trim_right("1 ") == "1");
    CHECK(strings::trim_right("12 ") == "12");
    CHECK(strings::trim_right("123 ") == "123");
    CHECK(strings::trim_right("  ") == "");
    CHECK(strings::trim_right("1  ") == "1");
    CHECK(strings::trim_right("12  ") == "12");
    CHECK(strings::trim_right("123  ") == "123");
    CHECK(strings::trim_right("  1  ") == "  1");

    CHECK(strings::trim("") == "");
    CHECK(strings::trim("1") == "1");
    CHECK(strings::trim("12") == "12");
    CHECK(strings::trim("123") == "123");
    CHECK(strings::trim("  ") == "");
    CHECK(strings::trim(" 1 ") == "1");
    CHECK(strings::trim(" 12 ") == "12");
    CHECK(strings::trim(" 123 ") == "123");
    CHECK(strings::trim("    ") == "");
    CHECK(strings::trim("  1  ") == "1");
    CHECK(strings::trim("  12  ") == "12");
    CHECK(strings::trim("  123  ") == "123");
    CHECK(strings::trim("  1  ") == "1");

    CHECK(strings::trim_braces("[]") == "");
    CHECK(strings::trim_braces("[1]") == "1");
    CHECK(strings::trim_braces("[12]") == "12");
    CHECK(strings::trim_braces("12]") == "12]");
    CHECK(strings::trim_braces("[12]") == "12");
    CHECK(strings::trim_braces("'12'", "'") == "12");
  }
  if (true) {
    auto w = " 1, 2, 3  , 4 "s;

    auto vsv = strings::split(w, ",");
    CHECK(vsv[0] == " 1");
    //* auto x = string::trim(v);
    strings::trim(vsv);
    CHECK(vsv[0] == "1");

    auto vs = strings::split<std::string>(w, ",");
    strings::trim(vs);
    CHECK(vs[0] == "1");

    vsv = strings::split(w, ",");
    std::map<int, std::string> mss;
    for (size_t i = 0; i < vsv.size(); ++i) {
      mss[static_cast<int>(i)] = vsv[i];
    }
    CHECK(mss[0] == " 1");
    strings::trim(mss);
    CHECK(mss[0] == "1");
  }
  if (true) {
    std::string s{"  abc  "};
    strings::trim_left(s);
    CHECK(s == "abc  ");
    s = "  abc  ";
    strings::trim_right(s);
    CHECK(s == "  abc");
    s = "  abc  ";
    strings::trim(s);
    CHECK(s == "abc");
  }
}

#pragma endregion
#pragma region AddBraces

TEST_CASE("AddBraces", "[StringUtilsTest]") {
  if (true) {
    CHECK(strings::add_braces("") == "[]");
    CHECK(strings::add_braces("1") == "[1]");
    CHECK(strings::add_braces("12") == "[12]");
    CHECK(strings::add_braces("1", "{}") == "{1}");
    CHECK(strings::add_braces("12", "{}") == "{12}");
    CHECK(strings::add_braces("12", "'") == "'12'");
  }
}

#pragma endregion
#pragma region ParseNum

TEST_CASE("ParseNum", "[StringUtilsTest]") {
  if (true) {
    std::string_view sv;
    sv = "123";
    int64_t t;
    CHECK(strings::extract_num(t, sv));
    CHECK(t == 123);
    CHECK(sv.empty());

    sv = "123 456";
    CHECK(strings::extract_num(t, sv));
    CHECK(t == 123);
    CHECK_FALSE(sv.empty());
    CHECK(sv == " 456");
    CHECK(strings::extract_num(t, sv));
    CHECK(t == 456);
    CHECK(sv.empty());

    sv = "abc";
    CHECK_FALSE(strings::extract_num(t, sv));
    CHECK(sv == "abc");
    CHECK(strings::extract_num<16>(t, sv));
    CHECK(t == 0xabc);
    CHECK(sv.empty());

    sv = "123";
    auto r = strings::extract_num(sv);
    CHECK(r.has_value());
    CHECK(r.value_or(-1) == 123);
    CHECK(sv.empty());
    r = strings::extract_num(sv);
    CHECK_FALSE(r.has_value());
    CHECK(42 == r.value_or(42));

    sv = "123";
    t = strings::parse_num(sv, -1);
    CHECK(t == 123);
    sv = "abc";
    t = strings::parse_num(sv, -1);
    CHECK(t == -1);
    t = strings::parse_num<int64_t, 16>(sv, -1);
    CHECK(t == 0xabc);

    sv = "123 ";
    t = strings::parse_num(sv, -1);
    CHECK(t == -1);

    std::optional<int64_t> ot;

    sv = "123 ";
    ot = strings::parse_num(sv);
    CHECK_FALSE(ot.has_value());

    sv = "123";
    ot = strings::parse_num(sv);
    CHECK(ot.has_value());
    CHECK(ot.value_or(-1) == 123);

    // Verify default values with various integral types
    sv = "77";
    char c = strings::parse_num<char>(sv, 'x');
    CHECK(c == 77);
    sv = "x";
    c = strings::parse_num<char>(sv, 'x');
    CHECK(c == 'x');

    sv = "42";
    auto us = strings::parse_num<unsigned short>(sv, 7);
    CHECK(us == 42);
    sv = "foo";
    us = strings::parse_num<unsigned short>(sv, 7);
    CHECK(us == 7);
  }
  if (true) {
    std::string_view sv;
    double t;
    sv = "12.3";

    CHECK(strings::extract_num(t, sv));
    CHECK(t == 12.3);
    CHECK(sv.empty());

    sv = "12.3 45.6";
    CHECK(strings::extract_num(t, sv));
    CHECK(t == 12.3);
    CHECK_FALSE(sv.empty());
    CHECK(sv == " 45.6");
    CHECK(strings::extract_num(t, sv));
    CHECK(t == 45.6);
    CHECK(sv.empty());

#ifdef ONLY_WORKED_ON_MSVC
    std::string s;
    // strings::append<std::chars_format::hex>(s, 12.3L);
    s = "1.899999999999ap+3";
    sv = s;
    CHECK(sv == "1.899999999999ap+3");
    // Succeed with totally wrong answer.
    CHECK(strings::extract_num(t, sv));
    CHECK(sv == "ap+3");
    sv = s;
    CHECK(t == 1.8999999999990000L);
    CHECK(strings::extract_num<std::chars_format::hex>(t, sv));
    CHECK(t == 12.3L);
    CHECK(sv.empty());
#endif

    sv = "12.3";
    auto r = strings::extract_num<double>(sv);
    CHECK(r.has_value());
    CHECK(r.value_or(-1) == 12.3);
    CHECK(sv.empty());
    r = strings::extract_num<double>(sv);
    CHECK_FALSE(r.has_value());
    CHECK(4.2 == r.value_or(4.2));

    sv = "12.3";
    t = strings::parse_num<double>(sv, -1.2);
    CHECK(t == 12.3);
    sv = "xyz";
    t = strings::parse_num<double>(sv, -1.2);
    CHECK(t == -1.2);

    sv = "12.3 ";
    t = strings::parse_num<double>(sv, -1.2);
    CHECK(t == -1.2);

    std::optional<double> ot;

    sv = "12.3 ";
    ot = strings::parse_num<double>(sv);
    CHECK_FALSE(ot.has_value());

    sv = "12.3";
    ot = strings::parse_num<double>(sv);
    CHECK(ot.has_value());
    CHECK(ot.value_or(-1) == 12.3);
  }
}

#pragma endregion
#pragma region ParseInt

TEST_CASE("ParseInt", "[StringUtilsTest]") {
  // `parse_int` is consteval, so every call must be a constant expression.
  if (true) {
    // Decimal parsing of positive values.
    static_assert(strings::parse_int<int>("0") == 0);
    static_assert(strings::parse_int<int>("7") == 7);
    static_assert(strings::parse_int<int>("123") == 123);
    static_assert(strings::parse_int<int>("2147483647") == 2147483647);
  }
  if (true) {
    // Negative values are accepted for signed types.
    static_assert(strings::parse_int<int>("-1") == -1);
    static_assert(strings::parse_int<int>("-123") == -123);
    // The most-negative value is representable even though its magnitude
    // exceeds the positive maximum.
    static_assert(strings::parse_int<int>("-2147483648") == -2147483647 - 1);
    static_assert(strings::parse_int<int8_t>("-128") == int8_t{-128});
  }
  if (true) {
    // Negative values are rejected for unsigned types.
    static_assert(strings::parse_int<unsigned>("-1") == std::nullopt);
    static_assert(strings::parse_int<unsigned>("42") == 42u);
  }
  if (true) {
    // Empty input, a lone sign, and non-digit characters all fail.
    static_assert(strings::parse_int<int>("") == std::nullopt);
    static_assert(strings::parse_int<int>("-") == std::nullopt);
    static_assert(strings::parse_int<int>("abc") == std::nullopt);
    static_assert(strings::parse_int<int>("12a") == std::nullopt);
    static_assert(strings::parse_int<int>("1 2") == std::nullopt);
    static_assert(strings::parse_int<int>("+5") == std::nullopt);
  }
  if (true) {
    // Works with other integral widths.
    static_assert(
        strings::parse_int<int64_t>("9223372036854775807") ==
        9223372036854775807LL);
    static_assert(strings::parse_int<uint8_t>("255") == uint8_t{255});
  }
  // A runtime CHECK keeps the case visible in the test report.
  CHECK(strings::parse_int<int>("123") == 123);
}

#pragma endregion
#pragma region AppendNum

TEST_CASE("AppendNum", "[StringUtilsTest]") {
  if (true) {
    CHECK(strings::num_as_string(1) == "1");
    CHECK(strings::num_as_string(0) == "0");
    CHECK((strings::num_as_string<10, 5>(0)) == "    0");
    CHECK(strings::num_as_string<16>(uint8_t(0)) == "0x00");
    CHECK(strings::num_as_string<16>(uint16_t(0)) == "0x0000");
    CHECK(strings::num_as_string<16>(uint32_t(0)) == "0x00000000");
    CHECK(strings::num_as_string<16>(uint64_t(0)) == "0x0000000000000000");
    CHECK(strings::num_as_string(float(0.25F)) == "0.25");
    CHECK(strings::num_as_string(double(0.25F)) == "0.25");
    CHECK(((strings::num_as_string<std::chars_format::hex>(double(0.25)))) ==
          ("1p-2"));
    CHECK(strings::num_as_string<std::chars_format::fixed>(double(65536.25)) ==
          "65536.25");
    CHECK(strings::num_as_string<std::chars_format::scientific>(
              double(65536.25)) == "6.553625e+04");
    CHECK(
        ((strings::num_as_string<std::chars_format::hex>(double(65536.25)))) ==
        ("1.00004p+16"));
    CHECK(strings::num_as_string<std::chars_format::general>(
              double(65536.25)) == "65536.25");
  }
}

#pragma endregion
// Test number conversion over a wide code unit.
#pragma region WideConversion

TEST_CASE("WideConversion", "[StringUtilsTest]") {
  // append_num formats into a target of any code unit.
  std::u16string s;
  strings::append_num(s, 42);
  CHECK(s == u"42");
  s.clear();
  strings::append_num<16>(s, uint16_t(0xab)); // hex "0x" prefix + zero-pad
  CHECK(s == u"0x00ab");
  s.clear();
  strings::append_num<10, 5>(s, 7); // width and pad
  CHECK(s == u"    7");
  s.clear();
  strings::append_num(s, double(0.25));
  CHECK(s == u"0.25");

  // num_as_string can return a wide string (code unit as the trailing arg).
  CHECK((strings::num_as_string<16, 0, ' ', char16_t>(uint8_t(0))) == u"0x00");

  // extract_num / parse_num read a number out of a wide view.
  std::u16string_view sv = u"  123 rest";
  auto v = strings::extract_num<int>(sv);
  CHECK(v.has_value());
  CHECK(*v == 123);
  CHECK(sv == u" rest");
  CHECK(strings::parse_num<int>(u"456", -1) == 456);
  CHECK(strings::parse_num<int>(u"nope", -1) == -1);
  CHECK(strings::parse_num<double>(u"2.5", -1.0) == 2.5);
}

#pragma endregion
#pragma region Append

TEST_CASE("Append", "[StringUtilsTest]") {
  using strings::join_opt;
  std::string s;

  s.clear();
  strings::append(s, "1"); // is_string_view_convertible_v
  strings::append(s, "2"s);
  strings::append(s, "3"sv);
  CHECK(s == "123");
  s.clear();
  strings::append(s, '1');       // char (digit)
  strings::append(s, "2", "3");  // many
  strings::append(s, (char*){}); // char nullptr
  strings::append(s, (size_t)4); // append size_t
  strings::append(s, nullptr);   // nullptr
  CHECK(s == "123null4null");

  s.clear();
  strings::append<2>(s, 0xaa); // append binary
  CHECK(s == "10101010");
  s.clear();
  strings::append<16>(s, 0xaa); // append hex
  CHECK(s == "0x000000aa");

  s.clear();
  strings::append(s, false);          // append bool
  strings::append(s, true);           // append bool
  strings::append(s, (float)2);       // append float
  strings::append(s, (double)3);      // append float
  strings::append(s, (long double)4); // append float
  CHECK(s == "falsetrue234");

  s.clear();
  strings::append(s, (double)123.456789012345);
  CHECK(s == "123.456789012345");
  s.clear();
  strings::append<std::chars_format::scientific>(s, (double)123.456789012345);
  CHECK(s == "1.23456789012345e+02");
  s.clear();
  strings::append<std::chars_format::fixed, 1>(s, (double)123.456789012345);
  CHECK(s == "123.5");
  s.clear();
  strings::append<std::chars_format::fixed, 6>(s, (double)123.456789012345);
  CHECK(s == "123.456789");

  const int i = 42;
  s.clear();
  strings::append(s, i);
  CHECK(s == "42");
  s.clear();
  strings::append(s, &i);
  CHECK(s == "42");
  s.clear();
  strings::append(s, reinterpret_cast<intptr_t>(&i));
  CHECK(s != "42");
  CHECK_FALSE(s.starts_with("0x"));
  s.clear();
  strings::append(s, (void*){});
  CHECK(s == "0x0000000000000000");
  s.clear();
  strings::append(s, reinterpret_cast<const void*>(&i));
  CHECK(s != "42");
  CHECK(s.starts_with("0x"));
  auto oi = std::make_optional(i);
  s.clear();
  strings::append(s, oi);
  CHECK(s == "42");
  oi.reset();
  s.clear();
  strings::append(s, oi);
  CHECK(s == "null");

  // None of these char-like types are char so they're all treated as ints.
  CHECK_FALSE((std::is_same_v<char, signed char>));
  CHECK_FALSE((std::is_same_v<char, unsigned char>));
  CHECK_FALSE((std::is_same_v<char, char8_t>));
  CHECK_FALSE((std::is_same_v<signed char, unsigned char>));

  s.clear();
  strings::append(s, (signed char)1);
  strings::append(s, (unsigned char)2);
  strings::append(s, (wchar_t)3);
  strings::append(s, (char8_t)4);
  strings::append(s, (char16_t)5);
  strings::append(s, (char32_t)6);
  CHECK(s == "123456");

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
  CHECK(s == "12345678");

  s.clear();
  strings::append(s, (int8_t)1);
  strings::append(s, (int16_t)2);
  strings::append(s, (int32_t)3);
  strings::append(s, (int64_t)4);
  CHECK(s == "1234");

  s.clear();
  strings::append(s, (uint8_t)1);
  strings::append(s, (uint16_t)2);
  strings::append(s, (uint32_t)3);
  strings::append(s, (uint64_t)4);
  CHECK(s == "1234");

  s.clear();
  strings::append(s, (int_fast8_t)1);
  strings::append(s, (int_fast16_t)2);
  strings::append(s, (int_fast32_t)3);
  strings::append(s, (int_fast64_t)4);
  CHECK(s == "1234");

  s.clear();
  strings::append(s, (uint_fast8_t)1);
  strings::append(s, (uint_fast16_t)2);
  strings::append(s, (uint_fast32_t)3);
  strings::append(s, (uint_fast64_t)4);
  CHECK(s == "1234");

  s.clear();
  strings::append(s, (int_least8_t)1);
  strings::append(s, (int_least16_t)2);
  strings::append(s, (int_least32_t)3);
  strings::append(s, (int_least64_t)4);
  CHECK(s == "1234");

  s.clear();
  strings::append(s, (uint_least8_t)1);
  strings::append(s, (uint_least16_t)2);
  strings::append(s, (uint_least32_t)3);
  strings::append(s, (uint_least64_t)4);
  CHECK(s == "1234");

  s.clear();
  strings::append(s, (intmax_t)1);
  strings::append(s, (intptr_t)2);
  strings::append(s, (uintmax_t)3);
  strings::append(s, (uintptr_t)4);
  CHECK(s == "1234");

  enum ColorEnum : std::uint8_t { red, green = 20, blue };
  enum class ColorClass : std::uint8_t { red, green = 20, blue };

  s.clear();
  strings::append(s, green);
  strings::append(s, ColorClass::blue);
  CHECK(s == "2021");
  s.clear();

  strings::append(s, ColorClass::green);
  CHECK(s == "20");
  s.clear();
  strings::append(s, ColorClass(20));
  CHECK(s == "20"); // not hex!

  std::map<std::string, int> msi{{"a", 1}, {"c", 2}};
  std::set<int> si{3, 4, 5};
  s.clear();
  strings::append(s, msi);
  strings::append(s, si);
  CHECK(s == "12345");

  std::variant<std::monostate, int, std::map<std::string, int>> va;
  s.clear();
  strings::append(s, va);
  CHECK(s == "null");
  va = 52;
  s.clear();
  strings::append(s, va);
  CHECK(s == "52");

  va = msi;
  s.clear();
  strings::append(s, va);
  CHECK(s == "12");

  std::map<std::string, std::set<int>> mssi{{"c", {5, 4}}, {"a", {3, 2, 1}}};
  s.clear();
  strings::append(s, mssi);
  CHECK(s == "12345");

  CHECK_FALSE((std::is_same_v<int8_t, char>));
  CHECK(strings::num_as_string(42) == "42");
  CHECK(strings::num_as_string<16>(10) == "0x0000000a");
  CHECK(strings::num_as_string('a') == "97");
  CHECK(strings::num_as_string(123.0L) == "123");
  CHECK(strings::num_as_string(12.3L) == "12.3");

  CHECK(strings::concat("1", "2"sv, "3"s) == "123");
  CHECK(strings::concat(1, 2.0, 3ULL) == "123");
  CHECK(strings::concat(true, std::byte{2}, 3) == "true23");
  CHECK(strings::concat("1", "2") == "12");

  auto t = std::make_tuple("1"s, 2, 3.0);
  auto l = {"1"s, "2"s, "3"s};
  auto p = std::make_pair("1"s, "2, 3");
  auto a = std::array<int, 3>{1, 2, 3};
  std::vector<std::string> v = l;

  CHECK(strings::concat(l, v) == "123123");

  CHECK(strings::concat(t) == "123");
  CHECK(strings::concat(p) == "12, 3");
  CHECK(strings::concat(a) == "123");

  CHECK(strings::join<join_opt::quoted>(l) == R"(["1", "2", "3"])");

  s.clear();
  strings::append_join_with(s, ";", "123"); // single
  CHECK(s == "123");
  strings::append_join_with(s, ";", std::optional<std::string>{});
  strings::append_join_with(s, ";", "456");
  CHECK(s == "123null456");
  strings::append_join_with<join_opt::prefixed, '`', '\''>(s, ";", "789");
  CHECK(s == "123null456;`789'");

  s.clear();
  strings::append_join_with(s, ";", v); // container
  CHECK(s == "[1;2;3]");

  s = "a";
  strings::append_join_with<join_opt::prefixed>(s, ";", v);
  CHECK(s == "a;[1;2;3]");
  s = "v=";
  strings::append_join_with(s, ";", v);
  CHECK(s == "v=[1;2;3]");

  s.clear();
  strings::append_join_with(s, ";", t); // tuple
  CHECK(s == "{1;2;3}");

  s.clear();
  strings::append_join_with<join_opt::flat>(s, ";", t); // tuple
  CHECK(s == "1;2;3");

  s.clear();
  strings::append_join_with(s, ";", intptr_t(42));
  CHECK(s == "42");

  s.clear();
  strings::append_join_with(s, ";", reinterpret_cast<const void*>(&i));
  CHECK(s != "42");

  s.clear();
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  strings::append_join_with(s, ";", reinterpret_cast<const void*>(NULL));
  CHECK(s == "0x0000000000000000");

  s.clear();
  const char* psz{};
  strings::append_join_with(s, ";", psz);
  CHECK(s == "null");

  va = 52;
  s.clear();
  strings::append_join_with(s, ";", va);
  CHECK(s == "52");
  CHECK(strings::concat(va) == "52");

  va = msi;
  s.clear();
  strings::append_join_with(s, ";", va);
  CHECK(s == "[1;2]");

  s.clear();
  strings::append_join_with(s, ",", std::pair{"a", 1});
  CHECK(s == "1");

  s.clear();
  strings::append_join_with<join_opt::flat>(s, ",", std::pair{"a", 1});
  CHECK(s == "1");

  s.clear();
  strings::append_join_with<join_opt::keyed>(s, ",", std::pair{"a", 1});
  CHECK(s == "{a,1}");

  s.clear();
  strings::append_join_with<join_opt::flat_keyed>(s, ",", std::pair{"a", 1});
  CHECK(s == "a,1");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ",", std::pair{"a", 1});
  CHECK(s == R"("a": 1)");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ",", std::pair{1, "a"});
  CHECK(s == R"("1": "a")");

  s.clear();
  strings::append_join_with(s, ",", mssi);
  CHECK(s == "[[1,2,3],[4,5]]");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ", ", mssi);
  CHECK(s == R"({"a": [1, 2, 3], "c": [4, 5]})");

  auto om = std::make_optional(mssi);
  s.clear();
  strings::append_join_with(s, ",", om);
  CHECK(s == "[[1,2,3],[4,5]]");

  s.clear();
  strings::append_join<join_opt::json>(s, mssi);
  CHECK(s == R"({"a": [1, 2, 3], "c": [4, 5]})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<std::string, int>{{"b", 2}, {"a", 1}});
  CHECK(s == R"({"a": 1, "b": 2})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<std::string, std::string>{{"b", "2"}, {"a", "1"}});
  CHECK(s == R"({"a": "1", "b": "2"})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<int, std::string>{{2, "2"}, {1, "1"}});
  CHECK(s == R"({"1": "1", "2": "2"})");

  s.clear();
  auto il = {3, 1, 4};
  strings::append_join_with(s, ";", il); // Initializer list.
  // Unbound initializer lists can't be deduced by templates.
  // * strings::append_join_with(s, ";", {1, 2, 3});
  CHECK(s == "[3;1;4]");

  CHECK(strings::join(1, 2, 3) == "[1, 2, 3]");
  CHECK(strings::join("1"s, "2"s, "3"s) == "[1, 2, 3]");
  CHECK(strings::join(t) == "{1, 2, 3}");
  CHECK(strings::join<join_opt::keyed>(p) == "{1, 2, 3}");
  s.clear();
  strings::append_join_with<join_opt::keyed>(s, ",", std::pair{1, 2});
  CHECK(s == "{1,2}");
  CHECK(strings::join(a) == "[1, 2, 3]");

  CHECK(strings::join<join_opt::flat_keyed>(t) == "1, 2, 3");
  CHECK(strings::join<join_opt::flat_keyed>(p) == "1, 2, 3");
  CHECK(strings::join<join_opt::flat_keyed>(a) == "1, 2, 3");

  CHECK(strings::concat('1', '2', '3') == "123");

  CHECK(strings::join(mssi) == "[[1, 2, 3], [4, 5]]");
  CHECK(strings::join<join_opt::flat>(mssi) == "1, 2, 3, 4, 5");

  // More JSON A/B tests.
  s.clear();
  strings::append_join_with(s, ", ", std::vector{1, 2, 3});
  CHECK(s == "[1, 2, 3]");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ", ", std::vector{1, 2, 3});
  CHECK(s == "[1, 2, 3]");

  s.clear();
  strings::append_join_with(s, ", ", std::vector<int>{});
  CHECK(s == "[]");

  s = strings::join_json(std::vector<int>{});
  CHECK(s == "[]");

  s.clear();
  strings::append_join_with(s, ", ", std::vector{"a", "b", "c"});
  CHECK(s == "[a, b, c]");

  s = strings::join_json(std::vector{"a", "b", "c"});
  CHECK(s == R"(["a", "b", "c"])");

  s = strings::join_json(std::vector{"a", "b", "c"},
      std::vector{"d", "e", "f"});
  CHECK(s == R"([["a", "b", "c"], ["d", "e", "f"]])");
}

#pragma endregion
#pragma region Edges

TEST_CASE("Edges", "[StringUtilsTest]") {
  using strings::join_opt;

  std::vector<int> a{1, 2, 3};
  std::vector<int> b{4, 5, 6};
  CHECK(strings::join(a, b) == "[[1, 2, 3], [4, 5, 6]]");
  CHECK(strings::join<join_opt::flat>(a, b) == "1, 2, 3, 4, 5, 6");
  CHECK(((strings::join<join_opt::braced, '(', ')'>(a, b))) ==
        ("([1, 2, 3], [4, 5, 6])"));
  CHECK(((strings::join<join_opt::prefixed, '{', '}'>(a, b))) ==
        (", {[1, 2, 3], [4, 5, 6]}"));
  CHECK((strings::join<join_opt::prefixed>(a, b)) ==
        (", [[1, 2, 3], [4, 5, 6]]"));
}

#pragma endregion
#pragma region Streams

TEST_CASE("Streams", "[StringUtilsTest]") {
  using strings::join_opt;

  std::vector<int> a{1, 2, 3};
  std::vector<int> b{4, 5, 6};

  if (true) {
    std::stringstream s;
    strings::append_join(s, a, b);
    CHECK(s.str() == "[[1, 2, 3], [4, 5, 6]]");
  }
}

#pragma endregion

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
consteval auto corvid_enum_spec(rgb*) {
  return make_bitmask_enum_spec<rgb, "red,green,blue">();
}

#pragma region AppendEnum

TEST_CASE("AppendEnum", "[StringUtilsTest]") {
  std::string s;
  CHECK(strings::concat(rgb::yellow) == "red + green");
  CHECK(((strings::join(rgb::yellow, rgb::cyan))) ==
        ("[red + green, green + blue]"));
  s = (strings::join<strings::join_opt::braced, -1, -1>(rgb::yellow,
      rgb::cyan));
  CHECK(s == "red + green, green + blue");
  s.clear();
  strings::append_join_with<strings::join_opt::braced, -1, -1>(s, ", ",
      rgb::yellow, rgb::cyan);
  CHECK(s == "red + green, green + blue");
}

#pragma endregion

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

consteval auto corvid_enum_spec(marine_rank*) {
  return make_sequence_enum_spec<marine_rank,
      "Civilian,Private,PrivateFirstClass,LanceCorporal,Sergeant,"
      "StaffSergeant,GunnerySergeant,MasterSergeant,FirstSergeant,"
      "MasterGunnerySergeant,SergeantMajor,SergeantMajorOfTheMarineCorps",
      wrapclip::limit>();
}

// Enum with special characters in names for quote-encoding test.
enum class special_chars : int { normal, has_backslash, has_quote };

consteval auto corvid_enum_spec(special_chars*) {
  return make_sequence_enum_spec<special_chars,
      R"(normal,back\slash,has"quote)">();
}

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

#pragma region AppendStream

TEST_CASE("AppendStream", "[StringUtilsTest]") {
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
    CHECK(os.str() == "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::stringstream os;
    strings::append_stream(os, pyle);
    CHECK(os.str() == "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append_stream(s, pyle);
    CHECK(s == "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append(s, pyle);
    CHECK(s == "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, pyle);
    CHECK(s == "[Gomer, Private, 12345678]");
  }
  if (true) {
    auto v = std::vector{std::make_pair(pyle, 30'000.15),
        std::make_pair(carter, 40'000.21)};
    std::string s;
    strings::append_join<join_opt::keyed>(s, v);
    CHECK((s) ==
          ("[{[Gomer, Private, 12345678], 30000.15}, {[Vince, "
           "GunnerySergeant, "
           "23456789], 40000.21}]"));

    s.clear();
    strings::append_join<strings::join_opt::json>(s, v);
    CHECK(
        (s) ==
        (R"({"[Gomer, Private, 12345678]": 30000.15, "[Vince, GunnerySergeant, 23456789]": 40000.21})"));
  }
  if (true) {
    std::string s;
    strings::append(s, doe);
    CHECK(s == "Doe, John");

    s.clear();
    strings::append_join(s, doe);
    CHECK(s == "[Doe, John]");
  }
  if (true) {
    std::string s;
    strings::append_join_with<strings::join_opt::flat>(s, "; ", doe);
    CHECK(s == "Doe; John");
  }
  if (true) {
    std::string s;
    strings::append_join_with<strings::join_opt::quoted>(s, "; ", doe);
    CHECK(s == R"(["Doe"; "John"])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, doe);
    CHECK(s == R"(["Doe", "John"])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, doe, doe, doe);
    CHECK(s == R"([["Doe", "John"], ["Doe", "John"], ["Doe", "John"]])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::flat>(s, doe, doe, doe);
    CHECK(s == "Doe, John, Doe, John, Doe, John");
  }
  if (true) {
    tuple0 t0{};
    tuple1 t1{1};
    tuple2 t2{1, "2"};
    std::string s;

    strings::append_join(s, t0);
    CHECK(s == "");
    s.clear();
    strings::append_join<strings::join_opt::flat>(s, t0);
    CHECK(s == "");
    s.clear();
    strings::append_join<strings::join_opt::quoted>(s, t0);
    CHECK(s == "");
    s.clear();
    strings::append_join<strings::join_opt::json>(s, t0);
    CHECK(s == "");
    // Test whether we need a special case.
    s.clear();
    strings::append_join_with(s, ";", t0, t1, t2);
    CHECK(s == "[;{1};{1;2}]");
    s.clear();
    strings::append_join_with(s, ";", t0);
    CHECK(s == "");

    s.clear();
    strings::append_join(s, t1);
    CHECK(s == "{1}");
    s.clear();
    strings::append_join<strings::join_opt::flat>(s, t1);
    CHECK(s == "1");
    s.clear();
    strings::append_join<strings::join_opt::quoted>(s, t1);
    CHECK(s == "{1}");
    s.clear();
    strings::append_join<strings::join_opt::json>(s, t1);
    CHECK(s == "{1}");

    s.clear();
    strings::append_join(s, t2);
    CHECK(s == "{1, 2}");
    s.clear();
    strings::append_join<strings::join_opt::flat>(s, t2);
    CHECK(s == "1, 2");
    s.clear();
    strings::append_join<strings::join_opt::json>(s, t2);
    CHECK(s == "{1, \"2\"}");
  }
}

#pragma endregion
#pragma region AppendJson

TEST_CASE("AppendJson", "[StringUtilsTest]") {
  using strings::join_opt;

  if (true) {
    std::string s;
    strings::append_json(s, marine_rank::Private);
    CHECK(s == R"("Private")");
  }
  if (true) {
    std::string s;
    strings::append_escaped(s, R"(he"l"lo)");
    CHECK(s == R"(he\"l\"lo)");
  }
  if (true) {
    std::string s;
    strings::append_escaped(s, R"(he"l"lo)");
    CHECK(s == R"(he\"l\"lo)");
    s.clear();
    strings::append_escaped(s, "a\tb\\c\"d\n\b\f\r\x1f");
    CHECK(s == R"(a\tb\\c\"d\n\b\f\r\u001f)");
  }
  if (true) {
    std::string s;
    const char* p{};
    strings::append_json(s, p);
    CHECK(s == R"(null)");
    s.clear();
    strings::append_json(s, cstring_view{});
    CHECK(s == R"(null)");
    s.clear();
    strings::append_json(s, cstring_view{""});
    CHECK(s == R"("")");
  }
  // Test quote-encoding when non-string wrapped in quotes.
  // Strings are escaped, but enums are not. This documents current behavior.
  if (true) {
    std::string s;

    // String with special chars gets escaped.
    strings::append_json(s, R"(back\slash)");
    CHECK(s == R"("back\\slash")");
    s.clear();
    strings::append_json(s, R"(has"quote)");
    CHECK(s == (R"("has\"quote")"));

    // Enum with special chars in name is NOT escaped (current behavior).
    s.clear();
    strings::append_json(s, special_chars::has_backslash);
    CHECK((s) == (R"("back\slash")"));
    s.clear();
    strings::append_json(s, special_chars::has_quote);
    CHECK((s) == (R"("has"quote")"));
  }
}

#pragma endregion
#pragma region StdFromChars

TEST_CASE("StdFromChars", "[StringUtilsTest]") {
  // Test std::from_chars directly for float.
  if (true) {
    float value{};
    std::string_view sv;

    // Basic positive float.
    sv = "3.14";
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 3.13f && value < 3.15f));
    CHECK(result.ptr == (sv.data() + sv.size()));

    // Basic negative float.
    sv = "-2.5";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == -2.5f);
    CHECK(result.ptr == (sv.data() + sv.size()));

    // Integer parsed as float.
    sv = "42";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 42.0f);

    // Zero.
    sv = "0.0";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 0.0f);

    // Negative zero.
    sv = "-0.0";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    // Negative zero should be equal to positive zero.
    CHECK(value == 0.0f);

    // Scientific notation.
    sv = "1.5e3";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 1500.0f);

    // Negative exponent.
    sv = "1.5e-2";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 0.014f && value < 0.016f));

    // Large positive exponent.
    sv = "1e30";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value > 9e29f);

    // Very small positive number.
    sv = "1e-30";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 0.0f && value < 1e-29f));

    // Partial parse (stops at non-numeric).
    sv = "3.14abc";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 3.13f && value < 3.15f));
    CHECK(result.ptr == (sv.data() + 4)); // Stops at 'a'.
  }
  // Test std::from_chars directly for double.
  if (true) {
    double value{};
    std::string_view sv;

    // Basic positive double.
    sv = "3.141592653589793";
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 3.14159265358979 && value < 3.14159265358980));
    CHECK(result.ptr == (sv.data() + sv.size()));

    // Basic negative double.
    sv = "-2.718281828";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value < -2.71828182 && value > -2.71828183));

    // Large double.
    sv = "1.7976931348623157e308";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value > 1e307);

    // Small positive double.
    sv = "2.2250738585072014e-308";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 0.0 && value < 1e-307));

    // Scientific notation with capital E.
    sv = "1.5E10";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 1.5e10);

    // Leading decimal point.
    sv = ".5";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 0.5);

    // Trailing decimal point.
    sv = "5.";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 5.0);
  }
  // Test error handling.
  if (true) {
    float fvalue{42.0f};
    std::string_view sv;

    // Empty string - properly returns error.
    sv = "";
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), fvalue);
    CHECK(result.ec != std::errc{});

    sv = "abc";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), fvalue);
    CHECK(result.ptr == sv.data());
    CHECK(result.ec == std::errc::invalid_argument);
  }
  // Test extract_num float wrappers (which use std::from_chars internally).
  if (true) {
    std::string_view sv;
    float f{};

    // Basic extraction.
    sv = "  3.14  ";
    CHECK(strings::extract_num(f, sv));
    CHECK((f > 3.13f && f < 3.15f));
    CHECK(sv == "  "); // Whitespace trimmed from left, remaining on right.

    // Scientific notation.
    sv = "6.022e23";
    CHECK(strings::extract_num(f, sv));
    CHECK(f > 6e23f);
    CHECK(sv.empty());
  }
  // Test parse_num float wrappers.
  if (true) {
    // Successful parse.
    auto opt = strings::parse_num<float>("2.5"sv);
    CHECK(opt.has_value());
    CHECK(opt.value() == 2.5f);

    // Failure - trailing garbage.
    opt = strings::parse_num<float>("2.5abc"sv);
    CHECK_FALSE(opt.has_value());

    // Failure - invalid.
    opt = strings::parse_num<float>("invalid"sv);
    CHECK_FALSE(opt.has_value());

    // With default value.
    float val = strings::parse_num<float>("1.5"sv, -1.0f);
    CHECK(val == 1.5f);

    val = strings::parse_num<float>("bad"sv, -1.0f);
    CHECK(val == -1.0f);

    val = strings::parse_num<float>("1.5 "sv, -1.0f);
    CHECK(val == -1.0f); // Trailing space causes failure.
  }
  // Test parse_num double wrappers.
  if (true) {
    // Successful parse.
    auto opt = strings::parse_num<double>("3.141592653589793"sv);
    CHECK(opt.has_value());
    CHECK(opt.value() > 3.14159265358979);

    // Scientific notation.
    opt = strings::parse_num<double>("1e-100"sv);
    CHECK(opt.has_value());
    CHECK((opt.value() > 0.0 && opt.value() < 1e-99));

    // With default value.
    double val = strings::parse_num<double>("2.718281828"sv, 0.0);
    CHECK((val > 2.71828182 && val < 2.71828183));

    val = strings::parse_num<double>("xyz"sv, -999.0);
    CHECK(val == -999.0);
  }
}

#pragma endregion
#pragma region NoZero

TEST_CASE("NoZero", "[StringUtilsTest]") {
  // Sanity check: `clear` does not release a heap buffer, but `shrink_to_fit`
  // does. SSO ensures capacity never drops to zero.
  if (true) {
    std::string s;
    s.resize(50);
    CHECK(s.capacity() >= 50u);
    s.clear();
    CHECK(s.capacity() >= 50u);
    s.shrink_to_fit();
    CHECK((s.capacity()) < (50u));
    CHECK((s.capacity()) > (0u));
  }

  // Capture the SSO capacity (typically 15 on libc++ 64-bit).
  const auto sso_cap = std::string{}.capacity();

  // Ensure that the small values we use below are within SSO capacity.
  CHECK((sso_cap) > (10u));

  using namespace corvid::strings::no_zero_funcs;

  // `resize_to`: size matches the requested value exactly; capacity covers it.
  // Shrinking via `resize_to` does NOT reduce capacity: important for the
  // fill-buffer-then-commit pattern.
  if (true) {
    std::string s;

    // Zero.
    no_zero::resize_to(s, 0);
    CHECK(s.size() == 0u);

    // Tiny (SSO range).
    no_zero::resize_to(s, 2);
    CHECK(s.size() == 2u);
    CHECK(s.capacity() >= 2u);

    no_zero::resize_to(s, 4);
    CHECK(s.size() == 4u);
    CHECK(s.capacity() >= 4u);

    // Shrink within SSO: capacity must not change.
    auto cap = s.capacity();
    no_zero::resize_to(s, 2);
    CHECK(s.size() == 2u);
    CHECK(s.capacity() == cap);

    // Small (heap range).
    no_zero::resize_to(s, 50);
    CHECK(s.size() == 50u);
    CHECK(s.capacity() >= 50u);

    no_zero::resize_to(s, 100);
    CHECK(s.size() == 100u);
    CHECK(s.capacity() >= 100u);

    // Shrink on heap: capacity must not change.
    cap = s.capacity();
    no_zero::resize_to(s, 50);
    CHECK(s.size() == 50u);
    CHECK(s.capacity() == cap);

    // Same size (no-op).
    no_zero::resize_to(s, 50);
    CHECK(s.size() == 50u);
    CHECK(s.capacity() == cap);
  }

  // `enlarge_to_cap`: resizes to the full current capacity so that
  // `size() == capacity()`.
  if (true) {
    // On an empty string: size expands to the SSO capacity.
    std::string s;
    no_zero::enlarge_to_cap(s);
    CHECK(s.size() == sso_cap);
    CHECK(s.size() == s.capacity());

    // On a heap-allocated string: fills out to the full allocated capacity.
    no_zero::resize_to(s, 50);
    auto cap = s.capacity();
    no_zero::enlarge_to_cap(s);
    CHECK(s.size() == cap);
    CHECK(s.size() == s.capacity());
  }

  // `trim_to`: shrinks when `new_size` is smaller, but never enlarges and
  // never changes capacity.
  if (true) {
    static_assert(requires(std::string& value) {
      no_zero::trim_to(value, int{1});
    });
    static_assert(requires(std::string& value) {
      no_zero::trim_to(value, unsigned{1});
    });
    static_assert(requires(std::string& value) {
      no_zero::trim_to(value, int16_t{-1});
    });

    std::string s;

    no_zero::resize_to(s, 50);
    auto cap = s.capacity();

    // Shrink within current size.
    no_zero::trim_to(s, 20);
    CHECK(s.size() == 20u);
    CHECK(s.capacity() == cap);

    // Same size is a no-op.
    no_zero::trim_to(s, 20);
    CHECK(s.size() == 20u);
    CHECK(s.capacity() == cap);

    // Larger size request must not enlarge.
    no_zero::trim_to(s, 40);
    CHECK(s.size() == 20u);
    CHECK(s.capacity() == cap);

    // Trimming to zero works and still preserves capacity.
    no_zero::trim_to(s, 0);
    CHECK(s.size() == 0u);
    CHECK(s.capacity() == cap);

    // Negative signed values clamp to zero.
    no_zero::resize_to(s, 30);
    no_zero::trim_to(s, -1);
    CHECK(s.size() == 0u);
    CHECK(s.capacity() == cap);

    // Positive signed values trim normally after the signed check.
    no_zero::resize_to(s, 30);
    no_zero::trim_to(s, int16_t{6});
    CHECK(s.size() == 6u);
    CHECK(s.capacity() == cap);

    // Any integer type is accepted, including unsigned non-size_t.
    no_zero::resize_to(s, 30);
    no_zero::trim_to(s, 7u);
    CHECK(s.size() == 7u);
    CHECK(s.capacity() == cap);

    // Returns a reference to the same string.
    CHECK(&no_zero::trim_to(s, 10) == &s);
    CHECK(s.size() == 7u);
    CHECK(s.capacity() == cap);
  }

  // `enlarge_to`: size is at least `minimum_size`, and always fills capacity.
  // When `minimum_size` fits in the current buffer, no reallocation occurs.
  if (true) {
    // Tiny request on an empty string: fits in SSO, so size expands to the
    // full SSO capacity.
    std::string s;
    no_zero::enlarge_to(s, 3);
    CHECK(s.size() >= 3u);
    CHECK(s.size() == sso_cap);
    CHECK(s.size() == s.capacity());

    // Another tiny request within current capacity: no reallocation, size
    // stays at the full current capacity.
    auto cap_before = s.capacity();
    no_zero::enlarge_to(s, 2);
    CHECK(s.size() == cap_before);
    CHECK(s.capacity() == cap_before);

    // Small request beyond current capacity: reallocates, then fills capacity.
    no_zero::enlarge_to(s, 50);
    CHECK(s.size() >= 50u);
    CHECK(s.size() == s.capacity());

    // Request within the new capacity: no reallocation.
    cap_before = s.capacity();
    no_zero::enlarge_to(s, 50);
    CHECK(s.size() == cap_before);
    CHECK(s.capacity() == cap_before);

    // Large request well beyond current capacity: reallocates and fills.
    auto large = s.capacity() * 4;
    no_zero::enlarge_to(s, large);
    CHECK(s.size() >= large);
    CHECK(s.size() == s.capacity());

    // Returns a reference to the same string.
    CHECK(&no_zero::enlarge_to(s, 4) == &s);
  }

  // `clear_out`: releases the heap buffer (capacity drops to SSO level) and
  // sets size to zero. On an SSO string, capacity is already minimal.
  if (true) {
    // Heap-allocated string: buffer is released.
    std::string s;
    no_zero::enlarge_to(s, 100);
    CHECK(s.capacity() >= 100u);
    no_zero::clear_out(s);
    CHECK(s.size() == 0u);
    CHECK((s.capacity()) < (100u));
    CHECK(s.capacity() >= sso_cap);

    // SSO-sized string: capacity is unchanged (nothing to release).
    std::string t;
    no_zero::resize_to(t, 4);
    auto cap = t.capacity();
    no_zero::clear_out(t);
    CHECK(t.size() == 0u);
    CHECK(t.capacity() == cap);

    // Returns a reference to the same string.
    CHECK(&no_zero::clear_out(s) == &s);
  }

  // `rightsize_to`: when capacity is within [minimum_size, maximum_size],
  // behaves like `enlarge_to`; when capacity exceeds `maximum_size`, releases
  // the buffer and resizes to exactly `minimum_size` (rounding up to
  // capacity).
  if (true) {
    // Tiny: SSO capacity within bounds -> enlarge_to path.
    std::string s;
    no_zero::rightsize_to(s, 3, 100);
    CHECK(s.size() >= 3u);
    CHECK(s.size() == s.capacity());

    // Tiny: SSO capacity above maximum -> shrink to minimum_size.
    std::string t;
    no_zero::resize_to(t, 4); // capacity == sso_cap
    no_zero::rightsize_to(t, 2, sso_cap - 1);
    CHECK(t.size() == 2u);

    // Small: capacity within bounds -> enlarge_to path.
    std::string u;
    no_zero::rightsize_to(u, 50, 500);
    CHECK(u.size() >= 50u);
    CHECK(u.size() == u.capacity());

    // Small: capacity above maximum -> shrinks to minimum_size.
    no_zero::enlarge_to(u, 200);
    CHECK(u.capacity() >= 200u);
    no_zero::rightsize_to(u, 50, 100);
    CHECK(u.size() == 50u);
    CHECK((u.capacity()) < (200u));

    // Returns a reference to the same string.
    CHECK(&no_zero::rightsize_to(u, 50, 500) == &u);
  }
}

#pragma endregion

// Test token_parser.
#pragma region TokenParser

TEST_CASE("TokenParser", "[StringUtilsTest]") {
  using strings::token_parser;

  token_parser p{"\r\n"};
  std::string_view input = "alpha\r\n\r\nbeta";

  CHECK(p.separator() == "\r\n");
  CHECK(p.next_delimited(input) == "alpha");
  CHECK(p.next_delimited(input) == "");
  CHECK(p.next_delimited(input) == "beta");
  CHECK(p.next_delimited(input) == "");
  CHECK(input.empty());

  p.separator(", ");
  input = "one, two";
  CHECK(p.separator() == ", ");
  CHECK(p.next_delimited(input) == "one");
  CHECK(p.next_delimited(input) == "two");
  CHECK(input.empty());

  p.separator("\r\n");
  input = "alpha\r\n\r\nbeta";

  auto token = p.next_terminated(input);
  REQUIRE(token.has_value());
  CHECK(*token == "alpha");

  token = p.next_terminated(input);
  REQUIRE(token.has_value());
  CHECK(*token == "");

  token = p.next_terminated(input);
  REQUIRE_FALSE(token.has_value());
  CHECK(input == "beta");

  input = {};
  token = p.next_terminated(input);
  REQUIRE_FALSE(token.has_value());

  // Char-separator overloads for next_delimited.
  input = "one,two,three";
  CHECK(token_parser::next_delimited(',', input) == "one");
  CHECK(token_parser::next_delimited(',', input) == "two");
  CHECK(token_parser::next_delimited(',', input) == "three");
  CHECK(token_parser::next_delimited(',', input) == "");
  CHECK(input.empty());

  // Char-separator overloads for next_terminated.
  input = "one,two,three";
  auto ctoken = token_parser::next_terminated(',', input);
  REQUIRE(ctoken.has_value());
  CHECK(*ctoken == "one");
  ctoken = token_parser::next_terminated(',', input);
  REQUIRE(ctoken.has_value());
  CHECK(*ctoken == "two");
  // No trailing terminator; expect nullopt but input unchanged.
  ctoken = token_parser::next_terminated(',', input);
  REQUIRE_FALSE(ctoken.has_value());
  CHECK(input == "three");
}

#pragma endregion

// Test any_strings, strings::as_vector, and strings::as_any.
#pragma region AnyStrings

TEST_CASE("AnyStrings", "[StringUtilsTest]") {
  using strings::any_strings;

  // strings::as_vector: zero, one, and multiple strings.
  auto v0 = strings::as_vector();
  CHECK(v0.empty());

  auto v1 = strings::as_vector(std::string{"hello"});
  REQUIRE(v1.size() == 1u);
  CHECK(v1[0] == "hello");

  auto v2 =
      strings::as_vector(std::string{"a"}, std::string{"b"}, std::string{"c"});
  REQUIRE(v2.size() == 3u);
  CHECK(v2[0] == "a");
  CHECK(v2[1] == "b");
  CHECK(v2[2] == "c");

  // strings::as_any: zero args -> monostate.
  auto a0 = strings::as_any();
  CHECK(std::holds_alternative<std::monostate>(a0));

  // strings::as_any: one arg -> string.
  auto a1 = strings::as_any(std::string{"hello"});
  REQUIRE(std::holds_alternative<std::string>(a1));
  CHECK(std::get<std::string>(a1) == "hello");

  // strings::as_any: multiple args -> vector.
  auto a2 = strings::as_any(std::string{"x"}, std::string{"y"});
  REQUIRE(std::holds_alternative<std::vector<std::string>>(a2));
  const auto& sv = std::get<std::vector<std::string>>(a2);
  REQUIRE(sv.size() == 2u);
  CHECK(sv[0] == "x");
  CHECK(sv[1] == "y");
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
