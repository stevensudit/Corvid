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

// Minimal scoped enum used by POS-parameter and at() tests.
// It has no arithmetic operators, which exercises the as_sz/as_pos casts.
enum class slot_t : size_t {};

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

void FixedBitset_Empty() {
  // Default construction: all bits clear.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_TRUE(b.none());
    EXPECT_FALSE(b.any());
    EXPECT_FALSE(b.all());
    EXPECT_EQ(b.count(), 0U);
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

    b.reset(0);
    EXPECT_FALSE(b.test(0));
    EXPECT_TRUE(b.test(63));

    b.reset(63);
    EXPECT_FALSE(b.test(63));
    EXPECT_TRUE(b.none());
  }

  // Set/reset all 64 bits.
  if (true) {
    fixed_bitset<64> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_TRUE(b.all());
    EXPECT_EQ(b.count(), 64U);

    for (std::size_t i = 0; i < 64; ++i) b.reset(i);
    EXPECT_TRUE(b.none());
  }

  // set(pos, false) clears the bit — equivalent to reset(pos).
  if (true) {
    fixed_bitset<64> b;
    b.set(10);
    b.set(20);
    b.set(10, false);
    EXPECT_FALSE(b.test(10));
    EXPECT_TRUE(b.test(20));

    b.set(20, true); // redundant but valid
    EXPECT_TRUE(b.test(20));
  }

  // set() with no arguments sets all bits at once.
  if (true) {
    fixed_bitset<64> b;
    b.set();
    EXPECT_TRUE(b.all());
    EXPECT_EQ(b.count(), 64U);
  }

  // set() with padding respects the valid-bit mask.
  if (true) {
    fixed_bitset<8, size_t, void, 64> b;
    b.set();
    EXPECT_TRUE(b.all());
    EXPECT_EQ(b.count(), 8U);
    EXPECT_EQ(b.array()[0], uint64_t{0xFF}); // only 8 valid bits set
  }

  // reset(pos_t) clears one bit and returns *this, enabling chaining.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    b.set(10);
    b.set(20);
    b.reset(10).reset(20);
    EXPECT_TRUE(b.test(5));
    EXPECT_FALSE(b.test(10));
    EXPECT_FALSE(b.test(20));
  }
}

// operator[] has two overloads: const (returns bool) and non-const
// (returns a reference proxy for mutable access).
void FixedBitset_Subscript() {
  // Const overload: read-only, returns bool directly.
  if (true) {
    fixed_bitset<64> b;
    b.set(7);
    b.set(42);
    const fixed_bitset<64>& cb = b;

    EXPECT_TRUE(cb[7]);
    EXPECT_TRUE(cb[42]);
    EXPECT_FALSE(cb[0]);
    EXPECT_FALSE(cb[6]);
    EXPECT_FALSE(cb[8]);
    EXPECT_FALSE(cb[63]);

    // Must agree with test() at every position.
    for (std::size_t i = 0; i < 64; ++i) EXPECT_EQ(cb[i], b.test(i));
  }

  // Non-const overload: returns a reference proxy; writes go through.
  if (true) {
    fixed_bitset<64> b;
    b[7] = true;
    EXPECT_TRUE(b.test(7));
    b[7] = false;
    EXPECT_FALSE(b.test(7));

    b[0] = true;
    b[63] = true;
    EXPECT_EQ(b.count(), 2U);

    // Non-const [] also reads correctly through the proxy.
    EXPECT_TRUE(bool(b[0]));
    EXPECT_TRUE(bool(b[63]));
    EXPECT_FALSE(bool(b[1]));
  }
}

void FixedBitset_Popcount() {
  if (true) {
    fixed_bitset<64> b;
    b.set(3);
    b.set(7);
    b.set(62);
    EXPECT_EQ(b.count(), 3U);
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
    EXPECT_EQ(b.count(), 64U);
  }
}

