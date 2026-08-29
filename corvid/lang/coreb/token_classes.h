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
#include <cstdint>
#include <string_view>

#include "../../strings/cases.h"

namespace corvid { inline namespace lang { namespace coreb {

// Shared symbol token classes.
//
// Symbols divide into two disjoint token classes, in Hall and Monty alike
// (see "coreb.md"): word symbols and operator symbols, with the `%` kernel
// prefix admitting a word symbol after it. This header is the one authority
// both front ends classify against, so a symbol either syntax produces is a
// symbol the other can read; it also carries each operator's grammar
// classification and the literal and keyword word lists the grammars share.
//
//   is_word_symbol("nil?")            // true
//   find_operator("<=")->chains       // true
//   is_symbol_spelling("%if")         // true
//   is_symbol_spelling("comb-over")   // false: blends do not exist
//   is_literal_word("nil")            // true: reads as a value, not a name

#pragma region token_classes

// An operator's grammar family: the two arithmetic families that fold
// infix, the comparisons, and the statement spellings (`=` and `:=`),
// which have no expression grammar at all, though like every operator
// they mention as values.
enum class operator_kind : std::uint8_t {
  additive,
  multiplicative,
  comparison,
  statement
};

// Whether `kind` is an arithmetic family.
[[nodiscard]] constexpr bool is_arithmetic(operator_kind kind) noexcept {
  return kind == operator_kind::additive ||
         kind == operator_kind::multiplicative;
}

// One operator: its spelling, its grammar family, and whether comparison
// chains may repeat it.
struct operator_symbol final {
  std::string_view spelling;
  operator_kind kind;
  bool chains{};
};

// The closed operator table, longest spellings first so a scanner may take
// the first match.
inline constexpr operator_symbol operator_table[]{
    {"==", operator_kind::comparison, true},
    {"!=", operator_kind::comparison, false},
    {"<=", operator_kind::comparison, true},
    {">=", operator_kind::comparison, true},
    {":=", operator_kind::statement, false},
    {"+", operator_kind::additive, false},
    {"-", operator_kind::additive, false},
    {"*", operator_kind::multiplicative, false},
    {"/", operator_kind::multiplicative, false},
    {"<", operator_kind::comparison, true},
    {">", operator_kind::comparison, true},
    {"=", operator_kind::statement, false},
};

// The table entry for `t`, or null when `t` is not an operator spelling.
[[nodiscard]] constexpr const operator_symbol* find_operator(
    std::string_view t) noexcept {
  for (const auto& op : operator_table)
    if (op.spelling == t) return &op;
  return nullptr;
}

// Whether `t` spells an operator symbol from the closed table.
[[nodiscard]] constexpr bool is_operator_symbol(std::string_view t) noexcept {
  return find_operator(t) != nullptr;
}

// The literal words, which read as values rather than symbols and so can
// never name a binding.
inline constexpr std::string_view literal_words[]{"nil", "true", "false"};

// Whether `t` spells a literal word.
[[nodiscard]] constexpr bool is_literal_word(std::string_view t) noexcept {
  return std::ranges::contains(literal_words, t);
}

// Monty's contextual keywords, claimed by statement or clause position
// only; everywhere else the same spellings are ordinary words.
inline constexpr std::string_view contextual_keywords[]{"fun", "if", "return",
    "elif", "else"};

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
// NOLINTNEXTLINE(bugprone-exception-escape): substr follows an empty check.
[[nodiscard]] constexpr bool is_word_symbol(std::string_view t) noexcept {
  if (t.empty() || !is_word_lead(t.front())) return false;
  auto rest = t.substr(1);
  if (rest.ends_with('?')) rest.remove_suffix(1);
  return std::ranges::all_of(rest, is_word_char);
}

// Whether `t` names a binding a Monty definition may create: a word symbol
// that is not a literal word. Operator symbols bind through their mention
// spelling instead, and Hall places no word requirement at all.
[[nodiscard]] constexpr bool is_bindable_word(std::string_view t) noexcept {
  return is_word_symbol(t) && !is_literal_word(t);
}

// Whether `t` is a valid symbol spelling: a word symbol, an operator
// symbol, or the `%` kernel prefix on a word symbol.
// NOLINTNEXTLINE(bugprone-exception-escape): substr follows a prefix check.
[[nodiscard]] constexpr bool is_symbol_spelling(std::string_view t) noexcept {
  if (t.starts_with('%')) return is_word_symbol(t.substr(1));
  return is_word_symbol(t) || is_operator_symbol(t);
}

#pragma endregion

}}} // namespace corvid::lang::coreb
