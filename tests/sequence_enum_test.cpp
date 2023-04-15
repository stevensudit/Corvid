// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
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

#include <cstdint>
#include <map>
#include <set>

#include "../corvid/meta.h"
#include "../corvid/enums/sequence_enum.h"
#include "../corvid/containers.h"
#include "AccutestShim.h"

using namespace corvid;
using namespace corvid::enums;
using namespace corvid::enums::sequence;

enum class tiger_pick { eeny, meany, miny, moe };

template<>
constexpr auto registry::enum_spec_v<tiger_pick> =
    make_sequence_enum_spec<tiger_pick, "eeny, meany, miny, moe">();

enum old_enum { old_one, old_two, old_three };
enum new_enum { new_one, new_two, new_three };

void SequentialEnumTest_Registry() {
  if (true) {
    EXPECT_EQ((strings::enum_as_string(tiger_pick::eeny)), "eeny");
  }
}

void SequentialEnumTest_Ops() {
  if (true) {
    auto e = tiger_pick::eeny;
    int i = *e;
    EXPECT_EQ(i, 0);

    // The next line correctly fails because std::byte isn't a registered enum.
    // auto bad = make<std::byte>(23);

    // The next line correctly fails to compile because int isn't an enum.
    // * auto worse = make<int>(23);

    e = e + 1;
    EXPECT_EQ(e, tiger_pick::meany);

    // Commutative.
    e = 0 + e;

    e += 1;
    EXPECT_EQ(e, tiger_pick::miny);
    e = tiger_pick::eeny;
    EXPECT_EQ(++e, tiger_pick::meany);
    EXPECT_EQ(e, tiger_pick::meany);
    EXPECT_EQ(e++, tiger_pick::meany);
    EXPECT_EQ(e, tiger_pick::miny);

    e = tiger_pick::moe;
    e = e - 1;
    EXPECT_EQ(e, tiger_pick::miny);

    // Does not compile because subtraction is not commutative.
    // * auto bad = 1 - e;

    e -= 1;
    EXPECT_EQ(e, tiger_pick::meany);
    e = tiger_pick::moe;
    EXPECT_EQ(--e, tiger_pick::miny);
    EXPECT_EQ(e, tiger_pick::miny);
    EXPECT_EQ(e--, tiger_pick::miny);
    EXPECT_EQ(e, tiger_pick::meany);

    // The following shows what happens when wrapping isn't enabled.
    e = tiger_pick::eeny;
    --e;
    EXPECT_EQ(*e, -1);
  }
  if (true) {
    using namespace strings;
    EXPECT_EQ((enum_as_string(tiger_pick::eeny)), "eeny");
    EXPECT_EQ((enum_as_string(tiger_pick::meany)), "meany");
    EXPECT_EQ((enum_as_string(tiger_pick::miny)), "miny");
    EXPECT_EQ((enum_as_string(tiger_pick::moe)), "moe");
    EXPECT_EQ(enum_as_string(tiger_pick(-1)), "-1");
    EXPECT_EQ(enum_as_string(tiger_pick(4)), "4");
  }
}

// Range of 0 to 3.
enum class e0_3 : int8_t {};

template<>
constexpr auto registry::enum_spec_v<e0_3> =
    make_sequence_enum_spec<e0_3, "a, *, c, ?", wrapclip::limit>();

// Range of 10 to 13. Tests non-zero minimums.
enum class e10_13 : int8_t {};

template<>
constexpr auto registry::enum_spec_v<e10_13> = make_sequence_enum_spec<e10_13,
    "ten, eleven, twelve, thirteen", wrapclip::limit, e10_13{10}>();

// Range of -3 to 3. Tests negative minimums.
enum class eneg3_3 : int8_t {};

template<>
constexpr auto registry::enum_spec_v<eneg3_3> = make_sequence_enum_spec<
    eneg3_3, "neg-three, neg-two, neg-one, zero, one, two, three",
    wrapclip::limit, eneg3_3{-3}>();

// Range of 0 to 255. Tests exact fit, unsigned. Enabling wrap has no effect.
enum class e0_255 : uint8_t {};

template<>
constexpr auto registry::enum_spec_v<e0_255> =
    make_sequence_enum_spec<e0_255, e0_255{255}>();

// Range of -128 to 127. Tests exact fit, signed.
enum class eneg128_127 : int8_t {};

template<>
constexpr auto registry::enum_spec_v<eneg128_127> = make_sequence_enum_spec<
    eneg128_127, eneg128_127{127}, eneg128_127{-128}>();

