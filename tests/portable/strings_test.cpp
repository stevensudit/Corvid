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
#include <compare>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <type_traits>
#include <vector>

#include "corvid/strings.h"
#include "corvid/enums.h"
#include "corvid/infra/ostream_redirector.h"

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
using namespace corvid::strings::delimiting;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(readability-function-size)

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
  std::string_view w;
  std::string_view part;
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
    // NOLINTNEXTLINE(bugprone-use-after-move): moved-from is asserted.
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
#pragma region WhitespaceDelim

TEST_CASE("WhitespaceDelim", "[StringUtilsTest]") {
  using V = std::vector<std::string_view>;
  // The named set splits on all ASCII whitespace, not just the default lone
  // space.
  if (true) {
    CHECK(strings::split("a b\tc\nd") == V{"a", "b\tc\nd"});
    CHECK(strings::split("a b\tc\nd", strings::whitespace) ==
          V{"a", "b", "c", "d"});
    CHECK(strings::split(L"a b\tc", strings::wwhitespace) ==
          std::vector<std::wstring_view>{L"a", L"b", L"c"});
    CHECK(strings::trim("\t a \r\n", strings::whitespace) == "a");
  }
  // Pin the set to is_space exactly, so the two cannot drift apart.
  if (true) {
    CHECK(strings::is_space(strings::whitespace));
    CHECK(strings::whitespace.size() == 6U);
    for (auto ndx = 0; ndx < 256; ++ndx) {
      const auto c = static_cast<char>(ndx);
      CHECK(strings::is_space(c) ==
            // find/npos is the oracle that is_space is checked against.
            // NOLINTNEXTLINE(readability-container-contains)
            (strings::whitespace.find(c) != strings::npos));
    }
    CHECK(strings::is_space(strings::wwhitespace));
    CHECK(strings::wwhitespace.size() == strings::whitespace.size());
  }
}

#pragma endregion
#pragma region SplitN

TEST_CASE("SplitN", "[StringUtilsTest]") {
  using V = std::vector<std::string_view>;
  // At most n splits; the final part is the untouched remainder.
  if (true) {
    CHECK(strings::split_n("a,b,c,d", 2, ",") == V{"a", "b", "c,d"});
    CHECK(strings::split_n("a,b,c,d", 1, ",") == V{"a", "b,c,d"});
    CHECK(strings::split_n("a,b,c,d", 0, ",") == V{"a,b,c,d"});
    CHECK(strings::split_n("a,,b", 1, ",") == V{"a", ",b"});
    CHECK(strings::split_n("a,", 1, ",") == V{"a", ""});
  }
  // With enough splits, equivalent to split; empty input yields no parts.
  if (true) {
    CHECK(strings::split_n("a,b", 5, ",") == strings::split("a,b", ","));
    CHECK(strings::split_n("", 3, ",") == V{});
    CHECK(strings::split_n("a b c", 1) == V{"a", "b c"});
  }
  // A temporary string yields owning copies.
  if (true) {
    CHECK(strings::split_n(std::string{"a b c"}, 1) ==
          std::vector<std::string>{"a", "b c"});
  }
}

#pragma endregion
#pragma region RSplit

TEST_CASE("RSplit", "[StringUtilsTest]") {
  using V = std::vector<std::string_view>;
  // rextract_piece peels the last piece and its delimiter off the tail.
  if (true) {
    auto whole = "k1=k2=v"sv;
    CHECK(strings::rextract_piece(whole, "=") == "v");
    CHECK(whole == "k1=k2");
    CHECK(strings::rextract_piece(whole, "=") == "k2");
    CHECK(strings::rextract_piece(whole, "=") == "k1");
    CHECK(whole.empty());
    CHECK(strings::rextract_piece(whole, "=") == "");
  }
  // rsplit returns the parts in right-to-left encounter order.
  if (true) {
    CHECK(strings::rsplit("a b c") == V{"c", "b", "a"});
    CHECK(strings::rsplit(" a") == V{"a", ""});
    CHECK(strings::rsplit("a ") == V{"", "a"});
    CHECK(strings::rsplit("") == V{});
    CHECK(strings::rsplit("abc") == V{"abc"});
  }
  // rsplit_n bounds the splits from the right; the remainder (the head)
  // comes last, delimiters intact.
  if (true) {
    CHECK(strings::rsplit_n("a,b,c,d", 2, ",") == V{"d", "c", "a,b"});
    CHECK(strings::rsplit_n("a,b,c,d", 1, ",") == V{"d", "a,b,c"});
    CHECK(strings::rsplit_n("a,b,c,d", 0, ",") == V{"a,b,c,d"});
    CHECK(strings::rsplit_n(",a", 1, ",") == V{"a", ""});
    CHECK(strings::rsplit_n("a,b", 5, ",") == strings::rsplit("a,b", ","));
    CHECK(strings::rsplit_n("", 3, ",") == V{});
  }
  // Reversed, `rsplit_n` matches Python `rsplit` with `maxsplit`.
  if (true) {
    auto parts = strings::rsplit_n("a,b,c,d", 2, ",");
    std::ranges::reverse(parts);
    CHECK(parts == V{"a,b", "c", "d"});
  }
  // Pin the invariant: rsplit is exactly split reversed, empties included.
  if (true) {
    for (const auto s : {"x,,y,"sv, ",x"sv, "x"sv, ",,"sv, "a,b,c"sv}) {
      auto fwd = strings::split(s, ",");
      std::ranges::reverse(fwd);
      CHECK(strings::rsplit(s, ",") == fwd);
    }
  }
  // A temporary string yields owning copies.
  if (true) {
    CHECK(strings::rsplit(std::string{"a b"}) ==
          std::vector<std::string>{"b", "a"});
  }
}

#pragma endregion
#pragma region SplitPg

