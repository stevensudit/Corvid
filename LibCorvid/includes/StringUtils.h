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
#include "BitmaskEnum.h"

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

  enum class emit { prevent, allow, force };

  // Append when `allow` and the `target` isn't empty, or when `force`.
  template<auto e = emit::allow>
  constexpr auto& append_maybe(std::string& target) const {
    if constexpr (e == emit::force)
      target.append(*this);
    else if constexpr (e == emit::allow) {
      if (!target.empty()) target.append(*this);
    }
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
inline namespace conversion {

//
// Numerical conversions
//

// To int.

// Extract integer out of a `std::string_view`, setting output parameter.
//
// Skips leading white space, accepts leading minus sign, and does not accept
// "0x" or "0X", even when `base` is 16. (This is true for all of these related
// functions.)
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

// Extract integer from a `std::string_view`, returning it as `std::optional`.
//
// On success, returns optional with value, and removes parsed characters from
// the string view.
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
// On failure, leaves output value alone, possibly removes some characters from
// the string view, and returns false.
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
// On success, returns optional with value, and removes parsed characters from
// the string view.
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

// Parse floating-point from copy of `std::string_view` with a `default_value`.
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

} // namespace conversion
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
inline namespace appending {

//
// Append, Concat, and Join
//

// The `append`, `append_join`, and `append_join_with` functions take a
// `target` string as the first parameter and append the rest to it.
//
// The `concat`, `join`, and `join_with` functions take the pieces and return
// the whole as a string.
//
// For the `append_join_with` and `join_with` functions, the parameter right
// after `target` is interpreted as the delimiter to separate the other values
// with. The `append_join` and `join` functions instead default the delimiter
// to ", ".
//
// All of the joining function can have `braces_opt` specified to control
// whether container elements are surrounded with appropriate braces and
// whether a delimiter should be emitted at the start; see enum definition
// below for description.
//
// The supported types for the pieces include: `std::string`,
// `std::string_view`, `const char*`, `char`, `bool`, `int`, `double`, `enum`,
// and containers.
//
// Containers include `std::pair`, `std::tuple`, `std::initializer_list`, and
// anything you can do a ranged-for over, such as `std::vector`. For keyed
// containers, such as `std::map`, only the values are used, unless
// `braces_opt` specifies otherwise. Containers may be nested arbitrarily.
//
// In addition to `int` and `double`, all other native numeric types are
// supported.
//
// Pointers and `std::optional` are dereferenced if a value is available. To
// instead show the address of a pointer in hex, cast it to `void*`, `intptr_t`
// or `uintptr_t`.
//
// Any other type can be supported by adding your own overload of `append`
// (and, if it needs to support internal delimiters, `append_join_with`).

// Append one stringlike thing to `target`.
template<typename T, enable_if_0<is_string_view_convertible_v<T>> = 0>
constexpr auto& append(std::string& target, const T& part) {
  if constexpr (is_char_ptr_v<T>)
    return target.append(part ? std::string_view{part} : std::string_view{});
  else
    return target.append(part);
}

// Append one integral number (or `char`) to `target`. When called directly,
// `base` may be specified.
template<int base = 10, typename T, enable_if_0<is_integral_number_v<T>> = 0>
constexpr auto& append(std::string& target, const T& part) {
  auto radix = base;
  if constexpr (std::is_same_v<T, char>) {
    return target.append(1U, part);
  } else {
    if constexpr (std::is_same_v<T, intptr_t> || std::is_same_v<T, uintptr_t>)
      radix = 16;

    std::array<char, 64> b;
    auto [ptr, ec] = std::to_chars(b.data(), b.data() + b.size(), part, radix);
    if (ec == std::errc()) target.append(b.data(), ptr - b.data());
    return target;
  }
}

// Append one floating-point number to `target`. When called directly, `fmt`
// and `precision` may be specified.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, typename T, enable_if_0<is_floating_number_v<T>> = 0>
constexpr auto& append(std::string& target, const T& part) {
  std::array<char, 64> b;
  std::to_chars_result res;
  if constexpr (precision != -1)
    res = std::to_chars(b.data(), b.data() + b.size(), part, fmt, precision);
  else
    res = std::to_chars(b.data(), b.data() + b.size(), part, fmt);
  if (auto [ptr, ec] = res; ec == std::errc())
    target.append(b.data(), ptr - b.data());
  return target;
}

// Append one pointer or optional value to `target`.
template<typename T, enable_if_0<is_optional_like_v<T>> = 0>
constexpr std::string& append(std::string& target, const T& part) {
  if (part) append(target, *part);
  return target;
}

// Append one void pointer, as hex, to `target`.
template<typename T, enable_if_0<is_void_ptr_v<T>> = 0>
constexpr auto& append(std::string& target, const T& part) {
  return append(target, reinterpret_cast<uintptr_t>(part));
}

// Append one `bool`, as `int`, to `target`.
template<typename T, enable_if_0<is_bool_v<T>> = 0>
constexpr auto& append(std::string& target, const T& part) {
  return append(target, static_cast<int>(part));
}

// Append one scoped or unscoped `enum`, as underlying integer, to `target`.
template<typename T, enable_if_0<is_enum_v<T>> = 0>
constexpr auto& append(std::string& target, const T& part) {
  return append(target, as_underlying(part));
}

// Append one container, as its element values, to `target` without
// delimiters. See `append_join_with` for delimiter support. When called
// directly, `keyed` may be specified.
template<typename T, bool keyed = false, enable_if_0<is_container_v<T>> = 0>
constexpr auto& append(std::string& target, const T& parts) {
  for (auto& part : parts) append(target, container_element_v<keyed>(&part));
  return target;
}

// Append pieces to `target` without delimiters. See `append_join_with` for
// delimiter support.
template<typename Head, typename Middle, typename... Tail>
constexpr auto& append(std::string& target, const Head& head,
    const Middle& middle, const Tail&... tail) {
  append(append(target, head), middle);
  if constexpr (sizeof...(tail) != 0) append(target, tail...);
  return target;
}

// Append one `std::tuple` or `std::pair`, as its elements, to `target`
// without delimiters. See `append_join_with` for delimiter support.
template<template<typename...> typename T, typename... Ts,
    enable_if_0<is_tuple_equiv_v<T<Ts...>>> = 0>
constexpr auto& append(std::string& target, const T<Ts...>& parts) {
  std::apply(
      [&target](const Ts&... parts) {
        if constexpr (sizeof...(parts) != 0) append(target, parts...);
      },
      parts);
  return target;
}

// Append one variant to `target`, as its current type.
template<typename T, enable_if_0<is_variant_v<T>> = 0>
constexpr auto& append(std::string& target, const T& part) {
  if (!part.valueless_by_exception()) {
    std::visit([&target](auto&& part) { append(target, part); }, part);
  }
  return target;
}

// Concatenate pieces together into `std::string` without delimiters. See
// `join` and `join_with` for delimiter support.
template<typename Head, typename... Tail>
constexpr [[nodiscard]] auto concat(const Head& head, const Tail&... tail) {
  std::string target;
  append(target, head, tail...);
  return target;
}

// Make integer into `std::string`.
template<int base = 10, typename T, enable_if_0<is_integral_number_v<T>> = 0>
constexpr [[nodiscard]] auto from_num(const T& t) {
  std::string target;
  if constexpr (std::is_same_v<T, char>) {
    append<base>(target, static_cast<int8_t>(t));
  } else {
    append<base>(target, t);
  }
  return target;
}

// Make floating point into `std::string`.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, typename T, enable_if_0<is_floating_number_v<T>> = 0>
constexpr [[nodiscard]] auto from_num(const T& t) {
  std::string target;
  append<fmt, precision>(target, t);
  return target;
}

namespace details {

// Internal join state and option flags.
enum class join_flags {
  // prevent brace - Avoid surrounding containers with braces.
  prevent_brace = 1,
  // prevent delimit - Avoid prefixing with delimiter. True after
  // open brace, but not propagated. Delimiting requires this to be false and
  // and target to be non-empty (unless delimit_empty).
  prevent_delimit = 2,
  // delimit_empty - Avoid checking that target is empty before delimiting.
  // Does not override `prevent_delimit`. This is primarily an optimization.
  delimit_empty = 4,
  // show_key - Show keys in collections, in addition to values.
  show_key = 8
};

} // namespace details
} // namespace appending
} // namespace corvid::strings

