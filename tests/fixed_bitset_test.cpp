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

#include <cstddef>
#include <vector>

#include "../corvid/containers/fixed_bitset.h"
#include "minitest.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

void FixedBitset_Empty() {
  // Default construction: all bits clear.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_TRUE(b.none());
    EXPECT_FALSE(b.any());
    EXPECT_FALSE(b.all());
    EXPECT_EQ(b.popcount(), 0U);
  }

  // Iterating an empty bitset yields no elements.
  if (true) {
    fixed_bitset<64> b;
    std::vector<std::size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_TRUE(bits.empty());
  }
}

void FixedBitset_SetClearTest() {
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    EXPECT_TRUE(b.test(0));
    EXPECT_FALSE(b.test(1));
    EXPECT_FALSE(b.test(63));

    b.set(63);
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(63));

    b.clear(0);
    EXPECT_FALSE(b.test(0));
    EXPECT_TRUE(b.test(63));

    b.clear(63);
    EXPECT_FALSE(b.test(63));
    EXPECT_TRUE(b.none());
  }

  // Set/clear all 64 bits.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_TRUE(b.all());
    EXPECT_EQ(b.popcount(), 64U);

    for (std::size_t i = 0; i < 64; ++i) b.clear(i);
    EXPECT_TRUE(b.none());
  }
}

// operator[] is read-only and equivalent to test().
void FixedBitset_Subscript() {
  if (true) {
    fixed_bitset<64> b;
    b.set(7);
    b.set(42);

    EXPECT_TRUE(b[7]);
    EXPECT_TRUE(b[42]);
    EXPECT_FALSE(b[0]);
    EXPECT_FALSE(b[6]);
    EXPECT_FALSE(b[8]);
    EXPECT_FALSE(b[63]);

    // Must agree with test() at every position.
    for (std::size_t i = 0; i < 64; ++i) EXPECT_EQ(b[i], b.test(i));
  }
}

void FixedBitset_Popcount() {
  if (true) {
    fixed_bitset<64> b;
    b.set(3);
    b.set(7);
    b.set(62);
    EXPECT_EQ(b.popcount(), 3U);
    EXPECT_TRUE(b.any());
    EXPECT_FALSE(b.all());
    EXPECT_FALSE(b.none());
  }

  // all() only returns true when every bit is set.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 63; ++i) b.set(i);
    EXPECT_FALSE(b.all());
    b.set(63);
    EXPECT_TRUE(b.all());
    EXPECT_EQ(b.popcount(), 64U);
  }
}

void FixedBitset_Reset() {
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(31);
    b.set(63);
    EXPECT_EQ(b.popcount(), 3U);
    b.reset();
    EXPECT_TRUE(b.none());
    EXPECT_EQ(b.popcount(), 0U);
  }
}

void FixedBitset_Equality() {
  if (true) {
    fixed_bitset<64> a, b;
    EXPECT_EQ(a, b);

    a.set(5);
    EXPECT_NE(a, b);

    b.set(5);
    EXPECT_EQ(a, b);

    a.set(63);
    b.set(63);
    EXPECT_EQ(a, b);
  }
}

void FixedBitset_CopyMove() {
  // Copy construction produces an independent equal copy.
  if (true) {
    fixed_bitset<64> a;
    a.set(1);
    a.set(33);

    fixed_bitset<64> b{a};
    EXPECT_EQ(a, b);

    // Modifying the copy doesn't affect the original.
    b.set(50);
    EXPECT_FALSE(a.test(50));
    EXPECT_NE(a, b);
  }

  // Copy assignment.
  if (true) {
    fixed_bitset<64> a, b;
    a.set(10);
    b = a;
    EXPECT_EQ(a, b);
    b.clear(10);
    EXPECT_TRUE(a.test(10));
  }

  // Move construction leaves a usable value (for trivial types, same as copy).
  if (true) {
    fixed_bitset<64> a;
    a.set(5);
    fixed_bitset<64> b{std::move(a)};
    EXPECT_TRUE(b.test(5));
  }

  // Move assignment.
  if (true) {
    fixed_bitset<64> a, b;
    a.set(20);
    b = std::move(a);
    EXPECT_TRUE(b.test(20));
  }

  // bit_count_v is the right value.
  if (true) {
    EXPECT_EQ(fixed_bitset<64>::bit_count_v, 64U);
    EXPECT_EQ(fixed_bitset<128>::bit_count_v, 128U);
    EXPECT_EQ(fixed_bitset<192>::bit_count_v, 192U);
  }
}

