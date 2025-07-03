// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2025 Steven Sudit
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
#include "optional_ptr.h"

namespace corvid { inline namespace opt_find {

// Search container for key `k`, returning `optional_ptr` to element. When a
// search fails to find anything, the `has_value` of the return is false.
//
// Uses `find` method on `T::key_type` if available, `std::find` otherwise.
// Note that this means it only uses the find method on associative containers,
// not strings.
//
// Works for `std::vector`, `std::set`, `std::map`, `std::array`, and similar
// classes. Returns pointer to found value, which means that, for keyed
// collections such as `std::map`, it points to the `pair.second`, not the
// `pair` (unless `field` is `key_value`).
//
// For `std::string` and `std::string_view`, searches for a single character
// using `std::find`. Even works for arrays, but not arrays decayed into
// pointers (because we can't determine the size, then).
template<auto field = extract_field::value>
[[nodiscard]] constexpr auto find_opt(KeyFindable auto&& c, const auto& k) {
  return internal::optional_ptr{it_to_ptr<field>(c, c.find(k))};
}

template<auto field = extract_field::value>
[[nodiscard]] constexpr auto
find_opt(RangeWithoutFind auto&& c, const auto& k) {
  using namespace std;
  return internal::optional_ptr{
      it_to_ptr<field>(c, std::find(begin(c), end(c), k))};
}

// Determine whether the container has the key.
[[nodiscard]] constexpr bool contains(auto&& c, const auto& k) {
  return find_opt(c, k).has_value();
}

}} // namespace corvid::opt_find
