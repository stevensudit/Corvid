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
#include "delimiting.h"

namespace corvid::strings { inline namespace trimming {

//
// Trim
//

// For all split functions, `delim` defaults to " " and can be specified as any
// set of characters.

// Trim whitespace on left, returning part.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto trim_left(std::string_view whole, delim ws = {}) {
  auto pos = ws.find_not_in(whole);
  std::string_view part;
  if (pos != part.npos) part = whole.substr(pos);
  return R{part};
}

// Trim whitespace on right, returning part.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto
trim_right(std::string_view whole, delim ws = {}) {
  auto pos = ws.find_last_not_in(whole);
  auto part = whole.substr(0, pos + 1);
  return R{part};
}

// Trim whitespace, returning part.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto trim(std::string_view whole, delim ws = {}) {
  return trim_right<R>(trim_left(whole, ws), ws);
}

// Trim container in place.
constexpr void trim(Container auto& wholes, const delim ws = {}) {
  for (auto& item : wholes) {
    auto& part = element_value(item);
    part = trim<std::remove_reference_t<decltype(part)>>(part, ws);
  }
}

// TODO: Determine if there's a safe, correct way to pass through a temporary
// container. Maybe try binding on a defaulted parameter.

//
// Braces
//

// For braces, the `delim` is interpreted as a pair of characters.

// Trim off matching braces, returning part.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto
trim_braces(std::string_view whole, delim braces = {"[]"}) {
  auto front = braces.front();
  auto back = braces.back();
  if (whole.size() > 1 && whole.front() == front && whole.back() == back) {
    whole.remove_prefix(1);
    whole.remove_suffix(1);
  }
  return R{whole};
}

// Add braces.
[[nodiscard]] constexpr auto
add_braces(std::string_view whole, delim braces = {"[]"}) {
  std::string target;
  target.reserve(whole.size() + 2);
  target.push_back(braces.front());
  target.append(whole);
  target.push_back(braces.back());
  return target;
}

// TODO: Consider writing versions that modify strings in place.

}} // namespace corvid::strings::trimming
