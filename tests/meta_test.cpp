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

#include "../corvid/meta.h"
#include "minitest.h"

// #include "Interval.h"

using namespace std::literals;
using namespace corvid;

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

MAKE_TEST_LIST(MetaTest_OStreamdDerived, MetaTest_EnumBitWidth,
    MetaTest_EnumHighestValueInNBits, MetaTest_EnumPow2,
    MetaTest_SpanConstness, MetaTest_FunctionVoidReturn);

// TODO: Port the tests below.

#if 0
struct Foo {};

template<typename T>
struct Goo {};


TEST(MetaTest, Special) {
  EXPECT_TRUE((is_specialization_of_v<std::vector<int>, std::vector>));
  EXPECT_FALSE((is_specialization_of_v<std::vector<int>, std::map>));
  EXPECT_FALSE((is_specialization_of_v<int, std::map>));
  EXPECT_FALSE((is_specialization_of_v<int, Goo>));
  EXPECT_TRUE((is_specialization_of_v<Goo<int>, Goo>));

  // Fails because char is not a template with type parameters.
  // * EXPECT_FALSE((is_specialization_of_v<int, char>));

  // Fails because std::array is not specialized on just type parameters.
  // * EXPECT_FALSE((is_specialization_of_v<int, std::array<int, 4>>));
}

TEST(MetaTest, PointerElement) {
  EXPECT_TRUE((std::is_same_v<int, pointer_element_t<int*>>));
  EXPECT_TRUE((std::is_same_v<int, pointer_element_t<std::unique_ptr<int>>>));
  EXPECT_TRUE((std::is_same_v<void, pointer_element_t<int>>));
}

TEST(MetaTest, IsDeref) {
  EXPECT_TRUE((is_dereferenceable_v<int*>));
  EXPECT_TRUE((is_dereferenceable_v<std::unique_ptr<int>>));
  EXPECT_FALSE((is_dereferenceable_v<int>));
  EXPECT_TRUE((is_dereferenceable_v<std::optional<int>()>));
}

TEST(MetaTest, IsPair) {
  EXPECT_TRUE((is_pair_v<std::pair<int, int>>));
  EXPECT_FALSE((is_pair_v<std::tuple<int, int>>));
  EXPECT_FALSE((is_pair_v<int>));
  EXPECT_FALSE((is_pair_v<intervals::interval<int>>));

  EXPECT_TRUE((is_pair_like_v<std::pair<int, int>>));
  EXPECT_FALSE((is_pair_like_v<std::tuple<int, int>>));
  EXPECT_FALSE((is_pair_like_v<int>));
  EXPECT_TRUE((is_pair_like_v<intervals::interval<int>>));
  using T = intervals::interval<int>;
  EXPECT_TRUE((is_pair_like_v<T>));
  using U = const intervals::interval<int>&;
  EXPECT_TRUE((is_pair_like_v<U>));
  using V = intervals::interval<int>&;
  EXPECT_TRUE((is_pair_like_v<V>));
  using W = const intervals::interval<int>;
  EXPECT_TRUE((is_pair_like_v<W>));
}

TEST(MetaTest, ContainerElement) {
  if (true) {
    std::pair<int, int> kv{1, 2};
    auto p = &kv;
    EXPECT_EQ(container_element_v(p), 2);
  }
  if (true) {
    int v{2};
    auto p = &v;
    EXPECT_EQ(container_element_v(p), 2);
  }
  if (true) {
    std::string s{"abc"};
    EXPECT_EQ(container_element_v(&s[1]), 'b');
  }
}

TEST(MetaTest, FindRet) {
  using M = std::map<int, Foo>;
  EXPECT_TRUE((has_key_find_v<M, int>));
  EXPECT_FALSE((has_key_find_v<M, Foo>));
  EXPECT_TRUE(
      (std::is_same_v<keyfinding::details::find_ret_t<M, int>, M::iterator>));

  using S = std::set<Foo>;
  EXPECT_TRUE((has_key_find_v<S, Foo>));
  EXPECT_FALSE((has_key_find_v<S, int>));
  EXPECT_TRUE(
      (std::is_same_v<keyfinding::details::find_ret_t<S, Foo>, S::iterator>));

  using V = std::vector<int>;
  EXPECT_FALSE((has_key_find_v<V, int>));
  EXPECT_FALSE((has_key_find_v<V, Foo>));
}

TEST(MetaTest, TypeName) {
  using T = std::string;
  using U = const std::string;
  using V = std::string&;
  using W = const std::string&;
  EXPECT_EQ(type_name<T>(), type_name<T>());
  EXPECT_NE(type_name<T>(), type_name<U>());
  EXPECT_NE(type_name<U>(), type_name<V>());
  EXPECT_NE(type_name<V>(), type_name<W>());
  EXPECT_EQ(type_name<T>(), type_name(T{}));
}

