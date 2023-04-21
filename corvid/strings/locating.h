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

// Search and replace, except `search`, `find`, `replace`, and `erase` are all
// symbols in the `std` namespace, so we had to substitute `locate`.
// `substitute`, and `excise` in order to disambiguate them so as to avoid the
// conflicts that can happen despite namespaces. And, of course, to avoid any
// possible royalties.
//
// On another naming note, we use `pos` to refer to the specific variable or
// member name, while pos is more broadly a size_t that is interpreted as a
// position and supports the convention of treating `npos` as the logical
// equivalent of `size`.
//
// Since the size can be more convenient than `npos` for uses such as half-open
// intervals, we offer the option to have the size returned where `npos` would
// be. This is controlled by specializing on the `npos_value::size` enum value.
//
// The functions that locate values support both single and multiple values.
// The latter is distinct from, and more powerful than, calling once for each
// value. For example, you can swap 'a' with 'b' and 'b' with 'a' in a single
// pass, whereas two separate calls would leave you entirely without `b`s.
//
// As part of the extended expressive power, the returned position is of type
// `location`, which contains the `pos` of where the value was found, and the
// `pos_value`, which is the index of the value that was found. It has an
// `nloc` value that has `npos` in both `pos` and `pos_value`.
//
// There are many overloads, but few function names:
// - locate: Locate occurrence of any of the values in the target.
// - located: Whether any values were located, updating `pos`.
// - rlocate, rlocated: Same, but fromt the rear.
// - count_located: Count of the values.
// - substitute: Substitute all `from` with matching `to`.
// - excise: Excise all occurrences of the values.
// - substituted, excised: Same, but returning modified string.
//
// There's also `point_past`, which works with `located`, and some largely
// internal functions: `value_size`,  `min_value_size`, `as_npos`, `as_nloc`,
// and `convert::as_span`.
//
// Single-value functions accept `char` or anything that can be converted
// to `std::string_view`.
//
// Multi-value functions primarily accept `std::initializer_list<char>`,
// `std::initializer_list<std::string_view>`. They also accept
// `std::array<char>`, `std::array<std::string_view>`, `std::span<char>` or
// `std::span<std::string_view>`, as well as other types that convert to spans,
// such as `std::vector<char>` and `std::vector<std::string_view>`,.
//
// Notably absent is guaranteed support for types that convert to
// `std::string_view`, whether it's `char[]` or `corvid::cstring_view`.
// For initializer lists, simply use `std::string_view` values, such as `sv`
// literals. For cases where you have values found at runtime, such as some
// strings read from a file, the solution is to call `convert::as_views` to
// generate a `std::vector<std::string_view>`.

// You can include this namespace with `using namespace
// corvid::strings::locating;`, but it's not strictly necessary. If not,
// though, you will likely want to specify `using namespace
// corvid::literals;` to get `npos` and `nloc`.
namespace corvid::strings { inline namespace locating {

// A single value to locate, which can be a `char` or something that converts
// to a `std::string_view`. Contrast with lists of these values.
template<typename T>
concept SingleLocateValue = StringViewConvertible<T> || is_char_v<T>;

namespace convert {
// Convert a list of char values to a `std::span`, including identity. Does
// not convert any strings or string-like things.
inline constexpr std::span<const char> as_span(
    std::initializer_list<char> values) noexcept {
  return {values.begin(), values.end()};
}
inline constexpr std::span<const char> as_span(
    std::span<const char> values) noexcept {
  return values;
}

// Convert a list of `std::string_view` values to a `std::span`, including
// identity. Does not convert any strings or string-like things.
inline constexpr std::span<const std::string_view> as_span(
    std::initializer_list<std::string_view> values) noexcept {
  return {values.begin(), values.end()};
}
inline constexpr std::span<const std::string_view> as_span(
    std::span<const std::string_view> values) noexcept {
  return values;
}

// Convert anything container shaped like a `std::span` whose elements are
// convertible to `std::string_view` into a `std::vector<std::string_view>`
// that you can use for functions requiring multiple values.
//
// Note that the `std::string_view` elements do not own any memory, but
// instead use whatever the source values point at, and that the return value
// is a temporary vector, so use caution.
template<typename T>
constexpr auto as_views(const T& values) {
  std::vector<std::string_view> result;
  result.reserve(values.size());
  for (auto& value : values) result.push_back(std::string_view{value});
  return result;
}
} // namespace convert

// For `locate` on a list of values, the `location` is used to return both
// the index of where the value was located in the target and the index (in
// the value list) of which value was located. Also used as state for
// `located`. The two pos are set to `npos` when nothing was located.
struct location {
  size_t pos{};
  size_t pos_value{};

