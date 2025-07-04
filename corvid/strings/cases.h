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

namespace corvid::strings { inline namespace cases {

//
// Case change.
//

// Convert to uppercase.
// Avoids `std::toupper` because it's locale-dependent and slow.
[[nodiscard]] constexpr char to_upper(char c) noexcept {
  return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

// Convert to uppercase.
constexpr void to_upper(Range auto& r) noexcept {
  for (auto& ch : std::span{r}) ch = to_upper(static_cast<char>(ch));
}

// Return as uppercase.
[[nodiscard]] constexpr std::string as_upper(std::string_view sv) {
  std::string s{sv};
  to_upper(s);
  return s;
}

// Convert to lowercase.
// Avoids `std::tolower` because it's locale-dependent and slow.
[[nodiscard]] constexpr char to_lower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

// Convert to lowercase.
constexpr void to_lower(Range auto& r) noexcept {
  for (auto& ch : std::span{r}) ch = to_lower(static_cast<char>(ch));
}

// Return as lowercase.
[[nodiscard]] constexpr std::string as_lower(std::string_view sv) {
  std::string s{sv};
  to_lower(s);
  return s;
}

}} // namespace corvid::strings::cases
