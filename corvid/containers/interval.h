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
#define NOMINMAX

#include "containers_shared.h"
#include "../strings.h"
#include "../enums.h"
#include <cassert>
#include <limits>
#include <iterator>

namespace corvid { inline namespace intervals {

//
// interval
//

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
// The class is implemented as a child of `std::pair(begin, end)`, where this
// holds a half-open interval, [begin, end), but is exposed as a closed
// interval, [min, max], in keeping with the vector fiction.
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
//
// TODO: Consider defaulting `U` to `int64_t` so that it works better when `V`
// is an enum. Alternately, create something like `as_underlying_t` that maps
// to a larger size when possible. Ideally, it would only use a larger size if
// necessary.
template<typename V = int64_t, typename U = as_underlying_t<V>>
requires Integer<V> || StdEnum<V>
class interval: public std::pair<U, U> {
public:
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
    //
    // TODO: It's tempting to consider storing a closed interval, because it
    // would mean we don't need to use a larger type, except in the iterator.
    // We could even handle values bigger than 64 bits as an iterator
    // specialization, although this is of questionable value. What argues
    // against this is the empty interval, which cannot be distinguished from
    // the full range. However, we could merge invalid and empty into a single
    // state, allowing us to represent empty as any inverted pair. This would
    // impede the efficiency of push_back and push_front, as they'd need to
    // touch both values. The workaround for that would be to require that the
    // instance be non-empty before pushing back. This means the caller would
    // have to insert before pushing back or front. We could also default to
    // [max,min] to make the push functions safer. This particular value could
    // also be checked for as "uninitialized", and asserted on. The question is
    // whether such an iterator would work properly in a ranged for.
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

public:
  //
  // Types
  //

  using parent = std::pair<U, U>;
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

  //
  // Construction
  //

  constexpr interval() noexcept : parent{U{}, U{}} {};
  constexpr interval(const interval&) noexcept = default;
  explicit constexpr interval(V val) noexcept : interval{val, val} {}
  constexpr interval(V min_val, V max_val) noexcept
      : parent{as_u(min_val), as_u(max_val) + 1} {
    assert(!invalid());
  }

  constexpr interval& operator=(const interval&) = default;

  void clear() { *this = interval{}; }

  // TODO: Consider writing comparison operators and make it work even if
  // their types are different. Equality is simple, but it's unclear how
  // inequality works when there's range overlap. We'd also want to consider
  // all empty equal to all empty, all invalid equal to all invalid. Or maybe
  // invalid, like null, is never equal. We'd also need to be careful when
  // comparing signed and unsigned.

  //
  // Iterators
  //

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

  //
  // Size
  //

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
    if (empty()) {
      min(u).max(u);
      return true;
    }
    if (u < min()) {
      min(u);
      return true;
    }
    if (u > max()) {
      max(u);
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
  // is a highly-optimized function that does not consider, much less alter,
  // the value of `front` in any way. It does not check for `empty` or even
  // `invalid`. If any of these are possible, call `insert` instead.
  constexpr bool push_back(V v) noexcept {
    assert(!empty());
    auto u = as_u(v);
    if (u <= max()) return false;
    max(u);
    return true;
  }

  // Push value to the front.
  //
  // Only inserts if less than `front`. Returns whether the value was inserted.
  //
  // Do not use on an `empty` interval or if value might be above `back`. This
  // is a highly-optimized function that does not consider, much less alter,
  // the value of `back` in any way. It does not check for `empty` or even
  // `invalid`. If any of these are possible, call `insert` instead.
  constexpr bool push_front(V v) noexcept {
    assert(!empty());
    auto u = as_u(v);
    if (u >= min()) return false;
    min(u);
    return true;
  }

  // Pop values from back.
  //
  // Only valid when `!empty()` and `size() >= len`.
  constexpr void pop_back(size_type len = 1) noexcept {
    assert(!empty() && size() >= len);
    e() -= as_u(len);
  }

  // Pop values from front.
  //
  // Only valid when `!empty()` and `size() >= len`.
  constexpr void pop_front(size_type len = 1) noexcept {
    assert(!empty() && size() >= len);
    b() += as_u(len);
  }

  //
  // Min/max
  //

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
    assert(static_cast<size_t>(u) < max_size());
    e() = u + 1;
    return *this;
  }

  // Append.
  // TODO: Consider printing as half-open interval unless JSON.
  // TODO: Consider printing enum string instead of integer.
  template<AppendTarget A>
  static auto& append_fn(A& target, const interval& i) {
    if (i.empty()) return target;
    return corvid::strings::append(target, i.min(), ", ", i.max());
  }

  template<auto opt = strings::join_opt::braced, char open = 0, char close = 0,
      AppendTarget A>
  static A&
  append_join_with_fn(A& target, strings::delim d, const interval& i) {
    using namespace corvid::strings;
    constexpr auto is_json = decode::json_v<opt>;
    constexpr char next_open = open ? open : (is_json ? '[' : 0);
    constexpr char next_close = close ? close : (is_json ? ']' : 0);
    if (i.empty()) {
      if constexpr (next_open && next_close)
        strings::append(strings::append(target, next_open), next_close);

      return target;
    }
    return corvid::strings::append_join_with<opt, next_open, next_close>(
        target, d, i.min(), i.max());
  }

private:
  [[nodiscard]] constexpr parent& p() { return static_cast<parent&>(*this); }
  [[nodiscard]] constexpr const parent& p() const {
    return static_cast<const parent&>(*this);
  }
  [[nodiscard]] constexpr U& b() noexcept { return p().first; }
  [[nodiscard]] constexpr const U& b() const noexcept { return p().first; }

  [[nodiscard]] constexpr U& e() noexcept { return p().second; }
  [[nodiscard]] constexpr const U& e() const noexcept { return p().second; }

  [[nodiscard]] static constexpr U as_u(V v) { return static_cast<U>(v); }
  [[nodiscard]] static constexpr V as_v(U u) { return static_cast<V>(u); }
};

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

// `T` must be an an `interval`.
template<typename T>
concept Interval = is_specialization_of_v<T, interval>;

}} // namespace corvid::intervals
namespace corvid::strings {
// Register appends.
template<corvid::AppendTarget A, typename V, typename U>
constexpr auto append_override_fn<A, interval<V, U>> =
    interval<V, U>::template append_fn<A>;

template<join_opt opt, char open, char close, corvid::AppendTarget A,
    typename V, typename U>
constexpr auto append_join_override_fn<opt, open, close, A, interval<V, U>> =
    interval<V, U>::template append_join_with_fn<opt, open, close, A>;
} // namespace corvid::strings