TEST_CASE("SplitPg", "[StringUtilsTest]") {
  using PG = strings::piece_generator;
  if (true) {
    using V = std::vector<std::string_view>;

    CHECK(strings::split_gen(std::string_view{}) == V{});
    CHECK(strings::split_gen(opt_string_view{std::nullopt}) == V{});
    CHECK(strings::split_gen(0_optsv) == V{});
    CHECK(strings::split_gen(""sv) == V{""});
    CHECK(strings::split_gen(""_optsv) == V{""});
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
    CHECK((split_gen<PG, R>((0_optsv))) == V{});
    CHECK((split_gen<PG, R>((""sv))) == V{""});
    CHECK((split_gen<PG, R>((""_optsv))) == V{""});
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
  // The generator-based split is usable in constant evaluation, now that
  // more_pieces and reset are constexpr like their callers.
  static_assert(strings::split_gen("a b"sv).size() == 2U);
}

#pragma endregion
#pragma region SplitLines

TEST_CASE("SplitLines", "[StringUtilsTest]") {
  using V = std::vector<std::string_view>;
  // Any of the universal line breaks splits, and "\r\n" counts as one.
  if (true) {
    CHECK(strings::split_lines("a\nb"sv) == V{"a", "b"});
    CHECK(strings::split_lines("a\rb"sv) == V{"a", "b"});
    CHECK(strings::split_lines("a\r\nb"sv) == V{"a", "b"});
    CHECK(strings::split_lines("abc"sv) == V{"abc"});
  }
  // A trailing line break adds no empty line, but interior ones are kept.
  if (true) {
    CHECK(strings::split_lines("a\n"sv) == V{"a"});
    CHECK(strings::split_lines("a\r\n"sv) == V{"a"});
    CHECK(strings::split_lines(""sv) == V{});
    CHECK(strings::split_lines("\n"sv) == V{""});
    CHECK(strings::split_lines("\r\n"sv) == V{""});
    CHECK(strings::split_lines("\n\n"sv) == V{"", ""});
    CHECK(strings::split_lines("a\r\n\nb\r"sv) == V{"a", "", "b"});
    CHECK(strings::split_lines("\r\r\n"sv) == V{"", ""});
  }
  // Passing line_ends::keep retains each line's own break, Python
  // keepends-style.
  if (true) {
    using LE = strings::line_ends;
    CHECK(strings::split_lines("a\r\nb\n"sv, LE::keep) == V{"a\r\n", "b\n"});
    CHECK(strings::split_lines("a\n\nb"sv, LE::keep) == V{"a\n", "\n", "b"});
    CHECK(strings::split_lines("abc"sv, LE::keep) == V{"abc"});
    CHECK(strings::split_lines("\n"sv, LE::keep) == V{"\n"});
  }
  // extract_line peels one line at a time, consuming the break either way.
  if (true) {
    auto whole = "a\r\nb\nc"sv;
    CHECK(strings::extract_line(whole) == "a");
    CHECK(whole == "b\nc");
    CHECK(strings::extract_line(whole, strings::line_ends::keep) == "b\n");
    CHECK(strings::extract_line(whole) == "c");
    CHECK(whole.empty());
    // Empty remainder yields an empty line, which is why callers check whole
    // first, or use more_lines.
    CHECK(strings::extract_line(whole) == "");
  }
  // more_lines drives the loop, returning false at exhaustion with the line
  // untouched.
  if (true) {
    auto whole = "a\r\nb"sv;
    std::string_view line;
    CHECK(strings::more_lines(line, whole));
    CHECK(line == "a");
    CHECK(strings::more_lines(line, whole, strings::line_ends::keep));
    CHECK(line == "b");
    CHECK_FALSE(strings::more_lines(line, whole));
    CHECK(line == "b");
  }
  // Any code unit works, and a temporary string yields owning copies.
  if (true) {
    CHECK(strings::split_lines(u"a\r\nb"sv) ==
          std::vector<std::u16string_view>{u"a", u"b"});
    CHECK(strings::split_lines(std::string{"a\nb"}) ==
          std::vector<std::string>{"a", "b"});
    CHECK(
        strings::split_lines(std::string{"a\nb"}, strings::line_ends::keep) ==
        std::vector<std::string>{"a\n", "b"});
  }
  // The finder also plugs into the piece generator, where standard split
  // semantics apply instead: a trailing line break yields a trailing empty
  // piece.
  if (true) {
    strings::basic_piece_generator pg{"a\r\nb\n"sv,
        strings::line_delim_finder<char>{},
        strings::default_piece_filter<char>{}};
    CHECK(strings::split(pg) == V{"a", "b", ""});
  }
  static_assert(strings::split_lines("a\r\nb"sv).size() == 2U);
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
  std::u16string_view w = u"a;b";
  std::u16string_view part;
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
  CHECK(strings::as_upper(u'a') == u'A');
  CHECK(strings::as_lower(U'Z') == U'z');
  CHECK(strings::is_digit(u'7'));
  CHECK(strings::is_alpha(U'q'));
  CHECK_FALSE(strings::is_upper(char16_t{0xe9})); // U+00E9, not ASCII
  CHECK(strings::is_ascii('a'));
  CHECK(strings::is_ascii('\x7f'));
  CHECK_FALSE(strings::is_ascii(static_cast<char>(0x80)));
  CHECK_FALSE(strings::is_ascii(char16_t{0xe9}));
  CHECK(strings::is_printable(' '));
  CHECK(strings::is_printable('~'));
  CHECK_FALSE(strings::is_printable('\t'));
  CHECK_FALSE(strings::is_printable('\x7f'));
  CHECK_FALSE(strings::is_printable(static_cast<char>(0x80)));
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
  CHECK(strings::as_upper("abc"_czsv) == "ABC");
  CHECK(strings::ci_equal("Hi"_optsv, "hi"));

  // The hex helpers are constexpr, so parse_hex4 can be constant-evaluated.
  static_assert(strings::is_hex_digit('f'));
  static_assert(strings::is_hex_digit(u'A'));
  static_assert(!strings::is_hex_digit('g'));
  static_assert(strings::hex_digit_value('F') == 15);
  static_assert(strings::parse_hex4("beef"sv, 0).value() == 0xbeefU);
  // Regression: a huge pos used to wrap the bounds check and read out of
  // bounds.
  static_assert(!strings::parse_hex4("beef"sv, npos).has_value());
}

#pragma endregion

// Test basic_delim over a wide code unit.
#pragma region CaseStrings

TEST_CASE("CaseStrings", "[StringUtilsTest]") {
  // The string overloads require non-empty and all code units passing.
  if (true) {
    CHECK(strings::is_digit("123"));
    CHECK_FALSE(strings::is_digit("12a"));
    CHECK_FALSE(strings::is_digit(""));
    CHECK(strings::is_alpha("abc"));
    CHECK_FALSE(strings::is_alpha("ab1"));
    CHECK(strings::is_alnum("ab1"));
    CHECK_FALSE(strings::is_alnum("ab 1"));
    CHECK(strings::is_hex_digit("1aF"));
    CHECK_FALSE(strings::is_hex_digit("1aG"));
    CHECK(strings::is_space(" \t\r\n\v\f"));
    CHECK_FALSE(strings::is_space(" x "));
    CHECK(strings::is_lower("abc"));
    // Unlike Python `islower`, uncased characters are not ignored.
    CHECK_FALSE(strings::is_lower("abc1"));
    CHECK(strings::is_upper("ABC"));
    CHECK_FALSE(strings::is_upper("ABc"));
    CHECK(strings::is_digit(u"123"sv));
    CHECK(strings::is_ascii("plain text\r\n"));
    CHECK_FALSE(strings::is_ascii("caf\xc3\xa9"));
    // Unlike Python `isascii`, empty is false, as for all these predicates.
    CHECK_FALSE(strings::is_ascii(""));
    CHECK(strings::is_printable("plain text~"));
    CHECK_FALSE(strings::is_printable("tab\there"));
    CHECK_FALSE(strings::is_printable(""));
  }
  // Being objects, the predicates pass directly into algorithms and range
  // adaptors.
  if (true) {
    CHECK(std::ranges::all_of("123"sv, strings::is_digit));
    CHECK(std::ranges::none_of("abc"sv, strings::is_digit));
    auto digits = "a1b2c3"sv | std::views::filter(strings::is_digit);
    CHECK(std::string{digits.begin(), digits.end()} == "123");
  }
  // `is_title` follows the `as_titled` word rule, Python `istitle`-style: at
  // least
  // one letter, uppercase exactly at word starts.
  if (true) {
    CHECK(strings::is_title("Hello World"));
    CHECK(strings::is_title("A"));
    CHECK_FALSE(strings::is_title("hello world"));
    CHECK_FALSE(strings::is_title("HELLO"));
    CHECK_FALSE(strings::is_title("Hello world"));
    CHECK_FALSE(strings::is_title("3rd"));
    CHECK_FALSE(strings::is_title(""));
    CHECK_FALSE(strings::is_title("123"));
    // The quirk carries over: any non-letter starts a new word.
    CHECK(strings::is_title("They'Re"));
    CHECK(strings::is_title("3Rd Place"));
    CHECK_FALSE(strings::is_title("They're"));
    // The invariant with as_titled, quirk included.
    CHECK(strings::is_title(strings::as_titled("mIxEd uP, they're 3rd")));
    CHECK(strings::is_title(u"Wide Words"sv));
  }
  // The `is_python_*` predicates ignore non-letters, Python `islower` and
  // `isupper` style: at least one letter and none of the opposite case.
  if (true) {
    CHECK(strings::is_python_lower("abc0"));
    CHECK_FALSE(strings::is_lower("abc0"));
    CHECK_FALSE(strings::is_python_lower("aA"));
    CHECK_FALSE(strings::is_python_lower("0"));
    CHECK_FALSE(strings::is_python_lower(""));
    CHECK(strings::is_python_upper("ABC-0"));
    CHECK_FALSE(strings::is_python_upper("ABc"));
    CHECK_FALSE(strings::is_python_upper("123"));
    CHECK(strings::is_python_lower(u"abc 123"sv));
  }
  // ci_compare orders as if lowercased, as a weak ordering: case-insensitive
  // equals are equivalent, not equal.
  if (true) {
    CHECK(strings::ci_compare("apple", "APPLE") ==
          std::weak_ordering::equivalent);
    CHECK(strings::ci_compare("apple", "banana") == std::weak_ordering::less);
    CHECK(
        strings::ci_compare("Banana", "apple") == std::weak_ordering::greater);
    CHECK(strings::ci_compare("app", "apple") == std::weak_ordering::less);
    CHECK(strings::ci_compare("apple", "app") == std::weak_ordering::greater);
    CHECK(strings::ci_compare("", "") == std::weak_ordering::equivalent);
    CHECK(strings::ci_compare("", "a") == std::weak_ordering::less);
    CHECK(strings::ci_compare(u"AB"sv, u"ab"sv) ==
          std::weak_ordering::equivalent);
    // Contrast with the case-sensitive ordering, where 'B' < 'a' in ASCII.
    CHECK(std::is_lt("Banana"sv <=> "apple"sv));
  }
  // Swap, capitalize, and title-case, on copies and in place.
  if (true) {
    CHECK(strings::as_swapped('a') == 'A');
    CHECK(strings::as_swapped('A') == 'a');
    CHECK(strings::as_swapped('1') == '1');
    CHECK(strings::as_swapped("Hello, World!") == "hELLO, wORLD!");
    CHECK(strings::as_capitalized("hello WORLD") == "Hello world");
    CHECK(strings::as_capitalized("") == "");
    CHECK(strings::as_titled("hello world") == "Hello World");
    CHECK(strings::as_titled("HELLO WORLD") == "Hello World");
    // The Python `title` quirk: any non-letter starts a new word.
    CHECK(strings::as_titled("they're") == "They'Re");
    CHECK(strings::as_titled("3rd place") == "3Rd Place");
    std::string s{"aBc"};
    strings::to_swapped(s);
    CHECK(s == "AbC");
    strings::to_capitalized(s);
    CHECK(s == "Abc");
    s = "a b";
    strings::to_titled(s);
    CHECK(s == "A B");
  }
}

#pragma endregion
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

  // Container of wide strings: the delimiter's code unit is deduced from the
  // elements, so the default (single space) works with no explicit delimiter.
  std::vector<std::u16string> v{u" a ", u"  b"};
  strings::trim(v);
  CHECK(v[0] == u"a");
  CHECK(v[1] == u"b");

  // An explicit wide delimiter still works.
  std::vector<std::u16string> v2{u".a.", u"..b"};
  strings::trim(v2, strings::basic_delim<char16_t>{u"."});
  CHECK(v2[0] == u"a");
  CHECK(v2[1] == u"b");

  // Map-like container: the code unit is deduced from the mapped value.
  std::map<int, std::u16string> m{{1, u" a "}, {2, u"  b"}};
  strings::trim(m);
  CHECK(m[1] == u"a");
  CHECK(m[2] == u"b");

  // Braces.
  CHECK(strings::trim_braces(u"[x]") == u"x");
  CHECK(strings::add_braces(u"x") == u"[x]");
}

#pragma endregion
#pragma region AsViews

TEST_CASE("AsViews", "[StringUtilsTest]") {
  // Narrow container deduces `std::string_view` (unchanged behavior).
  const std::vector<std::string> narrow{"a", "bc"};
  const auto nv = strings::as_views(narrow);
  using nview = decltype(nv)::value_type;
  static_assert(std::is_same_v<nview, std::string_view>);
  CHECK(nv[0] == "a");
  CHECK(nv[1] == "bc");

  // Wide container deduces the elements' code unit.
  const std::vector<std::u16string> wide{u"a", u"bc"};
  const auto wv = strings::as_views(wide);
  using wview = decltype(wv)::value_type;
  static_assert(std::is_same_v<wview, std::u16string_view>);
  CHECK(wv[0] == u"a");
  CHECK(wv[1] == u"bc");
}

#pragma endregion

// Test locate.
#pragma region Locate

TEST_CASE("Locate", "[StringUtilsTest]") {
  using corvid::strings::location;
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
    // Regression: an empty value matches everywhere, so a non-match is never
    // found. These used to loop forever.
    CHECK(strings::locate_not("abc"sv, ""sv) == npos);
    CHECK(strings::locate_not(""sv, ""sv) == npos);
    CHECK(strings::rlocate_not("abc"sv, ""sv) == npos);
    CHECK(strings::rlocate_not(""sv, ""sv) == npos);
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
    CHECK(strings::rlocate_not(""sv, "abcdef"sv) == npos);
    pos = s.size();
    CHECK(strings::rlocated_not(pos, s, 'a') == true);
    CHECK(pos == 4U);
    --pos;
    CHECK(strings::rlocated_not(pos, s, 'a') == false);
    CHECK(pos == npos);
  }
  if (true) {
    using corvid::strings::location;
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

    // count_located is constexpr, like the locate family it wraps.
    static_assert(strings::count_located("abcabc"sv, 'a') == 2U);
    static_assert(strings::count_located("abcabc"sv, "abc"sv) == 2U);
  }
}

