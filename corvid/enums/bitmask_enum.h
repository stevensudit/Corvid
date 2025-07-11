// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include "enums_shared.h"
#include "../strings/lite.h"
#include "../containers/opt_find.h"
#include "enum_registry.h"
#include "scoped_enum.h"

namespace corvid { inline namespace enums { namespace bitmask {

//
// bitmask enum
//

// A bitmask enum is a scoped enum (aka `enum class`) whose values are
// made of bits that can be independently referenced. It satisfies the
// BitmaskType named requirements, as defined by
// https://en.cppreference.com/w/cpp/named_req/BitmaskType, while providing
// some additional functionality.

// Prerequisites: Your scoped enum must have 1 or more contiguous bits,
// starting from the lsb, such that the value of any combination of those bits
// is valid. Valid values do not need to be named and neither do valid bits.
//
// It is generally a good idea to define the enum in terms of an unsigned type,
// since this is a collection of bits and not a numerical value. Failing to do
// so can lead to strange side-effects, such as `max_value` being negative when
// all bits are valid.
//
// The way to register a scoped enum as a bitmask is to specialize the
// `corvid::meta::enums::registry::enum_spec_v` for the enum type and assign an
// instance of `bitmask_enum_spec` to it. There are various
// `make_bitmask_enum*_spec` helper functions that let you specify the valid
// bits, and optionally, their names.

// Wrapping:
//
// If you want to enable wrapping, which ensures that operations keep
// values within valid range (at the cost of runtime range checks), set
// `bitclip` to `wrapclip::limit`.
//
// The only operation that sets invalid bits when given valid inputs is
// `operator~`, but `flip` offers a safe alternative. While `make` can set
// invalid bits given an invalid input, `make_safely` does not.
//
// However, when `bitclip` is `wrapclip::limit`, then `operator~` and
// `make` become equivalent to `flip` and `make_safely`, respectively.
// (This also affects the functions that rely on these.)
//
// While this feature is relatively inexpensive, it does count as a subtle
// violation of BitmaskType requirements. Having said that, it's opt-in.

// Registration.
//
// Example:
//
//    enum class rgb { red = 4, green = 2, blue = 1 };
//
//    template<>
//    constexpr inline auto registry::enum_spec_v<rgb> =
//        make_bitmask_enum_spec<rgb, "red,green,blue">();

template<ScopedEnum E, uint64_t validbits = 0, wrapclip bitclip = wrapclip{}>
struct bitmask_enum_spec
    : public registry::scoped_enum_spec<E, E{}, E{}, false, wrapclip{},
          validbits, bitclip> {};

inline namespace internal {

// valid_bits_v
//
// The valid bits of the enum, as mask..
template<typename E>
constexpr auto valid_bits_v = registry::enum_spec_v<E>.valid_bits_v;

// Whether to clip operations to the valid bits.
template<typename E>
constexpr bool bit_clip_v =
    (registry::enum_spec_v<E>.bit_clip_v == wrapclip::limit);

// Concept for bitmask enum.
template<typename E>
concept BitmaskEnum = (valid_bits_v<E> != 0);

namespace details {
// Guts of max_value, moved up to satisfy compiler.
template<BitmaskEnum E>
constexpr E do_max_value() noexcept {
  return E(valid_bits_v<E>);
}
} // namespace details

} // namespace internal
inline namespace ops {

//
// Operator overloads.
//

//
// Logical operators
//

template<BitmaskEnum E>
[[nodiscard]] constexpr bool operator!(E v) noexcept {
  return !as_underlying<E>(v);
}

// Dereference operator.
//
// The precedent for this is `std::optional`.
template<BitmaskEnum E>
[[nodiscard]] constexpr auto operator*(E v) noexcept {
  return as_underlying<E>(v);
}

// Or operators.
template<BitmaskEnum E>
[[nodiscard]] constexpr E operator|(E l, E r) noexcept {
  return E(*l | *r);
}

template<BitmaskEnum E>
constexpr const E& operator|=(E& l, E r) noexcept {
  return l = l | r;
}

// And operators.
template<BitmaskEnum E>
[[nodiscard]] constexpr E operator&(E l, E r) noexcept {
  return E(*l & *r);
}

template<BitmaskEnum E>
constexpr const E& operator&=(E& l, E r) noexcept {
  return l = l & r;
}

// Xor operators.
template<BitmaskEnum E>
[[nodiscard]] constexpr E operator^(E l, E r) noexcept {
  return E(*l ^ *r);
}

template<BitmaskEnum E>
constexpr const E& operator^=(E& l, E r) noexcept {
  return l = l ^ r;
}

// Complement operator.
//
// Unless `wrapclip::limit`, this may set invalid bits, whereas `flip` will
// not. When `wrapclip::limit`, does the same thing as `flip`.
template<BitmaskEnum E>
[[nodiscard]] constexpr E operator~(E v) noexcept {
  if constexpr (bit_clip_v<E>)
    return v ^ details::do_max_value<E>();
  else
    return E(~*v);
}

// Plus operators.
template<BitmaskEnum E>
[[nodiscard]] constexpr E operator+(E l, E r) noexcept {
  return l | r;
}

template<BitmaskEnum E>
constexpr const E& operator+=(E& l, E r) noexcept {
  return l = l + r;
}

// Minus operators.
template<BitmaskEnum E>
[[nodiscard]] constexpr E operator-(E l, E r) noexcept {
  return l & ~r;
}

template<BitmaskEnum E>
constexpr const E& operator-=(E& l, E r) noexcept {
  return l = l - r;
}

} // namespace ops

//
// Named functions
//

// Traits

// Maximum value, inclusive, which is also a mask of valid bits.
//
// Note: If underlying type is signed and the high bit is valid, this value
// will be negative. It's technically correct, even then, but maybe you should
// use an unsigned underlying type. However, the default underlying type is
// `int`, which is signed.
template<BitmaskEnum E>
[[nodiscard]] constexpr E max_value() noexcept {
  return details::do_max_value<E>();
}

// Minimum value, inclusive, which is always 0.
template<BitmaskEnum E>
[[nodiscard]] constexpr E min_value() noexcept {
  return E{};
}

// Length of bits.
template<BitmaskEnum E>
[[nodiscard]] constexpr size_t bits_length() noexcept {
  return std::bit_width(valid_bits_v<E>);
}

// Cast bitmask to specified integral type.
//
// Like `std::to_integer<IntegerType>(std::byte)`.
template<std::integral T>
[[nodiscard]] constexpr T to_integer(BitmaskEnum auto v) noexcept {
  return static_cast<T>(v);
}

// Length of range.
//
// This is the number of distinct values that are valid, if and only if valid
// bits are contiguous.
//
// Note: If `max_value_v` is the same as the maximum value of `size_t`, returns
// 0, which is confusing but technically correct, which is the best kind of
// correct.
//
// TODO: Consider making this correct even for validity masks with holes, or at
// least failing cleanly if we can't do that.
template<BitmaskEnum E>
[[nodiscard]] constexpr auto range_length() noexcept {
  return to_integer<size_t>(max_value<E>()) + 1;
}

// Makers

// Cast integer value to bitmask, keeping only the valid bits.
template<BitmaskEnum E>
[[nodiscard]] constexpr E make_safely(std::underlying_type_t<E> u) noexcept {
  return static_cast<E>(u) & max_value<E>();
}

// Cast integer value to bitmask. When `wrapclip::limit`, clips value to ensure
// safety.
template<BitmaskEnum E>
[[nodiscard]] constexpr E make(std::underlying_type_t<E> u) noexcept {
  if constexpr (bit_clip_v<E>)
    return make_safely<E>(u);
  else
    return static_cast<E>(u);
}

// Return value with bit at `ndx` (counting from the lsb) set.
template<BitmaskEnum E>
[[nodiscard]] constexpr E make_at(size_t ndx) noexcept {
  return make<E>(std::underlying_type_t<E>{1} << (ndx - 1));
}

// Set

// Return `v` with the bits in `m` set.
template<BitmaskEnum E>
[[nodiscard]] constexpr E set(E v, E m) noexcept {
  return v + m;
}

// Return `v` with the bits in `m` set only if `pred`.
template<BitmaskEnum E>
[[nodiscard]] constexpr E set_if(E v, E m, bool pred) noexcept {
  return pred ? v + m : v;
}

// Return `v` with the bits set in `m` cleared.
template<BitmaskEnum E>
[[nodiscard]] constexpr E clear(E v, E m) noexcept {
  return v - m;
}

// Return `v` with the bits in `m` cleared only if `pred`.
template<BitmaskEnum E>
[[nodiscard]] constexpr E clear_if(E v, E m, bool pred) noexcept {
  return pred ? v - m : v;
}

// Return `v` with the bits set in `m` set to `value`.
template<BitmaskEnum E>
[[nodiscard]] constexpr E set_to(E v, E m, bool value) noexcept {
  return value ? v + m : v - m;
}

// Return `v` with only the valid bits flipped.
template<BitmaskEnum E>
[[nodiscard]] constexpr E flip(E v) noexcept {
  return v ^ max_value<E>();
}

// Set at index

// Return `v` with the bit at `ndx` set.
template<BitmaskEnum E>
[[nodiscard]] constexpr E set_at(E v, size_t ndx) noexcept {
  return v + make_at<E>(ndx);
}

// Return `v` with the bit at `ndx` set only if `pred`.
template<BitmaskEnum E>
[[nodiscard]] constexpr E set_at_if(E v, size_t ndx, bool pred) noexcept {
  return pred ? v + make_at<E>(ndx) : v;
}

// Return `v` with the bit at `ndx` clear.
template<BitmaskEnum E>
[[nodiscard]] constexpr E clear_at(E v, size_t ndx) noexcept {
  return v - make_at<E>(ndx);
}

// Return `v` with the bit at `ndx` clear only if `pred`.
template<BitmaskEnum E>
[[nodiscard]] constexpr E clear_at_if(E v, size_t ndx, bool pred) noexcept {
  return pred ? v - make_at<E>(ndx) : v;
}

// Return `v` with the bit at `ndx` set to `value`.
template<BitmaskEnum E>
[[nodiscard]] constexpr E set_at_to(E v, size_t ndx, bool value) noexcept {
  return value ? set_at(v, ndx) : clear_at(v, ndx);
}

// Has

// Return whether `v` has any of the bits in `m` set.
template<BitmaskEnum E>
[[nodiscard]] constexpr bool has(E v, E m) noexcept {
  return (v & m) != E(0);
}

// Return whether `v` has all the bits in `m` set.
template<BitmaskEnum E>
[[nodiscard]] constexpr bool has_all(E v, E m) noexcept {
  return (v & m) == m;
}

// Returns whether `v` is missing some of the bits set in `m`.
template<BitmaskEnum E>
[[nodiscard]] constexpr bool missing(E v, E m) noexcept {
  return !has_all(v, m);
}

// Return whether `v` is missing all of the bits set in `m`.
template<BitmaskEnum E>
[[nodiscard]] constexpr bool missing_all(E v, E m) noexcept {
  return !has(v, m);
}

namespace details {
// Helper function to append bitmask to target, using bit names.
template<ScopedEnum E, size_t N>
auto& do_bit_append(AppendTarget auto& target, E v,
    const std::array<std::string_view, N>& names) {
  static constexpr strings::delim plus(" + ");
  bool first{true};

  for (size_t ndx = N; ndx != 0; --ndx) {
    auto mask = make_at<E>(ndx);
    auto ofs = N - ndx;

    // If bit matched, print and remove.
    if (has(v, mask) && names[ofs].size()) {
      plus.append_skip_first(target, first);
      strings::appender{target}.append(names[ofs]);
      v = E(*v & ~*mask);
    }
  }

  // Print residual in hex.
  if (*v || first)
    strings::append_num<16>(plus.append_skip_first(target, first), *v);
  return target;
}

// Helper function to append bitmask to target, using value names.
template<ScopedEnum E, size_t N>
auto& do_value_append(AppendTarget auto& target, E v,
    const std::array<std::string_view, N>& names) {
  static constexpr strings::delim plus(" + ");
  constexpr size_t all_valid_bits = valid_bits_v<E>;
  bool first{true};

  // First try to do a direct lookup.
  const auto valid_part = *v & all_valid_bits;
  if (names[valid_part].size()) {
    plus.append_skip_first(target, first);
    strings::appender{target}.append(names[valid_part]);
    v = E(*v & ~all_valid_bits);
  }

  // Otherwise, do a linear search for the remaining named values.
  if (first) {
    for (int64_t ndx = valid_part; ndx > 0; --ndx) {
      auto mask = E(ndx);

      // If bits matched, print and remove.
      if (has_all(v, mask) && names[ndx].size()) {
        plus.append_skip_first(target, first);
        strings::appender{target}.append(names[ndx]);
        v = E(*v & ~*mask);

        // If no valid bits left, drop to number.
        if (((*v & all_valid_bits) == 0)) break;
      }
    }
  }

  // As a special case, if we haven't printed anything, then try to print the
  // zeroth value before we deal with residuals.
  if (first && names[0].size()) {
    first = false;
    strings::appender{target}.append(names[0]);
  }

  // Print residual.
  if (*v || first)
    strings::append_num<16>(plus.append_skip_first(target, first), *v);
  return target;
}

// Specialization of `bitmask_enum_spec`, adding a list of names, either for
// the bits or the values. Use `make_bitmask_enum_spec` or
// `make_bitmask_enum_names_spec`, respectively, to construct.
template<ScopedEnum E, wrapclip bitclip = wrapclip{}, E validbits = E{},
    std::size_t N = 0>
struct bitmask_enum_names_spec
    : public bitmask_enum_spec<E, meta::as_underlying(validbits), bitclip> {
  constexpr bitmask_enum_names_spec(
      const std::array<std::string_view, N>& name_list)
      : names(name_list) {}

  auto& append(AppendTarget auto& target, E v) const {
    if constexpr (N == bits_length<E>())
      return details::do_bit_append(target, v, names);
    else if constexpr (N)
      return details::do_value_append(target, v, names);
    else
      return strings::append_num<16>(target, *v);
  }

  bool lookup(E& v, std::string_view sv) const {
    if (sv.empty()) return false;
    if (registry::details::lookup_helper(v, sv)) {
      if constexpr (bit_clip_v<E>)
        if (v != make<E>(*v)) return false;
      return true;
    }
    auto found = std::find(names.begin(), names.end(), sv);
    if (found == names.end()) return false;
    auto ofs = std::distance(names.begin(), found);
    constexpr auto bits = bits_length<E>();
    if constexpr (N == bits) ofs = 1 << (bits - ofs - 1);
    v = static_cast<E>(ofs);
    return true;
  }

  const std::array<std::string_view, N> names;
};

// Compile-time conversion of bit name array to valid bits. The names start
// with the msb. For each non-empty name, sets the corresponding bit as valid.
// Do not put a leading comma in the name list.
//
/// Whitespace is trimmed. An empty string or hyphen means invalid.
//
// TODO: Consider offering a version that is aligned to the msb instead of the
// lsb. This would avoid forcing the user to potentally pad with dozens of
// commas.
template<strings::fixed_string bit_names>
consteval uint64_t calc_valid_bits_from_bit_names() {
  static_assert(!strings::trim(bit_names.view(), " -").starts_with(","));
  constexpr auto name_array = strings::fixed_split_trim<bit_names, " -">();
  uint64_t valid_bits = 0;
  uint64_t pow2 = 1;

  for (int i = name_array.size() - 1; i >= 0; --i) {
    if (!name_array[i].empty()) valid_bits |= pow2;
    pow2 <<= 1;
  }

  return valid_bits;
}

// Compile-time conversion of bit value array to valid bits. The values start
// at 0 and are sequential. The union of the bits from each of the values
// defines the valid bits.
//
/// Whitespace is trimmed.
template<strings::fixed_string bit_names>
consteval uint64_t calc_valid_bits_from_value_names() {
  static_assert(contains(bit_names.view(), ','));
  constexpr auto name_array = strings::fixed_split_trim<bit_names, " -">();
  uint64_t valid_bits = 0;
  for (size_t i = 1; i < name_array.size(); ++i) {
    if (!name_array[i].empty()) valid_bits |= i;
  }
  return valid_bits;
}
} // namespace details

// Make an `enum_spec_v` from its valid bits, marking `E` as a bitmask enum.
// If the enum has named value that is the maximum, pass that in for validbits.
// Otherwise, specify the number explicitly.
//
// Set `bitclip` to `wrapclip::limit` to enable clipping.
//
// The numerical value is printed in hex.
template<ScopedEnum E, IntegerOrEnum auto validbits = 0,
    wrapclip bitclip = wrapclip{}>
consteval auto make_bitmask_enum_spec() {
  return details::bitmask_enum_names_spec<E, bitclip, validbits, 0>{
      std::array<std::string_view, 0>{}};
}

// Make an `enum_spec_v` from a list of bit names, starting with msb, marking
// `E` as a bitmask enum.
//
// The list must be a string literal, delimited by commas. Whitespace is
// trimmed.
//
//  An element that is empty or a hyphen means that bit is invalid, while a
//  question mark or asterisk is a wildcard placeholder which means nothing is
//  shown for that bit, but the bit is valid.
//
// You may wish to choose to follow the convention of specifying names with a
// built-in scope, as in "rgb::red" as opposed to "red".
//
// Set `bitclip` to `wrapclip::limit` to enable clipping.
//
// Prints the matching name for the value as a combination of bit names.
// Any bits, valid or otherwise, that are not named are printed in hex.
template<ScopedEnum E, strings::fixed_string bit_names,
    wrapclip bitclip = wrapclip{}>
consteval auto make_bitmask_enum_spec() {
  constexpr auto name_array = strings::fixed_split_trim<bit_names, " -">();
  constexpr auto filtered_names =
      strings::fixed_split_trim<bit_names, " -?*">();
  constexpr auto name_count = name_array.size();
  constexpr auto valid_bits =
      details::calc_valid_bits_from_bit_names<bit_names>();
  return details::bitmask_enum_names_spec<E, bitclip, E{valid_bits},
      name_count>{filtered_names};
}

// Make a `enum_spec_v` from a list of value names, marking `E` as a bitmask
// enum.
//
// The list is a string literal, delimited by commas. Whitespace is
// trimmed. These are the names of  all possible bit combinations, in sequence,
// starting from zero.
//
//  An empty string or hyphen means that value is invalid, while a question
//  mark or asterisk is a wildcard placeholder which means nothing is shown for
//  that value, but it is valid.
//
//  Only bits which are contained in the index of at least one of the valid
//  names are themselves valid.
//
// You may wish to choose to follow the convention of specifying names with a
// built-in scope, as in "rgb::yellow" as opposed to "yellow".
//
// Set `bitclip` to `wrapclip::limit` to enable clipping.
//
// Prints the matching name for the value. Any residual value is printed in
// hex, in combination with the known part of the value, if any.
template<ScopedEnum E, strings::fixed_string bit_names,
    wrapclip bitclip = wrapclip{}>
[[nodiscard]] constexpr auto make_bitmask_enum_values_spec() {
  constexpr auto name_array = strings::fixed_split<bit_names>();
  constexpr auto trimmed_names =
      strings::fixed_split_trim<bit_names, " -?*">();
  constexpr auto name_count = name_array.size();
  constexpr auto valid_bits =
      details::calc_valid_bits_from_value_names<bit_names>();
  return details::bitmask_enum_names_spec<E, bitclip, E{valid_bits},
      name_count>{trimmed_names};
}

}}} // namespace corvid::enums::bitmask

