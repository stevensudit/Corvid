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
#include <string_view>

namespace corvid::strings {

// A `position` is a `size_t` that represents a location within a string, with
// `npos` as the logical equivalent of the string's size.
using position = std::size_t;

// To get `npos`, use:
//  using namespace corvid::strings::literals;
inline namespace literals {

constexpr position npos = std::string_view::npos;

} // namespace literals
} // namespace corvid::strings

// Publish these literals corvid-wide.
namespace corvid::literals {
using namespace corvid::strings::literals;
}
