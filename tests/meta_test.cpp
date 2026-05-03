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

#include "../corvid/meta.h"
#include "minitest.h"

#include <map>
#include <memory>
#include <set>

// #include "Interval.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

// OStreamDerived

auto& stream_out(OStreamDerived auto& os, const OStreamable auto& osb) {
  return os << osb;
}

void MetaTest_OStreamdDerived() {
  std::ostringstream oss;
  stream_out(oss, 1);
  EXPECT_EQ(oss.str(), "1");
#ifdef NOT_SUPPOSED_TO_COMPILE
  std::string s{"Hello"};
  foo(s, 42);
  stream_out(oss, oss);
#endif
}

void MetaTest_EnumBitWidth() {
  EXPECT_EQ(std::bit_width(0ULL), 0);
  EXPECT_EQ(std::bit_width(1ULL), 1);
  EXPECT_EQ(std::bit_width(2ULL), 2);
  EXPECT_EQ(std::bit_width(3ULL), 2);
  EXPECT_EQ(std::bit_width(4ULL), 3);
  EXPECT_EQ(std::bit_width(7ULL), 3);
  EXPECT_EQ(std::bit_width(8ULL), 4);
  EXPECT_EQ(std::bit_width(15ULL), 4);
  EXPECT_EQ(std::bit_width(16ULL), 5);
  EXPECT_EQ(std::bit_width(31ULL), 5);
  EXPECT_EQ(std::bit_width(32ULL), 6);
  EXPECT_EQ(std::bit_width(63ULL), 6);
  EXPECT_EQ(std::bit_width(64ULL), 7);
  EXPECT_EQ(std::bit_width(127ULL), 7);
  EXPECT_EQ(std::bit_width(128ULL), 8);
  EXPECT_EQ(std::bit_width(255ULL), 8);
  EXPECT_EQ(std::bit_width(256ULL), 9);
  EXPECT_EQ(std::bit_width(511ULL), 9);
  EXPECT_EQ(std::bit_width(512ULL), 10);
  EXPECT_EQ(std::bit_width(1023ULL), 10);
  EXPECT_EQ(std::bit_width(1024ULL), 11);
  EXPECT_EQ(std::bit_width(2047ULL), 11);
  EXPECT_EQ(std::bit_width(2048ULL), 12);
  EXPECT_EQ(std::bit_width(4095ULL), 12);
  EXPECT_EQ(std::bit_width(4096ULL), 13);
  EXPECT_EQ(std::bit_width(8191ULL), 13);
  EXPECT_EQ(std::bit_width(8192ULL), 14);
  EXPECT_EQ(std::bit_width(16383ULL), 14);
  EXPECT_EQ(std::bit_width(16384ULL), 15);
  EXPECT_EQ(std::bit_width(32767ULL), 15);
  EXPECT_EQ(std::bit_width(32768ULL), 16);
  EXPECT_EQ(std::bit_width(65535ULL), 16);
  EXPECT_EQ(std::bit_width(65536ULL), 17);
  EXPECT_EQ(std::bit_width(131071ULL), 17);
  EXPECT_EQ(std::bit_width(131072ULL), 18);
  EXPECT_EQ(std::bit_width(262143ULL), 18);
  EXPECT_EQ(std::bit_width(262144ULL), 19);
  EXPECT_EQ(std::bit_width(524287ULL), 19);
  EXPECT_EQ(std::bit_width(524288ULL), 20);
  EXPECT_EQ(std::bit_width(1048575ULL), 20);
  EXPECT_EQ(std::bit_width(1048576ULL), 21);
  EXPECT_EQ(std::bit_width(2097151ULL), 21);
  EXPECT_EQ(std::bit_width(2097152ULL), 22);
  EXPECT_EQ(std::bit_width(4194303ULL), 22);
  EXPECT_EQ(std::bit_width(4194304ULL), 23);
  EXPECT_EQ(std::bit_width(8388607ULL), 23);
  EXPECT_EQ(std::bit_width(8388608ULL), 24);
  EXPECT_EQ(std::bit_width(16777215ULL), 24);
  EXPECT_EQ(std::bit_width(16777216ULL), 25);
  EXPECT_EQ(std::bit_width(33554431ULL), 25);
  EXPECT_EQ(std::bit_width(33554432ULL), 26);
  EXPECT_EQ(std::bit_width(67108863ULL), 26);
  EXPECT_EQ(std::bit_width(67108864ULL), 27);
  EXPECT_EQ(std::bit_width(134217727ULL), 27);
  EXPECT_EQ(std::bit_width(134217728ULL), 28);
  EXPECT_EQ(std::bit_width(268435455ULL), 28);
  EXPECT_EQ(std::bit_width(268435456ULL), 29);
  EXPECT_EQ(std::bit_width(536870911ULL), 29);
  EXPECT_EQ(std::bit_width(536870912ULL), 30);
  EXPECT_EQ(std::bit_width(1073741823ULL), 30);
  EXPECT_EQ(std::bit_width(1073741824ULL), 31);
  EXPECT_EQ(std::bit_width(2147483647ULL), 31);
  EXPECT_EQ(std::bit_width(2147483648ULL), 32);
  EXPECT_EQ(std::bit_width(4294967295ULL), 32);
  EXPECT_EQ(std::bit_width(4294967296ULL), 33);
  EXPECT_EQ(std::bit_width(8589934591ULL), 33);
  EXPECT_EQ(std::bit_width(8589934592ULL), 34);
  EXPECT_EQ(std::bit_width(17179869183ULL), 34);
  EXPECT_EQ(std::bit_width(17179869184ULL), 35);
  EXPECT_EQ(std::bit_width(34359738367ULL), 35);
  EXPECT_EQ(std::bit_width(34359738368ULL), 36);
  EXPECT_EQ(std::bit_width(68719476735ULL), 36);
  EXPECT_EQ(std::bit_width(68719476736ULL), 37);
  EXPECT_EQ(std::bit_width(137438953471ULL), 37);
  EXPECT_EQ(std::bit_width(137438953472ULL), 38);
  EXPECT_EQ(std::bit_width(274877906943ULL), 38);
  EXPECT_EQ(std::bit_width(274877906944ULL), 39);
  EXPECT_EQ(std::bit_width(549755813887ULL), 39);
  EXPECT_EQ(std::bit_width(549755813888ULL), 40);
  EXPECT_EQ(std::bit_width(1099511627775ULL), 40);
  EXPECT_EQ(std::bit_width(1099511627776ULL), 41);
  EXPECT_EQ(std::bit_width(2199023255551ULL), 41);
  EXPECT_EQ(std::bit_width(2199023255552ULL), 42);
  EXPECT_EQ(std::bit_width(4398046511103ULL), 42);
  EXPECT_EQ(std::bit_width(4398046511104ULL), 43);
  EXPECT_EQ(std::bit_width(8796093022207ULL), 43);
  EXPECT_EQ(std::bit_width(8796093022208ULL), 44);
  EXPECT_EQ(std::bit_width(17592186044415ULL), 44);
  EXPECT_EQ(std::bit_width(17592186044416ULL), 45);
  EXPECT_EQ(std::bit_width(35184372088831ULL), 45);
  EXPECT_EQ(std::bit_width(35184372088832ULL), 46);
  EXPECT_EQ(std::bit_width(70368744177663ULL), 46);
  EXPECT_EQ(std::bit_width(70368744177664ULL), 47);
  EXPECT_EQ(std::bit_width(140737488355327ULL), 47);
  EXPECT_EQ(std::bit_width(140737488355328ULL), 48);
  EXPECT_EQ(std::bit_width(281474976710655ULL), 48);
  EXPECT_EQ(std::bit_width(281474976710656ULL), 49);
  EXPECT_EQ(std::bit_width(562949953421311ULL), 49);
  EXPECT_EQ(std::bit_width(562949953421312ULL), 50);
  EXPECT_EQ(std::bit_width(1125899906842623ULL), 50);
  EXPECT_EQ(std::bit_width(1125899906842624ULL), 51);
  EXPECT_EQ(std::bit_width(2251799813685247ULL), 51);
  EXPECT_EQ(std::bit_width(2251799813685248ULL), 52);
  EXPECT_EQ(std::bit_width(4503599627370495ULL), 52);
  EXPECT_EQ(std::bit_width(4503599627370496ULL), 53);
  EXPECT_EQ(std::bit_width(9007199254740991ULL), 53);
  EXPECT_EQ(std::bit_width(9007199254740992ULL), 54);
  EXPECT_EQ(std::bit_width(18014398509481983ULL), 54);
  EXPECT_EQ(std::bit_width(18014398509481984ULL), 55);
  EXPECT_EQ(std::bit_width(36028797018963967ULL), 55);
  EXPECT_EQ(std::bit_width(36028797018963968ULL), 56);
  EXPECT_EQ(std::bit_width(72057594037927935ULL), 56);
  EXPECT_EQ(std::bit_width(72057594037927936ULL), 57);
  EXPECT_EQ(std::bit_width(144115188075855871ULL), 57);
  EXPECT_EQ(std::bit_width(144115188075855872ULL), 58);
  EXPECT_EQ(std::bit_width(288230376151711743ULL), 58);
  EXPECT_EQ(std::bit_width(288230376151711744ULL), 59);
  EXPECT_EQ(std::bit_width(576460752303423487ULL), 59);
  EXPECT_EQ(std::bit_width(576460752303423488ULL), 60);
  EXPECT_EQ(std::bit_width(1152921504606846975ULL), 60);
  EXPECT_EQ(std::bit_width(1152921504606846976ULL), 61);
  EXPECT_EQ(std::bit_width(2305843009213693951ULL), 61);
  EXPECT_EQ(std::bit_width(2305843009213693952ULL), 62);
  EXPECT_EQ(std::bit_width(4611686018427387903ULL), 62);
  EXPECT_EQ(std::bit_width(4611686018427387904ULL), 63);
  EXPECT_EQ(std::bit_width(9223372036854775807ULL), 63);
  EXPECT_EQ(std::bit_width(9223372036854775808ULL), 64);
  EXPECT_EQ(std::bit_width(18446744073709551615ULL), 64);
}

