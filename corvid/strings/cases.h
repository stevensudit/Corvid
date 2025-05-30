// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2025 Steven Sudit
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

namespace corvid::strings { inline namespace cases {

//
// Case change.
//

// Convert to uppercase.
// Avoids `std::toupper` because it's locale-dependent and slow.
[[nodiscard]] inline char to_upper(char c) {
  return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

// Convert to uppercase.
inline void to_upper(Range auto& r) {
  std::span s{r};
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return toupper(c);
  });
}

// Return as uppercase.
[[nodiscard]] inline std::string as_upper(std::string_view sv) {
  std::string s{sv};
  to_upper(s);
  return s;
}

// Convert to lowercase.
// Avoids `std::tolower` because it's locale-dependent and slow.
[[nodiscard]] inline char to_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

// Convert to lowercase.
inline void to_lower(Range auto& r) {
  std::span s{r};
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return tolower(c);
  });
}

// Return as lowercase.
[[nodiscard]] inline std::string as_lower(std::string_view sv) {
  std::string s{sv};
  to_lower(s);
  return s;
}

}} // namespace corvid::strings::cases
