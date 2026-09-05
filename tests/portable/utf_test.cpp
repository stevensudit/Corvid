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
#include <cstddef>
#include <string_view>
#include <vector>

#include "corvid/strings/utf.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid::strings::unicode;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// Decode at `ndx`; a failed decode comes back as the empty `code_point`.
constexpr code_point decode_at(std::u8string_view s, size_t ndx = 0) {
  code_point cp;
  (void)cp.decode(s, ndx);
  return cp;
}

constexpr code_point bad{};

} // namespace

#pragma region Decode

TEST_CASE("Decode well-formed sequences", "[UtfTest]") {
  CHECK(decode_at(u8"A") == code_point{U'A', 1});
  CHECK(decode_at(u8"\x7F") == code_point{U'\x7F', 1});
  CHECK(decode_at(u8"\xC2\x80") == code_point{U'\u0080', 2});
  CHECK(decode_at(u8"\xC3\xA9") == code_point{U'\u00E9', 2});
  CHECK(decode_at(u8"\xDF\xBF") == code_point{U'\u07FF', 2});
  CHECK(decode_at(u8"\xE0\xA0\x80") == code_point{U'\u0800', 3});
  CHECK(decode_at(u8"\xE6\x97\xA5") == code_point{U'\u65E5', 3});
  CHECK(decode_at(u8"\xEF\xBF\xBF") == code_point{U'\uFFFF', 3});
  CHECK(decode_at(u8"\xF0\x90\x80\x80") == code_point{U'\U00010000', 4});
  CHECK(decode_at(u8"\xF0\x9F\x9A\x80") == code_point{U'\U0001F680', 4});
  CHECK(decode_at(u8"\xF4\x8F\xBF\xBF") == code_point{U'\U0010FFFF', 4});

  // The index selects the sequence; bytes after it are ignored.
  CHECK(decode_at(u8"x\xC3\xA9y", 1) == code_point{U'\u00E9', 2});
  CHECK(decode_at(u8"x\xC3\xA9y", 3) == code_point{U'y', 1});

  // Everything above is also usable at compile time.
  static_assert(decode_at(u8"\xE6\x97\xA5") == code_point{U'\u65E5', 3});
}

TEST_CASE("Decode rejects malformed input", "[UtfTest]") {
  // Past the end, including the empty string.
  CHECK(decode_at(u8"") == bad);
  CHECK(decode_at(u8"x", 1) == bad);
  CHECK(decode_at(u8"x", 100) == bad);

  // Lead bytes that are not leads: a stray continuation, and the five- and
  // six-byte forms of the original 1993 scheme.
  CHECK(decode_at(u8"\x80") == bad);
  CHECK(decode_at(u8"\xBF") == bad);
  CHECK(decode_at(u8"\xC3\xA9", 1) == bad);
  CHECK(decode_at(u8"\xF8\x88\x80\x80\x80") == bad);
  CHECK(decode_at(u8"\xFC\x84\x80\x80\x80\x80") == bad);
  CHECK(decode_at(u8"\xFF") == bad);

  // Truncated at every length.
  CHECK(decode_at(u8"\xC3") == bad);
  CHECK(decode_at(u8"\xE6\x97") == bad);
  CHECK(decode_at(u8"\xF0\x9F\x9A") == bad);

  // A non-continuation byte in every continuation position.
  CHECK(decode_at(u8"\xC3\x41") == bad);
  CHECK(decode_at(u8"\xE6\x41\xA5") == bad);
  CHECK(decode_at(u8"\xE6\x97\x41") == bad);
  CHECK(decode_at(u8"\xF0\x41\x9A\x80") == bad);
  CHECK(decode_at(u8"\xF0\x9F\x41\x80") == bad);
  CHECK(decode_at(u8"\xF0\x9F\x9A\x41") == bad);
  CHECK(decode_at(u8"\xC3\xC3\xA9") == bad);

  // Overlong encodings of '/' and of the largest value below each length's
  // minimum.
  CHECK(decode_at(u8"\xC0\xAF") == bad);
  CHECK(decode_at(u8"\xC1\xBF") == bad);
  CHECK(decode_at(u8"\xE0\x80\xAF") == bad);
  CHECK(decode_at(u8"\xE0\x9F\xBF") == bad);
  CHECK(decode_at(u8"\xF0\x80\x80\xAF") == bad);
  CHECK(decode_at(u8"\xF0\x8F\xBF\xBF") == bad);

  // UTF-16 surrogates, with their well-formed neighbors, and values past
  // U+10FFFF.
  CHECK(decode_at(u8"\xED\x9F\xBF") == code_point{U'\uD7FF', 3});
  CHECK(decode_at(u8"\xED\xA0\x80") == bad);
  CHECK(decode_at(u8"\xED\xBF\xBF") == bad);
  CHECK(decode_at(u8"\xEE\x80\x80") == code_point{U'\uE000', 3});
  CHECK(decode_at(u8"\xF4\x90\x80\x80") == bad);
  CHECK(decode_at(u8"\xF7\xBF\xBF\xBF") == bad);

  // The return value tracks `length`, and failure resets a previous decode.
  code_point cp;
  REQUIRE(cp.decode(u8"A", 0));
  CHECK(cp == code_point{U'A', 1});
  CHECK_FALSE(cp.decode(u8"A", 1));
  CHECK(cp == bad);
}