void MetaTest_EnumHighestValueInNBits() {
  EXPECT_EQ(meta::highest_value_in_n_bits(0ULL), 0ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(1ULL), 1ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(2ULL), 3ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(3ULL), 7ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(4ULL), 15ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(5ULL), 31ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(6ULL), 63ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(7ULL), 127ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(8ULL), 255ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(9ULL), 511ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(10ULL), 1023ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(11ULL), 2047ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(12ULL), 4095ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(13ULL), 8191ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(14ULL), 16383ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(15ULL), 32767ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(16ULL), 65535ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(17ULL), 131071ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(18ULL), 262143ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(19ULL), 524287ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(20ULL), 1048575ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(21ULL), 2097151ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(22ULL), 4194303ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(23ULL), 8388607ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(24ULL), 16777215ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(25ULL), 33554431ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(26ULL), 67108863ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(27ULL), 134217727ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(28ULL), 268435455ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(29ULL), 536870911ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(30ULL), 1073741823ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(31ULL), 2147483647ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(32ULL), 4294967295ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(33ULL), 8589934591ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(34ULL), 17179869183ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(35ULL), 34359738367ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(36ULL), 68719476735ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(37ULL), 137438953471ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(38ULL), 274877906943ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(39ULL), 549755813887ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(40ULL), 1099511627775ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(41ULL), 2199023255551ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(42ULL), 4398046511103ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(43ULL), 8796093022207ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(44ULL), 17592186044415ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(45ULL), 35184372088831ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(46ULL), 70368744177663ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(47ULL), 140737488355327ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(48ULL), 281474976710655ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(49ULL), 562949953421311ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(50ULL), 1125899906842623ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(51ULL), 2251799813685247ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(52ULL), 4503599627370495ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(53ULL), 9007199254740991ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(54ULL), 18014398509481983ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(55ULL), 36028797018963967ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(56ULL), 72057594037927935ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(57ULL), 144115188075855871ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(58ULL), 288230376151711743ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(59ULL), 576460752303423487ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(60ULL), 1152921504606846975ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(61ULL), 2305843009213693951ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(62ULL), 4611686018427387903ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(63ULL), 9223372036854775807ULL);
  EXPECT_EQ(meta::highest_value_in_n_bits(64ULL), 18446744073709551615ULL);
}

