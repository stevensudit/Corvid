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
#include "./meta_shared.h"

namespace corvid { inline namespace meta { inline namespace enums {

// Enums

// Cast enum to underlying integer value.
//
// Similar to `std::to_underlying_type` in C++23, but more forgiving. If `E` is
// not an enum, just passes the value through unchanged.
template<typename E>
constexpr auto as_underlying(E v) noexcept {
  if constexpr (std::is_enum_v<E>) {
    return static_cast<std::underlying_type_t<E>>(v);
  } else {
    return v;
  }
}

// Determine underlying type of enum. If not enum, harmlessly returns `E`.
template<typename E>
using as_underlying_t = decltype(as_underlying(std::declval<E>()));

// Cast underlying value to enum.
//
// Similar to `static_cast<E>(U)` except that, when `E` isn't an enum, instead
// returns a default-constructed `X`.
//
// If this seems like a strange thing to want to do, you're not wrong, but it
// turns out to be surprisingly useful.
template<typename E, typename X = std::byte, typename V>
constexpr auto from_underlying(const V& u) {
  if constexpr (ScopedEnum<E>) {
    return static_cast<E>(u);
  } else {
    return X{};
  }
}

// Minimum value of enum, based on its underlying type.
template<ScopedEnum E>
constexpr auto min_scoped_enum_v =
    static_cast<E>(std::numeric_limits<as_underlying_t<E>>::min());

// Maximum value of enum, based on its underlying type.
PRAGMA_CLANG_DIAG(push);
PRAGMA_CLANG_IGNORED("-Wenum-constexpr-conversion");
template<ScopedEnum E>
constexpr auto max_scoped_enum_v =
    static_cast<E>(std::numeric_limits<as_underlying_t<E>>::max());
PRAGMA_CLANG_DIAG(pop);

// Note:
// std::popcount() gives us how many bits are set.
// std::bit_width() gives us how many bits are required to represent the
// value.

// Compile-time pow2.
constexpr uint64_t pow2(uint64_t n) { return n < 64 ? 1ull << n : 0ull; }

// Compile-time reverse of std::bit_width. Returns the highest value that can
// be encoded in `n` bits.
constexpr uint64_t highest_value_in_n_bits(uint64_t n) { return pow2(n) - 1; }

}}} // namespace corvid::meta::enums
