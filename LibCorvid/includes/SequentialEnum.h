// Corvid: A general-purpose C++ library extending std.
// https://github.com/stevensudit/Corvid
//
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
#include <limits>

#include "Interval.h"

// If you don't import the `corvid` namespace, you should at least import
// `corvid::seq_ops`.
namespace corvid {
inline namespace seq_enum {

//
// Sequential enum (seq_enum)
//

// seq_max_v
//
// Allow a scoped enum (aka `enum class`) to be used sequentially, supporting
// add and subtract, while providing some additional functionality.
//
// Prerequisites: Your scoped enum must have a minimum and maximum valid value,
// and all values between these, inclusive, must be valid. This range doesn't
// have to start at 0, and if the underlying type is signed, then it can even
// start as negative. Valid values do not need to be named.
//
// To enable sequential behavior for your scoped enum, specialize the
// `seq_max_v` constant, setting it to the highest enum value that is valid. If
// the lowest value isn't 0, then also specialize `seq_min_v` to that.
//
// (In the degenerate case of having only one valid value, you'll need to
// override `seq_valid_v`, setting it to `true`.)
//
// To enable wrapping, which ensures that operations keep values within valid
// range (at the cost of range checks), specialize `seq_wrap_v` to `true`.
//
// For example:
//
//    enum class tiger_pick { eeny, meany, miny, moe };
//
//    template<>
//    constexpr auto corvid::seq_max_v<tiger_pick> = tiger_pick::moe;
template<typename E>
constexpr auto seq_max_v = from_underlying<E>(0);

// Minimum value. Specialize this if range does not start at 0. Must be less
// than `seq_max_v`.
template<typename E>
constexpr auto seq_min_v = from_underlying<E>(0);

// Whether to wrap all calculations to keep them in range. Specialize this to
// enable range checks that wrap the value.
template<typename E>
constexpr bool seq_wrap_v = false;

// Whether seq_enum is enabled. Specialize if not automatically detected.
template<typename E>
constexpr bool
    seq_valid_v = as_underlying(seq_max_v<E>) - as_underlying(seq_min_v<E>);

} // namespace seq_enum
inline namespace helpers {

// Maximum numerical value. (Do not specialize this.)
template<typename E>
constexpr auto seq_max_num_v = as_underlying(seq_max_v<E>);

// Minimum numerical value. (Do not specialize this.)
template<typename E>
constexpr auto seq_min_num_v = as_underlying(seq_min_v<E>);

// Number of distinct values in range. (Do not specialize this.)
template<typename E>
constexpr auto seq_size_v = seq_max_num_v<E> - seq_min_num_v<E> + 1;

// Whether wrapping is really enabled. We don't need to wrap when the range
// exactly fits the underlying type. (Do not specialize this.)
template<typename E>
constexpr bool seq_actually_wrap_v = (seq_size_v<E> != 0) && seq_wrap_v<E>;

// Enable if registered as valid.
template<typename T>
using enable_if_seq_enum_0 = enable_if_0<seq_valid_v<T>>;

// Clip, unless `noclip`, by modding to size.
template<typename E, bool noclip = false, enable_if_seq_enum_0<E> = 0>
constexpr auto clip(std::underlying_type_t<E> u) {
  if constexpr (!noclip && seq_size_v<E> != 0)
    return u % seq_size_v<E>;
  else
    return u;
}

// Clip if wrapping enabled.
template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr auto clip_if_wrap(std::underlying_type_t<E> u) {
  return clip<E, !seq_actually_wrap_v<E>>(u);
}

} // namespace helpers
inline namespace seq_ops {

// Dereference operator.
//
// The precedent for this is `std::optional`.
template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr auto operator*(E v) noexcept {
  return as_underlying<E>(v);
}

//
// Addition operators
//

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E operator+(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, true>(*l + clip_if_wrap<E>(r));
}

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E operator+(std::underlying_type_t<E> l, E r) noexcept {
  return r + l;
}

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E& operator+=(E& l, std::underlying_type_t<E> r) noexcept {
  return l = l + r;
}

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E& operator++(E& l) noexcept {
  if constexpr (seq_actually_wrap_v<E>) {
    if (l == seq_max_v<E>) return l = seq_min_v<E>;
  }
  return l = E{*l + 1};
}

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E operator++(E& l, int) noexcept {
  auto o = l;
  ++l;
  return o;
}

//
// Subtraction operators
//

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E operator-(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, true>(*l - clip_if_wrap<E>(r));
}

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E& operator-=(E& l, std::underlying_type_t<E> r) noexcept {
  return l = l - r;
}

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E& operator--(E& l) noexcept {
  if constexpr (seq_actually_wrap_v<E>) {
    if (*l == seq_min_num_v<E>) return l = seq_max_v<E>;
  }
  return l = E{*l - 1};
}

template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E operator--(E& l, int) noexcept {
  auto o = l;
  --l;
  return o;
}

} // namespace seq_ops
inline namespace seq_enum {

//
// Named functions.
//

// Cast seq_enum to specified integral type.
//
// Like `std::to_integer<IntegerType>(std::byte)`.
template<typename T, typename E,
    enable_if_0<seq_valid_v<E> && std::is_integral_v<T>> = 0>
constexpr T to_integer(E v) noexcept {
  return static_cast<T>(v);
}

// Maximum value.
template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E max_value() noexcept {
  return seq_max_v<E>;
}

// Minimum value.
template<typename E, enable_if_seq_enum_0<E> = 0>
constexpr E min_value() noexcept {
  return seq_min_v<E>;
}

// Cast integer value from underlying type to seq_enum, wrapping to keep it in
// range.
template<typename E, bool noclip = false, enable_if_seq_enum_0<E> = 0>
constexpr E make_safely(std::underlying_type_t<E> u) noexcept {
  // Wrapping only meaningful if underlying type is not a perfect fit.
  if constexpr (seq_size_v<E> != 0) {
    constexpr auto lo = seq_min_num_v<E>, hi = seq_max_num_v<E>;
    static_assert(lo <= hi);
    using U = std::underlying_type_t<E>;

    // Underflow is only possible if it starts above the min.
    if constexpr (lo != std::numeric_limits<U>::min()) {
      if (u < lo) return E(hi + clip<E, noclip>(u - lo + 1));
    }
    // Overflow is only possible if it ends below the max.
    if constexpr (hi != std::numeric_limits<U>::max()) {
      if (u > hi) return E(lo + clip<E, noclip>(u - hi - 1));
    }
  }
  return static_cast<E>(u);
}

// Cast integer value from underlying type to seq_enum. When `seq_wrap_v`,
// wraps value to ensure safety.
template<typename E, bool noclip = false, enable_if_seq_enum_0<E> = 0>
constexpr E make(std::underlying_type_t<E> u) noexcept {
  if constexpr (seq_actually_wrap_v<E>) {
    return make_safely<E, noclip>(u);
  } else {
    return static_cast<E>(u);
  }
}

// Make interval for full range of seq_enum, for use with ranged-for.
//
// Note: See comments in `interval` about the need to use a larger underlying
// type in some cases, as indicated by the static_assert.
template<typename E, typename U = as_underlying_t<E>,
    enable_if_seq_enum_0<E> = 0>
constexpr auto make_interval() noexcept {
  static_assert(seq_max_num_v<E> != std::numeric_limits<U>::max(),
      "Specify U as something larger than the underlying type");
  return interval<U, E>{seq_min_num_v<E>, U(seq_max_num_v<E> + 1)};
}

} // namespace seq_enum
} // namespace corvid

// TODO: Consider adding support for representing as string, based on a
// compile-time array of names. Missing values show up as the number. Expose
// this as a named function, and `operator<<`, ensuring that it works with
// `strings::append`.
