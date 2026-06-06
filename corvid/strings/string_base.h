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
#include <string_view>

#include "../meta.h"

namespace corvid::strings {

// A `position` is a `size_t` that represents a location within a string, with
// `npos` as the logical equivalent of the string's size.
using position = std::size_t;

// To get `npos`, use:
//  using namespace corvid::strings::literals;
inline namespace literals {

constexpr position npos = std::string_view::npos;

// View any string-like value as a `std::basic_string_view` of its own
// code-unit type. This is how the generic operations recover the view (and its
// code unit) from an argument that may be a `std::string`, a literal, a raw
// pointer, or a view wrapper, while keeping the implicit-conversion ergonomics
// that a bare `std::basic_string_view` parameter would lose.
template<StringViewLike S>
[[nodiscard]] constexpr auto as_view(const S& s) noexcept {
  return std::basic_string_view<char_type_of_t<S>>{s};
}

} // namespace literals
} // namespace corvid::strings

// Publish these literals corvid-wide.
namespace corvid::literals {
using namespace corvid::strings::literals;
}
