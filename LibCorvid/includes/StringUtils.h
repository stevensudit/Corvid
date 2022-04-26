// Corvid: A general-purpose C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022 Steven Sudit
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
#include <algorithm>
#include <charconv>
#include <iostream>
#include <vector>

#include "Meta.h"

// Recommendation: While you can import the entire `corvid::strings` namespace,
// you may not want to bring in all of these symbols, or you may wish to do so
// more selectively.
//
// The way to do that is to import just `corvid` and then reference symbols
// through the `strings` namespace, such as `strings::trim("a ")`. You can also
// choose to import the inline namespace for that group of symbols, such as
// `corvid::trimming`.
namespace corvid::strings {

// Import.
using namespace std::literals;
constexpr size_t npos = -1;

inline namespace cpp20 {

//
// C++20 emulators
//

// Emulates C++20's `std::string_view::starts_with`.
constexpr [[nodiscard]] bool
starts_with(std::string_view whole, std::string_view part) noexcept {
  return whole.compare(0, part.size(), part) == 0;
}

// Emulates C++20's `std::string_view::ends_with`.
constexpr [[nodiscard]] bool
ends_with(std::string_view whole, std::string_view part) noexcept {
  if (whole.size() < part.size()) return false;
  return whole.compare(whole.size() - part.size(), part.size(), part) == 0;
}

} // namespace cpp20
inline namespace cases {

//
// Case change.
//

// Convert to uppercase.
constexpr void to_upper(std::string& s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return std::toupper(c);
  });
}

// Return as uppercase.
constexpr [[nodiscard]] std::string as_upper(std::string_view sv) {
  std::string s{sv};
  to_upper(s);
  return s;
}

// Convert to lowercase.
constexpr void to_lower(std::string& s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return std::tolower(c);
  });
}