void FixedBitset_Reset() {
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(31);
    b.set(63);
    EXPECT_EQ(b.count(), 3U);
    b.reset();
    EXPECT_TRUE(b.none());
    EXPECT_EQ(b.count(), 0U);
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
    b.reset(10);
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

// word_t is automatically selected as the largest power-of-2 unsigned type
// whose width evenly divides N_BITS. bits_per_word_v reflects the word size.
void FixedBitset_WordType() {
  // Type selection: compile-time verification.
  if (true) {
    static_assert(std::is_same_v<fixed_bitset<64>::word_t, uint64_t>);
    static_assert(std::is_same_v<fixed_bitset<128>::word_t, uint64_t>);
    static_assert(std::is_same_v<fixed_bitset<32>::word_t, uint32_t>);
    static_assert(std::is_same_v<fixed_bitset<96>::word_t, uint32_t>);
    static_assert(std::is_same_v<fixed_bitset<16>::word_t, uint16_t>);
    static_assert(std::is_same_v<fixed_bitset<48>::word_t, uint16_t>);
    static_assert(std::is_same_v<fixed_bitset<8>::word_t, uint8_t>);
    static_assert(std::is_same_v<fixed_bitset<24>::word_t, uint8_t>);

    // bits_per_word_v matches the chosen type.
    static_assert(fixed_bitset<64>::bits_per_word_v == 64);
    static_assert(fixed_bitset<32>::bits_per_word_v == 32);
    static_assert(fixed_bitset<16>::bits_per_word_v == 16);
    static_assert(fixed_bitset<8>::bits_per_word_v == 8);

    // word_count_v: the canonical 24-bit / 3-word cases.
    static_assert(fixed_bitset<24>::bit_count_v == 24);
    static_assert(fixed_bitset<24>::bits_per_word_v == 8);
    static_assert(fixed_bitset<24>::word_count_v == 3);
    static_assert(fixed_bitset<48>::bits_per_word_v == 16);
    static_assert(fixed_bitset<48>::word_count_v == 3);
    static_assert(fixed_bitset<96>::bits_per_word_v == 32);
    static_assert(fixed_bitset<96>::word_count_v == 3);

    // No padding: the object is exactly N/8 bytes, one bit per bit.
    static_assert(sizeof(fixed_bitset<8>) == 1);
    static_assert(sizeof(fixed_bitset<16>) == 2);
    static_assert(sizeof(fixed_bitset<24>) == 3);
    static_assert(sizeof(fixed_bitset<32>) == 4);
    static_assert(sizeof(fixed_bitset<48>) == 6);
    static_assert(sizeof(fixed_bitset<64>) == 8);
    static_assert(sizeof(fixed_bitset<96>) == 12);
    static_assert(sizeof(fixed_bitset<128>) == 16);
  }

  // Runtime: fixed_bitset<8> — single uint8_t word.
  if (true) {
    fixed_bitset<8> b;
    EXPECT_TRUE(b.none());
    b.set(0);
    b.set(7);
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(7));
    EXPECT_FALSE(b.test(1));
    EXPECT_EQ(b.count(), 2U);

    std::vector<std::size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_EQ(bits.size(), 2U);
    EXPECT_EQ(bits[0], 0U);
    EXPECT_EQ(bits[1], 7U);

    // all() requires all 8 bits set.
    for (std::size_t i = 0; i < 8; ++i) b.set(i);
    EXPECT_TRUE(b.all());
    b.reset(3);
    EXPECT_FALSE(b.all());
  }

  // Runtime: fixed_bitset<24> — three uint8_t words.
  if (true) {
    fixed_bitset<24> b;

    // Set one bit per word.
    b.set(0);  // word 0, bit 0
    b.set(9);  // word 1, bit 1
    b.set(23); // word 2, bit 7
    EXPECT_EQ(b.count(), 3U);

    std::vector<std::size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_EQ(bits.size(), 3U);
    EXPECT_EQ(bits[0], 0U);
    EXPECT_EQ(bits[1], 9U);
    EXPECT_EQ(bits[2], 23U);

    // Complement flips all 24 bits.
    auto c = ~b;
    EXPECT_EQ(c.count(), 21U);
    EXPECT_FALSE(c.test(0));
    EXPECT_FALSE(c.test(9));
    EXPECT_FALSE(c.test(23));
    EXPECT_TRUE(c.test(1));
    EXPECT_TRUE(c.test(8));

    // AND across all three words.
    fixed_bitset<24> a;
    a.set(0);
    a.set(8);
    a.set(16);
    auto d = a & b;
    EXPECT_TRUE(d.test(0));   // in both
    EXPECT_FALSE(d.test(8));  // only in a
    EXPECT_FALSE(d.test(9));  // only in b
    EXPECT_FALSE(d.test(16)); // only in a

    // OR across all three words.
    auto e = a | b;
    EXPECT_TRUE(e.test(0));
    EXPECT_TRUE(e.test(8));
    EXPECT_TRUE(e.test(9));
    EXPECT_TRUE(e.test(16));
    EXPECT_TRUE(e.test(23));
  }

  // Runtime: fixed_bitset<32> — single uint32_t word.
  if (true) {
    fixed_bitset<32> b;
    b.set(0);
    b.set(31);
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(31));
    EXPECT_FALSE(b.test(16));
    EXPECT_EQ(b.count(), 2U);

    for (std::size_t i = 0; i < 32; ++i) b.set(i);
    EXPECT_TRUE(b.all());
  }

  // array() exposes the raw word array; mutations through it are reflected in
  // the bitset, and reads through it agree with test().
  if (true) {
    // Mutable: write a known pattern directly into the words.
    fixed_bitset<24> b;  // 3 × uint8_t
    b.array()[0] = 0x01; // bit 0 set in word 0
    b.array()[1] = 0x02; // bit 9 set in word 1
    b.array()[2] = 0x80; // bit 23 set in word 2
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(9));
    EXPECT_TRUE(b.test(23));
    EXPECT_EQ(b.count(), 3U);

    // Const: array().data() yields const word_t*.
    const fixed_bitset<24>& cb = b;
    static_assert(std::is_same_v<decltype(cb.array().data()),
        const fixed_bitset<24>::word_t*>);
    EXPECT_EQ(cb.array()[0], uint8_t{0x01});
    EXPECT_EQ(cb.array()[1], uint8_t{0x02});
    EXPECT_EQ(cb.array()[2], uint8_t{0x80});
  }
}

