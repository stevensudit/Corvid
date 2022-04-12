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

// Recommendation: Import the `corvid` namespace, but not `corvid::bitmask`,
// allowing you to make calls like `bitmask::flip`. If you don't want to import
// all of `corvid`, then import `corvid::bitmask_ops`, instead.
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

// Do not import this.
namespace bitmask {

// bitmask
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
//    using namespace corvid::bitmask_ops;
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
template<typename E>
constexpr size_t bit_count_v = 0;

} // namespace bitmask
inline namespace bitmask_ops {

//
// Operator overloads.
//

// Dereference operator.
//
// This is quirky but convenient.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr auto operator*(E v) noexcept {
  return to_underlying<E>(v);
}

// Or operators.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr E operator|(E l, E r) noexcept {
  return E(*l | *r);
}

template<typename E, typename U = std::underlying_type_t<E>,
    std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr const E& operator|=(E& l, E r) noexcept {
  return l = l | r;
}

// And operators.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr E operator&(E l, E r) noexcept {
  return E(*l & *r);
}

template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr const E& operator&=(E& l, E r) noexcept {
  return l = l & r;
}

// Xor operators.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr E operator^(E l, E r) noexcept {
  return E(*l ^ *r);
}

template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr const E& operator^=(E& l, E r) noexcept {
  return l = l ^ r;
}

// Complement operator.
//
// Note that this may set invalid bits; see flip for a safe alternative.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr E operator~(E v) noexcept {
  return E(~*v);
}

} // namespace bitmask_ops
namespace bitmask {

//
// Named functions.
//

// Cast `enum` to specified integral type.
//
// Like `std::to_integer<std::byte>`.
template<typename T, typename E,
    std::enable_if_t<bitmask::bit_count_v<E> && std::is_integral_v<T>, bool> =
        true>
constexpr T to_integer(E v) noexcept {
  return static_cast<T>(v);
}

// Cast integer value to bitmask.
template<typename E, typename U>
constexpr E make(U v) noexcept {
  static_assert(bit_count_v<E> != 0);
  return static_cast<E>(v);
}

// Maximum value, which is also a mask of valid bits.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr E max_value() noexcept {
  return make<E>((std::underlying_type_t<E>(1) << (bit_count_v<E>)) - 1);
}

// Cast integer value to bitmask, keeping only the valid bits.
template<typename E, typename U = std::underlying_type_t<E>>
constexpr E make_safely(U v) noexcept {
  return make<E>(v) & max_value<E>();
}

// Return `v` with the bits from `m` set. Union.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr E set(E v, E m) noexcept {
  return v | m;
}

// Return `v` with the bits set in `m` cleared. Difference.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr E clear(E v, E m) noexcept {
  return v & ~m;
}

// Return `v` with only the valid bits flipped. Complement.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr E flip(E v) noexcept {
  return v ^ max_value<E>();
}

// Return whether `v` has any of the bits in `m` set. Has intersection.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr bool overlaps(E v, E m) noexcept {
  return (v & m) != E(0);
}

// Returns whether `v` has all the bits in `m` set. Is superset.
template<typename E, std::enable_if_t<bitmask::bit_count_v<E>, bool> = true>
constexpr bool contains(E v, E m) noexcept {
  return (v & m) == m;
}

} // namespace bitmask
} // namespace corvid

// TODO: Consider adding support for streaming, based on an array of strings
// that define each bit. An extended version would instead support the full
// list by taking an association while preferring multibit values. Essentially,
// what C# does.

// TODO: Consider exposing named functions as quirky operator overloads. Set is
// +, clear is -, flip is !, but not sure about overlaps and contains (maybe
// reverse their parameters and use / and %). To be frank, not sure about any
// of these; they may be too quirky.

// TODO: Write a similar system for scoped enums that are not bitmaps but are
// instead ranges, enabling increment/decrement operators. Perhaps register the
// lowest and highest values, then allow safe conversion (by clipping), as well
// as perhaps exposing this as begin/end. Perhaps offer modulo (safe) math.
// Helpers might allow creation of corresponding bitmaps for checking matches
// against.

// TODO: Consider offering implicit registration by having a bitmask_bit_count
// value. This would be even more useful for sequential_max_value.