#pragma endregion
#pragma region RLocate

TEST_CASE("RLocate", "[StringUtilsTest]") {
  using corvid::strings::location;
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
    // Mirrors the corvid call it is compared against.
    // NOLINTNEXTLINE(performance-prefer-single-char-overloads)
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
  using corvid::strings::location;
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
    // An empty value matches everywhere, end of string included, mirroring
    // the forward direction.
    CHECK(strings::rlocate(s, "") == s.size());
    CHECK(strings::rlocate(s, "", 5U) == 5U);
    CHECK(strings::rlocate(s, {"xy", ""}) == location{s.size(), 1U});
    CHECK(strings::rlocate(s, {"xy", ""}, 5U) == location{5U, 1U});
    CHECK(strings::rlocate(""sv, "") == 0U);
    CHECK(strings::rlocate(""sv, {"xy", ""}) == location{0U, 1U});
  }
  // Confirm the correctness of infinite loops.
  if (true) {
    constexpr auto s = "abcdefghijabcdefghij"sv;
    // Mirrors the corvid call it is compared against.
    // NOLINTNEXTLINE(performance-prefer-single-char-overloads)
    CHECK(s.find("a") == 0U);
    CHECK(strings::locate(s, "a") == 0U);
    CHECK(s.find("") == 0U);
    CHECK(strings::locate(s, "") == 0U);
    CHECK(strings::locate(s, {""sv, ""sv}) == location{0U, 0U});
    CHECK(strings::locate(s, std::array<std::string_view, 0>{}) == nloc);
  }
}

#pragma endregion
#pragma region LocateUtilities