void SequentialEnumTest_MakeSafely() {
  if (true) {
    e0_3 e;
    e = make_safely<e0_3>(0);
    EXPECT_EQ(*e, 0);
    e = make_safely<e0_3>(1);
    EXPECT_EQ(*e, 1);
    e = make_safely<e0_3>(3);
    EXPECT_EQ(*e, 3);

    e = make_safely<e0_3>(3 + 1);
    EXPECT_EQ(*e, 0);
    e = make_safely<e0_3>(3 + 2);
    EXPECT_EQ(*e, 1);
    e = make_safely<e0_3>(3 + 3);
    EXPECT_EQ(*e, 2);
    e = make_safely<e0_3>(3 + 4);
    EXPECT_EQ(*e, 3);
    e = make_safely<e0_3>(3 + 5);
    EXPECT_EQ(*e, 0);
    e = make_safely<e0_3>(3 + 3 * 4);
    EXPECT_EQ(*e, 3);

    // Note: All of these casts are strictly unnecessary. They're just to
    // suppress spurious warnings of precision lost due to the implicit
    // cast.
    e = make_safely<e0_3>(int8_t(0 - 1));
    EXPECT_EQ(*e, 3);
    e = make_safely<e0_3>(int8_t(0 - 2));
    EXPECT_EQ(*e, 2);
    e = make_safely<e0_3>(int8_t(0 - 3));
    EXPECT_EQ(*e, 1);
    e = make_safely<e0_3>(int8_t(0 - 4));
    EXPECT_EQ(*e, 0);
    e = make_safely<e0_3>(int8_t(0 - 5));
    EXPECT_EQ(*e, 3);
    e = make_safely<e0_3>(int8_t(0 - 4 * 4));
    EXPECT_EQ(*e, 0);
  }
  if (true) {
    using namespace strings;
    EXPECT_EQ(enum_as_string(e0_3(-1)), "-1");
    EXPECT_EQ(enum_as_string(e0_3(0)), "a");
    EXPECT_EQ(enum_as_string(e0_3(1)), "1");
    EXPECT_EQ(enum_as_string(e0_3(2)), "c");
    EXPECT_EQ(enum_as_string(e0_3(3)), "3");
    EXPECT_EQ(enum_as_string(e0_3(4)), "4");
  }
  if (true) {
    e10_13 e;
    e = make_safely<e10_13>(10);
    EXPECT_EQ(*e, 10);
    e = make_safely<e10_13>(11);
    EXPECT_EQ(*e, 11);
    e = make_safely<e10_13>(13);
    EXPECT_EQ(*e, 13);

    e = make_safely<e10_13>(13 + 1);
    EXPECT_EQ(*e, 10);
    e = make_safely<e10_13>(13 + 2);
    EXPECT_EQ(*e, 11);
    e = make_safely<e10_13>(13 + 3);
    EXPECT_EQ(*e, 12);
    e = make_safely<e10_13>(13 + 4);
    EXPECT_EQ(*e, 13);
    e = make_safely<e10_13>(13 + 5);
    EXPECT_EQ(*e, 10);
    e = make_safely<e10_13>(13 + 3 * 4);
    EXPECT_EQ(*e, 13);

    e = make_safely<e10_13>(10 - 1);
    EXPECT_EQ(*e, 13);
    e = make_safely<e10_13>(10 - 2);
    EXPECT_EQ(*e, 12);
    e = make_safely<e10_13>(10 - 3);
    EXPECT_EQ(*e, 11);
    e = make_safely<e10_13>(10 - 4);
    EXPECT_EQ(*e, 10);
    e = make_safely<e10_13>(10 - 5);
    EXPECT_EQ(*e, 13);
    e = make_safely<e10_13>(int8_t(10 - 4 * 4));
    EXPECT_EQ(*e, 10);
  }
  if (true) {
    using namespace strings;
    EXPECT_EQ(enum_as_string(e10_13(-1)), "-1");
    EXPECT_EQ(enum_as_string(e10_13(9)), "9");
    EXPECT_EQ(enum_as_string(e10_13(10)), "ten");
    EXPECT_EQ(enum_as_string(e10_13(11)), "eleven");
    EXPECT_EQ(enum_as_string(e10_13(12)), "twelve");
    EXPECT_EQ(enum_as_string(e10_13(13)), "thirteen");
    EXPECT_EQ(enum_as_string(e10_13(14)), "14");
  }
  if (true) {
    eneg3_3 e;
    e = make_safely<eneg3_3>(0);
    EXPECT_EQ(*e, 0);
    e = make_safely<eneg3_3>(-3);
    EXPECT_EQ(*e, -3);
    e = make_safely<eneg3_3>(3);
    EXPECT_EQ(*e, 3);

    e = make_safely<eneg3_3>(3 + 1);
    EXPECT_EQ(*e, -3);
    e = make_safely<eneg3_3>(3 + 2);
    EXPECT_EQ(*e, -2);
    e = make_safely<eneg3_3>(3 + 3);
    EXPECT_EQ(*e, -1);
    e = make_safely<eneg3_3>(3 + 4);
    EXPECT_EQ(*e, 0);
    e = make_safely<eneg3_3>(3 + 5);
    EXPECT_EQ(*e, 1);
    e = make_safely<eneg3_3>(3 + 6);
    EXPECT_EQ(*e, 2);
    e = make_safely<eneg3_3>(3 + 7);
    EXPECT_EQ(*e, 3);
    e = make_safely<eneg3_3>(3 + 8);
    EXPECT_EQ(*e, -3);
    e = make_safely<eneg3_3>(3 + 7 * 4);
    EXPECT_EQ(*e, 3);

    e = make_safely<eneg3_3>(-3 - 1);
    EXPECT_EQ(*e, 3);
    e = make_safely<eneg3_3>(-3 - 2);
    EXPECT_EQ(*e, 2);
    e = make_safely<eneg3_3>(-3 - 3);
    EXPECT_EQ(*e, 1);
    e = make_safely<eneg3_3>(-3 - 4);
    EXPECT_EQ(*e, 0);
    e = make_safely<eneg3_3>(-3 - 5);
    EXPECT_EQ(*e, -1);
    e = make_safely<eneg3_3>(-3 - 6);
    EXPECT_EQ(*e, -2);
    e = make_safely<eneg3_3>(-3 - 7);
    EXPECT_EQ(*e, -3);
    e = make_safely<eneg3_3>(-3 - 8);
    EXPECT_EQ(*e, 3);
    e = make_safely<eneg3_3>(-3 - 7 * 4);
    EXPECT_EQ(*e, -3);
  }
  if (true) {
    using namespace strings;
    EXPECT_EQ(enum_as_string(eneg3_3(-4)), "-4");
    EXPECT_EQ(enum_as_string(eneg3_3(-3)), "neg-three");
    EXPECT_EQ(enum_as_string(eneg3_3(-2)), "neg-two");
    EXPECT_EQ(enum_as_string(eneg3_3(-1)), "neg-one");
    EXPECT_EQ(enum_as_string(eneg3_3(0)), "zero");
    EXPECT_EQ(enum_as_string(eneg3_3(1)), "one");
    EXPECT_EQ(enum_as_string(eneg3_3(2)), "two");
    EXPECT_EQ(enum_as_string(eneg3_3(3)), "three");
    EXPECT_EQ(enum_as_string(eneg3_3(4)), "4");
  }
  if (true) {
    e0_255 e;

    e = make_safely<e0_255>(0);
    EXPECT_EQ(*e, 0);
    e = make_safely<e0_255>(uint8_t(256));
    EXPECT_EQ(*e, 0);
    e = make_safely<e0_255>(uint8_t(257));
    EXPECT_EQ(*e, 1);

    e = make_safely<e0_255>(uint8_t(0 - 2));
    EXPECT_EQ(*e, 254);
    e = make_safely<e0_255>(uint8_t(255 + 3));
    EXPECT_EQ(*e, 2);
    e = make_safely<e0_255>(uint8_t(255 + 3 * 256));
    EXPECT_EQ(*e, 255);

    // Safety is meaningless in this case.
    e = make<e0_255>(0);
    EXPECT_EQ(*e, 0);
    e = make<e0_255>(uint8_t(256));
    EXPECT_EQ(*e, 0);
    e = make<e0_255>(uint8_t(257));
    EXPECT_EQ(*e, 1);

    e = make<e0_255>(uint8_t(0 - 2));
    EXPECT_EQ(*e, 254);
    e = make<e0_255>(uint8_t(255 + 3));
    EXPECT_EQ(*e, 2);
    e = make<e0_255>(uint8_t(255 + 3 * 256));
    EXPECT_EQ(*e, 255);
  }
  if (true) {
    eneg128_127 e;

    e = make_safely<eneg128_127>(0);
    EXPECT_EQ(*e, 0);
    e = make_safely<eneg128_127>(int8_t(128));
    EXPECT_EQ(*e, -128);
    e = make_safely<eneg128_127>(int8_t(-129));
    EXPECT_EQ(*e, 127);

    e = make_safely<eneg128_127>(int8_t(-128 - 2));
    EXPECT_EQ(*e, 126);
    e = make_safely<eneg128_127>(int8_t(127 + 3));
    EXPECT_EQ(*e, -126);
    e = make_safely<eneg128_127>(int8_t(127 + 3 * 256));
    EXPECT_EQ(*e, 127);

    // Safety is meaningless in this case.
    e = make<eneg128_127>(0);
    EXPECT_EQ(*e, 0);
    e = make<eneg128_127>(int8_t(128));
    EXPECT_EQ(*e, -128);
    e = make<eneg128_127>(int8_t(-129));
    EXPECT_EQ(*e, 127);

    e = make<eneg128_127>(int8_t(-128 - 2));
    EXPECT_EQ(*e, 126);
    e = make<eneg128_127>(int8_t(127 + 3));
    EXPECT_EQ(*e, -126);
    e = make<eneg128_127>(int8_t(127 + 3 * 256));
    EXPECT_EQ(*e, 127);
  }
}

