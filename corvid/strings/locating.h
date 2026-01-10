// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include "strings_shared.h"

#include <array>
#include <cstring>

// Search and replace, except `search`, `find`, `replace`, and `erase` are all
// symbols in the `std` namespace. We had to substitute `locate`, `substitute`,
// and `excise` in order to disambiguate them so as to avoid the conflicts that
// can happen despite namespaces. And, of course, to avoid any possible
// royalties.
//
// On another naming note, we use `pos` to refer to the specific variable or
// member name, while `position` is an alias for size_t used to indicate a
// location in a string, with `npos` treated as the logical equivalent of the
// string's size.
//
// Since the size can be more convenient than `npos` for uses such as half-open
// intervals, we offer the option to have the size returned where `npos` would
// be. This is controlled by specializing on the `npos_choice::size` enum
// value.
//
// Functions support both both single and multiple values. The latter case is
// distinct from, and more powerful than, calling once for each value. For
// example, you can swap 'a' with 'b' and 'b' with 'a' in a single pass,
// whereas two separate calls would leave you entirely without `b`s.
//
// As part of the extended expressive power, the returned position is a
// `location` pair, containing `pos`, which shows where the value was found in
// the target, and the `pos_value`, which shows the `position` (in the list of
// values) of the value that was found. Much as a `position` has an `npos`
// constant, `location` has an `nloc` constant which has `npos` in both `pos`
// and `pos_value` members.
//
// There are many overloads, but few function names:
// - locate: Locate occurrence of any of the values in the target.
// - located: Whether any values were located, updating `pos`.
// - rlocate, rlocated: Same, but from the rear.
// - locate_not, rlocate_not: Locate occurrence of any value not in the target.
// - located_not, rlocated_not: Same, but returning `bool`.
// - count_located: Count of the values.
// - substitute: Substitute all `from` with matching `to`.
// - excise: Excise all occurrences of the values.
// - substituted: Same, but returning modified string.
//
// There's also `point_past`, which works with `located`, and some other
// largely internal functions, such as `value_size`,  `min_value_size`,
// `as_npos`, and `as_nloc`.
//
// Single-value functions accept `char` or anything that can be converted
// to `std::string_view`.
//
// Multi-value functions primarily accept `std::initializer_list<char>`,
// `std::initializer_list<std::string_view>`. They also accept
// `std::array<char>`, `std::array<std::string_view>`, `std::span<char>` or
// `std::span<std::string_view>`, as well as other types that convert to spans,
// such as `std::vector<char>` and `std::vector<std::string_view>`. Some effort
// was taken to allow accepting types that convert to `std::string_view`, but
// it might not work. In that case, you can use `as_views` to bludgeon it into
// shape.

// You can include this namespace with `using namespace
// corvid::strings::locating;`, but it's not strictly necessary. If not,
// though, you will likely want to specify `using namespace
// corvid::literals;` to get `npos` and `nloc`.
namespace corvid::strings { inline namespace locating {

// A single value to `locate`, which can be a `char` or something that converts
// to a `std::string_view`. Contrast with lists of these values. Note that, in
// C++20, range-based construction was added for `std::string_view`, so we have
// to manually exclude it.
template<typename T>
concept SingleLocateValue = (StringViewConvertible<T> || Char<T>);

// Convert any container shaped like a `std::span` whose elements are
// convertible to `std::string_view` into a `std::vector<std::string_view>`
// that you can use for functions requiring multiple values.
//
// This function is usually unnecessary, but there might be some recalcitrant
// string-like types that need it. In theory, it could also be an optimization
// if you're doing multiple locates or substitutes with, say, a
// `std::vector<std::string>`. Then caching the output of `as_views` can speed
// things up.
//
// Note that the `std::string_view` elements do not own any memory, but
// instead use whatever the source values point at, and that the return value
// is a temporary vector, so use caution.
[[nodiscard]] constexpr auto as_views(const auto& values) {
  std::vector<std::string_view> result;
  result.reserve(values.size());
  for (auto& value : values) result.emplace_back(std::string_view{value});
  return result;
}

// For `locate` on a list of values, the `location` is used to return both
// the pos of where the value was located in the target and the pos (in
// the value list) of which value was located. The two values are set to `npos`
// when nothing was located.
struct location {
  position pos{};
  position pos_value{};