  constexpr auto operator<=>(const location&) const noexcept = default;
};

// To get `npos`, `nloc`, `npos_value`, and `nloc_value`, use:
//  using namespace corvid::literals;
inline namespace literals {
constexpr size_t npos = std::string_view::npos;
constexpr location nloc{npos, npos};

// Whether to return `npos` or `size` when nothing is found.
enum class npos_value { npos, size };
} // namespace literals

// Utility to return correct `npos` value based on `npos_value`.
template<npos_value v = npos_value::npos>
constexpr size_t
as_npos(const std::string_view& s, size_t pos = npos) noexcept {
  if constexpr (v == npos_value::size)
    if (pos == npos) pos = s.size();
  return pos;
}
// Same, but for `location`.
template<npos_value v = npos_value::npos>
constexpr location as_nloc(const std::string_view& s, const auto& values,
    size_t pos = npos, size_t pos_value = npos) noexcept {
  if constexpr (v == npos_value::size)
    if (pos == npos) {
      pos = s.size();
      pos_value = values.size();
    }
  return {pos, pos_value};
}

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

// Updates the `location` to point past the value that was just located,
// returning it as well. This can be used with `located` to loop over located
// values. Note that you cannot safely use it before the first call to
// `locate` or when `locate` fails because `pos_value` must be in range (and
// not npos).
template<typename T, ptrdiff_t mult = 1>
constexpr size_t
point_past(location& loc, std::span<const T> values) noexcept {
  assert(loc.pos_value < values.size());
  loc.pos += mult * value_size(values[loc.pos_value]);
  return loc.pos;
}
template<SingleLocateValue T>
constexpr size_t
point_past(location& loc, std::initializer_list<T> values) noexcept {
  return point_past(loc, convert::as_span(values));
}

//
// Locate
//

// Locate the first instance of a single `value` in `s`, starting at `pos`.
// Returns the index of where the value was located, or `npos`.
//
// To locate the next instance, call again with `pos` set to the returned
// pos plus the size of the located value. For `char`, the size of the
// located value is just 1. For `std::string_view`, this is its `size`.
template<npos_value v = npos_value::npos>
constexpr size_t locate(std::string_view s,
    const SingleLocateValue auto& value, size_t pos = 0) noexcept {
  if constexpr (Char<decltype(value)>)
    return as_npos<v>(s, s.find(value, pos));
  else
    return as_npos<v>(s, s.find(std::string_view{value}, pos));
}
// Same as above, but locate the last instance. To locate the previous
// instance, subtract the value size instead of adding.
template<npos_value npv = npos_value::npos>
constexpr size_t rlocate(std::string_view s,
    const SingleLocateValue auto& value, size_t pos = npos) noexcept {
  if constexpr (Char<decltype(value)>)
    return as_npos<npv>(s, s.rfind(value, pos));
  else
    return as_npos<npv>(s, s.rfind(std::string_view{value}, pos));
}

// Locate the first instance of any of the `char` `values` in `s`, starting
// at `pos`.
//
// Returns the `location`, which has both the pos of where the value was
// located in the target and the pos (in the value list) of which value was
// located. The two pos are set to `npos` when nothing was located.
//
// To locate the next instance, call again with `pos` set to the returned
// `pos`, incremented past the found value. For `char` values, the size is
// just 1.
//
// Usage:
//   auto [pos, pos_value] = locate(s, {'a', 'b', 'c'});
template<npos_value npv = npos_value::npos>
constexpr location locate(std::string_view s, std::span<const char> values,
    size_t pos = 0) noexcept {
  const auto value_sv = std::string_view{values.begin(), values.end()};
  for (; pos < s.size(); ++pos)
    for (size_t pos_value = 0; pos_value < value_sv.size(); ++pos_value)
      if (s[pos] == value_sv[pos_value]) return {pos, pos_value};
  return as_nloc<npv>(s, values);
}
template<npos_value npv = npos_value::npos>
constexpr auto locate(std::string_view s, std::initializer_list<char> values,
    size_t pos = 0) noexcept {
  return locate<npv>(s, {values.begin(), values.end()}, pos);
}
// Same as above, but locate the last instance.
template<npos_value npv = npos_value::npos>
constexpr location rlocate(std::string_view s, std::span<const char> values,
    size_t pos = npos) noexcept {
  const auto value_sv = std::string_view{values.begin(), values.end()};
  if (s.empty()) return as_nloc<npv>(s, values);
  if (pos >= s.size()) pos = s.size() - 1;
  for (++pos; pos-- > 0;)
    for (size_t pos_value = 0; pos_value < value_sv.size(); ++pos_value)
      if (s[pos] == value_sv[pos_value]) return {pos, pos_value};
  return as_nloc<npv>(s, values);
}
template<npos_value npv = npos_value::npos>
constexpr auto rlocate(std::string_view s, std::initializer_list<char> values,
    size_t pos = npos) noexcept {
  return rlocate<npv>(s, {values.begin(), values.end()}, pos);
}

// Locate the first instance of any of the `std::string_view` `values` in
// `s`, starting at `pos`.
//
// Returns the `location`, which has both the index of where the value was
// located in the target and the index (in the value list) of which value was
// located. The two loc are set to `npos` when nothing was located.
//
// To locate the next instance, call again with the `pos` set to the returned
// `pos`, incremented past the found value. For `std::string_view`, this is
// its `size`, which is most easily found by passing the return value of
// `point_past` as the new `pos`.
//
// Usage:
//   auto [pos, pos_value] = locate(s, {"abc", "def", "ghi"});
template<npos_value npv = npos_value::npos>
constexpr struct location locate(std::string_view s,
    std::span<const std::string_view> values, size_t pos = 0) noexcept {
  for (; pos <= s.size(); ++pos)
    for (size_t pos_value = 0; pos_value < values.size(); ++pos_value)
      if (s.substr(pos, values[pos_value].size()) == values[pos_value])
        return {pos, pos_value};
  return as_nloc<npv>(s, values);
}
template<npos_value npv = npos_value::npos>
constexpr auto locate(std::string_view s,
    std::initializer_list<std::string_view> values, size_t pos = 0) noexcept {
  return locate<npv>(s, std::span{values.begin(), values.end()}, pos);
}
// Same as above, but locate the last instance.
template<npos_value npv = npos_value::npos>
constexpr struct location rlocate(std::string_view s,
    std::span<const std::string_view> values, size_t pos = npos) noexcept {
  if (s.empty()) return as_nloc<npv>(s, values);
  if (pos >= s.size()) pos = s.size() - 1;
  for (++pos; pos-- > 0;)
    for (size_t pos_value = 0; pos_value < values.size(); ++pos_value)
      if (s.substr(pos, values[pos_value].size()) == values[pos_value])
        return {pos, pos_value};
  return as_nloc<npv>(s, values);
}
template<npos_value npv = npos_value::npos>
constexpr auto
rlocate(std::string_view s, std::initializer_list<std::string_view> values,
    size_t pos = npos) noexcept {
  return rlocate<npv>(s, std::span{values.begin(), values.end()}, pos);
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
template<npos_value npv = npos_value::npos>
constexpr bool located(size_t& pos, std::string_view s,
    const SingleLocateValue auto& value) noexcept {
  return (pos = locate<npv>(s, value, pos)) != as_npos<npv>(s);
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
template<npos_value npv = npos_value::npos>
constexpr bool located(location& loc, std::string_view s,
    std::span<const char> values) noexcept {
  return (loc = locate<npv>(s, values, loc.pos)).pos != as_npos<npv>(s);
}
template<npos_value npv = npos_value::npos>
constexpr bool located(location& loc, std::string_view s,
    std::span<const std::string_view> values) noexcept {
  return (loc = locate<npv>(s, values, loc.pos)).pos != as_npos<npv>(s);
}
template<npos_value npv = npos_value::npos, typename T>
constexpr bool located(location& loc, std::string_view s,
    std::initializer_list<T> values) noexcept {
  return located<npv>(loc, s, convert::as_span(values));
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
// `pos`. Note that empty values or an empty `std::string_view` value causes
// an infinite loop.
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
    std::span<const std::string_view> from,
    std::span<const std::string_view> to, size_t pos = 0) {
  assert(from.size() == to.size() && "from and to must be same size");
  assert(min_value_size(from) > 0 && "from is empty");
  size_t cnt{};
  for (location loc{pos, 0}; located(loc, std::string_view{s}, from);
       ++cnt, point_past(loc, from))
    s.replace(loc.pos, value_size(from[loc.pos_value]), to[loc.pos_value]);
  return cnt;
}
template<SingleLocateValue T>
size_t substitute(std::string& s, std::initializer_list<T> from,
    std::initializer_list<T> to, size_t pos = 0) {
  return substitute(s, convert::as_span(from), convert::as_span(to), pos);
}
// Disambiguate from `std::span<const char>`.
inline size_t
substitute(std::string& s, const char* from, const char* to, size_t pos = 0) {
  return substitute(s, std::string_view{from}, std::string_view{to}, pos);
}
inline size_t substitute(std::string& s, const std::string& from,
    const std::string& to, size_t pos = 0) {
  return substitute(s, std::string_view{from}, std::string_view{to}, pos);
}

//
// Substituted
//

// Return new string that contains `s` with `from` replaced with `to`.
[[nodiscard]] inline std::string
substituted(std::string s, const auto& from, const auto& to) noexcept {
  auto ss = std::string{std::move(s)};
  substitute(ss, from, to);
  return ss;
}

// TODO: Add excise, excised, rlocated, as the docs promise.
// Note that excise can always be optimized to do a single pass over the
// string, never copying the tail more than once.
// TODO: Consider replacing free functions with something more
// object-oriented. For example, a `locator` constructed over `s` and `loc`
// (and maybe `as_npos`), which then has a `located` and `substituted`
// method. We could also go the other way, making `from` and `to` into
// objects that have a `locate` taking `s`, or even taking a `locator`. Point
// is, we're not stuck repeating the C RTL endlessly.
// TODO: Benchmark whether it's faster to do replacements in-place or to
// build a new string. There's also the middle ground of in-place but in a
// single pass, so long as we're not growing.
// TODO: If span size is 1, forward to regular, with pos_value hardcoded to
// 0? If string size is 1, forward to regular?
// TODO:
// do rlocated. It starts off npos, ends off npos
// check for this:     EXPECT_EQ(strings::locate(s, {"uvw", "xyz"}),
// (location{npos, npos}));
// this should not match on span<char> here.
// Maybe we need an initializer list of char* that asserts!
// Sanity check: confirm that behavior with empty value matches std.
// TODO: Maybe we can improve support for multi-values of `std::string` and
// classes that convert cheaply to `std::string_view`. It would require going
// back to using a templated type at the lowest level and probably doing a
// last-second cast, but I don't think there's any way to avoid repeated
// strlen for char*. A related trick would be to have a fixed-size local
// array of `std::string_view` and fill it from the initializer list with a
// `std::transform` that converts each element. This could be avoided when
// the type is good enough, or maybe optimized away when it's a no-op. It
// generates warnings, but gcc is happy to use a runtime-sized local array but
// it won't optimize it away. But this is not a priority: the caller can always
// use `convert::as_values` and explicitly take the hit. The counterargument is
// that there shouldn't be a hit for `std::string`. For prior art:
// http://howardhinnant.github.io/short_alloc.h

}} // namespace corvid::strings::locating

// Publish these literals corvid-wide.
namespace corvid::literals {
using namespace corvid::strings::locating::literals;
}
