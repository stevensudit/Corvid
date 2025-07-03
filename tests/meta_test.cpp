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
#include "AccutestShim.h"

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
  EXPECT_EQ(std::bit_width(0ull), 0);
  EXPECT_EQ(std::bit_width(1ull), 1);
  EXPECT_EQ(std::bit_width(2ull), 2);
  EXPECT_EQ(std::bit_width(3ull), 2);
  EXPECT_EQ(std::bit_width(4ull), 3);
  EXPECT_EQ(std::bit_width(7ull), 3);
  EXPECT_EQ(std::bit_width(8ull), 4);
  EXPECT_EQ(std::bit_width(15ull), 4);
  EXPECT_EQ(std::bit_width(16ull), 5);
  EXPECT_EQ(std::bit_width(31ull), 5);
  EXPECT_EQ(std::bit_width(32ull), 6);
  EXPECT_EQ(std::bit_width(63ull), 6);
  EXPECT_EQ(std::bit_width(64ull), 7);
  EXPECT_EQ(std::bit_width(127ull), 7);
  EXPECT_EQ(std::bit_width(128ull), 8);
  EXPECT_EQ(std::bit_width(255ull), 8);
  EXPECT_EQ(std::bit_width(256ull), 9);
  EXPECT_EQ(std::bit_width(511ull), 9);
  EXPECT_EQ(std::bit_width(512ull), 10);
  EXPECT_EQ(std::bit_width(1023ull), 10);
  EXPECT_EQ(std::bit_width(1024ull), 11);
  EXPECT_EQ(std::bit_width(2047ull), 11);
  EXPECT_EQ(std::bit_width(2048ull), 12);
  EXPECT_EQ(std::bit_width(4095ull), 12);
  EXPECT_EQ(std::bit_width(4096ull), 13);
  EXPECT_EQ(std::bit_width(8191ull), 13);
  EXPECT_EQ(std::bit_width(8192ull), 14);
  EXPECT_EQ(std::bit_width(16383ull), 14);
  EXPECT_EQ(std::bit_width(16384ull), 15);
  EXPECT_EQ(std::bit_width(32767ull), 15);
  EXPECT_EQ(std::bit_width(32768ull), 16);
  EXPECT_EQ(std::bit_width(65535ull), 16);
  EXPECT_EQ(std::bit_width(65536ull), 17);
  EXPECT_EQ(std::bit_width(131071ull), 17);
  EXPECT_EQ(std::bit_width(131072ull), 18);
  EXPECT_EQ(std::bit_width(262143ull), 18);
  EXPECT_EQ(std::bit_width(262144ull), 19);
  EXPECT_EQ(std::bit_width(524287ull), 19);
  EXPECT_EQ(std::bit_width(524288ull), 20);
  EXPECT_EQ(std::bit_width(1048575ull), 20);
  EXPECT_EQ(std::bit_width(1048576ull), 21);
  EXPECT_EQ(std::bit_width(2097151ull), 21);
  EXPECT_EQ(std::bit_width(2097152ull), 22);
  EXPECT_EQ(std::bit_width(4194303ull), 22);
  EXPECT_EQ(std::bit_width(4194304ull), 23);
  EXPECT_EQ(std::bit_width(8388607ull), 23);
  EXPECT_EQ(std::bit_width(8388608ull), 24);
  EXPECT_EQ(std::bit_width(16777215ull), 24);
  EXPECT_EQ(std::bit_width(16777216ull), 25);
  EXPECT_EQ(std::bit_width(33554431ull), 25);
  EXPECT_EQ(std::bit_width(33554432ull), 26);
  EXPECT_EQ(std::bit_width(67108863ull), 26);
  EXPECT_EQ(std::bit_width(67108864ull), 27);
  EXPECT_EQ(std::bit_width(134217727ull), 27);
  EXPECT_EQ(std::bit_width(134217728ull), 28);
  EXPECT_EQ(std::bit_width(268435455ull), 28);
  EXPECT_EQ(std::bit_width(268435456ull), 29);
  EXPECT_EQ(std::bit_width(536870911ull), 29);
  EXPECT_EQ(std::bit_width(536870912ull), 30);
  EXPECT_EQ(std::bit_width(1073741823ull), 30);
  EXPECT_EQ(std::bit_width(1073741824ull), 31);
  EXPECT_EQ(std::bit_width(2147483647ull), 31);
  EXPECT_EQ(std::bit_width(2147483648ull), 32);
  EXPECT_EQ(std::bit_width(4294967295ull), 32);
  EXPECT_EQ(std::bit_width(4294967296ull), 33);
  EXPECT_EQ(std::bit_width(8589934591ull), 33);
  EXPECT_EQ(std::bit_width(8589934592ull), 34);
  EXPECT_EQ(std::bit_width(17179869183ull), 34);
  EXPECT_EQ(std::bit_width(17179869184ull), 35);
  EXPECT_EQ(std::bit_width(34359738367ull), 35);
  EXPECT_EQ(std::bit_width(34359738368ull), 36);
  EXPECT_EQ(std::bit_width(68719476735ull), 36);
  EXPECT_EQ(std::bit_width(68719476736ull), 37);
  EXPECT_EQ(std::bit_width(137438953471ull), 37);
  EXPECT_EQ(std::bit_width(137438953472ull), 38);
  EXPECT_EQ(std::bit_width(274877906943ull), 38);
  EXPECT_EQ(std::bit_width(274877906944ull), 39);
  EXPECT_EQ(std::bit_width(549755813887ull), 39);
  EXPECT_EQ(std::bit_width(549755813888ull), 40);
  EXPECT_EQ(std::bit_width(1099511627775ull), 40);
  EXPECT_EQ(std::bit_width(1099511627776ull), 41);
  EXPECT_EQ(std::bit_width(2199023255551ull), 41);
  EXPECT_EQ(std::bit_width(2199023255552ull), 42);
  EXPECT_EQ(std::bit_width(4398046511103ull), 42);
  EXPECT_EQ(std::bit_width(4398046511104ull), 43);
  EXPECT_EQ(std::bit_width(8796093022207ull), 43);
  EXPECT_EQ(std::bit_width(8796093022208ull), 44);
  EXPECT_EQ(std::bit_width(17592186044415ull), 44);
  EXPECT_EQ(std::bit_width(17592186044416ull), 45);
  EXPECT_EQ(std::bit_width(35184372088831ull), 45);
  EXPECT_EQ(std::bit_width(35184372088832ull), 46);
  EXPECT_EQ(std::bit_width(70368744177663ull), 46);
  EXPECT_EQ(std::bit_width(70368744177664ull), 47);
  EXPECT_EQ(std::bit_width(140737488355327ull), 47);
  EXPECT_EQ(std::bit_width(140737488355328ull), 48);
  EXPECT_EQ(std::bit_width(281474976710655ull), 48);
  EXPECT_EQ(std::bit_width(281474976710656ull), 49);
  EXPECT_EQ(std::bit_width(562949953421311ull), 49);
  EXPECT_EQ(std::bit_width(562949953421312ull), 50);
  EXPECT_EQ(std::bit_width(1125899906842623ull), 50);
  EXPECT_EQ(std::bit_width(1125899906842624ull), 51);
  EXPECT_EQ(std::bit_width(2251799813685247ull), 51);
  EXPECT_EQ(std::bit_width(2251799813685248ull), 52);
  EXPECT_EQ(std::bit_width(4503599627370495ull), 52);
  EXPECT_EQ(std::bit_width(4503599627370496ull), 53);
  EXPECT_EQ(std::bit_width(9007199254740991ull), 53);
  EXPECT_EQ(std::bit_width(9007199254740992ull), 54);
  EXPECT_EQ(std::bit_width(18014398509481983ull), 54);
  EXPECT_EQ(std::bit_width(18014398509481984ull), 55);
  EXPECT_EQ(std::bit_width(36028797018963967ull), 55);
  EXPECT_EQ(std::bit_width(36028797018963968ull), 56);
  EXPECT_EQ(std::bit_width(72057594037927935ull), 56);
  EXPECT_EQ(std::bit_width(72057594037927936ull), 57);
  EXPECT_EQ(std::bit_width(144115188075855871ull), 57);
  EXPECT_EQ(std::bit_width(144115188075855872ull), 58);
  EXPECT_EQ(std::bit_width(288230376151711743ull), 58);
  EXPECT_EQ(std::bit_width(288230376151711744ull), 59);
  EXPECT_EQ(std::bit_width(576460752303423487ull), 59);
  EXPECT_EQ(std::bit_width(576460752303423488ull), 60);
  EXPECT_EQ(std::bit_width(1152921504606846975ull), 60);
  EXPECT_EQ(std::bit_width(1152921504606846976ull), 61);
  EXPECT_EQ(std::bit_width(2305843009213693951ull), 61);
  EXPECT_EQ(std::bit_width(2305843009213693952ull), 62);
  EXPECT_EQ(std::bit_width(4611686018427387903ull), 62);
  EXPECT_EQ(std::bit_width(4611686018427387904ull), 63);
  EXPECT_EQ(std::bit_width(9223372036854775807ull), 63);
  EXPECT_EQ(std::bit_width(9223372036854775808ull), 64);
  EXPECT_EQ(std::bit_width(18446744073709551615ull), 64);
}