void FixedBitset_BitwiseAnd() {
  if (true) {
    fixed_bitset<64> a, b;
    a.set(1);
    a.set(3);
    a.set(5);
    b.set(3);
    b.set(5);
    b.set(7);

    auto c = a & b;
    EXPECT_FALSE(c.test(1));
    EXPECT_TRUE(c.test(3));
    EXPECT_TRUE(c.test(5));
    EXPECT_FALSE(c.test(7));
    EXPECT_EQ(c.popcount(), 2U);
  }

  // AND with itself is idempotent.
  if (true) {
    fixed_bitset<64> a;
    a.set(10);
    a.set(20);
    auto b = a & a;
    EXPECT_EQ(a, b);
  }

  // AND with empty yields empty.
  if (true) {
    fixed_bitset<64> a, empty;
    a.set(7);
    auto result = a & empty;
    EXPECT_TRUE(result.none());
  }

  // operator&= in-place.
  if (true) {
    fixed_bitset<64> a, b;
    a.set(2);
    a.set(4);
    b.set(4);
    b.set(6);
    a &= b;
    EXPECT_FALSE(a.test(2));
    EXPECT_TRUE(a.test(4));
    EXPECT_FALSE(a.test(6));
  }
}

void FixedBitset_BitwiseOr() {
  if (true) {
    fixed_bitset<64> a, b;
    a.set(1);
    a.set(3);
    b.set(3);
    b.set(5);

    auto c = a | b;
    EXPECT_TRUE(c.test(1));
    EXPECT_TRUE(c.test(3));
    EXPECT_TRUE(c.test(5));
    EXPECT_EQ(c.popcount(), 3U);
  }

  // OR with empty is idempotent.
  if (true) {
    fixed_bitset<64> a, empty;
    a.set(7);
    auto result = a | empty;
    EXPECT_EQ(result, a);
  }

  // operator|= in-place.
  if (true) {
    fixed_bitset<64> a, b;
    a.set(1);
    b.set(2);
    a |= b;
    EXPECT_TRUE(a.test(1));
    EXPECT_TRUE(a.test(2));
    EXPECT_EQ(a.popcount(), 2U);
  }
}

void FixedBitset_BitwiseXor() {
  // Basic XOR: shared bits cancel, unique bits survive.
  if (true) {
    fixed_bitset<64> a, b;
    a.set(1);
    a.set(3);
    a.set(5);
    b.set(3);
    b.set(5);
    b.set(7);

    auto c = a ^ b;
    EXPECT_TRUE(c.test(1));  // only in a
    EXPECT_FALSE(c.test(3)); // in both, cancelled
    EXPECT_FALSE(c.test(5)); // in both, cancelled
    EXPECT_TRUE(c.test(7));  // only in b
    EXPECT_EQ(c.popcount(), 2U);
  }

  // XOR with itself yields empty.
  if (true) {
    fixed_bitset<64> a;
    a.set(4);
    a.set(20);
    a.set(63);
    EXPECT_TRUE((a ^ a).none());
  }

  // XOR with empty is idempotent.
  if (true) {
    fixed_bitset<64> a, empty;
    a.set(9);
    EXPECT_EQ(a ^ empty, a);
  }

  // operator^= in-place.
  if (true) {
    fixed_bitset<64> a, b;
    a.set(0);
    a.set(1);
    b.set(1);
    b.set(2);
    a ^= b;
    EXPECT_TRUE(a.test(0));  // only in original a
    EXPECT_FALSE(a.test(1)); // cancelled
    EXPECT_TRUE(a.test(2));  // only in b
  }

  // XOR across word boundary (128-bit).
  if (true) {
    fixed_bitset<128> a, b;
    a.set(63);
    a.set(64);
    b.set(63);
    b.set(65);
    auto c = a ^ b;
    EXPECT_FALSE(c.test(63)); // cancelled
    EXPECT_TRUE(c.test(64));  // only in a
    EXPECT_TRUE(c.test(65));  // only in b
  }
}

