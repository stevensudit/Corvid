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
#include "containers_shared.h"

#include <array>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>

namespace corvid { inline namespace container {
inline namespace fixed_bitsets {

// Fixed-size bitset backed by std::array<uint64_t, N_BITS/64>.
//
// `N_BITS` must be a positive multiple of 64. The default is 64.
//
// `POS` is the position type used at the public interface. It defaults to
// `size_t`, but may be specialised on a scoped enum (e.g. `store_id_t`) to
// prevent accidental mixing of unrelated index spaces.
//
// `TAG` is an optional tag type for disambiguating multiple
// structurally-identical `fixed_bitset` types. It has no effect on the
// implementation, except to provide type safety and prevent mixing.
template<size_t N_BITS = 64, typename POS = size_t, typename TAG = void>
class fixed_bitset {
public:
  static constexpr size_t bit_count_v = N_BITS;
  static_assert(bit_count_v > 0 && bit_count_v % 64 == 0,
      "N_BITS must be a positive multiple of 64");

  using pos_t = POS;
  using tag_t = TAG;

  // Read-only iterator over set bit positions.
  //
  // Yields each set bit's position as `pos_t` in ascending order. Efficient:
  // each advance costs one bit-clear plus (amortized) one word-skip.
  class iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = pos_t;
    using difference_type = std::ptrdiff_t;
    using pointer = const pos_t*;
    using reference = const pos_t&;

    // End sentinel: `word_ndx_` == `word_count_v`, `bs_` == `nullptr`.
    constexpr iterator() noexcept = default;

    // Begin: scans forward from word 0 to find the first set bit.
    constexpr explicit iterator(const fixed_bitset& bs) noexcept
        : bs_(&bs), word_ndx_(0) {
      advance_to_next_set_word();
    }

    [[nodiscard]] constexpr pos_t operator*() const noexcept {
      return as_pos((word_ndx_ * 64) + std::countr_zero(current_word_));
    }

    constexpr iterator& operator++() noexcept {
      // Clear the lowest set bit in the current word.
      current_word_ &= current_word_ - 1;
      if (current_word_ == 0) {
        ++word_ndx_;
        advance_to_next_set_word();
      }
      return *this;
    }

    constexpr iterator operator++(int) noexcept {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    [[nodiscard]] constexpr bool operator==(
        const iterator& other) const noexcept {
      // Both exhausted (`word_ndx_` == `word_count_v`) regardless of `bs_`
      // pointer.
      if (word_ndx_ == word_count_v && other.word_ndx_ == word_count_v)
        return true;
      return word_ndx_ == other.word_ndx_ &&
             current_word_ == other.current_word_;
    }

  private:
    const fixed_bitset* bs_{nullptr};
    size_t word_ndx_{word_count_v}; // `word_count_v` signals "end"
    uint64_t current_word_{0};

    constexpr void advance_to_next_set_word() noexcept {
      while (word_ndx_ < word_count_v && bs_->words_[word_ndx_] == 0)
        ++word_ndx_;
      if (word_ndx_ < word_count_v) current_word_ = bs_->words_[word_ndx_];
    }
  };

  using const_iterator = iterator;

  // Construction

  constexpr fixed_bitset() = default;
  constexpr fixed_bitset(const fixed_bitset&) = default;
  constexpr fixed_bitset(fixed_bitset&&) noexcept = default;

  constexpr fixed_bitset& operator=(const fixed_bitset&) = default;
  constexpr fixed_bitset& operator=(fixed_bitset&&) noexcept = default;

  // Access.

  constexpr void set(pos_t pos) noexcept {
    words_[word_of(as_sz(pos))] |= mask_of(as_sz(pos));
  }

  constexpr void clear(pos_t pos) noexcept {
    words_[word_of(as_sz(pos))] &= ~mask_of(as_sz(pos));
  }

  [[nodiscard]] constexpr bool test(pos_t pos) const noexcept {
    return (words_[word_of(as_sz(pos))] & mask_of(as_sz(pos))) != 0;
  }

  [[nodiscard]] constexpr bool operator[](pos_t pos) const noexcept {
    return test(pos);
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return bit_count_v == 0;
  }
  [[nodiscard]] constexpr size_t size() const noexcept { return bit_count_v; }
  [[nodiscard]] constexpr size_t max_size() const noexcept {
    return bit_count_v;
  }

  [[nodiscard]] constexpr bool at(pos_t pos) const {
    const auto ndx = as_sz(pos);
    if (ndx >= bit_count_v) throw std::out_of_range{"fixed_bitset::at"};
    return (words_[word_of(ndx)] & mask_of(ndx)) != 0;
  }

  // Clear all bits.
  constexpr void reset() noexcept { words_.fill(0); }

  // Bitwise operators

  constexpr fixed_bitset& operator&=(const fixed_bitset& rhs) noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx)
      words_[ndx] &= rhs.words_[ndx];
    return *this;
  }