// FORCED_WORD overrides the auto-selected word type. When the word is larger
// than N_BITS, the top (N_BITS % bits_per_word_v) bits are padding kept zero.
void FixedBitset_ForcedWord() {
  // Compile-time: type and sizing.
  if (true) {
    // Force smaller words for a large bitset.
    using fb64x8 = fixed_bitset<64, size_t, void, 8>;
    static_assert(std::is_same_v<fb64x8::word_t, uint8_t>);
    static_assert(fb64x8::bits_per_word_v == 8);
    static_assert(fb64x8::word_count_v == 8);
    static_assert(sizeof(fb64x8) == 8); // 8 × uint8_t

    // Force a larger word for a small bitset (padding in the top word).
    using fb8x64 = fixed_bitset<8, size_t, void, 64>;
    static_assert(std::is_same_v<fb8x64::word_t, uint64_t>);
    static_assert(fb8x64::bits_per_word_v == 64);
    static_assert(fb8x64::word_count_v == 1);
    static_assert(sizeof(fb8x64) == 8); // 1 × uint64_t

    // 24 bits in a single uint32_t (8 padding bits).
    using fb24x32 = fixed_bitset<24, size_t, void, 32>;
    static_assert(std::is_same_v<fb24x32::word_t, uint32_t>);
    static_assert(fb24x32::word_count_v == 1);
    static_assert(sizeof(fb24x32) == 4); // 1 × uint32_t

    // 96 bits in two uint64_t words (32 padding bits in the top word).
    using fb96x64 = fixed_bitset<96, size_t, void, 64>;
    static_assert(std::is_same_v<fb96x64::word_t, uint64_t>);
    static_assert(fb96x64::word_count_v == 2);
    static_assert(sizeof(fb96x64) == 16); // 2 × uint64_t
  }

  // Runtime: fixed_bitset<64, size_t, void, 8> — 8 × uint8_t, no padding.
  if (true) {
    fixed_bitset<64, size_t, void, 8> b;
    b.set(0);
    b.set(7);  // last bit of word 0
    b.set(8);  // first bit of word 1
    b.set(63); // last bit of word 7
    EXPECT_EQ(b.count(), 4U);
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(7));
    EXPECT_TRUE(b.test(8));
    EXPECT_TRUE(b.test(63));
    EXPECT_FALSE(b.test(1));

    std::vector<size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_EQ(bits.size(), 4U);
    EXPECT_EQ(bits[0], 0U);
    EXPECT_EQ(bits[1], 7U);
    EXPECT_EQ(bits[2], 8U);
    EXPECT_EQ(bits[3], 63U);

    // all() requires all 64 bits set.
    for (size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_TRUE(b.all());
  }

  // Runtime: fixed_bitset<8, size_t, void, 64> — 1 × uint64_t, 56 pad bits.
  if (true) {
    using fb8 = fixed_bitset<8, size_t, void, 64>;
    fb8 b;
    b.set(0);
    b.set(7);
    EXPECT_EQ(b.count(), 2U);
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(7));

    // Padding bits (8-63) are never set by normal operations.
    EXPECT_EQ(b.array()[0] & ~uint64_t{0xFF}, uint64_t{0});

    // all() requires only the 8 valid bits to be set.
    for (size_t i = 0; i < 8; ++i) b.set(i);
    EXPECT_TRUE(b.all());
    EXPECT_EQ(b.count(), 8U);
    EXPECT_EQ(b.array()[0], uint64_t{0xFF}); // padding bits remain 0

    // flip() inverts only the 8 valid bits; padding stays zero.
    fb8 empty;
    auto full = ~empty;
    EXPECT_TRUE(full.all());
    EXPECT_EQ(full.array()[0], uint64_t{0xFF});

    // operator<<= keeps padding clear.
    fb8 s;
    s.set(3);
    s <<= 4;
    EXPECT_TRUE(s.test(7));
    EXPECT_FALSE(s.test(3));
    EXPECT_EQ(s.array()[0] & ~uint64_t{0xFF}, uint64_t{0});

    // countl_zero counts only the 8 valid bits.
    fb8 top_bit;
    top_bit.set(7); // highest valid bit
    EXPECT_EQ(top_bit.countl_zero(), 0U);

    fb8 bot_bit;
    bot_bit.set(0);
    EXPECT_EQ(bot_bit.countl_zero(), 7U);

    // countl_one.
    fb8 all_set;
    for (size_t i = 0; i < 8; ++i) all_set.set(i);
    EXPECT_EQ(all_set.countl_one(), 8U);

    fb8 top_two;
    top_two.set(7);
    top_two.set(6);
    EXPECT_EQ(top_two.countl_one(), 2U);

    // rotl wraps the top valid bit to bit 0.
    fb8 rot;
    rot.set(7);
    rot.rotl(1);
    EXPECT_TRUE(rot.test(0));
    EXPECT_FALSE(rot.test(7));
    EXPECT_EQ(rot.array()[0] & ~uint64_t{0xFF}, uint64_t{0});
  }

  // Runtime: fixed_bitset<24, size_t, void, 32> — 1 × uint32_t, 8 pad bits.
  if (true) {
    using fb24 = fixed_bitset<24, size_t, void, 32>;
    fb24 b;
    b.set(0);
    b.set(12);
    b.set(23); // highest valid bit
    EXPECT_EQ(b.count(), 3U);

    std::vector<size_t> bits;
    for (auto idx : b) bits.push_back(idx);
    EXPECT_EQ(bits.size(), 3U);
    EXPECT_EQ(bits[0], 0U);
    EXPECT_EQ(bits[1], 12U);
    EXPECT_EQ(bits[2], 23U);

    // flip() sets exactly 24 bits; top 8 bits of the uint32_t remain 0.
    fb24 empty;
    auto full = ~empty;
    EXPECT_EQ(full.count(), 24U);
    EXPECT_EQ(full.array()[0], uint32_t{0x00FFFFFF});
    EXPECT_TRUE(full.all());

    // countl_zero counts only the 24 valid bits.
    fb24 top_only;
    top_only.set(23);
    EXPECT_EQ(top_only.countl_zero(), 0U);

    fb24 bot_only;
    bot_only.set(0);
    EXPECT_EQ(bot_only.countl_zero(), 23U);

    // countl_one.
    EXPECT_EQ(full.countl_one(), 24U);

    // rotl wraps the top valid bit (23) to bit 0.
    fb24 rot;
    rot.set(23);
    rot.rotl(1);
    EXPECT_TRUE(rot.test(0));
    EXPECT_FALSE(rot.test(23));
    EXPECT_EQ(rot.array()[0] & ~uint32_t{0x00FFFFFF}, uint32_t{0});
  }
}

