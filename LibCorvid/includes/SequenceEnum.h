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

// Importing the `corvid::sequence` namespace is optional, but you need to
// import `corvid::sequence::ops` to get the operator overloads to work.
namespace corvid::sequence {

//
// sequence enum
//

// seq_max_v
//
// Allow a scoped enum (aka `enum class`) to be used as a sequence of
// contiguous values, supporting add and subtract, while providing some
// additional functionality. Conceptually, sequential values are mutually
// exclusive options.
//
// Prerequisites: Your scoped enum must have a minimum and maximum valid value,
// and all values between these, inclusive, must be valid. This range doesn't
// have to start at 0, and if the underlying type is signed, it can be
// negative. Valid values do not need to be named.
//
// To enable sequence support for your scoped enum, specialize the `seq_max_v`
// constant, setting it to the highest enum value that is valid. If the lowest
// value isn't 0, then also specialize `seq_min_v` to that.
//
// (In the degenerate case of having only one valid value, you'll need to
// override `seq_valid_v`, setting it to `true`.)
//
// To enable wrapping, which ensures that operations keep values within valid
// range (at the cost of runtime range checks), specialize `seq_wrap_v` to
// `true`.
//
// For example:
//
//    enum class tiger_pick { eeny, meany, miny, moe };
//
//    template<>
//    constexpr auto corvid::seq_max_v<tiger_pick> = tiger_pick::moe;
//
// Note that the max value is inclusive.
template<typename E>
constexpr auto seq_max_v = from_underlying<E>(0);

// Minimum value. Specialize this if range does not start at 0. Must be less
// than `seq_max_v`.
template<typename E>
constexpr auto seq_min_v = from_underlying<E>(0);

// Whether to wrap all calculations to keep them in range. Specialize this to
// enable runtime range checks that wrap the value.
template<typename E>
constexpr bool seq_wrap_v = false;

// Whether sequence is enabled. Specialize if not automatically detected.
template<typename E>
constexpr bool
    seq_valid_v = as_underlying(seq_max_v<E>) - as_underlying(seq_min_v<E>);

// Enable if registered as valid.
template<typename E>
using enable_if_sequence_0 = enable_if_0<seq_valid_v<E>>;

namespace details {

//
// Inferred
//

// Maximum numerical value.
template<typename E>
constexpr auto seq_max_num_v = as_underlying(seq_max_v<E>);

// Minimum numerical value.
template<typename E>
constexpr auto seq_min_num_v = as_underlying(seq_min_v<E>);

// Number of distinct values in range.
template<typename E>
constexpr auto seq_size_v = seq_max_num_v<E> - seq_min_num_v<E> + 1;

// Whether wrapping is really enabled. We don't need to wrap when the range
// exactly fits the underlying type, because of modulo math.
template<typename E>
constexpr bool seq_actually_wrap_v = (seq_size_v<E> != 0) && seq_wrap_v<E>;

// Clip, unless `noclip` set, by modding to size.
template<typename E, bool noclip = false, enable_if_sequence_0<E> = 0>
constexpr auto clip(std::underlying_type_t<E> u) {
  if constexpr (!noclip && seq_size_v<E> != 0)
    return u % seq_size_v<E>;
  else
    return u;
}

// Clip if wrapping enabled.
template<typename E, enable_if_sequence_0<E> = 0>
constexpr auto clip_if_wrap(std::underlying_type_t<E> u) {
  return clip<E, !seq_actually_wrap_v<E>>(u);
}

} // namespace details

//
// Makers
//

// Cast integer value from underlying type to sequence, wrapping to keep it in
// range.
template<typename E, bool noclip = false, enable_if_sequence_0<E> = 0>
constexpr E make_safely(std::underlying_type_t<E> u) noexcept {
  // Wrapping is only meaningful if the underlying type is not a perfect fit.
  if constexpr (details::seq_size_v<E> != 0) {
    constexpr auto lo = details::seq_min_num_v<E>,
                   hi = details::seq_max_num_v<E>;
    static_assert(lo <= hi);
    using U = std::underlying_type_t<E>;

    // Underflow is only possible if it starts above the underlying min.
    if constexpr (lo != std::numeric_limits<U>::min()) {
      if (u < lo) return E(hi + details::clip<E, noclip>(u - lo + 1));
    }

    // Overflow is only possible if it ends below the underlying max.
    if constexpr (hi != std::numeric_limits<U>::max()) {
      if (u > hi) return E(lo + details::clip<E, noclip>(u - hi - 1));
    }
  }
  return static_cast<E>(u);
}

// Cast integer value from underlying type to sequence. When `seq_wrap_v` set,
// wraps value to ensure safety.
template<typename E, bool noclip = false, enable_if_sequence_0<E> = 0>
constexpr E make(std::underlying_type_t<E> u) noexcept {
  if constexpr (details::seq_actually_wrap_v<E>) {
    return make_safely<E, noclip>(u);
  } else {
    return static_cast<E>(u);
  }
}

inline namespace ops {

// Dereference operator.
//
// The precedent for this is `std::optional`.
template<typename E, enable_if_sequence_0<E> = 0>
constexpr auto operator*(E v) noexcept {
  return as_underlying<E>(v);
}

// Math
//
// Only heterogenous, binary addition and subtraction operations are supported.
//
// When `seq_wrap_v` is set, all results are modulo the sequence size.
// Otherwise, they are undefined when they exceed the range.

//
// Addition operators
//

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E operator+(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, true>(*l + details::clip_if_wrap<E>(r));
}

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E operator+(std::underlying_type_t<E> l, E r) noexcept {
  return r + l;
}

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E& operator+=(E& l, std::underlying_type_t<E> r) noexcept {
  return l = l + r;
}

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E& operator++(E& l) noexcept {
  if constexpr (details::seq_actually_wrap_v<E>)
    if (l == seq_max_v<E>) return l = seq_min_v<E>;

  return l = E{*l + 1};
}

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E operator++(E& l, int) noexcept {
  auto o = l;
  ++l;
  return o;
}

//
// Subtraction operators
//

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E operator-(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, true>(*l - details::clip_if_wrap<E>(r));
}

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E& operator-=(E& l, std::underlying_type_t<E> r) noexcept {
  return l = l - r;
}

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E& operator--(E& l) noexcept {
  if constexpr (details::seq_actually_wrap_v<E>)
    if (*l == details::seq_min_num_v<E>) return l = seq_max_v<E>;

  return l = E{*l - 1};
}

template<typename E, enable_if_sequence_0<E> = 0>
constexpr E operator--(E& l, int) noexcept {
  auto o = l;
  --l;
  return o;
}

// Streaming.
template<typename E, enable_if_sequence_0<E> = 0>
std::ostream& operator<<(std::ostream& os, E v) {
  return strings::append_enum(os, v);
}

} // namespace ops

//
// Named functions
//

// Traits

// Maximum value.
template<typename E, enable_if_sequence_0<E> = 0>
constexpr E max_value() noexcept {
  return seq_max_v<E>;
}

// Minimum value.
template<typename E, enable_if_sequence_0<E> = 0>
constexpr E min_value() noexcept {
  return seq_min_v<E>;
}

// Length of range.
template<typename E, enable_if_sequence_0<E> = 0>
constexpr auto range_length() noexcept {
  return to_integer<size_t>(seq_size_v<E>());
}

// Cast sequence to specified integral type.
//
// Like `std::to_integer<IntegerType>(std::byte)`.
template<typename T, typename E,
    enable_if_0<seq_valid_v<E> && std::is_integral_v<T>> = 0>
constexpr T to_integer(E v) noexcept {
  return static_cast<T>(v);
}

//
// Interval
//

// Note: See "Interval.h" for relevant `make_interval` function.

namespace details {

// sequence_printer
template<typename E, std::size_t N>
struct sequence_printer {
public:
  constexpr sequence_printer(std::array<std::string_view, N> name_list)
      : names(name_list) {}