  constexpr fixed_bitset& operator|=(const fixed_bitset& rhs) noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx)
      words_[ndx] |= rhs.words_[ndx];
    return *this;
  }

  constexpr fixed_bitset& operator^=(const fixed_bitset& rhs) noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx)
      words_[ndx] ^= rhs.words_[ndx];
    return *this;
  }

  [[nodiscard]] friend constexpr fixed_bitset
  operator&(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs &= rhs;
  }

  [[nodiscard]] friend constexpr fixed_bitset
  operator|(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs |= rhs;
  }

  [[nodiscard]] friend constexpr fixed_bitset
  operator^(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs ^= rhs;
  }

  [[nodiscard]] constexpr fixed_bitset operator~() const noexcept {
    fixed_bitset out;
    for (size_t ndx = 0; ndx < word_count_v; ++ndx)
      out.words_[ndx] = ~words_[ndx];
    return out;
  }

  // Queries.

  [[nodiscard]] constexpr size_t popcount() const noexcept {
    size_t cnt = 0;
    for (auto w : words_) cnt += static_cast<size_t>(std::popcount(w));
    return cnt;
  }

  [[nodiscard]] constexpr bool none() const noexcept {
    for (auto w : words_)
      if (w) return false;
    return true;
  }

  [[nodiscard]] constexpr bool any() const noexcept { return !none(); }

  [[nodiscard]] constexpr bool all() const noexcept {
    for (auto w : words_)
      if (w != ~uint64_t{0}) return false;
    return true;
  }

  [[nodiscard]] constexpr pos_t countr_zero() const noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx) {
      const auto w = words_[ndx];
      if (w)
        return as_pos((ndx * 64) + static_cast<size_t>(std::countr_zero(w)));
    }
    return as_pos(bit_count_v);
  }

  [[nodiscard]] constexpr pos_t countr_one() const noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx) {
      const auto w = words_[ndx];
      if (w != ~uint64_t{0})
        return as_pos((ndx * 64) + static_cast<size_t>(std::countr_zero(~w)));
    }
    return as_pos(bit_count_v);
  }

  [[nodiscard]] constexpr pos_t countl_zero() const noexcept {
    for (size_t ndx = word_count_v; ndx > 0; --ndx) {
      const auto w = words_[ndx - 1];
      if (w)
        return as_pos(
            ((word_count_v - ndx) * 64) +
            static_cast<size_t>(std::countl_zero(w)));
    }
    return as_pos(bit_count_v);
  }

  [[nodiscard]] constexpr pos_t countl_one() const noexcept {
    for (size_t ndx = word_count_v; ndx > 0; --ndx) {
      const auto w = words_[ndx - 1];
      if (w != ~uint64_t{0})
        return as_pos(
            ((word_count_v - ndx) * 64) +
            static_cast<size_t>(std::countl_zero(~w)));
    }
    return as_pos(bit_count_v);
  }

  [[nodiscard]] constexpr bool has_single_bit() const noexcept {
    bool seen_one{};
    for (auto w : words_) {
      if (w == 0) continue;
      if (seen_one || !std::has_single_bit(w)) return false;
      seen_one = true;
    }
    return seen_one;
  }

  [[nodiscard]] constexpr pos_t bit_width() const noexcept {
    return as_pos(bit_count_v - as_sz(countl_zero()));
  }

  // Equality.

  [[nodiscard]] constexpr bool operator==(
      const fixed_bitset&) const noexcept = default;

  // Lexicographic over words_[]: word 0 (bits 0-63) dominates word 1
  // (bits 64-127), etc. Within a word, a higher-index bit produces a larger
  // uint64_t value and therefore a greater bitset.
  [[nodiscard]] constexpr auto operator<=>(
      const fixed_bitset&) const noexcept = default;

  // Iteration over set bit indices.

  [[nodiscard]] constexpr iterator begin() const noexcept {
    return iterator{*this};
  }
  [[nodiscard]] constexpr iterator end() const noexcept { return iterator{}; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
    return begin();
  }
  [[nodiscard]] constexpr const_iterator cend() const noexcept {
    return end();
  }

private:
  static constexpr size_t word_count_v = bit_count_v / 64;
  std::array<uint64_t, word_count_v> words_{};

  static constexpr size_t word_of(size_t pos) noexcept { return pos / 64; }
  static constexpr uint64_t mask_of(size_t pos) noexcept {
    return uint64_t{1} << (pos % 64);
  }

  static constexpr size_t as_sz(pos_t pos) noexcept {
    return static_cast<size_t>(pos);
  }
  static constexpr pos_t as_pos(size_t n) noexcept {
    return static_cast<pos_t>(n);
  }
};

}}} // namespace corvid::container::fixed_bitsets
