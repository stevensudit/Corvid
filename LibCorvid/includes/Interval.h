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
#include "BitmaskEnum.h"
#include "SequenceEnum.h"
#include "ConcatJoin.h"

namespace corvid {
inline namespace intervals {

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
// is a half-open interval, [begin, end), but it's exposed as a closed
// interval, [min, max], in keeping with the vector fiction.
//
// Note: Iterating over an interval that ends at the maximum value for the
// underlying type doesn't work, and can't work unless we use a prohibitvely
// expensive implementation. See note below.
//
// It's perfectly fine for an interval to be empty, but if the range is
// reversed, then it's invalid.
//
// `T` is the type used for the actual representation, while `V` is the type
// used for the presentation value. So, for example, `T` might be the
// underlying type of an enum while `V` is the enum itself.
//
// Either can be signed or unsigned, although mixing those might lead to
// confusion.
template<typename T = int64_t, typename V = T>
class interval: public std::pair<T, T> {
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
    // `T` can be a larger type while `T` remains as the presentation value
    // type.
    //
    // But if you're already using the largest integer (across platforms,
    // that's `int64_t`), then the only thing left is for the iterator to
    // maintain a separate flag to set when it goes out of range, and this
    // basically means testing for max before incrementing, and then including
    // that flag in an inequality test.
    //
    // In principle, this could be made efficient by relying on the CPU to set
    // a carry flag, which could then be added to the overflow, and then
    // testing the overflow only as a tie-breaker. This is essentially how
    // integers larger than what fits in a CPU register are implemented.
    // However, how to accomplish this reliably and efficently in
    // cross-platform C++ is non-obvious. And, really, the right answer in such
    // cases is to use a closed interval in the first place.
  public:
    using value_type = V;
    using difference_type = std::ptrdiff_t;

    constexpr interval_iterator(T t) : t_(t) {}

    constexpr const V operator*() const noexcept { return static_cast<V>(t_); }

    constexpr bool operator==(const interval_iterator& r) const noexcept {
      return t_ == r.t_;
    }
    constexpr bool operator!=(const interval_iterator& r) const noexcept {
      return t_ != r.t_;
    }

    constexpr auto& operator++() noexcept {
      ++t_;
      return *this;
    }

    constexpr auto operator++(int) noexcept {
      auto o = *this;
      operator++();
      return o;
    }

    constexpr auto& operator--() noexcept {
      --t_;
      return *this;
    }

    constexpr auto operator--(int) noexcept {
      auto o = *this;
      operator--();
      return o;
    }

  private:
    T t_;
  };

public:
  //
  // Types
  //

  using parent = std::pair<T, T>;
  using value_type = V;
  using representation_type = T;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using iterator = interval_iterator;
  using const_iterator = interval_iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  //
  // Construction
  //

  constexpr interval() noexcept : parent(T{}, T{}){};
  constexpr interval(const interval&) noexcept = default;
  explicit constexpr interval(T val) noexcept : interval(val, val) {}
  constexpr interval(T min_val, T max_val) noexcept
      : parent(min_val, max_val + 1) {
    assert(!invalid());
  }

  constexpr interval& operator=(const interval&) = default;

  //
  // Iterators
  //

  // Note: When `invalid`, so are the iterators.

  constexpr iterator begin() const noexcept {
    assert(!invalid());
    return b();
  }
  constexpr iterator end() const noexcept {
    assert(!invalid());
    return e();
  }
  constexpr const_iterator cbegin() const noexcept {
    assert(!invalid());
    return b();
  }
  constexpr const_iterator cend() const noexcept {
    assert(!invalid());
    return e();
  }

  constexpr auto rbegin() const noexcept { return reverse_iterator(end()); }
  constexpr auto rend() const noexcept { return reverse_iterator(begin()); }
  constexpr auto crbegin() const noexcept {
    return const_reverse_iterator(cend());
  }
  constexpr auto crend() const noexcept {
    return const_reverse_iterator(cbegin());
  }

  //
  // Size
  //

  // Note: When `invalid`, then `size` underflows by going negative.

  constexpr bool empty() const noexcept { return b() >= e(); }
  constexpr bool invalid() const noexcept { return b() > e(); }

  constexpr size_type size() const noexcept {
    assert(!invalid());
    return e() - b();
  }
  constexpr size_type max_size() const noexcept {
    return static_cast<size_type>(std::numeric_limits<T>::max());
  }

  // Access front and back value.
  //
  // Invalid when `empty`.
  constexpr V front() const noexcept {
    assert(!empty());
    return static_cast<V>(b());
  }
  constexpr V back() const noexcept {
    assert(!empty());
    return static_cast<V>(e() - 1);
  }

  // Resize by moving `back`.
  constexpr void resize(size_type len) noexcept { e() = b() + len; }

  // Insert value, expanding `front` and `back` as needed.
  //
  // Returns whether the value was inserted. Cleanly handles all cases
  // involving `empty` and `invalid` intervals.
  //
  // Note that, if the interval was `invalid`, the insertion will fail and it
  // will stay `invalid`.
  constexpr bool insert(V v) noexcept {
    bool inserted{};
    auto t = as_t(v);
    if (!invalid()) {
      if (empty()) {
        min(t).max(t);
        inserted = true;
      } else {
        if (t < min()) {
          min(t);
          inserted = true;
        }
        if (t > max()) {
          max(t);
          inserted = true;
        }
      }
    }
    return inserted;
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
    auto t = as_t(v);
    if (t <= max()) return false;
    max(t);
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
    auto t = as_t(v);
    if (t >= min()) return false;
    min(t);
    return true;
  }

  // Pop values from back.
  //
  // Only valid when `!empty()` and `size() >= len`.
  constexpr void pop_back(size_type len = 1) noexcept {
    assert(!empty() && size() >= len);
    e() -= V(len);
  }

  // Pop values from front.
  //
  // Only valid when `!empty()` and `size() >= len`.
  constexpr void pop_front(size_type len = 1) noexcept {
    assert(!empty() && size() >= len);
    b() += V(len);
  }

  //
  // Min/max
  //

  // The `min` and `max` methods provide lower-level access to the interval
  // values, both because they return T instead of V, and also because they
  // give direct control over the both.

  constexpr T min() const noexcept { return static_cast<V>(b()); }
  constexpr interval& min(T t) noexcept {
    b() = t;
    return *this;
  }

  constexpr T max() const noexcept { return static_cast<V>(e() - 1); }
  constexpr interval& max(T t) noexcept {
    e() = t + 1;
    return *this;
  }

private:
  constexpr parent& p() { return static_cast<parent&>(*this); }
  constexpr const parent& p() const {
    return static_cast<const parent&>(*this);
  }
  constexpr T& b() noexcept { return p().first; }
  constexpr const T& b() const noexcept { return p().first; }

  constexpr T& e() noexcept { return p().second; }
  constexpr const T& e() const noexcept { return p().second; }

  static constexpr T as_t(V v) { return static_cast<T>(v); }
  static constexpr V as_v(T t) { return static_cast<V>(t); }
};

// Make interval for full range of sequence, for use with ranged-for.
//
// Note: See comments in about the need to use a larger underlying type in some
// cases, as indicated by the static_assert.
template<typename E, typename V = as_underlying_t<E>,
    sequence::enable_if_sequence_0<E> = 0>
constexpr auto make_interval() noexcept {
  static_assert(
      sequence::details::seq_max_num_v<E> != std::numeric_limits<V>::max(),
      "Specify U as something larger than the underlying type");
  return interval<V, E>{sequence::details::seq_min_num_v<E>,
      V(sequence::details::seq_max_num_v<E>)};
}

// Make interval for full range of bitmask, for use with ranged-for.
//
// Note: See comments about the need to use a larger underlying type in some
// cases, as indicated by the static_assert.
template<typename E, typename V = as_underlying_t<E>,
    bitmask::enable_if_bitmask_0<E> = 0>
constexpr auto make_interval() noexcept {
  static_assert(*max_value<E>() != std::numeric_limits<V>::max(),
      "Specify U as something larger than the underlying type");
  return interval<V, E>{0, V(*max_value<E>())};
}

// Whether `T` is an `interval`.
template<typename T>
constexpr bool is_interval_v = is_specialization_of_v<T, interval>;

} // namespace intervals
} // namespace corvid