// operator[](pos) on a mutable bitset returns a reference proxy akin to
// std::bitset<N>::reference. The proxy is a zero-overhead abstraction that
// the compiler eliminates at -O1 and above.
void FixedBitset_Reference() {
  // operator bool() reads the current bit value through the proxy.
  if (true) {
    fixed_bitset<64> b;
    b.set(7);
    EXPECT_TRUE(static_cast<bool>(b[7]));
    EXPECT_FALSE(static_cast<bool>(b[0]));
  }

  // operator=(bool) writes through the proxy.
  if (true) {
    fixed_bitset<64> b;
    b[3] = true;
    EXPECT_TRUE(b.test(3));
    b[3] = false;
    EXPECT_FALSE(b.test(3));

    // Setting a bit that is already set / clearing one already clear.
    b.set(9);
    b[9] = true;
    EXPECT_TRUE(b.test(9));
    b[9] = false;
    EXPECT_FALSE(b.test(9));
  }

  // operator~() returns the inverted bit value without modifying the bitset.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    EXPECT_FALSE(~b[5]);     // set → inverted is false
    EXPECT_TRUE(~b[6]);      // clear → inverted is true
    EXPECT_TRUE(b.test(5));  // unchanged
    EXPECT_FALSE(b.test(6)); // unchanged
  }

  // flip() toggles the referenced bit in-place.
  if (true) {
    fixed_bitset<64> b;
    b.set(8);
    b[8].flip();
    EXPECT_FALSE(b.test(8));
    b[8].flip();
    EXPECT_TRUE(b.test(8));
  }

  // operator=(const reference&) copies the bit value from another reference.
  if (true) {
    fixed_bitset<64> a, b;
    a.set(2);
    b[5] = a[2]; // b[5] ← true (bit 2 of a)
    EXPECT_TRUE(b.test(5));
    b[5] = a[3]; // b[5] ← false (bit 3 of a is clear)
    EXPECT_FALSE(b.test(5));
  }

  // array() — mutable overload returns a reference to the underlying array.
  if (true) {
    fixed_bitset<64> b;
    static_assert(std::is_same_v<decltype(b.array()),
        std::array<fixed_bitset<64>::word_t,
            fixed_bitset<64>::word_count_v>&>);

    b.array()[0] = uint64_t{0xFF}; // set bits 0-7 directly
    EXPECT_EQ(b.count(), 8U);
    for (size_t i = 0; i < 8; ++i) EXPECT_TRUE(b.test(i));
    for (size_t i = 8; i < 64; ++i) EXPECT_FALSE(b.test(i));
  }

  // array() — const overload returns a const reference to the underlying
  // array.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(63);
    const fixed_bitset<64>& cb = b;
    static_assert(std::is_same_v<decltype(cb.array()),
        const std::array<fixed_bitset<64>::word_t,
            fixed_bitset<64>::word_count_v>&>);
    const auto expected = (uint64_t{1}) | (uint64_t{1} << 63);
    EXPECT_EQ(cb.array()[0], expected);
  }

  // Multi-word array() access.
  if (true) {
    fixed_bitset<128> b;
    b.set(0);
    b.set(64);
    EXPECT_EQ(b.array()[0], uint64_t{1}); // word 0
    EXPECT_EQ(b.array()[1], uint64_t{1}); // word 1
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
    EXPECT_EQ(c.count(), 2U);
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
    EXPECT_EQ(c.count(), 3U);
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
    EXPECT_EQ(a.count(), 2U);
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
    EXPECT_EQ(c.count(), 2U);
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
    EXPECT_EQ(full.count(), 64U);
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
    EXPECT_EQ(c.count(), 63U);
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
    EXPECT_EQ(c.count(), 126U);
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

  // Iteration count matches count().
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    b.set(15);
    b.set(25);
    std::size_t count = 0;
    for ([[maybe_unused]] auto idx : b) ++count;
    EXPECT_EQ(count, b.count());
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
    EXPECT_EQ(b.count(), 4U);

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
    EXPECT_EQ(c.count(), 1U);
  }

  // OR across word boundary.
  if (true) {
    fixed_bitset<128> a, b;
    a.set(60);
    b.set(68);
    auto c = a | b;
    EXPECT_TRUE(c.test(60));
    EXPECT_TRUE(c.test(68));
    EXPECT_EQ(c.count(), 2U);
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
    b.reset(64);
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

void FixedBitset_PosParam() {
  using bs_t = fixed_bitset<64, slot_t>;

  // pos_t is the enum type.
  static_assert(std::is_same_v<bs_t::pos_t, slot_t>);

  // set/test/operator[] accept pos_t.
  if (true) {
    bs_t b;
    b.set(slot_t{5});
    EXPECT_TRUE(b.test(slot_t{5}));
    EXPECT_TRUE(bool(b[slot_t{5}]));
    EXPECT_FALSE(b.test(slot_t{0}));

    b.reset(slot_t{5});
    EXPECT_FALSE(b.test(slot_t{5}));
  }

  // Iterator yields pos_t values.
  if (true) {
    bs_t b;
    b.set(slot_t{3});
    b.set(slot_t{17});
    b.set(slot_t{63});

    std::vector<slot_t> got;
    for (auto p : b) got.push_back(p);
    EXPECT_EQ(got.size(), 3U);
    // Use EXPECT_TRUE for enum comparisons: EXPECT_EQ's error path can't
    // print unprintable types.
    EXPECT_TRUE(got[0] == slot_t{3});
    EXPECT_TRUE(got[1] == slot_t{17});
    EXPECT_TRUE(got[2] == slot_t{63});
  }

  // countr_zero returns pos_t; sentinel is as_pos(bit_count_v).
  if (true) {
    bs_t empty;
    EXPECT_TRUE(empty.countr_zero() == slot_t{64});

    bs_t b;
    b.set(slot_t{7});
    EXPECT_TRUE(b.countr_zero() == slot_t{7});
  }

  // countr_one returns pos_t.
  if (true) {
    bs_t b;
    for (size_t i = 0; i < 4; ++i) b.set(slot_t{i});
    EXPECT_TRUE(b.countr_one() == slot_t{4});
  }

  // countl_zero returns pos_t.
  if (true) {
    bs_t empty;
    EXPECT_TRUE(empty.countl_zero() == slot_t{64});

    bs_t b;
    b.set(slot_t{63});
    EXPECT_TRUE(b.countl_zero() == slot_t{0});

    bs_t b2;
    b2.set(slot_t{62});
    EXPECT_TRUE(b2.countl_zero() == slot_t{1});
  }

  // countl_one returns pos_t.
  if (true) {
    bs_t empty;
    EXPECT_TRUE(empty.countl_one() == slot_t{0});

    bs_t b;
    b.set(slot_t{63});
    EXPECT_TRUE(b.countl_one() == slot_t{1});
  }

  // bit_width returns pos_t.
  if (true) {
    bs_t empty;
    EXPECT_TRUE(empty.bit_width() == slot_t{0});

    bs_t b;
    b.set(slot_t{5});
    EXPECT_TRUE(b.bit_width() == slot_t{6});
  }

  // Bitwise operators preserve pos_t interface.
  if (true) {
    bs_t a, b;
    a.set(slot_t{2});
    b.set(slot_t{2});
    b.set(slot_t{4});
    auto c = a & b;
    EXPECT_TRUE(c.test(slot_t{2}));
    EXPECT_FALSE(c.test(slot_t{4}));

    auto d = a | b;
    EXPECT_TRUE(d.test(slot_t{2}));
    EXPECT_TRUE(d.test(slot_t{4}));

    auto e = a ^ b;
    EXPECT_FALSE(e.test(slot_t{2}));
    EXPECT_TRUE(e.test(slot_t{4}));
  }
}

// size() is a constexpr instance method reflecting bit_count_v.
void FixedBitset_Size() {
  // Callable on instances.
  if (true) {
    EXPECT_EQ(fixed_bitset<64>{}.size(), 64U);
    EXPECT_EQ(fixed_bitset<128>{}.size(), 128U);
  }

  // Callable on named instances.
  if (true) {
    fixed_bitset<64> b;
    EXPECT_EQ(b.size(), 64U);
  }

  // Callable on const references.
  if (true) {
    const fixed_bitset<64> b;
    EXPECT_EQ(b.size(), 64U);
  }

  // Usable in constant expressions.
  if (true) {
    static_assert(fixed_bitset<64>{}.size() == 64);
    static_assert(fixed_bitset<192>{}.size() == 192);
  }
}

// at() and the other named access functions throw std::out_of_range for
// out-of-range positions. operator[] uses assert-only (no throw).
void FixedBitset_At() {
  // In-range: agrees with test() at every position.
  if (true) {
    fixed_bitset<64> b;
    b.set(10);
    b.set(50);
    for (size_t i = 0; i < 64; ++i) EXPECT_EQ(b.at(i), b.test(i));
  }

  // at() throws std::out_of_range for out-of-range positions.
  if (true) {
    fixed_bitset<64> b;
    TEST_EXCEPTION(b.at(64), std::out_of_range);
    TEST_EXCEPTION(b.at(1000), std::out_of_range);
  }

  // test() throws std::out_of_range for out-of-range positions.
  if (true) {
    fixed_bitset<64> b;
    TEST_EXCEPTION(b.test(64), std::out_of_range);
  }

  // set(pos) throws std::out_of_range for out-of-range positions.
  if (true) {
    fixed_bitset<64> b;
    TEST_EXCEPTION(b.set(64), std::out_of_range);
    TEST_EXCEPTION(b.set(64, false), std::out_of_range);
  }

  // reset(pos) throws std::out_of_range for out-of-range positions.
  if (true) {
    fixed_bitset<64> b;
    TEST_EXCEPTION(b.reset(64), std::out_of_range);
  }

  // flip(pos) throws std::out_of_range for out-of-range positions.
  if (true) {
    fixed_bitset<64> b;
    TEST_EXCEPTION(b.flip(64), std::out_of_range);
  }

  // Works with enum POS.
  if (true) {
    fixed_bitset<64, slot_t> b;
    b.set(slot_t{5});
    EXPECT_TRUE(b.at(slot_t{5}));
    EXPECT_FALSE(b.at(slot_t{6}));
    TEST_EXCEPTION(b.at(slot_t{64}), std::out_of_range);
  }

  // In-range at() is usable in constant expressions.
  if (true) {
    constexpr auto b = []() {
      fixed_bitset<64> r;
      r.set(7);
      return r;
    }();
    static_assert(b.at(7));
    static_assert(!b.at(6));
  }
}

// operator<=> provides lexicographic ordering over the underlying words array.
// Word 0 (bits 0-63) is compared before word 1 (bits 64-127), so lower-index
// words dominate even though they hold lower-index bits.
void FixedBitset_Ordering() {
  // Reflexive: equal bitsets compare equal.
  if (true) {
    fixed_bitset<64> a, b;
    EXPECT_TRUE((a <=> b) == 0);
    a.set(5);
    b.set(5);
    EXPECT_TRUE((a <=> b) == 0);
  }

  // Within one word: a higher-index bit makes the word value larger.
  if (true) {
    fixed_bitset<64> lo, hi;
    lo.set(0); // words_[0] = 1
    hi.set(1); // words_[0] = 2
    EXPECT_TRUE(lo < hi);
    EXPECT_TRUE(hi > lo);
    EXPECT_TRUE(lo <= hi);
    EXPECT_TRUE(hi >= lo);
  }

  // Empty is less than any set bit.
  if (true) {
    fixed_bitset<64> empty, b;
    b.set(0);
    EXPECT_TRUE(empty < b);
    EXPECT_FALSE(b < empty);
  }

  // Multi-word: word 0 dominates word 1, so a single bit in word 0 outweighs
  // any combination of bits in word 1.
  if (true) {
    fixed_bitset<128> word0, word1;
    word0.set(0);  // words_ = {1, 0}
    word1.set(64); // words_ = {0, 1}
    // word0 > word1 because words_[0]=1 > 0
    EXPECT_TRUE(word0 > word1);
    EXPECT_TRUE(word1 < word0);
  }

  // A full word 1 is still less than a single bit in word 0.
  if (true) {
    fixed_bitset<128> w0bit, w1full;
    w0bit.set(0);
    for (std::size_t i = 64; i < 128; ++i) w1full.set(i);
    // w0bit: words_ = {1, 0}; w1full: words_ = {0, ~0ull}
    // words_[0]: 1 > 0, so w0bit > w1full
    EXPECT_TRUE(w0bit > w1full);
  }

  // Ordering is consistent with equality.
  if (true) {
    fixed_bitset<64> a, b;
    a.set(10);
    b.set(10);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(a >= b);
    EXPECT_FALSE(a < b);
    EXPECT_FALSE(a > b);
  }
}

// TAG prevents structurally-identical bitsets from being mixed.
void FixedBitset_Tag() {
  struct tag_a {};
  struct tag_b {};
  using bs_a = fixed_bitset<64, size_t, tag_a>;
  using bs_b = fixed_bitset<64, size_t, tag_b>;
  using bs_u = fixed_bitset<64>; // untagged

  // Different tags produce incompatible types even with identical N_BITS/POS.
  static_assert(!std::is_same_v<bs_a, bs_b>);
  static_assert(!std::is_same_v<bs_a, bs_u>);
  static_assert(!std::is_assignable_v<bs_a&, bs_b>);
  static_assert(!std::is_constructible_v<bs_a, bs_b>);

  // tag_t alias is accessible and correct.
  static_assert(std::is_same_v<bs_a::tag_t, tag_a>);
  static_assert(std::is_same_v<bs_b::tag_t, tag_b>);
  static_assert(std::is_same_v<bs_u::tag_t, void>);

  // Each tagged type is independently usable at runtime.
  if (true) {
    bs_a a;
    a.set(1);
    a.set(33);
    EXPECT_TRUE(a.test(1));
    EXPECT_TRUE(a.test(33));
    EXPECT_EQ(a.count(), 2U);

    bs_b b;
    b.set(1);
    EXPECT_TRUE(b.test(1));
    EXPECT_EQ(b.count(), 1U);

    // a and b have equal content in the overlapping bit, but are distinct
    // objects of different types — no mixing is possible.
    EXPECT_EQ(a.test(1), b.test(1));
    EXPECT_NE(a.count(), b.count());
  }
}

// constexpr: bitset operations are usable in constant expressions.
void FixedBitset_Constexpr() {
  // Build a bitset at compile time.
  constexpr auto b = []() {
    fixed_bitset<64> r;
    r.set(5);
    r.set(63);
    return r;
  }();

  static_assert(b.test(5));
  static_assert(b.test(63));
  static_assert(!b.test(0));
  static_assert(b.count() == 2);
  static_assert(b.any());
  static_assert(!b.none());
  static_assert(!b.all());

  // Bitwise operators.
  constexpr auto a = []() {
    fixed_bitset<64> r;
    r.set(1);
    r.set(3);
    return r;
  }();
  constexpr auto c = []() {
    fixed_bitset<64> r;
    r.set(3);
    r.set(5);
    return r;
  }();

  static_assert((a & c).test(3));
  static_assert(!(a & c).test(1));
  static_assert((a | c).test(1));
  static_assert((a | c).test(5));
  static_assert((a ^ c).test(1));
  static_assert(!(a ^ c).test(3));
  static_assert((~a).test(0));
  static_assert(!(~a).test(1));

  // Counting queries.
  static_assert(a.countr_zero() == 1);
  static_assert(c.countr_zero() == 3);
  static_assert(b.countl_zero() == 0); // bit 63 is highest
  static_assert(b.bit_width() == 64);  // highest set bit is 63 → width 64

  // Iteration in a constexpr context.
  constexpr size_t iter_count = []() {
    fixed_bitset<64> r;
    r.set(10);
    r.set(20);
    r.set(30);
    size_t n = 0;
    for ([[maybe_unused]] auto p : r) ++n;
    return n;
  }();
  static_assert(iter_count == 3);

  // size() is constexpr.
  static_assert(fixed_bitset<64>{}.size() == 64);

  // set() (all bits) and set(pos, false) are constexpr.
  constexpr auto all_set = []() {
    fixed_bitset<64> r;
    r.set(); // set all 64 bits
    return r;
  }();
  static_assert(all_set.all());
  static_assert(all_set.count() == 64);

  constexpr auto cleared = []() {
    fixed_bitset<64> r;
    r.set();
    r.set(5, false); // clear bit 5
    return r;
  }();
  static_assert(!cleared.test(5));
  static_assert(cleared.count() == 63);

  // reset(pos_t) and chaining are constexpr.
  constexpr auto chained = []() {
    fixed_bitset<64> r;
    r.set(1);
    r.set(2);
    r.set(3);
    r.reset(2).reset(3);
    return r;
  }();
  static_assert(chained.test(1));
  static_assert(!chained.test(2));
  static_assert(!chained.test(3));
  static_assert(chained.count() == 1);

  // operator[] proxy and flip() are constexpr.
  constexpr auto via_ref = []() {
    fixed_bitset<64> r;
    r[10] = true;
    r[20] = true;
    r[10].flip(); // clears bit 10
    return r;
  }();
  static_assert(!via_ref.test(10));
  static_assert(via_ref.test(20));
  static_assert(via_ref.count() == 1);
}

void FixedBitset_Rotation() {
  // rotl by 1: bit 63 wraps to bit 0.
  if (true) {
    fixed_bitset<64> b;
    b.set(63);
    b.rotl(1);
    EXPECT_TRUE(b.test(0));
    EXPECT_FALSE(b.test(63));
    EXPECT_EQ(b.count(), 1U);
  }

  // rotr by 1: bit 0 wraps to bit 63.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.rotr(1);
    EXPECT_TRUE(b.test(63));
    EXPECT_FALSE(b.test(0));
    EXPECT_EQ(b.count(), 1U);
  }

  // rotl by bit_count_v: shift % bit_count_v == 0, so no-op.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    b.set(42);
    fixed_bitset<64> orig = b;
    b.rotl(64);
    EXPECT_EQ(b, orig);
  }

  // rotl by 2*bit_count_v: also a no-op.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    fixed_bitset<64> orig = b;
    b.rotl(128);
    EXPECT_EQ(b, orig);
  }

  // rotl(bit_count_v + k) == rotl(k).
  if (true) {
    fixed_bitset<64> b;
    b.set(10);
    fixed_bitset<64> expected = b;
    expected.rotl(3);
    b.rotl(64 + 3); // 67 % 64 == 3
    EXPECT_EQ(b, expected);
  }

  // rotr by bit_count_v: no-op.
  if (true) {
    fixed_bitset<64> b;
    b.set(7);
    b.set(33);
    fixed_bitset<64> orig = b;
    b.rotr(64);
    EXPECT_EQ(b, orig);
  }

  // rotr(bit_count_v + k) == rotr(k).
  if (true) {
    fixed_bitset<64> b;
    b.set(20);
    fixed_bitset<64> expected = b;
    expected.rotr(5);
    b.rotr(64 + 5); // 69 % 64 == 5
    EXPECT_EQ(b, expected);
  }

  // rotl and rotr are inverses.
  if (true) {
    fixed_bitset<64> b;
    b.set(15);
    b.set(40);
    fixed_bitset<64> c = b;
    c.rotl(7);
    c.rotr(7);
    EXPECT_EQ(b, c);
  }

  // Non-member rotl/rotr return a new bitset; original is unchanged.
  if (true) {
    fixed_bitset<64> b;
    b.set(10);
    auto lhs = rotl(b, 3);
    EXPECT_TRUE(lhs.test(13));
    EXPECT_FALSE(lhs.test(10));
    EXPECT_TRUE(b.test(10)); // original unchanged

    auto rhs = rotr(b, 3);
    EXPECT_TRUE(rhs.test(7));
    EXPECT_FALSE(rhs.test(10));
  }

  // Multi-word 128-bit: bit 127 wraps to bit 0 on rotl(1).
  if (true) {
    fixed_bitset<128> b;
    b.set(127);
    b.rotl(1);
    EXPECT_TRUE(b.test(0));
    EXPECT_FALSE(b.test(127));

    fixed_bitset<128> b2;
    b2.set(0);
    b2.rotr(1);
    EXPECT_TRUE(b2.test(127));
    EXPECT_FALSE(b2.test(0));
  }

  // Multi-word: rotl(128) and rotl(256) are no-ops.
  if (true) {
    fixed_bitset<128> b;
    b.set(5);
    b.set(70);
    fixed_bitset<128> orig = b;
    b.rotl(128);
    EXPECT_EQ(b, orig);
    b.rotl(256);
    EXPECT_EQ(b, orig);
  }
}

