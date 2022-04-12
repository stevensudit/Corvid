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

// Extract pointer from the container and iterator. Handles case of iterator
// instead being an index, such as with std::string.
//
// Returns pointer to the found value, so for keyed collections such as
// std::map, it points to the pair.second, not the pair.
//
// This is primarily a helper method for `find_opt`.
[[nodiscard]] auto it_to_ptr(auto& c, auto&& it) {
  using namespace std;
  if constexpr (is_dereferenceable_v<decltype(it)>)
    return it != end(c) ? &container_element_v(it) : nullptr;
  else
    return it != -1 ? &container_element_v(&c[it]) : nullptr;
}

// Search container for key, returning `corvid::optional_ptr` to element. This
// means that, when a search fails to find anything, the `has_value` of the
// return is false.
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

} // namespace corvid