  constexpr auto operator<=>(const location&) const noexcept = default;
};

// A pos_range is a pair of positions, used to indicate the range of the found
// item. When nothing is found, both positions are `npos`. Otherwise, `begin`
// points to the found item and `end` points to the character after it. This is
// probably most useful for locating delimiters.
struct pos_range {
  position begin{};
  position end{};
};

// To get `npos`, `nloc`, `npos_choice`, and `nloc_value`, use:
//  using namespace corvid::literals;
// Note: `npos` is defined in string_base.h
inline namespace literals {

constexpr location nloc{npos, npos};
constexpr pos_range npos_range{npos, npos};

// Whether to return `npos` or `size` when nothing is found.
enum class npos_choice : std::uint8_t { npos, size };
} // namespace literals

// Utility to convert `npos` to `size()`, based on `npos_choice`.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position
as_npos(const std::string_view& s, position pos = npos) noexcept {
  if constexpr (npv == npos_choice::size)
    if (pos == npos) pos = s.size();
  return pos;
}
// Same, but for `location`.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location
as_nloc(const std::string_view& s, const auto& values, position pos = npos,
    position pos_value = npos) noexcept {
  if constexpr (npv == npos_choice::size)
    if (pos == npos) {
      pos = s.size();
      pos_value = values.size();
    }
  return {pos, pos_value};
}

// Utility to convert `size()` to `npos`, based on `npos_choice`.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position
from_npos(const std::string_view& s, position pos) noexcept {
  if constexpr (npv == npos_choice::npos)
    if (pos >= s.size()) pos = npos;
  return pos;
}
// Same, but for `location`.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location
from_npos(const std::string_view& s, location loc) noexcept {
  if constexpr (npv == npos_choice::npos)
    if (loc.pos >= s.size()) return nloc;
  return loc;
}

// Utility to return `pos_range`.
[[nodiscard]] constexpr pos_range
as_pos_range(const std::string_view& s, position pos) noexcept {
  if (pos >= s.size()) return npos_range;
  return {pos, pos + 1};
}
// Same, but for location.
[[nodiscard]] constexpr pos_range as_pos_range(const std::string_view& s,
    const auto& values, location loc) noexcept {
  if (loc.pos >= s.size()) return npos_range;
  return {loc.pos, loc.pos + value_size(values[loc.pos_value])};
}

// Size of a single value, regardless of type.
[[nodiscard]] constexpr size_t value_size(const auto& value) noexcept {
  if constexpr (Char<decltype(value)>)
    return 1;
  else
    return std::string_view{value}.size();
}

// Smallest size of a list of values. If no values, returns 0.
[[nodiscard]] constexpr size_t min_value_size(
    const StringViewConvertibleSpan auto& values) noexcept {
  auto smallest = std::ranges::min_element(values,
      [](const auto& a, const auto& b) { return a.size() < b.size(); });
  if (smallest != values.end()) return smallest->size();
  return 0;
}

// Updates the `pos` or `location` to point past the value that was just
// located, returning it as well. This can be used with `located` to loop over
// located values.
//
// Note that you cannot safely use it before the first call to
// `locate` or when `locate` fails, because `pos_value` must be in range
// (and not npos).
constexpr position point_past(position& pos, char) noexcept { return ++pos; }
constexpr position
point_past(position& pos, const StringViewConvertible auto& value) noexcept {
  return pos += std::max(value_size(value), size_t{1});
}
constexpr position
point_past(location& loc, const SpanConvertible auto& values) noexcept {
  assert(loc.pos_value < values.size());
  loc.pos += std::max(value_size(values[loc.pos_value]), size_t{1});
  return loc.pos;
}
constexpr position
point_past(location& loc, std::initializer_list<char> values) noexcept {
  return point_past(loc, std::span<const char>{values.begin(), values.end()});
}
constexpr position point_past(location& loc,
    std::initializer_list<std::string_view> values) noexcept {
  return point_past(loc, std::span{values.begin(), values.end()});
}

//
// Locate
//

// We define the implementation functions separately from the public interface,
// avoiding all overloading. The public function then takes any type and
// explicitly chooses which helper function to forward to, with any necessary
// conversions. Unfortunately, we also have to overload the public function on
// initializer lists.
//
// The reason we do all this is to gain full control over overloading behavior
// in a way that is much less brittle in the face on ongoing language changes.
// C++23 broke this scheme in at least two distinct ways, so we should expect
// more of the same in the future.

// This namespace contains helpers that should not be called directly, but are
// also not purely internal implementation details because this is where the
// detailed function documentation is located.
namespace helpers {
// Single-value `locate`, `locate_not`, `rlocate`, `rlocate_not`.
//
// Locate the first instance of a single `value` in `s`, starting at `pos`.
// Returns the pos of where the value was located, or `npos`.
//
// To locate the next instance, call again with `pos` set to the returned
// pos plus the size of the located value. For `char`, the size of the
// located value is just 1. For `std::string_view`, this is its `size`.
//
// Note: When using `locate_not` or `rlocate_not` with a string value, the
// behavior is correct but not necessarily obvious. It compares one
// value-length chunk at a time, not one character at a time. In other words,
// it doesn't check for the value at each position, but at positions one
// value-length apart.
//
// So, for example, if you search for not "abc" in "abcd", it points to the
// 'd', not the 'b'. Likewise, if you reverse-search for not "de" in "abcde",
// it points to the 'b', not the 'c'.
//
// Implementation note: Previously, there was some clever overloading code to
// ensure that `char[]` was treated as a `std::string_view` and not a
// `std::span<const char>`. This worked in C++20 but was broken by an
// intentional (and otherwise good) change in C++23. To fix the break and
// remove future brittleness, we now overload more explicitly.

// Single character locating.

// Locate single char.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position
locate_char(std::string_view s, char value, position pos = 0) noexcept {
  return as_npos<npv>(s, s.find(value, pos));
}

// Locate single char not.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position
locate_not_char(std::string_view s, char value, position pos = 0) noexcept {
  return as_npos<npv>(s, s.find_first_not_of(value, pos));
}

// Reverse-locate single char.
//
// Same as above, but locate the last instance. To locate the previous
// instance, subtract 1, not the value size, from the returned `pos`.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position
rlocate_char(std::string_view s, char value, position pos = npos) noexcept {
  return as_npos<npv>(s, s.rfind(value, pos));
}

// Reverse-locate single char not.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position rlocate_not_char(std::string_view s,
    char value, position pos = npos) noexcept {
  return as_npos<npv>(s, s.find_last_not_of(value, pos));
}

// Single string locating.

// Locate single string.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position locate_string(std::string_view s,
    std::string_view value, position pos = 0) noexcept {
  return as_npos<npv>(s, s.find(value, pos));
}

// Locate single string not.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position locate_not_string(std::string_view s,
    std::string_view value, position pos = 0) noexcept {
  for (auto v = std::string_view{value}; pos < s.size(); pos += v.size())
    if (s.substr(pos, v.size()) != v) break;

  return from_npos<npv>(s, pos);
}

