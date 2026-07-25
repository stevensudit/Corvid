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
#include <compare>
#include <string>
#include <type_traits>

#include "../meta/concepts.h"
#include "string_literals.h"

namespace corvid::strings { inline namespace cases {

// Cases
//
// ASCII letter-case utilities: character predicates, case conversion, and
// case-insensitive comparison. The per-character predicates and `as_upper`/
// `as_lower` work on any code-unit type; the semantics stay deliberately
// ASCII-only and locale-independent, so only the 26 Latin letters are ever
// affected, whatever the encoding. This sidesteps the localization and Unicode
// complications (and the cost) of the `std::ctype` and `std::toupper`/
// `std::tolower` facilities.

#pragma region Predicates

// Character-classification predicates.
//
// Each `is_*` name here is a constexpr predicate object with two forms: pass
// a code unit to test it directly, or pass anything string-like to test that
// the string is non-empty and every code unit passes. Because the names are
// objects rather than overload sets, they can also be passed directly to
// algorithms and range adaptors, e.g. `std::views::filter(is_digit)`.
//
// For the case predicates, the string form deviates from Python deliberately.
// Python's `islower` and `isupper` ignore uncased characters, so
// `"abc0".islower()` is true, while `is_lower("abc0")` here is false because
// '0' fails the character predicate. Beware that `!is_upper(s)` does not
// reproduce the Python semantics either: it is also true for mixed-case
// strings like "aA" and for letterless strings. The Python rule is available
// by name as `is_python_lower` and `is_python_upper`.

// Whether `ch` is a lowercase hex letter, 'a' through 'f'.
template<CharType CharT>
[[nodiscard]] constexpr bool is_lc_hex_alpha(CharT ch) noexcept {
  return (ch >= CharT{'a'} && ch <= CharT{'f'});
}

// Whether `ch` is an uppercase hex letter, 'A' through 'F'.
template<CharType CharT>
[[nodiscard]] constexpr bool is_uc_hex_alpha(CharT ch) noexcept {
  return (ch >= CharT{'A'} && ch <= CharT{'F'});
}

namespace details {

// Extend a per-code-unit predicate to whole strings.
//
// The code-unit form applies `CharPred` directly; the string form is true
// when the string is non-empty and every code unit passes.
template<typename CharPred>
struct code_unit_pred {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    return CharPred{}(c);
  }
  template<StringViewLike S>
  [[nodiscard]] constexpr bool operator()(const S& s) const noexcept {
    const auto sv = as_view(s);
    return !sv.empty() && std::ranges::all_of(sv, CharPred{});
  }
};

struct lower_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    using C = decltype(c);
    return c >= C{'a'} && c <= C{'z'};
  }
};

struct upper_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    using C = decltype(c);
    return c >= C{'A'} && c <= C{'Z'};
  }
};

struct alpha_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    return lower_char{}(c) || upper_char{}(c);
  }
};

struct digit_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    using C = decltype(c);
    return c >= C{'0'} && c <= C{'9'};
  }
};

struct alnum_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    return alpha_char{}(c) || digit_char{}(c);
  }
};

struct hex_digit_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    return digit_char{}(c) || is_lc_hex_alpha(c) || is_uc_hex_alpha(c);
  }
};

struct space_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    using C = decltype(c);
    return c == C{' '} || c == C{'\t'} || c == C{'\n'} || c == C{'\v'} ||
           c == C{'\f'} || c == C{'\r'};
  }
};

struct ascii_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    return static_cast<std::make_unsigned_t<decltype(c)>>(c) <= 0x7f;
  }
};

struct printable_char {
  [[nodiscard]] constexpr bool operator()(CharType auto c) const noexcept {
    using C = decltype(c);
    return c >= C{' '} && c <= C{'~'};
  }
};

} // namespace details

// Whether lowercase: 'a' through 'z'.
inline constexpr details::code_unit_pred<details::lower_char> is_lower{};

// Whether uppercase: 'A' through 'Z'.
inline constexpr details::code_unit_pred<details::upper_char> is_upper{};

