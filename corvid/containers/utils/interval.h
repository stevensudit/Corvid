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
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <utility>

#include "../core/containers_shared.h"
#include "../../strings/delimiting.h"
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
// Internally it holds a `std::pair{begin, end}` as a half-open interval,
// [begin, end), but exposes a closed interval, [min, max], in keeping with the
// vector fiction.
//
// Note: Iterating over an interval that ends at the maximum value for the
// underlying type doesn't work, and can't work unless we use a prohibitively
// expensive implementation. See note below.
//
// It's perfectly fine for an interval to be empty, but if the range is
// reversed, then it's invalid.
//
// `U` is the type used for the underlying representation, while `V` is the
// type used for the presentation value. So, for example, `U` might be the
// underlying type of an enum while `V` is the enum itself. Or `U` could be
// larger than `V` to allow full-range iteration.
//
// Either can be signed or unsigned, although mixing those would probably be a
// bad idea.
template<typename V = int64_t, typename U = as_underlying_t<V>>
requires Integer<V> || StdEnum<V>
class interval {
public:
#pragma region interval_iterator

  class interval_iterator {
    // A note on iterator size:
    //
    // In a half-open interval, there's no way to represent the maximum value
    // of an integer range. This means that a loop over an interval would have
    // to stop one short, otherwise it won't start at all. This is true
    // regardless of whether it's signed or unsigned.
    //
    // The only solution is for the iterator to be able to represent a state
    // past the maximum value. The easy way is to use an underlying
    // representation that's larger. That's why `V` can be overridden; so that
    // `U` can be a larger type while `U` remains as the presentation value
    // type.
    //
    // But if you're already using the largest integer (which, across all
    // platforms, is `int64_t`), then the only thing left is for the iterator
    // to maintain a separate flag to set when it goes out of range, and this
    // basically means testing for max before incrementing, and then including
    // that flag in an inequality test.
    //
    // In principle, this could be made efficient by relying on the CPU to set
    // a carry flag, which could then be added to the overflow, and then
    // testing the overflow only as a tie-breaker. This is essentially how
    // integers larger than what fits in a CPU register are implemented.
    // However, how to accomplish this reliably and efficiently in
    // cross-platform C++ is non-obvious. And, really, the right answer in such
    // cases is to use a closed interval in the first place.
  public:
    using value_type = V;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using pointer = V*;
    using reference = V&;

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
    U u_;
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

#pragma endregion
#pragma region Construction

  constexpr interval() noexcept : pair_{U{}, U{}} {}
  constexpr interval(const interval&) noexcept = default;
  explicit constexpr interval(V val) noexcept : interval{val, val} {}
  constexpr interval(V min_val, V max_val) noexcept
      : pair_{as_u(min_val), as_u(max_val) + 1} {
    assert(!invalid());
  }

  constexpr interval& operator=(const interval&) = default;

  void clear() { *this = interval{}; }

  constexpr void swap(interval& other) noexcept { pair_.swap(other.pair_); }
  friend constexpr void swap(interval& l, interval& r) noexcept { l.swap(r); }

  // Compare by the underlying half-open [begin, end) representation. This also
  // generates `operator==`.
  [[nodiscard]] constexpr auto operator<=>(
      const interval&) const noexcept = default;

  // Convert to a copy of the underlying half-open [begin, end) pair.
  [[nodiscard]] constexpr operator raw_pair() const noexcept { return pair_; }

#pragma endregion
#pragma region Iterators

  // Note: When `invalid`, so are the iterators, but `empty` is fine.