// Return as lowercase.
constexpr [[nodiscard]] std::string as_lower(std::string_view sv) {
  std::string s{sv};
  to_lower(s);
  return s;
}

} // namespace cases
inline namespace search_and {

//
// Search and Replace
//

// Return whether `value` was found in `s` at `ndx`, updating `ndx`.
template<typename T>
constexpr bool found_next(size_t& ndx, std::string_view s, const T& value) {
  return (ndx = s.find(value, ndx)) != npos;
}

// Replace instances of `from` in `s` with `to`, returning count of
// replacements.
constexpr size_t
replace(std::string& s, std::string_view from, std::string_view to) {
  size_t cnt{};
  for (size_t ndx = 0; found_next(ndx, s, from); ndx += to.size()) {
    ++cnt;
    s.replace(ndx, from.size(), to);
  }
  return cnt;
}

// Replace instances of `from` in `s` with `to`, returning count of
// replacements.
constexpr size_t replace(std::string& s, char from, char to) {
  size_t cnt{};
  for (size_t ndx = 0; found_next(ndx, s, from); ++ndx) {
    ++cnt;
    s[ndx] = to;
  }
  return cnt;
}

} // namespace search_and
inline namespace targeting {

//
// Appender target
//

// The appender is a thin wrapper over a target stream or string. As its name
// suggests, it's used in the various append functions to support either type
// of target seamlessly.
//
// Note: Under clang and gcc, this is optimized away entirely. Under MSVC, not
// quite. But this is consistent with MSVC's overall pattern of underwhelming
// optimization.

namespace details {

// Generic ostream specialization.
template<typename T>
struct appender {
  explicit appender(T& target) : target(target) {}

  auto& append(std::string_view sv) {
    target.write(sv.data(), sv.size());
    return *this;
  }

  auto& append(const char* ps, size_t len) {
    target.write(ps, len);
    return *this;
  }

  auto& append(char ch) {
    target.put(ch);
    return *this;
  }

  auto& append(size_t len, char ch) {
    while (len--) target.put(ch);
    return *this;
  }

  auto& reserve(size_t len) { return *this; }

  T& operator*() { return target; }

  T& target;
};

// String partial specialization.
template<>
struct appender<std::string> {
  explicit appender(std::string& target) : target(target) {}

  auto& append(std::string_view sv) {
    target.append(sv);
    return *this;
  }

  auto& append(const char* ps, size_t len) {
    target.append(ps, len);
    return *this;
  }

  auto& append(char ch) {
    target += ch;
    return *this;
  }

  auto& append(size_t len, char ch) {
    target.append(len, ch);
    return *this;
  }

  auto& reserve(size_t len) {
    target.reserve(target.size() + len);
    return *this;
  }

  std::string& operator*() { return target; }

  std::string& target;
};

} // namespace details

// Determine whether `T` is appendable to.
template<typename T>
constexpr bool is_appendable_v =
    std::is_same_v<std::remove_cvref_t<T>, std::string> ||
    std::is_base_of_v<std::ostream, std::remove_cvref_t<T>>;

// Make appendable target out of `t`.
template<typename T>
auto make_appender(T& t) {
  using U = std::remove_cvref_t<T>;
  if constexpr (is_string_view_convertible_v<U>)
    return details::appender<std::string>(t);
  else
    return details::appender<U>(t);
}

} // namespace targeting
inline namespace delimiting {

//
// Delimiter
//

// Delimiter wrapper.
//
// This class is not intended for standalone use. While it provides some
// utility, it is very limited and internal. The only reason it's externally
// visible at all is to document the delimiter parameters with a distinct type.
//
// The precise usage varies depending upon context:
// - When splitting, checks for any of the characters.
// - When joining, appends the entire string.
// - When manipulating braces, treated as an open/close pair.
struct delim: public std::string_view {
  constexpr delim() : delim(" ") {}

  template<typename T>
  constexpr delim(T&& list) : std::string_view(std::forward<T>(list)) {}

  constexpr [[nodiscard]] auto find_in(std::string_view whole) const {
    if (size() == 1) return whole.find(front());
    return whole.find_first_of(*this);
  }

  constexpr [[nodiscard]] auto find_not_in(std::string_view whole) const {
    if (size() == 1) return whole.find_first_not_of(front());
    return whole.find_first_not_of(*this);
  }

  constexpr [[nodiscard]] auto find_last_not_in(std::string_view whole) const {
    if (size() == 1) return whole.find_last_not_of(front());
    return whole.find_last_not_of(*this);
  }

  // Append.
  template<typename A, enable_if_0<is_appendable_v<A>> = 0>
  constexpr auto& append(A& target) const {
    make_appender(target).append(*this);
    return target;
  }

  // Append after the first time.
  //
  // Set `skip` initially. Then, on the first call, `skip` will be cleared, but
  // nothing will be appended. On subsequent calls `skip` will remain cleared,
  // so the delimiter will be appended.
  template<typename A, enable_if_0<is_appendable_v<A>> = 0>
  constexpr auto& append_skip_once(A& target, bool& skip) const {
    if (!skip)
      append(target);
    else
      skip = false;
    return target;
  }

  // Append when `emit`.
  template<bool emit = true, typename A, enable_if_0<is_appendable_v<A>> = 0>
  constexpr auto& append_if(A& target) const {
    if constexpr (emit) append(target);
    return target;
  }
};

} // namespace delimiting
inline namespace streaming {

//
// Streaming
//

// Lightweight streaming wrappers to avoid having to constantly type
// `std::cout <<` and `<< std::endl`, while providing a small amount of
// additional utility.

template<typename... Args>
constexpr auto& stream_out(std::ostream& os, Args&&... args) {
  return (os << ... << args);
}

template<typename Head, typename... Tail>
constexpr auto& stream_out_with(std::ostream& os, delim d, const Head& head,
    const Tail&... tail) {
  os << head;
  return ((os << d << (tail)), ...);
}

template<typename... Ts>
constexpr auto& print(const Ts&... parts) {
  return stream_out(std::cout, parts...);
}

template<typename... Ts>
constexpr auto& print_with(delim d, const Ts&... parts) {
  return stream_out_with(std::cout, d, parts...);
}

template<typename... Ts>
constexpr auto& println(const Ts&... parts) {
  return print(parts...) << '\n';
}

template<typename... Ts>
constexpr auto& println_with(delim d, const Ts&... parts) {
  return print_with(d, parts...) << '\n';
}

template<typename... Ts>
constexpr auto& log(const Ts&... parts) {
  return stream_out(std::clog, parts...) << std::endl;
}

template<typename... Ts>
constexpr auto& log_if(bool emit, const Ts&... parts) {
  if (emit) log(parts...);
  return std::clog;
}

template<typename... Ts>
constexpr auto& log_with(delim d, const Ts&... parts) {
  return stream_out_with(std::clog, d, parts...) << std::endl;
}

// Redirect a `std::ostream`, `from`, to a different one, `to`, during its
// lifespan.
class ostream_redirector {
public:
  ostream_redirector(std::ostream& from, std::ostream& to)
      : from_{&from}, rdbuf_{from.rdbuf()} {
    from.rdbuf(to.rdbuf());
  }

  ~ostream_redirector() { from_->rdbuf(rdbuf_); }

private:
  std::ostream* from_;
  std::streambuf* rdbuf_;
};

} // namespace streaming
inline namespace trimming {

//
// Trim
//

// Trim whitespace on left, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto trim_left(std::string_view whole, delim ws = {}) {
  auto pos = ws.find_not_in(whole);
  std::string_view part;
  if (pos != npos) part = whole.substr(pos);
  return R{part};
}

