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
#include <string_view>

#include "../../strings/cases.h"

namespace corvid { inline namespace lang { namespace coreb {

// Shared symbol token classes.
//
// Symbols divide into two disjoint token classes, in Hall and Monty alike
// (see "coreb.md"): word symbols and operator symbols, with the `%` kernel
// prefix admitting a word symbol after it. This header is the one authority
// both front ends classify against, so a symbol either syntax produces is a
// symbol the other can read.
//
//   is_word_symbol("nil?")         // true
//   is_operator_symbol("<=")       // true
//   is_symbol_spelling("%if")      // true
//   is_symbol_spelling("comb-over")  // false: blends do not exist

#pragma region token_classes

// The closed operator table, longest spellings first so a scanner may take
// the first match.
inline constexpr std::string_view operator_symbols[]{
    "==", "!=", "<=", ">=", ":=", "+", "-", "*", "/", "<", ">", "="};

// Whether `c` may lead a word symbol.
[[nodiscard]] constexpr bool is_word_lead(char c) noexcept {
  return strings::is_alpha(c) || c == '_';
}

// Whether `c` may continue a word symbol.
[[nodiscard]] constexpr bool is_word_char(char c) noexcept {
  return strings::is_alpha(c) || strings::is_digit(c) || c == '_';
}

// Whether `t` spells a word symbol: a leading letter or underscore, then
// word characters, with `?` permitted only as the final character.
[[nodiscard]] constexpr bool is_word_symbol(std::string_view t) noexcept {
  if (t.empty() || !is_word_lead(t.front())) return false;
  auto rest = t.substr(1);
  if (rest.ends_with('?')) rest.remove_suffix(1);
  return std::ranges::all_of(rest, is_word_char);
}

// Whether `t` spells an operator symbol from the closed table.
[[nodiscard]] constexpr bool is_operator_symbol(std::string_view t) noexcept {
  return std::ranges::contains(operator_symbols, t);
}

// Whether `t` is a valid symbol spelling: a word symbol, an operator
// symbol, or the `%` kernel prefix on a word symbol.
[[nodiscard]] constexpr bool is_symbol_spelling(std::string_view t) noexcept {
  if (t.starts_with('%')) return is_word_symbol(t.substr(1));
  return is_word_symbol(t) || is_operator_symbol(t);
}

#pragma endregion

}}} // namespace corvid::lang::coreb