void MetaTest_EnumPow2() {
  EXPECT_EQ(meta::pow2(0), 1ULL);
  EXPECT_EQ(meta::pow2(1), 2ULL);
  EXPECT_EQ(meta::pow2(2), 4ULL);
  EXPECT_EQ(meta::pow2(3), 8ULL);
  EXPECT_EQ(meta::pow2(4), 16ULL);
  EXPECT_EQ(meta::pow2(5), 32ULL);
  EXPECT_EQ(meta::pow2(6), 64ULL);
  EXPECT_EQ(meta::pow2(7), 128ULL);
  EXPECT_EQ(meta::pow2(8), 256ULL);
  EXPECT_EQ(meta::pow2(9), 512ULL);
  EXPECT_EQ(meta::pow2(10), 1024ULL);
  EXPECT_EQ(meta::pow2(11), 2048ULL);
  EXPECT_EQ(meta::pow2(12), 4096ULL);
  EXPECT_EQ(meta::pow2(13), 8192ULL);
  EXPECT_EQ(meta::pow2(14), 16384ULL);
  EXPECT_EQ(meta::pow2(15), 32768ULL);
  EXPECT_EQ(meta::pow2(16), 65536ULL);
  EXPECT_EQ(meta::pow2(17), 131072ULL);
  EXPECT_EQ(meta::pow2(18), 262144ULL);
  EXPECT_EQ(meta::pow2(19), 524288ULL);
  EXPECT_EQ(meta::pow2(20), 1048576ULL);
  EXPECT_EQ(meta::pow2(21), 2097152ULL);
  EXPECT_EQ(meta::pow2(22), 4194304ULL);
  EXPECT_EQ(meta::pow2(23), 8388608ULL);
  EXPECT_EQ(meta::pow2(24), 16777216ULL);
  EXPECT_EQ(meta::pow2(25), 33554432ULL);
  EXPECT_EQ(meta::pow2(26), 67108864ULL);
  EXPECT_EQ(meta::pow2(27), 134217728ULL);
  EXPECT_EQ(meta::pow2(28), 268435456ULL);
  EXPECT_EQ(meta::pow2(29), 536870912ULL);
  EXPECT_EQ(meta::pow2(30), 1073741824ULL);
  EXPECT_EQ(meta::pow2(31), 2147483648ULL);
  EXPECT_EQ(meta::pow2(32), 4294967296ULL);
  EXPECT_EQ(meta::pow2(33), 8589934592ULL);
  EXPECT_EQ(meta::pow2(34), 17179869184ULL);
  EXPECT_EQ(meta::pow2(35), 34359738368ULL);
  EXPECT_EQ(meta::pow2(36), 68719476736ULL);
  EXPECT_EQ(meta::pow2(37), 137438953472ULL);
  EXPECT_EQ(meta::pow2(38), 274877906944ULL);
  EXPECT_EQ(meta::pow2(39), 549755813888ULL);
  EXPECT_EQ(meta::pow2(40), 1099511627776ULL);
  EXPECT_EQ(meta::pow2(41), 2199023255552ULL);
  EXPECT_EQ(meta::pow2(42), 4398046511104ULL);
  EXPECT_EQ(meta::pow2(43), 8796093022208ULL);
  EXPECT_EQ(meta::pow2(44), 17592186044416ULL);
  EXPECT_EQ(meta::pow2(45), 35184372088832ULL);
  EXPECT_EQ(meta::pow2(46), 70368744177664ULL);
  EXPECT_EQ(meta::pow2(47), 140737488355328ULL);
  EXPECT_EQ(meta::pow2(48), 281474976710656ULL);
  EXPECT_EQ(meta::pow2(49), 562949953421312ULL);
  EXPECT_EQ(meta::pow2(50), 1125899906842624ULL);
  EXPECT_EQ(meta::pow2(51), 2251799813685248ULL);
  EXPECT_EQ(meta::pow2(52), 4503599627370496ULL);
  EXPECT_EQ(meta::pow2(53), 9007199254740992ULL);
  EXPECT_EQ(meta::pow2(54), 18014398509481984ULL);
  EXPECT_EQ(meta::pow2(55), 36028797018963968ULL);
  EXPECT_EQ(meta::pow2(56), 72057594037927936ULL);
  EXPECT_EQ(meta::pow2(57), 144115188075855872ULL);
  EXPECT_EQ(meta::pow2(58), 288230376151711744ULL);
  EXPECT_EQ(meta::pow2(59), 576460752303423488ULL);
  EXPECT_EQ(meta::pow2(60), 1152921504606846976ULL);
  EXPECT_EQ(meta::pow2(61), 2305843009213693952ULL);
  EXPECT_EQ(meta::pow2(62), 4611686018427387904ULL);
  EXPECT_EQ(meta::pow2(63), 9223372036854775808ULL);
  EXPECT_EQ(meta::pow2(64), 0ULL);
}