TEST_CASE("LocateUtilities", "[StringUtilsTest]") {
  using corvid::strings::location;
  using corvid::strings::pos_range;
  constexpr auto s = "abxcdef"sv;
  const auto vals = std::array{"ab"sv, "cd"sv};

  // as_pos_range spans the located value.
  auto loc = strings::locate(s, vals);
  CHECK(loc == location{0U, 0U});
  CHECK(strings::as_pos_range(s, vals, loc) == pos_range{0U, 2U});
  loc = strings::locate(s, vals, 1);
  CHECK(loc == location{3U, 1U});
  CHECK(strings::as_pos_range(s, vals, loc) == pos_range{3U, 5U});

  // Not-found yields npos_range, as does a locate_not-style location whose
  // pos_value indexes no value (regression: that used to read out of
  // bounds).
  CHECK(strings::as_pos_range(s, vals, nloc) == npos_range);
  CHECK(
      strings::as_pos_range(s, vals, location{1U, vals.size()}) == npos_range);

  // An empty value located at end-of-string is a real match (regression:
  // it used to misreport as npos_range).
  const auto ve = std::array{"q"sv, ""sv};
  loc = strings::locate(s, ve, s.size());
  CHECK(loc == location{s.size(), 1U});
  CHECK(strings::as_pos_range(s, ve, loc) == pos_range{s.size(), s.size()});

  // The single-position overload.
  CHECK(
      strings::as_pos_range(s, strings::locate(s, 'x')) == pos_range{2U, 3U});
  CHECK(strings::as_pos_range(s, npos) == npos_range);

  // min_value_size, including elements that merely convert to a view
  // (regression: `const char*` elements used to fail to compile).
  CHECK(
      strings::min_value_size(std::span<const std::string_view>{vals}) == 2U);
  const auto mixed = std::array{"abc"sv, "d"sv};
  CHECK(
      strings::min_value_size(std::span<const std::string_view>{mixed}) == 1U);
  const auto raw = std::array{"abc", "de"};
  CHECK(strings::min_value_size(std::span<const char* const>{raw}) == 2U);
  CHECK(strings::min_value_size(std::span<const std::string_view>{}) == 0U);

  // The location overload of from_npos.
  CHECK(strings::from_npos(s, location{s.size(), 0U}) == nloc);
  CHECK(strings::from_npos(s, location{2U, 0U}) == location{2U, 0U});
  CHECK((strings::from_npos<strings::npos_choice::size>(s,
            location{s.size(), 2U})) == location{s.size(), 2U});

  // locate_subview recovers the offset of a slice by identity.
  CHECK(strings::locate_subview(s, s.substr(3, 2)) == 3U);
  CHECK(strings::locate_subview(s, s) == 0U);
  // An empty slice is still located, even at the very end.
  CHECK(strings::locate_subview(s, s.substr(3, 0)) == 3U);
  CHECK(strings::locate_subview(s, s.substr(s.size())) == s.size());
  // Equal content in a different buffer is not a subview.
  const std::string elsewhere{s.substr(0, 3)};
  CHECK(strings::locate_subview(s, elsewhere) == npos);
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
  if (true) {
    // Regression: with an empty `to`, the multi-value overload used to skip
    // a character after each replacement, missing adjacent matches that the
    // single-value overload catches.
    auto s = std::string{"abab"};
    CHECK(strings::substitute(s, "ab"sv, ""sv) == 2U);
    CHECK(s == "");
    s = std::string{"abab"};
    CHECK(strings::substitute(s, {"ab"sv}, {""sv}) == 2U);
    CHECK(s == "");
    s = std::string{"xababy"};
    CHECK(strings::substitute(s, {"ab"sv}, {""sv}) == 2U);
    CHECK(s == "xy");
  }
  if (true) {
    // Substitution honors a nonzero starting pos; the prefix stays intact.
    auto s = std::string{"ababab"};
    CHECK(strings::substitute(s, "ab"sv, "x"sv, 2) == 2U);
    CHECK(s == "abxx");
    s = std::string{"ababab"};
    CHECK(strings::substitute(s, {"ab"sv}, {"xyz"sv}, 2) == 2U);
    CHECK(s == "abxyzxyz");
    // Growing substitution handles adjacent matches.
    s = std::string{"abab"};
    CHECK(strings::substitute(s, "ab"sv, "xyz"sv) == 2U);
    CHECK(s == "xyzxyz");
    // Pythonic insertion honors pos too.
    s = std::string{"abc"};
    CHECK(strings::substitute(s, ""sv, "x"sv, 1) == 3U);
    CHECK(s == "axbxcx");
    s = std::string{"abc"};
    CHECK(strings::substitute(s, {""sv}, {"x"sv}, 1) == 3U);
    CHECK(s == "axbxcx");
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
  if (true) {
    // Excision honors a nonzero starting pos; the prefix stays intact.
    auto s = std::string{"aXaXa"};
    CHECK(strings::excise(s, 'a', 1) == 2U);
    CHECK(s == "aXX");
    s = std::string{"abcabcabc"};
    CHECK(strings::excise(s, "abc"sv, 3) == 2U);
    CHECK(s == "abc");
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
    ostream_redirector cerr_to_ss(std::cerr, ss);
    strings::report("a=", 5);
    CHECK(ss.str() == "a=5\n");
  }
  if (true) {
    std::stringstream ss;
    ostream_redirector cout_to_ss(std::cout, ss);
    strings::println("a=", 5);
    CHECK(ss.str() == "a=5\n");
    ss.str("");
    strings::println_with(", ", 'a', 5, "bc", 5.5);
    CHECK(ss.str() == "a, 5, bc, 5.5\n");
  }
  if (true) {
    std::stringstream ss;
    ostream_redirector cout_to_ss(std::cout, ss);
    strings::print_with(", ", 42);
    CHECK(ss.str() == "42");
  }
  if (true) {
    std::stringstream ss;
    ostream_redirector cout_to_ss(std::cout, ss);
    strings::println_with(", ", 42);
    CHECK(ss.str() == "42\n");
  }
  if (true) {
    std::stringstream ss;
    ostream_redirector cerr_to_ss(std::cerr, ss);
    strings::report_with(", ", 42);
    CHECK(ss.str() == "42\n");
  }
#ifdef NOT_SUPPOSED_TO_COMPILE
  if (true) {
    std::stringstream ss;
    ostream_redirector cout_to_ss(std::cout, ss);
    strings::print("a=", NotStreamable{});
    CHECK(ss.str() == "a=5");
  }
#endif
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
    for (auto ndx = 0UZ; ndx < vsv.size(); ++ndx) {
      mss[static_cast<int>(ndx)] = vsv[ndx];
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
  if (true) {
    // Regression: an all-whitespace trim_left (and trim) used to return a
    // null view; it now returns an empty view anchored in the input,
    // matching trim_right.
    constexpr auto sp = "  "sv;
    CHECK(strings::trim_left(sp).data() == sp.data() + sp.size());
    CHECK(strings::trim_right(sp).data() == sp.data());
    CHECK(strings::trim(sp).data() != nullptr);
    CHECK(strings::trim(""sv).data() != nullptr);
    // So a trimmed non-null empty still splits as one empty piece.
    CHECK(strings::split_gen(strings::trim(sp)) ==
          std::vector<std::string_view>{""});
  }
}

#pragma endregion
#pragma region TrimAffixes

TEST_CASE("TrimAffixes", "[StringUtilsTest]") {
  if (true) {
    CHECK(strings::trim_prefix("foo.bar", "foo.") == "bar");
    CHECK(strings::trim_prefix("foo.bar", "bar") == "foo.bar");
    CHECK(strings::trim_prefix("foo", "foobar") == "foo");
    CHECK(strings::trim_prefix("foo", "") == "foo");
    CHECK(strings::trim_prefix("foo", "foo") == "");
    CHECK(strings::trim_prefix("", "foo") == "");
    // Removed at most once, unlike the set-based trims.
    CHECK(strings::trim_prefix("aab", "a") == "ab");
  }
  if (true) {
    CHECK(strings::trim_suffix("archive.tar.gz", ".gz") == "archive.tar");
    CHECK(strings::trim_suffix("archive.tar", ".gz") == "archive.tar");
    CHECK(strings::trim_suffix("gz", ".gz") == "gz");
    CHECK(strings::trim_suffix("x", "") == "x");
    CHECK(strings::trim_suffix("x", "x") == "");
    CHECK(strings::trim_suffix("baa", "a") == "ba");
  }
  // Any code unit works.
  if (true) {
    CHECK(strings::trim_prefix(u"..xy", u"..") == u"xy");
    CHECK(strings::trim_suffix(L"xy..", L"..") == L"xy");
  }
  // The returned view stays anchored in the input.
  if (true) {
    constexpr auto sp = "ab"sv;
    CHECK(strings::trim_prefix(sp, "a").data() == sp.data() + 1);
    CHECK(strings::trim_suffix(sp, "b").data() == sp.data());
  }
}

#pragma endregion
#pragma region ExpandTabs

TEST_CASE("ExpandTabs", "[StringUtilsTest]") {
  // A tab advances to the next multiple of tab_size, so the space count
  // depends on the column it falls in.
  if (true) {
    CHECK(strings::expand_tabs("\t") == "        ");
    CHECK(strings::expand_tabs("a\tb") == "a       b");
    CHECK(strings::expand_tabs("ab\tc") == "ab      c");
    // A tab landing exactly on a stop advances a full tab_size.
    CHECK(strings::expand_tabs("12345678\tx") == "12345678        x");
    CHECK(strings::expand_tabs("\t\t") == std::string(16, ' '));
    // The Python docs example, both at the default and at 4.
    CHECK(strings::expand_tabs("01\t012\t0123\t01234") ==
          "01      012     0123    01234");
    CHECK(strings::expand_tabs("01\t012\t0123\t01234", 4) ==
          "01  012 0123    01234");
  }
  // The column resets after a newline or carriage return.
  if (true) {
    CHECK(strings::expand_tabs("ab\n\tc") == "ab\n        c");
    CHECK(strings::expand_tabs("ab\r\tc") == "ab\r        c");
    CHECK(strings::expand_tabs("ab\r\n\tc") == "ab\r\n        c");
  }
  // Degenerate tab sizes: 1 makes every tab a single space, 0 deletes tabs.
  if (true) {
    CHECK(strings::expand_tabs("a\t\tb", 1) == "a  b");
    CHECK(strings::expand_tabs("a\tb\tc", 0) == "abc");
  }
  // Tab-free input passes through, and any code unit works.
  if (true) {
    CHECK(strings::expand_tabs("") == "");
    CHECK(strings::expand_tabs("no tabs here") == "no tabs here");
    CHECK(strings::expand_tabs(u"a\tb", 4) == u"a   b");
  }
}

#pragma endregion
#pragma region Textwrap

TEST_CASE("Textwrap", "[StringUtilsTest]") {
  namespace textwrap = strings::textwrap;
  using V = std::vector<std::string>;

  // The module is constexpr end to end: results can be computed and compared
  // entirely at compile time, so long as the allocation stays transient.
  static_assert(
      textwrap::dedent("    hello\n      world\n") == "hello\n  world\n");
  static_assert(textwrap::indent("a\nb", "> ") == "> a\n> b");
  static_assert(
      textwrap::wrap("one two three", {.width = 7}) ==
      std::vector<std::string>{"one two", "three"});
  static_assert(
      textwrap::fill("one two three", {.width = 8}) == "one two\nthree");
  static_assert(textwrap::shorten("one  two", 20) == "one two");

  // Dedent removes the margin common to all content lines.
  if (true) {
    CHECK(textwrap::dedent("    hello\n      world\n") == "hello\n  world\n");
    CHECK(textwrap::dedent("\tabc\n\tdef") == "abc\ndef");
    // The shortest indent bounds the margin.
    CHECK(textwrap::dedent("    less\n  least\n      most") ==
          "  less\nleast\n    most");
    // Tabs and spaces are distinct, so a mixed margin does not match.
    CHECK(textwrap::dedent("  a\n\tb") == "  a\n\tb");
  }
  // Whitespace-only lines neither count toward the margin nor keep their
  // whitespace.
  if (true) {
    CHECK(textwrap::dedent("  a\n   \n  b") == "a\n\nb");
    CHECK(textwrap::dedent("  a\n\n  b") == "a\n\nb");
    CHECK(textwrap::dedent("   ") == "");
  }
  // Degenerate inputs pass through.
  if (true) {
    CHECK(textwrap::dedent("") == "");
    CHECK(textwrap::dedent("abc") == "abc");
  }
  // The raw-string-literal use case that motivates it.
  if (true) {
    constexpr auto usage = R"(
      usage: frob [-x] file...
        -x  enable X mode)";
    CHECK(textwrap::dedent(usage) ==
          "\nusage: frob [-x] file...\n  -x  enable X mode");
  }
  // The consteval overload dedents a literal at compile time, returning a
  // zero-terminated view of a static constant and leaving nothing for
  // runtime.
  if (true) {
    constexpr auto usage = textwrap::dedent<R"(
      usage: frob [-x] file...
        -x  enable X mode)">();
    static_assert(
        usage.view() == "\nusage: frob [-x] file...\n  -x  enable X mode");
    CHECK(usage == "\nusage: frob [-x] file...\n  -x  enable X mode");
    CHECK(usage.c_str()[usage.size()] == '\0');
    // The backing storage is the `dedented` fixed_string constant.
    static_assert(usage.view().data() ==
                  textwrap::dedented<R"(
      usage: frob [-x] file...
        -x  enable X mode)">.data());
    // Any code unit, and the empty-margin and all-whitespace edges, work the
    // same as at runtime.
    constexpr auto wide = textwrap::dedent<L"  a\n  b">();
    static_assert(wide.view() == L"a\nb");
    static_assert(textwrap::dedent<"abc">().view() == "abc");
    static_assert(textwrap::dedent<"   ">().empty());
  }
  // Divergence from Python: line breaks are universal, so a blank CRLF line
  // is blank; Python sees its '\r' as content and gives up on the margin.
  if (true) { CHECK(textwrap::dedent("  a\r\n\r\n  b") == "a\r\n\r\nb"); }

  // Indent prefixes lines with content; blank and whitespace-only lines pass
  // through untouched.
  if (true) {
    CHECK(textwrap::indent("a\nb", "> ") == "> a\n> b");
    CHECK(textwrap::indent("a\n\nb\n", "> ") == "> a\n\n> b\n");
    CHECK(textwrap::indent("a\n  \nb", "> ") == "> a\n  \n> b");
    CHECK(textwrap::indent("one\ntwo", "  ") == "  one\n  two");
    CHECK(textwrap::indent("a\r\nb", "> ") == "> a\r\n> b");
  }
  // The predicate overload decides per line, seeing each line's break.
  if (true) {
    CHECK(textwrap::indent("a\n\nb", "> ", [](std::string_view) {
      return true;
    }) == "> a\n> \n> b");
  }

  // Wrap packs words greedily. All expectations here were generated from
  // CPython 3.12 with break_on_hyphens and fix_sentence_endings off, the
  // configuration this implementation matches.
  if (true) {
    const auto dog = "The quick brown fox jumped over the lazy dog"sv;
    CHECK(textwrap::wrap(dog) == V{std::string{dog}});
    CHECK(textwrap::wrap(dog, {.width = 10}) ==
          V{"The quick", "brown fox", "jumped", "over the", "lazy dog"});
    CHECK(textwrap::wrap(dog, {.width = 15}) ==
          V{"The quick brown", "fox jumped over", "the lazy dog"});
    CHECK(textwrap::fill(dog, {.width = 10}) ==
          "The quick\nbrown fox\njumped\nover the\nlazy dog");
  }
  // Overlong words break at the width, or get a line to themselves.
  if (true) {
    CHECK(textwrap::wrap("aaaaaaaaaab", {.width = 3}) ==
          V{"aaa", "aaa", "aaa", "ab"});
    CHECK(textwrap::wrap("aaaaaaaaaab",
              {.width = 3, .break_long_words = false}) == V{"aaaaaaaaaab"});
    // Without Python's break_on_hyphens, hyphenated words are no different.
    CHECK(textwrap::wrap("foo-bar baz", {.width = 6}) == V{"foo-ba", "r baz"});
  }
  // Indents count toward the width.
  if (true) {
    CHECK(
        textwrap::wrap("hello world",
            {.width = 8, .initial_indent = "* ", .subsequent_indent = "  "}) ==
        V{"* hello", "  world"});
  }
  // The whitespace knobs: keeping it, keeping line breaks, and tab handling.
  if (true) {
    CHECK(textwrap::wrap("a  b", {.width = 3, .drop_whitespace = false}) ==
          V{"a  ", "b"});
    CHECK(textwrap::wrap("a\nb", {.width = 10, .replace_whitespace = false}) ==
          V{"a\nb"});
    CHECK(textwrap::wrap("a\tb", {.width = 20}) == V{"a       b"});
    CHECK(textwrap::wrap("a\tb", {.width = 20, .expand_tabs = false}) ==
          V{"a b"});
    CHECK(textwrap::wrap("a\tb", {.width = 20, .tab_size = 4}) == V{"a   b"});
  }
  // Leading whitespace survives on the first line when content follows it;
  // whitespace-only input yields no lines at all.
  if (true) {
    CHECK(textwrap::wrap("  hello", {.width = 10}) == V{"  hello"});
    CHECK(textwrap::wrap("   ", {.width = 10}).empty());
    CHECK(textwrap::wrap("", {.width = 10}).empty());
  }
  // max_lines truncates, marking the cut with the placeholder.
  if (true) {
    const auto q = "Hello there, how are you this fine day?"sv;
    CHECK(
        textwrap::wrap(q, {.width = 15, .max_lines = 1}) == V{"Hello [...]"});
    CHECK(textwrap::wrap(q, {.width = 15, .max_lines = 2}) ==
          V{"Hello there,", "how are [...]"});
    CHECK(textwrap::wrap(q, {.width = 15, .max_lines = 3}) ==
          V{"Hello there,", "how are you", "this fine day?"});
    // Divergence from Python, which raises when the placeholder cannot fit:
    // here it is emitted anyway, overflowing the width.
    CHECK(textwrap::wrap("word", {.width = 4, .max_lines = 1}) == V{"word"});
    CHECK(textwrap::wrap("word another", {.width = 4, .max_lines = 1}) ==
          V{"[...]"});
  }

  // Shorten collapses whitespace and truncates to a single line.
  if (true) {
    CHECK(textwrap::shorten("Hello  world!", 12) == "Hello world!");
    CHECK(textwrap::shorten("Hello  world!", 11) == "Hello [...]");
    CHECK(textwrap::shorten("Hello, world!", 10, "...") == "Hello,...");
    CHECK(textwrap::shorten("Hello, world!", 5, "...") == "...");
    CHECK(textwrap::shorten("  leading and trailing   ", 100) ==
          "leading and trailing");
    CHECK(textwrap::shorten("one two three", 8) == "[...]");
  }

  // Any code unit works.
  if (true) {
    CHECK(textwrap::dedent(u"  a\n  b") == u"a\nb");
    CHECK(textwrap::indent(L"a\nb", L"> ") == L"> a\n> b");
    CHECK(textwrap::wrap(u"one two three", {.width = 7}) ==
          std::vector<std::u16string>{u"one two", u"three"});
    CHECK(textwrap::shorten(L"one  two", 20) == L"one two");
  }
}

