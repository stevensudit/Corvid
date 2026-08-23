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
#include <string>

#include "../meta/concepts.h"
#include "delimiting.h"

namespace corvid::strings { inline namespace concatenating {

#pragma region String concatenation functions

// Variadic concatenation, so that `a += b += c` can be written without the
// parentheses that `std::string` would otherwise need.

// A part that `std::string::operator+=` can take: anything convertible to
// `std::string_view`, or a single `char`. Other integrals are excluded so
// that an `int` is never silently appended as a character.
template<typename T>
concept Concatenable = StringViewConvertible<T> || Char<T>;

// Append each of `parts`, in order, to `target`, returning it.
constexpr auto&
concat_to(std::string& target, const Concatenable auto&... parts) {
  ((target += parts), ...);
  return target;
}

// Return the concatenation of `parts`, in order, as a new string.
[[nodiscard]] constexpr std::string concat(const Concatenable auto&... parts) {
  std::string target;
  concat_to(target, parts...);
  return target;
}

// Append `head`, then each of `tail` preceded by `d`, to `target`, returning
// it.
constexpr auto& concat_with_to(std::string& target, delim d,
    const Concatenable auto& head, const Concatenable auto&... tail) {
  target += head;
  if constexpr (sizeof...(tail) > 0) { ((target += d, target += tail), ...); }
  return target;
}

// Return `head`, then each of `tail` preceded by `d`, as a new string.
[[nodiscard]] constexpr std::string concat_with(delim d,
    const Concatenable auto& head, const Concatenable auto&... tail) {
  std::string target;
  concat_with_to(target, d, head, tail...);
  return target;
}

#pragma endregion

}} // namespace corvid::strings::concatenating