void FixedBitset_Shift() {
  // <<= by 0: no-op.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(7);
    b <<= 0;
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(7));
    EXPECT_EQ(b.count(), 2U);
  }

  // <<= by 1: each bit moves to the next-higher index; bit 0 vacated.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(7);
    b <<= 1;
    EXPECT_TRUE(b.test(1));
    EXPECT_TRUE(b.test(8));
    EXPECT_FALSE(b.test(0));
    EXPECT_FALSE(b.test(7));
  }

  // <<= by bit_count_v: clears everything.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(63);
    b <<= 64;
    EXPECT_TRUE(b.none());
  }

  // <<= by more than bit_count_v: also clears.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    b <<= 100;
    EXPECT_TRUE(b.none());
  }

  // >>= by 0: no-op.
  if (true) {
    fixed_bitset<64> b;
    b.set(10);
    b >>= 0;
    EXPECT_TRUE(b.test(10));
    EXPECT_EQ(b.count(), 1U);
  }

  // >>= by 1: each bit moves to the next-lower index; top bit vacated.
  if (true) {
    fixed_bitset<64> b;
    b.set(7);
    b.set(63);
    b >>= 1;
    EXPECT_TRUE(b.test(6));
    EXPECT_TRUE(b.test(62));
    EXPECT_FALSE(b.test(7));
    EXPECT_FALSE(b.test(63));
    EXPECT_FALSE(b.test(0)); // not fed from bit 1
  }

  // >>= by bit_count_v: clears everything.
  if (true) {
    fixed_bitset<64> b;
    b.set(0);
    b.set(63);
    b >>= 64;
    EXPECT_TRUE(b.none());
  }

  // >>= by more than bit_count_v: also clears.
  if (true) {
    fixed_bitset<64> b;
    b.set(30);
    b >>= 200;
    EXPECT_TRUE(b.none());
  }

  // Non-member << and >> return a new bitset; original is unchanged.
  if (true) {
    fixed_bitset<64> b;
    b.set(5);
    auto lhs = b << 3;
    EXPECT_TRUE(lhs.test(8));
    EXPECT_FALSE(lhs.test(5));
    EXPECT_TRUE(b.test(5)); // original unchanged

    auto rhs = b >> 3;
    EXPECT_TRUE(rhs.test(2));
    EXPECT_FALSE(rhs.test(5));
  }

  // Multi-word: <<= by 1 moves the top bit of word 0 into word 1.
  if (true) {
    fixed_bitset<128> b;
    b.set(63); // last bit of word 0
    b <<= 1;
    EXPECT_TRUE(b.test(64)); // now in word 1
    EXPECT_FALSE(b.test(63));
  }

  // Multi-word: >>= by 1 moves the low bit of word 1 into word 0.
  if (true) {
    fixed_bitset<128> b;
    b.set(64); // first bit of word 1
    b >>= 1;
    EXPECT_TRUE(b.test(63)); // now in word 0
    EXPECT_FALSE(b.test(64));
  }

  // Multi-word: <<= by a full word width shifts word 0 into word 1.
  if (true) {
    fixed_bitset<128> b;
    b.set(0);
    b.set(3);
    b <<= 64;
    EXPECT_TRUE(b.test(64));
    EXPECT_TRUE(b.test(67));
    EXPECT_FALSE(b.test(0));
    EXPECT_FALSE(b.test(3));
    EXPECT_EQ(b.array()[0], uint64_t{0}); // low word cleared
  }

  // Multi-word: >>= by a full word width shifts word 1 into word 0.
  if (true) {
    fixed_bitset<128> b;
    b.set(64);
    b.set(67);
    b >>= 64;
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(3));
    EXPECT_EQ(b.array()[1], uint64_t{0}); // high word cleared
  }

  // Multi-word: <<= by bit_count_v clears a 128-bit bitset.
  if (true) {
    fixed_bitset<128> b;
    b.set(0);
    b.set(127);
    b <<= 128;
    EXPECT_TRUE(b.none());
  }

  // Multi-word: >>= by bit_count_v clears a 128-bit bitset.
  if (true) {
    fixed_bitset<128> b;
    b.set(0);
    b.set(127);
    b >>= 128;
    EXPECT_TRUE(b.none());
  }

  // Multi-word: <<= by more than bit_count_v also clears.
  if (true) {
    fixed_bitset<128> b;
    b.set(64);
    b <<= 300;
    EXPECT_TRUE(b.none());
  }
}

