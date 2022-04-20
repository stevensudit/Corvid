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
#include "Meta.h"
#include "Interval.h"

// If you don't import the `corvid` namespace, you should at least import
// `bitmask_ops`.
namespace corvid {
inline namespace bitmask {

//
// bitmask
//

// bit_count_v
//
// Allow a scoped enum (aka `enum class`) to satisfy the requirements of
// BitmaskType, per https://en.cppreference.com/w/cpp/named_req/BitmaskType,
// while providing some additional functionality.
//
// Prerequisites: Your scoped enum must have 1 or more contiguous bits,
// starting from the lsb, such that the value of any combination of bits in
// that set is valid. Valid values do not need to be named.
//
// (If the entire range of the underlying type is valid, you should probably
// make it unsigned to avoid `max_value` being negative.)
//
// To enable bitmask support for your scoped `enum`, specialize the
// `bit_count_v` constant, setting it to the number of bits (starting from the
// lsb) that are valid.
//
// For example:
//
//    enum class rgb { red = 4, green = 2, blue = 1 };
//
//    template<>
//    constexpr size_t corvid::bitmask::bit_count_v<rgb> = 3;
//
// The only operation that sets invalid bits when given valid inputs is
// `operator~`, but `flip` offers a safe version. While `make` can set invalid
// bits, `make_safely` does not.
//
// However, when `bit_clip_v` is enabled, `operator~` and `make` become
// equivalent to `flip` and `make_safely`, respectively. Note that, while this
// is inexpensive, it does count as a subtle violation of BitmaskType
// requirements.
template<typename E>
constexpr size_t bit_count_v = 0;

// Whether to clip all operations to the valid bits.
template<typename E>
constexpr bool bit_clip_v = false;

} // namespace bitmask
inline namespace bitmask_ops {

// Enable if registered as valid.
template<typename T>
using enable_if_bitmask_0 = enable_if_0<bitmask::bit_count_v<T>>;

//
// Operator overloads.
//

// Dereference operator.
//
// The precedent for this is `std::optional`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr auto operator*(E v) noexcept {
  return as_underlying<E>(v);
}

// Or operators.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E operator|(E l, E r) noexcept {
  return E(*l | *r);
}

template<typename E, typename U = std::underlying_type_t<E>,
    enable_if_bitmask_0<E> = 0>
constexpr const E& operator|=(E& l, E r) noexcept {
  return l = l | r;
}

// And operators.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E operator&(E l, E r) noexcept {
  return E(*l & *r);
}

template<typename E, enable_if_bitmask_0<E> = 0>
constexpr const E& operator&=(E& l, E r) noexcept {
  return l = l & r;
}

// Xor operators.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E operator^(E l, E r) noexcept {
  return E(*l ^ *r);
}

template<typename E, enable_if_bitmask_0<E> = 0>
constexpr const E& operator^=(E& l, E r) noexcept {
  return l = l ^ r;
}

// Complement operator.
//
// Unless `bit_clip_v`, this may set invalid bits, whereas `flip` will not.
// When `bit_clip_v`, does the same thing as `flip`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E operator~(E v) noexcept {
  if constexpr (bit_clip_v<E>)
    return v ^ max_value<E>();
  else
    return E(~*v);
}

// Plus operators.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E operator+(E l, E r) noexcept {
  return l | r;
}

template<typename E, typename U = std::underlying_type_t<E>,
    enable_if_bitmask_0<E> = 0>
constexpr const E& operator+=(E& l, E r) noexcept {
  return l = l + r;
}

// Minus operators.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E operator-(E l, E r) noexcept {
  return l & ~r;
}

template<typename E, typename U = std::underlying_type_t<E>,
    enable_if_bitmask_0<E> = 0>
constexpr const E& operator-=(E& l, E r) noexcept {
  return l = l - r;
}

} // namespace bitmask_ops
inline namespace bitmask {

//
// Named functions.
//

// Cast `enum` to specified integral type.
//
// Like `std::to_integer<IntegerType>(std::byte)`.
template<typename T, typename E,
    enable_if_0<bitmask::bit_count_v<E> && std::is_integral_v<T>> = 0>
constexpr T to_integer(E v) noexcept {
  return static_cast<T>(v);
}

// Maximum value, which is also a mask of valid bits.
//
// Note: If underlying type is signed and `bit_count_v` includes the high bit,
// this value will be negative. It's still correct, even then, but maybe you
// should use an unsigned underlying type.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E max_value() noexcept {
  return static_cast<E>(
      (std::underlying_type_t<E>(1) << (bit_count_v<E>)) - 1);
}

// Minimum value, which is always 0 for bitmasks.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E min_value() noexcept {
  return static_cast<E>(0);
}

// Cast integer value to bitmask, keeping only the valid bits.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E make_safely(std::underlying_type_t<E> u) noexcept {
  return static_cast<E>(u) & max_value<E>();
}

// Cast integer value to bitmask. When `bit_clip_v`, clips value to ensure
// safety.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E make(std::underlying_type_t<E> u) noexcept {
  if constexpr (bit_clip_v<E>)
    return make_safely<E>(u);
  else
    return static_cast<E>(u);
}

// Return `v` with the bits from `m` set.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E set(E v, E m) noexcept {
  return v + m;
}

// Return `v` with the bits set in `m` cleared.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E clear(E v, E m) noexcept {
  return v - m;
}

// Return `v` with the bits set in `m` set to `value`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E set_to(E v, E m, bool value) noexcept {
  return value ? v + m : v - m;
}

// Return `v` with only the valid bits flipped.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E flip(E v) noexcept {
  return v ^ max_value<E>();
}

// Return whether `v` has any of the bits in `m` set.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr bool has(E v, E m) noexcept {
  return (v & m) != E(0);
}

// Return whether `v` has all the bits in `m` set.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr bool has_all(E v, E m) noexcept {
  return (v & m) == m;
}

// Returns whether `v` has is missing some of the bits set in `m`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr bool missing(E v, E m) noexcept {
  return !has_all(v, m);
}

// Return whether `v` has is missing all of the bits set in `m`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr bool missing_all(E v, E m) noexcept {
  return !has(v, m);
}

// Make interval for full range of bitmask, for use with ranged-for.
//
// Note: See comments in `interval` about the need to use a larger underlying
// type in some cases, as indicated by the static_assert.
template<typename E, typename U = as_underlying_t<E>,
    enable_if_bitmask_0<E> = 0>
constexpr auto make_interval() noexcept {
  static_assert(*max_value<E>() != std::numeric_limits<U>::max(),
      "Specify U as something larger than the underlying type");
  return interval<U, E>{0, U(*max_value<E>()) + 1};
}

} // namespace bitmask
} // namespace corvid

// TODO: Consider adding support for streaming, based on an array of strings
// that define each bit. An extended version would instead support the full
// list by taking an association while preferring multibit values (or,
// alternately, keep a second list of multibit values, which must be sorted).
// Essentially, what C# does.

// TODO: Wacky idea:
// `rgb_yellow == some(rgb::red, rgb::green)`
//
// `rgb_yellow == all(rgb::red, rgb::green)`
//
// The function returns a local type that is initialized on the & of the
// parameters and offers an appropriate op== and !=. So != some means has none
// and != all means it doesn't have all, but might have some. This isn't
// terrible. Make sure it doesn't interfere with direct == and !=.