#pragma endregion
#pragma region Fnmatch

TEST_CASE("Fnmatch", "[StringUtilsTest]") {
  namespace fnmatch = strings::fnmatch;

  // Everything matches at compile time.
  static_assert(fnmatch::fnmatch("Notes.TXT", "*.txt"));
  static_assert(fnmatch::fnmatchcase("data_07.csv", "data_[0-9][0-9].csv"));
  static_assert(!fnmatch::fnmatchcase("Notes.TXT", "*.txt"));

  // Literals and anchoring. Expectations here and below are pinned against
  // CPython `fnmatch.fnmatchcase` (3.12 and 3.14 agree on every case).
  if (true) {
    CHECK(fnmatch::fnmatchcase("abc", "abc"));
    CHECK_FALSE(fnmatch::fnmatchcase("abc", "abd"));
    CHECK(fnmatch::fnmatchcase("", ""));
    CHECK_FALSE(fnmatch::fnmatchcase("a", ""));
    CHECK_FALSE(fnmatch::fnmatchcase("abc", "ab"));
    CHECK_FALSE(fnmatch::fnmatchcase("ab", "abc"));
  }
  // Star.
  if (true) {
    CHECK(fnmatch::fnmatchcase("", "*"));
    CHECK(fnmatch::fnmatchcase("abc", "*"));
    CHECK(fnmatch::fnmatchcase("abc", "a*"));
    CHECK(fnmatch::fnmatchcase("abc", "*c"));
    CHECK(fnmatch::fnmatchcase("abc", "a*c"));
    CHECK(fnmatch::fnmatchcase("abc", "a*b*c"));
    CHECK(fnmatch::fnmatchcase("aXbYc", "a*b*c"));
    CHECK_FALSE(fnmatch::fnmatchcase("abc", "a*d"));
    CHECK_FALSE(fnmatch::fnmatchcase("aa", "a*a*a"));
    // Backtracking: the first "bc" run must be released for the second.
    CHECK(fnmatch::fnmatchcase("abcbcd", "a*bcd"));
    CHECK(fnmatch::fnmatchcase("mississippi", "m*issip*"));
    // Repeated stars collapse.
    CHECK(fnmatch::fnmatchcase("", "**"));
    CHECK(fnmatch::fnmatchcase("abc", "a**c"));
    // String matching, not path globbing: `*` crosses separators, and
    // newlines are ordinary code units.
    CHECK(fnmatch::fnmatchcase("a/b.txt", "*.txt"));
    CHECK(fnmatch::fnmatchcase("a\nb", "a*b"));
  }
  // Question mark.
  if (true) {
    CHECK_FALSE(fnmatch::fnmatchcase("", "?"));
    CHECK(fnmatch::fnmatchcase("abc", "a?c"));
    CHECK(fnmatch::fnmatchcase("abc", "???"));
    CHECK_FALSE(fnmatch::fnmatchcase("abc", "??"));
    CHECK_FALSE(fnmatch::fnmatchcase("abc", "????"));
    CHECK(fnmatch::fnmatchcase("a/b", "a?b"));
    CHECK(fnmatch::fnmatchcase("a\nb", "a?b"));
  }
  // Bracket sets.
  if (true) {
    CHECK(fnmatch::fnmatchcase("abc", "a[b]c"));
    CHECK(fnmatch::fnmatchcase("adc", "a[bd]c"));
    CHECK_FALSE(fnmatch::fnmatchcase("acc", "a[bd]c"));
    CHECK_FALSE(fnmatch::fnmatchcase("abc", "a[!b]c"));
    CHECK(fnmatch::fnmatchcase("adc", "a[!b]c"));
    CHECK(fnmatch::fnmatchcase("abc", "a[a-c]c"));
    CHECK_FALSE(fnmatch::fnmatchcase("adc", "a[a-c]c"));
    // `*` and `?` are literal inside a set.
    CHECK(fnmatch::fnmatchcase("*", "[*]"));
    CHECK_FALSE(fnmatch::fnmatchcase("a", "[*]"));
    CHECK(fnmatch::fnmatchcase("?", "[?]"));
    CHECK_FALSE(fnmatch::fnmatchcase("a", "[?]"));
    // Multiple ranges, and a `-` that is a member rather than a range.
    CHECK(fnmatch::fnmatchcase("b", "[a-cx-z]"));
    CHECK(fnmatch::fnmatchcase("y", "[a-cx-z]"));
    CHECK_FALSE(fnmatch::fnmatchcase("e", "[a-cx-z]"));
    CHECK(fnmatch::fnmatchcase("-", "[a-c-z]"));
    CHECK(fnmatch::fnmatchcase("z", "[a-c-z]"));
    CHECK_FALSE(fnmatch::fnmatchcase("m", "[a-c-z]"));
  }
  // Bracket-set edge cases, all matching CPython.
  if (true) {
    // A `]` first in the set is a literal member.
    CHECK(fnmatch::fnmatchcase("]", "[]]"));
    CHECK_FALSE(fnmatch::fnmatchcase("a", "[]]"));
    CHECK_FALSE(fnmatch::fnmatchcase("]", "[!]]"));
    CHECK(fnmatch::fnmatchcase("a", "[!]]"));
    // An unterminated `[` is a literal, as is everything after it.
    CHECK(fnmatch::fnmatchcase("[", "["));
    CHECK_FALSE(fnmatch::fnmatchcase("a", "["));
    CHECK(fnmatch::fnmatchcase("a[b", "a[b"));
    CHECK(fnmatch::fnmatchcase("[!", "[!"));
    CHECK(fnmatch::fnmatchcase("[!]", "[!]"));
    CHECK_FALSE(fnmatch::fnmatchcase("!", "[!]"));
    // A `-` first or last in the set is a literal member.
    CHECK(fnmatch::fnmatchcase("-", "[-a]"));
    CHECK(fnmatch::fnmatchcase("a", "[-a]"));
    CHECK_FALSE(fnmatch::fnmatchcase("b", "[-a]"));
    CHECK(fnmatch::fnmatchcase("-", "[a-]"));
    CHECK(fnmatch::fnmatchcase("a", "[a-]"));
    CHECK_FALSE(fnmatch::fnmatchcase("b", "[a-]"));
    // A reversed range is empty.
    CHECK_FALSE(fnmatch::fnmatchcase("a", "[z-a]"));
    CHECK_FALSE(fnmatch::fnmatchcase("z", "[z-a]"));
    CHECK(fnmatch::fnmatchcase("m", "[!z-a]"));
    // No escape character: a backslash is an ordinary code unit.
    CHECK(fnmatch::fnmatchcase("\\", "\\"));
    CHECK(fnmatch::fnmatchcase("\\a", "\\a"));
    CHECK(fnmatch::fnmatchcase("\\", "[\\]"));
    CHECK_FALSE(fnmatch::fnmatchcase("x", "[\\]"));
  }
  // `fnmatch` folds ASCII case on both sides, including range endpoints;
  // `fnmatchcase` does not.
  if (true) {
    CHECK(fnmatch::fnmatch("ABC", "abc"));
    CHECK(fnmatch::fnmatch("abc", "ABC"));
    CHECK(fnmatch::fnmatch("ABC", "[a-z][a-z][a-z]"));
    CHECK(fnmatch::fnmatch("abc", "[A-Z]bc"));
    CHECK_FALSE(fnmatch::fnmatchcase("ABC", "abc"));
    CHECK_FALSE(fnmatch::fnmatchcase("ABC", "[a-z][a-z][a-z]"));
    // Unlike Python on Windows, no path-separator rewrite ever happens.
    CHECK_FALSE(fnmatch::fnmatch("a/b", "a\\b"));
  }
  // Any code unit works, and so does any string-like argument.
  if (true) {
    CHECK(fnmatch::fnmatch(L"README.MD", L"readme.??"));
    CHECK(fnmatch::fnmatchcase(u"data.csv", u"*.csv"));
    CHECK(fnmatch::fnmatch(std::string{"Notes.TXT"}, "*.txt"));
  }
  // `filter` and `filterfalse` are lazy views over the name range. Both are
  // pinned against CPython 3.14, which is where `filterfalse` was added.
  if (true) {
    const std::vector<std::string> names{"a.cpp", "b.h", "C.CPP", "d.txt"};
    const auto matched =
        fnmatch::filter(names, "*.cpp") | std::ranges::to<std::vector>();
    CHECK(matched == std::vector<std::string>{"a.cpp", "C.CPP"});
    const auto rest =
        fnmatch::filterfalse(names, "*.cpp") | std::ranges::to<std::vector>();
    CHECK(rest == std::vector<std::string>{"b.h", "d.txt"});
    // Composes with further adaptors.
    CHECK(std::ranges::distance(
              fnmatch::filter(names, "*.cpp") | std::views::take(1)) == 1);
  }
}

#pragma endregion
#pragma region PurePath

