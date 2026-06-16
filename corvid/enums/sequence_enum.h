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
#include <algorithm>
#include <string>
#include <string_view>
#include <stdexcept>
#include <optional>

#include "enums_shared.h"
#include "../strings/fixed_string_utils.h"
#include "../strings/string_view_wrapper.h"
#include "../strings/cstring_view.h"
#include "../strings/targeting.h"
#include "../strings/conversion.h"
#include "enum_registry.h"
#include "scoped_enum.h"

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
//      return make_sequence_enum_spec<tiger_pick, "eeny,meany,miny,moe">();
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

// Length of range: the count of values in `[min, max]`, including any unnamed
// gaps. Returns 0 when the range spans the full underlying type and the count
// wraps, matching the bitmask `range_length`.
template<SequentialEnum E>
[[nodiscard]] constexpr auto range_length() noexcept {
  return static_cast<size_t>(seq_size_v<E>);
}

#pragma endregion
#pragma region Lookup

// Look up the canonical name for value, or an empty view if it has none.
// Requires `E` to be registered with a name list.
template<SequentialEnum E>
[[nodiscard]] constexpr cstring_view enum_as_view(E v) noexcept {
  return registry::enum_spec_v<std::decay_t<E>>.find_name_by_enum(v);
}

// Map a candidate name to the registry's own copy of it (interning), or an
// empty view if it is not a registered name. Names only; never interprets
// numeric text. Requires `E` to be registered with a name list.
template<SequentialEnum E>
[[nodiscard]] constexpr cstring_view
enum_intern_name(std::string_view sv) noexcept {
  return registry::enum_spec_v<E>.intern_name(sv);
}

// Linear search of a sequence enum's names for an exact match, returning the
// enum, or nullopt. Names only; never interprets numeric text. Requires `E` to
// be registered with a name list.
template<SequentialEnum E>
[[nodiscard]] constexpr std::optional<E>
enum_find_by_name(std::string_view sv) noexcept {
  return registry::enum_spec_v<E>.find_enum_by_name(sv);
}

#pragma endregion
#pragma region enum_name

// A name for a registered value of a sequence enum, validated and interned.
// Can do both at compile time, as part of implicit conversion. Acts as a
// strongly typed wrapper for `std::string_view`, which cannot be empty. Does
// not provide a UDL directly but is designed to easily support one.
//
// The `consteval` constructors accept a string literal that is the name for a
// value, or the value itself. As a result, a typo, an unknown name, or an
// unnamed value is a compile error rather than a runtime failure.
//
// Allows checked runtime assignment, with a risky `force` option.
//
// Example:
//
//  using color_name = enum_name<color>;
//
//  color_name red{"red"};   // OK
//  color_name blue{"blue"}; // OK
//  color_name typo{"reed"}; // Compile error: not a registered name
//  color_name empty{""};    // Compile error: empty is not a valid name
//  color_name green{color::green}; // OK: name looked up from the value
//
//  void paint(color_name c) { ... }
//
//  paint{"red"};      // OK
//  paint{"reed"};     // Compile error: not a registered name
//  paint{color::red}; // OK
//
//  consteval color_name operator""_color(const char* s, std::size_t n) {
//    return color_name{s, n};
//  }
//
//  auto c = "red"_color;  // OK
//  auto d = "reed"_color; // Compile error: not a registered name
template<SequentialEnum E>
class enum_name: public string_view_wrapper<enum_name<E>> {
  using base = string_view_wrapper<enum_name<E>>;
  using base::operator->;
  using base::operator*;

public:
  using view_t = base::view_t;

#pragma region Construction

  // Convert from literal.
  template<size_t N>
  consteval enum_name(const char (&s)[N]) : enum_name(s, N - 1) {}

  // Convert from pointer + length.
  consteval enum_name(const char* s, size_t n)
      : base{enum_intern_name<E>({s, n})} {
    if (base::empty())
      throw std::invalid_argument("not a registered name for this enum");
  }

  // Convert from the enum value itself. Can be called `consteval` but does not
  // need to be. To construct with a value that has no name, use `force`.
  constexpr enum_name(E e) : base{enum_as_view(e)} {
    if (base::empty())
      throw std::invalid_argument("not a registered name for this enum");
  }

#pragma endregion
#pragma region Factories