void FixedBitset_Complement() {
  // ~empty is all-ones.
  if (true) {
    fixed_bitset<64> empty;
    auto full = ~empty;
    EXPECT_TRUE(full.all());
    EXPECT_EQ(full.popcount(), 64U);
  }

  // ~all-ones is empty.
  if (true) {
    fixed_bitset<64> full;
    for (std::size_t i = 0; i < 64; ++i) full.set(i);
    EXPECT_TRUE((~full).none());
  }

  // Complement is its own inverse: ~~b == b.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(17);
    b.set(63);
    EXPECT_EQ(~~b, b);
  }

  // Specific bit inversion.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    auto c = ~b;
    EXPECT_FALSE(c.test(5));
    EXPECT_TRUE(c.test(0));
    EXPECT_TRUE(c.test(4));
    EXPECT_TRUE(c.test(6));
    EXPECT_TRUE(c.test(63));
    EXPECT_EQ(c.popcount(), 63U);
  }

  // Complement across word boundary (128-bit).
  if (true) {
    fixed_bitset<128> b;
    b.set(63);
    b.set(64);
    auto c = ~b;
    EXPECT_FALSE(c.test(63));
    EXPECT_FALSE(c.test(64));
    EXPECT_TRUE(c.test(62));
    EXPECT_TRUE(c.test(65));
    EXPECT_EQ(c.popcount(), 126U);
  }
}