TEST_CASE("PurePath", "[StringUtilsTest]") {
  namespace pure_path = strings::pure_path;

  // `match` right-anchors a relative pattern. Expectations here and below
  // are pinned against CPython 3.14 `PurePosixPath` (which is
  // case-sensitive, hence via the `_case` variants).
  if (true) {
    CHECK(pure_path::match_case("a/b/c.py", "c.py"));
    CHECK(pure_path::match_case("a/b/c.py", "*.py"));
    CHECK(pure_path::match_case("a/b/c.py", "b/*.py"));
    CHECK(pure_path::match_case("a/b/c.py", "a/b/c.py"));
    CHECK_FALSE(pure_path::match_case("a/b/c.py", "a/*.py"));
    CHECK_FALSE(pure_path::match_case("a/b/c.py", "x/c.py"));
    CHECK(pure_path::match_case("/a/b/c.py", "b/*.py"));
    CHECK(pure_path::match_case("/a/b/c.py", "a/b/c.py"));
    // A pattern longer than the path cannot match.
    CHECK_FALSE(pure_path::match_case("c.py", "b/c.py"));
    CHECK_FALSE(pure_path::match_case("c.py", "*/c.py"));
  }
  // An anchored pattern must cover the whole path.
  if (true) {
    CHECK(pure_path::match_case("/a/b/c.py", "/a/b/c.py"));
    CHECK(pure_path::match_case("/a", "/a"));
    CHECK_FALSE(pure_path::match_case("/a/b/c.py", "/*.py"));
    CHECK(pure_path::match_case("/a/b/c.py", "/*/*/*.py"));
    CHECK_FALSE(pure_path::match_case("/a/b/c.py", "/**/*.py"));
    CHECK_FALSE(pure_path::match_case("a/b/c.py", "/a/b/c.py"));
  }
  // Wildcards never match a bare root, and never cross separators.
  if (true) {
    CHECK_FALSE(pure_path::match_case("/a/b", "*/a/b"));
    CHECK_FALSE(pure_path::match_case("/a/b", "?/a/b"));
    CHECK_FALSE(pure_path::match_case("/a/b", "[/]/a/b"));
    CHECK_FALSE(pure_path::match_case("a/b", "a*b"));
    CHECK_FALSE(pure_path::match_case("a/b", "a?b"));
    CHECK(pure_path::match_case("ab", "a*b"));
  }
  // In `match`, `**` degrades to `*`: one component, not a run.
  if (true) {
    CHECK(pure_path::match_case("a/b/c.py", "**/*.py"));
    CHECK(pure_path::match_case("a/b/c.py", "**/**/*.py"));
    CHECK(pure_path::match_case("a/b/c.py", "a/**/c.py"));
    CHECK_FALSE(pure_path::match_case("a/x/b/c.py", "a/**/c.py"));
  }
  // "." components, duplicate separators, and a trailing separator all
  // normalize away, while ".." stays literal, on both sides.
  if (true) {
    CHECK(pure_path::match_case("./a/b", "a/b"));
    CHECK(pure_path::match_case("a/./b", "a/b"));
    CHECK(pure_path::match_case("a//b", "a/b"));
    CHECK(pure_path::match_case("a/b/", "a/b"));
    CHECK(pure_path::match_case("a/b", "./a/b"));
    CHECK(pure_path::match_case("a/b", "a//b"));
    CHECK(pure_path::match_case("a/b", "a/b/"));
    CHECK(pure_path::match_case("../a", "../a"));
    CHECK(pure_path::match_case("../a", "*/a"));
    CHECK(pure_path::match_case("a/../b", "a/../b"));
    CHECK_FALSE(pure_path::match_case("b", "a/../b"));
  }
  // Bare root and dot, and the no-raise divergence: an empty pattern (or
  // "."), which Python rejects with `ValueError`, matches nothing here.
  if (true) {
    CHECK(pure_path::match_case("/", "/"));
    CHECK_FALSE(pure_path::match_case(".", "*"));
    CHECK(pure_path::match_case("a", "*"));
    CHECK_FALSE(pure_path::match_case("a", ""));
    CHECK_FALSE(pure_path::match_case("a", "."));
    CHECK_FALSE(pure_path::full_match_case("a", ""));
    CHECK_FALSE(pure_path::full_match_case("a", "."));
  }
  // `full_match` covers the whole path.
  if (true) {
    CHECK_FALSE(pure_path::full_match_case("a/b/c.py", "c.py"));
    CHECK_FALSE(pure_path::full_match_case("a/b/c.py", "*.py"));
    CHECK_FALSE(pure_path::full_match_case("a/b/c.py", "b/*.py"));
    CHECK(pure_path::full_match_case("a/b/c.py", "a/b/c.py"));
    CHECK(pure_path::full_match_case("a/b/c.py", "a/*/c.py"));
    CHECK(pure_path::full_match_case("a/b/c.py", "*/*/*.py"));
    CHECK(pure_path::full_match_case("/a/b", "/a/b"));
    CHECK_FALSE(pure_path::full_match_case("/a/b", "a/b"));
    CHECK_FALSE(pure_path::full_match_case("a/b", "/a/b"));
    CHECK_FALSE(pure_path::full_match_case("/a", "*/a"));
    CHECK_FALSE(pure_path::full_match_case("/a", "[/]/a"));
  }
  // A `**` component matches any run of components; embedded, it degrades
  // to `*`.
  if (true) {
    CHECK(pure_path::full_match_case("a/b/c.py", "**/*.py"));
    CHECK(pure_path::full_match_case("a/b/c.py", "a/**/c.py"));
    CHECK(pure_path::full_match_case("a/c.py", "a/**/c.py"));
    CHECK(pure_path::full_match_case("a/b/x/c.py", "a/**/c.py"));
    CHECK(pure_path::full_match_case("a/b/c.py", "a/**"));
    CHECK(pure_path::full_match_case("a/b/c.py", "**"));
    CHECK(pure_path::full_match_case("a/b/c.py", "**/c.py"));
    CHECK(pure_path::full_match_case("a/b/c.py", "**/b/c.py"));
    CHECK(pure_path::full_match_case("a", "**"));
    CHECK(pure_path::full_match_case(".", "**"));
    CHECK(pure_path::full_match_case("../a", "**/a"));
    CHECK(pure_path::full_match_case("a/../b", "a/*/b"));
    CHECK(pure_path::full_match_case("a/b", "a**/b"));
    CHECK_FALSE(pure_path::full_match_case("a/b", "**b"));
  }
  // The root subtleties, exactly as CPython has them: `**` absorbs a root
  // only together with at least one real component, except that a bare `**`
  // matches everything.
  if (true) {
    CHECK(pure_path::full_match_case("/a/b", "**"));
    CHECK(pure_path::full_match_case("/", "**"));
    CHECK(pure_path::full_match_case("/a/b", "**/b"));
    CHECK_FALSE(pure_path::full_match_case("/a/b", "**/a/b"));
    CHECK_FALSE(pure_path::full_match_case("/a", "**/a"));
    CHECK(pure_path::full_match_case("a", "**/a"));
    CHECK(pure_path::full_match_case("/a/b", "/**"));
    CHECK(pure_path::full_match_case("/a/b", "/**/b"));
    CHECK(pure_path::full_match_case("/a/b", "/**/a/b"));
    CHECK(pure_path::full_match_case("/a/b", "/a/**"));
    CHECK(pure_path::full_match_case("/", "/**"));
    CHECK_FALSE(pure_path::full_match_case("a/b", "/**/a/b"));
  }
  // The folded variants, and wide path arguments.
  if (true) {
    CHECK(pure_path::match("A/B/C.PY", "c.py"));
    CHECK(pure_path::full_match("SRC/a.h", "src/*.[Hh]"));
    CHECK_FALSE(pure_path::match_case("A/B/C.PY", "c.py"));
    CHECK(pure_path::match(L"a/b/c.py", L"b/*.py"));
  }
#ifdef _WIN32
  // Drive-letter grammar: the host's path grammar does the parsing, so
  // these only hold on Windows. Pinned against `PureWindowsPath` (which
  // folds case, hence the unfolded variants).
  if (true) {
    CHECK(pure_path::match("C:/x/y", "c:/x/y"));
    CHECK_FALSE(pure_path::match("C:/x/y", "*.py"));
    CHECK(pure_path::match("C:/x/y.py", "*.PY"));
    CHECK(pure_path::match("C:/x/y", "x/y"));
    CHECK(pure_path::match_case("C:x", "C:x"));
    CHECK(pure_path::match_case("C:x", "x"));
    CHECK(pure_path::full_match("C:/x/y", "C:/**"));
    CHECK(pure_path::full_match("C:/x/y", "C:/x/**"));
    CHECK(pure_path::full_match("C:/x/y", "**"));
    CHECK_FALSE(pure_path::full_match("C:/x/y", "D:/**"));
    // A drive-plus-root pair falls to `**` on its own; a lone drive only
    // along with a real component.
    CHECK(pure_path::full_match_case("C:/x", "**/x"));
    CHECK_FALSE(pure_path::full_match_case("C:x", "**/x"));
    CHECK(pure_path::full_match_case("C:x/y", "**/y"));
  }
#endif
}

#pragma endregion
#pragma region Justification