TEST(MetaTest, StringViewConvertible) {
  EXPECT_TRUE(is_string_view_convertible_v<std::string_view>);
  EXPECT_TRUE(is_string_view_convertible_v<std::string>);
  EXPECT_TRUE(is_string_view_convertible_v<char*>);
  EXPECT_TRUE(is_string_view_convertible_v<char[]>);
  EXPECT_FALSE(is_string_view_convertible_v<int>);
  EXPECT_FALSE(is_string_view_convertible_v<nullptr_t>);
  EXPECT_TRUE(is_string_view_convertible_v<std::string&>);
  EXPECT_TRUE(is_string_view_convertible_v<std::string&&>);

  EXPECT_FALSE(can_ranged_for_v<int>);
  EXPECT_TRUE(can_ranged_for_v<std::vector<int>>);
  EXPECT_TRUE(can_ranged_for_v<std::string>);
  EXPECT_TRUE((can_ranged_for_v<int[4]>));
  EXPECT_TRUE((can_ranged_for_v<char[4]>));
  EXPECT_FALSE((can_ranged_for_v<char*>));

  EXPECT_FALSE(is_container_v<int>);
  EXPECT_TRUE(is_container_v<std::vector<int>>);
  EXPECT_FALSE(is_container_v<std::string>);
  EXPECT_TRUE((is_container_v<std::array<int, 2>>));
  EXPECT_TRUE((is_container_v<int[4]>));
  EXPECT_FALSE((is_container_v<char[4]>));
  EXPECT_FALSE((is_container_v<char*>));
}

TEST(MetaTest, Number) {
  EXPECT_TRUE(is_number_v<char>);
  EXPECT_TRUE(is_number_v<int>);
  EXPECT_TRUE(is_number_v<float>);
  EXPECT_TRUE(is_number_v<double>);

  EXPECT_FALSE(is_number_v<std::byte>);
  EXPECT_TRUE(std::is_enum_v<std::byte>);
  EXPECT_FALSE(std::is_enum_v<const std::byte&>);
  EXPECT_TRUE(is_enum_v<const std::byte&>);

  EXPECT_TRUE(std::is_arithmetic_v<int>);
  EXPECT_FALSE(std::is_arithmetic_v<int&>);

  EXPECT_TRUE(std::is_arithmetic_v<bool>);
  EXPECT_FALSE(is_number_v<bool>);
  EXPECT_TRUE(is_bool_v<bool>);

  enum ColorEnum { red, green = 20, blue };
  enum class ColorClass { red, green = 20, blue };

  EXPECT_TRUE(is_enum_v<ColorClass>);
  EXPECT_TRUE(is_enum_v<ColorEnum>);
}

TEST(MetaTest, Tuple) {
  using T0 = std::tuple<>;
  using T2 = std::tuple<int, int>;
  using PI = std::pair<int, int>;
  using I2 = std::array<int, 2>;

  EXPECT_EQ(std::tuple_size_v<T2>, 2);
  EXPECT_EQ(std::tuple_size_v<T0>, 0);
  EXPECT_EQ(std::tuple_size_v<PI>, 2);
  EXPECT_EQ(std::tuple_size_v<I2>, 2);

  EXPECT_TRUE(is_std_array_v<I2>);
  EXPECT_FALSE(is_std_array_v<T2>);
  EXPECT_TRUE((is_tuple_like_v<I2>));
  EXPECT_FALSE((is_tuple_like_v<std::string>));

  EXPECT_FALSE(is_tuple_v<int>);
  EXPECT_TRUE((is_tuple_v<T0>));
  EXPECT_TRUE((is_tuple_v<T2>));
  EXPECT_FALSE((is_tuple_v<PI>));
  EXPECT_TRUE((is_tuple_like_v<PI>));
}

TEST(MetaTest, Detection) {
  if (true) {
    auto il = {1, 2, 3};
    EXPECT_TRUE((is_initializer_list_v<decltype(il)>));
  }
  if (true) {
    std::variant<int, float> va = 42;
    EXPECT_TRUE((is_variant_v<decltype(va)>));
  }
  if (true) {
    EXPECT_TRUE((is_optional_like_v<std::optional<int>>));
    EXPECT_TRUE((is_optional_like_v<int*>));
    EXPECT_FALSE((is_optional_like_v<void*>));
    EXPECT_FALSE((is_optional_like_v<const char*>));
  }
  if (true) {
    EXPECT_TRUE((is_void_ptr_v<void*>));
    EXPECT_TRUE((is_void_ptr_v<const void*>));
    EXPECT_TRUE((is_void_ptr_v<void* const>));
    EXPECT_TRUE((is_void_ptr_v<const void* const>));
    EXPECT_FALSE((is_void_ptr_v<int>));
    EXPECT_FALSE((is_void_ptr_v<int*>));
    EXPECT_FALSE((is_void_ptr_v<int* const>));
  }
  if (true) {
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
}

TEST(MetaTest, Underlying) {
  enum class X : size_t { x1 = 1, x2 };
  enum class Y : int64_t { ylow = -1 };
  enum Z { z1 = 1 };
  auto x = as_underlying(X::x1);
  EXPECT_EQ(x, 1);
  EXPECT_TRUE((std::is_same_v<size_t, decltype(x)>));
  auto y = as_underlying(Y::ylow);
  EXPECT_EQ(y, -1);
  EXPECT_TRUE((std::is_same_v<int64_t, decltype(y)>));

  // No conversion needed for unscoped, although we can also used the scoped
  // name.
  auto z0 = Z::z1;
  auto z = as_underlying(z1);
  EXPECT_EQ(z0, 1);
  EXPECT_EQ(z, 1);
  EXPECT_TRUE((std::is_same_v<int, decltype(z)>));
}

TEST(MetaTest, Streamable) {
  // This sort of works a little.
  EXPECT_TRUE(can_stream_out_v<int>);
  EXPECT_FALSE(can_stream_out_v<Foo>);
}
#endif
