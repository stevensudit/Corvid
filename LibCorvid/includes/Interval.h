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
#include "Meta.h"

namespace corvid::intervals {

//
// interval
//

// Represent an interval of integers.
//
// Canonically half-open `[b, e)`, but can also be exposed as closed `[f,l]`.
//
// Note: Iterating over an interval that ends at the maximum value for the
// underlying type doesn't work, and can't work unless we use a prohibitvely
// expensive implementation. See below.
//
// `T` is the type used for the representation, while `U` is the type used for
// presentation.
template<typename T, typename U = T>
class interval {
  // In a half-open interval, there's no way to represent the maximum value of
  // an integer range. This means that a loop over an interval would have to
  // stop one short, otherwise it won't start at all. This is true regardless
  // of whether it's signed or unsigned.
  //
  // The only solution is for the iterator to be able to represent a state past
  // the maximum value. The easy way is to use an underlying representation
  // that's larger. That's why `U` can be overridden; so that `T` can be the
  // larger representation while `U` remains as the presentation.
  //
  // But if you're already using the largest integer (across platforms, that's
  // `int64_t`), then the only thing left is for the iterator to maintain a
  // separate flag to set when it goes out of range, and this basically means
  // testing for max before incrementing and then including that flag in an
  // inequality test.
  //
  // In principle, this could be made efficient by relying on the CPU to set a
  // carry flag, which could then be added to the overflow, and then testing
  // the overflow only as a tie-breaker. This is essentially how integers
  // larger than what fits in a CPU register are implemented. However, how to
  // accomplish this reliably and efficently in cross-platform C++ is
  // non-obvious. And, really, the right answer in such cases is to use a
  // closed interval in the first place.
  class interval_iterator {
  public:
    constexpr interval_iterator(T t) : t_(t) {}

    U operator*() const { return static_cast<U>(t_); }

    bool operator==(const interval_iterator& r) { return t_ == r.t_; }
    bool operator!=(const interval_iterator& r) { return t_ != r.t_; }

    constexpr auto& operator++() noexcept {
      ++t_;
      return *this;
    }

    constexpr auto operator++(int) noexcept {
      auto o = *this;
      operator++();
      return o;
    }

  private:
    T t_;
  };

public:
  interval() = default;
  interval(const interval&) = default;
  interval(T b, T e) : begin_(b), end_(e) {}

  interval_iterator begin() const { return {begin_}; }
  interval_iterator end() const { return {end_}; }
  interval_iterator cbegin() const { return {begin_}; }
  interval_iterator cend() const { return {end_}; }

  U front() const { return static_cast<U>(begin_); }
  U back() const { return static_cast<U>(end_ - 1); }

private:
  T begin_ = T{};
  T end_ = T{};
};

} // namespace corvid::intervals

//
// TODO
//

// TODO: There is a great deal that can be added here. This should probably
// convert to a `std::pair`. It needs a `size`, some mutators, and perhaps some
// set ops. There are assorted open issues, such as how to handle negative
// intervals.

// TODO: Consider fulfilling the requirements of
// https://en.cppreference.com/w/cpp/named_req/Container.
