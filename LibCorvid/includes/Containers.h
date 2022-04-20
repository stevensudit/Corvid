// Corvid: A general-purpose C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022 Steven Sudit
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
#include "OptionalPtr.h"

namespace corvid {
inline namespace containers {

// Extract pointer from the container and iterator. Handles case of iterator
// instead being an index, such as with `std::string`.
//
// Returns pointer to the found value, so for keyed collections such as
// `std::map`, it points to the `pair.second`, not the `pair`.
//
// This is primarily a helper method for `find_opt`.
[[nodiscard]] auto it_to_ptr(auto& c, auto&& it) {
  using namespace std;
  if constexpr (is_dereferenceable_v<decltype(it)>)
    return it != end(c) ? &container_element_v(it) : nullptr;
  else
    return it != -1 ? &container_element_v(&c[it]) : nullptr;
}

// Search container for key, returning `optional_ptr` to element. When a search
// fails to find anything, the `has_value` of the return is false.
//
// Uses `find` method if available, `std::find` otherwise.
//
// Works for `std::vector`, `std::set`, `std::map`, `std::array`, and similar
// classes. Returns pointer to found value, which means that, for keyed
// collections such as `std::map`, it points to the `pair.second`, not the
// `pair`.
//
// For `std::string` and `std::string_view`, whether you search for a single
// character or a sub-string, the return value points to the found character.
//
// Even works for arrays, but not arrays decayed into pointers (because we
// can't determine the size, then).
[[nodiscard]] auto find_opt(auto& c, const auto& k) {
  using namespace std;
  if constexpr (has_find_v<decltype(c), decltype(k)>)
    return optional_ptr{it_to_ptr(c, c.find(k))};
  else
    return optional_ptr{it_to_ptr(c, std::find(begin(c), end(c), k))};
}

// Determine whether the container has the key.
[[nodiscard]] bool contains(auto& c, const auto& k) {
  return find_opt(c, k).has_value();
}

// The transparent comparator allows comparing any stringlike values without
// constructing temporary strings from them.
//
// By specifying it as the `less<>` replacement in a container that supports
// `is_transparent` (currently `std::map` and `std::set`, but also
// `std::unordered_set` and `std::unordered_map` in C++20), methods such as
// `find` take advantage of this copy-free comparison. The `string_map<V>` and
// `string_set` aliases do this for you.
//
// The comparator should also work for collections keyed on anything else that
// can be cast to `std::string_view`.
//
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r3.html
struct transparent_less_stringlike {
  using is_transparent = void;

  template<typename T, typename U>
  constexpr bool operator()(const T& l, const U& r) const {
    return static_cast<std::string_view>(l) < static_cast<std::string_view>(r);
  }
};

// Map keyed by `std::string`, with transparent search.
template<typename V = std::string,
    typename A = std::allocator<std::pair<const std::string, V>>>
using string_map = std::map<std::string, V, transparent_less_stringlike, A>;

// Set of `std::string`, with transparent search.
template<typename A = std::allocator<std::string>>
using string_set = std::map<std::string, transparent_less_stringlike, A>;

} // namespace containers
} // namespace corvid

// TODO: Consider setting up defaulted but overrideable `keyed` for
// `container_element_v`.