  [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
    assert(!invalid());
    return b();
  }
  [[nodiscard]] constexpr const_iterator cend() const noexcept {
    assert(!invalid());
    return e();
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

  // Note: When `invalid`, then `size` underflows by going negative. Therefore,
  // while an invalid instance counts as empty, its size is undefined.

  [[nodiscard]] constexpr bool empty() const noexcept { return b() >= e(); }
  [[nodiscard]] constexpr bool invalid() const noexcept { return b() > e(); }

  [[nodiscard]] constexpr size_type size() const noexcept {
    assert(!invalid());
    return e() - b();
  }

  [[nodiscard]] constexpr static size_type max_size() noexcept {
    return static_cast<size_type>(std::numeric_limits<U>::max());
  }

  // Access front and back value.
  //
  // Invalid when `empty`.
  [[nodiscard]] constexpr V front() const noexcept {
    assert(!empty());
    return as_v(b());
  }
  [[nodiscard]] constexpr V back() const noexcept {
    assert(!empty());
    return as_v(e() - 1);
  }

  // Resize by moving `back`.
  constexpr void resize(size_type len) noexcept {
    e() = b() + static_cast<U>(len);
  }

  // Insert value, expanding `front` and `back` as needed.
  //
  // Returns whether the value was inserted. Cleanly handles all cases
  // involving `empty` and `invalid` intervals.
  //
  // Note that, if the interval was `invalid`, the insertion will fail and it
  // will stay `invalid`.
  constexpr bool insert(V v) noexcept {
    auto u = as_u(v);
    if (invalid()) return false;
    if (empty()) return min(u).max(u).ok();

    if (u < min()) return min(u).ok();
    if (u > max()) return max(u).ok();
    return false;
  }

  // Push value to the back.
  //
  // Only inserts if greater than `back`. Returns whether the value was
  // inserted.
  //
  // Do not use on an `empty` interval or if value might be below `front`. This
  // is a highly optimized function that does not consider, much less alter,
  // the value of `front` in any way. It does not check for `empty` or even
  // `invalid`. If any of these are possible, call `insert` instead.
  constexpr bool push_back(V v) noexcept {
    assert(!empty());
    auto u = as_u(v);
    if (u <= max()) return false;
    return max(u).ok();
  }

  // Push value to the front.
  //
  // Only inserts if less than `front`. Returns whether the value was inserted.
  //
  // Do not use on an `empty` interval or if value might be above `back`. This
  // is a highly optimized function that does not consider, much less alter,
  // the value of `back` in any way. It does not check for `empty` or even
  // `invalid`. If any of these are possible, call `insert` instead.
  constexpr bool push_front(V v) noexcept {
    assert(!empty());
    auto u = as_u(v);
    if (u >= min()) return false;
    return min(u).ok();
  }

  // Pop values from back.
  //
  // Only valid when `!empty() && size() >= len`.
  constexpr void pop_back(size_type len = 1) noexcept {
    assert(!empty() && size() >= len);
    e() -= as_u(len);
  }

  // Pop values from front.
  //
  // Only valid when `!empty() && size() >= len`.
  constexpr void pop_front(size_type len = 1) noexcept {
    assert(!empty() && size() >= len);
    b() += as_u(len);
  }

#pragma endregion
#pragma region Min/max

  // The `min` and `max` methods provide lower-level access to the interval
  // values, both because they return U instead of V, and also because they
  // give direct control over the both.

  [[nodiscard]] constexpr U min() const noexcept { return b(); }
  constexpr interval& min(U u) noexcept {
    b() = u;
    return *this;
  }

  [[nodiscard]] constexpr U max() const noexcept { return e() - 1; }
  constexpr interval& max(U u) noexcept {
    // Ensure u+1 won't overflow (u must be less than the max representable U)
    assert(u < std::numeric_limits<U>::max());
    e() = u + 1;
    return *this;
  }

#pragma endregion
#pragma region Implementation
private:
  [[nodiscard]] constexpr bool ok() const noexcept {
    assert(!invalid());
    return true;
  }
  [[nodiscard]] constexpr auto& p(this auto& self) noexcept {
    return self.pair_;
  }

  [[nodiscard]] constexpr auto& b(this auto& self) noexcept {
    return self.pair_.first;
  }
  [[nodiscard]] constexpr auto& e(this auto& self) noexcept {
    return self.pair_.second;
  }

  [[nodiscard]] static constexpr U as_u(V v) { return static_cast<U>(v); }
  [[nodiscard]] static constexpr V as_v(U u) { return static_cast<V>(u); }

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
// names. An empty interval is `[]` and an invalid one (reversed bounds) is
// `[invalid]`. Debug `{:?}` shows the raw half-open storage in the underlying
// integer representation, `[begin, end)`; there an empty interval reads as
// `[n, n)` and an invalid one as reversed bounds. The only accepted specs are
// the empty spec and `?`.
template<typename V, typename U>
struct std::formatter<corvid::interval<V, U>, char> {
  constexpr auto parse(auto& ctx) {
    auto it = ctx.begin();
    if (it != ctx.end() && *it == '?') {
      debug_ = true;
      ++it;
    }
    if (it != ctx.end() && *it != '}')
      throw std::format_error("interval format spec accepts only '?'");
    return it;
  }

  template<typename FormatContext>
  auto format(const corvid::interval<V, U>& iv, FormatContext& ctx) const {
    auto out = ctx.out();
    if (debug_) {
      // Raw half-open [begin, end) in the underlying integers. The unary plus
      // promotes a char-like `U` so it prints as a number, not a character.
      const std::pair<U, U> p = iv;
      return std::format_to(out, "[{}, {})", +p.first, +p.second);
    }
    if (iv.invalid()) return std::format_to(out, "[invalid]");
    if (iv.empty()) return std::format_to(out, "[]");
    return std::format_to(out, "[{}, {}]", iv.front(), iv.back());
  }

private:
  bool debug_{false};
};

#pragma endregion
