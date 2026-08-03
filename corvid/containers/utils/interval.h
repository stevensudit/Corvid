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
#define NOMINMAX

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

#include "../../meta/concepts.h"
#include "../../meta/enums.h"
#include "../../meta/traits.h"
#include "../../enums/bitmask_enum.h"
#include "../../enums/sequence_enum.h"

namespace corvid { inline namespace intervals {

#pragma region interval

// Represent a closed interval of integers.
//
// Fulfills the requirements for a Container.
// https://en.cppreference.com/w/cpp/named_req/Container.
//
// Conceptually, an `interval` container is like an ordered, solid vector of
// integers. In other words, an interval of [min,max] is like a `std::vector`
// with the minimum value as `front`, the maximum value as `back`, and all the
// contiguous values (implicitly) in between.
//
// It also has some things in common with `std::string_view`, in that you can
// move the front and back around but can't modify anything being referred to.
// As a result, all iterators are const.
//
// Internally it stores the closed `{min, max}` pair directly, so an interval
// can reach from edge to edge of `U`, signed or unsigned, without overflow:
// expanding a bound is pure min/max comparison, never arithmetic.
//
// A reversed pair, with `min` above `max`, reads as empty. The canonical
// empty, produced by default construction and `clear`, stores the extremes
// reversed, which is the identity of min/max accumulation.
//
// Two derived operations still need arithmetic and carry caveats: `size` of
// the full span of a 64-bit `U` is one past what `size_type` can represent,
// so it wraps to 0, and iteration must represent one past `back`, so an
// interval whose `back` is the top of `U` can be stored but not iterated. The
// workaround is to write a conventional `for` loop from `min` to `max`. For
// types smaller than 64 bits, you can gain iteration headroom by specializing
// `U` wider than `V`.
//
// `U` is the type used for the underlying representation, while `V` is the
// type used for the presentation value. So, for example, `U` might be the
// underlying type of an enum while `V` is the enum itself. Or `U` could be
// larger than `V` to allow full-range iteration.
//
// `U` must match the signedness of `V` (for an enum, of its underlying type)
// and be at least as wide, so that every `V` value is representable in `U`
// with order preserved.
template<typename V = int64_t, typename U = as_underlying_t<V>>
requires Integer<V> || StdEnum<V>
class interval {
public:
#pragma region interval_iterator

  class interval_iterator {
  public:
    using value_type = V;
    using difference_type = std::ptrdiff_t;
    // `operator*` returns a prvalue, so this models the C++20 bidirectional
    // concept via `iterator_concept`, while the Cpp17 category honestly caps
    // at input (the classic requirements demand a true reference beyond
    // that). Same shape as `std::ranges::iota_view`'s iterator.
    using iterator_concept = std::bidirectional_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using reference = V;
    using pointer = void;

    constexpr interval_iterator() noexcept = default;
    constexpr interval_iterator(U u) : u_{u} {}

    constexpr V operator*() const noexcept { return as_v(u_); }

    constexpr auto operator<=>(
        const interval_iterator& r) const noexcept = default;

    constexpr auto& operator++() noexcept {
      ++u_;
      return *this;
    }

    constexpr auto operator++(int) noexcept {
      auto o = *this;
      operator++();
      return o;
    }

    constexpr auto& operator--() noexcept {
      --u_;
      return *this;
    }

    constexpr auto operator--(int) noexcept {
      auto o = *this;
      operator--();
      return o;
    }

  private:
    U u_{};
  };

#pragma endregion
#pragma region Types

public:
  using raw_pair = std::pair<U, U>;
  using value_type = V;
  using representation_type = U;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using reference = U&;
  using const_reference = const U&;
  using iterator = interval_iterator;
  using const_iterator = interval_iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
  // Unsigned mirror of `U`, for the derived arithmetic in `size`, iteration,
  // and popping: modular unsigned math sidesteps signed overflow.
  using uu_t = std::make_unsigned_t<U>;