void MetaTest_SpanConstness() {
  EXPECT_TRUE((Span<std::span<char>, char>));
  EXPECT_TRUE((Span<std::span<char>, const char>));
  EXPECT_FALSE((Span<std::span<const char>, char>));
  EXPECT_TRUE((Span<std::span<const char>, const char>));
}

void MetaTest_FunctionVoidReturn() {
  using FNV0 = std::function<void()>;
  using FNV1 = std::function<void(int)>;
  using FNI0 = std::function<int()>;
  using FNI1 = std::function<int(int)>;

  EXPECT_TRUE((CallableReturningVoid<FNV0>));
  EXPECT_TRUE((CallableReturningVoid<FNV1, int>));
  EXPECT_FALSE((CallableReturningVoid<FNI0>));
  EXPECT_FALSE((CallableReturningVoid<FNI1, int>));

  EXPECT_FALSE((CallableReturningNonVoid<FNV0>));
  EXPECT_FALSE((CallableReturningNonVoid<FNV1, int>));
  EXPECT_TRUE((CallableReturningNonVoid<FNI0>));
  EXPECT_TRUE((CallableReturningNonVoid<FNI1, int>));
}

// Helper types for specialization tests
struct Foo {};

template<typename T>
struct Goo {};

void MetaTest_Specialization() {
  EXPECT_TRUE((is_specialization_of_v<std::vector<int>, std::vector>));
  EXPECT_FALSE((is_specialization_of_v<std::vector<int>, std::map>));
  EXPECT_FALSE((is_specialization_of_v<int, std::map>));
  EXPECT_FALSE((is_specialization_of_v<int, Goo>));
  EXPECT_TRUE((is_specialization_of_v<Goo<int>, Goo>));

  // Note: These would fail to compile:
  // - is_specialization_of_v<int, char> (char is not a template)
  // - is_specialization_of_v<std::array<int, 4>, std::array> (non-type params)
}

void MetaTest_PointerElement() {
  EXPECT_TRUE((std::is_same_v<int, pointer_element_t<int*>>));
  EXPECT_TRUE((std::is_same_v<int, pointer_element_t<std::unique_ptr<int>>>));
  EXPECT_TRUE((std::is_same_v<void, pointer_element_t<int>>));
}

void MetaTest_Dereferenceable() {
  EXPECT_TRUE((Dereferenceable<int*>));
  EXPECT_TRUE((Dereferenceable<std::unique_ptr<int>>));
  EXPECT_FALSE((Dereferenceable<int>));
  EXPECT_TRUE((Dereferenceable<decltype(std::optional<int>())>));
}

void MetaTest_IsPair() {
  EXPECT_TRUE((is_pair_v<std::pair<int, int>>));
  EXPECT_FALSE((is_pair_v<std::tuple<int, int>>));
  EXPECT_FALSE((is_pair_v<int>));

  // PairConvertible concept replaces the old is_pair_like_v trait.
  // Note: std::tuple<F, S> should be usable to construct std::pair<F, S>,
  // though some standard libraries do not model that as an implicit
  // conversion.
  EXPECT_TRUE((PairConvertible<std::pair<int, int>>));
  EXPECT_TRUE(
      (PairConvertible<std::tuple<int, int>>)); // tuple<2> is pair-convertible
  EXPECT_FALSE((PairConvertible<int>));

  // Test with type aliases and cv-qualifiers
  using T = std::pair<int, int>;
  EXPECT_TRUE((PairConvertible<T>));
  using U = const std::pair<int, int>&;
  EXPECT_TRUE((PairConvertible<U>));
  using V = std::pair<int, int>&;
  EXPECT_TRUE((PairConvertible<V>));
  using W = const std::pair<int, int>;
  EXPECT_TRUE((PairConvertible<W>));

  // Note: Tests with intervals::interval skipped (requires Interval.h)
}

void MetaTest_ContainerElement() {
  // Test with pair - extracts the second element (value)
  {
    std::pair<int, int> kv{1, 2};
    auto p = &kv;
    EXPECT_EQ(container_element_v(p), 2);
  }

  // Test with plain value - returns the value itself
  {
    int v{2};
    auto p = &v;
    EXPECT_EQ(container_element_v(p), 2);
  }

  // Test with string element pointer
  {
    std::string s{"abc"};
    EXPECT_EQ(container_element_v(&s[1]), 'b');
  }
}

