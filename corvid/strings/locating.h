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

// Search and replace, except `search` and `find` are words used `std`, so we
// had to replace them with `locate` to avoid conflicts (despite namespaces),
// and possible royalties.
//
// Note:
// For functions which accept a `std::initializer_list<std::string_view>`, it's
// fine to call them with a `std::initializer_list<const char*>`. In other
// words, it's ok for the elements to be regular string literals.
//
// However, if you assign that same list to a variable, it will fail with a
// compiler error. One solution is to replace the string literals with
// `std::string_view` literals. You could also use a `std::span`.

namespace corvid::strings { inline namespace locating {

// A single value to locate, which can be a `char` or something that converts
// to a `std::string_view`. Contrast with lists of these values.
template<typename T>
concept SingleLocateValue = StringViewConvertible<T> || is_char_v<T>;

namespace details {

// Convert a list of values to a `std::span`, including identity.
// TODO: Once everything works, simplify this.
inline constexpr std::span<const char> as_span(
    std::initializer_list<char> values) noexcept {
  return {values.begin(), values.end()};
}
inline constexpr std::span<const char> as_span(
    std::span<const char> values) noexcept {
  return values;
}
// TODO: Make this function work for StringViewConvertible
inline constexpr std::span<const std::string_view> as_span(
    std::initializer_list<std::string_view> values) noexcept {
  return {values.begin(), values.end()};
}
template<StringViewConvertible T>
inline constexpr std::span<const std::string_view>
as_span(std::span<const T> values) noexcept {
  return values;
}
} // namespace details

// For `locate` on a list of values, the `location` is used to return both the
// index of where the value was located in the target and the index (in the
// value list) of which value was located. Also used as state for `located`.
// The two indexes are set to `npos` when nothing was located.
struct location {
  size_t ndx{};
  size_t ndx_value{};