void MetaTest_EnumHighestValueInNBits() {
  EXPECT_EQ(meta::highest_value_in_n_bits(0ull), 0ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(1ull), 1ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(2ull), 3ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(3ull), 7ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(4ull), 15ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(5ull), 31ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(6ull), 63ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(7ull), 127ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(8ull), 255ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(9ull), 511ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(10ull), 1023ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(11ull), 2047ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(12ull), 4095ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(13ull), 8191ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(14ull), 16383ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(15ull), 32767ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(16ull), 65535ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(17ull), 131071ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(18ull), 262143ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(19ull), 524287ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(20ull), 1048575ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(21ull), 2097151ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(22ull), 4194303ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(23ull), 8388607ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(24ull), 16777215ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(25ull), 33554431ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(26ull), 67108863ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(27ull), 134217727ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(28ull), 268435455ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(29ull), 536870911ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(30ull), 1073741823ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(31ull), 2147483647ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(32ull), 4294967295ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(33ull), 8589934591ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(34ull), 17179869183ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(35ull), 34359738367ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(36ull), 68719476735ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(37ull), 137438953471ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(38ull), 274877906943ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(39ull), 549755813887ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(40ull), 1099511627775ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(41ull), 2199023255551ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(42ull), 4398046511103ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(43ull), 8796093022207ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(44ull), 17592186044415ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(45ull), 35184372088831ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(46ull), 70368744177663ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(47ull), 140737488355327ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(48ull), 281474976710655ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(49ull), 562949953421311ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(50ull), 1125899906842623ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(51ull), 2251799813685247ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(52ull), 4503599627370495ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(53ull), 9007199254740991ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(54ull), 18014398509481983ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(55ull), 36028797018963967ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(56ull), 72057594037927935ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(57ull), 144115188075855871ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(58ull), 288230376151711743ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(59ull), 576460752303423487ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(60ull), 1152921504606846975ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(61ull), 2305843009213693951ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(62ull), 4611686018427387903ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(63ull), 9223372036854775807ull);
  EXPECT_EQ(meta::highest_value_in_n_bits(64ull), 18446744073709551615ull);
}