void MetaTest_KeyFind() {
  // has_key_find_v checks if container has find(key_type) method
  using M = std::map<int, Foo>;
  EXPECT_TRUE((has_key_find_v<M>));

  using S = std::set<Foo>;
  EXPECT_TRUE((has_key_find_v<S>));

  using V = std::vector<int>;
  EXPECT_FALSE((has_key_find_v<V>));

  // Note: Old two-parameter version (checking specific key type compatibility)
  // and find_ret_t have been removed from the API
}

void MetaTest_TypeName() {
  using T = std::string;
  using U = const std::string;
  using V = std::string&;
  using W = const std::string&;

  // Same types have same names
  EXPECT_EQ(type_name<T>(), type_name<T>());

  // Different cv-qualifiers produce different names
  EXPECT_NE(type_name<T>(), type_name<U>());
  EXPECT_NE(type_name<U>(), type_name<V>());
  EXPECT_NE(type_name<V>(), type_name<W>());

  // Value-based overload matches type-based version
  EXPECT_EQ(type_name<T>(), type_name(T{}));
}

void MetaTest_StringViewConvertible() {
  // StringViewConvertible concept (replaces is_string_view_convertible_v)
  EXPECT_TRUE((StringViewConvertible<std::string_view>));
  EXPECT_TRUE((StringViewConvertible<std::string>));
  EXPECT_TRUE((StringViewConvertible<char*>));
  EXPECT_TRUE((StringViewConvertible<char[]>));
  EXPECT_FALSE((StringViewConvertible<int>));
  EXPECT_FALSE((StringViewConvertible<std::nullptr_t>));
  EXPECT_TRUE((StringViewConvertible<std::string&>));
  EXPECT_TRUE((StringViewConvertible<std::string&&>));

  // Range concept (replaces can_ranged_for_v)
  EXPECT_FALSE((Range<int>));
  EXPECT_TRUE((Range<std::vector<int>>));
  EXPECT_TRUE((Range<std::string>));
  EXPECT_TRUE((Range<int[4]>));
  EXPECT_TRUE((Range<char[4]>));
  EXPECT_FALSE((Range<char*>));

  // Container concept (replaces is_container_v)
  EXPECT_FALSE((Container<int>));
  EXPECT_TRUE((Container<std::vector<int>>));
  EXPECT_FALSE((Container<std::string>)); // Excluded (StringViewConvertible)
  EXPECT_TRUE((Container<std::array<int, 2>>));
  EXPECT_TRUE((Container<int[4]>));
  EXPECT_FALSE((Container<char[4]>)); // Excluded (StringViewConvertible)
  EXPECT_FALSE((Container<char*>));
}

void MetaTest_Number() {
  // Integer concept (integral excluding bool)
  EXPECT_TRUE((Integer<char>));
  EXPECT_TRUE((Integer<int>));
  EXPECT_FALSE((Integer<float>));
  EXPECT_FALSE((Integer<double>));

  // Floating point check
  EXPECT_TRUE((std::floating_point<float>));
  EXPECT_TRUE((std::floating_point<double>));

  // std::byte is an enum, not arithmetic
  EXPECT_FALSE((Integer<std::byte>));
  EXPECT_TRUE(std::is_enum_v<std::byte>);
  EXPECT_FALSE(std::is_enum_v<const std::byte&>);
  EXPECT_TRUE((StdEnum<const std::byte&>)); // StdEnum strips cvref

  // Arithmetic checks
  EXPECT_TRUE(std::is_arithmetic_v<int>);
  EXPECT_FALSE(std::is_arithmetic_v<int&>);

  // Bool is arithmetic but not Integer
  EXPECT_TRUE(std::is_arithmetic_v<bool>);
  EXPECT_FALSE((Integer<bool>));
  EXPECT_TRUE((is_bool_v<bool>));

  enum ColorEnum { red, green = 20, blue };
  enum class ColorClass { red, green = 20, blue };

  EXPECT_TRUE((StdEnum<ColorClass>));
  EXPECT_TRUE((StdEnum<ColorEnum>));

  // Note: is_number_v (arithmetic excluding bool) no longer exists
  // Use Integer for integral types or std::floating_point for floats
}

void MetaTest_Tuple() {
  using T0 = std::tuple<>;
  using T2 = std::tuple<int, int>;
  using PI = std::pair<int, int>;
  using I2 = std::array<int, 2>;

  // std::tuple_size works for tuples, pairs, and arrays
  EXPECT_EQ(std::tuple_size_v<T2>, 2);
  EXPECT_EQ(std::tuple_size_v<T0>, 0);
  EXPECT_EQ(std::tuple_size_v<PI>, 2);
  EXPECT_EQ(std::tuple_size_v<I2>, 2);

  // is_std_array_v trait
  EXPECT_TRUE((is_std_array_v<I2>));
  EXPECT_FALSE((is_std_array_v<T2>));

  // TupleLike concept (replaces is_tuple_like_v)
  // Note: TupleLike = StdTuple || PairConvertible (excludes array)
  EXPECT_FALSE((TupleLike<I2>)); // array is NOT tuple-like
  EXPECT_FALSE((TupleLike<std::string>));

  // is_tuple_v trait
  EXPECT_FALSE((is_tuple_v<int>));
  EXPECT_TRUE((is_tuple_v<T0>));
  EXPECT_TRUE((is_tuple_v<T2>));
  EXPECT_FALSE((is_tuple_v<PI>));
  EXPECT_TRUE((TupleLike<PI>)); // pair is tuple-like
}

