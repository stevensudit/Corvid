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
#include "strings_shared.h"
#include "../meta/concepts.h"
#include "delimiting.h"

namespace corvid::strings { inline namespace trimming {

#pragma region Trim

// For all trim functions, the delimiter defaults to a single space and can be
// any set of characters of the haystack's code unit. The return type `R`
// defaults to a view of the haystack; pass an owning string type to get a
// copy.

// Trim whitespace on left, returning part. When all whitespace, returns empty.
template<StringViewLike S,
    typename R = std::basic_string_view<char_type_of_t<S>>>
[[nodiscard]] constexpr auto
trim_left(const S& whole, basic_delim<char_type_of_t<S>> ws = {}) {
  using C = char_type_of_t<S>;
  const std::basic_string_view<C> sv{as_view(whole)};
  auto pos = ws.find_not_in(sv);
  std::basic_string_view<C> part;
  if (pos != part.npos) part = sv.substr(pos);
  return R{part};
}

// Trim whitespace on right, returning part. When all whitespace, returns
// empty.
template<StringViewLike S,
    typename R = std::basic_string_view<char_type_of_t<S>>>
[[nodiscard]] constexpr auto
trim_right(const S& whole, basic_delim<char_type_of_t<S>> ws = {}) {
  using C = char_type_of_t<S>;
  const std::basic_string_view<C> sv{as_view(whole)};
  auto pos = ws.find_last_not_in(sv);
  return R{sv.substr(0, pos + 1)};
}

// Trim whitespace, returning part.
template<StringViewLike S,
    typename R = std::basic_string_view<char_type_of_t<S>>>
[[nodiscard]] constexpr auto
trim(const S& whole, basic_delim<char_type_of_t<S>> ws = {}) {
  return R{trim_right(trim_left(whole, ws), ws)};
}

// Trim each container element in place. Like the scalar overloads, the
// delimiter defaults to a single space of the elements' code unit, deduced
// from the element value type (the mapped value for map-like containers).
template<Container Cont, CharType Char = char_type_of_t<element_value_t<Cont>>>
constexpr void trim(Cont& wholes, basic_delim<Char> ws = {}) {
  for (auto& item : wholes) {
    auto& part = element_value(item);
    using P = std::remove_cvref_t<decltype(part)>;
    part = trim<P, P>(part, ws);
  }
}

// Trim whitespace on left in place.
template<typename C>
void trim_left(std::basic_string<C>& whole, basic_delim<C> ws = {}) {
  const std::basic_string_view<C> sv{whole};
  auto pos = ws.find_not_in(sv);
  if (pos == sv.npos)
    whole.clear();
  else if (pos)
    whole.erase(0, pos);
}

// Trim whitespace on right in place.
template<typename C>
void trim_right(std::basic_string<C>& whole, basic_delim<C> ws = {}) {
  const std::basic_string_view<C> sv{whole};
  auto pos = ws.find_last_not_in(sv);
  if (pos == sv.npos)
    whole.clear();
  else if (pos + 1 < whole.size())
    whole.erase(pos + 1);
}

// Trim whitespace in place.
template<typename C>
void trim(std::basic_string<C>& whole, basic_delim<C> ws = {}) {
  const std::basic_string_view<C> sv{whole};
  auto left = ws.find_not_in(sv);
  if (left == sv.npos) {
    whole.clear();
    return;
  }
  auto right = ws.find_last_not_in(sv);
  whole.erase(right + 1);
  if (left) whole.erase(0, left);
}

#pragma endregion
#pragma region Braces

// Backing storage for the default square-bracket brace pair, per code unit.
template<CharType Char>
inline constexpr Char square_braces[] = {Char('['), Char(']')};

// The default brace pair ("[]") for the given code unit.
template<CharType Char>
[[nodiscard]] constexpr basic_delim<Char> bracket_delim() noexcept {
  return basic_delim<Char>{
      std::basic_string_view<Char>{square_braces<Char>, 2}};
}

// For braces, the delimiter is interpreted as a pair of characters.

// Trim off matching braces, returning part.
template<StringViewLike S,
    typename R = std::basic_string_view<char_type_of_t<S>>>
[[nodiscard]] constexpr auto trim_braces(const S& whole,
    basic_delim<char_type_of_t<S>> braces =
        bracket_delim<char_type_of_t<S>>()) {
  using C = char_type_of_t<S>;
  std::basic_string_view<C> sv{as_view(whole)};
  auto front = braces.front();
  auto back = braces.back();
  if (sv.size() > 1 && sv.front() == front && sv.back() == back) {
    sv.remove_prefix(1);
    sv.remove_suffix(1);
  }
  return R{sv};
}

// Add braces.
template<StringViewLike S>
[[nodiscard]] constexpr auto add_braces(const S& whole,
    basic_delim<char_type_of_t<S>> braces =
        bracket_delim<char_type_of_t<S>>()) {
  using C = char_type_of_t<S>;
  const std::basic_string_view<C> sv{as_view(whole)};
  std::basic_string<C> target;
  target.reserve(sv.size() + 2);
  target.push_back(braces.front());
  target.append(sv);
  target.push_back(braces.back());
  return target;
}

#pragma endregion
}} // namespace corvid::strings::trimming
