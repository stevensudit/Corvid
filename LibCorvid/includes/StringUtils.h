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
#include <array>
#include <charconv>
#include <string>
#include <type_traits>
#include <cstdint>

#include "Meta.h"
#include "Bitmask.h"

// Recommendation: Import the `corvid` namespace, but not `corvid::strings`,
// allowing you to make calls like `strings::concat()` while avoiding unwanted
// symbols, such as `braces_opt`.
namespace corvid::strings {
using namespace std::literals;

// Emulates C++20's `std::string_view::starts_with`.
constexpr [[nodiscard]] bool starts_with(std::string_view whole,
    std::string_view part) noexcept {
  return whole.compare(0, part.size(), part) == 0;
}

// Emulates C++20's `std::string_view::ends_with`.
constexpr [[nodiscard]] bool ends_with(std::string_view whole,
    std::string_view part) noexcept {
  if (whole.size() < part.size()) return false;
  return whole.compare(whole.size() - part.size(), part.size(), part) == 0;
}

// Delimiter wrapper.
//
// This class is not intended for use outside of this header. While it provides
// some utility, it is very limited, and the primary purpose is to document the
// delimiter parameters with a type.
//
// The precise meaning varies depending upon context:
// - When splitting, checks for any of the characters.
// - When joining, appends the entire string.
// - When manipulating braces, treated as open/close pair.
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

  template<bool show = true>
  constexpr auto& append_join_to(std::string& target) const {
    if constexpr (show)
      if (!target.empty()) target.append(*this);
    return target;
  }
};

// Extract next delimited piece destructively from `whole`.
//
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
constexpr [[nodiscard]] R extract_piece(std::string_view& whole,
    const Delim& delim = {}) {
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
constexpr [[nodiscard]] bool more_pieces(R& part, std::string_view& whole,
    const Delim& delim = {}) {
  auto all = whole.size();
  part = extract_piece<R>(whole, delim);
  return part.size() != all;
}

// Split all pieces by delimiters and return parts in vector.
//
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto split(std::string_view whole,
    const Delim& delim = {}) {
  std::vector<R> parts;
  std::string_view part;
  for (bool more = !whole.empty(); more;) {
    more = more_pieces(part, whole, delim);
    parts.push_back(R{part});
  }

  return parts;
}

// Split a temporary string by delimiters, making deep copies of the parts.
constexpr [[nodiscard]] auto split(std::string&& whole,
    const Delim& delim = {}) {
  return split<std::string>(std::string_view(whole), delim);
}

// Trim whitespace on left, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto trim_left(std::string_view whole,
    const Delim& ws = {}) {
  auto pos = ws.find_not_in(whole);
  std::string_view part;
  if (pos != -1) part = whole.substr(pos);
  return R{part};
}

// Trim whitespace on right, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto trim_right(std::string_view whole,
    const Delim& ws = {}) {
  auto pos = ws.find_last_not_in(whole);
  auto part = whole.substr(0, pos + 1);
  return R{part};
}

// Trim whitespace, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto trim(std::string_view whole,
    const Delim& ws = {}) {
  return trim_right<R>(trim_left(whole, ws), ws);
}

// Trim vector in place.
template<typename S>
constexpr void trim(std::vector<S>& wholes, const Delim& ws = {}) {
  for (auto& whole : wholes) whole = trim<S>(whole, ws);
}

// Trim a temporary vector, passing it through.
//
// Ideal for calling directly on the result of split.
template<typename S>
constexpr [[nodiscard]] auto&& trim(std::vector<S>&& wholes,
    const Delim& ws = {}) {
  trim(wholes, ws);
  return wholes;
}

// Append/Concat.
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
// to a comma with a space.
//
// All of the joining function can have `braces_opt` specified to control
// whether container elements are surrounded with appropriate braces; see below
// for description.
//
// The supported types for the pieces include: `std::string`,
// `std::string_view`, `char*`, `char`, `bool`, `int`, `double`, `enum`, and
// containers.
//
// Containers include `std::pair`, `std::tuple`, initializer lists, and
// anything you can do a ranged-for over, such as `std::vector`. For keyed
// containers, such as `std::map`, only the values are used. Containers may be
// nested arbitrarily.
//
// In addition to `int` and `double`, all other native numeric types are
// supported, and pointers are shown in hex. Any other type can be supported by
// adding your own overload of `append` (and, if it should support internal
// delimiters, `append_join_with`).

// Append one stringlike thing to `target`.
template<typename T,
    std::enable_if_t<is_string_view_convertible_v<T>, bool> = true>
constexpr auto& append(std::string& target, const T& part) {
  return target.append(part);
}

