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
#include <charconv>
#include <concepts>
#include <cstddef>
#include <limits>
#include <system_error>
#include <type_traits>

namespace corvid::strings { inline namespace charconv {

// Character conversion
//
// Generic replacements/wrappers for `std::from_chars` and `std::to_chars`,
// templated on the character (code-unit) type as well as the number type, so
// they work with `char`, `char8_t`, `char16_t`, `char32_t`, and `wchar_t`
// alike.
//
// The integer functions (`int_from_chars`, `int_to_chars`) own the algorithm:
// parsing and formatting integers is plain digit arithmetic that is correct on
// any code-unit type, since the digit and sign characters all lie in the ASCII
// range. They follow `std::from_chars`/`std::to_chars` semantics: no leading
// whitespace, no '+' sign, no "0x" prefix, base 2 through 36, and the same
// `{ptr, ec}` reporting (`invalid_argument` when no digits parse,
// `result_out_of_range` on overflow, `value_too_large` when the output buffer
// is too small).
//
// The floating-point functions (`float_from_chars`, `float_to_chars`) wrap the
// standard `char` versions. Single-byte code units (`char`, `char8_t`) are
// reinterpreted and forwarded directly; a code unit wider than a byte
// transcodes through a fixed `char` buffer. Because the numeric text is
// entirely ASCII, the transcode is a byte-for-byte widening or narrowing, so a
// parsed `ptr` maps straight back into the caller's range.

#pragma region Results

// Mirror of `std::from_chars_result`, but templated on the code-unit type.
template<typename CharT>
struct from_chars_result {
  const CharT* ptr;
  std::errc ec;
};

// Mirror of `std::to_chars_result`, but templated on the code-unit type.
template<typename CharT>
struct to_chars_result {
  CharT* ptr;
  std::errc ec;
};

#pragma endregion
#pragma region Digit helpers

// Value of a base-36 digit character, or -1 if `c` is not a digit. Accepts
// '0'-'9', 'a'-'z', and 'A'-'Z'; the caller rejects values that are not below
// its base.
template<typename CharT>
[[nodiscard]] constexpr int digit_value(CharT c) noexcept {
  if (c >= CharT('0') && c <= CharT('9'))
    return static_cast<int>(c - CharT('0'));
  if (c >= CharT('a') && c <= CharT('z'))
    return static_cast<int>(c - CharT('a')) + 10;
  if (c >= CharT('A') && c <= CharT('Z'))
    return static_cast<int>(c - CharT('A')) + 10;
  return -1;
}

// Character for a base-36 digit value in [0, 36), using lowercase letters
// above 9, matching `std::to_chars`.
template<typename CharT>
[[nodiscard]] constexpr CharT to_digit_char(int d) noexcept {
  return d < 10 ? static_cast<CharT>(CharT('0') + d)
                : static_cast<CharT>(CharT('a') + (d - 10));
}

#pragma endregion
#pragma region int_from_chars

// Parse an integer from `[first, last)`, base 2 through 36, into `value`.
template<typename CharT, std::integral T>
requires(!std::same_as<T, bool>)
[[nodiscard]] constexpr from_chars_result<CharT> int_from_chars(
    const CharT* first, const CharT* last, T& value, int base = 10) noexcept {
  using U = std::make_unsigned_t<T>;
  const CharT* p = first;

  bool negative = false;
  if constexpr (std::is_signed_v<T>)
    if (p != last && *p == CharT('-')) {
      negative = true;
      ++p;
    }

  // Largest magnitude representable: `max()` for positive, one more for the
  // most-negative signed value.
  U limit = static_cast<U>(std::numeric_limits<T>::max());
  if constexpr (std::is_signed_v<T>)
    if (negative) limit = static_cast<U>(limit + 1);

  const U ubase = static_cast<U>(base);
  U acc = 0;
  bool any = false;
  bool overflow = false;
  for (; p != last; ++p) {
    const int d = digit_value(*p);
    if (d < 0 || d >= base) break;
    any = true;
    if (acc > (limit - static_cast<U>(d)) / ubase)
      overflow = true;
    else
      acc = static_cast<U>((acc * ubase) + static_cast<U>(d));
  }

  if (!any) return {first, std::errc::invalid_argument};
  if (overflow) return {p, std::errc::result_out_of_range};

  if constexpr (std::is_signed_v<T>)
    value = negative ? static_cast<T>(static_cast<U>(0) - acc)
                     : static_cast<T>(acc);
  else
    value = static_cast<T>(acc);
  return {p, std::errc{}};
}

#pragma endregion
#pragma region int_to_chars

// Format an integer `value` into `[first, last)`, base 2 through 36. Writes no
// sign for non-negative values and no "0x" prefix.
template<typename CharT, std::integral T>
requires(!std::same_as<T, bool>)
[[nodiscard]] constexpr to_chars_result<CharT>
int_to_chars(CharT* first, CharT* last, T value, int base = 10) noexcept {
  using U = std::make_unsigned_t<T>;
  bool negative = false;
  U mag = static_cast<U>(value);
  if constexpr (std::is_signed_v<T>)
    if (value < 0) {
      negative = true;
      mag = static_cast<U>(static_cast<U>(0) - mag);
    }

  // Emit least-significant digit first into a scratch buffer; the widest case
  // is base 2, which needs one digit per bit.
  CharT digits[std::numeric_limits<U>::digits];
  int n = 0;
  const U ubase = static_cast<U>(base);
  do {
    digits[n++] = to_digit_char<CharT>(static_cast<int>(mag % ubase));
    mag = static_cast<U>(mag / ubase);
  } while (mag != 0);

  const std::size_t total = static_cast<std::size_t>(n) + (negative ? 1U : 0U);
  if (static_cast<std::size_t>(last - first) < total)
    return {last, std::errc::value_too_large};

  CharT* out = first;
  if (negative) *out++ = CharT('-');
  while (n > 0) *out++ = digits[--n];
  return {out, std::errc{}};
}

#pragma endregion
#pragma region Float buffer

// Scratch size for transcoding floating-point text between `char` and a wider
// code unit. Ample for any `general`- or `scientific`-format double or long
// double.
inline constexpr std::size_t float_buffer_size = 128;

// Upper bound on `precision` for `float_to_chars`, leaving room for the sign,
// leading digit, radix point, and exponent within `float_buffer_size`. A
// larger request is clipped to this; the buffer cannot hold more anyway.
inline constexpr int max_float_precision =
    static_cast<int>(float_buffer_size) - 16;

#pragma endregion
#pragma region float_from_chars

// Parse a floating-point value from `[first, last)` into `value`, which may be
// the start of a number followed by other content. Single-byte code units
// (`char`, `char8_t`) are reinterpreted and forwarded to `std` directly. A
// wider code unit is narrowed through a `char` buffer; a non-ASCII code unit
// ends the number, and a number whose text is longer than `float_buffer_size`
// is truncated.
template<typename CharT, std::floating_point T>
[[nodiscard]] from_chars_result<CharT>
float_from_chars(const CharT* first, const CharT* last, T& value,
    std::chars_format fmt = std::chars_format::general) noexcept {
  if constexpr (sizeof(CharT) == 1) {
    const auto* f = reinterpret_cast<const char*>(first);
    const auto r =
        std::from_chars(f, reinterpret_cast<const char*>(last), value, fmt);
    return {first + (r.ptr - f), r.ec};
  } else {
    char buf[float_buffer_size];
    std::size_t n = 0;
    for (const CharT* p = first; p != last && n < float_buffer_size; ++p) {
      const auto u = static_cast<std::make_unsigned_t<CharT>>(*p);
      if (u > 0x7F) break;
      buf[n++] = static_cast<char>(u);
    }
    const auto r = std::from_chars(buf, buf + n, value, fmt);
    return {first + (r.ptr - buf), r.ec};
  }
}

#pragma endregion
#pragma region float_to_chars

// Format a floating-point `value` into `[first, last)`. A `precision` of -1
// selects the shortest round-trippable form; otherwise it is clipped to
// `max_float_precision`. Single-byte code units (`char`, `char8_t`) are
// reinterpreted and forwarded to `std` directly; a wider code unit is
// formatted as `char` and widened.
template<typename CharT, std::floating_point T>
[[nodiscard]] to_chars_result<CharT> float_to_chars(CharT* first, CharT* last,
    T value, std::chars_format fmt = std::chars_format::general,
    int precision = -1) noexcept {
  precision = std::min(precision, max_float_precision);

  if constexpr (sizeof(CharT) == 1) {
    auto* f = reinterpret_cast<char*>(first);
    auto* l = reinterpret_cast<char*>(last);
    const auto r =
        (precision < 0)
            ? std::to_chars(f, l, value, fmt)
            : std::to_chars(f, l, value, fmt, precision);
    return {first + (r.ptr - f), r.ec};
  } else {
    char buf[float_buffer_size];
    const auto r =
        (precision < 0)
            ? std::to_chars(buf, buf + float_buffer_size, value, fmt)
            : std::to_chars(buf, buf + float_buffer_size, value, fmt,
                  precision);
    if (r.ec != std::errc{}) return {first, r.ec};
    const auto n = static_cast<std::size_t>(r.ptr - buf);
    if (static_cast<std::size_t>(last - first) < n)
      return {last, std::errc::value_too_large};
    CharT* out = first;
    for (std::size_t i = 0; i < n; ++i)
      *out++ = static_cast<CharT>(static_cast<unsigned char>(buf[i]));
    return {out, std::errc{}};
  }
}

#pragma endregion

}} // namespace corvid::strings::charconv
