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
#include <string>

#include "../meta/concepts.h"
#include "string_literals.h"

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
[[nodiscard]] constexpr bool is_lc_hex_alpha(C ch) noexcept {
  return (ch >= C('a') && ch <= C('f'));
}

template<CharType C>
[[nodiscard]] constexpr bool is_uc_hex_alpha(C ch) noexcept {
  return (ch >= C('A') && ch <= C('F'));
}

[[nodiscard]] constexpr bool is_hex_digit(CharType auto ch) noexcept {
  return is_digit(ch) || is_lc_hex_alpha(ch) || is_uc_hex_alpha(ch);
}

// Whether `c` is ASCII whitespace: space, tab, or one of the newline,
// vertical-tab, form-feed, and carriage-return controls.
template<CharType C>
[[nodiscard]] constexpr bool is_space(C c) noexcept {
  return c == C(' ') || c == C('\t') || c == C('\n') || c == C('\v') ||
         c == C('\f') || c == C('\r');
}

#pragma endregion
#pragma region String predicates

// The string overloads of the character predicates: each is true when the
// string is non-empty and every code unit satisfies the corresponding
// character predicate.
//
// For the case predicates, this deviates from Python deliberately. Python's
// `islower` and `isupper` ignore uncased characters, so `"abc0".islower()` is
// true, while `is_lower("abc0")` here is false because '0' fails the character
// predicate. Beware that `!is_upper(s)` does not reproduce the Python
// semantics either: it is also true for mixed-case strings like "aA" and for
// letterless strings. Python's `islower` amounts to "contains at least one
// letter and no uppercase letters"; if you truly need that, combine
// `std::ranges::any_of` on `is_alpha` with `std::ranges::none_of` on
// `is_upper`.

template<StringViewLike S>
[[nodiscard]] constexpr bool is_lower(const S& s) noexcept {
  const auto sv{as_view(s)};
  return !sv.empty() && std::ranges::all_of(sv, [](CharType auto c) {
    return is_lower(c);
  });
}

template<StringViewLike S>
[[nodiscard]] constexpr bool is_upper(const S& s) noexcept {
  const auto sv{as_view(s)};
  return !sv.empty() && std::ranges::all_of(sv, [](CharType auto c) {
    return is_upper(c);
  });
}

template<StringViewLike S>
[[nodiscard]] constexpr bool is_alpha(const S& s) noexcept {
  const auto sv{as_view(s)};
  return !sv.empty() && std::ranges::all_of(sv, [](CharType auto c) {
    return is_alpha(c);
  });
}

template<StringViewLike S>
[[nodiscard]] constexpr bool is_digit(const S& s) noexcept {
  const auto sv{as_view(s)};
  return !sv.empty() && std::ranges::all_of(sv, [](CharType auto c) {
    return is_digit(c);
  });
}

template<StringViewLike S>
[[nodiscard]] constexpr bool is_alnum(const S& s) noexcept {
  const auto sv{as_view(s)};
  return !sv.empty() && std::ranges::all_of(sv, [](CharType auto c) {
    return is_alnum(c);
  });
}

template<StringViewLike S>
[[nodiscard]] constexpr bool is_hex_digit(const S& s) noexcept {
  const auto sv{as_view(s)};
  return !sv.empty() && std::ranges::all_of(sv, [](CharType auto c) {
    return is_hex_digit(c);
  });
}

template<StringViewLike S>
[[nodiscard]] constexpr bool is_space(const S& s) noexcept {
  const auto sv{as_view(s)};
  return !sv.empty() && std::ranges::all_of(sv, [](CharType auto c) {
    return is_space(c);
  });
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

// Swap the case of `c`, Python swapcase-style: lowercase becomes uppercase,
// uppercase becomes lowercase, everything else passes through.
template<CharType C>
[[nodiscard]] constexpr C to_swapped(C c) noexcept {
  if (is_lower(c)) return to_upper(c);
  if (is_upper(c)) return to_lower(c);
  return c;
}

// Swap case in place.
constexpr void to_swapped(Range auto& r) noexcept {
  for (auto& ch : r) ch = to_swapped(ch);
}

// Return with case swapped. Accepts any string-like argument and yields a
// `std::basic_string` of its code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_swapped(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_swapped(r);
  return r;
}

// Capitalize in place, Python capitalize-style: uppercase the first code unit
// and lowercase the rest.
constexpr void to_capitalized(Range auto& r) noexcept {
  bool first = true;
  for (auto& ch : r) {
    ch = first ? to_upper(ch) : to_lower(ch);
    first = false;
  }
}

// Return as capitalized. Accepts any string-like argument and yields a
// `std::basic_string` of its code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_capitalized(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_capitalized(r);
  return r;
}

// Title-case in place, Python title-style: uppercase each letter that follows
// a non-letter (or starts the string), lowercase the other letters.
//
// The Python quirk comes along: any non-letter starts a new word, so "they're"
// becomes "They'Re" and "3rd" becomes "3Rd".
constexpr void to_titled(Range auto& r) noexcept {
  bool prev_alpha = false;
  for (auto& ch : r) {
    if (is_alpha(ch)) ch = prev_alpha ? to_lower(ch) : to_upper(ch);
    prev_alpha = is_alpha(ch);
  }
}

// Return as title-cased. Accepts any string-like argument and yields a
// `std::basic_string` of its code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_titled(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_titled(r);
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