//
// TODO
//

// TODO: Offer a printer that displays the value in binary. It would be defined
// much like the rest, except that the names would just so happen to be
// single-character, such as "r,g,b". Given this definition, we'd display "RGB"
// for white, "RgB" for purple, and "rgb" for black. If the definition was
// instead "R,G,B", then  we would display "RGB" for white, "R-B" for purple,
// and "---" for black.
// We follow the same rules about placeholders, where space or hyphen mean its
// invalid and an asterisk or question mark mean its valid but unnamed. When
// displaying a value, the position of a nameless bit is filled with a hyphen,
// so if the definition is "r,-,b" or "r,?,b", we'd show purple or white as
// "R-B". For white, since we have a residual, we'd show it as usual, as a hex
// number added to the end. In theory, we could just treat a definition with
// single-character names as a binary output, but that would be unwise due to
// possible ambiguities. Instead, we need a separate
// make_bitmask_enum_binary_spec() that enforces single-character names and
// also sets a new flag in bitmask_enum_names_spec so that its append
// multiplexer knows how to handle it.

// Consider offering a parser to convert string to enum, using the same
// definition. In principle, it could split on "+", trim spaces, and then add
// up the bit values. A working system would simply do a linear search through
// the list of names, but maybe something like
// https://blog.quarkslab.com/frozen-an-header-only-constexpr-alternative-to-gperf-for-c14-users.html
// would work better. The idea would be to construct a map of names to values
// at compile time and then query it at runtime. We really just need the part
// where it sorts it at compile time, allowing us to do binary searches at
// runtime.

// TODO: Consider providing `operator[]` that returns bool for a given
// index. Essentially, the op version of `get_at`. At that point, we could
// also provide a proxy object to invoke `set_at`.

// TODO: Wacky idea:
// `rgb_yellow == some(rgb::red, rgb::green)`
//
// `rgb_yellow == all(rgb::red, rgb::green)`
//
// The function returns a local type that is initialized on the & of the
// parameters and offers an appropriate op== and !=. So != some means has
// none and != all means it doesn't have all, but might have some. This
// isn't terrible. Make sure it doesn't interfere with direct == and !=.