void MetaTest_Detection() {
  // initializer_list detection
  {
    auto il = {1, 2, 3};
    EXPECT_TRUE((is_initializer_list_v<decltype(il)>));
  }

  // variant detection
  {
    std::variant<int, float> va = 42;
    EXPECT_TRUE((is_variant_v<decltype(va)>));
  }

  // OptionalLike concept (replaces is_optional_like_v)
  {
    EXPECT_TRUE((OptionalLike<std::optional<int>>));
    EXPECT_TRUE((OptionalLike<int*>));
    EXPECT_FALSE((OptionalLike<void*>)); // void* not dereferenceable to value
    EXPECT_FALSE((OptionalLike<const char*>)); // string-like
  }

  // char pointer detection
  {
    EXPECT_TRUE((is_char_ptr_v<char*>));
    EXPECT_TRUE((is_char_ptr_v<const char*>));
    EXPECT_TRUE((is_char_ptr_v<char[]>));
    EXPECT_TRUE((is_char_ptr_v<const char[]>));
    EXPECT_TRUE((is_char_ptr_v<char* const>));
    EXPECT_TRUE((is_char_ptr_v<const char* const>));
    EXPECT_FALSE((is_char_ptr_v<void*>));
    EXPECT_FALSE((is_char_ptr_v<int*>));
    EXPECT_FALSE((is_char_ptr_v<char>));
    const char* psz{};
    EXPECT_TRUE((is_char_ptr_v<decltype(psz)>));
  }

  // Note: is_void_ptr_v trait no longer exists
}

void MetaTest_Underlying() {
  enum class X : size_t { x1 = 1, x2 };
  enum class Y : int64_t { ylow = -1 };
  enum Z { z1 = 1 };

  // as_underlying converts scoped enum to underlying type
  auto x = as_underlying(X::x1);
  EXPECT_EQ(x, 1UL);
  EXPECT_TRUE((std::is_same_v<size_t, decltype(x)>));

  auto y = as_underlying(Y::ylow);
  EXPECT_EQ(y, -1);
  EXPECT_TRUE((std::is_same_v<int64_t, decltype(y)>));

  // as_underlying works for unscoped enums too
  auto z0 = Z::z1;
  auto z = as_underlying(z1);
  EXPECT_EQ(z0, 1U);
  EXPECT_EQ(z, 1U);
  // Unscoped enum has unsigned int as underlying type
  EXPECT_TRUE((std::is_same_v<unsigned int, decltype(z)>));
}

void MetaTest_Streamable() {
  // OStreamable concept (replaces can_stream_out_v)
  EXPECT_TRUE((OStreamable<int>));
  EXPECT_FALSE((OStreamable<Foo>));
}

void MetaTest_MaybeTypes() {
  EXPECT_TRUE((std::is_empty_v<empty_t>));
  EXPECT_TRUE((std::is_same_v<maybe_t<int, true>, int>));
  EXPECT_TRUE((std::is_same_v<maybe_t<int, false>, empty_t>));

  EXPECT_TRUE((std::is_same_v<maybe_void_t<int>, int>));
  EXPECT_TRUE((std::is_same_v<maybe_void_t<void>, empty_t>));
  EXPECT_TRUE((std::is_same_v<maybe_void_t<>, empty_t>));

  struct NoExtraSpace {
    [[no_unique_address]] maybe_t<int, false> maybe{42};
    int value{};
  };
  struct Baseline {
    int value{};
  };
  EXPECT_EQ(sizeof(NoExtraSpace), sizeof(Baseline));
}

// address_forwarder

struct Trackable: public address_forwarder<Trackable> {
  int value{};
  explicit Trackable(int v) : value{v} {}
  friend std::ostream& operator<<(std::ostream& os, const Trackable& t) {
    return os << "Trackable{" << t.value << "}";
  }
};

static_assert(AddressForwarder<Trackable>);
static_assert(!AddressForwarder<int>);

void MetaTest_AddressForwarder_Basic() {
  Trackable t{42};
  EXPECT_TRUE(t.is_valid());
  EXPECT_EQ(t.forwarding_address(), nullptr);
}

void MetaTest_AddressForwarder_Track() {
  Trackable* ptr{};
  {
    Trackable t{7};
    ptr = &t;                      // set initial value manually
    t.forwarding_address() = &ptr; // register for future updates
    EXPECT_EQ(ptr, &t);
  }
  // Destruction writes nullptr through the registered pointer.
  EXPECT_EQ(ptr, nullptr);
}

void MetaTest_AddressForwarder_MoveConstruct() {
  Trackable* ptr{};
  Trackable a{1};
  ptr = &a;
  a.forwarding_address() = &ptr;
  EXPECT_EQ(ptr, &a);

  Trackable b{std::move(a)};
  // Move construction updates `ptr` to the new location.
  EXPECT_EQ(ptr, &b);
  // Source no longer holds the forwarding address slot.
  EXPECT_EQ(a.forwarding_address(), nullptr);
}

void MetaTest_AddressForwarder_MoveAssign() {
  Trackable* ptr{};
  Trackable a{2};
  ptr = &a;
  a.forwarding_address() = &ptr;
  EXPECT_EQ(ptr, &a);

  Trackable b{99};
  b = std::move(a);
  EXPECT_EQ(ptr, &b);
  EXPECT_EQ(a.forwarding_address(), nullptr);

  // Clearing the forwarding address on `b` stops future tracking, but does
  // not touch the external `ptr` variable itself.
  b.forwarding_address() = nullptr;
  EXPECT_EQ(ptr, &b);
}

