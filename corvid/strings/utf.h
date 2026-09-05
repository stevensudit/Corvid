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
#include <span>
#include <string_view>

#include "unicode_tables.h"

// UTF-8 decoding and the Unicode classes a BPE pre-tokenizer splits on.
//
// The decoder iterates code points over a byte string; it does not
// transcode. The classifier answers the three questions GPT-2's split rule
// asks of a code point (letter? number? white space?) by searching the
// generated range tables in "unicode_tables.h". Both are sized to that
// consumer; transcoding and the remaining general categories wait for one.
namespace corvid::strings::unicode {

// A decoded code point and the byte length of its UTF-8 encoding.
struct code_point {
  char32_t value{};
  size_t length{};

  constexpr auto operator<=>(const code_point&) const = default;

  // Decode the code point whose encoding starts at `ndx` in `input`.
  //
  // Returns true and sets `value` and `length` on success. Returns false,
  // with both reset to zero, otherwise.
  [[nodiscard]] constexpr bool
  decode(std::u8string_view input, size_t ndx) noexcept {
    value = {};
    length = 0;
    if (ndx >= input.size()) return false;

    const auto lead = input[ndx];
    const auto ones = std::countl_one(static_cast<uint8_t>(lead));
    if (ones == 1 || ones > 4) return false;

    const auto len = static_cast<size_t>(std::max(ones, 1));
    if (input.size() - ndx < len) return false;

    uint32_t bits = lead & (0xFFU >> (ones + 1));
    for (size_t i = 1; i < len; ++i) {
      const auto cont = input[ndx + i];
      if ((cont & 0xC0U) != 0x80U) return false;
      bits = (bits << 6) | (cont & 0x3FU);
    }

    // A value that fits in fewer bytes is overlong; the rest of the range
    // rules out UTF-16 surrogates and anything past the last plane.
    constexpr std::array<uint32_t, 5> min_for_length{0, 0, 0x80, 0x800,
        0x10000};
    if (bits < min_for_length[len]) return false;
    if ((bits >= 0xD800 && bits <= 0xDFFF) || bits > 0x10FFFF) return false;

    value = static_cast<char32_t>(bits);
    length = len;
    return true;
  }
};

// Whether `cp` falls within `ranges`, a sorted list of disjoint ranges.
[[nodiscard]] constexpr bool
in_ranges(std::span<const codepoint_range> ranges, char32_t cp) noexcept {
  // The only range that can hold `cp` is the last one starting at or before
  // it, which sits just before the first one starting after it.
  const auto after =
      std::ranges::upper_bound(ranges, cp, {}, &codepoint_range::first);
  return (after != ranges.begin()) && (cp <= (after - 1)->last);
}

// General category L: Lu, Ll, Lt, Lm, Lo.
[[nodiscard]] constexpr bool is_letter(char32_t cp) noexcept {
  return in_ranges(letter_ranges, cp);
}

// General category N: Nd, Nl, No.
[[nodiscard]] constexpr bool is_number(char32_t cp) noexcept {
  return in_ranges(number_ranges, cp);
}

// The White_Space property, which is what a regex backslash-s matches.
[[nodiscard]] constexpr bool is_white_space(char32_t cp) noexcept {
  return in_ranges(white_space_ranges, cp);
}

// The class of a code point under a BPE pre-tokenizer's split rule.
//
// The three named classes are disjoint, and `other` is everything else:
// punctuation, symbols, marks, controls, format characters, and unassigned
// code points.
enum class code_point_class : uint8_t { other, letter, number, white_space };

// Classify `cp` under a BPE pre-tokenizer's split rule.
[[nodiscard]] constexpr code_point_class classify(char32_t cp) noexcept {
  if (is_letter(cp)) return code_point_class::letter;
  if (is_number(cp)) return code_point_class::number;
  if (is_white_space(cp)) return code_point_class::white_space;
  return code_point_class::other;
}

} // namespace corvid::strings::unicode
