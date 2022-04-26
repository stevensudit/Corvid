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
struct Delim: public std::string_view {
  constexpr Delim() : Delim(" ") {}

  template<typename T>
  constexpr Delim(T&& list) : std::string_view(std::forward<T>(list)) {}

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

  // Append after the first time.
  //
  // Set `skip` initially. Then, on the first call, `skip` will be cleared, but
  // nothing will be appended. On subsequent calls `skip` will remainc cleared,
  // so the delimiter will be appended.
  constexpr auto& append_skip_once(std::string& target, bool& skip) const {
    if (!skip)
      target.append(*this);
    else
      skip = false;
    return target;
  }

  // Append when `emit`.
  template<bool emit = true>
  constexpr auto& append_if(std::string& target) const {
    if constexpr (emit) target.append(*this);
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
constexpr auto& stream_out_with(std::ostream& os, const Delim& delim,
    const Head& head, const Tail&... tail) {
  os << head;
  return ((os << delim << (tail)), ...);
}

template<typename... Ts>
constexpr auto& print(const Ts&... parts) {
  return stream_out(std::cout, parts...);
}

template<typename... Ts>
constexpr auto& print_with(const Delim& delim, const Ts&... parts) {
  return stream_out_with(std::cout, delim, parts...);
}

template<typename... Ts>
constexpr auto& println(const Ts&... parts) {
  return print(parts...) << '\n';
}

template<typename... Ts>
constexpr auto& println_with(const Delim& delim, const Ts&... parts) {
  return print_with(delim, parts...) << '\n';
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
constexpr auto& log_with(const Delim& delim, const Ts&... parts) {
  return stream_out_with(std::clog, delim, parts...) << std::endl;
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
constexpr [[nodiscard]] auto
trim_left(std::string_view whole, const Delim& ws = {}) {
  auto pos = ws.find_not_in(whole);
  std::string_view part;
  if (pos != npos) part = whole.substr(pos);
  return R{part};
}

// Trim whitespace on right, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto
trim_right(std::string_view whole, const Delim& ws = {}) {
  auto pos = ws.find_last_not_in(whole);
  auto part = whole.substr(0, pos + 1);
  return R{part};
}

// Trim whitespace, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto
trim(std::string_view whole, const Delim& ws = {}) {
  return trim_right<R>(trim_left(whole, ws), ws);
}

// Trim container in place.
template<typename T, enable_if_0<is_container_v<T>> = 0>
constexpr void trim(T& wholes, const Delim ws = {}) {
  for (auto& item : wholes) {
    auto& part = container_element_v(&item);
    part = trim<std::remove_reference_t<decltype(part)>>(part);
  }
}

// Trim a temporary container, passing it through.
//
// Ideal for calling directly on the result of split.
template<typename T, enable_if_0<is_container_v<T>> = 0>
constexpr [[nodiscard]] auto&& trim(T&& wholes, const Delim& ws = {}) {
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
extract_piece(std::string_view& whole, const Delim& delim = {}) {
  auto pos = std::min(whole.size(), delim.find_in(whole));
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
more_pieces(R& part, std::string_view& whole, const Delim& delim = {}) {
  auto all = whole.size();
  part = extract_piece<R>(whole, delim);
  return part.size() != all;
}

// Split all pieces by delimiters and return parts in vector.
//
// Keeps any empty parts.
//
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto
split(std::string_view whole, const Delim& delim = {}) {
  std::vector<R> parts;
  std::string_view part;
  for (bool more = !whole.empty(); more;) {
    more = more_pieces(part, whole, delim);
    parts.push_back(R{part});
  }
  return parts;
}

// Split a temporary string by delimiters, making deep copies of the parts.
constexpr [[nodiscard]] auto
split(std::string&& whole, const Delim& delim = {}) {
  return split<std::string>(std::string_view(whole), delim);
}

} // namespace splitting
inline namespace targeting {

//
// Appender
//

// Wrapper for appending arbitrary text. Generic ostream specialization.
template<typename T>
class appender {
public:
  explicit appender(T& target_ref) : target_ref_(target_ref) {}

  auto& append(std::string_view sv) {
    target_ref_.write(sv.data(), sv.size());
    return *this;
  }

  auto& append(char ch) {
    target_ref_.put(ch);
    return *this;
  }

  T& operator*() { return target_ref_; }

private:
  T& target_ref_;
};

// String partial specialization.
template<>
class appender<std::string> {
public:
  explicit appender(std::string& target_ref) : target_ref_(target_ref) {}

  auto& append(std::string_view sv) {
    target_ref_.append(sv);
    return *this;
  }

  auto& append(char ch) {
    target_ref_ += ch;
    return *this;
  }

  std::string& operator*() { return target_ref_; }

private:
  std::string& target_ref_;
};

template<typename T>
constexpr bool is_appendable_v =
    std::is_same_v<std::remove_cvref_t<T>, std::string> ||
    std::is_base_of_v<std::ostream, std::remove_cvref_t<T>>;

template<typename T>
auto make_appender(T& t) {
  using U = std::remove_cvref_t<T>;
  if constexpr (is_string_view_convertible_v<U>)
    return appender<std::string>(t);
  else
    return appender<U>(t);
}

template<typename A, enable_if_0<is_appendable_v<A>> = 0>
auto& test_append(A& target, std::string_view part) {
  auto a = make_appender(target);
  a.append(part);
  return *a;
}

} // namespace targeting
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
bool extract_num(T& t, std::string_view& sv) {
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
std::optional<T> extract_num(std::string_view& sv) {
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
std::optional<T> parse_num(std::string_view sv) {
  return extract_num<base>(sv);
}

// Parse integer from copy of a `std::string_view` with a `default_value`.
//
// On success, returns parsed value.
//
// On failure, returns `default_value`.
template<typename T = int64_t, int base = 10,
    enable_if_0<is_integral_number_v<T>> = 0>
T parse_num(std::string_view sv, T default_value) {
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
bool extract_num(T& t, std::string_view& sv) {
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
std::optional<T> extract_num(std::string_view& sv) {
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
std::optional<T> parse_num(std::string_view sv) {
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
T parse_num(std::string_view sv, T default_value) {
  T t;
  return extract_num<fmt>(t, sv) ? t : default_value;
}

//
// To string.
//

// Append integral number to `target`. Hex is prefixed with "0x" and
// zero-padded to an appropriate size.
template<int base = 10, size_t width = 0, char pad = ' ', typename T,
    enable_if_0<is_integral_number_v<T>> = 0>
std::string& append_num(std::string& target, T num) {
  std::array<char, 64> b;
  if (auto [ptr, ec] = std::to_chars(b.data(), b.data() + b.size(), num, base);
      ec == std::errc())
  {
    size_t len = ptr - b.data();
    if constexpr ((width && pad) || base == 16) {
      auto w = width;
      auto p = pad;
      if constexpr (base == 16 && !width) {
        target.append("0x"sv);
        p = '0';
        w = sizeof(T) * 2;
      }
      if (len < w) target.append(w - len, p);
    }
    target.append(b.data(), len);
  }
  return target;
}

// Return integral number as string.
template<int base = 10, size_t width = 0, char pad = ' ', typename T,
    enable_if_0<is_integral_number_v<T>> = 0>
std::string num_as_string(T num) {
  std::string target;
  return append_num<base, width, pad>(target, num);
  return target;
}

// Append floating-point number to `target`.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' ', typename T,
    enable_if_0<is_floating_number_v<T>> = 0>
auto& append_num(std::string& target, T num) {
  std::array<char, 64> b;
  std::to_chars_result res;
  if constexpr (precision != -1)
    res = std::to_chars(b.data(), b.data() + b.size(), num, fmt, precision);
  else
    res = std::to_chars(b.data(), b.data() + b.size(), num, fmt);
  if (auto [ptr, ec] = res; ec == std::errc()) {
    size_t len = ptr - b.data();
    if constexpr (width && pad)
      if (len < width) target.append(width - len, pad);
    target.append(b.data(), len);
  }
  return target;
}

// Return floating-point number as string.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' ', typename T,
    enable_if_0<is_floating_number_v<T>> = 0>
std::string num_as_string(T num) {
  std::string target;
  return append_num<fmt, precision, width, pad>(target, num);
  return target;
}

inline namespace enumprint {

//
// enumprint
//

namespace details {

// default_enum_printer
template<typename T>
struct default_enum_printer {
  std::string& append(std::string& target, T t) const {
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
template<typename T, enable_if_0<is_enum_v<T>> = 0>
std::string& append_enum(std::string& target, T t) {
  return enum_printer_v<T>.append(target, t);
}

// Return enum as string.
template<typename T, enable_if_0<is_enum_v<T>> = 0>
std::string enum_as_string(T t) {
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
// destination value.

// TODO: Maybe supplement replace with remove and remove_any.

// TODO: Maybe make `log` and such thread-safe? It's not really intended to be
// a full, production logging system, so this might be overkill.

// TODO: Add a `strings::Target` class that binds to `std::ostream&` or to
// `std::string_view` and allows `append` of a `std::string_view`. (Maybe
// support `const char*, size_t` just to avoid the issue of `nullptr`.)
// Anyhow, use the Target class instead of `std::string& target` everywhere.
// Perhaps deal with the issue of determining whether it's empty. This is
// trivial for `std::string` and perhaps possible for `std::stringstream`, but
// maybe we're just doing it wrong. Maybe, for non-strings, we need to always
// say we're not empty, requiring the top-level call to suppress the leading
// delimiter explicitly. Or maybe add a bool that starts as false but is set
// true after the first write. Think it through.

// TODO: maybe an op<< for enum?

// TODO: Wacky idea: overload unary `operator+` for `std::string_view` to mean
// non-empty.

} // namespace corvid::strings
