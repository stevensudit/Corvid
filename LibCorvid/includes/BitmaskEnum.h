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
#include "StringUtils.h"

// Importing the `corvid::bitmask` namespace is optional, but you need to
// import `corvid::bitmask::ops` to get the operator overloads to work.
namespace corvid::bitmask {

//
// bitmask enum
//

// bit_count_v
//
// Allow a scoped enum (aka `enum class`) to satisfy the requirements of
// BitmaskType, per https://en.cppreference.com/w/cpp/named_req/BitmaskType,
// while providing some additional functionality. Conceptually, bitmask values
// are flags that can be combined.
//
// Prerequisites: Your scoped enum must have 1 or more contiguous bits,
// starting from the lsb, such that the value of any combination of bits in
// that set is valid. Valid values do not need to be named.
//
// It is generally a good idea to define the enum in terms of an unsigned type,
// since this is a collection of bits and not a numerical value. Failing to do
// so leads to strange side-effects, such as `max_value` being negative (when
// all bits are valid).
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
//    constexpr size_t bitmask::bit_count_v<rgb> = 3;
//
// The only operation that sets invalid bits when given valid inputs is
// `operator~`, but `flip` offers a safe alternative. While `make` can set
// invalid bits given an invalid input, `make_safely` does not.
//
// However, when `bit_clip_v` is enabled, then `operator~` and `make` become
// equivalent to `flip` and `make_safely`, respectively. (This also affects the
// functions that rely on these.)
//
// While this feature is relatively inexpensive, it does count as a subtle
// violation of BitmaskType requirements.
template<typename E>
constexpr size_t bit_count_v = 0;

// Whether to clip operations to the valid bits.
template<typename E>
constexpr bool bit_clip_v = false;

// Enable if registered as valid.
template<typename E>
using enable_if_bitmask_0 = enable_if_0<bitmask::bit_count_v<E>>;

inline namespace ops {

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

template<typename E, typename V = std::underlying_type_t<E>,
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
// Unless `bit_clip_v` is set, this may set invalid bits, whereas `flip` will
// not. When `bit_clip_v` is set, does the same thing as `flip`.
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

template<typename E, typename V = std::underlying_type_t<E>,
    enable_if_bitmask_0<E> = 0>
constexpr const E& operator+=(E& l, E r) noexcept {
  return l = l + r;
}

// Minus operators.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E operator-(E l, E r) noexcept {
  return l & ~r;
}

template<typename E, typename V = std::underlying_type_t<E>,
    enable_if_bitmask_0<E> = 0>
constexpr const E& operator-=(E& l, E r) noexcept {
  return l = l - r;
}

// Streaming.
template<typename E, enable_if_bitmask_0<E> = 0>
std::ostream& operator<<(std::ostream& os, E v) {
  return strings::append_enum(os, v);
}

} // namespace ops

//
// Named functions
//

// Traits

// Maximum value, which is also a mask of valid bits.
//
// Note: If underlying type is signed and `bit_count_v` includes the high bit,
// this value will be negative. It's technically correct, even then, but maybe
// you should use an unsigned underlying type.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E max_value() noexcept {
  return static_cast<E>(
      (std::underlying_type_t<E>(1) << (bit_count_v<E>)) - 1);
}

// Minimum value, which is always 0.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E min_value() noexcept {
  return static_cast<E>(0);
}

// Length of bits.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr size_t bits_length() noexcept {
  return bit_count_v<E>;
}

// Length of range.
//
// This is the number of distinct values that are valid.
//
// Note: If `max_value_v` is the same as the maximum value of `size_t`, returns
// 0, which is confusing but technically correct, which is the best kind of
// correct.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr auto range_length() noexcept {
  return to_integer<size_t>(max_value<E>()) + 1;
}

// Cast bitmask to specified integral type.
//
// Like `std::to_integer<IntegerType>(std::byte)`.
template<typename T, typename E,
    enable_if_0<bitmask::bit_count_v<E> && std::is_integral_v<T>> = 0>
constexpr T to_integer(E v) noexcept {
  return static_cast<T>(v);
}

// Makers

// Cast integer value to bitmask, keeping only the valid bits.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E make_safely(std::underlying_type_t<E> u) noexcept {
  return static_cast<E>(u) & max_value<E>();
}

// Cast integer value to bitmask. When `bit_clip_v` set, clips value to ensure
// safety.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E make(std::underlying_type_t<E> u) noexcept {
  if constexpr (bit_clip_v<E>)
    return make_safely<E>(u);
  else
    return static_cast<E>(u);
}

// Return value with bit at `ndx` (counting from the lsb) set.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E make_at(size_t ndx) noexcept {
  return make<E>(1 << (ndx - 1));
}

// Set

// Return `v` with the bits in `m` set.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E set(E v, E m) noexcept {
  return v + m;
}

// Return `v` with the bits in `m` set only if `pred`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E set_if(E v, E m, bool pred) noexcept {
  return pred ? v + m : v;
}

// Return `v` with the bits set in `m` cleared.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E clear(E v, E m) noexcept {
  return v - m;
}

// Return `v` with the bits in `m` cleared only if `pred`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E clear_if(E v, E m, bool pred) noexcept {
  return pred ? v - m : v;
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

// Set at index

// Return `v` with the bit at `ndx` set.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E set_at(E v, size_t ndx) noexcept {
  return v + make_at<E>(ndx);
}

// Return `v` with the bit at `ndx` set only if `pred`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E set_at_if(E v, size_t ndx, bool pred) noexcept {
  return pred ? v + make_at<E>(ndx) : v;
}

// Return `v` with the bit at `ndx` clear.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E clear_at(E v, size_t ndx) noexcept {
  return v - make_at<E>(ndx);
}