  static constexpr auto u_min = std::numeric_limits<U>::min();
  static constexpr auto u_max = std::numeric_limits<U>::max();
  static constexpr auto uu_max = std::numeric_limits<uu_t>::max();
  static constexpr auto uu_size_max = static_cast<size_type>(uu_max);
  static constexpr auto size_max = std::numeric_limits<size_type>::max();

  // `U` must cover `V`'s domain with order preserved: same signedness (for an
  // enum `V`, of its underlying type) and at least as wide.
  static_assert(
      (std::is_signed_v<U> == std::is_signed_v<as_underlying_t<V>>) &&
          (sizeof(U) >= sizeof(as_underlying_t<V>)),
      "U must match V's signedness and be at least as wide");

#pragma endregion
#pragma region Construction
public:
  // Construct with the canonical empty, storing the extremes reversed.
  constexpr interval() noexcept : pair_{u_max, u_min} {}

  constexpr interval(const interval&) noexcept = default;

  // Construct from a single value, which becomes both `front` and `back`.
  explicit constexpr interval(V val) noexcept : interval{val, val} {}

  // Construct from `min_val` and `max_val`, where `min_val > max_val` leaves
  // it empty. Prefer the default constructor for a canonical empty.
  constexpr interval(V min_val, V max_val) noexcept
      : pair_{as_u(min_val), as_u(max_val)} {}

  constexpr interval& operator=(const interval&) = default;

  constexpr void clear() noexcept { *this = interval{}; }

  constexpr void swap(interval& other) noexcept { pair_.swap(other.pair_); }
  friend constexpr void swap(interval& l, interval& r) noexcept { l.swap(r); }

  // Compare by the stored closed {min, max} representation. This also
  // generates `operator==`.
  //
  // Note: empties with differing representations compare unequal; every
  // API-produced empty is canonical.
  [[nodiscard]] constexpr auto operator<=>(
      const interval&) const noexcept = default;

  // Convert to a copy of the stored closed {min, max} pair.
  [[nodiscard]] constexpr operator raw_pair() const noexcept { return pair_; }

#pragma endregion
#pragma region Iterators

  // Note: When the underlying value of `max` is the top of `U`, the interval
  // is still valid but cannot be iterated. See class documentation for
  // details.

  [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
    if (empty()) return {};
    return {lo()};
  }
  [[nodiscard]] constexpr const_iterator cend() const noexcept {
    if (empty()) return {};
    assert(hi() != u_max);
    return {static_cast<U>(as_uu(hi()) + 1U)};
  }
  [[nodiscard]] constexpr iterator begin() const noexcept { return cbegin(); }
  [[nodiscard]] constexpr iterator end() const noexcept { return cend(); }

  [[nodiscard]] constexpr auto crbegin() const noexcept {
    return std::make_reverse_iterator(cend());
  }
  [[nodiscard]] constexpr auto crend() const noexcept {
    return std::make_reverse_iterator(cbegin());
  }
  [[nodiscard]] constexpr auto rbegin() const noexcept { return crbegin(); }
  [[nodiscard]] constexpr auto rend() const noexcept { return crend(); }

#pragma endregion
#pragma region Size

  // A reversed interval counts as empty.
  [[nodiscard]] constexpr bool empty() const noexcept { return lo() > hi(); }

  // Size is well-defined for every representable interval except the full span
  // of a 64-bit `U`, whose actual count is one past what `size_type` can hold,
  // and therefore wraps to 0. This means that such a value has a `size` of 0,
  // but is not `empty`.
  [[nodiscard]] constexpr size_type size() const noexcept {
    if (empty()) return 0UZ;
    return static_cast<size_type>(
               static_cast<uu_t>(as_uu(hi()) - as_uu(lo()))) +
           1UZ;
  }

  // The count of the full span, saturated to `size_type` for a 64-bit `U`.
  [[nodiscard]] constexpr static size_type max_size() noexcept {
    return (uu_size_max == size_max) ? uu_size_max : uu_size_max + 1UZ;
  }

