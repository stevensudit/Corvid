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
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>

namespace corvid { inline namespace math { inline namespace arithmetic {

#pragma region Constants

// Mathematically meaningful constants that the standard's <numbers> does not
// provide, in the same variable-template form as std::numbers. General math
// only: a value tied to a particular domain (a hexagon's apothem-to-vertex
// ratio, say) does not belong here even when it derives from one of these.

// A full turn in radians, 2 pi.
template<std::floating_point T = float>
inline constexpr T two_pi_v = std::numbers::pi_v<T> * T{2};

// The cosine of 30 degrees, sqrt(3) / 2.
template<std::floating_point T = float>
inline constexpr T cos_30_v = std::numbers::sqrt3_v<T> / T{2};

#pragma endregion
#pragma region ceil_div

// Ceiling of the integer division `n / d`: the count of `d`-sized buckets
// needed to cover `n` items. Requires `d > 0`, and `n >= 0` for signed types.
// Computes without the overflow that the `(n + d - 1) / d` idiom suffers when
// `n` is near the type's maximum.
template<std::integral T, std::integral U>
[[nodiscard]] constexpr std::common_type_t<T, U> ceil_div(T n, U d) noexcept {
  using R = std::common_type_t<T, U>;
  assert(d > 0 && "ceil_div divisor must be positive");
  if constexpr (std::is_signed_v<T>)
    assert(n >= 0 && "ceil_div dividend must be non-negative");
  const auto rn = static_cast<R>(n);
  const auto rd = static_cast<R>(d);
  return (rn / rd) + static_cast<R>(rn % rd != 0);
}

#pragma endregion
#pragma region round_up_to_multiple

// Smallest multiple of `m` not less than `n`: rounds `n` up to a multiple of
// `m`. Requires `m > 0`, and `n >= 0` for signed types. Built on `ceil_div`,
// so the division does not overflow; the final multiply can still overflow if
// the rounded result (which is less than `n + m`) exceeds the common type's
// maximum.
template<std::integral T, std::integral U>
[[nodiscard]] constexpr std::common_type_t<T, U>
round_up_to_multiple(T n, U m) noexcept {
  using R = std::common_type_t<T, U>;
  return ceil_div(n, m) * static_cast<R>(m);
}

#pragma endregion
#pragma region extract_byte

// Byte `Ndx` of `v`, counting from the least significant: `Ndx == 0` is the
// low byte, `Ndx == 1` the one above it, and so on up to the width of `T`.
//
//    extract_byte<3>(addr), extract_byte<2>(addr), extract_byte<1>(addr),
//        extract_byte<0>(addr)
//
// Unsigned only, because the byte decomposition of a negative value depends
// on its representation rather than on its value. Asking for a byte wider
// than `T` is a compile error rather than a zero: the index is fixed at the
// call site, so a byte that cannot be there is a mistake in the caller, not a
// value worth reporting.
template<size_t Ndx = 0>
[[nodiscard]] constexpr uint8_t
extract_byte(std::unsigned_integral auto v) noexcept {
  // NOLINTNEXTLINE(bugprone-sizeof-expression): the width is the real bound.
  static_assert(Ndx < sizeof(decltype(v)),
      "extract_byte index exceeds the type's width");
  return static_cast<uint8_t>(v >> (Ndx * 8));
}

#pragma endregion

}}} // namespace corvid::math::arithmetic