// Register join_flags as bitmask.
template<>
constexpr size_t corvid::bit_count_v<corvid::strings::details::join_flags> = 4;

namespace corvid::strings {
inline namespace appending {

// Braces options for join methods.
enum class braces_opt {
  // Allow braces around containers.
  braced,
  // Prevent braces.
  flat = *details::join_flags::prevent_brace,
  // Merge with previous contents of target, skipping leading delimiter.
  merged = *details::join_flags::prevent_delimit,
  // Show key and values in braces, instead of just values.
  keyed = *details::join_flags::show_key
};

namespace details {

// Determine whether to add braces.
template<braces_opt opt, char open, char close>
constexpr bool braces_v =
    missing(join_flags(opt), join_flags::prevent_brace) && (open && close);

// Calculate next opt for head. If adding braces, don't delimit.
template<braces_opt opt, bool add_braces>
constexpr braces_opt head_opt_v = braces_opt(
    set_to(join_flags(opt), join_flags::prevent_delimit, add_braces));

// Calculate next opt given the previous one.
template<braces_opt opt>
constexpr braces_opt next_opt_v = braces_opt(
    join_flags(opt) - join_flags::prevent_delimit + join_flags::delimit_empty);

// Calculate delimiter option.
template<braces_opt opt>
constexpr Delim::emit delimit_v =
    has(join_flags(opt), join_flags::prevent_delimit) ? Delim::emit::prevent
    : has(join_flags(opt), join_flags::delimit_empty)
        ? Delim::emit::force
        : Delim::emit::allow;

// Determine whether to show keys.
template<braces_opt opt>
constexpr bool keyed_v = has(join_flags(opt), join_flags::show_key);

} // namespace details

// Append one piece to `target`, joining with `delim`.
template<auto opt = braces_opt::braced, char open = 0, char close = 0,
    typename T,
    enable_if_0<!is_container_v<T> && !is_variant_v<T> &&
                !is_optional_like_v<T>> = 0>
constexpr auto&
append_join_with(std::string& target, const Delim& delim, const T& part) {
  constexpr bool add_braces = details::braces_v<opt, open, close>;
  delim.append_maybe<details::delimit_v<opt>>(target);
  if constexpr (add_braces) append(target, open);
  append(target, part);
  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one pointer or optional value to `target`, joining with `delim`.
template<auto opt = braces_opt::braced, char open = 0, char close = 0,
    typename T, enable_if_0<is_optional_like_v<T>> = 0>
constexpr auto&
append_join_with(std::string& target, const Delim& delim, const T& part) {
  if (part) append_join_with<opt>(target, delim, *part);
  return target;
}

// Append one variant to `target`, as its current type, joining with `delim`.
template<auto opt = braces_opt::braced, char open = 0, char close = 0,
    typename T, enable_if_0<is_variant_v<T>> = 0>
constexpr auto&
append_join_with(std::string& target, const Delim& delim, const T& part) {
  if (!part.valueless_by_exception()) {
    std::visit(
        [&target, &delim](auto&& part) {
          append_join_with<opt>(target, delim, part);
        },
        part);
  }
  return target;
}

// Append one container, as its element values, to `target`, joining with
// `delim`.
template<auto opt = braces_opt::braced, char open = '[', char close = ']',
    typename T, enable_if_0<is_container_v<T>> = 0>
constexpr auto&
append_join_with(std::string& target, const Delim& delim, const T& parts) {
  constexpr bool add_braces = details::braces_v<opt, open, close>;
  constexpr auto head_opt = details::head_opt_v<opt, add_braces>;
  constexpr auto next_opt = details::next_opt_v<opt>;
  constexpr bool keyed = details::keyed_v<opt>;
  if constexpr (add_braces)
    append(delim.append_maybe<details::delimit_v<opt>>(target), open);

  if (auto b = std::cbegin(parts), e = std::cend(parts); b != e) {
    append_join_with<head_opt>(target, delim, container_element_v<keyed>(b));

    for (++b; b != e; ++b)
      append_join_with<next_opt>(target, delim, container_element_v<keyed>(b));
  }

  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one `std::tuple` or `std::pair`, as its elements, to `target`,
// joining with `delim`.
template<auto opt = braces_opt::braced, char open = 0, char close = 0,
    template<typename...> typename T, typename... Ts,
    enable_if_0<is_tuple_equiv_v<T<Ts...>>> = 0>
constexpr auto& append_join_with(std::string& target, const Delim& delim,
    const T<Ts...>& parts) {
  constexpr bool is_pair = is_pair_v<decltype(parts)>;
  constexpr auto next_open = open ? open : (is_pair ? '(' : '{');
  constexpr auto next_close = close ? close : (is_pair ? ')' : '}');

  std::apply(
      [&target, &delim](const Ts&... parts) {
        if constexpr (sizeof...(parts) != 0)
          append_join_with<opt, next_open, next_close>(target, delim,
              parts...);
      },
      parts);
  return target;
}

namespace details {
template<braces_opt opt, char open = 0, char close = 0, typename Head,
    typename... Tail>
constexpr auto& ajwh(std::string& target, const Delim& delim, const Head& head,
    const Tail&... tail) {
  constexpr auto next_opt = details::next_opt_v<opt>;
  append_join_with<opt>(target, delim, head);
  if constexpr (sizeof...(tail) != 0) ajwh<next_opt>(target, delim, tail...);
  return target;
}
} // namespace details

// Append pieces to `target`, joining with `delim`.
template<auto opt = braces_opt::braced, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr auto& append_join_with(std::string& target, const Delim& delim,
    const Head& head, const Tail&... tail) {
  constexpr bool add_braces = details::braces_v<opt, open, close>;
  constexpr auto head_opt = details::head_opt_v<opt, add_braces>;
  constexpr auto next_opt = details::next_opt_v<opt>;
  if constexpr (add_braces)
    append(delim.append_maybe<details::delimit_v<opt>>(target), open);

  append_join_with<head_opt>(target, delim, head);

  if constexpr (sizeof...(tail) != 0)
    details::ajwh<next_opt>(target, delim, tail...);

  if constexpr (add_braces) append(target, close);
  return target;
}

// Append pieces to `target`, joining with a comma and space delimiter.
template<auto opt = braces_opt::braced, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr auto&
append_join(std::string& target, const Head& head, const Tail&... tail) {
  constexpr Delim delim{", "sv};
  if constexpr (details::braces_v<opt, open, close>)
    return append_join_with<opt, open, close>(target, delim, head, tail...);
  else
    return append_join_with<opt>(target, delim, head, tail...);
}

// Join pieces together, with `delim`, into `std::string`.
template<auto opt = braces_opt::merged, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr [[nodiscard]] auto
join_with(const Delim& delim, const Head& head, const Tail&... tail) {
  std::string target;
  if constexpr (details::braces_v<opt, open, close>)
    append_join_with<opt, open, close>(target, delim, head, tail...);
  else
    append_join_with<opt>(target, delim, head, tail...);
  return target;
}

// Join pieces together, comma-delimited, into `std::string`.
template<auto opt = braces_opt::merged, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr [[nodiscard]] auto join(const Head& head, const Tail&... tail) {
  std::string target;
  constexpr Delim delim{", "sv};
  if constexpr (details::braces_v<opt, open, close>)
    append_join_with<opt, open, close>(target, delim, head, tail...);
  else
    append_join_with<opt>(target, delim, head, tail...);
  return target;
}

} // namespace appending
inline namespace bracing {

//
// Braces
//

// For braces, the `Delim` is interpreted as a pair of characters.

// Trim off matching braces, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto
trim_braces(std::string_view whole, const Delim& braces = {"[]"}) {
  auto front = braces.front();
  auto back = braces.back();
  if (whole.size() > 1 && whole.front() == front && whole.back() == back) {
    whole.remove_prefix(1);
    whole.remove_suffix(1);
  }
  return R{whole};
}

// Add braces.
constexpr [[nodiscard]] auto
add_braces(std::string_view whole, const Delim& braces = {"[]"}) {
  return concat(braces.front(), whole, braces.back());
}

} // namespace bracing

// TODO: Consider breaking out some inline namespaces into their own headers.

// TODO: Add method that takes pieces and counts up their total (estimated)
// size, for the purpose of reserving target capacity.

// TODO: Get extract_num to work with cstring_view cleanly. It's safe because
// we only remove the prefix, never the suffix. The brute-force solution is to
// add overloads for the extract methods.

// TODO: Maybe add a replace_any that replaces any matching chars with the
// destination value.

// TODO: Maybe supplement replace with remove and remove_any.

// TODO: Maybe make `log` and such thread-safe? It's not really intended to be
// a full, production logging system, so this might be overkill.

// TODO: Benchmark Delim `find` single-char optimizations, to make sure they're
// faster.

} // namespace corvid::strings