// Return `v` with the bit at `ndx` clear only if `pred`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E clear_at_if(E v, size_t ndx, bool pred) noexcept {
  return pred ? v - make_at<E>(ndx) : v;
}

// Return `v` with the bit at `ndx` set to `value`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr E set_at_to(E v, size_t ndx, bool value) noexcept {
  return value ? set_at(v, ndx) : clear_at(v, ndx);
}

// Has

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

// Returns whether `v` is missing some of the bits set in `m`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr bool missing(E v, E m) noexcept {
  return !has_all(v, m);
}

// Return whether `v` is missing all of the bits set in `m`.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr bool missing_all(E v, E m) noexcept {
  return !has(v, m);
}

//
// Interval
//

// Note: See "Interval.h" for relevant `make_interval` function.

namespace details {

// bitmask_printer
template<typename E, std::size_t N>
struct bitmask_printer {
  constexpr bitmask_printer(std::array<std::string_view, N> name_list)
      : names(name_list) {}

  // Append when we have one name per bit.
  template<typename A>
  A& append_bits(A& target, E v) const {
    static constexpr strings::delim plus(" + ");
    bool skip{true};

    for (size_t ndx = N; ndx != 0; --ndx) {
      auto mask = make_at<E>(ndx);
      auto ofs = N - ndx;

      // If bit matched, print and remove.
      if (has(v, mask) && names[ofs].size()) {
        plus.append_skip_once(target, skip);
        strings::make_appender(target).append(names[ofs]);
        v = E(*v & ~*mask);
      }
    }
    // Print residual in hex.
    if (*v || skip)
      strings::append_num<16>(plus.append_skip_once(target, skip), *v);
    return target;
  }

  // Append when we have one name for each value in range.
  template<typename A>
  A& append_range(A& target, E v) const {
    static constexpr strings::delim plus(" + ");
    bool skip{true};
    int all_valid_bits = N - 1;

    for (int ndx = all_valid_bits; ndx >= 0; --ndx) {
      auto mask = E(ndx);

      // If bits matched, print and remove.
      if (has_all(v, mask) && names[ndx].size()) {
        plus.append_skip_once(target, skip);
        strings::make_appender(target).append(names[ndx]);
        v = E(*v & ~*mask);

        // If no valid bits left, drop to number.
        if ((*v & all_valid_bits) == 0) break;
      }
    }
    // Print residual in hex.
    if (*v || skip)
      strings::append_num<16>(plus.append_skip_once(target, skip), *v);
    return target;
  }

  template<typename A>
  A& append(A& target, E v) const {
    if constexpr (N == bit_count_v<E>)
      return append_bits(target, v);
    else if constexpr (N == range_length<E>())
      return append_range(target, v);
    else
      return strings::append_num<16>(target, *v);
  }

  const std::array<std::string_view, N> names;
};

} // namespace details

// Enum printer

// Make enum printer for E by taking a list of names for values.
//
// For example:
//
//    template<>
//    constexpr auto strings::enum_printer_v<rgb> =
//        bitmask::make_enum_printer<rgb>({"red", "green", "blue"});
//
// There are two mutually-exclusive ways to specify the names.
//
// You can, as shown in the above example, list just the names of the valid
// bits, from highest to lowest (lsb). You must specify exactly `bits_length`
// values, but you can leave any of them blank, if you wish. The value of an
// enum will be shown as the sum of bits, with any residual in hex.
//
// You can also list all of the possible values, from lowest to highest. You
// must specify exactly `range_length` values, but you can leave any of them
// blank, if you wish. The value of an enum will be shown as the greedy minimal
// list summing the named values. As before, any residual is shown in hex.
//
// The final alternative is to not specify any names at all, in which case
// you always get the hex.
template<typename E, std::size_t N, enable_if_bitmask_0<E> = 0>
constexpr auto make_enum_printer(std::string_view(&&l)[N]) {
  static_assert(N == bits_length<E>() || N == range_length<E>(),
      "Must be bits_length or range_length.");
  return details::bitmask_printer<E, N>(std::to_array<std::string_view>(l));
}

// No-names overload.
template<typename E, enable_if_bitmask_0<E> = 0>
constexpr auto make_enum_printer() {
  return details::bitmask_printer<E, 0>(std::array<std::string_view, 0>());
}

} // namespace corvid::bitmask

//
// TODO
//

// TODO: Consider whether we want to support bitmasks that are valid from the
// msb down, not the lsb up, perhaps encoding the bit count as a negative
// number.

// TODO: Consider extending enum printer with a mode that takes an association
// of values to names, so that it can be sparse.

// TODO: Wacky idea:
// `rgb_yellow == some(rgb::red, rgb::green)`
//
// `rgb_yellow == all(rgb::red, rgb::green)`
//
// The function returns a local type that is initialized on the & of the
// parameters and offers an appropriate op== and !=. So != some means has none
// and != all means it doesn't have all, but might have some. This isn't
// terrible. Make sure it doesn't interfere with direct == and !=.

// TODO: Consider providing `operator[]` that returns bool for a given index.
// Essentially, the op version of `get_at`. At that point, we could also
// provide a proxy object to invoke `set_at`.

// TODO: Consider allowing a value specialization for `make_enum_printer` that
// defines the direction of the inputs and outputs. For bits, reversing the
// input would mean treating the first name as the lsb instead of the lsb. For
// bits, reversing the output would mean displaying the lsb before the msb. For
// ranges, reversing the input would mean treating first name as the highest
// value instead of the lowest. For ranges, reversing the output would mean
// showing the lower values before the higher ones (which would require
// buffering and prepending).