// Whether a Latin letter.
inline constexpr details::code_unit_pred<details::alpha_char> is_alpha{};

// Whether a decimal digit, meaning '0' through '9' only.
//
// Not the Python `isdigit`, which also admits non-ASCII digits like
// superscripts; within ASCII, Python's `isdecimal`, `isdigit`, and `isnumeric`
// all collapse to this.
inline constexpr details::code_unit_pred<details::digit_char> is_digit{};

// Whether a Latin letter or decimal digit.
inline constexpr details::code_unit_pred<details::alnum_char> is_alnum{};

// Whether a hex digit, in either case.
inline constexpr details::code_unit_pred<details::hex_digit_char>
    is_hex_digit{};

// Whether ASCII whitespace: space, tab, or one of the newline, vertical-tab,
// form-feed, and carriage-return controls.
inline constexpr details::code_unit_pred<details::space_char> is_space{};

// Whether in the ASCII range, 0 through 0x7f.
//
// Unlike Python's `isascii`, which is true for the empty string, the string
// form follows the non-empty rule like the others.
inline constexpr details::code_unit_pred<details::ascii_char> is_ascii{};

// Whether a printable ASCII character, space (0x20) through tilde (0x7e).
inline constexpr details::code_unit_pred<details::printable_char>
    is_printable{};

// The Python-rule case predicates.
//
// Each is true when the string contains at least one letter and none of the
// opposite case, ignoring non-alphabetical characters exactly as Python
// `islower` and `isupper` do.
//
// So `is_python_lower("abc0")` is true, unlike `is_lower("abc0")`, while empty
// and letterless strings are false for both.

template<StringViewLike S>
[[nodiscard]] constexpr bool is_python_lower(const S& s) noexcept {
  bool has_letter = false;
  for (const auto c : as_view(s)) {
    if (is_upper(c)) return false;
    if (is_lower(c)) has_letter = true;
  }
  return has_letter;
}

template<StringViewLike S>
[[nodiscard]] constexpr bool is_python_upper(const S& s) noexcept {
  bool has_letter = false;
  for (const auto c : as_view(s)) {
    if (is_lower(c)) return false;
    if (is_upper(c)) has_letter = true;
  }
  return has_letter;
}

// Whether `s` is title-cased, Python `istitle`-style.
//
// True when the string contains at least one letter, every letter starting a
// word (following a non-letter, or the string start) is uppercase, and every
// other letter is lowercase. Words break exactly as in `as_titled`, quirk
// included, so `is_title(as_titled(s))` holds whenever `s` has a letter.
template<StringViewLike S>
[[nodiscard]] constexpr bool is_title(const S& s) noexcept {
  bool prev_alpha = false;
  bool has_letter = false;
  for (const auto c : as_view(s)) {
    if (is_alpha(c)) {
      if (prev_alpha ? is_upper(c) : is_lower(c)) return false;
      has_letter = true;
    }
    prev_alpha = is_alpha(c);
  }
  return has_letter;
}

#pragma endregion
#pragma region Case change

// Return `c` as uppercase.
//
// Avoids `std::toupper` because it's locale-dependent and slow.
template<CharType CharT>
[[nodiscard]] constexpr CharT as_upper(CharT c) noexcept {
  return is_lower(c) ? static_cast<CharT>(c - (CharT{'a'} - CharT{'A'})) : c;
}

// Convert to uppercase in place.
constexpr void to_upper(Range auto& r) noexcept {
  for (auto& ch : r) ch = as_upper(ch);
}

// Return as uppercase.
//
// Accepts any string-like argument and yields a `std::basic_string` of its
// code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_upper(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_upper(r);
  return r;
}

// Return `c` as lowercase.
//
// Avoids `std::tolower` because it's locale-dependent and slow.
template<CharType CharT>
[[nodiscard]] constexpr CharT as_lower(CharT c) noexcept {
  return is_upper(c) ? static_cast<CharT>(c + (CharT{'a'} - CharT{'A'})) : c;
}