TEST_CASE("Decode iterates a string", "[UtfTest]") {
  // "naive" with a diaeresis, two CJK characters, a rocket.
  const auto text = u8"na\u00EFve \u65E5\u672C \U0001F680!"sv;
  std::vector<char32_t> values;
  std::vector<size_t> lengths;
  code_point cp;
  for (size_t ndx = 0; cp.decode(text, ndx); ndx += cp.length) {
    values.push_back(cp.value);
    lengths.push_back(cp.length);
  }
  CHECK(values == std::vector<char32_t>{U'n', U'a', U'\u00EF', U'v', U'e',
                      U' ', U'\u65E5', U'\u672C', U' ', U'\U0001F680', U'!'});
  CHECK(lengths == std::vector<size_t>{1, 1, 2, 1, 1, 1, 3, 3, 1, 4, 1});

  // The lengths tile the input exactly.
  size_t total = 0;
  for (const auto len : lengths) total += len;
  CHECK(total == text.size());
}

#pragma endregion Decode

#pragma region Classify

TEST_CASE("Classify letters", "[UtfTest]") {
  CHECK(is_letter(U'A'));
  CHECK(is_letter(U'z'));
  CHECK(is_letter(U'\u00E9'));     // e with acute, Ll
  CHECK(is_letter(U'\u00DF'));     // sharp s, Ll
  CHECK(is_letter(U'\u01C5'));     // Dz with caron, Lt
  CHECK(is_letter(U'\u02B0'));     // modifier letter small h, Lm
  CHECK(is_letter(U'\u03A9'));     // Greek capital omega, Lu
  CHECK(is_letter(U'\u0430'));     // Cyrillic small a, Ll
  CHECK(is_letter(U'\u05D0'));     // Hebrew alef, Lo
  CHECK(is_letter(U'\u65E5'));     // CJK, Lo
  CHECK(is_letter(U'\U00010400')); // Deseret capital long i, Lu

  CHECK_FALSE(is_letter(U'0'));
  CHECK_FALSE(is_letter(U' '));
  CHECK_FALSE(is_letter(U'!'));
  CHECK_FALSE(is_letter(U'\u0301'));     // combining acute, Mn
  CHECK_FALSE(is_letter(U'\U0001F680')); // rocket, So

  // Boundaries of the first table entry, U+0041 to U+005A.
  CHECK_FALSE(is_letter(U'@'));
  CHECK(is_letter(U'A'));
  CHECK(is_letter(U'Z'));
  CHECK_FALSE(is_letter(U'['));

  // Below the first entry and past the last.
  CHECK_FALSE(is_letter(U'\0'));
  CHECK_FALSE(is_letter(U'\U0010FFFF'));

  static_assert(is_letter(U'\u65E5'));
  static_assert(!is_letter(U'!'));
}

