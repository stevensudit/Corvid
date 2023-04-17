// Corvid20: A general-purpose C++20 library extending std.
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

// Search and replace, except `search` and `find` and `replace` are words used
// `std`, so we  had to substitute `locate` and `substitute` to avoid conflicts
// (despite namespaces). And, of course, any possible royalties.
//
// The support for multiple values is distinct from calling once for each
// value, in that the `to` will never be treated as a `from`.
//
// Note:
// For functions which accept a `std::initializer_list<std::string_view>`, it's
// fine to call them with a `std::initializer_list<const char*>`. In other
// words, it's ok for the elements to be regular string literals.
//
// However, if you assign that same list to a variable, it will fail with a
// compiler error. One solution is to replace -- uhm, I mean substitute -- the
// string literals with `std::string_view` literals. You could also use a
// `std::span` or `std::array`.

namespace corvid::strings { inline namespace locating {

// A single value to locate, which can be a `char` or something that converts
// to a `std::string_view`. Contrast with lists of these values.
template<typename T>
concept SingleLocateValue = StringViewConvertible<T> || is_char_v<T>;

namespace convert {

// Convert a list of char values to a `std::span`, including identity. Does not
// convert any strings or string-like things.
// TODO: Add test to confirm that string literals are not treated as char
// arrays.
// TODO: Make sure it works for std::array.
// TODO: Once everything works, simplify this.
inline constexpr std::span<const char> as_span(
    std::initializer_list<char> values) noexcept {
  return {values.begin(), values.end()};
}
inline constexpr std::span<const char> as_span(
    std::span<const char> values) noexcept {
  return values;
}

// Convert a list of string-like values to a `std::span`, including identity.
// TODO: Make this function work for StringViewConvertible.
// TODO: Make sure it works for std::array.
inline constexpr std::span<const std::string_view> as_span(
    std::initializer_list<std::string_view> values) noexcept {
  return {values.begin(), values.end()};
}
inline constexpr std::span<const std::string_view> as_span(
    std::span<const std::string_view> values) noexcept {
  return values;
}
} // namespace convert

// For `locate` on a list of values, the `location` is used to return both the
// index of where the value was located in the target and the index (in the
// value list) of which value was located. Also used as state for `located`.
// The two loc are set to `npos` when nothing was located.
struct location {
  size_t pos{};
  size_t pos_value{};

