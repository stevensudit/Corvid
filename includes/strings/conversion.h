// Corvid20: A general-purpose C++ 20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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
#include "trimming.h"

namespace corvid::strings {
inline namespace conversion {

//
// Numerical conversions
//

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
// On failure, leaves output value alone, possibly removes some characters
// from the string view, and returns false.
template<int base = 10>
constexpr bool extract_num(std::integral auto& t, std::string_view& sv) {
  sv = trim_left(sv);
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), t, base);
  sv.remove_prefix(ptr - sv.data());
  return ec == std::errc{};
}

// Extract integer from a `std::string_view`, returning it as
// `std::optional`.
//
// On success, returns optional with value, and removes parsed characters
// from the string view.
//
// On failure, returns optional without value, and possibly removes some
// characters from the string view.
template<std::integral T = int64_t, int base = 10>
constexpr std::optional<T> extract_num(std::string_view& sv) {
  T t;
  return extract_num<base>(t, sv) ? std::make_optional(t) : std::nullopt;
}

// Parse integer from copy of a `std::string_view`, returning it as
// `std::optional`.
//
// On success, returns optional with value.
//
// On failure, returns optional without value.
template<std::integral T = int64_t, int base = 10>
constexpr std::optional<T> parse_num(std::string_view sv) {
  return extract_num<T, base>(sv);
}

// Parse integer from copy of a `std::string_view` with a `default_value`.
//
// On success, returns parsed value.
//
// On failure, returns `default_value`.
template<std::integral T = int64_t, int base = 10>
constexpr T parse_num(std::string_view sv, std::integral auto default_value) {
  T t;
  return extract_num<base>(t, sv) ? t : default_value;
}

// Append integral number to `target`. Hex is prefixed with "0x" and
// zero-padded to an appropriate size.
template<int base = 10, size_t width = 0, char pad = ' '>
constexpr auto& append_num(AppendTarget auto& target, Integral auto num) {
  auto a = appender{target};
  std::array<char, 64> b;
  if (auto [ptr, ec] = std::to_chars(b.data(), b.data() + b.size(), num, base);
      ec == std::errc())
  {
    size_t len = ptr - b.data();
    // Apply padding and prefix.
    if constexpr ((width && pad) || base == 16) {
      auto w = width;
      auto p = pad;
      if constexpr (base == 16 && !width) {
        a.append("0x"sv);
        p = '0';
        w = sizeof(num) * 2;
      }
      if (len < w) a.append(w - len, p);
    }
    // Append number.
    a.append(b.data(), len);
  }
  return target;
}

// Append bool as number.
// Cast is needed because `std::to_chars` doesn't accept bool.
template<int base = 10, size_t width = 0, char pad = ' '>
constexpr auto& append_num(AppendTarget auto& target, Bool auto num) {
  return append_num<base, width, pad>(target, static_cast<int>(num));
}

// Return integral number as string.
// Accepts integers or bool.
template<int base = 10, size_t width = 0, char pad = ' '>
constexpr std::string num_as_string(std::integral auto num) {
  std::string target;
  return append_num<base, width, pad>(target, num);
}

} // namespace cvt_int
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
// On failure, leaves output value alone, possibly removes some characters
// from the string view, and returns false.
template<std::chars_format fmt = std::chars_format::general>
constexpr bool extract_num(std::floating_point auto& t, std::string_view& sv) {
  sv = trim_left(sv);
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), t, fmt);
  sv.remove_prefix(ptr - sv.data());
  return ec == std::errc{};
}

// Extract floating-point from a `std::string_view`, returning it as
// `std::optional`.
//
// On success, returns optional with value, and removes parsed characters
// from the string view.
//
// On failure, returns optional without value, and possibly removes some
// characters from the string view.
template<std::floating_point T,
    std::chars_format fmt = std::chars_format::general>
constexpr std::optional<T> extract_num(std::string_view& sv) {
  T t;
  return extract_num<fmt>(t, sv) ? std::make_optional(t) : std::nullopt;
}

// Parse floating-point from copy of a `std::string_view`, returning it as
// `std::optional`.
//
// On success, returns optional with value.
//
// On failure, returns optional without value.
template<std::floating_point T,
    std::chars_format fmt = std::chars_format::general>
constexpr std::optional<T> parse_num(std::string_view sv) {
  return extract_num<T, fmt>(sv);
}

// Parse floating-point from copy of `std::string_view` with a
// `default_value`.
//
// On success, returns parsed value.
//
// On failure, returns `default_value`.
template<std::floating_point T,
    std::chars_format fmt = std::chars_format::general>
constexpr T parse_num(std::string_view sv, T default_value) {
  T t;
  return extract_num<fmt>(t, sv) ? t : default_value;
}

// Append floating-point number to `target`.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' '>
constexpr auto&
append_num(AppendTarget auto& target, std::floating_point auto num) {
  auto a = appender{target};
  std::array<char, 64> b;
  std::to_chars_result res;
  if constexpr (precision != -1)
    res = std::to_chars(b.data(), b.data() + b.size(), num, fmt, precision);
  else
    res = std::to_chars(b.data(), b.data() + b.size(), num, fmt);
  if (auto [ptr, ec] = res; ec == std::errc()) {
    size_t len = ptr - b.data();
    if constexpr (width && pad)
      if (len < width) a.append(width - len, pad);
    a.append(b.data(), len);
  }
  return target;
}

// Return floating-point number as string.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' '>
constexpr std::string num_as_string(std::floating_point auto num) {
  std::string target;
  return append_num<fmt, precision, width, pad>(target, num);
}

} // namespace cvt_float

inline namespace cvt_enum {

// From enum.

namespace details {

// TODO: Get rid of this entirely and instead bring in the enum registry.
// default_enum_printer
template<typename T>
struct default_enum_printer {
  auto& append(AppendTarget auto& target, ScopedEnum auto t) const {
    return append_num(target, as_underlying(t));
  }
};
} // namespace details

// TODO: Get rid of this entirely and instead bring in the enum registry.
// enum_printer_v
//
// Enable printing an enum value as text.
//
// The default enum printer just outputs the underlying integer, but other
// versions handle bitmask and sequential enums.
template<ScopedEnum T>
constexpr auto enum_printer_v = details::default_enum_printer<T>();

// Append enum to `target`.
constexpr auto& append_enum(AppendTarget auto& target, ScopedEnum auto t) {
  return enum_printer_v<decltype(t)>.append(target, t);
}

// Return enum as string.
constexpr std::string enum_as_string(ScopedEnum auto t) {
  std::string target;
  return append_enum(target, t);
}

} // namespace cvt_enum

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

// Append streamable `t` to `target`.
// TODO: Add support for escaping.
constexpr auto&
append_stream(AppendTarget auto& target, const OStreamable auto& t) {
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

} // namespace conversion
} // namespace corvid::strings

// Append scoped enum to `os`.
auto& operator<<(std::ostream& os, corvid::ScopedEnum auto t) {
  return corvid::strings::append_enum(os, t);
}