  template<typename A>
  A& append(A& target, E v) const {
    auto n = as_underlying(v);
    auto ofs = n - *min_value<E>();

    // Print looked-up name or the numerical value.
    if (ofs < N && names[ofs].size())
      strings::make_appender(target).append(names[ofs]);
    else
      strings::append_num(target, n);
    return target;
  }

  const std::array<std::string_view, N> names;
};

} // namespace details

// Make enum printer for E by taking a list of names for values.
//
// For example:
//
//    template<>
//    constexpr auto strings::enum_printer_v<tiger_pick> =
//        make_enum_printer<tiger_pick>({"eeny", "meany", "miny", "moe"});
//
// The first name matches the `min_value()`. Must not have more names than
// `range_length()`. Anything without a name, either because we have more
// values than names or because the name is empty, gets outputed as a number.
template<typename E, std::size_t N, enable_if_sequence_0<E> = 0>
constexpr auto make_enum_printer(std::string_view(&&l)[N]) {
  static_assert(N <= details::seq_size_v<E>, "Too many names");
  return details::sequence_printer<E, N>(std::to_array<std::string_view>(l));
}

} // namespace corvid::sequence

//
// TODO
//

// TODO: It might be nice if we could specialize `std::numeric_limits` for all
// enums that are flagged as sequence, inheriting from the underlying class and
// providing correct min and max values. Not sure if that's actually possible,
// though. Maybe we can't do it automatically but can make it easier. Maybe
// inheriting with specialization might work.

// TODO: On a related note, maybe we can provide `min_value_v`, `max_value_v`
// and `range_length_v` in "Meta.h" for regular enums. This would necessarily
// ignore any registered bitmask or sequence limits. This would need to
// disambiguate itself from bitmasks and sequences, though.
