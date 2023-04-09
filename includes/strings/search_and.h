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

namespace corvid::strings {
inline namespace search_and {

//
// Search and Replace
//

// Return whether `value` was found in `s`, starting at `ndx`, updating `ndx`.
bool found_next(size_t& ndx, std::string_view s, const auto& value) {
  return (ndx = s.find(value, ndx)) != npos;
}

// Replace instances of `from` in `s` with `to`, returning count of
// replacements.
inline size_t
replace(std::string& s, std::string_view from, std::string_view to) {
  size_t cnt{};
  for (size_t ndx{}; found_next(ndx, s, from); ndx += to.size()) {
    ++cnt;
    s.replace(ndx, from.size(), to);
  }
  return cnt;
}

// Replace instances of `from` in `s` with `to`, returning count of
// replacements.
inline size_t replace(std::string& s, char from, char to) {
  size_t cnt{};
  for (size_t ndx{}; found_next(ndx, s, from); ++ndx) {
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

} // namespace search_and
} // namespace corvid::strings