namespace corvid::strings {
inline namespace appending {

// Append one variant to `target`, as its current type, joining with `delim`.
template<typename A, typename T, typename U, enable_if_append_target_0<A> = 0>
constexpr auto& append(A& target, const corvid::interval<T, U>& part) {
  if (!part.empty()) {
    append(target, part.front());
    append(target, ":");
    append(target, part.back());
    return target;
  }
}

#if 0
// Append one variant to `target`, as its current type, joining with `delim`.
template<auto opt = strings::join_opt::braced, char open = '[',
    char close = ']', typename T, typename A,
    enable_if_appendable_0<A, is_interval_v<T>> = 0>
constexpr auto& append_join_with(A& target, delim d, const T& part) {
  return target;
}

#endif
} // namespace appending
} // namespace corvid::strings

// Tuple specializations.
//
// Note: Inheritance doesn't apply to partial specialization, so we have to
// provide these manually.
namespace std {

template<std::size_t I, class T, class U>
constexpr const U get(const corvid::interval<U, T>&& i) noexcept {
  if constexpr (I == 0)
    return i.front();
  else
    return i.back();
}

template<class T, class U>
struct tuple_size<corvid::interval<T, U>>
    : std::integral_constant<std::size_t, 2> {};

} // namespace std

//
// TODO
//

// TODO: Write `append_join` but not `append`.

// Consider replace `make_interval` here with a scheme similar to
// `make_enum_printer`, so that these can be decentralized and the
// specializations can return a pair instead of an interval. Of course, we'd
// also have to pass V, not just T.