TEST_CASE("Justification", "[StringUtilsTest]") {
  // `ljust` and `rjust` name where the content goes; the fill lands opposite.
  if (true) {
    CHECK(strings::ljust("ab", 5) == "ab   ");
    CHECK(strings::rjust("ab", 5) == "   ab");
    CHECK(strings::ljust("ab", 5, '*') == "ab***");
    CHECK(strings::rjust("ab", 5, '*') == "***ab");
    // Already wide enough: an unpadded copy.
    CHECK(strings::ljust("abc", 2) == "abc");
    CHECK(strings::rjust("abc", 3) == "abc");
    CHECK(strings::ljust("", 3) == "   ");
  }
  // `center` puts odd padding on the right, as `std::format`'s `^` does;
  // Python `center` would give "  ab " for width 5.
  if (true) {
    CHECK(strings::center("ab", 4) == " ab ");
    CHECK(strings::center("ab", 5) == " ab  ");
    CHECK(strings::center("a", 4, '.') == ".a..");
    CHECK(strings::center("abc", 2) == "abc");
    CHECK(strings::center("", 2) == "  ");
  }
  // `zfill` zero-fills after any leading sign.
  if (true) {
    CHECK(strings::zfill("42", 5) == "00042");
    CHECK(strings::zfill("-42", 5) == "-0042");
    CHECK(strings::zfill("+3.14", 7) == "+003.14");
    CHECK(strings::zfill("abc", 5) == "00abc");
    CHECK(strings::zfill("", 3) == "000");
    CHECK(strings::zfill("-", 2) == "-0");
    CHECK(strings::zfill("12345", 3) == "12345");
  }
  // Any code unit works.
  if (true) {
    CHECK(strings::rjust(u"ab"sv, 4) == u"  ab");
    CHECK(strings::center(L"ab", 4, L'-') == L"-ab-");
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
    int64_t t{};
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
  if (true) {
    // Regression: a signed value in prefixed hex used to garble the output
    // ("0x000000-1"); it now renders as the unsigned two's-complement bit
    // pattern.
    CHECK(strings::num_as_string<16>(int8_t{-1}) == "0xff");
    CHECK(strings::num_as_string<16>(int32_t{-1}) == "0xffffffff");
    CHECK(strings::num_as_string<16>(int32_t{16}) == "0x00000010");
    CHECK(strings::num_as_string<16>(std::numeric_limits<int64_t>::min()) ==
          "0x8000000000000000");
    // Regression: the worst-case integer rendering (a sign plus 64 binary
    // digits) used to overflow the buffer and silently append nothing.
    CHECK(strings::num_as_string<2>(std::numeric_limits<int64_t>::min()) ==
          "-1" + std::string(63, '0'));
    // Regression: fixed-format output longer than 64 characters used to
    // silently append nothing.
    const auto big = strings::num_as_string<std::chars_format::fixed>(1e300);
    CHECK(big.size() == 301U);
    CHECK(big.starts_with('1'));
    CHECK((strings::num_as_string<std::chars_format::fixed, 100>(1.5)) ==
          "1.5" + std::string(99, '0'));
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
  strings::append_num(s, 0.25);
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
    CHECK((value > 3.13F && value < 3.15F));
    CHECK(result.ptr == (sv.data() + sv.size()));

    // Basic negative float.
    sv = "-2.5";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == -2.5F);
    CHECK(result.ptr == (sv.data() + sv.size()));

    // Integer parsed as float.
    sv = "42";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 42.0F);

    // Zero.
    sv = "0.0";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 0.0F);

    // Negative zero.
    sv = "-0.0";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    // Negative zero should be equal to positive zero.
    CHECK(value == 0.0F);

    // Scientific notation.
    sv = "1.5e3";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value == 1500.0F);

    // Negative exponent.
    sv = "1.5e-2";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 0.014F && value < 0.016F));

    // Large positive exponent.
    sv = "1e30";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(value > 9e29F);

    // Very small positive number.
    sv = "1e-30";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 0.0F && value < 1e-29F));

    // Partial parse (stops at non-numeric).
    sv = "3.14abc";
    result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK((value > 3.13F && value < 3.15F));
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
    float fvalue{42.0F};
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
    CHECK((f > 3.13F && f < 3.15F));
    CHECK(sv == "  "); // Whitespace trimmed from left, remaining on right.

    // Scientific notation.
    sv = "6.022e23";
    CHECK(strings::extract_num(f, sv));
    CHECK(f > 6e23F);
    CHECK(sv.empty());
  }
  // Test parse_num float wrappers.
  if (true) {
    // Successful parse.
    auto opt = strings::parse_num<float>("2.5"sv);
    CHECK(opt.has_value());
    CHECK(opt.value() == 2.5F);

    // Failure - trailing garbage.
    opt = strings::parse_num<float>("2.5abc"sv);
    CHECK_FALSE(opt.has_value());

    // Failure - invalid.
    opt = strings::parse_num<float>("invalid"sv);
    CHECK_FALSE(opt.has_value());

    // With default value.
    auto val = strings::parse_num<float>("1.5"sv, -1.0F);
    CHECK(val == 1.5F);

    val = strings::parse_num<float>("bad"sv, -1.0F);
    CHECK(val == -1.0F);

    val = strings::parse_num<float>("1.5 "sv, -1.0F);
    CHECK(val == -1.0F); // Trailing space causes failure.
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
    auto val = strings::parse_num<double>("2.718281828"sv, 0.0);
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
    CHECK(s.capacity() >= 50U);
    s.clear();
    CHECK(s.capacity() >= 50U);
    s.shrink_to_fit();
    CHECK((s.capacity()) < (50U));
    CHECK((s.capacity()) > (0U));
  }

  // Capture the SSO capacity (typically 15 on libc++ 64-bit).
  const auto sso_cap = std::string{}.capacity();

  // Ensure that the small values we use below are within SSO capacity.
  CHECK((sso_cap) > (10U));

  using corvid::strings::no_zero;

  // `resize_to`: size matches the requested value exactly; capacity covers it.
  // Shrinking via `resize_to` does NOT reduce capacity: important for the
  // fill-buffer-then-commit pattern.
  if (true) {
    std::string s;

    // Zero.
    no_zero{s}.resize_to(0);
    CHECK(s.size() == 0U);

    // Tiny (SSO range).
    no_zero{s}.resize_to(2);
    CHECK(s.size() == 2U);
    CHECK(s.capacity() >= 2U);

    no_zero{s}.resize_to(4);
    CHECK(s.size() == 4U);
    CHECK(s.capacity() >= 4U);

    // Shrink within SSO: capacity must not change.
    auto cap = s.capacity();
    no_zero{s}.resize_to(2);
    CHECK(s.size() == 2U);
    CHECK(s.capacity() == cap);

    // Small (heap range).
    no_zero{s}.resize_to(50);
    CHECK(s.size() == 50U);
    CHECK(s.capacity() >= 50U);

    no_zero{s}.resize_to(100);
    CHECK(s.size() == 100U);
    CHECK(s.capacity() >= 100U);

    // Shrink on heap: capacity must not change.
    cap = s.capacity();
    no_zero{s}.resize_to(50);
    CHECK(s.size() == 50U);
    CHECK(s.capacity() == cap);

    // Same size (no-op).
    no_zero{s}.resize_to(50);
    CHECK(s.size() == 50U);
    CHECK(s.capacity() == cap);
  }

  // `enlarge_to_cap`: resizes to the full current capacity so that
  // `size() == capacity()`.
  if (true) {
    // On an empty string: size expands to the SSO capacity.
    std::string s;
    no_zero{s}.enlarge_to_cap();
    CHECK(s.size() == sso_cap);
    CHECK(s.size() == s.capacity());

    // On a heap-allocated string: fills out to the full allocated capacity.
    no_zero{s}.resize_to(50);
    auto cap = s.capacity();
    no_zero{s}.enlarge_to_cap();
    CHECK(s.size() == cap);
    CHECK(s.size() == s.capacity());
  }

  // `trim_to`: shrinks when `new_size` is smaller, but never enlarges and
  // never changes capacity.
  if (true) {
    static_assert(requires(std::string& value) { no_zero{value}.trim_to(1); });
    static_assert(requires(std::string& value) {
      no_zero{value}.trim_to(unsigned{1});
    });
    static_assert(requires(std::string& value) {
      no_zero{value}.trim_to(int16_t{-1});
    });

    std::string s;

    no_zero{s}.resize_to(50);
    auto cap = s.capacity();

    // Shrink within current size.
    no_zero{s}.trim_to(20);
    CHECK(s.size() == 20U);
    CHECK(s.capacity() == cap);

    // Same size is a no-op.
    no_zero{s}.trim_to(20);
    CHECK(s.size() == 20U);
    CHECK(s.capacity() == cap);

    // Larger size request must not enlarge.
    no_zero{s}.trim_to(40);
    CHECK(s.size() == 20U);
    CHECK(s.capacity() == cap);

    // Trimming to zero works and still preserves capacity.
    no_zero{s}.trim_to(0);
    CHECK(s.size() == 0U);
    CHECK(s.capacity() == cap);

    // Negative signed values clamp to zero.
    no_zero{s}.resize_to(30);
    no_zero{s}.trim_to(-1);
    CHECK(s.size() == 0U);
    CHECK(s.capacity() == cap);

    // Positive signed values trim normally after the signed check.
    no_zero{s}.resize_to(30);
    no_zero{s}.trim_to(int16_t{6});
    CHECK(s.size() == 6U);
    CHECK(s.capacity() == cap);

    // Any integer type is accepted, including unsigned non-size_t.
    no_zero{s}.resize_to(30);
    no_zero{s}.trim_to(7U);
    CHECK(s.size() == 7U);
    CHECK(s.capacity() == cap);

    // Returns a reference to the same string.
    CHECK(&*no_zero{s}.trim_to(10) == &s);
    CHECK(s.size() == 7U);
    CHECK(s.capacity() == cap);
  }

  // `enlarge_to`: size is at least `minimum_size`, and always fills capacity.
  // When `minimum_size` fits in the current buffer, no reallocation occurs.
  if (true) {
    // Tiny request on an empty string: fits in SSO, so size expands to the
    // full SSO capacity.
    std::string s;
    no_zero{s}.enlarge_to(3);
    CHECK(s.size() >= 3U);
    CHECK(s.size() == sso_cap);
    CHECK(s.size() == s.capacity());

    // Another tiny request within current capacity: no reallocation, size
    // stays at the full current capacity.
    auto cap_before = s.capacity();
    no_zero{s}.enlarge_to(2);
    CHECK(s.size() == cap_before);
    CHECK(s.capacity() == cap_before);

    // Small request beyond current capacity: reallocates, then fills capacity.
    no_zero{s}.enlarge_to(50);
    CHECK(s.size() >= 50U);
    CHECK(s.size() == s.capacity());

    // Request within the new capacity: no reallocation.
    cap_before = s.capacity();
    no_zero{s}.enlarge_to(50);
    CHECK(s.size() == cap_before);
    CHECK(s.capacity() == cap_before);

    // Large request well beyond current capacity: reallocates and fills.
    auto large = s.capacity() * 4;
    no_zero{s}.enlarge_to(large);
    CHECK(s.size() >= large);
    CHECK(s.size() == s.capacity());

    // Returns a reference to the same string.
    CHECK(&*no_zero{s}.enlarge_to(4) == &s);
  }

  // `clear_out`: releases the heap buffer (capacity drops to SSO level) and
  // sets size to zero. On an SSO string, capacity is already minimal.
  if (true) {
    // Heap-allocated string: buffer is released.
    std::string s;
    no_zero{s}.enlarge_to(100);
    CHECK(s.capacity() >= 100U);
    no_zero{s}.clear_out();
    CHECK(s.size() == 0U);
    CHECK((s.capacity()) < (100U));
    CHECK(s.capacity() >= sso_cap);

    // SSO-sized string: capacity is unchanged (nothing to release).
    std::string t;
    no_zero{t}.resize_to(4);
    auto cap = t.capacity();
    no_zero{t}.clear_out();
    CHECK(t.size() == 0U);
    CHECK(t.capacity() == cap);

    // Returns a reference to the same string.
    CHECK(&*no_zero{s}.clear_out() == &s);
  }

  // `rightsize_to`: when capacity is within [minimum_size, maximum_size],
  // behaves like `enlarge_to`; when capacity exceeds `maximum_size`, releases
  // the buffer and resizes to exactly `minimum_size` (rounding up to
  // capacity).
  if (true) {
    // Tiny: SSO capacity within bounds -> enlarge_to path.
    std::string s;
    no_zero{s}.rightsize_to(3, 100);
    CHECK(s.size() >= 3U);
    CHECK(s.size() == s.capacity());

    // Tiny: SSO capacity above maximum -> shrink to minimum_size.
    std::string t;
    no_zero{t}.resize_to(4); // capacity == sso_cap
    no_zero{t}.rightsize_to(2, sso_cap - 1);
    CHECK(t.size() == 2U);

    // Small: capacity within bounds -> enlarge_to path.
    std::string u;
    no_zero{u}.rightsize_to(50, 500);
    CHECK(u.size() >= 50U);
    CHECK(u.size() == u.capacity());

    // Small: capacity above maximum -> shrinks to minimum_size.
    no_zero{u}.enlarge_to(200);
    CHECK(u.capacity() >= 200U);
    no_zero{u}.rightsize_to(50, 100);
    CHECK(u.size() == 50U);
    CHECK((u.capacity()) < (200U));

    // Returns a reference to the same string.
    CHECK(&*no_zero{u}.rightsize_to(50, 500) == &u);
  }

  // The wrapper is code-unit generic, methods chain, and the arrow reaches
  // the wrapped string.
  if (true) {
    std::wstring w;
    no_zero{w}.enlarge_to(50);
    CHECK(w.size() >= 50U);
    CHECK(w.size() == w.capacity());
    CHECK(no_zero{w}.trim_to(3)->size() == 3U);

    std::u16string u16;
    CHECK(&*no_zero{u16}.resize_to(20).clear_out() == &u16);
    CHECK(u16.size() == 0U);
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

  // Regression: an empty separator used to return an empty token without
  // consuming anything, an infinite-loop trap; it now consumes the whole
  // input as one token, matching extract_piece.
  input = "abc";
  CHECK(token_parser::next_delimited("", input) == "abc");
  CHECK(input.empty());
  CHECK(token_parser::next_delimited("", input) == "");
}

