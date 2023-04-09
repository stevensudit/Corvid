// Corvid20: A general-purpose C++20 library extending std.
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
inline namespace containers {

// Transparent comparators allowscomparing any stringlike values without
// constructing temporary `std::string` instances from them. This works for
// `find` but not `operator[]`.
//
// For ordered collections, we need to define a `less`. For unordered, we need
// `hash` and `equal`.
//
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r3.html
struct transparent_less_stringlike {
  using is_transparent = void;

  template<typename T, typename V>
  constexpr bool operator()(const T& l, const V& r) const {
    return static_cast<std::string_view>(l) < static_cast<std::string_view>(r);
  }
};

struct transparent_hash_stringlike {
  using is_transparent = void;

  template<typename T>
  constexpr size_t operator()(const T& t) const {
    return std::hash<std::string_view>{}(static_cast<std::string_view>(t));
  }
};

struct transaprent_equal_stringlike {
  using is_transparent = void;

  template<typename T, typename V>
  constexpr bool operator()(const T& l, const V& r) const {
    return static_cast<std::string_view>(l) ==
           static_cast<std::string_view>(r);
  }
};

// Map keyed by `std::string`, with transparent search.
template<typename V = std::string,
    typename A = std::allocator<std::pair<const std::string, V>>>
using string_map = std::map<std::string, V, transparent_less_stringlike, A>;

// Set of `std::string`, with transparent search.
template<typename A = std::allocator<std::string>>
using string_set = std::set<std::string, transparent_less_stringlike, A>;

// Unordered map keyed by `std::string`, with transparent search.
template<typename V = std::string>
using string_unordered_map = std::unordered_map<std::string, V,
    transparent_hash_stringlike, transaprent_equal_stringlike>;

// Unordered set of `std::string`, with transparent search.
template<typename A = std::allocator<std::string>>
using string_unordered_set = std::unordered_set<std::string,
    transparent_hash_stringlike, transaprent_equal_stringlike>;

} // namespace containers
} // namespace corvid