// Trim whitespace on right, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto
trim_right(std::string_view whole, delim ws = {}) {
  auto pos = ws.find_last_not_in(whole);
  auto part = whole.substr(0, pos + 1);
  return R{part};
}

// Trim whitespace, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto trim(std::string_view whole, delim ws = {}) {
  return trim_right<R>(trim_left(whole, ws), ws);
}

// Trim container in place.
template<typename T, enable_if_0<is_container_v<T>> = 0>
constexpr void trim(T& wholes, const delim ws = {}) {
  for (auto& item : wholes) {
    auto& part = container_element_v(&item);
    part = trim<std::remove_reference_t<decltype(part)>>(part);
  }
}

// Trim a temporary container, passing it through.
//
// Ideal for calling directly on the result of split.
template<typename T, enable_if_0<is_container_v<T>> = 0>
constexpr [[nodiscard]] auto&& trim(T&& wholes, delim ws = {}) {
  trim(wholes, ws);
  return wholes;
}

} // namespace trimming
inline namespace splitting {

//
// Split
//

// For all split functions, `delim` defaults to " " and can be specified as any
// set of characters.

// Extract next delimited piece destructively from `whole`.
//
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto
extract_piece(std::string_view& whole, delim d = {}) {
  auto pos = std::min(whole.size(), d.find_in(whole));
  auto part = whole.substr(0, pos);
  whole.remove_prefix(std::min(whole.size(), pos + 1));
  return R{part};
}

// Extract next delimited piece into `part`, removing it from `whole`.
//
// Returns true so long as there's more work to do.
//
// Specify R as `std::string` to make a deep copy.
template<typename R>
constexpr [[nodiscard]] bool
more_pieces(R& part, std::string_view& whole, delim d = {}) {
  auto all = whole.size();
  part = extract_piece<R>(whole, d);
  return part.size() != all;
}

// Split all pieces by delimiters and return parts in vector.
//
// Keeps any empty parts.
//
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto split(std::string_view whole, delim d = {}) {
  std::vector<R> parts;
  std::string_view part;
  for (bool more = !whole.empty(); more;) {
    more = more_pieces(part, whole, d);
    parts.push_back(R{part});
  }
  return parts;
}

// Split a temporary string by delimiters, making deep copies of the parts.
constexpr [[nodiscard]] auto split(std::string&& whole, delim d = {}) {
  return split<std::string>(std::string_view(whole), d);
}

} // namespace splitting
inline namespace conversion {

//
// Numerical conversions
//

// To int.

// Extract integer out of a `std::string_view`, setting output parameter.
//
// Skips leading white space, accepts leading minus sign, and does not accept
// "0x" or "0X", even when `base` is 16. (This is true for all of these
// related functions.)
//
// On success, sets output value, removes parsed characters from the string
// view, and returns true.
//
// On failure, leaves output value alone, possibly removes some characters
// from the string view, and returns false.
template<int base = 10, typename T, enable_if_0<is_integral_number_v<T>> = 0>
constexpr bool extract_num(T& t, std::string_view& sv) {
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
template<typename T = int64_t, int base = 10,
    enable_if_0<is_integral_number_v<T>> = 0>
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
template<typename T = int64_t, int base = 10,
    enable_if_0<is_integral_number_v<T>> = 0>
constexpr std::optional<T> parse_num(std::string_view sv) {
  return extract_num<base>(sv);
}

// Parse integer from copy of a `std::string_view` with a `default_value`.
//
// On success, returns parsed value.
//
// On failure, returns `default_value`.
template<typename T = int64_t, int base = 10,
    enable_if_0<is_integral_number_v<T>> = 0>
constexpr T parse_num(std::string_view sv, T default_value) {
  T t;
  return extract_num<base>(t, sv) ? t : default_value;
}

// To float.

// Extract floating-point out of a `std::string_view`, setting output
// parameter.
//
// Skips leading white space, accepts leading minus sign, and does not accept
// "0x" or "0X", even when `fmt` is `hex`. (This is true for all of these
// related functions.)
//
// On success, sets output value, removes parsed characters from the string
// view, and returns true.
//
// On failure, leaves output value alone, possibly removes some characters
// from the string view, and returns false.
template<std::chars_format fmt = std::chars_format::general, typename T,
    enable_if_0<is_floating_number_v<T>> = 0>
constexpr bool extract_num(T& t, std::string_view& sv) {
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
template<typename T, std::chars_format fmt = std::chars_format::general,
    enable_if_0<is_floating_number_v<T>> = 0>
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
template<typename T, std::chars_format fmt = std::chars_format::general,
    enable_if_0<is_floating_number_v<T>> = 0>
constexpr std::optional<T> parse_num(std::string_view sv) {
  return extract_num<fmt>(sv);
}

// Parse floating-point from copy of `std::string_view` with a
// `default_value`.
//
// On success, returns parsed value.
//
// On failure, returns `default_value`.
template<typename T, std::chars_format fmt = std::chars_format::general,
    enable_if_0<is_floating_number_v<T>> = 0>
constexpr T parse_num(std::string_view sv, T default_value) {
  T t;
  return extract_num<fmt>(t, sv) ? t : default_value;
}

//
// To string.
//

// Append integral number to `target`. Hex is prefixed with "0x" and
// zero-padded to an appropriate size.
template<int base = 10, size_t width = 0, char pad = ' ', typename T,
    typename A, enable_if_0<is_appendable_v<A> && is_integral_number_v<T>> = 0>
constexpr auto& append_num(A& target, T num) {
  auto a = make_appender(target);
  std::array<char, 64> b;
  if (auto [ptr, ec] = std::to_chars(b.data(), b.data() + b.size(), num, base);
      ec == std::errc())
  {
    size_t len = ptr - b.data();
    if constexpr ((width && pad) || base == 16) {
      auto w = width;
      auto p = pad;
      if constexpr (base == 16 && !width) {
        a.append("0x"sv);
        p = '0';
        w = sizeof(T) * 2;
      }
      if (len < w) a.append(w - len, p);
    }
    a.append(b.data(), len);
  }
  return target;
}

// Return integral number as string.
template<int base = 10, size_t width = 0, char pad = ' ', typename T,
    enable_if_0<is_integral_number_v<T>> = 0>
constexpr std::string num_as_string(T num) {
  std::string target;
  return append_num<base, width, pad>(target, num);
}

// Append floating-point number to `target`.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' ', typename T,
    typename A, enable_if_0<is_appendable_v<A> && is_floating_number_v<T>> = 0>
constexpr auto& append_num(A& target, T num) {
  auto a = make_appender(target);
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
    int precision = -1, size_t width = 0, char pad = ' ', typename T,
    enable_if_0<is_floating_number_v<T>> = 0>
constexpr std::string num_as_string(T num) {
  std::string target;
  return append_num<fmt, precision, width, pad>(target, num);
}

inline namespace enumprint {

//
// enumprint
//

namespace details {

// default_enum_printer
template<typename T>
struct default_enum_printer {
  template<typename A>
  auto& append(A& target, T t) const {
    return append_num(target, as_underlying(t));
  }
};
} // namespace details

// enum_printer_v
//
// Enable printing an enum value as text.
//
// The default enum printer just outputs the underlying integer, but other
// versions handle bitmask and sequential enums.
template<typename T, enable_if_0<is_enum_v<T>> = 0>
constexpr auto enum_printer_v = details::default_enum_printer<T>();

// Append enum to `target`.
template<typename T, typename A,
    enable_if_0<is_appendable_v<A> && is_enum_v<T>> = 0>
constexpr A& append_enum(A& target, T t) {
  return enum_printer_v<T>.append(target, t);
}

// Return enum as string.
template<typename T, enable_if_0<is_enum_v<T>> = 0>
constexpr std::string enum_as_string(T t) {
  std::string target;
  return append_enum(target, t);
}

} // namespace enumprint
} // namespace conversion

//
// TODO
//

// TODO: Consider moving `width` and `pad` to defaulted parameters, not
// defaulted non-class templates. After all, these might change at runtime.

// TODO: Get extract_num to work with cstring_view cleanly. It's safe because
// we only remove the prefix, never the suffix. The brute-force solution is to
// add overloads for the extract methods. Maybe add extract_num and such to
// cstring, so as not to pollute this.

// TODO: Maybe add a replace_any that replaces any matching chars with the
// destination value. Maybe supplement replace with remove and remove_any.

// TODO: Maybe make `log` and such thread-safe? It's not really intended to be
// a full, production logging system, so this might be overkill.

// TODO: Maybe an `operator<<` for enum? (But likely opt-in and only for
// registered).

// TODO: Wacky idea: overload unary `operator+` for `std::string_view` to mean
// non-empty.

} // namespace corvid::strings