  constexpr auto operator<=>(const location&) const noexcept = default;
};

// Size of a single value.
constexpr size_t value_size(const SingleLocateValue auto& value) noexcept {
  if constexpr (is_char_v<decltype(value)>)
    return 1;
  else
    return std::string_view{value}.size();
}

// Updates the `location` to point past the value that was just located,
// returning it as well. This can be used with `located` to loop over located
// values. Note that you cannot safely use it before the first call to `locate`
// or when `locate` fails because `ndx_value` must be valid.
template<typename T>
constexpr size_t
point_past(location& indexes, std::span<const T> values) noexcept {
  assert(indexes.ndx_value < values.size());
  indexes.ndx += value_size(values[indexes.ndx_value]);
  return indexes.ndx;
}

// Same as point_past above, but for an initializer list.
template<SingleLocateValue T>
constexpr size_t
point_past(location& indexes, std::initializer_list<T> values) noexcept {
  return point_past(indexes, details::as_span(values));
}

// Locate the first instance of a single `value` in `s`, starting at `ndx`.
// Works for `char` and `std::string_view`. Returns the index of where the
// value was located, or npos.
//
// To locate the next instance, call again with the `ndx` set to the returned
// `ndx` plus the size of the located value. For `char`, the size of the
// located value is just 1. For `std::string_view`, this is its `size`.
constexpr size_t locate(std::string_view s,
    const SingleLocateValue auto& value, size_t ndx = 0) noexcept {
  return s.find(value, ndx);
}

// Locate the first instance of any of the `char` `values` in `s`, starting at
// `ndx`.
//
// Returns the `location`, which has both the index of where the value was
// located in the target and the index (in the value list) of which value was
// located. The two indexes are set to `npos` when nothing was located.
//
// To locate the next instance, call again with the `ndx` set to the returned
// `ndx`, incremented past the found value. For `char` values, the size is
// just 1.
inline constexpr location locate(std::string_view s,
    std::span<const char> values, size_t ndx = 0) noexcept {
  const auto value_sv = std::string_view{values.begin(), values.end()};
  for (; ndx < s.size(); ++ndx)
    for (size_t ndx_value = 0; ndx_value < value_sv.size(); ++ndx_value)
      if (s[ndx] == value_sv[ndx_value]) return {ndx, ndx_value};
  return {s.npos, s.npos};
}

// Locate the first instance of any of the `char` initializer list `values` in
// `s`, starting at `ndx`. See above for details about the return values.
//
// Usage:
//   auto [ndx, ndx_value] = locate(s, {'a', 'b', 'c'});
inline constexpr auto locate(std::string_view s,
    std::initializer_list<char> values, size_t ndx = 0) noexcept {
  return locate(s, {values.begin(), values.end()}, ndx);
}

// Locate the first instance of any of the `std::string_view` `values` in `s`,
// starting at `ndx`.
//
// Returns the `location`, which has both the index of where the value was
// located in the target and the index (in the value list) of which value was
// located. The two indexes are set to `npos` when nothing was located.
//
// To locate the next instance, call again with the `ndx` set to the returned
// `ndx`, incremented past the found value. For `std::string_view`, this is its
// `size`, which is most easily found by passing the return value of
// `point_past` as the new `ndx`.
inline constexpr struct location locate(std::string_view s,
    std::span<const std::string_view> values, size_t ndx = 0) noexcept {
  for (; ndx <= s.size(); ++ndx)
    for (size_t ndx_value = 0; ndx_value < values.size(); ++ndx_value)
      if (s.substr(ndx, values[ndx_value].size()) == values[ndx_value])
        return {ndx, ndx_value};
  return {s.npos, s.npos};
}

// Locate the first instance of any of the `std::string_view` initializer list
// `values` in `s`, starting at `ndx`. See above for details about the return
// values.
//
// Usage:
//   auto [ndx, ndx_value] = locate(s, {"abc", "def", "ghi"});
inline constexpr auto locate(std::string_view s,
    std::initializer_list<std::string_view> values, size_t ndx = 0) noexcept {
  return locate(s, std::span{values.begin(), values.end()}, ndx);
}

// Return whether a single `value` was located in `s`, starting at `ndx`,
// and updating `ndx` to where it was located. Works for `char` and
// `std::string_view`. The index is set to `npos` when nothing was located.
//
//  To locate the next instance, you must increment `ndx` past the located
//  `value`. For `char`, the size is just 1. For `std::string_view`, this is
//  its `size`.
constexpr bool located(size_t& ndx, std::string_view s,
    const SingleLocateValue auto& value) noexcept {
  return (ndx = locate(s, value, ndx)) != s.npos;
}

// Return whether any of the `values` was located in `s`, starting at the `ndx`
// in `indexes`, and updating both the `ndx` and `ndx_value`. Ignores the
// initial value of `ndx_value`. The two indexes are set to `npos` when nothing
// was located.
//
//  To locate the next instance, you must increment `ndx` past the located
//  `value`. For `char`, the size of the located value is just 1. For
//  `std::string_view`, this is its `size`, which is most easily found by
//  calling `point_past` on `indexes`
template<SingleLocateValue T>
constexpr bool located(location& indexes, std::string_view s,
    std::span<const T> values) noexcept {
  return (indexes = locate(s, values, indexes.ndx)).ndx != s.npos;
}
template<typename T>
constexpr bool located(location& indexes, std::string_view s,
    std::initializer_list<T> values) noexcept {
#if 0
  return (indexes = locate(s, details::as_span(values), indexes.ndx)).ndx !=
         s.npos;
#else
  return located(indexes, s, details::as_span(values));
#endif
}

// Return count of instances of a single `value` in `s`, starting at
// `ndx`. Note that an empty `std::string_view` value causes an infinite
// loop.
size_t count_located(std::string_view s, const SingleLocateValue auto& value,
    size_t ndx = 0) noexcept {
  size_t cnt{};
  while (located(ndx, s, value)) ++cnt, ndx += value_size(value);
  return cnt;
}

// Return count of instances of any of the `values` in `s`, starting at
// `ndx`. Note that an empty `std::string_view` value causes an infinite
// loop.
size_t count_located(std::string_view s, const auto& values,
    size_t ndx = 0) noexcept {
  size_t cnt{};
  location indexes{ndx, 0};
  auto v = details::as_span(values);
  while (located(indexes, s, v)) ++cnt, point_past(indexes, v);
  return cnt;
}

// Replace instances of `from` in `s` with `to`, returning count of
// replacements. Note that an empty `std::string_view` value causes an
// infinite loop.
inline size_t
replace(std::string& s, std::string_view from, std::string_view to) noexcept {
  size_t cnt{};
  for (size_t ndx{}; located(ndx, s, from); ndx += to.size()) {
    ++cnt;
    s.replace(ndx, from.size(), to);
  }
  return cnt;
}

// Replace instances of `from` in `s` with `to`, returning count of
// replacements.
inline size_t replace(std::string& s, char from, char to) noexcept {
  size_t cnt{};
  for (size_t ndx{}; located(ndx, s, from); ++ndx) {
    ++cnt;
    s[ndx] = to;
  }
  return cnt;
}

// Return new string that contains `s` with `from` replaced with `to`.
[[nodiscard]] inline std::string
replaced(std::string s, std::string_view from, std::string_view to) noexcept {
  auto ss = std::string{std::move(s)};
  replace(ss, from, to);
  return ss;
}

// TODO: Consider mass-renaming ndx to pos. Besides consistency, a pos
// indicates that -1 is special.
// TODO: Add reverse versions of all functions.
// TODO: Benchmark whether it's faster to do replacements in-place or to
// build a new string.

}} // namespace corvid::strings::locating