// Convert to lowercase in place.
constexpr void to_lower(Range auto& r) noexcept {
  for (auto& ch : r) ch = as_lower(ch);
}

// Return as lowercase.
//
// Accepts any string-like argument and yields a `std::basic_string` of its
// code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_lower(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_lower(r);
  return r;
}

// Return `c` with its case swapped, Python `swapcase`-style.
//
// Lowercase becomes uppercase, uppercase becomes lowercase, everything else
// passes through.
template<CharType CharT>
[[nodiscard]] constexpr CharT as_swapped(CharT c) noexcept {
  if (is_lower(c)) return as_upper(c);
  if (is_upper(c)) return as_lower(c);
  return c;
}

// Swap case in place.
constexpr void to_swapped(Range auto& r) noexcept {
  for (auto& ch : r) ch = as_swapped(ch);
}

// Return with case swapped.
//
// Accepts any string-like argument and yields a `std::basic_string` of its
// code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_swapped(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_swapped(r);
  return r;
}

// Capitalize in place, Python `capitalize`-style.
//
// Uppercases the first code unit and lowercases the rest.
constexpr void to_capitalized(Range auto& r) noexcept {
  bool first = true;
  for (auto& ch : r) {
    ch = first ? as_upper(ch) : as_lower(ch);
    first = false;
  }
}

// Return as capitalized.
//
// Accepts any string-like argument and yields a `std::basic_string` of its
// code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_capitalized(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_capitalized(r);
  return r;
}

// Title-case in place, Python `title`-style.
//
// Uppercase each letter that follows a non-letter (or starts the string),
// lowercase the other letters.
//
// The Python quirk comes along: any non-letter starts a new word, so "they're"
// becomes "They'Re" and "3rd" becomes "3Rd".
constexpr void to_titled(Range auto& r) noexcept {
  bool prev_alpha = false;
  for (auto& ch : r) {
    if (is_alpha(ch)) ch = prev_alpha ? as_lower(ch) : as_upper(ch);
    prev_alpha = is_alpha(ch);
  }
}

// Return as title-cased.
//
// Accepts any string-like argument and yields a `std::basic_string` of its
// code-unit type.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_titled(const S& s) {
  std::basic_string<char_type_of_t<S>> r{as_view(s)};
  to_titled(r);
  return r;
}

#pragma endregion
#pragma region Case-insensitive comparison

// Compare case-insensitively.
//
// In many cases, it is better to store `as_lower` versions and compare those,
// particularly if one of the values is checked against repeatedly. Both
// arguments must be string-like with the same code-unit type.
template<StringViewLike A, StringViewLike B>
requires std::same_as<char_type_of_t<A>, char_type_of_t<B>>
[[nodiscard]] constexpr bool ci_equal(const A& a, const B& b) noexcept {
  const auto lhs = as_view(a);
  const auto rhs = as_view(b);
  if (lhs.size() != rhs.size()) return false;
  for (size_t i = 0; i < lhs.size(); ++i)
    if (as_lower(lhs[i]) != as_lower(rhs[i])) return false;
  return true;
}

// Compare case-insensitively, ordering as if both sides were lowercased.
//
// See above about using `as_lower` instead.
template<StringViewLike A, StringViewLike B>
requires std::same_as<char_type_of_t<A>, char_type_of_t<B>>
[[nodiscard]] constexpr std::weak_ordering
ci_compare(const A& a, const B& b) noexcept {
  const auto lhs = as_view(a);
  const auto rhs = as_view(b);
  const auto n = std::min(lhs.size(), rhs.size());
  for (size_t i = 0; i < n; ++i) {
    const auto l = as_lower(lhs[i]);
    const auto r = as_lower(rhs[i]);
    if (l != r)
      return (l < r) ? std::weak_ordering::less : std::weak_ordering::greater;
  }
  return lhs.size() <=> rhs.size();
}

#pragma endregion

}} // namespace corvid::strings::cases
