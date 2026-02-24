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
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace corvid { inline namespace container {
inline namespace fixed_bitsets {

// Fixed-size bitset backed by `std::array<word_t, word_count_v>`.
//
// This is a maximalist version of `std::bitset`.
//
// `N_BITS` must be a positive multiple of 8. The default is 64.
//
// The internal word type is selected automatically as the largest power-of-2
// unsigned type whose bit-width evenly divides N_BITS, unless overridden by
// `FORCED_WORD`:
//
//   N_BITS divisible by 64 -> uint64_t  (FORCED_WORD == 0)
//   N_BITS divisible by 32 -> uint32_t  (FORCED_WORD == 0)
//   N_BITS divisible by 16 -> uint16_t  (FORCED_WORD == 0)
//   otherwise              -> uint8_t   (FORCED_WORD == 0)
//
// `FORCED_WORD` (8, 16, 32, or 64) overrides the auto-selected word size.
// When the word is larger than N_BITS (e.g. N_BITS=8 with FORCED_WORD=64),
// the high bits of the single word are padding and are always kept zero.
// When the word is smaller than the auto choice (e.g. N_BITS=64 with
// FORCED_WORD=8), more words are used at the cost of loop overhead.
//
// Examples: fixed_bitset<64>                 -> array<uint64_t, 1>
//           fixed_bitset<24>                 -> array<uint8_t,  3>
//           fixed_bitset<96>                 -> array<uint32_t, 3>
//           fixed_bitset<64, size_t, void, 8>  -> array<uint8_t,  8>
//           fixed_bitset<8,  size_t, void, 64> -> array<uint64_t, 1> (56 pad)
//
// `POS` is the position type used at the public interface. It defaults to
// `size_t`, but may be specialized on a scoped enum (e.g. `store_id_t`) to
// prevent accidental mixing of unrelated index spaces.
//
// `TAG` is an optional tag type for disambiguating multiple
// structurally-identical `fixed_bitset` types. It has no effect on the
// implementation, except to provide type safety and prevent mixing.
//
// So long as `FORCED_WORD=0`, `sizeof(fixed_bitset<...>)` is `N_BITS / 8`,
// meaning that there is no overhead.
template<size_t N_BITS = 64, typename POS = size_t, typename TAG = void,
    size_t FORCED_WORD = 0>
class fixed_bitset {
public:
  static constexpr size_t bit_count_v = N_BITS;
  static constexpr size_t forced_word_v = FORCED_WORD;

  using pos_t = POS;
  using tag_t = TAG;

  static_assert(bit_count_v > 0 && bit_count_v % 8 == 0,
      "N_BITS must be a positive multiple of 8");
  static_assert(
      forced_word_v == 0 || forced_word_v == 8 || forced_word_v == 16 ||
          forced_word_v == 32 || forced_word_v == 64,
      "FORCED_WORD must be 0 (auto), 8, 16, 32, or 64");

private:
  // Effective word bit-width: `forced_word_v` if nonzero, otherwise the
  // largest power of 2 that evenly divides `bit_count_v`, from {64, 32, 16,
  // 8}.
  static constexpr size_t word_bits_v =
      forced_word_v != 0      ? forced_word_v
      : bit_count_v % 64 == 0 ? 64u
      : bit_count_v % 32 == 0 ? 32u
      : bit_count_v % 16 == 0
          ? 16u
          : 8u;

public:
  // Word type: `FORCED_WORD` overrides auto-selection (0 = auto). Auto selects
  // the largest power-of-2 unsigned type whose width evenly divides
  // `bit_count_v`.
  using word_t = std::conditional_t<(word_bits_v == 64), uint64_t,
      std::conditional_t<(word_bits_v == 32), uint32_t,
          std::conditional_t<(word_bits_v == 16), uint16_t, uint8_t>>>;
  static constexpr size_t bits_per_word_v = sizeof(word_t) * 8;

  // Number of words needed to hold all `bit_count_v` bits (ceiling division).
  static constexpr size_t word_count_v =
      (bit_count_v + bits_per_word_v - 1) / bits_per_word_v;

  // Read-only iterator over bit positions that are set.
  //
  // Yields each set bit's position as `pos_t` in ascending order.
  // This is efficient, as each advance costs one bit-clear plus (amortized)
  // one word-skip. It would not be faster to provide a `for_each`.
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

    // Position of the current set bit as `pos_t`.
    [[nodiscard]] constexpr pos_t operator*() const noexcept {
      return as_pos(
          (word_ndx_ * bits_per_word_v) +
          static_cast<size_t>(std::countr_zero(current_word_)));
    }

    // Advance to the next set bit.
    constexpr iterator& operator++() noexcept {
      // Clear the lowest set bit in the current word.
      current_word_ &= current_word_ - 1;
      if (current_word_ == 0) {
        ++word_ndx_;
        advance_to_next_set_word();
      }
      return *this;
    }

    // Post-increment; returns a copy before advancing.
    constexpr iterator operator++(int) noexcept {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    // Equal when at the same position.
    [[nodiscard]] constexpr bool operator==(
        const iterator& other) const noexcept {
      assert(bs_ == other.bs_ || bs_ == nullptr || other.bs_ == nullptr);
      return word_ndx_ == other.word_ndx_ &&
             current_word_ == other.current_word_;
    }

  private:
    const fixed_bitset* bs_{nullptr};
    size_t word_ndx_{word_count_v}; // `word_count_v` signals "end"
    word_t current_word_{0};

    // Scan forward from `word_ndx_` until a non-zero word or all words are
    // exhausted.
    constexpr void advance_to_next_set_word() noexcept {
      while (word_ndx_ < word_count_v && bs_->words_[word_ndx_] == 0)
        ++word_ndx_;
      if (word_ndx_ < word_count_v) current_word_ = bs_->words_[word_ndx_];
    }
  };

  using const_iterator = iterator;

  // Akin to `std::bitset<N>::reference`.
  // This is a proxy object that behaves like a reference to a single bit,
  // allowing read and write access. It is returned by the mutable
  // `operator[]`. This is pure syntactic sugar that the compiler optimizes
  // away.
  class reference {
  public:
    constexpr reference(const reference&) noexcept = default;

    // Set the referenced bit to `value`.
    constexpr reference& operator=(bool value) noexcept {
      bitset_.set_unchecked(ndx_, value);
      return *this;
    }

    // Assign by reading `rhs` as `bool`; self-assignment is a harmless no-op.
    // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
    constexpr reference& operator=(const reference& rhs) noexcept {
      return *this = static_cast<bool>(rhs);
    }

    // Read the referenced bit.
    [[nodiscard]] constexpr operator bool() const noexcept {
      return bitset_.test_unchecked(ndx_);
    }

    // Complement of the referenced bit; does not modify it.
    [[nodiscard]] constexpr bool operator~() const noexcept {
      return !static_cast<bool>(*this);
    }

    // Flip the referenced bit in-place.
    constexpr reference& flip() noexcept {
      bitset_.flip_unchecked(ndx_);
      return *this;
    }

  private:
    fixed_bitset& bitset_;
    const size_t ndx_{0};

    // Only constructed by `fixed_bitset::operator[]`.
    explicit constexpr reference(fixed_bitset& bitset, size_t ndx) noexcept
        : bitset_(bitset), ndx_(ndx) {}

    friend class fixed_bitset;
  };

  // Construction.

  constexpr fixed_bitset() = default;
  constexpr fixed_bitset(const fixed_bitset&) = default;
  constexpr fixed_bitset(fixed_bitset&&) noexcept = default;

  constexpr fixed_bitset& operator=(const fixed_bitset&) = default;
  constexpr fixed_bitset& operator=(fixed_bitset&&) noexcept = default;

  // TODO: Consider adding fancier constructors that take a number or a string.

  // Comparison.

  // Equality comparison.
  [[nodiscard]] constexpr bool operator==(
      const fixed_bitset&) const noexcept = default;

  // Lexicographic over words_[]: word 0 dominates word 1, etc. Within a
  // word, a higher-index bit produces a larger word value and therefore a
  // greater bitset.
  [[nodiscard]] constexpr auto operator<=>(
      const fixed_bitset&) const noexcept = default;

  // Element access.

  // Read-only bit access. Does not throw. Fastest path.
  [[nodiscard]] constexpr bool operator[](pos_t pos) const noexcept {
    return test_unchecked(as_sz(pos));
  }

  // Mutable proxy access via subscript. Does not throw. Fastest path, even for
  // reads.
  [[nodiscard]] constexpr reference operator[](pos_t pos) noexcept {
    return reference{*this, as_sz(pos)};
  }

  // True if bit at `pos` is set. Throws `std::out_of_range`. The fastest path
  // is `operator[]`.
  [[nodiscard]] constexpr bool test(pos_t pos) const {
    return test_unchecked(checked_index(pos));
  }

  // Bounds-checked bit access; throws `std::out_of_range` if `pos >= N_BITS`.
  [[nodiscard]] constexpr bool at(pos_t pos) const { return test(pos); }

  // True if all bits are set.
  [[nodiscard]] constexpr bool all() const noexcept {
    for (size_t ndx = 0; ndx + 1 < word_count_v; ++ndx)
      if (words_[ndx] != all_ones_v) return false;
    return words_[word_count_v - 1] == top_word_mask_;
  }

  // True if at least one bit is set.
  [[nodiscard]] constexpr bool any() const noexcept { return !none(); }

  // True if no bits are set.
  [[nodiscard]] constexpr bool none() const noexcept {
    for (auto w : words_)
      if (w) return false;
    return true;
  }

  // Number of set bits.
  [[nodiscard]] constexpr size_t count() const noexcept {
    size_t cnt{};
    for (auto w : words_) cnt += static_cast<size_t>(std::popcount(w));
    return cnt;
  }

  // Capacity.

  // Number of bit positions (N_BITS).
  [[nodiscard]] constexpr size_t size() const noexcept { return bit_count_v; }

  // Modifiers.

  // AND each word with rhs in place.
  constexpr fixed_bitset& operator&=(const fixed_bitset& rhs) noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx)
      words_[ndx] &= rhs.words_[ndx];
    return *this;
  }

  // OR each word with rhs in place.
  constexpr fixed_bitset& operator|=(const fixed_bitset& rhs) noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx)
      words_[ndx] |= rhs.words_[ndx];
    return *this;
  }

  // XOR each word with rhs in place.
  constexpr fixed_bitset& operator^=(const fixed_bitset& rhs) noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx)
      words_[ndx] ^= rhs.words_[ndx];
    return *this;
  }

  // Return a new bitset with all bits complemented.
  [[nodiscard]] constexpr fixed_bitset operator~() const noexcept {
    fixed_bitset out{*this};
    out.flip();
    return out;
  }

  // Bitwise AND.
  [[nodiscard]] friend constexpr fixed_bitset
  operator&(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs &= rhs;
  }

  // Bitwise OR.
  [[nodiscard]] friend constexpr fixed_bitset
  operator|(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs |= rhs;
  }

  // Bitwise XOR.
  [[nodiscard]] friend constexpr fixed_bitset
  operator^(fixed_bitset lhs, const fixed_bitset& rhs) noexcept {
    return lhs ^= rhs;
  }

  // Left shift.
  [[nodiscard]] friend constexpr fixed_bitset
  operator<<(fixed_bitset lhs, size_t shift) noexcept {
    return lhs <<= shift;
  }

  // Shift bits toward higher indices; vacated low bits become zero.
  constexpr fixed_bitset& operator<<=(size_t shift) noexcept {
    if (shift == 0) return *this;
    if (shift >= bit_count_v) {
      reset();
      return *this;
    }

    const size_t word_shift = shift / bits_per_word_v;
    const size_t bit_shift = shift % bits_per_word_v;

    if (bit_shift == 0) {
      for (size_t ndx = word_count_v; ndx-- > word_shift;)
        words_[ndx] = words_[ndx - word_shift];
    } else {
      const size_t rshift = bits_per_word_v - bit_shift;
      for (size_t ndx = word_count_v; ndx-- > word_shift + 1;)
        words_[ndx] = static_cast<word_t>(
            (words_[ndx - word_shift] << bit_shift) |
            (words_[ndx - word_shift - 1] >> rshift));
      words_[word_shift] = static_cast<word_t>(words_[0] << bit_shift);
    }

    for (size_t ndx = 0; ndx < word_shift; ++ndx) words_[ndx] = 0;
    if constexpr (top_padding_bits_ != 0)
      words_[word_count_v - 1] &= top_word_mask_;
    return *this;
  }

  // Right shift.
  [[nodiscard]] friend constexpr fixed_bitset
  operator>>(fixed_bitset lhs, size_t shift) noexcept {
    return lhs >>= shift;
  }

  // Shift bits toward lower indices; vacated high bits become zero.
  constexpr fixed_bitset& operator>>=(size_t shift) noexcept {
    if (shift == 0) return *this;
    if (shift >= bit_count_v) {
      reset();
      return *this;
    }

    const size_t word_shift = shift / bits_per_word_v;
    const size_t bit_shift = shift % bits_per_word_v;
    const size_t limit = word_count_v - word_shift;

    if (bit_shift == 0) {
      for (size_t ndx = 0; ndx < limit; ++ndx)
        words_[ndx] = words_[ndx + word_shift];
    } else {
      const size_t lshift = bits_per_word_v - bit_shift;
      for (size_t ndx = 0; ndx + 1 < limit; ++ndx)
        words_[ndx] = static_cast<word_t>(
            (words_[ndx + word_shift] >> bit_shift) |
            (words_[ndx + word_shift + 1] << lshift));
      words_[limit - 1] =
          static_cast<word_t>(words_[word_count_v - 1] >> bit_shift);
    }

    for (size_t ndx = limit; ndx < word_count_v; ++ndx) words_[ndx] = 0;
    return *this;
  }

  // Rotate bits left by shift positions.
  constexpr fixed_bitset& rotl(size_t shift) noexcept {
    shift %= bit_count_v;
    if (shift == 0) return *this;

    // When the top word has padding, the multi-word rotation logic would
    // incorrectly treat padding bits as data. Decompose into two shifts
    // instead (`operator<<=` already masks out the padding).
    if constexpr (top_padding_bits_ != 0) {
      *this = (*this << shift) | (*this >> (bit_count_v - shift));
      return *this;
    }

    const size_t word_shift = shift / bits_per_word_v;
    const size_t bit_shift = shift % bits_per_word_v;
    std::array<word_t, word_count_v> out{};

    if (bit_shift == 0) {
      size_t src = word_count_v - word_shift;
      for (size_t ndx = 0; ndx < word_count_v; ++ndx) {
        out[ndx] = words_[src];
        if (++src == word_count_v) src = 0;
      }
    } else {
      const size_t rshift = bits_per_word_v - bit_shift;
      size_t src = word_count_v - word_shift;
      size_t prev = (src == 0) ? (word_count_v - 1) : (src - 1);

      for (size_t ndx = 0; ndx < word_count_v; ++ndx) {
        out[ndx] = static_cast<word_t>(
            (words_[src] << bit_shift) | (words_[prev] >> rshift));
        prev = src;
        if (++src == word_count_v) src = 0;
      }
    }

    words_ = out;
    return *this;
  }

  // Rotate bits right by shift positions.
  constexpr fixed_bitset& rotr(size_t shift) noexcept {
    shift %= bit_count_v;
    if (shift == 0) return *this;

    if constexpr (top_padding_bits_ != 0) {
      *this = (*this >> shift) | (*this << (bit_count_v - shift));
      return *this;
    }

    const size_t word_shift = shift / bits_per_word_v;
    const size_t bit_shift = shift % bits_per_word_v;
    std::array<word_t, word_count_v> out{};

    if (bit_shift == 0) {
      size_t src = word_shift;
      for (size_t ndx = 0; ndx < word_count_v; ++ndx) {
        out[ndx] = words_[src];
        if (++src == word_count_v) src = 0;
      }
    } else {
      const size_t lshift = bits_per_word_v - bit_shift;
      size_t src = word_shift;
      size_t next = src + 1;
      if (next == word_count_v) next = 0;

      for (size_t ndx = 0; ndx < word_count_v; ++ndx) {
        out[ndx] = static_cast<word_t>(
            (words_[src] >> bit_shift) | (words_[next] << lshift));
        src = next;
        if (++next == word_count_v) next = 0;
      }
    }

    words_ = out;
    return *this;
  }

  // Non-member left rotation.
  [[nodiscard]] friend constexpr fixed_bitset
  rotl(fixed_bitset lhs, size_t shift) noexcept {
    return lhs.rotl(shift);
  }

  // Non-member right rotation.
  [[nodiscard]] friend constexpr fixed_bitset
  rotr(fixed_bitset lhs, size_t shift) noexcept {
    return lhs.rotr(shift);
  }

  // Set all bits.
  constexpr void set() noexcept {
    words_.fill(all_ones_v);
    if constexpr (top_padding_bits_ != 0)
      words_[word_count_v - 1] &= top_word_mask_;
  }

  // Set or clear bit at `pos`. Throws `std::out_of_range`. The fastest path is
  // `operator[]`.
  constexpr void set(pos_t pos, bool value = true) {
    set_unchecked(checked_index(pos), value);
  }

  // Clear all bits.
  constexpr void reset() noexcept { words_.fill(0); }

  // Clear bit at `pos`. Throws `std::out_of_range`. The fastest path is
  // `operator[]`.
  constexpr fixed_bitset& reset(pos_t pos) {
    return set_unchecked(checked_index(pos), false);
  }

  // Flip all bits.
  constexpr fixed_bitset& flip() noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx) words_[ndx] ^= all_ones_v;
    if constexpr (top_padding_bits_ != 0)
      words_[word_count_v - 1] &= top_word_mask_;
    return *this;
  }

  // Flip bit at pos. Throws `std::out_of_range`. The fastest path is
  // `operator[]`.
  constexpr fixed_bitset& flip(pos_t pos) {
    return flip_unchecked(checked_index(pos));
  }

  // Extended queries.

  // Number of consecutive zero bits starting at bit 0.
  [[nodiscard]] constexpr pos_t countr_zero() const noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx) {
      const auto w = words_[ndx];
      if (w)
        return as_pos(
            (ndx * bits_per_word_v) +
            static_cast<size_t>(std::countr_zero(w)));
    }
    return as_pos(bit_count_v);
  }

  // Number of consecutive one bits starting at bit 0.
  [[nodiscard]] constexpr pos_t countr_one() const noexcept {
    for (size_t ndx = 0; ndx < word_count_v; ++ndx) {
      const auto w = words_[ndx];
      if (w != all_ones_v)
        return as_pos(
            (ndx * bits_per_word_v) +
            static_cast<size_t>(std::countr_zero(static_cast<word_t>(~w))));
    }
    return as_pos(bit_count_v);
  }

  // Number of consecutive zero bits from the most significant bit downward.
  [[nodiscard]] constexpr pos_t countl_zero() const noexcept {
    // Shift the top word left by top_padding_bits_ to discard padding before
    // counting. When top_padding_bits_ == 0 the shift is a no-op.
    const auto top = words_[word_count_v - 1];
    if (top)
      return as_pos(static_cast<size_t>(
          std::countl_zero(static_cast<word_t>(top << top_padding_bits_))));
    size_t accum = top_word_valid_bits_;
    for (size_t ndx = word_count_v - 1; ndx > 0; --ndx) {
      const auto w = words_[ndx - 1];
      if (w) return as_pos(accum + static_cast<size_t>(std::countl_zero(w)));
      accum += bits_per_word_v;
    }
    return as_pos(bit_count_v);
  }

  // Number of consecutive one bits from the most significant bit downward.
  [[nodiscard]] constexpr pos_t countl_one() const noexcept {
    // Compare the top word against top_word_mask_ (not all_ones_v) so that
    // padding bits do not affect the result.
    const auto top = words_[word_count_v - 1];
    if (top != top_word_mask_)
      return as_pos(static_cast<size_t>(std::countl_zero(static_cast<word_t>(
          static_cast<word_t>(~top) << top_padding_bits_))));
    size_t accum = top_word_valid_bits_;
    for (size_t ndx = word_count_v - 1; ndx > 0; --ndx) {
      const auto w = words_[ndx - 1];
      if (w != all_ones_v)
        return as_pos(
            accum +
            static_cast<size_t>(std::countl_zero(static_cast<word_t>(~w))));
      accum += bits_per_word_v;
    }
    return as_pos(bit_count_v);
  }

  // True if exactly one bit is set.
  [[nodiscard]] constexpr bool has_single_bit() const noexcept {
    bool seen_one{};
    for (auto w : words_) {
      if (w == 0) continue;
      if (seen_one || !std::has_single_bit(w)) return false;
      seen_one = true;
    }
    return seen_one;
  }

  // Position of the highest set bit plus one; zero if all bits are clear.
  [[nodiscard]] constexpr pos_t bit_width() const noexcept {
    return as_pos(bit_count_v - as_sz(countl_zero()));
  }

  // Iteration. All const.

  // Iterator to the first set bit, or end() if none are set.
  [[nodiscard]] constexpr iterator begin() const noexcept {
    return iterator{*this};
  }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
    return begin();
  }
  // Past-the-end sentinel.
  [[nodiscard]] constexpr iterator end() const noexcept { return iterator{}; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept {
    return end();
  }

  // Underlying word array, by reference.
  [[nodiscard]] constexpr decltype(auto) array(this auto& self) noexcept {
    return (self.words_);
  }

private:
  std::array<word_t, word_count_v> words_{};

  // Valid bits in the topmost word. Less than `bits_per_word_v` only when
  // `FORCED_WORD` causes `N_BITS` to not be a multiple of `bits_per_word_v`.
  static constexpr size_t top_word_valid_bits_ =
      (bit_count_v % bits_per_word_v != 0)
          ? (bit_count_v % bits_per_word_v)
          : bits_per_word_v;

  // Padding bits at the top of the last word (bits must always be kept zero).
  static constexpr size_t top_padding_bits_ =
      bits_per_word_v - top_word_valid_bits_;

  // Mask covering only the valid bits of the top word.
  static constexpr word_t top_word_mask_ =
      (top_padding_bits_ == 0)
          ? std::numeric_limits<word_t>::max()
          : static_cast<word_t>((word_t{1} << top_word_valid_bits_) - 1);

  // All-ones sentinel. Uses` numeric_limits` rather than `~word_t{0}` to avoid
  // integer-promotion bugs when `word_t` is narrower than `int`: `~uint8_t{0}`
  // promotes to `int{-1}`, which does not compare equal to `uint8_t{0xFF}`.
  static constexpr word_t all_ones_v = std::numeric_limits<word_t>::max();

  // Word index for the bit at `pos`.
  static constexpr size_t word_of(size_t pos) noexcept {
    return pos / bits_per_word_v;
  }

  // Single-bit mask for `pos` within its word.
  static constexpr word_t mask_of(size_t pos) noexcept {
    return static_cast<word_t>(word_t{1} << (pos % bits_per_word_v));
  }

  // Cast `pos_t` to `size_t` for indexing.
  static constexpr size_t as_sz(pos_t pos) noexcept {
    assert(static_cast<size_t>(pos) < bit_count_v);
    return static_cast<size_t>(pos);
  }

  // Cast `size_t` back to `pos_t`.
  static constexpr pos_t as_pos(size_t ndx) noexcept {
    assert(ndx <= bit_count_v);
    return static_cast<pos_t>(ndx);
  }

  // Test the bit at pre-validated `ndx` (no bounds check).
  [[nodiscard]] constexpr bool test_unchecked(size_t ndx) const noexcept {
    return (words_[word_of(ndx)] & mask_of(ndx)) != 0;
  }

  // Set or clear the bit at pre-validated `ndx` (no bounds check).
  constexpr fixed_bitset& set_unchecked(size_t ndx, bool value) noexcept {
    const auto w = word_of(ndx);
    const auto m = mask_of(ndx);
    words_[w] = (words_[w] & ~m) | (static_cast<word_t>(-value) & m);
    return *this;
  }

  // Flip the bit at pre-validated `ndx` (no bounds check).
  constexpr fixed_bitset& flip_unchecked(size_t ndx) noexcept {
    words_[word_of(ndx)] ^= mask_of(ndx);
    return *this;
  }

  // Return bit index as size_t; throw if out of range.
  static constexpr size_t checked_index(pos_t pos) {
    const auto ndx = as_sz(pos);
    if (ndx >= bit_count_v) [[unlikely]]
      throw std::out_of_range{"fixed_bitset: pos out of range"};
    return ndx;
  }
};

}}} // namespace corvid::container::fixed_bitsets