TEST_CASE("Classify numbers", "[UtfTest]") {
  CHECK(is_number(U'0'));
  CHECK(is_number(U'9'));
  CHECK(is_number(U'\u0660')); // Arabic-Indic zero, Nd
  CHECK(is_number(U'\u0967')); // Devanagari one, Nd
  CHECK(is_number(U'\u00B2')); // superscript two, No
  CHECK(is_number(U'\u00BD')); // one half, No
  CHECK(is_number(U'\u216B')); // Roman numeral twelve, Nl
  CHECK(is_number(U'\uFF11')); // fullwidth one, Nd

  CHECK_FALSE(is_number(U'/'));
  CHECK_FALSE(is_number(U':'));
  CHECK_FALSE(is_number(U'A'));
  CHECK_FALSE(is_number(U'#'));
  CHECK_FALSE(is_number(U'\u00B1')); // plus-minus, Sm
}

TEST_CASE("Classify white space", "[UtfTest]") {
  CHECK(is_white_space(U' '));
  CHECK(is_white_space(U'\t'));
  CHECK(is_white_space(U'\n'));
  CHECK(is_white_space(U'\r'));
  CHECK(is_white_space(U'\f'));
  CHECK(is_white_space(U'\v'));
  CHECK(is_white_space(U'\u0085')); // next line
  CHECK(is_white_space(U'\u00A0')); // no-break space
  CHECK(is_white_space(U'\u1680')); // Ogham space mark
  CHECK(is_white_space(U'\u2002')); // en space
  CHECK(is_white_space(U'\u200A')); // hair space
  CHECK(is_white_space(U'\u2028')); // line separator
  CHECK(is_white_space(U'\u202F')); // narrow no-break space
  CHECK(is_white_space(U'\u3000')); // ideographic space

  // Neighbors of the table entries, and the zero-width characters that a
  // regex backslash-s does not match.
  CHECK_FALSE(is_white_space(U'\x08'));
  CHECK_FALSE(is_white_space(U'\x0E'));
  CHECK_FALSE(is_white_space(U'\u200B')); // zero width space, Cf
  CHECK_FALSE(is_white_space(U'\u200D')); // zero width joiner, Cf
  CHECK_FALSE(is_white_space(U'\uFEFF')); // byte order mark, Cf
  CHECK_FALSE(is_white_space(U'a'));
}

TEST_CASE("Classify partitions code points", "[UtfTest]") {
  using enum code_point_class;
  CHECK(classify(U'A') == letter);
  CHECK(classify(U'\u65E5') == letter);
  CHECK(classify(U'7') == number);
  CHECK(classify(U'\u00B2') == number);
  CHECK(classify(U' ') == white_space);
  CHECK(classify(U'\u00A0') == white_space);
  CHECK(classify(U'!') == other);
  CHECK(classify(U'\'') == other);
  CHECK(classify(U'\u0301') == other);
  CHECK(classify(U'\u200D') == other);
  CHECK(classify(U'\U0001F680') == other);
  CHECK(classify(U'\uFFFD') == other);
  CHECK(classify(U'\0') == other);

  // The three named classes never overlap. Walk every white space code
  // point, and the boundaries of every letter and number range.
  for (const auto& r : white_space_ranges) {
    for (auto cp = r.first; cp <= r.last; ++cp) {
      CHECK_FALSE(is_letter(cp));
      CHECK_FALSE(is_number(cp));
    }
  }
  for (const auto& r : number_ranges) {
    CHECK_FALSE(is_letter(r.first));
    CHECK_FALSE(is_letter(r.last));
    CHECK_FALSE(is_white_space(r.first));
    CHECK_FALSE(is_white_space(r.last));
  }
  for (const auto& r : letter_ranges) {
    CHECK_FALSE(is_number(r.first));
    CHECK_FALSE(is_number(r.last));
    CHECK_FALSE(is_white_space(r.first));
    CHECK_FALSE(is_white_space(r.last));
  }
}

#pragma endregion Classify

// NOLINTEND(readability-function-cognitive-complexity)