void FixedBitset_CountBits() {
  // --- countr_zero: index of the lowest set bit, or bit_count_v if none ---

  // Empty → bit_count_v.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_EQ(b.countr_zero(), 64U);
  }
  // Bit 0 set → 0 trailing zeros.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    EXPECT_EQ(b.countr_zero(), 0U);
  }
  // Bit 5 set (bits 0-4 clear) → 5 trailing zeros.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    EXPECT_EQ(b.countr_zero(), 5U);
  }
  // All set → 0 trailing zeros.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_EQ(b.countr_zero(), 0U);
  }
  // Multi-word: only bit 64 set → 64 trailing zeros.
  if (true) {
    fixed_bitset<128> b;
    b.set(64);
    EXPECT_EQ(b.countr_zero(), 64U);
  }

  // --- countr_one: number of consecutive ones from bit 0 ---

  // Empty → 0.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_EQ(b.countr_one(), 0U);
  }
  // Bit 0 only → 1.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    EXPECT_EQ(b.countr_one(), 1U);
  }
  // Bits 0-4 set, bit 5 clear → 5.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 5; ++i) b.set(i);
    EXPECT_EQ(b.countr_one(), 5U);
  }
  // All set → 64.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_EQ(b.countr_one(), 64U);
  }
  // Multi-word: bits 0-63 set, bit 64 clear → 64.
  if (true) {
    fixed_bitset<128> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_EQ(b.countr_one(), 64U);
  }
  // Multi-word: bits 0-64 set → 65.
  if (true) {
    fixed_bitset<128> b;
    for (std::size_t i = 0; i <= 64; ++i) b.set(i);
    EXPECT_EQ(b.countr_one(), 65U);
  }

  // --- countl_zero: leading zeros from the top bit ---

  // Empty → bit_count_v.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_EQ(b.countl_zero(), 64U);
  }
  // Bit 63 set (highest) → 0 leading zeros.
  if (true) {
    fixed_bitset<64> b;
    b.set(63);
    EXPECT_EQ(b.countl_zero(), 0U);
  }
  // Bit 62 set → 1 leading zero.
  if (true) {
    fixed_bitset<64> b;
    b.set(62);
    EXPECT_EQ(b.countl_zero(), 1U);
  }
  // Bit 0 only → 63 leading zeros.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    EXPECT_EQ(b.countl_zero(), 63U);
  }
  // All set → 0 leading zeros.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_EQ(b.countl_zero(), 0U);
  }
  // Multi-word 128-bit: only bit 64 set → 63 leading zeros.
  if (true) {
    fixed_bitset<128> b;
    b.set(64);
    EXPECT_EQ(b.countl_zero(), 63U);
  }
  // Multi-word 128-bit: only bit 0 set → 127 leading zeros.
  if (true) {
    fixed_bitset<128> b;
    b.set(0);
    EXPECT_EQ(b.countl_zero(), 127U);
  }

  // --- countl_one: leading ones from the top bit ---

  // Empty → 0.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_EQ(b.countl_one(), 0U);
  }
  // Bit 63 set → 1.
  if (true) {
    fixed_bitset<64> b;
    b.set(63);
    EXPECT_EQ(b.countl_one(), 1U);
  }
  // Bits 62-63 set → 2.
  if (true) {
    fixed_bitset<64> b;
    b.set(62);
    b.set(63);
    EXPECT_EQ(b.countl_one(), 2U);
  }
  // All set → 64.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_EQ(b.countl_one(), 64U);
  }
  // Multi-word 128-bit: bits 64-127 set, word 0 empty → 64.
  if (true) {
    fixed_bitset<128> b;
    for (std::size_t i = 64; i < 128; ++i) b.set(i);
    EXPECT_EQ(b.countl_one(), 64U);
  }

  // --- bit_width: position of highest set bit + 1, or 0 if empty ---

  // Empty → 0.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_EQ(b.bit_width(), 0U);
  }
  // Bit 0 → 1.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    EXPECT_EQ(b.bit_width(), 1U);
  }
  // Bit 5 → 6.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    EXPECT_EQ(b.bit_width(), 6U);
  }
  // Bit 63 → 64.
  if (true) {
    fixed_bitset<64> b;
    b.set(63);
    EXPECT_EQ(b.bit_width(), 64U);
  }
  // bit_width tracks the highest bit, not the count.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(10);
    b.set(30);
    EXPECT_EQ(b.bit_width(), 31U);
  }
}

void FixedBitset_HasSingleBit() {
  // Empty → false.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_FALSE(b.has_single_bit());
  }

  // Each individual bit → true.
  if (true) {
    for (std::size_t i = 0; i < 64; ++i) {
      fixed_bitset<64> b;
      b.set(i);
      EXPECT_TRUE(b.has_single_bit());
    }
  }

  // Two bits in the same word → false.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(1);
    EXPECT_FALSE(b.has_single_bit());
  }

  // Two bits in different words → false.
  if (true) {
    fixed_bitset<128> b;
    b.set(0);
    b.set(64);
    EXPECT_FALSE(b.has_single_bit());
  }

  // A single bit in the second word → true.
  if (true) {
    fixed_bitset<128> b;
    b.set(100);
    EXPECT_TRUE(b.has_single_bit());
  }

  // All set → false.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_FALSE(b.has_single_bit());
  }
}