void SequentialEnumTest_SafeOps() {
  if (true) {
    e0_3 e;
    e = make<e0_3>(0);
    EXPECT_EQ(*e, 0);
    --e;
    EXPECT_EQ(*e, 3);
    ++e;
    EXPECT_EQ(*e, 0);
    e += 4;
    EXPECT_EQ(*e, 0);
    e += 4 * 4;
    EXPECT_EQ(*e, 0);
    e -= 4;
    EXPECT_EQ(*e, 0);
    e -= 4 * 4;
    EXPECT_EQ(*e, 0);
  }
}

enum class tiger_nochoice { tiger };

template<>
constexpr auto registry::enum_spec_v<tiger_nochoice> =
    make_sequence_enum_spec<tiger_nochoice, tiger_nochoice{}>();

void SequentialEnumTest_NoChoice() {
  if (true) {
    auto e = tiger_nochoice::tiger;
    auto n = *e;
  }
}

// Range of 0 to 3, but without wrapping.
enum class e0_3unsafe : int8_t {};

template<>
constexpr auto registry::enum_spec_v<e0_3unsafe> =
    make_sequence_enum_spec<e0_3unsafe, e0_3unsafe{3}>();

void SequentialEnumTest_SubtleBugRepro() {
  e0_3unsafe e;

  // This is a regression test now.
  e = make_safely<e0_3unsafe>(36);
  EXPECT_EQ(*e, 0);
  EXPECT_NE(*e, 36);
}