// Reverse-locate single string.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position rlocate_string(std::string_view s,
    std::string_view value, position pos = npos) noexcept {
  return as_npos<npv>(s, s.rfind(value, pos));
}

// Reverse-locate single string not.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr position rlocate_not_string(std::string_view s,
    std::string_view value, position pos = npos) noexcept {
  auto v = std::string_view{value};
  if (v.size() > s.size()) return 0;

  auto last = s.size() - v.size();
  if (pos == npos || pos > last) pos = last;

  for (;; pos = (pos > v.size() ? pos - v.size() : 0)) {
    if (s.substr(pos, v.size()) != v) break;
    if (pos == 0) {
      pos = npos;
      break;
    }
  }
  return from_npos<npv>(s, pos);
}

// Multiple character `locate`, `locate_not`, `rlocate`, `rlocate_not`.

// Locate the first instance of any of the `values` of type `char` in `s`,
// starting at `pos`.
//
// Returns the `location`, which has both the pos of where the value was
// located in the target, `pos`, and the pos (in the value list) of which value
// was located, `pos_value`. The two values are set to `npos` when nothing was
// located.
//
// To locate the next instance, call again with `pos` set to the returned
// `pos`, incremented past the found value. For `char` values, the size is
// just 1.
//
// Usage:
//   auto [pos, pos_value] = locate(s, {'a', 'b', 'c'});

// Locate any of the chars.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location locate_any_char(std::string_view s,
    const ConstCharSpan auto& values, position pos = 0) noexcept {
  for (; pos < s.size(); ++pos)
    for (position pos_value = 0; pos_value < values.size(); ++pos_value)
      if (s[pos] == values[pos_value]) return {pos, pos_value};
  return as_nloc<npv>(s, values);
}

// Locate none of the chars.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location locate_none_char(std::string_view s,
    const ConstCharSpan auto& values, position pos = 0) noexcept {
  for (; pos < s.size(); ++pos) {
    position pos_value = 0;
    for (; pos_value < values.size(); ++pos_value)
      if (s[pos] == values[pos_value]) break;
    if (pos_value == values.size()) return {pos, pos_value};
  }
  return as_nloc<npv>(s, values);
}

