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
#include <array>
#include <cassert>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "../meta/concepts.h"
#include "cases.h"
#include "charconv_wrapper.h"
#include "string_literals.h"
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
template<int base = 10, CharType CharT>
[[nodiscard]] constexpr bool
extract_num(std::integral auto& t, std::basic_string_view<CharT>& sv) {
  const auto save_sv = sv;
  sv = trim_left(sv);
  auto [ptr, ec] = int_from_chars(sv.data(), sv.data() + sv.size(), t, base);
  sv.remove_prefix(ptr - sv.data());
  if (ec == std::errc{}) return true;
  sv = save_sv;
  return false;
}

// Extract integer from a `std::string_view`, returning it as `std::optional`.
//
// On success, returns optional with value, and removes parsed characters
// from the string view.
//
// On failure, returns optional without value and leaves string view unchanged.
template<std::integral T = int64_t, int base = 10, CharType CharT>
[[nodiscard]] constexpr std::optional<T>
extract_num(std::basic_string_view<CharT>& sv) {
  T t{};
  return extract_num<base>(t, sv) ? std::make_optional(t) : std::nullopt;
}

// Parse integer from copy of a `std::string_view`, returning it as
// `std::optional`. Fails if there are any unparsed characters.
//
// On success, returns optional with value.
//
// On failure, returns optional without value.
template<std::integral T = int64_t, int base = 10, StringViewLike S>
[[nodiscard]] constexpr std::optional<T> parse_num(const S& s) {
  auto sv = as_view(s);
  T t{};
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
[[nodiscard]] constexpr T parse_num(const S& s, T default_value) {
  auto sv = as_view(s);
  T t{};
  return (extract_num<base>(t, sv) && sv.empty()) ? t : default_value;
}

// Append integral number to `target`. Returns `target`.
//
// When `base` is 16 and `width` is 0, the output is prefixed with "0x" and
// zero-padded to the full width of the type, with a signed value rendered as
// its unsigned two's-complement bit pattern. With an explicit `width`, there
// is no prefix and the caller's `pad` is used.
template<int base = 10, size_t width = 0UZ, char pad = ' '>
constexpr auto& append_num(AnyAppendTarget auto& target, Integer auto num) {
  using T = decltype(num);
  if constexpr (base == 16 && !width && std::is_signed_v<T>) {
    // The prefixed hex form shows the type's bit pattern, so a signed value
    // routes through its unsigned equivalent.
    return append_num<base, width, pad>(target,
        static_cast<std::make_unsigned_t<T>>(num));
  } else {
    appender a{target};
    using C = decltype(a)::view_t::value_type;
    // Worst case: `int64_t` min in base 2 is a sign plus 64 digits.
    // Deliberately uninitialized: only the written cells are ever read.
    std::array<C, 65> b;
    auto [ptr, ec] = int_to_chars(b.data(), b.data() + b.size(), num, base);
    if (ec != std::errc{}) return target;
    const auto len = static_cast<size_t>(ptr - b.data());
    // Apply padding and prefix.
    if constexpr ((width && pad) || base == 16) {
      auto w = width;
      C p{pad};
      if constexpr (base == 16 && !width) {
        a.append(C{'0'}).append(C{'x'});
        p = C{'0'};
        w = sizeof(num) * 2;
      }
      if (len < w) a.append(w - len, p);
    }
    // Append number.
    return *a.append(b.data(), len);
  }
}

// Append bool, as number, to `target`.  Returns `target`.
template<int base = 10, size_t width = 0UZ, char pad = ' '>
constexpr auto& append_num(AnyAppendTarget auto& target, Bool auto num) {
  // Cast is needed because `std::to_chars` intentionally doesn't accept bool.
  return append_num<base, width, pad>(target, static_cast<int>(num));
}

// Return integral number as string. Accepts integers or bool.
template<int base = 10, size_t width = 0UZ, char pad = ' ',
    CharType CharT = char>
[[nodiscard]] constexpr std::basic_string<CharT>
num_as_string(std::integral auto num) {
  std::basic_string<CharT> target;
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
template<std::chars_format fmt = std::chars_format::general, CharType CharT>
[[nodiscard]] constexpr bool
extract_num(std::floating_point auto& t, std::basic_string_view<CharT>& sv) {
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
    std::chars_format fmt = std::chars_format::general, CharType CharT>
[[nodiscard]] constexpr std::optional<T>
extract_num(std::basic_string_view<CharT>& sv) {
  T t{};
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
[[nodiscard]] constexpr std::optional<T> parse_num(const S& s) {
  auto sv = as_view(s);
  T t{};
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
[[nodiscard]] constexpr T parse_num(const S& s, T default_value) {
  auto sv = as_view(s);
  T t{};
  return extract_num<fmt>(t, sv) && sv.empty() ? t : default_value;
}

// Append floating-point number to `target`. Returns `target`.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0UZ, char pad = ' '>
constexpr auto&
append_num(AnyAppendTarget auto& target, std::floating_point auto num) {
  appender a{target};
  using C = decltype(a)::view_t::value_type;
  // Sized for the worst case: `fixed` format at the largest exponent (or the
  // deepest subnormal) with the maximum precision. A wide code unit is capped
  // at `float_buffer_size` by `float_to_chars` itself, so a larger buffer
  // would go unused.
  constexpr auto buf_size =
      (sizeof(C) == 1)
          ? size_t{std::numeric_limits<decltype(num)>::max_exponent10} +
                size_t{max_float_precision} + 8
          : float_buffer_size;
  // Deliberately uninitialized: only the written cells are ever read.
  std::array<C, buf_size> b;
  auto [ptr, ec] =
      float_to_chars(b.data(), b.data() + b.size(), num, fmt, precision);
  if (ec != std::errc{}) return target;
  const auto len = static_cast<size_t>(ptr - b.data());
  if constexpr (width && pad)
    if (len < width) a.append(width - len, C{pad});
  return *a.append(b.data(), len);
}

// Return floating-point number as string.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0UZ, char pad = ' ',
    CharType CharT = char>
[[nodiscard]] constexpr std::basic_string<CharT>
num_as_string(std::floating_point auto num) {
  std::basic_string<CharT> target;
  return append_num<fmt, precision, width, pad>(target, num);
}

} // namespace cvt_float

#pragma endregion
#pragma region cvt_bytes

inline namespace cvt_bytes {

// Reinterpret the bytes of `sv` as a span of `char_t`. Not `constexpr`
// because `reinterpret_cast` is never allowed in constant evaluation.
template<typename char_t = uint8_t>
requires(sizeof(char_t) == 1)
[[nodiscard]] std::span<const char_t>
as_byte_span(std::string_view sv) noexcept {
  return {reinterpret_cast<const char_t*>(sv.data()), sv.size()};
}

// Reinterpret the bytes of `s` as a `std::string_view`. Not `constexpr` for
// the same reason.
template<typename char_t>
requires(sizeof(char_t) == 1)
[[nodiscard]] std::string_view as_string_view(std::span<char_t> s) noexcept {
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
#pragma region Utilities

// Convert a hex digit value to the corresponding lowercase character. Uses
// just the last four bits of `n`.
template<CharType CharT = char>
[[nodiscard]] constexpr CharT as_hex_lc_digit(std::integral auto n) {
  static constexpr char hex[] = "0123456789abcdef";
  return static_cast<CharT>(hex[n & 0xf]);
}

// Convert a hex digit value to the corresponding uppercase character. Uses
// just the last four bits of `n`.
template<CharType CharT = char>
[[nodiscard]] constexpr CharT as_hex_uc_digit(std::integral auto n) {
  static constexpr char hex[] = "0123456789ABCDEF";
  return static_cast<CharT>(hex[n & 0xf]);
}

// Convert a hex digit character to its value. Returns -1 if `c` is not a hex
// digit.
template<CharType CharT>
[[nodiscard]] constexpr int16_t hex_digit_value(CharT ch) noexcept {
  if (is_digit(ch)) return static_cast<int16_t>(ch - CharT{'0'});
  if (is_lc_hex_alpha(ch)) return static_cast<int16_t>(10 + (ch - CharT{'a'}));
  if (is_uc_hex_alpha(ch)) return static_cast<int16_t>(10 + (ch - CharT{'A'}));
  return -1;
}

// Parse four hex digits from `s` at `pos`, returning their value.
template<CharType CharT = char>
[[nodiscard]] constexpr std::optional<uint16_t>
parse_hex4(std::basic_string_view<CharT> s, size_t pos) noexcept {
  // Spelled to avoid wrapping when `pos` is huge (e.g. `npos`).
  if (pos > s.size() || s.size() - pos < 4) return std::nullopt;
  uint16_t value{};
  for (auto ndx = 0UZ; ndx < 4; ++ndx) {
    const auto ch = s[pos + ndx];
    if (!is_hex_digit(ch)) return std::nullopt;
    value = static_cast<uint16_t>((value << 4U) | hex_digit_value(ch));
  }
  return value;
}

// Appends UTF-8 encoding of `code_point` to `out`. Returns false if
// `code_point` is not a valid Unicode code point.
[[nodiscard]] constexpr bool
append_utf8(std::string& out, uint32_t code_point) {
  // Reject invalid Unicode: surrogates and anything past U+10FFFF.
  if (code_point > 0x10FFFFU ||
      (code_point >= 0xD800U && code_point <= 0xDFFFU))
    return false;

  if (code_point <= 0x7FU) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
  return true;
}

#pragma endregion
#pragma region Escaping

// RAII to truncate a string back to its entry size on destruction, unless
// released.
//
// Serves the failure paths of the parsers below: arm it on entry, `release`
// on success, and any early failure return discards the partial output.
class truncate_guard final {
public:
  constexpr explicit truncate_guard(std::string& target) noexcept
      : target_{&target}, init_size_{target.size()} {}

  truncate_guard(const truncate_guard&) = delete;
  truncate_guard& operator=(const truncate_guard&) = delete;

  // NOLINTNEXTLINE(bugprone-exception-escape): the resize only shrinks.
  constexpr ~truncate_guard() {
    if (target_ && target_->size() > init_size_) target_->resize(init_size_);
  }

  // Disarm the guard so the truncation will not run.
  //
  // Always returns true, so a success path can return it directly.
  constexpr bool release() noexcept {
    target_ = nullptr;
    return true;
  }

private:
  std::string* target_;
  size_t init_size_;
};

// Callback that appends a single `char` and always returns true.
template<typename T>
concept CharAppenderFn = requires(T& append_cb, char c) {
  { append_cb(c) } -> std::same_as<bool>;
};

// Append `ch` as a hex escape, such as "\u{1f}" or "\u{f}", using `append_cb`
// (which must return true).
constexpr bool
append_escaped_ucode(unsigned char ch, CharAppenderFn auto append_cb) {
  assert(ch < 0x20 || ch >= 0x7f);
  append_cb('\\') && append_cb('u') && append_cb('{');
  if (ch >= 0x10) append_cb(as_hex_lc_digit<char>(ch >> 4));
  return append_cb(as_hex_lc_digit<char>(ch)) && append_cb('}');
}

// Append `ch` using `append_cb` (which must return true), escaping it if
// necessary.
constexpr bool append_escaped(char ch, CharAppenderFn auto append_cb) {
  switch (ch) {
  case '\\': return append_cb('\\') && append_cb('\\');
  case '\t': return append_cb('\\') && append_cb('t');
  case '\n': return append_cb('\\') && append_cb('n');
  case '\r': return append_cb('\\') && append_cb('r');
  case '"': return append_cb('\\') && append_cb('"');
  default:
    const auto byte = static_cast<unsigned char>(ch);
    if (byte >= 0x20 && byte < 0x7f) return append_cb(ch);
    return append_escaped_ucode(byte, append_cb);
  }
}

// Append `s` to `out`, escaping any characters that need it. Returns true.
constexpr bool append_escaped(std::string& out, std::string_view s) {
  out.reserve(out.size() + s.size());
  for (const char ch : s)
    append_escaped(ch, [&out](char c) {
      out += c;
      return true;
    });
  return true;
}

// Parse hex escape, such as "\u{1f}" or "\u{f}", out of the start of `sv`
// into `ch`.
//
// On success, returns true and sets `ch`, removing the parsed characters from
// `sv`. On failure, which includes an empty "\u{}" and a value over 0xff,
// returns false and leaves `sv` unchanged.
[[nodiscard]] constexpr bool parse_u_code(std::string_view& sv, char& ch) {
  if (sv.size() < 5 || sv[0] != '\\' || sv[1] != 'u' || sv[2] != '{')
    return false;
  size_t ndx = 3;
  unsigned value{};
  for (; ndx < sv.size() && sv[ndx] != '}'; ++ndx) {
    const auto digit = static_cast<unsigned>(hex_digit_value(sv[ndx]));
    if (digit > 0xf) return false;
    value = (value << 4U) | digit;
    if (value > 0xffU) return false;
  }
  if (ndx == 3 || ndx == sv.size() || sv[ndx] != '}') return false;
  ch = static_cast<char>(value);
  sv.remove_prefix(ndx + 1);
  return true;
}

// Parse an escaped character out of the start of `sv` into `ch`.
//
// On success, returns true and sets `ch`, removing the parsed characters from
// `sv`. On failure, returns false and leaves `sv` unchanged.
[[nodiscard]] constexpr bool parse_escaped(std::string_view& sv, char& ch) {
  if (sv.empty() || sv[0] != '\\') return false;
  if (sv.size() < 2) return false;
  switch (sv[1]) {
  case '\\': ch = '\\'; break;
  case 't': ch = '\t'; break;
  case 'n': ch = '\n'; break;
  case 'r': ch = '\r'; break;
  case '"': ch = '"'; break;
  case 'u': return parse_u_code(sv, ch);
  default: return false;
  }
  sv.remove_prefix(2);
  return true;
}

// Parse all of `sv`, appending the unescaped characters to `out`.
//
// Returns true on success, false on failure (with `out` preserved).
[[nodiscard]] constexpr bool
parse_escaped(std::string_view sv, std::string& out) {
  truncate_guard guard(out);
  out.reserve(out.size() + sv.size());
  char ch{};
  while (!sv.empty()) {
    if (sv[0] == '\\') {
      if (!parse_escaped(sv, ch)) return false;
      out += ch;
    } else {
      out += sv[0];
      sv.remove_prefix(1);
    }
  }
  return guard.release();
}

// Parse `sv`, which starts with a quoted string, appending the unescaped
// characters to `out`.
//
// Does not append the surrounding quotes; updates `sv` to point to the
// remainder. Returns true on success, false on failure (with `out` and `sv`
// preserved).
[[nodiscard]] constexpr bool
parse_escaped_quoted(std::string_view& sv, std::string& out) {
  auto rest = sv;
  if (rest.empty() || rest[0] != '"') return false;
  rest.remove_prefix(1);
  truncate_guard guard(out);
  while (!rest.empty() && rest[0] != '"') {
    char ch{};
    if (rest[0] == '\\') {
      if (!parse_escaped(rest, ch)) return false;
      out += ch;
    } else {
      out += rest[0];
      rest.remove_prefix(1);
    }
  }
  if (rest.empty() || rest[0] != '"') return false;
  rest.remove_prefix(1);
  sv = rest;
  return guard.release();
}

#pragma endregion
}} // namespace corvid::strings::conversion