void SequentialEnumTest_StreamingOut() {
  EXPECT_TRUE(OStreamable<tiger_pick>);
  if (true) {
    std::stringstream ss;
    ss << *tiger_pick::moe << std::flush;
    EXPECT_EQ(ss.str(), "3");
  }

  if (true) {
    std::stringstream ss;
    ss << tiger_pick::moe << std::flush;
    EXPECT_EQ(ss.str(), "moe");
  }
}

enum class tiger_missing { eeny, miny = 2, moe };

template<>
constexpr auto registry::enum_spec_v<tiger_missing> =
    make_sequence_enum_spec<tiger_missing, "eeny, - , miny   , moe">();

void SequentialEnumTest_Missing() {
  if (true) {
    using namespace strings;
    EXPECT_EQ((enum_as_string(tiger_missing::eeny)), "eeny");
    EXPECT_EQ(enum_as_string(tiger_missing(1)), "1");
    EXPECT_EQ((enum_as_string(tiger_missing::miny)), "miny");
    EXPECT_EQ((enum_as_string(tiger_missing::moe)), "moe");
    EXPECT_EQ(enum_as_string(tiger_missing(-1)), "-1");
    EXPECT_EQ(enum_as_string(tiger_missing(4)), "4");
  }
}

void SequentialEnumTest_Intervals() {
  if (true) {
    int c{}, s{};
    for (auto e : make_interval<e0_3>()) {
      ++c;
      s += *e;
    }
    EXPECT_EQ(c, 4);
    EXPECT_EQ(s, 0 + 1 + 2 + 3);
  }
  if (true) {
    int c{}, s{};
    for (auto e : make_interval<e10_13>()) {
      ++c;
      s += *e;
    }
    EXPECT_EQ(c, 4);
    EXPECT_EQ(s, 10 + 11 + 12 + 13);
  }
  if (true) {
    int c{}, s{};
    // This fails with an assertion.
    // * auto bad = make_interval<e0_255>();
    for (auto e : make_interval<e0_255, uint16_t>()) {
      ++c;
      s += *e;
    }
    EXPECT_EQ(c, 256);
    EXPECT_EQ(s, 32640);
  }
  if (true) {
    int c{}, s{};
    // This fails with an assertion.
    // * auto bad = make_interval<eneg128_127>();
    for (auto e : make_interval<eneg128_127, int16_t>()) {
      ++c;
      s += *e;
    }
    EXPECT_EQ(c, 256);
    EXPECT_EQ(s, -128);
  }
}

MAKE_TEST_LIST(SequentialEnumTest_Registry, SequentialEnumTest_Ops,
    SequentialEnumTest_MakeSafely, SequentialEnumTest_SafeOps,
    SequentialEnumTest_SubtleBugRepro, SequentialEnumTest_StreamingOut,
    SequentialEnumTest_Missing, SequentialEnumTest_Intervals);