  // Access front and back value, using the `std::string_view` idiom.
  //
  // Invalid when `empty`.
  [[nodiscard]] constexpr V front() const noexcept {
    assert(!empty());
    return as_v(lo());
  }
  [[nodiscard]] constexpr V back() const noexcept {
    assert(!empty());
    return as_v(hi());
  }

  // Resize by moving `back`, anchored at `front`. A zero length clears; a
  // nonzero length requires a non-empty interval and a result that fits `U`.
  constexpr void resize(size_type len) noexcept {
    if (len == 0UZ) return clear();
    assert(!empty());
    assert(len - 1UZ <= static_cast<size_type>(uu_max - as_uu(lo())));
    hi() = static_cast<U>(as_uu(lo()) + static_cast<uu_t>(len - 1UZ));
  }

  // Insert value, expanding `front` and `back` as needed.
  //
  // Returns whether the interval grew; a value already contained leaves it
  // unchanged. Inserting into an empty interval, canonical or not, restarts
  // it at exactly that value.
  constexpr bool insert(V v) noexcept {
    const auto u = as_u(v);
    if (empty()) {
      lo() = hi() = u;
      return true;
    }
    if (u < lo()) {
      lo() = u;
      return true;
    }
    if (u > hi()) {
      hi() = u;
      return true;
    }
    return false;
  }

  // Push value to the back.
  //
  // Only inserts if greater than `back`. Returns whether the value was
  // inserted.
  //
  // Do not use on an `empty` interval or if value might be below `front`. This
  // is a highly optimized function that does not consider, much less alter,
  // the value of `front` in any way. If either is possible, call `insert`
  // instead.
  constexpr bool push_back(V v) noexcept {
    assert(!empty());
    const auto u = as_u(v);
    if (u <= hi()) return false;
    hi() = u;
    return true;
  }

  // Push value to the front.
  //
  // Only inserts if less than `front`. Returns whether the value was inserted.
  //
  // Do not use on an `empty` interval or if value might be above `back`. This
  // is a highly optimized function that does not consider, much less alter,
  // the value of `back` in any way. If either is possible, call `insert`
  // instead.
  constexpr bool push_front(V v) noexcept {
    assert(!empty());
    const auto u = as_u(v);
    if (u >= lo()) return false;
    lo() = u;
    return true;
  }

  // Pop values from back.
  //
  // Only valid when `!empty() && size() >= len`. Popping every value leaves
  // the canonical empty.
  constexpr void pop_back(size_type len = 1) noexcept {
    assert(!empty() && (size() >= len));
    if (len == size()) return clear();
    hi() = static_cast<U>(as_uu(hi()) - static_cast<uu_t>(len));
  }

  // Pop values from front.
  //
  // Only valid when `!empty() && size() >= len`. Popping every value leaves
  // the canonical empty.
  constexpr void pop_front(size_type len = 1) noexcept {
    assert(!empty() && (size() >= len));
    if (len == size()) return clear();
    lo() = static_cast<U>(as_uu(lo()) + static_cast<uu_t>(len));
  }

#pragma endregion
#pragma region Min/max

  // The bounds in their natural min/max wording, as opposed to the collection
  // wording of `front` and `back`. Unlike those, the getters tolerate
  // `empty`. The setters move one bound directly; setting `min` above `max`
  // yields a reversed pair, which reads as empty, so prefer `clear` for
  // deliberate emptying.

  [[nodiscard]] constexpr V min() const noexcept { return as_v(lo()); }
  constexpr interval& min(V v) noexcept {
    lo() = as_u(v);
    return *this;
  }

  [[nodiscard]] constexpr V max() const noexcept { return as_v(hi()); }
  constexpr interval& max(V v) noexcept {
    hi() = as_u(v);
    return *this;
  }

#pragma endregion
#pragma region Implementation
private:
  [[nodiscard]] constexpr auto& lo(this auto& self) noexcept {
    return self.pair_.first;
  }
  [[nodiscard]] constexpr auto& hi(this auto& self) noexcept {
    return self.pair_.second;
  }

