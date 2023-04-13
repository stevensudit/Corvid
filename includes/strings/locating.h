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

// Search and replace, except `search` and `find` are `std`, so we had to
// replace them with `locate` to avoid conflicts (despite namespaces).

namespace corvid::strings {
inline namespace locating {

// A single value to locate, which can be a `char` or `std::string_view`.
// Contrast with lists of these values.
template<typename T>
concept SingleLocateValue = StringViewConvertible<T> || is_char_v<T>;

// For multi-value locates, index of where the value was found in the target
// and index of which value was located.
struct location {
  size_t ndx;
  size_t ndx_value;

  constexpr auto operator<=>(const location&) const = default;
};

// Locate the first instance of a single `value` in `s`, starting at `ndx`.
// Works for `char` and `std::string_view`. Returns the index of where the
// value was found, or npos.
constexpr size_t locate(std::string_view s,
    const SingleLocateValue auto& value, size_t ndx = 0) {
  return s.find(value, ndx);
}

// Locate the first instance of any of the `values` in `s`, starting at `ndx`,
// where these are `char`.
// Returns the location, which has both the index of where the value was
// located in the target and the index of which value was located. To locate
// the next instance, call again with the `ndx` set to the returned `ndx`
// incremented by the size of the value at `ndx_value` (which, in this case, is
// always 1).
inline constexpr location
locate(std::string_view s, std::span<const char> values, size_t ndx = 0) {
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
    std::initializer_list<char> values, size_t ndx = 0) {
  return locate(s, {values.begin(), values.end()}, ndx);
}

// Locate the first instance of any of the `values` in `s`, starting at `ndx`,
// where these are `std::string_view`.
// Returns the location, which has both the index of where the value was
// located in the target and the index of which value was located. To locate
// the next instance, call again with the `ndx` set to the returned `ndx`
// incremented by the size of the value at `ndx_value` (which is
// values[ndx_value].size()).
inline constexpr struct location locate(std::string_view s,
    std::span<const std::string_view> values, size_t ndx = 0) {
  for (; ndx < s.size(); ++ndx)
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
    std::initializer_list<std::string_view> values, size_t ndx = 0) {
  return locate(s, std::span{values.begin(), values.end()}, ndx);
}

// Return whether a single `value` was found in `s`, starting at `ndx`,
// and updating `ndx` to where it was found. Works for `char` and
// `std::string_view`.
constexpr bool
located(size_t& ndx, std::string_view s, const SingleLocateValue auto& value) {
  return (ndx = locate(s, value, ndx)) != s.npos;
}

// Return whether any of the `values` was found in `s`, starting at the `ndx`
// in `indexes`, and updating both the `ndx` and `ndx_value`. Ignores initial
// value of `ndx_value`.
constexpr bool
located(location& indexes, std::string_view s, const auto& values) {
  return (indexes = locate(s, values, indexes.ndx)).ndx != s.npos;
}

// Replace instances of `from` in `s` with `to`, returning count of
// replacements.
inline size_t
replace(std::string& s, std::string_view from, std::string_view to) {
  size_t cnt{};
  for (size_t ndx{}; located(ndx, s, from); ndx += to.size()) {
    ++cnt;
    s.replace(ndx, from.size(), to);
  }
  return cnt;
}

// Replace instances of `from` in `s` with `to`, returning count of
// replacements.
inline size_t replace(std::string& s, char from, char to) {
  size_t cnt{};
  for (size_t ndx{}; located(ndx, s, from); ++ndx) {
    ++cnt;
    s[ndx] = to;
  }
  return cnt;
}

// Return new string that contains `s` with `from` replaced with `to`.
[[nodiscard]] inline std::string
replaced(std::string s, std::string_view from, std::string_view to) {
  auto ss = std::string{std::move(s)};
  replace(ss, from, to);
  return ss;
}

} // namespace locating
} // namespace corvid::strings