#pragma endregion

// Test token parsing over a wide code unit.
#pragma region WideTokenParser

TEST_CASE("WideTokenParser", "[StringUtilsTest]") {
  using parser = strings::basic_token_parser<char16_t>;

  parser p{u"\r\n"};
  std::u16string_view input = u"alpha\r\n\r\nbeta";
  CHECK(p.separator() == u"\r\n");
  CHECK(p.next_delimited(input) == u"alpha");
  CHECK(p.next_delimited(input) == u"");
  CHECK(p.next_delimited(input) == u"beta");
  CHECK(input.empty());

  // Single-character separator overloads.
  input = u"one,two,three";
  CHECK(parser::next_delimited(u',', input) == u"one");
  CHECK(parser::next_delimited(u',', input) == u"two");
  CHECK(parser::next_delimited(u',', input) == u"three");
  CHECK(input.empty());

  // Terminated: the terminator is required.
  input = u"a;b";
  auto t = parser::next_terminated(u';', input);
  REQUIRE(t.has_value());
  CHECK(*t == u"a");
  t = parser::next_terminated(u';', input);
  REQUIRE_FALSE(t.has_value()); // no trailing ';'
  CHECK(input == u"b");
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
  REQUIRE(v1.size() == 1U);
  CHECK(v1[0] == "hello");

  auto v2 =
      strings::as_vector(std::string{"a"}, std::string{"b"}, std::string{"c"});
  REQUIRE(v2.size() == 3U);
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
  REQUIRE(sv.size() == 2U);
  CHECK(sv[0] == "x");
  CHECK(sv[1] == "y");
}

TEST_CASE("DelimFormats", "[StringUtilsTest]") {
  // basic_delim is a string_view_wrapper child, so the wrapper formatter
  // already covers it; this guards against a regression in that coverage.
  CHECK(std::format("{}", delim{", "}) == ", ");
  CHECK(std::format("{:?}", delim{"\t"}) == R"("\t")");
}

#pragma endregion
#pragma region Escaping

TEST_CASE("Escaping", "[StringUtilsTest]") {
  SECTION("append_escaped by character") {
    std::string out;
    auto put = [&out](char c) {
      out += c;
      return true;
    };
    CHECK(strings::append_escaped('a', put));
    CHECK(strings::append_escaped('"', put));
    CHECK(strings::append_escaped('\\', put));
    CHECK(strings::append_escaped('\t', put));
    CHECK(strings::append_escaped('\n', put));
    CHECK(strings::append_escaped('\r', put));
    CHECK(strings::append_escaped('\x1f', put));
    CHECK(out == R"(a\"\\\t\n\r\u{1f})");
  }
  SECTION("append_escaped_ucode digit widths") {
    std::string out;
    auto put = [&out](char c) {
      out += c;
      return true;
    };
    CHECK(strings::append_escaped_ucode(0x00, put));
    CHECK(strings::append_escaped_ucode(0x0f, put));
    CHECK(strings::append_escaped_ucode(0x10, put));
    CHECK(strings::append_escaped_ucode(0xff, put));
    CHECK(out == R"(\u{0}\u{f}\u{10}\u{ff})");
  }
  SECTION("append_escaped whole string") {
    std::string out;
    CHECK(strings::append_escaped(out, "say \"hi\"\t"));
    CHECK(out == R"(say \"hi\"\t)");
    out.clear();
    CHECK(strings::append_escaped(out, "\x01\x7f"));
    CHECK(out == R"(\u{1}\u{7f})");
  }
  SECTION("parse_u_code") {
    char ch{};
    auto sv = R"(\u{1f}rest)"sv;
    CHECK(strings::parse_u_code(sv, ch));
    CHECK(ch == '\x1f');
    CHECK(sv == "rest");

    sv = R"(\u{f})"sv;
    CHECK(strings::parse_u_code(sv, ch));
    CHECK(ch == '\x0f');
    CHECK(sv.empty());

    // Leading zeros are legal while the value fits a byte.
    sv = R"(\u{00ff})"sv;
    CHECK(strings::parse_u_code(sv, ch));
    CHECK(ch == '\xff');

    // Failure leaves the view unchanged.
    const auto reject = [](std::string_view bad) {
      char c{};
      const auto save = bad;
      const auto parsed = strings::parse_u_code(bad, c);
      return !parsed && bad == save;
    };
    CHECK(reject(R"(\u{})"));    // no digits
    CHECK(reject(R"(\u{100})")); // over 0xff
    CHECK(reject(R"(\u{zz})"));  // not hex
    CHECK(reject(R"(\u{1f)"));   // unterminated
    CHECK(reject(R"(\x{1f})"));  // wrong introducer
    CHECK(reject("plain"));
  }
  SECTION("parse_escaped single") {
    char ch{};
    auto sv = R"(\n\t\r\"\\x)"sv;
    CHECK(strings::parse_escaped(sv, ch));
    CHECK(ch == '\n');
    CHECK(strings::parse_escaped(sv, ch));
    CHECK(ch == '\t');
    CHECK(strings::parse_escaped(sv, ch));
    CHECK(ch == '\r');
    CHECK(strings::parse_escaped(sv, ch));
    CHECK(ch == '"');
    CHECK(strings::parse_escaped(sv, ch));
    CHECK(ch == '\\');
    // What remains is plain text, not an escape.
    CHECK_FALSE(strings::parse_escaped(sv, ch));
    CHECK(sv == "x");

    sv = R"(\u{41}z)"sv;
    CHECK(strings::parse_escaped(sv, ch));
    CHECK(ch == 'A');
    CHECK(sv == "z");

    sv = R"(\q)"sv;
    CHECK_FALSE(strings::parse_escaped(sv, ch));
    CHECK(sv == R"(\q)");
    sv = R"(\)"sv;
    CHECK_FALSE(strings::parse_escaped(sv, ch));
  }
  SECTION("parse_escaped whole string") {
    std::string out;
    CHECK(strings::parse_escaped(R"(say \"hi\"\t\u{1})", out));
    CHECK(out == "say \"hi\"\t\x01");
    out.clear();
    CHECK(strings::parse_escaped("plain", out));
    CHECK(out == "plain");
    out.clear();
    CHECK_FALSE(strings::parse_escaped(R"(bad \q escape)", out));
  }
  SECTION("escape round trip") {
    const std::string original =
        "mixed \"text\"\twith\r\nbytes \x01\x7f and \\ too";
    std::string escaped;
    CHECK(strings::append_escaped(escaped, original));
    std::string back;
    CHECK(strings::parse_escaped(escaped, back));
    CHECK(back == original);
  }
  SECTION("parse_escaped_quoted") {
    std::string out;
    auto sv = R"("say \"hi\"" tail)"sv;
    CHECK(strings::parse_escaped_quoted(sv, out));
    CHECK(out == R"(say "hi")");
    CHECK(sv == " tail");

    out.clear();
    sv = R"(""x)"sv;
    CHECK(strings::parse_escaped_quoted(sv, out));
    CHECK(out.empty());
    CHECK(sv == "x");

    out.clear();
    sv = "no quote"sv;
    CHECK_FALSE(strings::parse_escaped_quoted(sv, out));
    sv = R"("unterminated)"sv;
    CHECK_FALSE(strings::parse_escaped_quoted(sv, out));
    sv = R"("bad \q")"sv;
    CHECK_FALSE(strings::parse_escaped_quoted(sv, out));
  }
}

#pragma endregion

// NOLINTEND(readability-function-size)
// NOLINTEND(readability-function-cognitive-complexity)