// Append one integral number (or `char`) to `target`. When called directly,
// `base` may be specified.
template<int base = 10, typename T,
    std::enable_if_t<is_integral_number_v<T>, bool> = true>
constexpr auto& append(std::string& target, const T& part) {
  if constexpr (std::is_same_v<T, char>) {
    return append(target, std::string_view(&part, 1));
  } else {
    std::array<char, 64> b;
    auto [ptr, ec] = std::to_chars(b.data(), b.data() + b.size(), part, base);
    if (ec == std::errc()) target.append(b.data(), ptr - b.data());
    return target;
  }
}

// Append one floating-point number to `target`. When called directly, `fmt`
// and `precision` may be specified.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, typename T,
    std::enable_if_t<is_floating_number_v<T>, bool> = true>
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

// Append one pointer, as hex, to `target`.
template<typename T,
    std::enable_if_t<is_dereferenceable_v<T> && !is_enum_v<T> &&
                         !is_string_view_convertible_v<T>,
        bool> = true>
constexpr std::string& append(std::string& target, const T& part) {
  const void* ptr = &*part;
  auto n = reinterpret_cast<uintptr_t>(ptr);
  return append<16>(target, n);
}

// Append one `bool`, as `int`, to `target`.
template<typename T, std::enable_if_t<is_bool_v<T>, bool> = true>
constexpr auto& append(std::string& target, const T& part) {
  return append(target, static_cast<int>(part));
}

// Append one scoped or unscoped `enum`, as underlying integer, to `target`.
template<typename T, std::enable_if_t<is_enum_v<T>, bool> = true>
constexpr auto& append(std::string& target, const T& part) {
  return append(target, static_cast<std::underlying_type_t<T>>(part));
}

// Append one container, as its element values, to `target`.
template<typename T, std::enable_if_t<is_container_v<T>, bool> = true>
constexpr auto& append(std::string& target, const T& parts) {
  for (auto& part : parts) append(target, container_element_v(&part));
  return target;
}

// Append pieces to `target`.
template<typename Head, typename Middle, typename... Tail>
constexpr auto& append(std::string& target, const Head& head,
    const Middle& middle, const Tail&... tail) {
  append(append(target, head), middle);
  if constexpr (sizeof...(tail) != 0) append(target, tail...);
  return target;
}

// Append one `std::tuple` or `std::pair`, as its elements, to `target`.
template<template<typename...> typename T, typename... Ts,
    std::enable_if_t<is_tuple_equiv_v<T<Ts...>>, bool> = true>
constexpr auto& append(std::string& target, const T<Ts...>& parts) {
  std::apply(
      [&target](const Ts&... parts) {
        if constexpr (sizeof...(parts) != 0) append(target, parts...);
      },
      parts);
  return target;
}

// Concatenate pieces together into `std::string`.
template<typename Head, typename... Tail>
constexpr [[nodiscard]] auto concat(const Head& head, const Tail&... tail) {
  std::string target;
  append(target, head, tail...);
  return target;
}

namespace details {

// Internal join state flags.
enum class join_flags {
  // inside - In nested scope, as opposed to top-level.
  inside = 1,
  // brace - Whether to surround containers with braces if inside.
  brace = 2,
  // delimit - Whether to prefix with delimiter. False after open brace.
  delimit = 4
};

} // namespace details
} // namespace corvid::strings

template<>
constexpr size_t
    corvid::bitmask::bit_count_v<corvid::strings::details::join_flags> = 3;

