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
#include "enums_shared.h"
#include "../strings/lite.h"
#include "enum_registry.h"
#include "scoped_enum.h"
#include <string>

namespace corvid {
inline namespace enums { namespace sequence {

#pragma region sequence spec

// A sequence enum is a scoped enum (aka `enum class`) that holds a sequence of
// contiguous values. It supports operations such as add and subtract while
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
// The way to register a scoped enum as a sequence is to declare a
// `corvid_enum_spec` overload for it in the enum's own namespace, returning
// the result of a `make_sequence_enum_spec` overload. It is found by ADL.
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
//    consteval auto corvid_enum_spec(tiger_pick*) {
//      return make_sequence_enum_spec<tiger_pick, "eeny, meany, miny, moe">();
//    }

template<ScopedEnum E, E maxseq = E{}, E minseq = E{},
    wrapclip wrapseq = wrapclip{}>
struct sequence_enum_spec
    : public registry::scoped_enum_spec<E, minseq, maxseq, true, wrapseq, 0,
          wrapclip{}> {};

#pragma endregion
#pragma region Concept

// Concept for sequential enum.
template<typename E>
concept SequentialEnum = (registry::enum_spec_v<E>.seq_valid_v);

#pragma endregion
#pragma region internal

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

// Clip by modding to size when `mode` is `wrapclip::limit`.
template<SequentialEnum E, wrapclip mode = wrapclip::limit>
[[nodiscard]] constexpr auto clip(std::underlying_type_t<E> u) {
  if constexpr (mode == wrapclip::limit && seq_actually_need_wrap_v<E>)
    return u % seq_size_v<E>;
  else
    return u;
}

// Clip if wrapping enabled.
template<SequentialEnum E>
[[nodiscard]] constexpr auto clip_if_wrap(std::underlying_type_t<E> u) {
  return clip<E, seq_actually_wrap_v<E> ? wrapclip::limit : wrapclip::none>(u);
}

} // namespace internal

#pragma endregion
#pragma region Makers

// Cast integer value from underlying type to sequence, wrapping to keep it in
// range.
template<SequentialEnum E, wrapclip mode = wrapclip::limit>
[[nodiscard]] constexpr E make_safely(std::underlying_type_t<E> u) noexcept {
  // Wrapping is only meaningful if the underlying type is not a perfect fit.
  if constexpr (seq_actually_need_wrap_v<E>) {
    constexpr auto lo = seq_min_num_v<E>;
    constexpr auto hi = seq_max_num_v<E>;
    static_assert(lo <= hi);
    using U = decltype(u);

    // Underflow is only possible if it starts above the underlying min.
    if constexpr (lo != std::numeric_limits<U>::min()) {
      if (u < lo) return E(hi + clip<E, mode>(u - lo + 1));
    }

    // Overflow is only possible if it ends below the underlying max.
    if constexpr (hi != std::numeric_limits<U>::max()) {
      if (u > hi) return E(lo + clip<E, mode>(u - hi - 1));
    }
  }
  return static_cast<E>(u);
}

// Cast integer value from underlying type to sequence. When `wrapclip::limit`,
// wraps value to ensure safety.
template<SequentialEnum E, wrapclip mode = wrapclip::limit>
[[nodiscard]] constexpr E make(std::underlying_type_t<E> u) noexcept {
  if constexpr (seq_actually_wrap_v<E>)
    return make_safely<E, mode>(u);
  else
    return static_cast<E>(u);
}

#pragma endregion
#pragma region ops

inline namespace ops {

#pragma region Dereference

// Dereference operator.
//
// The precedent for this is `std::optional`.
template<SequentialEnum E>
[[nodiscard]] constexpr auto operator*(E v) noexcept {
  return as_underlying<E>(v);
}

#pragma endregion
#pragma region Logical ops

template<SequentialEnum E>
[[nodiscard]] constexpr bool operator!(E v) noexcept {
  return !as_underlying<E>(v);
}

#pragma endregion
#pragma region Addition ops

// Only heterogeneous addition and subtraction operations are supported.
//
// When `wrapclip::limit`, all results are modulo the sequence size. Otherwise,
// they are undefined when they exceed the range.
template<SequentialEnum E>
[[nodiscard]] constexpr E
operator+(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, wrapclip::none>(*l + clip_if_wrap<E>(r));
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

#pragma endregion
#pragma region Subtraction ops

template<SequentialEnum E>
[[nodiscard]] constexpr E
operator-(E l, std::underlying_type_t<E> r) noexcept {
  return make<E, wrapclip::none>(*l - clip_if_wrap<E>(r));
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

#pragma endregion

} // namespace ops

#pragma endregion
#pragma region Traits

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

#pragma endregion
#pragma region range_length

// Length of range.
template<SequentialEnum E>
[[nodiscard]] constexpr auto range_length() noexcept {
  return to_integer<size_t>(seq_size_v<E>);
}

#pragma endregion
#pragma region Lookup

// Look up exact string_view for value, or "" if not found or empty.
template<SequentialEnum E>
[[nodiscard]] constexpr std::string_view enum_as_view(E v) noexcept {
  return registry::enum_spec_v<std::decay_t<E>>.as_view(v);
}

// Linear search of a sequence enum's names for an exact match, returning that
// name or an empty view if absent. Names only; never interprets numeric text.
// Requires `E` to be registered with a name list.
template<SequentialEnum E>
[[nodiscard]] constexpr std::string_view
enum_find_named(std::string_view sv) noexcept {
  return registry::enum_spec_v<E>.find_named(sv);
}

// Linear search of a sequence enum's names for an exact match, returning the
// enum, or nullopt. Names only; never interprets numeric text. Requires `E` to
// be registered with a name list.
template<SequentialEnum E>
[[nodiscard]] constexpr std::optional<E>
enum_find_named_enum(std::string_view sv) noexcept {
  return registry::enum_spec_v<E>.find_named_enum(sv);
}

#pragma endregion
#pragma region enum_string_view

// A compile-time-validated name for one of sequence enum `E`'s values. Can be
// used as a type-safe wrapper over `std::string_view`.
//
// The `consteval` constructors accept a string literal that names a registered
// value, or the enum value itself, so a typo, an unknown name, or an unnamed
// value is a compile error rather than a runtime failure.
//
// Converts to `std::string_view`, so it passes anywhere a name view is wanted
// while carrying a guarantee a bare view cannot. `E` must be registered with a
// name list.
//
// Does not provide a UDL directly but is designed to easily support one.
//
// Example:
//
//  using color_name = enum_string_view<color>;
//
//  color_name red{"red"}; // OK
//  color_name blue{"blue"}; // OK
//  color_name typo{"reed"}; // Compile error: not a registered name
//  color_name empty{""}; // Compile error: empty is not a valid name
//  color_name green{color::green}; // OK: name looked up from the value
//
//  void paint(color_name c) { ... }
//
//  paint{"red"}; // OK
//  paint{"reed"}; // Compile error: not a registered name
//  paint{color::red}; // OK
//
//  consteval color_name operator""_color(const char* s, std::size_t n) {
//   return color_name{s, n};
//  }
//
//  auto c = "red"_color; // OK
//  auto d = "reed"_color; // Compile error: not a registered name
template<SequentialEnum E>
class enum_string_view {
public:
#pragma region Construction

  // Literal.
  template<size_t N>
  consteval enum_string_view(const char (&s)[N])
      : enum_string_view(s, N - 1) {}

  // From pointer + length (used by the literal ctor above and by UDLs).
  consteval enum_string_view(const char* s, size_t n)
      : sv_(enum_find_named<E>({s, n})) {
    if (sv_.empty()) throw "not a registered name for this enum";
  }

  // From the enum value itself.
  constexpr enum_string_view(E e) : sv_(enum_as_view(e)) {
    if (sv_.empty()) throw "not a registered name for this enum";
  }

#pragma endregion
#pragma region Factories

  // Safely force conversion at runtime.
  static constexpr auto convert(std::string_view sv) {
    assert(enum_find_named<E>(sv) == sv);
    return enum_string_view(sv, force_tag{});
  }

  // Unsafely force conversion at runtime.
  static constexpr auto force(std::string_view sv) {
    return enum_string_view(sv, force_tag{});
  }

#pragma endregion
#pragma region Accessors

  // Look up enum at compile time.
  [[nodiscard]] consteval E as_enum(E or_default = E{}) const noexcept {
    return enum_find_named_enum<E>(sv_).value_or(or_default);
  }

  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return sv_;
  }
  [[nodiscard]] constexpr std::string_view operator*() const noexcept {
    return sv_;
  }

  [[nodiscard]] constexpr auto operator->() const noexcept { return &sv_; }

#pragma endregion
protected:
#pragma region Forced construction

  struct force_tag {};
  constexpr enum_string_view(std::string_view sv, force_tag) : sv_(sv) {}

#pragma endregion
private:
#pragma region Data members

  std::string_view sv_;

#pragma endregion
};

#pragma endregion
#pragma region with value

// Extends `enum_string_view<E>` with the `E` value it names, both
// resolved at compile time by the same constructors. Use it where a call site
// needs the validated name and its enum together (e.g. to skip a runtime
// name->enum lookup), without adding a field to the bare string view.
template<SequentialEnum E>
class enum_value_string_view: enum_string_view<E> {
  using force_tag = enum_string_view<E>::force_tag;

public:
  using base = enum_string_view<E>;

#pragma region Construction

  // Literal.
  template<size_t N>
  consteval enum_value_string_view(const char (&s)[N])
      : base{s}, enum_{base{s}.as_enum()} {}

  // From pointer + length (e.g. a UDL).
  consteval enum_value_string_view(const char* s, size_t n)
      : base{s, n}, enum_{base{s, n}.as_enum()} {}

  // From the enum value itself.
  constexpr enum_value_string_view(E e) : base{e}, enum_{e} {}

#pragma endregion
#pragma region Factories

  // Safely force conversion at runtime.
  static constexpr auto convert(std::string_view sv, E e) {
    return enum_value_string_view{base::convert(sv), e, force_tag{}};
  }

  // Unsafely force conversion at runtime.
  static constexpr auto force(std::string_view sv, E e) {
    return enum_value_string_view{base::force(sv), e, force_tag{}};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return base::operator std::string_view();
  }
  [[nodiscard]] constexpr base as_name() const noexcept { return *this; }
  [[nodiscard]] constexpr E as_enum() const noexcept { return enum_; }

#pragma endregion
protected:
#pragma region Forced construction

  constexpr enum_value_string_view(std::string_view sv, E e, force_tag f)
      : base{sv, f}, enum_{e} {}

#pragma endregion
private:
#pragma region Data members

  E enum_;

#pragma endregion
};

#pragma endregion
#pragma region details

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

  constexpr auto& append(AppendTarget auto& target, E v) const {
    return do_seq_append(target, v, names);
  }

  [[nodiscard]] constexpr std::string_view as_view(E v) const noexcept {
    auto n = as_underlying(v);
    size_t ofs = n - *min_value<E>();
    if (ofs < names.size() && names[ofs].size()) return names[ofs];
    return {};
  }

  // Linear search of the names for an exact match, returning the enum, or
  // nullopt. Names only: unlike `lookup`, it never interprets numeric text, so
  // it stays usable in a constant expression.
  [[nodiscard]] constexpr std::optional<E> find_named_enum(
      std::string_view sv) const noexcept {
    if (sv.empty()) return {};
    auto found = std::find(names.begin(), names.end(), sv);
    return found != names.end()
               ? make<E>(std::distance(names.begin(), found) + *min_value<E>())
               : std::optional<E>{};
  }

  // Linear search of the names for an exact match, returning that name or an
  // empty view if absent. Names only: unlike `lookup`, it never interprets
  // numeric text, so it stays usable in a constant expression.
  [[nodiscard]] constexpr std::string_view find_named(
      std::string_view sv) const noexcept {
    if (sv.empty()) return {};
    auto found = std::find(names.begin(), names.end(), sv);
    return found != names.end() ? *found : std::string_view{};
  }

  [[nodiscard]] bool lookup(E& v, std::string_view sv) const {
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

#pragma endregion
#pragma region make spec

// Make an `enum_spec_v` from a list of names, marking `E` as a sequence enum.
//
// The list must be a string literal, delimited by commas. Whitespace is
// trimmed. An element that is empty, a hyphen, or a question mark or asterisk
// is a placeholder, and its numeric value is printed instead of a name. The
// same applies to values outside the name list's range.
//
// Set `wrapseq` to `wrapclip::limit` to enable wrapping.
//
// The `maxseq` is automatically calculated from the number of names, but if
// the minimum value isn't 0, you must set `minseq` to it.
template<ScopedEnum E, strings::fixed_string names,
    wrapclip wrapseq = wrapclip{}, E minseq = E{}>
[[nodiscard]] consteval auto make_sequence_enum_spec() {
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
[[nodiscard]] consteval auto make_sequence_enum_spec() {
  return sequence_enum_spec<E, maxseq, minseq, wrapseq>{};
}

#pragma endregion

}} // namespace enums::sequence

using namespace corvid::enums::sequence::ops;

} // namespace corvid