void MetaTest_EnumPow2() {
  EXPECT_EQ(meta::pow2(0), 1ull);
  EXPECT_EQ(meta::pow2(1), 2ull);
  EXPECT_EQ(meta::pow2(2), 4ull);
  EXPECT_EQ(meta::pow2(3), 8ull);
  EXPECT_EQ(meta::pow2(4), 16ull);
  EXPECT_EQ(meta::pow2(5), 32ull);
  EXPECT_EQ(meta::pow2(6), 64ull);
  EXPECT_EQ(meta::pow2(7), 128ull);
  EXPECT_EQ(meta::pow2(8), 256ull);
  EXPECT_EQ(meta::pow2(9), 512ull);
  EXPECT_EQ(meta::pow2(10), 1024ull);
  EXPECT_EQ(meta::pow2(11), 2048ull);
  EXPECT_EQ(meta::pow2(12), 4096ull);
  EXPECT_EQ(meta::pow2(13), 8192ull);
  EXPECT_EQ(meta::pow2(14), 16384ull);
  EXPECT_EQ(meta::pow2(15), 32768ull);
  EXPECT_EQ(meta::pow2(16), 65536ull);
  EXPECT_EQ(meta::pow2(17), 131072ull);
  EXPECT_EQ(meta::pow2(18), 262144ull);
  EXPECT_EQ(meta::pow2(19), 524288ull);
  EXPECT_EQ(meta::pow2(20), 1048576ull);
  EXPECT_EQ(meta::pow2(21), 2097152ull);
  EXPECT_EQ(meta::pow2(22), 4194304ull);
  EXPECT_EQ(meta::pow2(23), 8388608ull);
  EXPECT_EQ(meta::pow2(24), 16777216ull);
  EXPECT_EQ(meta::pow2(25), 33554432ull);
  EXPECT_EQ(meta::pow2(26), 67108864ull);
  EXPECT_EQ(meta::pow2(27), 134217728ull);
  EXPECT_EQ(meta::pow2(28), 268435456ull);
  EXPECT_EQ(meta::pow2(29), 536870912ull);
  EXPECT_EQ(meta::pow2(30), 1073741824ull);
  EXPECT_EQ(meta::pow2(31), 2147483648ull);
  EXPECT_EQ(meta::pow2(32), 4294967296ull);
  EXPECT_EQ(meta::pow2(33), 8589934592ull);
  EXPECT_EQ(meta::pow2(34), 17179869184ull);
  EXPECT_EQ(meta::pow2(35), 34359738368ull);
  EXPECT_EQ(meta::pow2(36), 68719476736ull);
  EXPECT_EQ(meta::pow2(37), 137438953472ull);
  EXPECT_EQ(meta::pow2(38), 274877906944ull);
  EXPECT_EQ(meta::pow2(39), 549755813888ull);
  EXPECT_EQ(meta::pow2(40), 1099511627776ull);
  EXPECT_EQ(meta::pow2(41), 2199023255552ull);
  EXPECT_EQ(meta::pow2(42), 4398046511104ull);
  EXPECT_EQ(meta::pow2(43), 8796093022208ull);
  EXPECT_EQ(meta::pow2(44), 17592186044416ull);
  EXPECT_EQ(meta::pow2(45), 35184372088832ull);
  EXPECT_EQ(meta::pow2(46), 70368744177664ull);
  EXPECT_EQ(meta::pow2(47), 140737488355328ull);
  EXPECT_EQ(meta::pow2(48), 281474976710656ull);
  EXPECT_EQ(meta::pow2(49), 562949953421312ull);
  EXPECT_EQ(meta::pow2(50), 1125899906842624ull);
  EXPECT_EQ(meta::pow2(51), 2251799813685248ull);
  EXPECT_EQ(meta::pow2(52), 4503599627370496ull);
  EXPECT_EQ(meta::pow2(53), 9007199254740992ull);
  EXPECT_EQ(meta::pow2(54), 18014398509481984ull);
  EXPECT_EQ(meta::pow2(55), 36028797018963968ull);
  EXPECT_EQ(meta::pow2(56), 72057594037927936ull);
  EXPECT_EQ(meta::pow2(57), 144115188075855872ull);
  EXPECT_EQ(meta::pow2(58), 288230376151711744ull);
  EXPECT_EQ(meta::pow2(59), 576460752303423488ull);
  EXPECT_EQ(meta::pow2(60), 1152921504606846976ull);
  EXPECT_EQ(meta::pow2(61), 2305843009213693952ull);
  EXPECT_EQ(meta::pow2(62), 4611686018427387904ull);
  EXPECT_EQ(meta::pow2(63), 9223372036854775808ull);
  EXPECT_EQ(meta::pow2(64), 0ull);
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