  // Intern `sv` at runtime.
  //
  // Looks up the name in the registry and keeps the registry's own canonical
  // copy, so the result carries the same interning guarantee as the
  // `consteval` constructors. Throws if `sv` is not a registered name.
  static constexpr auto intern(std::string_view sv) {
    if (const auto self = try_intern(sv); self) return *self;
    throw std::invalid_argument("not a registered name for this enum");
  }

  // Attempt to intern `sv` at runtime.
  //
  // If this does not match a name registered for the enum, fails so that the
  // caller can use the `force`.
  static constexpr std::optional<enum_name<E>> try_intern(
      std::string_view sv) {
    if (const auto interned = enum_intern_name<E>(sv); !interned.empty())
      return force(interned);
    return std::nullopt;
  }

  // Force runtime construction of non-interned name, which might not even be
  // registered for a value.
  //
  // Keeps `sv` verbatim, without interning. Does not look it up; only
  // validates that it's not empty.
  //
  // The caller vouches for the bytes and their lifetime, and the result of
  // calls with equal contents do not necessarily share storage.
  //
  // Note: There is no `try_force` (or `triforce`), only do `force`.
  static constexpr auto force(std::string_view sv) {
    if (sv.empty())
      throw std::invalid_argument("empty string is not a valid name");
    return enum_name(sv, force_tag{});
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] constexpr auto as_view() const noexcept {
    return base::view();
  }

  // Look up enum at compile time.
  [[nodiscard]] consteval E as_enum() const noexcept {
    return *enum_find_by_name<E>(as_view());
  }

  // Find enum at runtime. Can fail if `force` was used, hence the
  // `or_default`.
  [[nodiscard]] E find_enum(E or_default = E{}) const noexcept {
    return enum_find_by_name<E>(as_view()).value_or(or_default);
  }

#pragma endregion
protected:
#pragma region Forced construction

  struct force_tag {};
  constexpr enum_name(std::string_view sv, force_tag) : base{sv} {}

#pragma endregion
};

#pragma endregion
#pragma region with value

// Extends `enum_name<E>` with the `E` value it names, both
// resolved at compile time by the same constructors. Use it where a call site
// needs the validated name and its enum together (e.g. to skip a runtime
// name->enum lookup), without adding a field to the bare string view.
template<SequentialEnum E>
class enum_named_value: enum_name<E> {
  using force_tag = enum_name<E>::force_tag;

public:
  using base = enum_name<E>;

#pragma region Construction

  // Convert from literal.
  template<size_t N>
  consteval enum_named_value(const char (&s)[N])
      : base{s}, enum_{base::as_enum()} {}

  // Convert from pointer + length.
  consteval enum_named_value(const char* s, size_t n)
      : base{s, n}, enum_{base::as_enum()} {}

  // Convert from the enum value itself.
  constexpr enum_named_value(E e) : base{e}, enum_{e} {}

#pragma endregion
#pragma region Factories

  // Intern at runtime.
  //
  // If `e` does not have a name, or if `sv` is nonempty and does not match it,
  // throws.
  static constexpr auto intern(std::string_view sv, E e) {
    if (const auto self = try_intern(sv, e); self) return *self;
    throw std::invalid_argument("enum has no registered name to intern");
  }

  // Intern at runtime.
  //
  // If `sv` does not  match a name, throws.
  static constexpr auto intern(std::string_view sv) {
    if (const auto self = try_intern(sv); self) return *self;
    throw std::invalid_argument("not a registered name for this enum");
  }

  // Intern at runtime.
  //
  // If `e` does not have a name, throws.
  static constexpr auto intern(E e) {
    if (const auto self = try_intern(e); self) return *self;
    throw std::invalid_argument("enum has no registered name to intern");
  }

  // Attempt to intern `sv` and `e` at runtime.
  //
  // If `e` does not have a name, or if `sv` is nonempty and does not match it,
  // fails so that the caller can use the `force`.
  static constexpr std::optional<enum_named_value<E>>
  try_intern([[maybe_unused]] std::string_view sv, E e) {
    const auto self = try_intern(e);
    if (!self) return std::nullopt;
    if (!sv.empty() && sv != *self) return std::nullopt;
    return self;
  }