// Reverse-locate any of the chars.
//
// Same as above, but locate the last instance. To locate the previous
// instance, subtract 1, not the value size.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location rlocate_any_char(std::string_view s,
    const ConstCharSpan auto& values, position pos = npos) noexcept {
  if (s.empty()) return as_nloc<npv>(s, values);
  if (pos >= s.size()) pos = s.size() - 1;
  for (++pos; pos-- > 0;)
    for (position pos_value = 0; pos_value < values.size(); ++pos_value)
      if (s[pos] == values[pos_value]) return {pos, pos_value};
  return as_nloc<npv>(s, values);
}

// Reverse-locate none of the chars.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location rlocate_none_char(std::string_view s,
    const ConstCharSpan auto& values, position pos = npos) noexcept {
  if (s.empty()) return as_nloc<npv>(s, values);
  if (pos >= s.size()) pos = s.size() - 1;
  for (++pos; pos-- > 0;) {
    position pos_value = 0;
    for (; pos_value < values.size(); ++pos_value)
      if (s[pos] == values[pos_value]) break;
    if (pos_value == values.size()) return {pos, pos_value};
  }
  return as_nloc<npv>(s, values);
}

// Locate strings.

// Locate the first instance of any of the `values` of type `std::string_view`
// in `s`, starting at `pos`.
//
// Returns the `location`, which has both the pos of where the value was
// located in the target, `pos`, and the pos (in the value list) of which value
// was located, `pos_value`. The two values are set to `npos` when nothing was
// located.
//
// To locate the next instance, call again with the `pos` set to the returned
// `pos`, incremented past the found value. For `std::string_view`, this is its
// `size`, which is most easily found by passing the return value of
// `point_past` as the new `pos`.
//
// Usage:
//   auto [pos, pos_value] = locate(s, {"abc", "def", "ghi"});

// Locate any of the strings.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr struct location locate_any_string(std::string_view s,
    const StringViewConvertibleSpan auto& values, position pos = 0) noexcept {
  for (; pos <= s.size(); ++pos)
    for (position pos_value = 0; pos_value < values.size(); ++pos_value) {
      std::string_view value{values[pos_value]};
      if (s.substr(pos, value.size()) == value) return {pos, pos_value};
    }
  return as_nloc<npv>(s, values);
}

// Locate none of the strings.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location locate_none_string(std::string_view s,
    const StringViewConvertibleSpan auto& values, position pos = 0) noexcept {
  for (; pos < s.size(); ++pos) {
    bool matched = false;
    position pos_value = 0;
    for (; pos_value < values.size(); ++pos_value) {
      std::string_view value{values[pos_value]};
      if (s.substr(pos, value.size()) == value) {
        matched = true;
        break;
      }
    }
    if (!matched) return {pos, values.size()};
  }
  return as_nloc<npv>(s, values);
}

// Reverse-locate any of the strings.
//
// Same as above, but locate the last instance. Subtract 1, not the value
// size, from the returned pos to locate the previous instance.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr struct location rlocate_any_string(std::string_view s,
    const StringViewConvertibleSpan auto& values,
    position pos = npos) noexcept {
  if (s.empty()) return as_nloc<npv>(s, values);
  if (pos >= s.size()) pos = s.size() - 1;
  for (++pos; pos-- > 0;)
    for (position pos_value = 0; pos_value < values.size(); ++pos_value) {
      std::string_view value{values[pos_value]};
      if (s.substr(pos, value.size()) == value) return {pos, pos_value};
    }
  return as_nloc<npv>(s, values);
}

// Reverse-locate none of the strings.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location rlocate_none_string(std::string_view s,
    const StringViewConvertibleSpan auto& values,
    position pos = npos) noexcept {
  if (s.empty()) return as_nloc<npv>(s, values);
  if (pos >= s.size()) pos = s.size() - 1;
  for (++pos; pos-- > 0;) {
    bool matched = false;
    position pos_value = 0;
    for (; pos_value < values.size(); ++pos_value) {
      std::string_view value{values[pos_value]};
      if (s.substr(pos, value.size()) == value) {
        matched = true;
        break;
      }
    }
    if (!matched) return {pos, values.size()};
  }
  return as_nloc<npv>(s, values);
}

} // namespace helpers

// NOLINTBEGIN(bugprone-branch-clone)

