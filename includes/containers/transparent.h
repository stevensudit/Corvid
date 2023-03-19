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

namespace covid {
inline namespace containers {

// The transparent comparator allows comparing any stringlike values without
// constructing temporary `std::string` instances from them.
//
// By specifying it as the `less<>` replacement in a container that supports
// `is_transparent` (currently `std::map` and `std::set`, but also
// `std::unordered_set` and `std::unordered_map`, starting in C++20), methods
// such as `find` take advantage of this copy-free comparison.
//
// The `string_map<V>` and `string_set` aliases do this for you.
//
// The comparator should also work for collections keyed on anything else that
// can be cast to `std::string_view`.
//
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r3.html
struct transparent_less_stringlike {
  using is_transparent = void;

  template<typename T, typename V>
  constexpr bool operator()(const T& l, const V& r) const {
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

// TODO: Add unsorted_map and unsorted_set.

} // namespace containers
} // namespace covid