  // Attempt to intern `e` at runtime.
  //
  // If `e` does not have a name, fails.
  static constexpr std::optional<enum_named_value<E>> try_intern(E e) {
    if (const auto name = enum_as_view(e); !name.empty())
      return force(name, e);
    return std::nullopt;
  }

  // Attempt to intern `sv`. If `sv` does not match a name, fails.
  static constexpr std::optional<enum_named_value<E>> try_intern(
      [[maybe_unused]] std::string_view sv) {
    const auto value = enum_find_by_name<E>(sv);
    if (!value) return std::nullopt;
    const auto name = enum_as_view(*value);
    // TODO: We can optimize this by having a single lookup that returns both;
    // particularly when, for sparse enums, finding by enum requires a small
    // search.
    assert(sv == name);
    return force(name, *value);
  }

  // Force runtime construction of non-interned name, which might not even be
  // registered for a value, and might or might not match the value.
  //
  // Keeps `sv` and `e` verbatim, without interning. Does not look either up;
  // only validates that `sv`'s not empty.
  //
  // The caller vouches for the bytes and their lifetime, and the result of
  // calls with equal contents do not necessarily share storage.
  //
  //  Note: There is no `try_force` (or `triforce`), only do `force`.
  static constexpr auto force(std::string_view sv, E e) {
    if (sv.empty())
      throw std::invalid_argument("empty string is not a valid name");
    return enum_named_value{sv, e, force_tag{}};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return base::as_view();
  }
  [[nodiscard]] constexpr base as_name() const noexcept { return *this; }
  [[nodiscard]] constexpr E as_enum() const noexcept { return enum_; }

#pragma endregion
protected:
#pragma region Forced construction