// See documentation for the corresponding `details::locate_*` above.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr auto
locate(std::string_view s, auto&& value, position pos = 0) noexcept {
  using T = std::remove_cvref_t<decltype(value)>;
  if constexpr (Char<T>)
    return helpers::locate_char<npv>(s, value, pos);
  else if constexpr (StdArray<T>)
    return locate<npv>(s, std::span{value}, pos);
  else if constexpr (ConstCharSpan<T>)
    return helpers::locate_any_char<npv>(s, value, pos);
  else if constexpr (StringViewConvertibleSpan<T>)
    return helpers::locate_any_string<npv>(s, value, pos);
  else if constexpr (StringViewConvertible<T>)
    return helpers::locate_string<npv>(s, std::string_view{value}, pos);
  else if constexpr (SpanConvertible<T>)
    return locate<npv>(s, std::span{value}, pos);
  else
    static_assert(false, "Invalid type for locate");
}

// NOLINTEND(bugprone-branch-clone)

template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location locate(std::string_view s,
    std::initializer_list<char> values, position pos = 0) noexcept {
  return locate<npv>(s, std::span<const char>{values.begin(), values.end()},
      pos);
}

template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location locate(std::string_view s,
    std::initializer_list<const std::string_view> values,
    position pos = 0) noexcept {
  return locate<npv>(s,
      std::span<const std::string_view>{values.begin(), values.end()}, pos);
}

// NOLINTBEGIN(bugprone-branch-clone)

// See documentation for the corresponding `details::locate_not_*` above.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr auto
locate_not(std::string_view s, auto&& value, position pos = 0) noexcept {
  using T = std::remove_cvref_t<decltype(value)>;
  if constexpr (Char<T>)
    return helpers::locate_not_char<npv>(s, value, pos);
  else if constexpr (StdArray<T>)
    return locate_not<npv>(s, std::span{value}, pos);
  else if constexpr (ConstCharSpan<T>)
    return helpers::locate_none_char<npv>(s, value, pos);
  else if constexpr (StringViewConvertibleSpan<T>)
    return helpers::locate_none_string<npv>(s, value, pos);
  else if constexpr (StringViewConvertible<T>)
    return helpers::locate_not_string<npv>(s, std::string_view{value}, pos);
  else if constexpr (SpanConvertible<T>)
    return locate_not<npv>(s, std::span{value}, pos);
  else
    static_assert(false, "Invalid type for locate");
}

// NOLINTEND(bugprone-branch-clone)

template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location locate_not(std::string_view s,
    std::initializer_list<char> values, position pos = 0) noexcept {
  return locate_not<npv>(s,
      std::span<const char>{values.begin(), values.end()}, pos);
}

template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location locate_not(std::string_view s,
    std::initializer_list<const std::string_view> values,
    position pos = 0) noexcept {
  return locate_not<npv>(s,
      std::span<const std::string_view>{values.begin(), values.end()}, pos);
}

// NOLINTBEGIN(bugprone-branch-clone)

// See documentation for the corresponding `details::rlocate_*` above.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr auto
rlocate(std::string_view s, auto&& value, position pos = npos) noexcept {
  using T = std::remove_cvref_t<decltype(value)>;
  if constexpr (Char<T>)
    return helpers::rlocate_char<npv>(s, value, pos);
  else if constexpr (StdArray<T>)
    return rlocate<npv>(s, std::span{value}, pos);
  else if constexpr (ConstCharSpan<T>)
    return helpers::rlocate_any_char<npv>(s, value, pos);
  else if constexpr (StringViewConvertibleSpan<T>)
    return helpers::rlocate_any_string<npv>(s, value, pos);
  else if constexpr (StringViewConvertible<T>)
    return helpers::rlocate_string<npv>(s, std::string_view{value}, pos);
  else if constexpr (SpanConvertible<T>)
    return rlocate<npv>(s, std::span{value}, pos);
  else
    static_assert(false, "Invalid type for locate");
}

// NOLINTEND(bugprone-branch-clone)

template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location rlocate(std::string_view s,
    std::initializer_list<char> values, position pos = npos) noexcept {
  return rlocate<npv>(s, std::span<const char>{values.begin(), values.end()},
      pos);
}
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location rlocate(std::string_view s,
    std::initializer_list<const std::string_view> values,
    position pos = npos) noexcept {
  return rlocate<npv>(s,
      std::span<const std::string_view>{values.begin(), values.end()}, pos);
}

// NOLINTBEGIN(bugprone-branch-clone)