void FixedBitset_Iteration() {
  // Single bit.
  if (true) {
    fixed_bitset<64> b;
    b.set(17);
    std::vector<std::size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_EQ(bits.size(), 1U);
    EXPECT_EQ(bits[0], 17U);
  }

  // Multiple bits, should come out in ascending order.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(7);
    b.set(31);
    b.set(63);
    std::vector<std::size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_EQ(bits.size(), 4U);
    EXPECT_EQ(bits[0], 0U);
    EXPECT_EQ(bits[1], 7U);
    EXPECT_EQ(bits[2], 31U);
    EXPECT_EQ(bits[3], 63U);
  }

  // Iteration count matches popcount.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    b.set(15);
    b.set(25);
    std::size_t count = 0;
    for ([[maybe_unused]] auto idx : b) ++count;
    EXPECT_EQ(count, b.popcount());
  }

  // Post-increment yields the old value.
  if (true) {
    fixed_bitset<64> b;
    b.set(3);
    b.set(9);
    auto it = b.begin();
    auto old = it++;
    EXPECT_EQ(*old, 3U);
    EXPECT_EQ(*it, 9U);
  }

  // cbegin/cend agree with begin/end.
  if (true) {
    fixed_bitset<64> b;
    b.set(11);
    EXPECT_EQ(*b.cbegin(), *b.begin());
    EXPECT_TRUE(b.cend() == b.end());
  }
}

void FixedBitset_MultiWord() {
  // 128-bit (2-word) bitset: bits span both words.
  if (true) {
    fixed_bitset<128> b;
    b.set(0);   // word 0, bit 0
    b.set(63);  // word 0, bit 63
    b.set(64);  // word 1, bit 0
    b.set(127); // word 1, bit 63
    EXPECT_EQ(b.popcount(), 4U);

    std::vector<std::size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_EQ(bits.size(), 4U);
    EXPECT_EQ(bits[0], 0U);
    EXPECT_EQ(bits[1], 63U);
    EXPECT_EQ(bits[2], 64U);
    EXPECT_EQ(bits[3], 127U);
  }

  // AND across word boundary.
  if (true) {
    fixed_bitset<128> a, b;
    a.set(63);
    a.set(64);
    b.set(63);
    b.set(65);
    auto c = a & b;
    EXPECT_TRUE(c.test(63));
    EXPECT_FALSE(c.test(64));
    EXPECT_FALSE(c.test(65));
    EXPECT_EQ(c.popcount(), 1U);
  }

  // OR across word boundary.
  if (true) {
    fixed_bitset<128> a, b;
    a.set(60);
    b.set(68);
    auto c = a | b;
    EXPECT_TRUE(c.test(60));
    EXPECT_TRUE(c.test(68));
    EXPECT_EQ(c.popcount(), 2U);
  }

  // Iteration skips entirely empty words.
  if (true) {
    fixed_bitset<128> b;
    b.set(100); // only in word 1
    std::vector<std::size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_EQ(bits.size(), 1U);
    EXPECT_EQ(bits[0], 100U);
  }

  // Reset clears all words.
  if (true) {
    fixed_bitset<128> b;
    for (std::size_t i = 0; i < 128; ++i) b.set(i);
    EXPECT_TRUE(b.all());
    b.reset();
    EXPECT_TRUE(b.none());
  }

  // all() requires every word to be full.
  if (true) {
    fixed_bitset<128> b;
    for (std::size_t i = 0; i < 128; ++i) b.set(i);
    EXPECT_TRUE(b.all());
    b.clear(64);
    EXPECT_FALSE(b.all());
  }

  // Equality across words.
  if (true) {
    fixed_bitset<128> a, b;
    a.set(0);
    a.set(127);
    b.set(0);
    EXPECT_NE(a, b);
    b.set(127);
    EXPECT_EQ(a, b);
  }
}

MAKE_TEST_LIST(FixedBitset_Empty, FixedBitset_SetClearTest,
    FixedBitset_Subscript, FixedBitset_Popcount, FixedBitset_Reset,
    FixedBitset_Equality, FixedBitset_CopyMove, FixedBitset_BitwiseAnd,
    FixedBitset_BitwiseOr, FixedBitset_BitwiseXor, FixedBitset_Complement,
    FixedBitset_CountBits, FixedBitset_HasSingleBit, FixedBitset_Iteration,
    FixedBitset_MultiWord);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