namespace corvid::strings {

// Braces options for join methods.
enum class braces_opt {
  // nested - Add braces to nested containers.
  nested = *(details::join_flags::brace | details::join_flags::delimit),
  // forced - Force braces at top level, and for nested containers.
  forced = *(details::join_flags::inside | details::join_flags::brace),
  // flat - Suppress braces around containers.
  flat = *(details::join_flags::delimit)
};

namespace details {

// Determine whether to add braces.
template<braces_opt opt, char open, char close>
constexpr bool braces_v = bitmask::contains(join_flags(opt),
                              join_flags::brace | join_flags::inside) &&
                          (open && close);
// Calculate next opt given the previous one.
template<braces_opt opt>
constexpr braces_opt next_opt_v = braces_opt(
    join_flags(opt) | join_flags::inside | join_flags::delimit);

// Calculate next op for head.
template<braces_opt next_opt, bool add_braces>
constexpr braces_opt head_opt_v =
    add_braces
        ? braces_opt(bitmask::clear(join_flags(next_opt), join_flags::delimit))
        : next_opt;

// Determine whether to emit delimiter.
template<braces_opt opt>
constexpr bool delimit_v = bitmask::overlaps(join_flags(opt),
    join_flags::delimit);

} // namespace details

// Append one piece to `target`, joining with `delim`.
template<auto opt = braces_opt::nested, char open = 0, char close = 0,
    typename T, std::enable_if_t<!is_container_v<T>, bool> = true>
constexpr auto& append_join_with(std::string& target, const Delim& delim,
    const T& part) {
  constexpr bool add_braces = details::braces_v<opt, open, close>;
  delim.append_join_to<details::delimit_v<opt>>(target);
  if constexpr (add_braces) append(target, open);
  append(target, part);
  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one container, as its element values, to `target`, joining with
// `delim`.
template<auto opt = braces_opt::nested, char open = '[', char close = ']',
    typename T, std::enable_if_t<is_container_v<T>, bool> = true>
constexpr auto& append_join_with(std::string& target, const Delim& delim,
    const T& parts) {
  constexpr bool add_braces = details::braces_v<opt, open, close>;
  constexpr auto next_opt = details::next_opt_v<opt>;
  constexpr auto head_opt = details::head_opt_v<next_opt, add_braces>;
  if constexpr (add_braces)
    append(delim.append_join_to<details::delimit_v<opt>>(target), open);

  if (auto b = std::cbegin(parts), e = std::cend(parts); b != e) {
    append_join_with<head_opt>(target, delim, container_element_v(b));

    for (++b; b != e; ++b)
      append_join_with<next_opt>(target, delim, container_element_v(b));
  }

  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one `std::tuple` or `std::pair`, as its elements, to `target`,
// joining with `delim`.
template<auto opt = braces_opt::nested, char open = 0, char close = 0,
    template<typename...> typename T, typename... Ts,
    std::enable_if_t<is_tuple_equiv_v<T<Ts...>>, bool> = true>
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
template<auto opt = braces_opt::nested, char open = '{', char close = '}',
    typename Head, typename... Tail>
constexpr auto& append_join_with(std::string& target, const Delim& delim,
    const Head& head, const Tail&... tail) {
  constexpr bool add_braces = details::braces_v<opt, open, close>;
  constexpr auto next_opt = details::next_opt_v<opt>;
  constexpr auto head_opt = details::head_opt_v<next_opt, add_braces>;
  if constexpr (add_braces)
    append(delim.append_join_to<details::delimit_v<opt>>(target), open);

  append_join_with<head_opt>(target, delim, head);

  if constexpr (sizeof...(tail) != 0)
    details::ajwh<next_opt>(target, delim, tail...);

  if constexpr (add_braces) append(target, close);
  return target;
}

// Append pieces to `target`, joining with a comma and space delimiter.
template<auto opt = braces_opt::nested, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr auto& append_join(std::string& target, const Head& head,
    const Tail&... tail) {
  if constexpr (details::braces_v<opt, open, close>)
    return append_join_with<opt, open, close>(target, ", "sv, head, tail...);
  else
    return append_join_with<opt>(target, ", "sv, head, tail...);
}

// Join pieces together, with `delim`, into `std::string`.
template<auto opt = braces_opt::nested, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr [[nodiscard]] auto join_with(const Delim& delim, const Head& head,
    const Tail&... tail) {
  std::string target;
  if constexpr (details::braces_v<opt, open, close>)
    append_join_with<opt, open, close>(target, delim, head, tail...);
  else
    append_join_with<opt>(target, delim, head, tail...);
  return target;
}

// Join pieces together, comma-delimited, into `std::string`.
template<auto opt = braces_opt::nested, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr [[nodiscard]] auto join(const Head& head, const Tail&... tail) {
  std::string target;
  if constexpr (details::braces_v<opt, open, close>)
    append_join_with<opt, open, close>(target, ", "sv, head, tail...);
  else
    append_join_with<opt>(target, ", "sv, head, tail...);
  return target;
}

// Trim off matching braces, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto trim_braces(std::string_view whole,
    const Delim& braces = {"[]"}) {
  auto front = braces.front();
  auto back = braces.back();
  if (whole.size() && whole.front() == front && whole.back() == back) {
    whole.remove_prefix(1);
    whole.remove_suffix(1);
  }
  return R{whole};
}

// Add braces.
constexpr [[nodiscard]] auto add_braces(std::string_view whole,
    const Delim& braces = {"[]"}) {
  return concat(braces.front(), whole, braces.back());
}

// TODO: Add method that takes pieces and counts up their total (estimated)
// size, for the purpose of reserving target capacity.

// TODO: Consider supporting `std::optional` or even providing optional-like
// support for pointers instead of emitting as hex. Basically, if it can be
// dereferenced and coerced to bool, we can optionally dereference it.

// TODO: Allow trimming an entire container, values only.

// TODO: Consider offering an overload that causes keys to be emitted alongside
// values. Really only makes sense for join methods, so maybe add it to the
// enum and handle it in the container overload.

} // namespace corvid::strings