void FixedBitset_ArrayConstruct() {
  // Single byte with binary literal: each set bit is readable.
  if (true) {
    fixed_bitset<8> b{std::array<uint8_t, 1>{0b10101010}};
    EXPECT_FALSE(b.test(0));
    EXPECT_TRUE(b.test(1));
    EXPECT_FALSE(b.test(2));
    EXPECT_TRUE(b.test(3));
    EXPECT_FALSE(b.test(4));
    EXPECT_TRUE(b.test(5));
    EXPECT_FALSE(b.test(6));
    EXPECT_TRUE(b.test(7));
    EXPECT_EQ(b.count(), 4U);
  }

  // 64-bit single word.
  if (true) {
    fixed_bitset<64> b{
        std::array<uint64_t, 1>{uint64_t{1} | (uint64_t{1} << 63)}};
    EXPECT_TRUE(b.test(0));
    EXPECT_TRUE(b.test(63));
    EXPECT_EQ(b.count(), 2U);
  }

  // Multi-word: fixed_bitset<24> — three uint8_t words.
  if (true) {
    fixed_bitset<24> b{std::array<uint8_t, 3>{0x01, 0x02, 0x80}};
    EXPECT_TRUE(b.test(0));  // bit 0 of word 0
    EXPECT_TRUE(b.test(9));  // bit 1 of word 1
    EXPECT_TRUE(b.test(23)); // bit 7 of word 2
    EXPECT_EQ(b.count(), 3U);
  }

  // Padding bits in the top word are masked off by the constructor.
  if (true) {
    // fixed_bitset<8, size_t, void, 64> has a single uint64_t word; the top
    // 56 bits passed in must be silently cleared.
    using fb8x64 = fixed_bitset<8, size_t, void, 64>;
    fb8x64 b{std::array<uint64_t, 1>{uint64_t{0xFF'FF}}};
    EXPECT_EQ(b.array()[0], uint64_t{0xFF}); // only 8 valid bits survive
    EXPECT_EQ(b.count(), 8U);
    EXPECT_TRUE(b.all());
  }

  // Constructor is explicit: no implicit conversions.
  if (true) {
    static_assert(
        !std::is_convertible_v<std::array<uint64_t, 1>, fixed_bitset<64>>);
    static_assert(
        !std::is_convertible_v<std::array<uint8_t, 3>, fixed_bitset<24>>);
    static_assert(
        std::is_constructible_v<fixed_bitset<64>, std::array<uint64_t, 1>>);
  }

  // Constexpr construction from an array literal.
  if (true) {
    constexpr fixed_bitset<8> b{std::array<uint8_t, 1>{0b11110000}};
    static_assert(!b.test(0));
    static_assert(!b.test(3));
    static_assert(b.test(4));
    static_assert(b.test(5));
    static_assert(b.test(6));
    static_assert(b.test(7));
    static_assert(b.count() == 4);
  }
}

MAKE_TEST_LIST(FixedBitset_Empty, FixedBitset_SetClearTest,
    FixedBitset_Subscript, FixedBitset_Popcount, FixedBitset_Reset,
    FixedBitset_Equality, FixedBitset_CopyMove, FixedBitset_WordType,
    FixedBitset_ForcedWord, FixedBitset_Reference, FixedBitset_BitwiseAnd,
    FixedBitset_BitwiseOr, FixedBitset_BitwiseXor, FixedBitset_Complement,
    FixedBitset_CountBits, FixedBitset_HasSingleBit, FixedBitset_Iteration,
    FixedBitset_MultiWord, FixedBitset_PosParam, FixedBitset_Size,
    FixedBitset_At, FixedBitset_Ordering, FixedBitset_Tag,
    FixedBitset_Constexpr, FixedBitset_Rotation, FixedBitset_Shift,
    FixedBitset_ArrayConstruct);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
