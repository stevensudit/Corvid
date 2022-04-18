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
#include <type_traits>
#include <string>
#include <tuple>
#include <array>
#include <utility>

// Recommendation: Import the `covid` namespace. If you don't, then at least
// import `bitmask_ops`.
namespace corvid {

// Cast bitmask to underlying integer value.
//
// Stand-in for `std::to_underlying` in C++23.
//
// See also: operator*.
#ifndef __cpp_lib_to_underlying
template<typename E>
constexpr auto to_underlying(E v) noexcept {
  return static_cast<std::underlying_type_t<E>>(v);
}
#else
using std::to_underlying;
#endif

inline namespace bitmask {

//
// bitmask
//

// bit_count_v
//
// Allow a scoped `enum` (aka `enum class`) to satisfy the requirements of
// BitmaskType, per https://en.cppreference.com/w/cpp/named_req/BitmaskType,
// while providing some additional functionality.
//
// To enable bitmask support for your scoped `enum`, specialize the
// `bit_count_v` constant, setting it to the number of bits (starting from the
// lsb) that are valid.
//
// For example:
//
//    enum class rgb { /* ... */ };
//
//    template<>
//    constexpr size_t corvid::bitmask::bit_count_v<rgb> = 3;
//
// The only operation that sets invalid bits when given valid inputs is
// `operator~`, but `flip` offers a safe version. Unfortunately, we can't make
// `operator~` safe by default without violating the BitmaskType requirements.
// The other function that enforces the bit count is make_safely, which keeps
// only the valid bits.
//
// Note: All functions are enable_if'd on this, which is why they can't pollute
// the namespace.
template<typename E>
constexpr size_t bit_count_v = 0;

} // namespace bitmask
inline namespace bitmask_ops {

template<typename T>
using enable_if_bitmask_t = std::enable_if_t<bitmask::bit_count_v<T>, bool>;

template<typename T, typename U>
using enable_if_bitmask_int_t =
    std::enable_if_t<bitmask::bit_count_v<T> && std::is_integral_v<U>, bool>;

//
// Operator overloads.
//

// Dereference operator.
//
// The precedent for this is `std::optional`.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr auto operator*(E v) noexcept {
  return to_underlying<E>(v);
}

// Or operators.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E operator|(E l, E r) noexcept {
  return E(*l | *r);
}

template<typename E, typename U = std::underlying_type_t<E>,
    enable_if_bitmask_t<E> = true>
constexpr const E& operator|=(E& l, E r) noexcept {
  return l = l | r;
}

// And operators.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E operator&(E l, E r) noexcept {
  return E(*l & *r);
}

template<typename E, enable_if_bitmask_t<E> = true>
constexpr const E& operator&=(E& l, E r) noexcept {
  return l = l & r;
}

// Xor operators.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E operator^(E l, E r) noexcept {
  return E(*l ^ *r);
}

template<typename E, enable_if_bitmask_t<E> = true>
constexpr const E& operator^=(E& l, E r) noexcept {
  return l = l ^ r;
}

// Complement operator.
//
// Note that this may set invalid bits; see flip for a safe alternative.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E operator~(E v) noexcept {
  return E(~*v);
}

// Plus operators.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E operator+(E l, E r) noexcept {
  return l | r;
}

template<typename E, typename U = std::underlying_type_t<E>,
    enable_if_bitmask_t<E> = true>
constexpr const E& operator+=(E& l, E r) noexcept {
  return l = l + r;
}

// Minus operators.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E operator-(E l, E r) noexcept {
  return l & ~r;
}

template<typename E, typename U = std::underlying_type_t<E>,
    enable_if_bitmask_t<E> = true>
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
// Like `std::to_integer<std::byte>`.
template<typename T, typename E, enable_if_bitmask_int_t<E, T> = true>
constexpr T to_integer(E v) noexcept {
  return static_cast<T>(v);
}

// Cast integer value to bitmask.
template<typename E, typename U, enable_if_bitmask_int_t<E, U> = true>
constexpr E make(U v) noexcept {
  return static_cast<E>(v);
}

// Maximum value, which is also a mask of valid bits.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E max_value() noexcept {
  return make<E>((std::underlying_type_t<E>(1) << (bit_count_v<E>)) - 1);
}

// Cast integer value to bitmask, keeping only the valid bits.
template<typename E, typename U = std::underlying_type_t<E>,
    enable_if_bitmask_t<E> = true>
constexpr E make_safely(U v) noexcept {
  return make<E>(v) & max_value<E>();
}

// Return `v` with the bits from `m` set.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E set(E v, E m) noexcept {
  return v + m;
}

// Return `v` with the bits set in `m` cleared.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E clear(E v, E m) noexcept {
  return v - m;
}

// Return `v` with the bits set in `m` set to `value`.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E set_to(E v, E m, bool value) noexcept {
  return value ? v + m : v - m;
}

// Return `v` with only the valid bits flipped.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr E flip(E v) noexcept {
  return v ^ max_value<E>();
}

// Return whether `v` has any of the bits in `m` set.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr bool has(E v, E m) noexcept {
  return (v & m) != E(0);
}

// Returns whether `v` has all the bits in `m` set.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr bool has_all(E v, E m) noexcept {
  return (v & m) == m;
}

// Returns whether `v` has is missing some of the bits set in `m`.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr bool missing(E v, E m) noexcept {
  return !has_all(v, m);
}

// Returns whether `v` has is missing all of the bits set in `m`.
template<typename E, enable_if_bitmask_t<E> = true>
constexpr bool missing_all(E v, E m) noexcept {
  return !has(v, m);
}

} // namespace bitmask
} // namespace corvid

// TODO: Consider adding support for streaming, based on an array of strings
// that define each bit. An extended version would instead support the full
// list by taking an association while preferring multibit values. Essentially,
// what C# does.

// TODO: Write a similar system for scoped enums that are not bitmaps but are
// instead ranges, enabling increment/decrement operators. Perhaps register the
// lowest and highest values, then allow safe conversion (by clipping), as well
// as perhaps exposing this as begin/end. Perhaps offer modulo (safe) math.
// Helpers might allow creation of corresponding bitmaps for checking matches
// against.

// TODO: Consider offering implicit registration by having a bitmask_bit_count
// value. This would be even more useful for sequential_max_value.
