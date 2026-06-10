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
#include <type_traits>

namespace corvid { inline namespace math { inline namespace arithmetic {

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

#pragma endregion ceil_div

}}} // namespace corvid::math::arithmetic
