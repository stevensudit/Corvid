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
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace corvid { inline namespace container {
inline namespace fixed_bitsets {

// Fixed-size bitset backed by std::array<uint64_t, N_BITS/64>.
//
// N_BITS must be a positive multiple of 64. The default is 64.
template<size_t N_BITS = 64>
class fixed_bitset {
public:
  static constexpr size_t bit_count_v = N_BITS;

  static_assert(bit_count_v > 0 && bit_count_v % 64 == 0,
      "N_BITS must be a positive multiple of 64");

  // Read-only iterator over set bit positions.
  //
  // Yields the index of each set bit in ascending order. Efficient: each
  // advance costs one bit-clear plus (amortised) one word-skip.
  class iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = const size_t*;
    using reference = size_t;

    // End sentinel: word_ndx_ == word_count_v, bs_ == nullptr.
    iterator() noexcept = default;

    // Begin: scans forward from word 0 to find the first set bit.
    explicit iterator(const fixed_bitset& bs) noexcept
        : bs_(&bs), word_ndx_(0) {
      advance_to_next_set_word();
    }

    [[nodiscard]] size_t operator*() const noexcept {
      return word_ndx_ * 64 + std::countr_zero(current_word_);
    }

    iterator& operator++() noexcept {
      // Clear the lowest set bit in the current word.
      current_word_ &= current_word_ - 1;
      if (current_word_ == 0) {
        ++word_ndx_;
        advance_to_next_set_word();
      }
      return *this;
    }

    iterator operator++(int) noexcept {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    [[nodiscard]] bool operator==(const iterator& other) const noexcept {
      // Both exhausted (word_ndx_ == word_count_v) regardless of bs_ pointer.
      if (word_ndx_ == word_count_v && other.word_ndx_ == word_count_v)
        return true;
      return word_ndx_ == other.word_ndx_ &&
             current_word_ == other.current_word_;
    }

  private:
    const fixed_bitset* bs_{nullptr};
    size_t word_ndx_{word_count_v}; // word_count_v signals "end"
    uint64_t current_word_{0};

    void advance_to_next_set_word() noexcept {
      while (word_ndx_ < word_count_v && bs_->words_[word_ndx_] == 0)
        ++word_ndx_;
      if (word_ndx_ < word_count_v) current_word_ = bs_->words_[word_ndx_];
    }
  };

  using const_iterator = iterator;

  // Construction

  fixed_bitset() = default;
  fixed_bitset(const fixed_bitset&) = default;
  fixed_bitset(fixed_bitset&&) noexcept = default;

  fixed_bitset& operator=(const fixed_bitset&) = default;
  fixed_bitset& operator=(fixed_bitset&&) noexcept = default;

  // Access.

  void set(size_t pos) noexcept { words_[word_of(pos)] |= mask_of(pos); }

  void clear(size_t pos) noexcept { words_[word_of(pos)] &= ~mask_of(pos); }

  [[nodiscard]] bool test(size_t pos) const noexcept {
    return (words_[word_of(pos)] & mask_of(pos)) != 0;
  }

  [[nodiscard]] bool operator[](size_t pos) const noexcept {
    return test(pos);
  }

  // Clear all bits.
  void reset() noexcept { words_.fill(0); }

  // Bitwise operators

  fixed_bitset& operator&=(const fixed_bitset& rhs) noexcept {
    for (size_t i = 0; i < word_count_v; ++i) words_[i] &= rhs.words_[i];
    return *this;
  }

  fixed_bitset& operator|=(const fixed_bitset& rhs) noexcept {
    for (size_t i = 0; i < word_count_v; ++i) words_[i] |= rhs.words_[i];
    return *this;
  }

  fixed_bitset& operator^=(const fixed_bitset& rhs) noexcept {
    for (size_t i = 0; i < word_count_v; ++i) words_[i] ^= rhs.words_[i];
    return *this;
  }

  [[nodiscard]] friend fixed_bitset
  operator&(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs &= rhs;
  }

  [[nodiscard]] friend fixed_bitset
  operator|(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs |= rhs;
  }

  [[nodiscard]] friend fixed_bitset
  operator^(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs ^= rhs;
  }

  [[nodiscard]] fixed_bitset operator~() const noexcept {
    fixed_bitset out;
    for (size_t i = 0; i < word_count_v; ++i) out.words_[i] = ~words_[i];
    return out;
  }

  // Queries.

  [[nodiscard]] size_t popcount() const noexcept {
    size_t cnt = 0;
    for (auto w : words_) cnt += static_cast<size_t>(std::popcount(w));
    return cnt;
  }

  [[nodiscard]] bool none() const noexcept {
    for (auto w : words_)
      if (w) return false;
    return true;
  }

  [[nodiscard]] bool any() const noexcept { return !none(); }

  [[nodiscard]] bool all() const noexcept {
    for (auto w : words_)
      if (w != ~uint64_t{0}) return false;
    return true;
  }

  [[nodiscard]] size_t countr_zero() const noexcept {
    for (size_t i = 0; i < word_count_v; ++i) {
      const auto w = words_[i];
      if (w) return i * 64 + static_cast<size_t>(std::countr_zero(w));
    }
    return bit_count_v;
  }

  [[nodiscard]] size_t countr_one() const noexcept {
    for (size_t i = 0; i < word_count_v; ++i) {
      const auto w = words_[i];
      if (w != ~uint64_t{0})
        return i * 64 + static_cast<size_t>(std::countr_zero(~w));
    }
    return bit_count_v;
  }

  [[nodiscard]] size_t countl_zero() const noexcept {
    for (size_t i = word_count_v; i > 0; --i) {
      const auto w = words_[i - 1];
      if (w)
        return (word_count_v - i) * 64 +
               static_cast<size_t>(std::countl_zero(w));
    }
    return bit_count_v;
  }

  [[nodiscard]] size_t countl_one() const noexcept {
    for (size_t i = word_count_v; i > 0; --i) {
      const auto w = words_[i - 1];
      if (w != ~uint64_t{0})
        return (word_count_v - i) * 64 +
               static_cast<size_t>(std::countl_zero(~w));
    }
    return bit_count_v;
  }

  [[nodiscard]] bool has_single_bit() const noexcept {
    bool seen_one = false;
    for (auto w : words_) {
      if (w == 0) continue;
      if (seen_one || !std::has_single_bit(w)) return false;
      seen_one = true;
    }
    return seen_one;
  }

  [[nodiscard]] size_t bit_width() const noexcept {
    return bit_count_v - countl_zero();
  }

  // Equality.

  [[nodiscard]] bool operator==(const fixed_bitset&) const noexcept = default;

  // Iteration over set bit indices.

  [[nodiscard]] iterator begin() const noexcept { return iterator{*this}; }
  [[nodiscard]] iterator end() const noexcept { return iterator{}; }
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
  static constexpr size_t word_count_v = bit_count_v / 64;
  std::array<uint64_t, word_count_v> words_{};

  static constexpr size_t word_of(size_t pos) noexcept { return pos / 64; }
  static constexpr uint64_t mask_of(size_t pos) noexcept {
    return uint64_t{1} << (pos % 64);
  }
};

}}} // namespace corvid::container::fixed_bitsets
