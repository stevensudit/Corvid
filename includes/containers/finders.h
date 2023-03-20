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
#include "containers_shared.h"

namespace corvid {
inline namespace finders {

// Search container for key `k`, returning `optional_ptr` to element. When a
// search fails to find anything, the `has_value` of the return is false.
//
// Uses `find` method if available, `std::find` otherwise.
//
// Works for `std::vector`, `std::set`, `std::map`, `std::array`, and similar
// classes. Returns pointer to found value, which means that, for keyed
// collections such as `std::map`, it points to the `pair.second`, not the
// `pair` (unless `keyed` is set).
//
// For `std::string` and `std::string_view`, whether you search for a single
// character or a sub-string, the return value points to the first found
// character.
//
// Even works for arrays, but not arrays decayed into pointers (because we
// can't determine the size, then).
template<bool keyed = false>
[[nodiscard]] auto find_opt(Findable auto& c, const auto& k) {
  return internal::optional_ptr{it_to_ptr<keyed>(c, c.find(k))};
}

template<bool keyed = false>
[[nodiscard]] auto find_opt(RangeWithoutFind auto& c, const auto& k) {
  using namespace std;
  return internal::optional_ptr{
      it_to_ptr<keyed>(c, std::find(begin(c), end(c), k))};
}

// Determine whether the container has the key.
[[nodiscard]] bool contains(auto& c, const auto& k) {
  return find_opt(c, k).has_value();
}

} // namespace finders
} // namespace corvid
