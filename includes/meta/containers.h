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
#include "./meta_shared.h"
#include "./concepts.h"

namespace corvid::meta {
inline namespace containers {

// Containers

// References value from container element, including the key if `keyed`.
template<bool keyed = false>
[[nodiscard]] constexpr auto& element_value(auto&& e) {
  if constexpr (StdPair<decltype(e)> && !keyed)
    return e.second;
  else
    return e;
}

// References value from container iterator, including the key if `keyed`.
template<bool keyed = false>
[[nodiscard]] constexpr auto& container_element_v(auto&& it) {
  return element_value<keyed>(*it);
}

// Extract pointer from the container and iterator. Handles case of iterator
// instead being an index, such as with `std::string`.
//
// Returns pointer to the found value, so for keyed collections such as
// `std::map`, it points to the `pair.second`, not the `pair`, unless `keyed`
// is set.
template<bool keyed = false>
[[nodiscard]] auto it_to_ptr(auto& c, Dereferenceable auto&& it) {
  using namespace std;
  return it != end(c) ? &container_element_v<keyed>(it) : nullptr;
}

// Extract pointer from the container and index. Handles case of iterator
// instead being an index, such as with `std::string`.
//
// Returns pointer to the found value, so for keyed collections such as
// `std::map`, it points to the `pair.second`, not the `pair`, unless `keyed`
// is set.
template<bool keyed = false>
[[nodiscard]] auto it_to_ptr(auto& c, Integral auto ndx) {
  return ndx != -1 ? &container_element_v<keyed>(&c[ndx]) : nullptr;
}

} // namespace containers
} // namespace corvid::meta
