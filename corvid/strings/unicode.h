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
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "unicode_tables.h"

// UTF-8 decoding and Unicode character classification.
//
// The decoder reads one code point at a time from UTF-8 text, while the
// classifier answers membership questions for the classes tabulated in
// "unicode_tables.h".
namespace corvid::strings::unicode {

namespace utf {

// Decode the code point whose encoding starts at `ndx` in `text`.
//
// On success, returns the length of the encoding and sets `code_point`. On
// failure, returns 0, leaving `code_point` untouched.
[[nodiscard]] constexpr size_t decode(char32_t& code_point,
    std::u8string_view text, size_t ndx = 0) noexcept {
  if (ndx >= text.size()) return 0;
  const auto lead = text[ndx];
  const auto ones = std::countl_one(static_cast<uint8_t>(lead));
  if (ones == 1 || ones > 4) return 0;
  const auto len = static_cast<size_t>(std::max(ones, 1));
  if (text.size() - ndx < len) return 0;

  uint32_t bits = lead & (0xFFU >> (ones + 1));
  for (size_t i = 1; i < len; ++i) {
    const auto cont = text[ndx + i];
    if ((cont & 0xC0U) != 0x80U) return 0;
    bits = (bits << 6) | (cont & 0x3FU);
  }

  // A value that fits in fewer bytes is overlong; the rest of the range
  // rules out UTF-16 surrogates and anything past the last plane.
  constexpr std::array<uint32_t, 5> min_for_length{0, 0, 0x80, 0x800, 0x10000};
  if (bits < min_for_length[len]) return 0;
  if ((bits >= 0xD800 && bits <= 0xDFFF) || bits > 0x10FFFF) return 0;

  code_point = static_cast<char32_t>(bits);
  return len;
}

// Extract the code point at the front of `text`, consuming its encoding.
//
// On success, returns true and sets `code_point`, removing consumed bytes from
// `text`. On failure, returns false, leaving both untouched.
[[nodiscard]] constexpr bool
extract(char32_t& code_point, std::u8string_view& text) noexcept {
  const auto len = decode(code_point, text);
  if (len == 0) return false;
  text.remove_prefix(len);
  return true;
}

} // namespace utf
namespace classifier {

// The property bits of `cp`, as defined in "unicode_tables.h".
[[nodiscard]] constexpr uint8_t properties(char32_t cp) noexcept {
  if (cp > max_code_point) return 0;
  constexpr auto page_mask = (char32_t{1} << page_shift) - 1;
  return property_pages[property_index[cp >> page_shift]][cp & page_mask];
}

// General category L: Lu, Ll, Lt, Lm, Lo.
[[nodiscard]] constexpr bool is_letter(char32_t cp) noexcept {
  return (properties(cp) & letter_bit);
}

// General category N: Nd, Nl, No.
[[nodiscard]] constexpr bool is_number(char32_t cp) noexcept {
  return (properties(cp) & number_bit);
}

// The White_Space property, which is what a regex backslash-s matches.
[[nodiscard]] constexpr bool is_white_space(char32_t cp) noexcept {
  return (properties(cp) & white_space_bit);
}

// The tabulated class a code point belongs to, or `other` for none.
//
// The three named classes are disjoint. `other` covers punctuation,
// symbols, marks, controls, format characters, and unassigned code points.
enum class code_point_class : uint8_t { other, letter, number, white_space };

// Classify `cp`.
[[nodiscard]] constexpr code_point_class classify(char32_t cp) noexcept {
  const auto bits = properties(cp);
  if (bits & letter_bit) return code_point_class::letter;
  if (bits & number_bit) return code_point_class::number;
  if (bits & white_space_bit) return code_point_class::white_space;
  return code_point_class::other;
}

} // namespace classifier

} // namespace corvid::strings::unicode
