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
#include <variant>
#include <vector>
#include "strings_shared.h"

namespace corvid::strings { inline namespace any_strings_types {

// A single string, a vector of strings, or `std::monostate`.
using any_strings =
    std::variant<std::monostate, std::string, std::vector<std::string>>;

// `make_string_vector`: Efficiently fill a vector with strings by moving them
// in. If no parameters, returns an empty vector.
template<typename... Strings>
requires(std::same_as<Strings, std::string> && ...)
[[nodiscard]] inline std::vector<std::string>
make_string_vector(Strings&&... strings) {
  auto result = std::vector<std::string>{};
  result.reserve(sizeof...(Strings));
  (result.emplace_back(std::move(strings)), ...);
  return result;
}

// `make_any_strings`: Make an `any_strings` out of the parameters, moving them
// in. If no parameters, returns `std::monostate`.
template<typename... Strings>
requires(std::same_as<Strings, std::string> && ...)
[[nodiscard]] inline any_strings make_any_strings(Strings&&... strings) {
  if constexpr (sizeof...(Strings) == 0) {
    return std::monostate{};
  } else if constexpr (sizeof...(Strings) == 1) {
    return any_strings{std::in_place_type<std::string>, std::move(strings)...};
  } else {
    return any_strings{std::in_place_type<std::vector<std::string>>,
        make_string_vector(std::move(strings)...)};
  }
}

}} // namespace corvid::strings::any_strings_types