  constexpr enum_named_value(std::string_view sv, E e, force_tag f)
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

// A contiguous run of enum values `[start, start + length)`, with the names in
// the matching slice of the packed array.
//
// Segments are stored in ascending, non-overlapping order, so a slice's offset
// is the running sum of the preceding segments' lengths. A dense registration
// is a single segment; a sparse one breaks the runs apart at the gaps, sizing
// the packed array to the named count, not the value range.
template<std::integral U>
struct enum_segment {
  U start;
  size_t length;
};

// The packed names, segment table, and derived value range produced by parsing
// the segmented registration form. Exists only at compile time as an
// intermediary for constructing the final `sequence_enum_names_spec`.
template<std::integral U, size_t NameCount, size_t SegCount>
struct segmented_names {
  std::array<cstring_view, NameCount> packed{};
  std::array<enum_segment<U>, SegCount> segments{};
  U min{};
  U max{};
};

// Create a length-preserving copy of the registration with both delimiters
// ('|' and ',') turned into terminators, so each field becomes an
// independently null-terminated span in place. The packed names are views into
// this buffer, which is held as a template parameter and so has static
// storage.
template<strings::fixed_string names>
[[nodiscard]] consteval auto make_nulled() {
  return strings::fixed_replaced<strings::fixed_replaced<names, '|', '\0'>(),
      ',', '\0'>();
}

// Parse the segmented registration string into packed names plus a segment
// table.
//
// Each '|'-delimited segment is `start,name,name,...`: the first comma-field
// is the absolute start value and the rest are names, taken verbatim. An empty
// field is an empty name. Segments must ascend, and each segment's start must
// exceed the previous segment's last value by at least four, leaving at least
// three unnamed values between them; runs any closer are rejected and should be
// merged into one segment, using empty names for the gap, since a one- or
// two-value gap costs about as much as the empty names while a new segment also
// pays for its start value and the '|'. `min` and `max` are derived from them.
// The packed names are views into `Nulled`.
template<strings::fixed_string names, std::integral U, size_t NameCount,
    size_t SegCount, strings::fixed_string Nulled = make_nulled<names>()>
[[nodiscard]] consteval auto parse_segmented_names() {
  segmented_names<U, NameCount, SegCount> name_segments{};
  constexpr auto whole = names.view();
  const char* base = Nulled.data();
  size_t packed_ndx{};
  size_t pos{};

  for (size_t segment_ndx = 0; segment_ndx != SegCount; ++segment_ndx) {
    const size_t seg_end = std::min(whole.find('|', pos), whole.size());
    const size_t comma = whole.find(',', pos);
    if (comma > seg_end) throw std::invalid_argument("invalid structure");
    const auto maybe_start =
        strings::parse_num<U>(whole.substr(pos, comma - pos));
    if (!maybe_start) throw std::invalid_argument("invalid segment start");

    const U start = *maybe_start;
    if (segment_ndx != 0) {
      if (start <= name_segments.max)
        throw std::invalid_argument("segments must ascend");
      if (start - name_segments.max <= 3)
        throw std::invalid_argument("segments too close");
    }

    size_t length{};
    size_t field = comma + 1;
    while (true) {
      size_t field_end = std::min(whole.find(',', field), seg_end);
      name_segments.packed[packed_ndx++] =
          cstring_view{base + field, field_end - field + 1};
      ++length;
      if (field_end == seg_end) break;
      field = field_end + 1;
    }

    name_segments.segments[segment_ndx] = enum_segment<U>{start, length};
    if (segment_ndx == 0) name_segments.min = start;
    name_segments.max = static_cast<U>(start + static_cast<U>(length) - 1);
    pos = seg_end + 1;
  }
  return name_segments;
}

// Specialization of `sequence_enum_spec`, adding names for the values, stored
// as a packed array plus a segment table.
template<ScopedEnum E, E maxseq = E{}, E minseq = E{},
    wrapclip wrapseq = wrapclip{}, size_t NameCount = 0, size_t SegCount = 1>
struct sequence_enum_names_spec
    : public sequence_enum_spec<E, maxseq, minseq, wrapseq> {
  using U = std::underlying_type_t<E>;

  constexpr sequence_enum_names_spec(
      std::array<cstring_view, NameCount> packed_names,
      std::array<enum_segment<U>, SegCount> segment_table)
      : names(packed_names), segments(segment_table) {}

  // Append the name for `v`, falling back to its numeric text when `v` is out
  // of range or has an empty name.
  [[nodiscard]] constexpr auto& append(AppendTarget auto& target, E v) const {
    const auto name = find_name_by_enum(v);
    if (name.empty())
      strings::append_num(target, as_underlying(v));
    else
      strings::appender{target}.append(name);
    return target;
  }

  // The forward half of the name<->enum mapping: a fast lookup returning the
  // canonical name for `v`, or an empty view if `v` is out of range or has an
  // empty name.
  //
  // Walks the segments in order (O(1) for a dense enum's single segment),
  // stopping once `v` falls below a segment's start, and indexes into the
  // packed names.
  [[nodiscard]] constexpr cstring_view find_name_by_enum(E v) const noexcept {
    const auto n = as_underlying(v);
    size_t offset = 0;
    for (const auto& seg : segments) {
      if (n < seg.start) break;
      const auto intra = static_cast<size_t>(n - seg.start);
      if (intra < seg.length) {
        const auto name = names[offset + intra];
        return name.size() ? name : cstring_view{};
      }
      offset += seg.length;
    }
    return {};
  }

  // The reverse half of the name<->enum mapping: a linear search of the packed
  // names, segment by segment, for an exact match, recovering the enum as the
  // segment's start plus the intra-segment index.
  //
  // Names only: unlike `lookup`, it never interprets numeric text, so it stays
  // usable in a constant expression. The inter-segment gaps never appear in
  // the scan.
  [[nodiscard]] constexpr std::optional<E> find_enum_by_name(
      std::string_view sv) const noexcept {
    if (sv.empty()) return {};
    size_t offset = 0;
    for (const auto& seg : segments) {
      for (size_t ndx = 0; ndx != seg.length; ++ndx)
        if (names[offset + ndx] == sv)
          return make<E>(static_cast<U>(seg.start + static_cast<U>(ndx)));
      offset += seg.length;
    }
    return {};
  }

  // Map a candidate name to the registry's own copy of it, or an empty view if
  // it is not a registered name. Canonicalizes (interns) so the result points
  // into `names` rather than at the caller's buffer; emptiness doubles as the
  // "not a name" test. Names only, so it stays usable from `consteval`.
  [[nodiscard]] constexpr cstring_view intern_name(
      std::string_view sv) const noexcept {
    auto e = find_enum_by_name(sv);
    return e ? find_name_by_enum(*e) : cstring_view{};
  }

  // Look up value from string view, parsing numeric text if necessary.
  [[nodiscard]] bool lookup(E& v, std::string_view sv) const {
    if (sv.empty()) return false;
    if (registry::details::lookup_helper(v, sv)) {
      if constexpr (seq_actually_wrap_v<E>)
        if (v != make<E>(*v)) return false;
      return true;
    }
    auto e = find_enum_by_name(sv);
    if (!e) return false;
    v = *e;
    return true;
  }

  const std::array<cstring_view, NameCount> names;
  const std::array<enum_segment<U>, SegCount> segments;
};
} // namespace details

