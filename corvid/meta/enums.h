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
#include "./meta_shared.h"

namespace corvid { inline namespace meta { inline namespace enums {

// Enums

// Cast enum to underlying integer value.
//
// Similar to `std::to_underlying` in C++23, but more forgiving. If `E` is
// not an enum, just passes the value through unchanged.
template<typename E>
[[nodiscard]] constexpr auto as_underlying(E v) noexcept {
  if constexpr (std::is_enum_v<E>) {
    return static_cast<std::underlying_type_t<E>>(v);
  } else {
    return v;
  }
}

// Determine underlying type of enum. If not enum, harmlessly returns `E`.
template<typename E>
using as_underlying_t = decltype(as_underlying(std::declval<E>()));

PRAGMA_CLANG_DIAG(push);
PRAGMA_CLANG_IGNORED_ENUM_CONSTEXPR_CONV;

// Minimum value of enum, based on its underlying type.
template<ScopedEnum E>
constexpr auto min_scoped_enum_v =
    static_cast<E>(std::numeric_limits<as_underlying_t<E>>::min());

// Maximum value of enum, based on its underlying type.
template<ScopedEnum E>
constexpr auto max_scoped_enum_v =
    static_cast<E>(std::numeric_limits<as_underlying_t<E>>::max());

PRAGMA_CLANG_DIAG(pop);

// Note:
// std::popcount() gives us how many bits are set.
// std::bit_width() gives us how many bits are required to represent the
// value.

// Compile-time pow2.
[[nodiscard]] constexpr uint64_t pow2(uint64_t n) noexcept {
  return n < 64 ? 1ULL << n : 0ULL;
}

// Compile-time reverse of std::bit_width. Returns the highest value that can
// be encoded in `n` bits.
[[nodiscard]] constexpr uint64_t highest_value_in_n_bits(uint64_t n) noexcept {
  return pow2(n) - 1;
}

}}} // namespace corvid::meta::enums