  constexpr auto operator<=>(const location&) const noexcept = default;
};

// Size of a single value.
constexpr size_t value_size(const SingleLocateValue auto& value) noexcept {
  if constexpr (is_char_v<decltype(value)>)
    return 1;
  else
    return std::string_view{value}.size();
}

// Smallest size of a list of values. If no values, returns 0.
inline constexpr size_t min_value_size(
    const std::span<const std::string_view> values) noexcept {
  auto smallest = std::ranges::min_element(values,
      [](const auto& a, const auto& b) { return a.size() < b.size(); });
  if (smallest != values.end()) return smallest->size();
  return 0;
}

//
// Locate
//

// Updates the `location` to point past the value that was just located,
// returning it as well. This can be used with `located` to loop over located
// values. Note that you cannot safely use it before the first call to `locate`
// or when `locate` fails because `pos_value` must be in range (and not npos).
template<typename T>
constexpr size_t
point_past(location& loc, std::span<const T> values) noexcept {
  assert(loc.pos_value < values.size());
  loc.pos += value_size(values[loc.pos_value]);
  return loc.pos;
}
template<SingleLocateValue T>
constexpr size_t
point_past(location& loc, std::initializer_list<T> values) noexcept {
  return point_past(loc, convert::as_span(values));
}

// Locate the first instance of a single `value` in `s`, starting at `pos`.
// Returns the index of where the value was located, or npos.
//
// To locate the next instance, call again with the `pos` set to the returned
// `pos` plus the size of the located value. For `char`, the size of the
// located value is just 1. For `std::string_view`, this is its `size`. It can
// be convenient to use `point_past` for this.
constexpr size_t locate(std::string_view s,
    const SingleLocateValue auto& value, size_t pos = 0) noexcept {
  if constexpr (Char<decltype(value)>)
    return s.find(value, pos);
  else
    return s.find(std::string_view{value}, pos);
}

// Locate the first instance of any of the `char` `values` in `s`, starting at
// `pos`.
//
// Returns the `location`, which has both the index of where the value was
// located in the target and the index (in the value list) of which value was
// located. The two loc are set to `npos` when nothing was located.
//
// To locate the next instance, call again with the `pos` set to the returned
// `pos`, incremented past the found value. For `char` values, the size is
// just 1.
//
// Usage:
//   auto [pos, pos_value] = locate(s, {'a', 'b', 'c'});
inline constexpr location locate(std::string_view s,
    std::span<const char> values, size_t pos = 0) noexcept {
  const auto value_sv = std::string_view{values.begin(), values.end()};
  for (; pos < s.size(); ++pos)
    for (size_t pos_value = 0; pos_value < value_sv.size(); ++pos_value)
      if (s[pos] == value_sv[pos_value]) return {pos, pos_value};
  return {s.npos, s.npos};
}
inline constexpr auto locate(std::string_view s,
    std::initializer_list<char> values, size_t pos = 0) noexcept {
  return locate(s, {values.begin(), values.end()}, pos);
}

// Locate the first instance of any of the `std::string_view` `values` in `s`,
// starting at `pos`.
//
// Returns the `location`, which has both the index of where the value was
// located in the target and the index (in the value list) of which value was
// located. The two loc are set to `npos` when nothing was located.
//
// To locate the next instance, call again with the `pos` set to the returned
// `pos`, incremented past the found value. For `std::string_view`, this is its
// `size`, which is most easily found by passing the return value of
// `point_past` as the new `pos`.
//
// Usage:
//   auto [pos, pos_value] = locate(s, {"abc", "def", "ghi"});
inline constexpr struct location locate(std::string_view s,
    std::span<const std::string_view> values, size_t pos = 0) noexcept {
  for (; pos <= s.size(); ++pos)
    for (size_t pos_value = 0; pos_value < values.size(); ++pos_value)
      if (s.substr(pos, values[pos_value].size()) == values[pos_value])
        return {pos, pos_value};
  return {s.npos, s.npos};
}
inline constexpr auto locate(std::string_view s,
    std::initializer_list<std::string_view> values, size_t pos = 0) noexcept {
  return locate(s, std::span{values.begin(), values.end()}, pos);
}

//
// Located
//

// Return whether a single `value` was located in `s`, starting at `pos`,
// and updating `pos` to where it was located. Works for `char` and
// `std::string_view`. The index is set to `npos` when nothing was located.
//
//  To locate the next instance, you must increment `pos` past the located
//  `value`. For `char`, the size is just 1. For `std::string_view`, this is
//  its `size`.
constexpr bool located(size_t& pos, std::string_view s,
    const SingleLocateValue auto& value) noexcept {
  return (pos = locate(s, value, pos)) != s.npos;
}

// Return whether any of the `values` were located in `s`, starting at the
// `pos` in `loc`, and updating both the `pos` and `pos_value`. Ignores the
// initial value of `pos_value`. The two loc are set to `npos` when nothing
// was located.
//
//  To locate the next instance, you must increment `pos` past the located
//  `value`. For `char`, the size of the located value is just 1. For
//  `std::string_view`, this is its `size`, which is most easily found by
//  calling `point_past` on `loc`
constexpr bool located(location& loc, std::string_view s,
    std::span<const char> values) noexcept {
  return (loc = locate(s, values, loc.pos)).pos != s.npos;
}
inline constexpr bool located(location& loc, std::string_view s,
    std::span<const std::string_view> values) noexcept {
  return (loc = locate(s, values, loc.pos)).pos != s.npos;
}
template<typename T>
constexpr bool located(location& loc, std::string_view s,
    std::initializer_list<T> values) noexcept {
  return located(loc, s, convert::as_span(values));
}

//
// count_located
//

// Return count of instances of a single `value` in `s`, starting at
// `pos`. Note that an empty `std::string_view` value causes an infinite
// loop.
size_t count_located(std::string_view s, const SingleLocateValue auto& value,
    size_t pos = 0) noexcept {
  assert(value_size(value) > 0 && "value is empty");
  size_t cnt{};
  while (located(pos, s, value)) ++cnt, pos += value_size(value);
  return cnt;
}

// Return count of instances of any of the `values` in `s`, starting at
// `pos`. Note that empty values or an empty `std::string_view` value causes an
// infinite loop.
size_t count_located(std::string_view s, const auto& values,
    size_t pos = 0) noexcept {
  auto v = convert::as_span(values);
  assert(min_value_size(v) > 0 && "value is empty");
  assert(v.size() > 0 && "values are empty");
  size_t cnt{};
  for (location loc{pos, 0}; located(loc, s, v); ++cnt, point_past(loc, v)) {
  }
  return cnt;
}

//
// Substitute
//

// Substitute all instances of `from` in `s` with `to`, returning count of
// substitutions. Note that an empty `std::string_view` value causes an
// infinite loop.
inline size_t substitute(std::string& s, std::string_view from,
    std::string_view to, size_t pos = 0) noexcept {
  assert(!from.empty() && "from is empty");
  size_t cnt{};
  for (; located(pos, s, from); ++cnt, pos += to.size())
    s.replace(pos, from.size(), to);
  return cnt;
}
inline size_t
substitute(std::string& s, char from, char to, size_t pos = 0) noexcept {
  size_t cnt{};
  for (; located(pos, s, from); ++cnt, ++pos) s[pos] = to;
  return cnt;
}
inline size_t substitute(std::string& s, std::span<const char> from,
    std::span<const char> to, size_t pos = 0) {
  assert(from.size() == to.size());
  assert(from.size() > 0 && "from is empty");
  size_t cnt{};
  for (location loc{pos, 0}; located(loc, std::string_view{s}, from);
       ++cnt, ++loc.pos)
    s[loc.pos] = to[loc.pos_value];
  return cnt;
}
inline size_t substitute(std::string& s,
    std::span<const std::string_view>& from,
    std::span<const std::string_view>& to, size_t pos = 0) {
  assert(from.size() == to.size() && "from and to must be same size");
  assert(min_value_size(from) > 0 && "from is empty");
  size_t cnt{};
  for (location loc{pos, 0}; located(loc, std::string_view{s}, from);
       ++cnt, point_past(loc, from))
    s.replace(loc.pos, value_size(from[loc.pos_value]), to[loc.pos_value]);
  return cnt;
}
// Disambiguate from `std::span<const char>`.
inline size_t
substitute(std::string& s, const char* from, const char* to, size_t pos = 0) {
  return substitute(s, std::string_view{from}, std::string_view{to}, pos);
}
template<SingleLocateValue T>
size_t substitute(std::string& s, std::initializer_list<T> from,
    std::initializer_list<T> to, size_t pos = 0) {
  return substitute(s, convert::as_span(from), convert::as_span(to), pos);
}

//
// Substituted
//

// Return new string that contains `s` with `from` replaced with `to`.
[[nodiscard]] inline std::string substituted(std::string s,
    std::string_view from, std::string_view to) noexcept {
  auto ss = std::string{std::move(s)};
  substitute(ss, from, to);
  return ss;
}

// TODO: Add reverse versions of all functions.
// TODO: Benchmark whether it's faster to do replacements in-place or to
// build a new string. There's also the middle ground of in-place but in a
// single pass, so long as we're not growing.
}} // namespace corvid::strings::locating
