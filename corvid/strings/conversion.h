// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include "../enums/enum_registry.h"

namespace corvid::strings { inline namespace conversion {

inline namespace cvt_fix_from_chars {

#if __cpp_lib_to_chars < 202306L

// Ugly workaround for the fact that `std::from_chars` is not available in
// libstdc++. Does not honor `fmt`.
//
// TODO: Add thorough testing, or just replace with a stable third-party
// dependency.
template<typename T>
std::from_chars_result std_from_chars(const char* first, const char* last,
    T& value, std::chars_format fmt = std::chars_format::general) {
  // Format isn't supported.
  (void)fmt;

  // Default to failure.
  std::from_chars_result result{.ptr = first,
      .ec = std::errc::invalid_argument};

  // Sanity check.
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
      "Unsupported type. Only float and double are supported.");
  if (!first || !last || first >= last) return result;

  // Copy into buffer and use that.
  char buffer[128];
  const std::string_view view(first, last);
  const auto copied = std::min(view.size(), sizeof(buffer) - 1);
  view.copy(buffer, copied);
  buffer[copied] = '\0';

  // Assume failure.
  result.ec = std::errc::result_out_of_range;
  char* endptr = buffer;
  T parsed_value{};
  errno = 0;

  // Select the correct parsing function based on the type `T`.
  if constexpr (std::is_same_v<T, float>) {
    parsed_value = std::strtof(buffer, &endptr);
  } else if constexpr (std::is_same_v<T, double>) {
    parsed_value = std::strtod(buffer, &endptr);
  }
  if (errno == ERANGE) return result;

  result.ptr = first + (endptr - buffer);
  result.ec = {};
  value = parsed_value;

  return result;
}
#else

// Passthrough for gcc.
template<typename T>
std::from_chars_result std_from_chars(const char* first, const char* last,
    T& value, std::chars_format fmt = std::chars_format::general) {
  return std::from_chars(first, last, value, fmt);
}

#endif

} // namespace cvt_fix_from_chars

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
// On failure, leaves the parameters unchanged and returns false.
template<int base = 10>
constexpr bool extract_num(std::integral auto& t, std::string_view& sv) {
  const auto save_sv = sv;
  sv = trim_left(sv);
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), t, base);
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
template<std::integral T = int64_t, int base = 10>
constexpr std::optional<T> extract_num(std::string_view& sv) {
  T t;
  return extract_num<base>(t, sv) ? std::make_optional(t) : std::nullopt;
}

// Parse integer from copy of a `std::string_view`, returning it as
// `std::optional`. Fails if there are any unparsed characters.
//
// On success, returns optional with value.
//
// On failure, returns optional without value.
template<std::integral T = int64_t, int base = 10>
constexpr std::optional<T> parse_num(std::string_view sv) {
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
template<std::integral T = int64_t, int base = 10>
constexpr T parse_num(std::string_view sv, std::integral auto default_value) {
  T t;
  return (extract_num<base>(t, sv) && sv.empty()) ? t : default_value;
}

// Append integral number to `target`. Hex is prefixed with "0x" and
// zero-padded to an appropriate size. Returns `target`.
template<int base = 10, size_t width = 0, char pad = ' '>
constexpr auto& append_num(AppendTarget auto& target, Integer auto num) {
  auto a = appender{target};
  std::array<char, 64> b;
  auto [ptr, ec] = std::to_chars(b.data(), b.data() + b.size(), num, base);
  if (ec != std::errc{}) return target;
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
  return *a.append(b.data(), len);
}

// Append bool, as number, to `target`.  Returns `target`.
template<int base = 10, size_t width = 0, char pad = ' '>
constexpr auto& append_num(AppendTarget auto& target, Bool auto num) {
  // Cast is needed because `std::to_chars` intentionally doesn't accept bool.
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
// On failure, leaves parameters unchanged and returns false.
template<std::chars_format fmt = std::chars_format::general>
constexpr bool extract_num(std::floating_point auto& t, std::string_view& sv) {
  const auto save_sv = sv;
  sv = trim_left(sv);
  auto [ptr, ec] = std_from_chars(sv.data(), sv.data() + sv.size(), t, fmt);
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
    std::chars_format fmt = std::chars_format::general>
constexpr std::optional<T> extract_num(std::string_view& sv) {
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
    std::chars_format fmt = std::chars_format::general>
constexpr std::optional<T> parse_num(std::string_view sv) {
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
    std::chars_format fmt = std::chars_format::general>
constexpr T parse_num(std::string_view sv, T default_value) {
  T t;
  return extract_num<fmt>(t, sv) && sv.empty() ? t : default_value;
}

// Append floating-point number to `target`. Returns `target`.
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
  auto [ptr, ec] = res;
  if (ec != std::errc{}) return target;
  const size_t len = ptr - b.data();
  if constexpr (width && pad)
    if (len < width) a.append(width - len, pad);
  return *a.append(b.data(), len);
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

// Append enum to `target`. Returns `target`.
constexpr auto& append_enum(AppendTarget auto& target, ScopedEnum auto e) {
  return registry::enum_spec_v<decltype(e)>.append(target, e);
}

// Return enum as string.
constexpr std::string enum_as_string(ScopedEnum auto t) {
  std::string target;
  return append_enum(target, t);
}

// Note: See "enum_conversion.h" for `extract_enum` and `parse_enum`.
// These had to be separated out so as to manage dependencies.

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
}} // namespace corvid::strings::conversion

// Append scoped enum to `os`.
//
// No need to define this for old-style enums because they're treated as
// aliases for their underlying type, which is already supported.
auto& operator<<(std::ostream& os, corvid::ScopedEnum auto t) {
  return corvid::strings::append_enum(os, t);
}