// See documentation for the corresponding `details::rlocate_not_*` above.
template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr auto
rlocate_not(std::string_view s, auto&& value, position pos = npos) noexcept {
  using T = std::remove_cvref_t<decltype(value)>;
  if constexpr (Char<T>)
    return helpers::rlocate_not_char<npv>(s, value, pos);
  else if constexpr (StdArray<T>)
    return rlocate_not<npv>(s, std::span{value}, pos);
  else if constexpr (ConstCharSpan<T>)
    return helpers::rlocate_none_char<npv>(s, value, pos);
  else if constexpr (StringViewConvertibleSpan<T>)
    return helpers::rlocate_none_string<npv>(s, value, pos);
  else if constexpr (StringViewConvertible<T>)
    return helpers::rlocate_not_string<npv>(s, std::string_view{value}, pos);
  else if constexpr (SpanConvertible<T>)
    return rlocate_not<npv>(s, std::span{value}, pos);
  else
    static_assert(false, "Invalid type for locate");
}

// NOLINTEND(bugprone-branch-clone)

template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location rlocate_not(std::string_view s,
    std::initializer_list<char> values, position pos = npos) noexcept {
  return rlocate_not<npv>(s,
      std::span<const char>{values.begin(), values.end()}, pos);
}

template<npos_choice npv = npos_choice::npos>
[[nodiscard]] constexpr location rlocate_not(std::string_view s,
    std::initializer_list<const std::string_view> values,
    position pos = npos) noexcept {
  return rlocate_not<npv>(s,
      std::span<const std::string_view>{values.begin(), values.end()}, pos);
}

//
// Located
//

// Return whether a single `value` was located in `s`, starting at `pos`,
// and updating `pos` to where it was located. Works for `char` and
// `std::string_view`. The `pos` is set to `npos` when nothing was located.
//
//  To locate the next instance, you must increment `pos` past the located
//  `value`. For `char`, the size is just 1. For `std::string_view`, this is
//  its `size`. The easiest way to do this is to pass the return value of
//  `point_past` as the new `pos`.
//
template<npos_choice npv = npos_choice::npos>
constexpr bool
located(position& pos, std::string_view s, auto&& value) noexcept {
  using R = decltype(locate<npv>(s, value, pos));
  if constexpr (std::is_same_v<R, position>)
    return (pos = locate<npv>(s, value, pos)) != as_npos<npv>(s);
  else
    static_assert("Multiple values not supported");
  return {};
}
template<npos_choice npv = npos_choice::npos>
constexpr bool
located_not(position& pos, std::string_view s, auto&& value) noexcept {
  using R = decltype(locate_not<npv>(s, value, pos));
  if constexpr (std::is_same_v<R, position>)
    return (pos = locate_not<npv>(s, value, pos)) != as_npos<npv>(s);
  else
    static_assert("Single value not supported");
  return {};
}
// Same as above, but from the rear. To locate the previous instance,
// subtract 1.
//
// Note that `pos` must be initialized to `size`, not `npos`, because we need
// to reserve the latter (or any other value above `size`) for the stop
// condition. These odd semantics are required to avoid an infinite loop.
template<npos_choice npv = npos_choice::npos>
constexpr bool
rlocated(position& pos, std::string_view s, const auto& value) noexcept {
  if (pos > s.size()) {
    pos = as_npos<npv>(s);
    return false;
  }
  using R = decltype(rlocate<npv>(s, value, pos));
  if constexpr (std::is_same_v<R, position>)
    return (pos = rlocate<npv>(s, value, pos)) != as_npos<npv>(s);
  else
    static_assert("Multiple values not supported");
  return {};
}
template<npos_choice npv = npos_choice::npos>
constexpr bool
rlocated_not(position& pos, std::string_view s, const auto& value) noexcept {
  if (pos > s.size()) {
    pos = as_npos<npv>(s);
    return false;
  }
  using R = decltype(rlocate_not<npv>(s, value, pos));
  if constexpr (std::is_same_v<R, position>)
    return (pos = rlocate_not<npv>(s, value, pos)) != as_npos<npv>(s);
  else
    static_assert("Multiple values not supported");
  return {};
}

// Return whether any of the `values` were located in `s`, starting at the
// `pos` in `loc`, and updating both the `pos` and `pos_value`. Ignores the
// initial value of `pos_value`. The two values are set to `npos` when
// nothing was located.
//
//  To locate the next instance, you must increment `pos` past the located
//  `value`. For `char`, the size of the located value is just 1. For
//  `std::string_view`, this is its `size`, which is most easily found by
//  calling `point_past` on `loc`.
template<npos_choice npv = npos_choice::npos>
constexpr bool
located(location& loc, std::string_view s, auto&& values) noexcept {
  using R = decltype(locate<npv>(s, values, loc.pos));
  if constexpr (std::is_same_v<R, location>)
    return (loc = locate<npv>(s, values, loc.pos)).pos != as_npos<npv>(s);
  else
    static_assert("Single value not supported");
  return {};
}
// Same as above, but from the rear. Read the notes above for single-value
// `rlocated` for more about `pos`.
template<npos_choice npv = npos_choice::npos>
constexpr bool
rlocated(location& loc, std::string_view s, auto&& values) noexcept {
  if (loc.pos > s.size()) {
    loc.pos = as_npos<npv>(s);
    return false;
  }
  using R = decltype(rlocate<npv>(s, values, loc.pos));
  if constexpr (std::is_same_v<R, location>)
    return (loc = rlocate<npv>(s, values, loc.pos)).pos != as_npos<npv>(s);
  else
    static_assert("Single value not supported");
  return {};
}

