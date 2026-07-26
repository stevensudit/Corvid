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
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

#include "concepts.h"

namespace corvid { inline namespace meta { inline namespace containers {

#pragma region extract_field

// Extract just the value or the entire key-value pair.
enum class extract_field : bool { value, key_value };

#pragma endregion
#pragma region Element access

// References value from container element, based on `field`.
//
// The result is a true lvalue reference, so ranges with proxy references
// (such as `std::vector<bool>`) are unsupported and fail to compile.
template<auto field = extract_field::value>
[[nodiscard]] constexpr auto& element_value(auto&& e) noexcept {
  if constexpr (StdPair<decltype(e)> && field == extract_field::value)
    return e.second;
  else
    return e;
}

// Value type that `element_value` yields for a range's elements: the mapped
// value type for map-like ranges (pairs), otherwise the element type itself.
template<typename Cont>
using element_value_t = std::remove_cvref_t<decltype(element_value(
    std::declval<std::ranges::range_value_t<Cont>&>()))>;

// References value from container iterator, based on `field`
template<auto field = extract_field::value>
[[nodiscard]] constexpr auto& container_element_v(auto&& it) {
  return element_value<field>(*it);
}

// Extract pointer from the container and iterator. Handles case of iterator
// instead being an index, such as with `std::string`.
//
// Returns pointer to the found value, so for keyed collections such as
// `std::map`, it points to the `pair.second`, not the `pair`, unless `field`
// is `extract_field::key_value`.
template<auto field = extract_field::value>
[[nodiscard]] constexpr auto it_to_ptr(auto& c, Dereferenceable auto&& it) {
  // Enable ADL to find appropriate end() for custom container types.
  using namespace std;
  return (it != end(c)) ? &container_element_v<field>(it) : nullptr;
}

// Extract pointer from the container and index. Handles case of iterator
// instead being an index, such as with `std::string`.
//
// Returns pointer to the found value, so for keyed collections such as
// `std::map`, it points to the `pair.second`, not the `pair`, unless `field`
// is `extract_field::key_value`.
//
// Note: Uses -1 as sentinel for "not found". An unsigned index at least as
// wide as `int` works just as well: the comparison converts -1 to the
// unsigned type's maximum, which is exactly the `npos` convention of string
// find operations. A narrower unsigned type instead promotes to `int`, so its
// maximum never equals -1 and is treated as a valid index.
template<auto field = extract_field::value>
[[nodiscard]] constexpr auto it_to_ptr(auto& c, Integer auto ndx) {
  return (ndx != -1) ? &container_element_v<field>(&c[ndx]) : nullptr;
}

#pragma endregion

}}} // namespace corvid::meta::containers
