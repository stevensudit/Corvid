// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include "../strings/lite.h"
#include "enum_registry.h"
#include "scoped_enum.h"

namespace corvid { inline namespace enums { namespace sequence {

//
// sequence enum
//

// A sequence enum is a scoped enum (aka `enum class`) that holds a sequence of
// contiguous values. It supports operations such as add and subtract, while
// providing some additional functionality. Conceptually, sequential values are
// mutually exclusive options.
//
// Prerequisites: Your scoped enum must have a minimum and maximum valid value,
// and all values between these, inclusive, must be valid. In other words, the
// range is [min,max].
//
// This range doesn't have to start at 0, and if the underlying type is signed,
// you can specify a negative value for `minseq`.
//
// Valid values do not need to be named. So, for example, `std::byte` has a
// range of [0,255], but no named values.
//
// The way to register a scoped enum as a sequence is to specialize the
// `corvid::enums::registry::enum_spec_v` for the enum type and assign an
// instance of `sequence_enum_spec` to it by calling a
// `make_sequence_enum_spec` overload.
//
// You must set `maxseq` to the highest enum value that is valid, or allow it
// to be inferred from the comma-delimited list of value names. If the lowest
// value isn't 0, then also set `minseq`.
//
// If you want to enable wrapping, which ensures that operations keep
// values within valid range (at the cost of runtime range checks), set
// `wrapseq` to `wrapclip::limit`.
//
// For example:
//
//    enum class tiger_pick { eeny, meany, miny, moe };
//
//    template<>
//    constexpr inline auto registry::enum_spec_v<tiger_pick> =
//        make_sequence_enum_spec<tiger_pick, "eeny, meany, miny, moe">();

template<ScopedEnum E, E maxseq = E{}, E minseq = E{},
    wrapclip wrapseq = wrapclip{}>
struct sequence_enum_spec
    : public registry::scoped_enum_spec<E, minseq, maxseq, true, wrapseq, 0,
          wrapclip{}> {};

// Concept for sequential enum.
template<typename E>
concept SequentialEnum = (registry::enum_spec_v<E>.seq_valid_v);

inline namespace internal {

// Maximum value (inclusive).
template<typename E>
constexpr auto seq_max_v = registry::enum_spec_v<E>.seq_max_v;

// Minimum value (inclusive). Must be less than `seq_max_v`.
template<typename E>
constexpr auto seq_min_v = registry::enum_spec_v<E>.seq_min_v;

// Whether to wrap all calculations to keep them in range.
template<typename E>
constexpr bool seq_wrap_v =
    (registry::enum_spec_v<E>.seq_wrap_v == wrapclip::limit);

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

// Whether wrapping is really needed. We don't need to wrap when the range
// exactly fits the underlying type, because of modulo math.
template<typename E>
constexpr bool seq_actually_need_wrap_v = (seq_size_v<E> != 0);

// Whether wrapping is really enabled. It's enabled only when we are asked to
// wrap and actually need to.
template<typename E>
constexpr bool seq_actually_wrap_v =
    seq_actually_need_wrap_v<E> && seq_wrap_v<E>;

// Clip, unless `noclip` set, by modding to size.
template<SequentialEnum E, bool noclip = false>
[[nodiscard]] constexpr auto clip(std::underlying_type_t<E> u) {
  if constexpr (!noclip && seq_actually_need_wrap_v<E>)
    return u % seq_size_v<E>;
  else
    return u;
}

// Clip if wrapping enabled.
template<SequentialEnum E>
[[nodiscard]] constexpr auto clip_if_wrap(std::underlying_type_t<E> u) {
  return clip<E, !seq_actually_wrap_v<E>>(u);
}

} // namespace internal

//
// Makers
//

// Cast integer value from underlying type to sequence, wrapping to keep it in
// range.
template<SequentialEnum E, bool noclip = false>
[[nodiscard]] constexpr E make_safely(std::underlying_type_t<E> u) noexcept {
  // Wrapping is only meaningful if the underlying type is not a perfect fit.
  if constexpr (seq_actually_need_wrap_v<E>) {
    constexpr auto lo = seq_min_num_v<E>, hi = seq_max_num_v<E>;
    static_assert(lo <= hi);
    using U = std::underlying_type_t<E>;

    // Underflow is only possible if it starts above the underlying min.
    if constexpr (lo != std::numeric_limits<U>::min()) {
      if (u < lo) return E(hi + clip<E, noclip>(u - lo + 1));
    }

    // Overflow is only possible if it ends below the underlying max.
    if constexpr (hi != std::numeric_limits<U>::max()) {
      if (u > hi) return E(lo + clip<E, noclip>(u - hi - 1));
    }
  }
  return static_cast<E>(u);
}

// Cast integer value from underlying type to sequence. When `wrapclip::limit`,
// wraps value to ensure safety.
template<SequentialEnum E, bool noclip = false>
[[nodiscard]] constexpr E make(std::underlying_type_t<E> u) noexcept {
  if constexpr (seq_actually_wrap_v<E>)
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
// Only heterogeneous addition and subtraction operations are supported.
//
// When `wrapclip::limit`, all results are modulo the sequence size. Otherwise,
// they are undefined when they exceed the range.
//

//
// Logical operators
//

template<SequentialEnum E>
[[nodiscard]] constexpr bool operator!(E v) noexcept {
  return !as_underlying<E>(v);
}

//
// Addition operators
//

template<SequentialEnum E>
[[nodiscard]] constexpr E
operator+(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, true>(*l + clip_if_wrap<E>(r));
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
  if constexpr (seq_actually_wrap_v<E>)
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
  return make<E, true>(*l - clip_if_wrap<E>(r));
}

template<SequentialEnum E>
constexpr E& operator-=(E& l, std::underlying_type_t<E> r) noexcept {
  return l = l - r;
}

template<SequentialEnum E>
constexpr E& operator--(E& l) noexcept {
  if constexpr (seq_actually_wrap_v<E>)
    if (*l == seq_min_num_v<E>) return l = seq_max_v<E>;

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
[[nodiscard]] constexpr E max_value() noexcept {
  return seq_max_v<E>;
}

// Minimum value.
template<SequentialEnum E>
[[nodiscard]] constexpr E min_value() noexcept {
  return seq_min_v<E>;
}

// Cast sequence to specified integral type.
//
// Like `std::to_integer<IntegerType>(std::byte)`.
template<std::integral T>
[[nodiscard]] constexpr T to_integer(SequentialEnum auto v) noexcept {
  return static_cast<T>(v);
}

// Length of range.
template<SequentialEnum E>
[[nodiscard]] constexpr auto range_length() noexcept {
  return to_integer<size_t>(seq_size_v<E>());
}

namespace details {
// Helper function to append a sequence enum value to a target by using a list
// of value names. Behavior is documented in `make_sequence_enum_spec`.
template<ScopedEnum E, size_t N>
auto& do_seq_append(AppendTarget auto& target, E v,
    const std::array<std::string_view, N>& names) {
  auto n = as_underlying(v);
  size_t ofs = n - *min_value<E>();

  // Print looked-up name or the numerical value.
  if (ofs < names.size() && names[ofs].size())
    strings::appender{target}.append(names[ofs]);
  else
    strings::append_num(target, n);

  return target;
}

// Specialization of `sequence_enum_spec`, adding a list of names for the
// values. Use `make_sequence_enum_spec` to construct.
template<ScopedEnum E, E maxseq = E{}, E minseq = E{},
    wrapclip wrapseq = wrapclip{}, size_t N = 0>
struct sequence_enum_names_spec
    : public sequence_enum_spec<E, maxseq, minseq, wrapseq> {
  constexpr sequence_enum_names_spec(std::array<std::string_view, N> name_list)
      : names(name_list) {}

  auto& append(AppendTarget auto& target, E v) const {
    return do_seq_append(target, v, names);
  }

  bool lookup(E& v, std::string_view sv) const {
    if (sv.empty()) return false;
    if (registry::details::lookup_helper(v, sv)) {
      if constexpr (seq_actually_wrap_v<E>)
        if (v != make<E>(*v)) return false;
      return true;
    }
    auto found = std::find(names.begin(), names.end(), sv);
    if (found == names.end()) return false;
    v = make<E>(std::distance(names.begin(), found) + *min_value<E>());
    return true;
  }

  const std::array<std::string_view, N> names;
};
} // namespace details

// Make an `enum_spec_v` from a list of names, marking `E` as a sequence enum.
//
// The list must be a string literal, delimited by commas. Whitespace is
// trimmed. An element that is empty, a hyphen, or a question mark or asterisk
// means that nothing is shown for that value, but it's still valid.
//
// Set `wrapseq` to `wrapclip::limit` to enable wrapping.
//
// The `maxseq` is automatically calculated from the number of names, but if
// the minimum value isn't 0, you must set `minseq` to it.
//
// Prints the matching name for the value. If it is not in the range of the
// names, or if the name for that value is empty, the numerical value is
// printed.
template<ScopedEnum E, strings::fixed_string names,
    wrapclip wrapseq = wrapclip{}, E minseq = E{}>
[[nodiscard]] constexpr auto make_sequence_enum_spec() {
  constexpr auto name_array = strings::fixed_split_trim<names, " -?*">();
  constexpr auto name_count = name_array.size();
  constexpr auto maxseq = E{as_underlying(minseq) + name_count - 1};
  return details::sequence_enum_names_spec<E, maxseq, minseq, wrapseq,
      name_count>{name_array};
}

// Make an `enum_spec_v` from a range of values, marking `E` as a sequence
// enum.
//
// The `maxseq` must be specified, and if `minseq` isn't 0, it also does. It's
// ok to swap the order of the two.
//
// Set `wrapseq` to `wrapclip::limit` to enable wrapping.
//
// The numerical value is printed.
template<ScopedEnum E, E maxseq, E minseq = E{}, wrapclip wrapseq = wrapclip{}>
[[nodiscard]] constexpr auto make_sequence_enum_spec() {
  return sequence_enum_spec<E, maxseq, minseq, wrapseq>{};
}

//
// TODO
//

// TODO: It might be nice if we could specialize `std::numeric_limits` for all
// enums that are flagged as sequence, inheriting from the underlying class and
// providing correct min and max values. Not sure if that's actually possible,
// though. Maybe we can't do it automatically but can make it easier. Maybe
// inheriting with specialization might work.

// TODO: Consider making a variation on sequence enums that allows homogeneous
// operations, which makes sense for `std::byte` and other strongly-typed
// integers, like `arithmetic_enum`. It would also support the full set of ops.
// Possibly, it would also qualify as a SequentialEnum, but it would not
// benefit from anything more than numeric output.

}}} // namespace corvid::enums::sequence
