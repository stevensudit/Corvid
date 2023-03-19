// Corvid20: A general-purpose C++ 20 library extending std.
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
#include "enums_shared.h"

namespace corvid::enums {
namespace sequence {

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
// and all values between these, inclusive, must be valid. In other words, the
// range is [min,max].
//
// This range doesn't have to start at 0, and if the underlying type is signed,
// it can even be negative. Valid values do not need to be named. So, for
// example, `std::byte` has a range of [0,255], but no named values.
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
//    constexpr auto enums::sequence::seq_max_v<tiger_pick> = tiger_pick::moe;
//
// Note again that this max value is inclusive.
template<typename E>
constexpr auto seq_max_v = from_underlying<E>(0);

// TODO: It would be better if we could choose between specifying a seq_max_v
// or just specifying the complete list of values. Perhaps the way to do this
// would be to check for the enum printer publishing N as seq_max_v, defaulting
// to 0 if not found. (Or, alternately, publishing 0 if not a sequence.) We
// still want to allow explicit overriding of seq_max_v, though, for enums that
// aren't supposed to have named values, akin to `std::byte`.`.

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
constexpr bool seq_valid_v =
    as_underlying(seq_max_v<E>) - as_underlying(seq_min_v<E>);

// Concept for sequential enum.
template<typename E>
concept SequentialEnum = seq_valid_v<E>;

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
// Wraps to 0 if it's the full range of the underlying type.
template<typename E>
constexpr auto seq_size_v = seq_max_num_v<E> - seq_min_num_v<E> + 1;

// Whether wrapping is really enabled. We don't need to wrap when the range
// exactly fits the underlying type, because of modulo math.
template<typename E>
constexpr bool seq_actually_wrap_v = (seq_size_v<E> != 0) && seq_wrap_v<E>;

// Clip, unless `noclip` set, by modding to size.
template<SequentialEnum E, bool noclip = false>
[[nodiscard]] constexpr auto clip(std::underlying_type_t<E> u) {
  if constexpr (!noclip && seq_size_v<E> != 0)
    return u % seq_size_v<E>;
  else
    return u;
}

// Clip if wrapping enabled.
template<SequentialEnum E>
[[nodiscard]] constexpr auto clip_if_wrap(std::underlying_type_t<E> u) {
  return clip<E, !seq_actually_wrap_v<E>>(u);
}

} // namespace details

//
// Makers
//

// Cast integer value from underlying type to sequence, wrapping to keep it in
// range.
template<SequentialEnum E, bool noclip = false>
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
template<SequentialEnum E, bool noclip = false>
constexpr E make(std::underlying_type_t<E> u) noexcept {
  if constexpr (details::seq_actually_wrap_v<E>)
    return make_safely<E, noclip>(u);
  else
    return static_cast<E>(u);
}

inline namespace ops {

// Dereference operator.
//
// The precedent for this is `std::optional`.
template<SequentialEnum E>
[[nodiscard]] constexpr auto operator*(E v) noexcept {
  return as_underlying<E>(v);
}

// Math
//
// Only heterogenous addition and subtraction operations are supported.
//
// When `seq_wrap_v` is set, all results are modulo the sequence size.
// Otherwise, they are undefined when they exceed the range.
//
// TODO: Consider allowing opt in to homogenous operations, which makess sense
// for `std::byte` and other strongly-typed integers. This would require an
// additional concept, maybe `arithmetic_enum`. This would also support the
// full set of ops. Unclear whether this is best done as an extension of
// SequentialEnum or a separate concept.

//
// Addition operators
//

template<SequentialEnum E>
[[nodiscard]] constexpr E
operator+(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, true>(*l + details::clip_if_wrap<E>(r));
}

template<SequentialEnum E>
[[nodiscard]] constexpr E
operator+(std::underlying_type_t<E> l, E r) noexcept {
  return r + l;
}

template<SequentialEnum E>
constexpr E& operator+=(E& l, std::underlying_type_t<E> r) noexcept {
  return l = l + r;
}

template<SequentialEnum E>
constexpr E& operator++(E& l) noexcept {
  if constexpr (details::seq_actually_wrap_v<E>)
    if (l == seq_max_v<E>) return l = seq_min_v<E>;

  return l = E(l + 1);
}

template<SequentialEnum E>
[[nodiscard]] constexpr E operator++(E& l, int) noexcept {
  auto o = l;
  ++l;
  return o;
}

//
// Subtraction operators
//

template<SequentialEnum E>
[[nodiscard]] constexpr E
operator-(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, true>(*l - details::clip_if_wrap<E>(r));
}

template<SequentialEnum E>
constexpr E& operator-=(E& l, std::underlying_type_t<E> r) noexcept {
  return l = l - r;
}

template<SequentialEnum E>
constexpr E& operator--(E& l) noexcept {
  if constexpr (details::seq_actually_wrap_v<E>)
    if (*l == details::seq_min_num_v<E>) return l = seq_max_v<E>;

  return l = E(*l - 1);
}

template<SequentialEnum E>
[[nodiscard]] constexpr E operator--(E& l, int) noexcept {
  auto o = l;
  --l;
  return o;
}

} // namespace ops

//
// Named functions
//

// Traits

// Maximum value.
template<SequentialEnum E>
constexpr E max_value() noexcept {
  return seq_max_v<E>;
}

// Minimum value.
template<SequentialEnum E>
constexpr E min_value() noexcept {
  return seq_min_v<E>;
}

// Cast sequence to specified integral type.
//
// Like `std::to_integer<IntegerType>(std::byte)`.
template<std::integral T>
constexpr T to_integer(SequentialEnum auto v) noexcept {
  return static_cast<T>(v);
}

// Length of range.
template<SequentialEnum E>
constexpr auto range_length() noexcept {
  return to_integer<size_t>(details::seq_size_v<E>());
}

namespace details {

// sequence_printer
template<SequentialEnum E, std::size_t N>
struct sequence_printer {
public:
  constexpr sequence_printer(std::array<std::string_view, N> name_list)
      : names(name_list) {}

  auto& append(AppendTarget auto& target, E v) const {
    auto n = as_underlying(v);
    auto ofs = n - *min_value<E>();

    // Print looked-up name or the numerical value.
    if (ofs < N && names[ofs].size())
      strings::appender{target}.append(names[ofs]);
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
//
// Note that an empty string for a value results in the number being printed.
template<SequentialEnum E, std::size_t N>
constexpr auto make_enum_printer(std::string_view (&&l)[N]) {
  static_assert(N <= details::seq_size_v<E>, "Too many names");
  return details::sequence_printer<E, N>(std::to_array<std::string_view>(l));
}

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

} // namespace sequence
} // namespace corvid::enums