//
// count_located
//

// Return count of instances of a `value` in `s`, starting at `pos`.
[[nodiscard]] size_t
count_located(std::string_view s, auto&& value, position pos = 0) noexcept {
  size_t cnt{};
  using R = decltype(locate(s, value, pos));
  if constexpr (std::is_same_v<R, position>)
    while (located(pos, s, value)) ++cnt, point_past(pos, value);
  else
    for (location loc{pos, 0}; located(loc, s, value);
        ++cnt, point_past(loc, value));

  return cnt;
}

[[nodiscard]] size_t count_located(std::string_view s,
    std::initializer_list<char> values, position pos = 0) noexcept {
  return count_located(s, std::span<const char>{values.begin(), values.end()},
      pos);
}
[[nodiscard]] size_t count_located(std::string_view s,
    std::initializer_list<std::string_view> values,
    position pos = 0) noexcept {
  return count_located(s,
      std::span<const std::string_view>{values.begin(), values.end()}, pos);
}

//
// Substitute
//

// Substitute all instances of `from` in `s` with `to`, returning count of
// substitutions. Note that an empty `from` follows the same behavior as Python
// `string.replace`, in that it will insert `to` around each character in `s`.
size_t substitute(std::string& s, const SingleLocateValue auto& from,
    const SingleLocateValue auto& to, position pos = 0) {
  size_t cnt{};
  if constexpr (Char<decltype(from)>) {
    static_assert(Char<decltype(to)>, "from/to must match");
    for (; located(pos, s, from); ++cnt, ++pos) s[pos] = to;
  } else {
    static_assert(!Char<decltype(to)>, "from/to must match");
    auto from_sv = std::string_view{from};
    auto to_sv = std::string_view{to};
    size_t from_size = from_sv.size();
    size_t to_size = to_sv.size() + (from_size ? 0 : 1);
    for (; located(pos, s, from_sv); ++cnt, pos += to_size)
      s.replace(pos, from_size, to_sv);
  }
  return cnt;
}
inline size_t substitute(std::string& s, std::span<const char> from,
    std::span<const char> to, position pos = 0) {
  size_t cnt{};
  for (location loc{pos, 0}; located(loc, std::string_view{s}, from);
      ++cnt, ++loc.pos)
    s[loc.pos] = to[loc.pos_value];
  return cnt;
}
inline size_t substitute(std::string& s, std::initializer_list<char> from,
    std::initializer_list<char> to, position pos = 0) {
  return substitute(s, std::span<const char>{from}, std::span<const char>{to},
      pos);
}
inline size_t substitute(std::string& s,
    std::span<const std::string_view> from,
    std::span<const std::string_view> to, position pos = 0) {
  size_t cnt{};
  for (location loc{pos, 0}; located(loc, std::string_view{s}, from); ++cnt) {
    size_t from_size = from[loc.pos_value].size();
    s.replace(loc.pos, from_size, to[loc.pos_value]);
    size_t to_size = to[loc.pos_value].size() + (from_size ? 0 : 1);
    loc.pos += std::max(to_size, size_t{1});
  }
  return cnt;
}
inline size_t substitute(std::string& s,
    std::initializer_list<std::string_view> from,
    std::initializer_list<std::string_view> to, position pos = 0) {
  return substitute(s, std::span<const std::string_view>{from},
      std::span<const std::string_view>{to}, pos);
}

//
// Substituted
//

// Return new string that contains `s` with `from` replaced with `to`.
[[nodiscard]] std::string
substituted(std::string s, const auto& from, const auto& to) {
  auto ss = std::string{std::move(s)};
  substitute(ss, from, to);
  return ss;
}
[[nodiscard]] inline std::string substituted(std::string s,
    std::initializer_list<char> from, std::initializer_list<char> to) {
  auto ss = std::string{std::move(s)};
  substitute(ss, from, to);
  return ss;
}
[[nodiscard]] inline std::string substituted(std::string s,
    std::initializer_list<std::string_view> from,
    std::initializer_list<std::string_view> to) {
  auto ss = std::string{std::move(s)};
  substitute(ss, from, to);
  return ss;
}

