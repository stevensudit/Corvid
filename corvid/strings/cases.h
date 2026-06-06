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
#include "strings_shared.h"

namespace corvid::strings { inline namespace cases {

// Cases
//
// ASCII letter-case utilities: character predicates, case conversion, and
// case-insensitive comparison. The per-character predicates and `to_upper`/
// `to_lower` work on any code-unit type; the semantics stay deliberately
// ASCII-only and locale-independent, so only the 26 Latin letters are ever
// affected, whatever the encoding. This sidesteps the localization and Unicode
// complications (and the cost) of the `std::ctype` and `std::toupper`/
// `std::tolower` facilities.

#pragma region Character predicates

template<CharType C>
[[nodiscard]] constexpr bool is_lower(C c) noexcept {
  return c >= C('a') && c <= C('z');
}

template<CharType C>
[[nodiscard]] constexpr bool is_upper(C c) noexcept {
  return c >= C('A') && c <= C('Z');
}

[[nodiscard]] constexpr bool is_alpha(CharType auto c) noexcept {
  return is_lower(c) || is_upper(c);
}

template<CharType C>
[[nodiscard]] constexpr bool is_digit(C c) noexcept {
  return c >= C('0') && c <= C('9');
}

[[nodiscard]] constexpr bool is_alnum(CharType auto c) noexcept {
  return is_alpha(c) || is_digit(c);
}

template<CharType C>
[[nodiscard]] inline bool is_lc_hex_alpha(C ch) noexcept {
  return (ch >= C('a') && ch <= C('f'));
}

template<CharType C>
[[nodiscard]] inline bool is_uc_hex_alpha(C ch) noexcept {
  return (ch >= C('A') && ch <= C('F'));
}

[[nodiscard]] inline bool is_hex_digit(CharType auto ch) noexcept {
  return is_digit(ch) || is_lc_hex_alpha(ch) || is_uc_hex_alpha(ch);
}

#pragma endregion
#pragma region Case change

// Convert to uppercase.
// Avoids `std::toupper` because it's locale-dependent and slow.
template<CharType C>
[[nodiscard]] constexpr C to_upper(C c) noexcept {
  return is_lower(c) ? static_cast<C>(c - (C('a') - C('A'))) : c;
}

// Convert to uppercase in place.
constexpr void to_upper(Range auto& r) noexcept {
  for (auto& ch : r) ch = to_upper(ch);
}

// Return as uppercase. Accepts any string-like argument and yields a
// `std::basic_string` of its code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_upper(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_upper(r);
  return r;
}

// Convert to lowercase.
// Avoids `std::tolower` because it's locale-dependent and slow.
template<CharType C>
[[nodiscard]] constexpr C to_lower(C c) noexcept {
  return is_upper(c) ? static_cast<C>(c + (C('a') - C('A'))) : c;
}

// Convert to lowercase in place.
constexpr void to_lower(Range auto& r) noexcept {
  for (auto& ch : r) ch = to_lower(ch);
}

// Return as lowercase. Accepts any string-like argument and yields a
// `std::basic_string` of its code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_lower(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_lower(r);
  return r;
}

#pragma endregion
#pragma region ci_equal

// Compare case-insensitively. In many cases, it is better to store `as_lower`
// versions and compare those, particularly if one of the values is checked
// against repeatedly. Both arguments must be string-like with the same
// code-unit type.
template<StringViewLike A, StringViewLike B>
requires std::same_as<char_type_of_t<A>, char_type_of_t<B>>
[[nodiscard]] constexpr bool ci_equal(const A& a, const B& b) noexcept {
  const auto lhs = as_view(a);
  const auto rhs = as_view(b);
  if (lhs.size() != rhs.size()) return false;
  for (size_t i = 0; i < lhs.size(); ++i)
    if (to_lower(lhs[i]) != to_lower(rhs[i])) return false;
  return true;
}

#pragma endregion

}} // namespace corvid::strings::cases