void MetaTest_AddressForwarder_SelfAssign() {
  Trackable* ptr{};
  Trackable a{3};
  ptr = &a;
  a.forwarding_address() = &ptr;

  // Self-assignment must not corrupt state.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
  a = std::move(a);
#pragma clang diagnostic pop
  // After self-move `ptr` still points to `a`.
  EXPECT_EQ(ptr, &a);
}

void MetaTest_AddressForwarder_DestroySource() {
  Trackable* ptr{};
  Trackable b{0};
  {
    Trackable a{5};
    ptr = &a;
    a.forwarding_address() = &ptr;
    b = std::move(a);
    // `a` no longer owns the slot; destroying it must not null `ptr`.
  }
  EXPECT_EQ(ptr, &b);
  b.forwarding_address() = nullptr;
}

// Derived type with a custom move constructor that uses `as_base_move()`.
struct Trackable2: public address_forwarder<Trackable2> {
  int value{};
  explicit Trackable2(int v) : value{v} {}
  Trackable2(Trackable2&& o) noexcept
      : address_forwarder{o.as_base_move()}, value{o.value} {}
  Trackable2& operator=(Trackable2&&) = default;
  friend std::ostream& operator<<(std::ostream& os, const Trackable2& t) {
    return os << "Trackable2{" << t.value << "}";
  }
};

void MetaTest_AddressForwarder_AsBaseMove() {
  Trackable2* ptr{};
  Trackable2 a{8};
  ptr = &a;
  a.forwarding_address() = &ptr;
  EXPECT_EQ(ptr, &a);

  Trackable2 b{std::move(a)};
  EXPECT_EQ(ptr, &b);
  EXPECT_EQ(a.forwarding_address(), nullptr);
}

void MetaTest_AddressForwarder_BoundFunction() {
  // Primary use case: an object is moved into a `std::function` closure, and
  // a pointer registered before the move chain tracks it to its final home.
  Trackable* ptr{};
  Trackable t{99};
  t.forwarding_address() = &ptr;
  // ptr is still null here; each move in the chain below will update it.

  std::function<int()> fn = [t = std::move(t)]() mutable { return t.value; };

  // ptr now points to `t` inside fn's internal storage.
  EXPECT_NE(ptr, nullptr);
  EXPECT_EQ(ptr->value, 99);

  // fn() and ptr refer to the same object.
  EXPECT_EQ(fn(), 99);
  ptr->value = 42;
  EXPECT_EQ(fn(), 42);

  // Clear forwarding so fn's destructor does not write through the soon-to-be
  // dangling &ptr.
  ptr->forwarding_address() = nullptr;
}

// fixed_function compile-time size checks
static_assert(sizeof(fixed_function<64, int()>) == 64);
static_assert(sizeof(fixed_function<32, void(int, double)>) == 32);
static_assert(sizeof(fixed_function<128, int(int, int)>) == 128);

void MetaTest_FixedFunction_Basic() {
  fixed_function<64, int()> f{[] { return 42; }};
  EXPECT_EQ(f(), 42);
}

void MetaTest_FixedFunction_Args() {
  fixed_function<64, int(int, int)> add{[](int x, int y) { return x + y; }};
  EXPECT_EQ(add(3, 4), 7);
  EXPECT_EQ(add(10, -3), 7);
}

void MetaTest_FixedFunction_Bool() {
  fixed_function<64, int()> a{[] { return 1; }};
  EXPECT_TRUE(static_cast<bool>(a));
  fixed_function<64, int()> b{std::move(a)};
  EXPECT_FALSE(static_cast<bool>(a));
  EXPECT_TRUE(static_cast<bool>(b));
}

void MetaTest_FixedFunction_Move() {
  fixed_function<64, int()> a{[] { return 7; }};
  EXPECT_TRUE(static_cast<bool>(a));
  fixed_function<64, int()> b{std::move(a)};
  EXPECT_FALSE(static_cast<bool>(a));
  EXPECT_TRUE(static_cast<bool>(b));
  EXPECT_EQ(b(), 7);
}

void MetaTest_FixedFunction_MoveAssign() {
  fixed_function<64, int()> a{[] { return 99; }};
  fixed_function<64, int()> b{[] { return 0; }};
  b = std::move(a);
  EXPECT_FALSE(static_cast<bool>(a));
  EXPECT_TRUE(static_cast<bool>(b));
  EXPECT_EQ(b(), 99);

  b = nullptr;
  EXPECT_FALSE(static_cast<bool>(b));

  // Sizer test.
  struct Foo {
    int value{};
    char sz[48];
    explicit Foo(int v) : value{v} {}
    int operator()() const { return value; }
  };

  fixed_function_sizer<68, int()> c{Foo{42}};
  EXPECT_EQ(c.required, 68U);
}

void MetaTest_FixedFunction_Destructor() {
  // `Counted` does not null `count_` on move, so every `~Counted()` call
  // increments the counter regardless of moved-from state.
  struct Counted {
    int* count_;
    explicit Counted(int* c) noexcept : count_{c} {}
    Counted(Counted&& o) noexcept : count_{o.count_} {}
    ~Counted() {
      if (count_) ++(*count_);
    }
    void operator()() const noexcept {}
  };
  int n{};
  {
    fixed_function<64, void()> f{Counted{&n}};
    EXPECT_EQ(n, 1); // temporary destroyed after move into storage
    {
      fixed_function<64, void()> g{std::move(f)};
      EXPECT_EQ(n, 2); // move ctor immediately destructs f's storage
    } // g destroyed: Counted in g.storage_ destructed
    EXPECT_EQ(n, 3);
  } // f destroyed: manage_ is null, nothing happens
  EXPECT_EQ(n, 3);
}