//
// Excise
//

// Excise all instances of `from` in `s`, returning count of
// excisions. An empty `from` clears the string.
size_t
excise(std::string& s, const SingleLocateValue auto& from, position pos = 0) {
  size_t cnt{};
  if constexpr (Char<decltype(from)>) {
    for (; located(pos, s, from); ++cnt) s.erase(pos, 1);
  } else {
    auto from_sv = std::string_view{from};
    if (from_sv.empty()) {
      cnt = s.size();
      s.clear();
      return cnt;
    }
    for (; located(pos, s, from_sv); ++cnt) s.erase(pos, from_sv.size());
  }
  return cnt;
}
inline size_t
excise(std::string& s, std::span<const char> from, position pos = 0) {
  if (from.empty() || pos >= s.size()) return 0;

  std::array<bool, 256> del{};
  for (char c : from) del[static_cast<unsigned char>(c)] = true;
  size_t cnt{};

  auto* data = s.data();
  auto write = pos;
  for (auto read = pos; read < s.size(); ++read) {
    unsigned char ch = data[read];
    if (!del[ch])
      data[write++] = static_cast<char>(ch);
    else
      ++cnt;
  }
  if (cnt) s.erase(write);
  return cnt;
}
inline size_t
excise(std::string& s, std::initializer_list<char> from, position pos = 0) {
  return excise(s, std::span<const char>{from}, pos);
}
inline size_t excise(std::string& s, std::span<const std::string_view> from,
    position pos = 0) {
  if (from.empty() || pos >= s.size()) return 0;

  for (auto sv : from)
    if (sv.empty()) {
      auto cnt = s.size();
      s.clear();
      return cnt;
    }

  size_t cnt{};
  auto* data = s.data();
  auto write = pos;
  auto read = pos;
  while (read < s.size()) {
    bool matched = false;
    for (const auto& fv : from) {
      if (fv.size() && read + fv.size() <= s.size() &&
          std::memcmp(data + read, fv.data(), fv.size()) == 0)
      {
        read += fv.size();
        ++cnt;
        matched = true;
        break;
      }
    }
    if (!matched) data[write++] = data[read++];
  }
  if (cnt) s.erase(write);
  return cnt;
}
inline size_t excise(std::string& s,
    std::initializer_list<std::string_view> from, position pos = 0) {
  return excise(s, std::span<const std::string_view>{from}, pos);
}

//
// Excised.
//

// Return new string that contains `s` with `from` excised.
[[nodiscard]] std::string excised(std::string s, const auto& from) {
  auto ss = std::string{std::move(s)};
  excise(ss, from);
  return ss;
}
[[nodiscard]] inline std::string
excised(std::string s, std::initializer_list<char> from) {
  auto ss = std::string{std::move(s)};
  excise(ss, from);
  return ss;
}
[[nodiscard]] inline std::string
excised(std::string s, std::initializer_list<std::string_view> from) {
  auto ss = std::string{std::move(s)};
  excise(ss, from);
  return ss;
}
// TODO: Substituted and excised don't work for initializer lists. Wrap them.
// TODO: Excise can always be optimized to do a single pass over the string,
// never copying the tail more than once.
// TODO: Consider replacing free functions with something more object-oriented.
// For example, a `locator` constructed over `s` and `loc` (and maybe
// `as_npos`), which then has a `located` and `substituted` method. We could
// also go the other way, making `from` and `to` into objects that have a
// `locate` taking `s`, or even taking a `locator`. Point is, we're not stuck
// repeating the C RTL endlessly.
// TODO: Benchmark whether it's faster to do replacements in-place or to
// build a new string. There's also the middle ground of in-place but in a
// single pass, so long as we're not growing.
// TODO: If span size is 1, forward to regular, with pos_value hardcoded to
// 0? If string size is 1, forward to regular?
//
// Check whether examples of as_views in test are still needed.
//
// check for this:     EXPECT_EQ(strings::locate(s, {"uvw", "xyz"}),
// (location{npos, npos}));
// this should not match on span<char> here.
//
// Add tests for mismatched types in substitute/d. Add more tests for multiple
// values that are of a type convertible to `std::string_view`.
}} // namespace corvid::strings::locating

// Publish these literals corvid-wide.
namespace corvid::literals {
using namespace corvid::strings::locating::literals;
}