#pragma endregion
#pragma region make spec

// Make an `enum_spec_v` from a list of names, marking `E` as a sequence enum.
//
// The list must be a string literal. Names are taken verbatim: no whitespace
// is trimmed, and there are no special placeholder characters. An empty field
// has no name, and so does any value outside the named range; in both cases
// the numeric value is printed instead.
//
// In the dense form, the names are a single comma-delimited list indexed from
// `minseq`. The `maxseq` is calculated from the number of names, but if the
// minimum value isn't 0, you must set `minseq` to it. This is the right choice
// even if there are a few gaps.
//
// In the segmented form, a '|' separates two or more segments (with no leading
// or trailing '|'), listed in ascending order, with each segment's start at
// least four greater than the previous segment's last value (closer runs are a
// compile-time error: merge them into one segment, using empty names for the
// gap). Each segment's first comma-field is its
// absolute start value and the rest are names. This compacts a sparse enum:
// the gaps between segments cost nothing, instead of an empty name per skipped
// value. `min` and `max` are derived from the segments, so `minseq` is not
// passed.
//
// Set `wrapseq` to `wrapclip::limit` to enable wrapping.
template<ScopedEnum E, strings::fixed_string names,
    wrapclip wrapseq = wrapclip{}, E minseq = E{},
    strings::fixed_string Nulled = details::make_nulled<names>()>
[[nodiscard]] consteval auto make_sequence_enum_spec() {
  using U = std::underlying_type_t<E>;
  constexpr auto whole = names.view();
  if constexpr (whole.find('|') == whole.npos) {
    // Dense form: a single segment spanning the whole name list. Each
    // comma-field is a verbatim name, viewed in `Nulled`.
    constexpr auto name_count =
        static_cast<size_t>(std::ranges::count(whole, ',')) + 1;
    std::array<cstring_view, name_count> packed{};
    const char* base = Nulled.data();
    size_t name_ndx = 0;
    size_t field = 0;
    while (true) {
      const size_t field_end = std::min(whole.find(',', field), whole.size());
      packed[name_ndx++] = cstring_view{base + field, field_end - field + 1};
      if (field_end == whole.size()) break;
      field = field_end + 1;
    }
    constexpr auto maxseq = E{as_underlying(minseq) + name_count - 1};
    constexpr std::array<details::enum_segment<U>, 1> segments{
        {{as_underlying(minseq), name_count}}};
    return details::sequence_enum_names_spec<E, maxseq, minseq, wrapseq,
        name_count, 1>{packed, segments};
  } else {
    // Segmented form: min, max, and the segment table are derived from
    // parsing.
    static_assert(minseq == E{},
        "segmented registration derives min from the segments; do not pass "
        "minseq");
    constexpr auto seg_count =
        static_cast<size_t>(std::ranges::count(whole, '|')) + 1;
    constexpr auto name_count =
        static_cast<size_t>(std::ranges::count(whole, ','));
    constexpr auto parsed = details::parse_segmented_names<names, U,
        name_count, seg_count, Nulled>();
    return details::sequence_enum_names_spec<E, E{parsed.max}, E{parsed.min},
        wrapseq, name_count, seg_count>{parsed.packed, parsed.segments};
  }
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