  [[nodiscard]] static constexpr U as_u(V v) noexcept {
    return static_cast<U>(v);
  }
  [[nodiscard]] static constexpr V as_v(U u) noexcept {
    return static_cast<V>(u);
  }
  [[nodiscard]] static constexpr uu_t as_uu(U u) noexcept {
    return static_cast<uu_t>(u);
  }

#pragma endregion
#pragma region Data members

  raw_pair pair_{};

#pragma endregion
};

#pragma endregion
#pragma region make_interval

// Make interval for full range of sequence enum, for use with ranged-for.
//
// Note: See comments in about the need to use a larger underlying type in some
// cases, as indicated by the static_assert.
template<sequence::SequentialEnum E, typename U = as_underlying_t<E>>
[[nodiscard]] constexpr auto make_interval() noexcept {
  using namespace corvid::enums::sequence;
  static_assert(*seq_max_v<E> != std::numeric_limits<U>::max(),
      "Specify U as something larger than the underlying type");
  return interval<E, U>{seq_min_v<E>, seq_max_v<E>};
}

// Make interval for full range of bitmask, for use with ranged-for.
//
// Note: See comments about the need to use a larger underlying type in some
// cases, as indicated by the static_assert.
template<bitmask::BitmaskEnum E, typename U = as_underlying_t<E>>
[[nodiscard]] constexpr auto make_interval() noexcept {
  using namespace corvid::enums::bitmask;
  static_assert(*max_value<E>() != std::numeric_limits<U>::max(),
      "Specify U as something larger than the underlying type");
  return interval<E, U>{E{}, max_value<E>()};
}

#pragma endregion
#pragma region Interval

// `T` must be an an `interval`.
template<typename T>
concept Interval = is_specialization_of_v<T, interval>;

#pragma endregion
}} // namespace corvid::intervals

// NOLINTBEGIN(bugprone-std-namespace-modification).

#pragma region format_kind

// `interval` is iterable, so without this the std range formatter would
// enumerate every value instead of showing the bounds. Disabling its range
// format leaves the interval formatter below as the only match.
template<typename V, typename U>
constexpr std::range_format std::format_kind<corvid::interval<V, U>> =
    std::range_format::disabled;

#pragma endregion
#pragma region formatter

// Formatter for `interval`, narrow only: a numeric or enum range is a narrow
// concern, and going wide would mean parameterizing the brackets too.
//
// Regular `{}` shows the closed presentation interval, `[min, max]`, with the
// bounds formatted through `V`'s own formatter, so an enum interval prints its
// names. An empty interval is `[]`. Debug `{:?}` shows the raw closed storage
// pair in the underlying integer representation, `{min_u, max_u}`; there an
// empty interval reads as a reversed pair, with the canonical empty showing
// the extremes. The only accepted specs are the empty spec and `?`.
template<typename V, typename U>
struct std::formatter<corvid::interval<V, U>, char> {
  constexpr auto parse(auto& ctx) {
    auto it = ctx.begin();
    if (it != ctx.end() && *it == '?') {
      debug_ = true;
      ++it;
    }
    if (it != ctx.end() && *it != '}')
      throw std::format_error{"interval format spec accepts only '?'"};
    return it;
  }

  template<typename FormatContext>
  auto format(const corvid::interval<V, U>& iv, FormatContext& ctx) const {
    auto out = ctx.out();
    if (debug_) {
      // Raw closed {min, max} storage in the underlying integers. The unary
      // plus promotes a char-like `U` so it prints as a number, not a
      // character.
      const std::pair<U, U> p{iv};
      return std::format_to(out, "{{{}, {}}}", +p.first, +p.second);
    }
    if (iv.empty()) return std::format_to(out, "[]");
    return std::format_to(out, "[{}, {}]", iv.min(), iv.max());
  }

private:
  bool debug_{};
};

#pragma endregion
// NOLINTEND(bugprone-std-namespace-modification)
