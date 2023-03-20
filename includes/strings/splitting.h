// Corvid20: A general-purpose C++ 20 library extending std.
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
#include "delimiting.h"

namespace corvid::strings {
inline namespace splitting {

//
// Split
//

// For all split functions, `delim` defaults to " " and can be specified as any
// set of characters.

// Extract next delimited piece destructively from `whole`.
//
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto
extract_piece(std::string_view& whole, delim d = {}) {
  auto pos = std::min(whole.size(), d.find_in(whole));
  auto part = whole.substr(0, pos);
  whole.remove_prefix(std::min(whole.size(), pos + 1));
  return R{part};
}

// Extract next delimited piece into `part`, removing it from `whole`.
//
// Returns true so long as there's more work to do.
// Specify R as `std::string` to make a deep copy.
template<typename R>
[[nodiscard]] constexpr bool
more_pieces(R& part, std::string_view& whole, delim d = {}) {
  auto all = whole.size();
  part = extract_piece<R>(whole, d);
  return part.size() != all;
}

// Split all pieces by delimiters and return parts in vector.
//
// Does not omit empty parts.
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto split(std::string_view whole, delim d = {}) {
  std::vector<R> parts;
  std::string_view part;
  for (bool more = !whole.empty(); more;) {
    more = more_pieces(part, whole, d);
    parts.push_back(R{part});
  }
  return parts;
}

// Split a temporary string by delimiters, making deep copies of the parts.
[[nodiscard]] inline constexpr auto split(std::string&& whole, delim d = {}) {
  return split<std::string>(std::string_view(whole), d);
}

} // namespace splitting
} // namespace corvid::strings