// Shared variable used by MetaTest_FixedFunction_RefReturn.
static int g_ref_val = 42;

// Free function used by MetaTest_FixedFunction_FreeFn.
static int double_it(int x) { return x * 2; }

void MetaTest_FixedFunction_RefReturn() {
  // Callables that return an actual reference are safe.
  fixed_function<64, int&()> f{[&]() -> int& { return g_ref_val; }};
  EXPECT_EQ(f(), 42);
  f() = 99;
  EXPECT_EQ(g_ref_val, 99);
  g_ref_val = 42; // restore

  fixed_function<64, const int&()> g{[&]() -> const int& {
    return g_ref_val;
  }};
  EXPECT_EQ(g(), 42);

  fixed_function<64, int&&()> h{[]() -> int&& {
    return static_cast<int&&>(g_ref_val);
  }};
  EXPECT_EQ(h(), 42);

#ifdef NOT_SUPPOSED_TO_COMPILE
  // Both of these trigger the static_assert: callable returns a prvalue `int`
  // but the declared return type is a reference, so every call would dangle.
  fixed_function<64, int&()> bad1{[] { return 42; }};
  fixed_function<64, const int&()> bad2{[] { return 42; }};
#endif
}

void MetaTest_FixedFunction_EmptyThrows() {
  // Default-constructed instance is empty and throws on call.
  fixed_function<64, int()> empty{};
  EXPECT_TRUE(!empty);
  EXPECT_THROW(empty(), std::bad_function_call);

  // Moved-from instance is also empty and throws on call.
  fixed_function<64, int()> f{[] { return 1; }};
  fixed_function<64, int()> g{std::move(f)};
  EXPECT_TRUE(!f);
  EXPECT_THROW(f(), std::bad_function_call);

  // nullptr-assigned instance throws too.
  g = nullptr;
  EXPECT_THROW(g(), std::bad_function_call);
}

void MetaTest_FixedFunction_FreeFn() {
  // A plain function pointer satisfies MoveConsumable (it is a prvalue).
  fixed_function<64, int(int)> f{&double_it};
  EXPECT_TRUE(static_cast<bool>(f));
  EXPECT_EQ(f(21), 42);
}

void MetaTest_FixedFunction_Functor() {
  struct Adder {
    int n;
    int operator()(int x) const { return x + n; }
  };
  fixed_function<64, int(int)> f{Adder{10}};
  EXPECT_EQ(f(32), 42);
}

void MetaTest_FixedFunction_Swap() {
  using ff = fixed_function<64, int()>;
  ff a{[] { return 1; }};
  ff b{[] { return 2; }};

  // Member swap.
  a.swap(b);
  EXPECT_EQ(a(), 2);
  EXPECT_EQ(b(), 1);

  // ADL swap (finds the hidden-friend in namespace corvid::meta).
  using std::swap;
  swap(a, b);
  EXPECT_EQ(a(), 1);
  EXPECT_EQ(b(), 2);

  // Swap a full instance with an empty one.
  ff empty{};
  a.swap(empty);
  EXPECT_FALSE(static_cast<bool>(a));
  EXPECT_TRUE(static_cast<bool>(empty));
  EXPECT_EQ(empty(), 1);

  // Swap two empty instances is a no-op.
  ff empty2{};
  a.swap(empty2);
  EXPECT_FALSE(static_cast<bool>(a));
  EXPECT_FALSE(static_cast<bool>(empty2));
}

MAKE_TEST_LIST(MetaTest_OStreamdDerived, MetaTest_EnumBitWidth,
    MetaTest_EnumHighestValueInNBits, MetaTest_EnumPow2,
    MetaTest_SpanConstness, MetaTest_FunctionVoidReturn,
    MetaTest_Specialization, MetaTest_PointerElement, MetaTest_Dereferenceable,
    MetaTest_IsPair, MetaTest_ContainerElement, MetaTest_KeyFind,
    MetaTest_TypeName, MetaTest_StringViewConvertible, MetaTest_Number,
    MetaTest_Tuple, MetaTest_Detection, MetaTest_Underlying,
    MetaTest_Streamable, MetaTest_MaybeTypes, MetaTest_AddressForwarder_Basic,
    MetaTest_AddressForwarder_Track, MetaTest_AddressForwarder_MoveConstruct,
    MetaTest_AddressForwarder_MoveAssign, MetaTest_AddressForwarder_SelfAssign,
    MetaTest_AddressForwarder_DestroySource,
    MetaTest_AddressForwarder_AsBaseMove,
    MetaTest_AddressForwarder_BoundFunction, MetaTest_FixedFunction_Basic,
    MetaTest_FixedFunction_Args, MetaTest_FixedFunction_Bool,
    MetaTest_FixedFunction_Move, MetaTest_FixedFunction_MoveAssign,
    MetaTest_FixedFunction_Destructor, MetaTest_FixedFunction_RefReturn,
    MetaTest_FixedFunction_EmptyThrows, MetaTest_FixedFunction_FreeFn,
    MetaTest_FixedFunction_Functor, MetaTest_FixedFunction_Swap);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
