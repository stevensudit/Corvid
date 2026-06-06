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
#include <cstdint>
#include <span>

#include "strings_shared.h"
#include "cases.h"
#include "charconv_wrapper.h"
#include "trimming.h"

namespace corvid::strings { inline namespace conversion {

// Conversions
//
// Conversions between strings and other representations, organized into nested
// inline namespaces:
//
// - `cvt_int`: parse, extract, and format integral (and bool) values.
// - `cvt_float`: parse, extract, and format floating-point values.
// - `cvt_bytes`: reinterpret raw bytes between `std::string_view` and spans.
// - `cvt_stream`: append user types that opt in via `operator<<`.
//
// Within `cvt_int` and `cvt_float`, the `extract_num`, `parse_num`,
// `append_num`, and `num_as_string` names are shared and selected by overload.

#pragma region cvt_int

inline namespace cvt_int {

// To int.

// Extract integer out of a `std::string_view`, setting output parameter.
//
// Skips leading whitespace, accepts leading minus sign, and does not accept
// "0x" or "0X", even when `base` is 16. (This is true for all of these
// related functions.)
//
// On success, sets output value, removes parsed characters from the string
// view, and returns true.
//
// On failure, leaves the parameters unchanged and returns false.
template<int base = 10, typename C>
constexpr bool
extract_num(std::integral auto& t, std::basic_string_view<C>& sv) {
  const auto save_sv = sv;
  sv = trim_left(sv);
  auto [ptr, ec] = int_from_chars(sv.data(), sv.data() + sv.size(), t, base);
  sv.remove_prefix(ptr - sv.data());
  if (ec == std::errc{}) return true;
  sv = save_sv;
  return false;
}

// Extract integer from a `std::string_view`, returning it as
// `std::optional`.
//
// On success, returns optional with value, and removes parsed characters
// from the string view.
//
// On failure, returns optional without value and leaves string view unchanged.
template<std::integral T = int64_t, int base = 10, typename C>
constexpr std::optional<T> extract_num(std::basic_string_view<C>& sv) {
  T t;
  return extract_num<base>(t, sv) ? std::make_optional(t) : std::nullopt;
}

// Parse integer from copy of a `std::string_view`, returning it as
// `std::optional`. Fails if there are any unparsed characters.
//
// On success, returns optional with value.
//
// On failure, returns optional without value.
template<std::integral T = int64_t, int base = 10, StringViewLike S>
constexpr std::optional<T> parse_num(const S& s) {
  auto sv = as_view(s);
  T t;
  return extract_num<base>(t, sv) && sv.empty()
             ? std::make_optional(t)
             : std::nullopt;
}

// Parse integer from copy of a `std::string_view`, with a `default_value`.
// Fails if there are any unparsed characters.
//
// On success, returns parsed value.
//
// On failure, returns `default_value`.
template<std::integral T = int64_t, int base = 10, StringViewLike S>
constexpr T parse_num(const S& s, T default_value) {
  auto sv = as_view(s);
  T t;
  return (extract_num<base>(t, sv) && sv.empty()) ? t : default_value;
}

// Append integral number to `target`. Hex is prefixed with "0x" and
// zero-padded to an appropriate size. Returns `target`.
template<int base = 10, size_t width = 0, char pad = ' '>
constexpr auto& append_num(AnyAppendTarget auto& target, Integer auto num) {
  auto a = appender{target};
  using C = typename decltype(a)::view_t::value_type;
  std::array<C, 64> b;
  auto [ptr, ec] = int_to_chars(b.data(), b.data() + b.size(), num, base);
  if (ec != std::errc{}) return target;
  size_t len = ptr - b.data();
  // Apply padding and prefix.
  if constexpr ((width && pad) || base == 16) {
    auto w = width;
    auto p = C(pad);
    if constexpr (base == 16 && !width) {
      a.append(C('0')).append(C('x'));
      p = C('0');
      w = sizeof(num) * 2;
    }
    if (len < w) a.append(w - len, p);
  }
  // Append number.
  return *a.append(b.data(), len);
}

// Append bool, as number, to `target`.  Returns `target`.
template<int base = 10, size_t width = 0, char pad = ' '>
constexpr auto& append_num(AnyAppendTarget auto& target, Bool auto num) {
  // Cast is needed because `std::to_chars` intentionally doesn't accept bool.
  return append_num<base, width, pad>(target, static_cast<int>(num));
}

// Return integral number as string.
// Accepts integers or bool.
template<int base = 10, size_t width = 0, char pad = ' ', typename C = char>
[[nodiscard]] constexpr std::basic_string<C>
num_as_string(std::integral auto num) {
  std::basic_string<C> target;
  return append_num<base, width, pad>(target, num);
}

} // namespace cvt_int

#pragma endregion
#pragma region cvt_float

inline namespace cvt_float {

// To float.

// Extract floating-point out of a `std::string_view`, setting output
// parameter.
//
// Skips leading whitespace, accepts leading minus sign, and does not accept
// "0x" or "0X", even when `fmt` is `hex`. (This is true for all of these
// related functions.)
//
// On success, sets output value, removes parsed characters from the string
// view, and returns true.
//
// On failure, leaves parameters unchanged and returns false.
template<std::chars_format fmt = std::chars_format::general, typename C>
constexpr bool
extract_num(std::floating_point auto& t, std::basic_string_view<C>& sv) {
  const auto save_sv = sv;
  sv = trim_left(sv);
  auto [ptr, ec] = float_from_chars(sv.data(), sv.data() + sv.size(), t, fmt);
  sv.remove_prefix(ptr - sv.data());
  if (ec == std::errc{}) return true;
  sv = save_sv;
  return false;
}

// Extract floating-point from a `std::string_view`, returning it as
// `std::optional`.
//
// On success, returns optional with value, and removes parsed characters
// from the string view.
//
// On failure, returns optional without value and leaves string view unchanged.
template<std::floating_point T,
    std::chars_format fmt = std::chars_format::general, typename C>
constexpr std::optional<T> extract_num(std::basic_string_view<C>& sv) {
  T t;
  return extract_num<fmt>(t, sv) ? std::make_optional(t) : std::nullopt;
}

// Parse floating-point from copy of a `std::string_view`, returning it as
// `std::optional`. Fails if there are any unparsed characters.
//
// On success, returns optional with value.
//
// On failure, returns optional without value.
template<std::floating_point T,
    std::chars_format fmt = std::chars_format::general, StringViewLike S>
constexpr std::optional<T> parse_num(const S& s) {
  auto sv = as_view(s);
  T t;
  return extract_num<fmt>(t, sv) && sv.empty()
             ? std::make_optional(t)
             : std::nullopt;
}

// Parse floating-point from copy of `std::string_view`, with a
// `default_value`. Fails if there are any unparsed characters.
//
// On success, returns parsed value.
//
// On failure, returns `default_value`.
template<std::floating_point T,
    std::chars_format fmt = std::chars_format::general, StringViewLike S>
constexpr T parse_num(const S& s, T default_value) {
  auto sv = as_view(s);
  T t;
  return extract_num<fmt>(t, sv) && sv.empty() ? t : default_value;
}

// Append floating-point number to `target`. Returns `target`.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' '>
constexpr auto&
append_num(AnyAppendTarget auto& target, std::floating_point auto num) {
  auto a = appender{target};
  using C = typename decltype(a)::view_t::value_type;
  std::array<C, 64> b;
  auto [ptr, ec] =
      float_to_chars(b.data(), b.data() + b.size(), num, fmt, precision);
  if (ec != std::errc{}) return target;
  const size_t len = ptr - b.data();
  if constexpr (width && pad)
    if (len < width) a.append(width - len, C(pad));
  return *a.append(b.data(), len);
}

// Return floating-point number as string.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' ', typename C = char>
[[nodiscard]] constexpr std::basic_string<C>
num_as_string(std::floating_point auto num) {
  std::basic_string<C> target;
  return append_num<fmt, precision, width, pad>(target, num);
}

} // namespace cvt_float

#pragma endregion
#pragma region cvt_bytes

inline namespace cvt_bytes {

// Reinterpret the bytes of `sv` as a span of `char_t`.
template<typename char_t = uint8_t>
requires(sizeof(char_t) == 1)
[[nodiscard]] constexpr std::span<const char_t>
as_byte_span(std::string_view sv) noexcept {
  return {reinterpret_cast<const char_t*>(sv.data()), sv.size()};
}

// Reinterpret the bytes of `s` as a `std::string_view`.
template<typename char_t>
requires(sizeof(char_t) == 1)
[[nodiscard]] constexpr std::string_view
as_string_view(std::span<char_t> s) noexcept {
  return {reinterpret_cast<const char*>(s.data()), s.size()};
}

} // namespace cvt_bytes

#pragma endregion
#pragma region cvt_stream

inline namespace cvt_stream {

// From user-specified.

// Stream append flag.
//
// Specialize on a type which supports `operator<<` and set to `true`.
//
// For example:
//    template<>
//    constexpr bool strings::stream_append_v<soldier> = true;
//
// The result of doing this is that `append_stream` is enabled for the
// registered class, and that class can be used with the `append` functions
// natively.
template<OStreamable T>
constexpr bool stream_append_v = false;

// Append streamable `t` to `target`. Returns `target`.
auto& append_stream(AppendTarget auto& target, const OStreamable auto& t) {
  if constexpr (StringViewConvertible<decltype(target)>) {
    std::stringstream s;
    s << t;
    target.append(s.str());
  } else {
    target << t;
  }
  return target;
}
} // namespace cvt_stream

#pragma endregion
#pragma region consteval

// Simple compile-time integer parser.
template<std::integral U>
[[nodiscard]] consteval std::optional<U> parse_int(std::string_view sv) {
  if (sv.empty()) return std::nullopt;
  const bool neg = (sv.front() == '-');
  if (neg) {
    if constexpr (std::is_unsigned_v<U>) return std::nullopt;
    sv.remove_prefix(1);
    if (sv.empty()) return std::nullopt;
  }
  // For signed types, accumulate in the negative domain so the most-negative
  // value of a signed type stays representable; its magnitude exceeds the
  // positive maximum.
  U value{};
  for (const char c : sv) {
    if (!strings::is_digit(c)) return std::nullopt;
    U digit = c - '0';
    if constexpr (std::is_signed_v<U>) digit = -digit;
    value *= 10;
    value += digit;
  }
  if constexpr (std::is_signed_v<U>)
    if (!neg) value = -value;
  return value;
}

#pragma endregion

}} // namespace corvid::strings::conversion
