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
#include "catch2_main.h"

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

#pragma region OStreamdDerived

TEST_CASE("OStreamdDerived", "[MetaTest]") {
  std::ostringstream oss;
  stream_out(oss, 1);
  CHECK((oss.str()) == ("1"));
#ifdef NOT_SUPPOSED_TO_COMPILE
  std::string s{"Hello"};
  foo(s, 42);
  stream_out(oss, oss);
#endif
}

#pragma endregion
#pragma region EnumBitWidth

TEST_CASE("EnumBitWidth", "[MetaTest]") {
  CHECK((std::bit_width(0ULL)) == (0));
  CHECK((std::bit_width(1ULL)) == (1));
  CHECK((std::bit_width(2ULL)) == (2));
  CHECK((std::bit_width(3ULL)) == (2));
  CHECK((std::bit_width(4ULL)) == (3));
  CHECK((std::bit_width(7ULL)) == (3));
  CHECK((std::bit_width(8ULL)) == (4));
  CHECK((std::bit_width(15ULL)) == (4));
  CHECK((std::bit_width(16ULL)) == (5));
  CHECK((std::bit_width(31ULL)) == (5));
  CHECK((std::bit_width(32ULL)) == (6));
  CHECK((std::bit_width(63ULL)) == (6));
  CHECK((std::bit_width(64ULL)) == (7));
  CHECK((std::bit_width(127ULL)) == (7));
  CHECK((std::bit_width(128ULL)) == (8));
  CHECK((std::bit_width(255ULL)) == (8));
  CHECK((std::bit_width(256ULL)) == (9));
  CHECK((std::bit_width(511ULL)) == (9));
  CHECK((std::bit_width(512ULL)) == (10));
  CHECK((std::bit_width(1023ULL)) == (10));
  CHECK((std::bit_width(1024ULL)) == (11));
  CHECK((std::bit_width(2047ULL)) == (11));
  CHECK((std::bit_width(2048ULL)) == (12));
  CHECK((std::bit_width(4095ULL)) == (12));
  CHECK((std::bit_width(4096ULL)) == (13));
  CHECK((std::bit_width(8191ULL)) == (13));
  CHECK((std::bit_width(8192ULL)) == (14));
  CHECK((std::bit_width(16383ULL)) == (14));
  CHECK((std::bit_width(16384ULL)) == (15));
  CHECK((std::bit_width(32767ULL)) == (15));
  CHECK((std::bit_width(32768ULL)) == (16));
  CHECK((std::bit_width(65535ULL)) == (16));
  CHECK((std::bit_width(65536ULL)) == (17));
  CHECK((std::bit_width(131071ULL)) == (17));
  CHECK((std::bit_width(131072ULL)) == (18));
  CHECK((std::bit_width(262143ULL)) == (18));
  CHECK((std::bit_width(262144ULL)) == (19));
  CHECK((std::bit_width(524287ULL)) == (19));
  CHECK((std::bit_width(524288ULL)) == (20));
  CHECK((std::bit_width(1048575ULL)) == (20));
  CHECK((std::bit_width(1048576ULL)) == (21));
  CHECK((std::bit_width(2097151ULL)) == (21));
  CHECK((std::bit_width(2097152ULL)) == (22));
  CHECK((std::bit_width(4194303ULL)) == (22));
  CHECK((std::bit_width(4194304ULL)) == (23));
  CHECK((std::bit_width(8388607ULL)) == (23));
  CHECK((std::bit_width(8388608ULL)) == (24));
  CHECK((std::bit_width(16777215ULL)) == (24));
  CHECK((std::bit_width(16777216ULL)) == (25));
  CHECK((std::bit_width(33554431ULL)) == (25));
  CHECK((std::bit_width(33554432ULL)) == (26));
  CHECK((std::bit_width(67108863ULL)) == (26));
  CHECK((std::bit_width(67108864ULL)) == (27));
  CHECK((std::bit_width(134217727ULL)) == (27));
  CHECK((std::bit_width(134217728ULL)) == (28));
  CHECK((std::bit_width(268435455ULL)) == (28));
  CHECK((std::bit_width(268435456ULL)) == (29));
  CHECK((std::bit_width(536870911ULL)) == (29));
  CHECK((std::bit_width(536870912ULL)) == (30));
  CHECK((std::bit_width(1073741823ULL)) == (30));
  CHECK((std::bit_width(1073741824ULL)) == (31));
  CHECK((std::bit_width(2147483647ULL)) == (31));
  CHECK((std::bit_width(2147483648ULL)) == (32));
  CHECK((std::bit_width(4294967295ULL)) == (32));
  CHECK((std::bit_width(4294967296ULL)) == (33));
  CHECK((std::bit_width(8589934591ULL)) == (33));
  CHECK((std::bit_width(8589934592ULL)) == (34));
  CHECK((std::bit_width(17179869183ULL)) == (34));
  CHECK((std::bit_width(17179869184ULL)) == (35));
  CHECK((std::bit_width(34359738367ULL)) == (35));
  CHECK((std::bit_width(34359738368ULL)) == (36));
  CHECK((std::bit_width(68719476735ULL)) == (36));
  CHECK((std::bit_width(68719476736ULL)) == (37));
  CHECK((std::bit_width(137438953471ULL)) == (37));
  CHECK((std::bit_width(137438953472ULL)) == (38));
  CHECK((std::bit_width(274877906943ULL)) == (38));
  CHECK((std::bit_width(274877906944ULL)) == (39));
  CHECK((std::bit_width(549755813887ULL)) == (39));
  CHECK((std::bit_width(549755813888ULL)) == (40));
  CHECK((std::bit_width(1099511627775ULL)) == (40));
  CHECK((std::bit_width(1099511627776ULL)) == (41));
  CHECK((std::bit_width(2199023255551ULL)) == (41));
  CHECK((std::bit_width(2199023255552ULL)) == (42));
  CHECK((std::bit_width(4398046511103ULL)) == (42));
  CHECK((std::bit_width(4398046511104ULL)) == (43));
  CHECK((std::bit_width(8796093022207ULL)) == (43));
  CHECK((std::bit_width(8796093022208ULL)) == (44));
  CHECK((std::bit_width(17592186044415ULL)) == (44));
  CHECK((std::bit_width(17592186044416ULL)) == (45));
  CHECK((std::bit_width(35184372088831ULL)) == (45));
  CHECK((std::bit_width(35184372088832ULL)) == (46));
  CHECK((std::bit_width(70368744177663ULL)) == (46));
  CHECK((std::bit_width(70368744177664ULL)) == (47));
  CHECK((std::bit_width(140737488355327ULL)) == (47));
  CHECK((std::bit_width(140737488355328ULL)) == (48));
  CHECK((std::bit_width(281474976710655ULL)) == (48));
  CHECK((std::bit_width(281474976710656ULL)) == (49));
  CHECK((std::bit_width(562949953421311ULL)) == (49));
  CHECK((std::bit_width(562949953421312ULL)) == (50));
  CHECK((std::bit_width(1125899906842623ULL)) == (50));
  CHECK((std::bit_width(1125899906842624ULL)) == (51));
  CHECK((std::bit_width(2251799813685247ULL)) == (51));
  CHECK((std::bit_width(2251799813685248ULL)) == (52));
  CHECK((std::bit_width(4503599627370495ULL)) == (52));
  CHECK((std::bit_width(4503599627370496ULL)) == (53));
  CHECK((std::bit_width(9007199254740991ULL)) == (53));
  CHECK((std::bit_width(9007199254740992ULL)) == (54));
  CHECK((std::bit_width(18014398509481983ULL)) == (54));
  CHECK((std::bit_width(18014398509481984ULL)) == (55));
  CHECK((std::bit_width(36028797018963967ULL)) == (55));
  CHECK((std::bit_width(36028797018963968ULL)) == (56));
  CHECK((std::bit_width(72057594037927935ULL)) == (56));
  CHECK((std::bit_width(72057594037927936ULL)) == (57));
  CHECK((std::bit_width(144115188075855871ULL)) == (57));
  CHECK((std::bit_width(144115188075855872ULL)) == (58));
  CHECK((std::bit_width(288230376151711743ULL)) == (58));
  CHECK((std::bit_width(288230376151711744ULL)) == (59));
  CHECK((std::bit_width(576460752303423487ULL)) == (59));
  CHECK((std::bit_width(576460752303423488ULL)) == (60));
  CHECK((std::bit_width(1152921504606846975ULL)) == (60));
  CHECK((std::bit_width(1152921504606846976ULL)) == (61));
  CHECK((std::bit_width(2305843009213693951ULL)) == (61));
  CHECK((std::bit_width(2305843009213693952ULL)) == (62));
  CHECK((std::bit_width(4611686018427387903ULL)) == (62));
  CHECK((std::bit_width(4611686018427387904ULL)) == (63));
  CHECK((std::bit_width(9223372036854775807ULL)) == (63));
  CHECK((std::bit_width(9223372036854775808ULL)) == (64));
  CHECK((std::bit_width(18446744073709551615ULL)) == (64));
}

#pragma endregion
#pragma region EnumHighestValueInNBits

TEST_CASE("EnumHighestValueInNBits", "[MetaTest]") {
  CHECK((meta::highest_value_in_n_bits(0ULL)) == (0ULL));
  CHECK((meta::highest_value_in_n_bits(1ULL)) == (1ULL));
  CHECK((meta::highest_value_in_n_bits(2ULL)) == (3ULL));
  CHECK((meta::highest_value_in_n_bits(3ULL)) == (7ULL));
  CHECK((meta::highest_value_in_n_bits(4ULL)) == (15ULL));
  CHECK((meta::highest_value_in_n_bits(5ULL)) == (31ULL));
  CHECK((meta::highest_value_in_n_bits(6ULL)) == (63ULL));
  CHECK((meta::highest_value_in_n_bits(7ULL)) == (127ULL));
  CHECK((meta::highest_value_in_n_bits(8ULL)) == (255ULL));
  CHECK((meta::highest_value_in_n_bits(9ULL)) == (511ULL));
  CHECK((meta::highest_value_in_n_bits(10ULL)) == (1023ULL));
  CHECK((meta::highest_value_in_n_bits(11ULL)) == (2047ULL));
  CHECK((meta::highest_value_in_n_bits(12ULL)) == (4095ULL));
  CHECK((meta::highest_value_in_n_bits(13ULL)) == (8191ULL));
  CHECK((meta::highest_value_in_n_bits(14ULL)) == (16383ULL));
  CHECK((meta::highest_value_in_n_bits(15ULL)) == (32767ULL));
  CHECK((meta::highest_value_in_n_bits(16ULL)) == (65535ULL));
  CHECK((meta::highest_value_in_n_bits(17ULL)) == (131071ULL));
  CHECK((meta::highest_value_in_n_bits(18ULL)) == (262143ULL));
  CHECK((meta::highest_value_in_n_bits(19ULL)) == (524287ULL));
  CHECK((meta::highest_value_in_n_bits(20ULL)) == (1048575ULL));
  CHECK((meta::highest_value_in_n_bits(21ULL)) == (2097151ULL));
  CHECK((meta::highest_value_in_n_bits(22ULL)) == (4194303ULL));
  CHECK((meta::highest_value_in_n_bits(23ULL)) == (8388607ULL));
  CHECK((meta::highest_value_in_n_bits(24ULL)) == (16777215ULL));
  CHECK((meta::highest_value_in_n_bits(25ULL)) == (33554431ULL));
  CHECK((meta::highest_value_in_n_bits(26ULL)) == (67108863ULL));
  CHECK((meta::highest_value_in_n_bits(27ULL)) == (134217727ULL));
  CHECK((meta::highest_value_in_n_bits(28ULL)) == (268435455ULL));
  CHECK((meta::highest_value_in_n_bits(29ULL)) == (536870911ULL));
  CHECK((meta::highest_value_in_n_bits(30ULL)) == (1073741823ULL));
  CHECK((meta::highest_value_in_n_bits(31ULL)) == (2147483647ULL));
  CHECK((meta::highest_value_in_n_bits(32ULL)) == (4294967295ULL));
  CHECK((meta::highest_value_in_n_bits(33ULL)) == (8589934591ULL));
  CHECK((meta::highest_value_in_n_bits(34ULL)) == (17179869183ULL));
  CHECK((meta::highest_value_in_n_bits(35ULL)) == (34359738367ULL));
  CHECK((meta::highest_value_in_n_bits(36ULL)) == (68719476735ULL));
  CHECK((meta::highest_value_in_n_bits(37ULL)) == (137438953471ULL));
  CHECK((meta::highest_value_in_n_bits(38ULL)) == (274877906943ULL));
  CHECK((meta::highest_value_in_n_bits(39ULL)) == (549755813887ULL));
  CHECK((meta::highest_value_in_n_bits(40ULL)) == (1099511627775ULL));
  CHECK((meta::highest_value_in_n_bits(41ULL)) == (2199023255551ULL));
  CHECK((meta::highest_value_in_n_bits(42ULL)) == (4398046511103ULL));
  CHECK((meta::highest_value_in_n_bits(43ULL)) == (8796093022207ULL));
  CHECK((meta::highest_value_in_n_bits(44ULL)) == (17592186044415ULL));
  CHECK((meta::highest_value_in_n_bits(45ULL)) == (35184372088831ULL));
  CHECK((meta::highest_value_in_n_bits(46ULL)) == (70368744177663ULL));
  CHECK((meta::highest_value_in_n_bits(47ULL)) == (140737488355327ULL));
  CHECK((meta::highest_value_in_n_bits(48ULL)) == (281474976710655ULL));
  CHECK((meta::highest_value_in_n_bits(49ULL)) == (562949953421311ULL));
  CHECK((meta::highest_value_in_n_bits(50ULL)) == (1125899906842623ULL));
  CHECK((meta::highest_value_in_n_bits(51ULL)) == (2251799813685247ULL));
  CHECK((meta::highest_value_in_n_bits(52ULL)) == (4503599627370495ULL));
  CHECK((meta::highest_value_in_n_bits(53ULL)) == (9007199254740991ULL));
  CHECK((meta::highest_value_in_n_bits(54ULL)) == (18014398509481983ULL));
  CHECK((meta::highest_value_in_n_bits(55ULL)) == (36028797018963967ULL));
  CHECK((meta::highest_value_in_n_bits(56ULL)) == (72057594037927935ULL));
  CHECK((meta::highest_value_in_n_bits(57ULL)) == (144115188075855871ULL));
  CHECK((meta::highest_value_in_n_bits(58ULL)) == (288230376151711743ULL));
  CHECK((meta::highest_value_in_n_bits(59ULL)) == (576460752303423487ULL));
  CHECK((meta::highest_value_in_n_bits(60ULL)) == (1152921504606846975ULL));
  CHECK((meta::highest_value_in_n_bits(61ULL)) == (2305843009213693951ULL));
  CHECK((meta::highest_value_in_n_bits(62ULL)) == (4611686018427387903ULL));
  CHECK((meta::highest_value_in_n_bits(63ULL)) == (9223372036854775807ULL));
  CHECK((meta::highest_value_in_n_bits(64ULL)) == (18446744073709551615ULL));
}

#pragma endregion
#pragma region EnumPow2

TEST_CASE("EnumPow2", "[MetaTest]") {
  CHECK((meta::pow2(0)) == (1ULL));
  CHECK((meta::pow2(1)) == (2ULL));
  CHECK((meta::pow2(2)) == (4ULL));
  CHECK((meta::pow2(3)) == (8ULL));
  CHECK((meta::pow2(4)) == (16ULL));
  CHECK((meta::pow2(5)) == (32ULL));
  CHECK((meta::pow2(6)) == (64ULL));
  CHECK((meta::pow2(7)) == (128ULL));
  CHECK((meta::pow2(8)) == (256ULL));
  CHECK((meta::pow2(9)) == (512ULL));
  CHECK((meta::pow2(10)) == (1024ULL));
  CHECK((meta::pow2(11)) == (2048ULL));
  CHECK((meta::pow2(12)) == (4096ULL));
  CHECK((meta::pow2(13)) == (8192ULL));
  CHECK((meta::pow2(14)) == (16384ULL));
  CHECK((meta::pow2(15)) == (32768ULL));
  CHECK((meta::pow2(16)) == (65536ULL));
  CHECK((meta::pow2(17)) == (131072ULL));
  CHECK((meta::pow2(18)) == (262144ULL));
  CHECK((meta::pow2(19)) == (524288ULL));
  CHECK((meta::pow2(20)) == (1048576ULL));
  CHECK((meta::pow2(21)) == (2097152ULL));
  CHECK((meta::pow2(22)) == (4194304ULL));
  CHECK((meta::pow2(23)) == (8388608ULL));
  CHECK((meta::pow2(24)) == (16777216ULL));
  CHECK((meta::pow2(25)) == (33554432ULL));
  CHECK((meta::pow2(26)) == (67108864ULL));
  CHECK((meta::pow2(27)) == (134217728ULL));
  CHECK((meta::pow2(28)) == (268435456ULL));
  CHECK((meta::pow2(29)) == (536870912ULL));
  CHECK((meta::pow2(30)) == (1073741824ULL));
  CHECK((meta::pow2(31)) == (2147483648ULL));
  CHECK((meta::pow2(32)) == (4294967296ULL));
  CHECK((meta::pow2(33)) == (8589934592ULL));
  CHECK((meta::pow2(34)) == (17179869184ULL));
  CHECK((meta::pow2(35)) == (34359738368ULL));
  CHECK((meta::pow2(36)) == (68719476736ULL));
  CHECK((meta::pow2(37)) == (137438953472ULL));
  CHECK((meta::pow2(38)) == (274877906944ULL));
  CHECK((meta::pow2(39)) == (549755813888ULL));
  CHECK((meta::pow2(40)) == (1099511627776ULL));
  CHECK((meta::pow2(41)) == (2199023255552ULL));
  CHECK((meta::pow2(42)) == (4398046511104ULL));
  CHECK((meta::pow2(43)) == (8796093022208ULL));
  CHECK((meta::pow2(44)) == (17592186044416ULL));
  CHECK((meta::pow2(45)) == (35184372088832ULL));
  CHECK((meta::pow2(46)) == (70368744177664ULL));
  CHECK((meta::pow2(47)) == (140737488355328ULL));
  CHECK((meta::pow2(48)) == (281474976710656ULL));
  CHECK((meta::pow2(49)) == (562949953421312ULL));
  CHECK((meta::pow2(50)) == (1125899906842624ULL));
  CHECK((meta::pow2(51)) == (2251799813685248ULL));
  CHECK((meta::pow2(52)) == (4503599627370496ULL));
  CHECK((meta::pow2(53)) == (9007199254740992ULL));
  CHECK((meta::pow2(54)) == (18014398509481984ULL));
  CHECK((meta::pow2(55)) == (36028797018963968ULL));
  CHECK((meta::pow2(56)) == (72057594037927936ULL));
  CHECK((meta::pow2(57)) == (144115188075855872ULL));
  CHECK((meta::pow2(58)) == (288230376151711744ULL));
  CHECK((meta::pow2(59)) == (576460752303423488ULL));
  CHECK((meta::pow2(60)) == (1152921504606846976ULL));
  CHECK((meta::pow2(61)) == (2305843009213693952ULL));
  CHECK((meta::pow2(62)) == (4611686018427387904ULL));
  CHECK((meta::pow2(63)) == (9223372036854775808ULL));
  CHECK((meta::pow2(64)) == (0ULL));
}

#pragma endregion
#pragma region SpanConstness

TEST_CASE("SpanConstness", "[MetaTest]") {
  CHECK(((Span<std::span<char>, char>)));
  CHECK(((Span<std::span<char>, const char>)));
  CHECK_FALSE(((Span<std::span<const char>, char>)));
  CHECK(((Span<std::span<const char>, const char>)));
}

#pragma endregion
#pragma region FunctionVoidReturn

TEST_CASE("FunctionVoidReturn", "[MetaTest]") {
  using FNV0 = std::function<void()>;
  using FNV1 = std::function<void(int)>;
  using FNI0 = std::function<int()>;
  using FNI1 = std::function<int(int)>;

  CHECK(((CallableReturningVoid<FNV0>)));
  CHECK(((CallableReturningVoid<FNV1, int>)));
  CHECK_FALSE(((CallableReturningVoid<FNI0>)));
  CHECK_FALSE(((CallableReturningVoid<FNI1, int>)));

  CHECK_FALSE(((CallableReturningNonVoid<FNV0>)));
  CHECK_FALSE(((CallableReturningNonVoid<FNV1, int>)));
  CHECK(((CallableReturningNonVoid<FNI0>)));
  CHECK(((CallableReturningNonVoid<FNI1, int>)));
}

#pragma endregion

// Helper types for specialization tests
struct Foo {};

template<typename T>
struct Goo {};

#pragma region Specialization

TEST_CASE("Specialization", "[MetaTest]") {
  CHECK(((is_specialization_of_v<std::vector<int>, std::vector>)));
  CHECK_FALSE(((is_specialization_of_v<std::vector<int>, std::map>)));
  CHECK_FALSE(((is_specialization_of_v<int, std::map>)));
  CHECK_FALSE(((is_specialization_of_v<int, Goo>)));
  CHECK(((is_specialization_of_v<Goo<int>, Goo>)));

  // Note: These would fail to compile:
  // - is_specialization_of_v<int, char> (char is not a template)
  // - is_specialization_of_v<std::array<int, 4>, std::array> (non-type params)
}

#pragma endregion
#pragma region PointerElement

TEST_CASE("PointerElement", "[MetaTest]") {
  CHECK(((std::is_same_v<int, pointer_element_t<int*>>)));
  CHECK(((std::is_same_v<int, pointer_element_t<std::unique_ptr<int>>>)));
  CHECK(((std::is_same_v<void, pointer_element_t<int>>)));
}

#pragma endregion
#pragma region Dereferenceable

TEST_CASE("Dereferenceable", "[MetaTest]") {
  CHECK(((Dereferenceable<int*>)));
  CHECK(((Dereferenceable<std::unique_ptr<int>>)));
  CHECK_FALSE(((Dereferenceable<int>)));
  CHECK(((Dereferenceable<decltype(std::optional<int>())>)));
}

#pragma endregion
#pragma region IsPair

TEST_CASE("IsPair", "[MetaTest]") {
  CHECK(((is_pair_v<std::pair<int, int>>)));
  CHECK_FALSE(((is_pair_v<std::tuple<int, int>>)));
  CHECK_FALSE(((is_pair_v<int>)));

  // PairConvertible concept replaces the old is_pair_like_v trait.
  // Note: std::tuple<F, S> should be usable to construct std::pair<F, S>,
  // though some standard libraries do not model that as an implicit
  // conversion.
  CHECK(((PairConvertible<std::pair<int, int>>)));
  CHECK(((
      PairConvertible<std::tuple<int, int>>))); // tuple<2> is pair-convertible
  CHECK_FALSE(((PairConvertible<int>)));

  // Test with type aliases and cv-qualifiers
  using T = std::pair<int, int>;
  CHECK(((PairConvertible<T>)));
  using U = const std::pair<int, int>&;
  CHECK(((PairConvertible<U>)));
  using V = std::pair<int, int>&;
  CHECK(((PairConvertible<V>)));
  using W = const std::pair<int, int>;
  CHECK(((PairConvertible<W>)));

  // Note: Tests with intervals::interval skipped (requires Interval.h)
}

#pragma endregion
#pragma region ContainerElement

TEST_CASE("ContainerElement", "[MetaTest]") {
  // Test with pair - extracts the second element (value)
  {
    std::pair<int, int> kv{1, 2};
    auto p = &kv;
    CHECK((container_element_v(p)) == (2));
  }

  // Test with plain value - returns the value itself
  {
    int v{2};
    auto p = &v;
    CHECK((container_element_v(p)) == (2));
  }

  // Test with string element pointer
  {
    std::string s{"abc"};
    CHECK((container_element_v(&s[1])) == ('b'));
  }
}

#pragma endregion
#pragma region KeyFind

TEST_CASE("KeyFind", "[MetaTest]") {
  // has_key_find_v checks if container has find(key_type) method
  using M = std::map<int, Foo>;
  CHECK(((has_key_find_v<M>)));

  using S = std::set<Foo>;
  CHECK(((has_key_find_v<S>)));

  using V = std::vector<int>;
  CHECK_FALSE(((has_key_find_v<V>)));

  // Note: Old two-parameter version (checking specific key type compatibility)
  // and find_ret_t have been removed from the API
}

#pragma endregion
#pragma region TypeName

TEST_CASE("TypeName", "[MetaTest]") {
  using T = std::string;
  using U = const std::string;
  using V = std::string&;
  using W = const std::string&;

  // Same types have same names
  CHECK((type_name<T>()) == (type_name<T>()));

  // Different cv-qualifiers produce different names
  CHECK((type_name<T>()) != (type_name<U>()));
  CHECK((type_name<U>()) != (type_name<V>()));
  CHECK((type_name<V>()) != (type_name<W>()));

  // Value-based overload matches type-based version
  CHECK((type_name<T>()) == (type_name(T{})));
}

#pragma endregion
#pragma region StringViewConvertible

TEST_CASE("StringViewConvertible", "[MetaTest]") {
  // StringViewConvertible concept (replaces is_string_view_convertible_v)
  CHECK(((StringViewConvertible<std::string_view>)));
  CHECK(((StringViewConvertible<std::string>)));
  CHECK(((StringViewConvertible<char*>)));
  CHECK(((StringViewConvertible<char[]>)));
  CHECK_FALSE(((StringViewConvertible<int>)));
  CHECK_FALSE(((StringViewConvertible<std::nullptr_t>)));
  CHECK(((StringViewConvertible<std::string&>)));
  CHECK(((StringViewConvertible<std::string&&>)));

  // Range concept (replaces can_ranged_for_v)
  CHECK_FALSE(((Range<int>)));
  CHECK(((Range<std::vector<int>>)));
  CHECK(((Range<std::string>)));
  CHECK(((Range<int[4]>)));
  CHECK(((Range<char[4]>)));
  CHECK_FALSE(((Range<char*>)));

  // Container concept (replaces is_container_v)
  CHECK_FALSE(((Container<int>)));
  CHECK(((Container<std::vector<int>>)));
  CHECK_FALSE(((Container<std::string>))); // Excluded (StringViewConvertible)
  CHECK(((Container<std::array<int, 2>>)));
  CHECK(((Container<int[4]>)));
  CHECK_FALSE(((Container<char[4]>))); // Excluded (StringViewConvertible)
  CHECK_FALSE(((Container<char*>)));
}

#pragma endregion
#pragma region Number

TEST_CASE("Number", "[MetaTest]") {
  // Integer concept (integral excluding bool)
  CHECK(((Integer<char>)));
  CHECK(((Integer<int>)));
  CHECK_FALSE(((Integer<float>)));
  CHECK_FALSE(((Integer<double>)));

  // Floating point check
  CHECK(((std::floating_point<float>)));
  CHECK(((std::floating_point<double>)));

  // std::byte is an enum, not arithmetic
  CHECK_FALSE(((Integer<std::byte>)));
  CHECK((std::is_enum_v<std::byte>));
  CHECK_FALSE((std::is_enum_v<const std::byte&>));
  CHECK(((StdEnum<const std::byte&>))); // StdEnum strips cvref

  // Arithmetic checks
  CHECK((std::is_arithmetic_v<int>));
  CHECK_FALSE((std::is_arithmetic_v<int&>));

  // Bool is arithmetic but not Integer
  CHECK((std::is_arithmetic_v<bool>));
  CHECK_FALSE(((Integer<bool>)));
  CHECK(((is_bool_v<bool>)));

  enum ColorEnum { red, green = 20, blue };
  enum class ColorClass { red, green = 20, blue };

  CHECK(((StdEnum<ColorClass>)));
  CHECK(((StdEnum<ColorEnum>)));

  // Note: is_number_v (arithmetic excluding bool) no longer exists
  // Use Integer for integral types or std::floating_point for floats
}

#pragma endregion
#pragma region Tuple

TEST_CASE("Tuple", "[MetaTest]") {
  using T0 = std::tuple<>;
  using T2 = std::tuple<int, int>;
  using PI = std::pair<int, int>;
  using I2 = std::array<int, 2>;

  // std::tuple_size works for tuples, pairs, and arrays
  CHECK((std::tuple_size_v<T2>) == (2));
  CHECK((std::tuple_size_v<T0>) == (0));
  CHECK((std::tuple_size_v<PI>) == (2));
  CHECK((std::tuple_size_v<I2>) == (2));

  // is_std_array_v trait
  CHECK(((is_std_array_v<I2>)));
  CHECK_FALSE(((is_std_array_v<T2>)));

  // TupleLike concept (replaces is_tuple_like_v)
  // Note: TupleLike = StdTuple || PairConvertible (excludes array)
  CHECK_FALSE(((TupleLike<I2>))); // array is NOT tuple-like
  CHECK_FALSE(((TupleLike<std::string>)));

  // is_tuple_v trait
  CHECK_FALSE(((is_tuple_v<int>)));
  CHECK(((is_tuple_v<T0>)));
  CHECK(((is_tuple_v<T2>)));
  CHECK_FALSE(((is_tuple_v<PI>)));
  CHECK(((TupleLike<PI>))); // pair is tuple-like
}

#pragma endregion
#pragma region Detection

TEST_CASE("Detection", "[MetaTest]") {
  // initializer_list detection
  {
    auto il = {1, 2, 3};
    CHECK(((is_initializer_list_v<decltype(il)>)));
  }

  // variant detection
  {
    std::variant<int, float> va = 42;
    CHECK(((is_variant_v<decltype(va)>)));
  }

  // OptionalLike concept (replaces is_optional_like_v)
  {
    CHECK(((OptionalLike<std::optional<int>>)));
    CHECK(((OptionalLike<int*>)));
    CHECK_FALSE(((OptionalLike<void*>))); // void* not dereferenceable to value
    CHECK_FALSE(((OptionalLike<const char*>))); // string-like
  }

  // char pointer detection
  {
    CHECK(((is_char_ptr_v<char*>)));
    CHECK(((is_char_ptr_v<const char*>)));
    CHECK(((is_char_ptr_v<char[]>)));
    CHECK(((is_char_ptr_v<const char[]>)));
    CHECK(((is_char_ptr_v<char* const>)));
    CHECK(((is_char_ptr_v<const char* const>)));
    CHECK_FALSE(((is_char_ptr_v<void*>)));
    CHECK_FALSE(((is_char_ptr_v<int*>)));
    CHECK_FALSE(((is_char_ptr_v<char>)));
    const char* psz{};
    CHECK(((is_char_ptr_v<decltype(psz)>)));
  }

  // Note: is_void_ptr_v trait no longer exists
}

#pragma endregion
#pragma region Underlying

TEST_CASE("Underlying", "[MetaTest]") {
  enum class X : size_t { x1 = 1, x2 };
  enum class Y : int64_t { ylow = -1 };
  enum Z { z1 = 1 };

  // as_underlying converts scoped enum to underlying type
  auto x = as_underlying(X::x1);
  CHECK((x) == (1UL));
  CHECK(((std::is_same_v<size_t, decltype(x)>)));

  auto y = as_underlying(Y::ylow);
  CHECK((y) == (-1));
  CHECK(((std::is_same_v<int64_t, decltype(y)>)));

  // as_underlying works for unscoped enums too
  auto z0 = Z::z1;
  auto z = as_underlying(z1);
  CHECK((z0) == (1U));
  CHECK((z) == (1U));
  // Unscoped enum has unsigned int as underlying type
  CHECK(((std::is_same_v<unsigned int, decltype(z)>)));
}

#pragma endregion
#pragma region Streamable

TEST_CASE("Streamable", "[MetaTest]") {
  // OStreamable concept (replaces can_stream_out_v)
  CHECK(((OStreamable<int>)));
  CHECK_FALSE(((OStreamable<Foo>)));
}

#pragma endregion
#pragma region MaybeTypes

TEST_CASE("MaybeTypes", "[MetaTest]") {
  CHECK(((std::is_empty_v<empty_t>)));
  CHECK(((std::is_same_v<maybe_t<int, true>, int>)));
  CHECK(((std::is_same_v<maybe_t<int, false>, empty_t>)));

  CHECK(((std::is_same_v<maybe_void_t<int>, int>)));
  CHECK(((std::is_same_v<maybe_void_t<void>, empty_t>)));
  CHECK(((std::is_same_v<maybe_void_t<>, empty_t>)));

  struct NoExtraSpace {
    [[no_unique_address]] maybe_t<int, false> maybe{42};
    int value{};
  };
  struct Baseline {
    int value{};
  };
  CHECK((sizeof(NoExtraSpace)) == (sizeof(Baseline)));
}

#pragma endregion

// address_forwarder

#pragma region AddressForwarder_Basic

struct Trackable: public address_forwarder<Trackable> {
  int value{};
  explicit Trackable(int v) : value{v} {}
  friend std::ostream& operator<<(std::ostream& os, const Trackable& t) {
    return os << "Trackable{" << t.value << "}";
  }
};

static_assert(AddressForwarder<Trackable>);
static_assert(!AddressForwarder<int>);

TEST_CASE("Basic", "[AddressForwarder]") {
  Trackable t{42};
  CHECK((t.forwarding_address()) == (nullptr));
}

#pragma endregion
#pragma region AddressForwarder_Track

TEST_CASE("Track", "[AddressForwarder]") {
  Trackable* ptr{};
  {
    Trackable t{7};
    ptr = &t;                      // set initial value manually
    t.forwarding_address() = &ptr; // register for future updates
    CHECK((ptr) == (&t));
  }
  // Destruction writes nullptr through the registered pointer.
  CHECK((ptr) == (nullptr));
}

#pragma endregion
#pragma region AddressForwarder_MoveConstruct

TEST_CASE("MoveConstruct", "[AddressForwarder]") {
  Trackable* ptr{};
  Trackable a{1};
  ptr = &a;
  a.forwarding_address() = &ptr;
  CHECK((ptr) == (&a));

  Trackable b{std::move(a)};
  // Move construction updates `ptr` to the new location.
  CHECK((ptr) == (&b));
  // Source no longer holds the forwarding address slot.
  CHECK((a.forwarding_address()) == (nullptr));
}

#pragma endregion
#pragma region AddressForwarder_MoveAssign

TEST_CASE("MoveAssign", "[AddressForwarder]") {
  Trackable* ptr{};
  Trackable a{2};
  ptr = &a;
  a.forwarding_address() = &ptr;
  CHECK((ptr) == (&a));

  Trackable b{99};
  b = std::move(a);
  CHECK((ptr) == (&b));
  CHECK((a.forwarding_address()) == (nullptr));

  // Clearing the forwarding address on `b` stops future tracking, but does
  // not touch the external `ptr` variable itself.
  b.forwarding_address() = nullptr;
  CHECK((ptr) == (&b));
}

TEST_CASE("SelfAssign", "[AddressForwarder]") {
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
  CHECK((ptr) == (&a));
}

#pragma endregion
#pragma region AddressForwarder_DestroySource

TEST_CASE("DestroySource", "[AddressForwarder]") {
  Trackable* ptr{};
  Trackable b{0};
  {
    Trackable a{5};
    ptr = &a;
    a.forwarding_address() = &ptr;
    b = std::move(a);
    // `a` no longer owns the slot; destroying it must not null `ptr`.
  }
  CHECK((ptr) == (&b));
  b.forwarding_address() = nullptr;
}

#pragma endregion

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

#pragma region AddressForwarder_AsBaseMove

TEST_CASE("AsBaseMove", "[AddressForwarder]") {
  Trackable2* ptr{};
  Trackable2 a{8};
  ptr = &a;
  a.forwarding_address() = &ptr;
  CHECK((ptr) == (&a));

  Trackable2 b{std::move(a)};
  CHECK((ptr) == (&b));
  CHECK((a.forwarding_address()) == (nullptr));
}

#pragma endregion
#pragma region AddressForwarder_BoundFunction

TEST_CASE("BoundFunction", "[AddressForwarder]") {
  // Primary use case: an object is moved into a `std::function` closure, and
  // a pointer registered before the move chain tracks it to its final home.
  Trackable* ptr{};
  Trackable t{99};
  t.forwarding_address() = &ptr;
  // ptr is still null here; each move in the chain below will update it.

  std::function<int()> fn = [t = std::move(t)]() mutable { return t.value; };

  // ptr now points to `t` inside fn's internal storage.
  CHECK((ptr) != (nullptr));
  CHECK((ptr->value) == (99));

  // fn() and ptr refer to the same object.
  CHECK((fn()) == (99));
  ptr->value = 42;
  CHECK((fn()) == (42));

  // Clear forwarding so fn's destructor does not write through the soon-to-be
  // dangling &ptr.
  ptr->forwarding_address() = nullptr;
}

#pragma endregion

// fixed_function compile-time size checks
static_assert(sizeof(fixed_function<64, int()>) == 64);
static_assert(sizeof(fixed_function<32, void(int, double)>) == 32);
static_assert(sizeof(fixed_function<128, int(int, int)>) == 128);

#pragma region FixedFunction_Basic

TEST_CASE("Basic", "[FixedFunction]") {
  fixed_function<64, int()> f{[] { return 42; }};
  CHECK((f()) == (42));
}

#pragma endregion
#pragma region FixedFunction_Args

TEST_CASE("Args", "[FixedFunction]") {
  fixed_function<64, int(int, int)> add{[](int x, int y) { return x + y; }};
  CHECK((add(3, 4)) == (7));
  CHECK((add(10, -3)) == (7));
}

#pragma endregion
#pragma region FixedFunction_Bool

TEST_CASE("Bool", "[FixedFunction]") {
  fixed_function<64, int()> a{[] { return 1; }};
  CHECK((static_cast<bool>(a)));
  fixed_function<64, int()> b{std::move(a)};
  CHECK_FALSE((static_cast<bool>(a)));
  CHECK((static_cast<bool>(b)));
}

#pragma endregion
#pragma region FixedFunction_Move

TEST_CASE("Move", "[FixedFunction]") {
  fixed_function<64, int()> a{[] { return 7; }};
  CHECK((static_cast<bool>(a)));
  fixed_function<64, int()> b{std::move(a)};
  CHECK_FALSE((static_cast<bool>(a)));
  CHECK((static_cast<bool>(b)));
  CHECK((b()) == (7));
}

#pragma endregion
#pragma region FixedFunction_MoveAssign

TEST_CASE("MoveAssign", "[FixedFunction]") {
  fixed_function<64, int()> a{[] { return 99; }};
  fixed_function<64, int()> b{[] { return 0; }};
  b = std::move(a);
  CHECK_FALSE((static_cast<bool>(a)));
  CHECK((static_cast<bool>(b)));
  CHECK((b()) == (99));

  b = nullptr;
  CHECK_FALSE((static_cast<bool>(b)));
}

#pragma endregion
#pragma region FixedFunction_Destructor

TEST_CASE("Destructor", "[FixedFunction]") {
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
    CHECK((n) == (1)); // temporary destroyed after move into storage
    {
      fixed_function<64, void()> g{std::move(f)};
      CHECK((n) == (2)); // move ctor immediately destructs f's storage
    } // g destroyed: Counted in g.storage_ destructed
    CHECK((n) == (3));
  } // f destroyed: manage_ is null, nothing happens
  CHECK((n) == (3));
}

#pragma endregion

// Free function used by FixedFunction_CppRef.
static int cpref_num(int i) { return i; }

// Shared variable used by FixedFunction_RefReturn.
static int g_ref_val = 42;

// Free function used by FixedFunction_FreeFn.
static int double_it(int x) { return x * 2; }

// Mirrors the cppreference.com `std::function` sample.
// Member functions return values instead of printing so results are
// verifiable.
#pragma region FixedFunction_CppRef

TEST_CASE("CppRef", "[FixedFunction]") {
  struct Foo {
    Foo(int num) : num_(num) {}
    int add(int i) const { return num_ + i; }
    int num_;
  };
  struct PrintNum {
    int operator()(int i) const { return i; }
  };

  // store a free function
  fixed_function<64, int(int)> f_display{&cpref_num};
  CHECK((f_display(-9)) == (-9));

  // store a lambda
  fixed_function<64, int()> f_display_42{[] { return cpref_num(42); }};
  CHECK((f_display_42()) == (42));

  // store the result of a call to std::bind
  fixed_function<64, int()> f_display_31337{std::bind(cpref_num, 31337)};
  CHECK((f_display_31337()) == (31337));

  // store a call to a member function
  fixed_function<64, int(const Foo&, int)> f_add_display{&Foo::add};
  const Foo foo{314159};
  CHECK((f_add_display(foo, 1)) == (314160));
  CHECK((f_add_display(314159, 1)) == (314160)); // implicit Foo from int

  // store a call to a data member accessor
  fixed_function<64, int(const Foo&)> f_num{&Foo::num_};
  CHECK((f_num(foo)) == (314159));

  // store a call to a member function and object
  using std::placeholders::_1;
  fixed_function<64, int(int)> f_add_display2{std::bind(&Foo::add, foo, _1)};
  CHECK((f_add_display2(2)) == (314161));

  // store a call to a member function and object ptr
  fixed_function<64, int(int)> f_add_display3{std::bind(&Foo::add, &foo, _1)};
  CHECK((f_add_display3(3)) == (314162));

  // store a call to a function object
  fixed_function<64, int(int)> f_display_obj{PrintNum{}};
  CHECK((f_display_obj(18)) == (18));

  // recursive lambda: same self-referential pattern as the cppreference
  // factorial example, using fixed_function instead of std::function
  auto factorial = [](int n) {
    fixed_function<64, int(int)> fac;
    fac = fixed_function<64, int(int)>{[&fac](int k) -> int {
      return (k < 2) ? 1 : k * fac(k - 1);
    }};
    return fac(n);
  };
  CHECK((factorial(5)) == (120));
  CHECK((factorial(6)) == (720));
  CHECK((factorial(7)) == (5040));
}

#pragma endregion
#pragma region FixedFunction_RefReturn

TEST_CASE("RefReturn", "[FixedFunction]") {
  // Callables that return an actual reference are safe.
  fixed_function<64, int&()> f{[&]() -> int& { return g_ref_val; }};
  CHECK((f()) == (42));
  f() = 99;
  CHECK((g_ref_val) == (99));
  g_ref_val = 42; // restore

  fixed_function<64, const int&()> g{[&]() -> const int& {
    return g_ref_val;
  }};
  CHECK((g()) == (42));

  fixed_function<64, int&&()> h{[]() -> int&& {
    return static_cast<int&&>(g_ref_val);
  }};
  CHECK((h()) == (42));

#ifdef NOT_SUPPOSED_TO_COMPILE
  // Both of these trigger the static_assert: callable returns a prvalue `int`
  // but the declared return type is a reference, so every call would dangle.
  fixed_function<64, int&()> bad1{[] { return 42; }};
  fixed_function<64, const int&()> bad2{[] { return 42; }};
#endif
}

#pragma endregion
#pragma region FixedFunction_EmptyThrows

TEST_CASE("EmptyThrows", "[FixedFunction]") {
  // Default-constructed instance is empty and throws on call.
  fixed_function<64, int()> empty{};
  CHECK((!empty));
  CHECK_THROWS_AS(empty(), std::bad_function_call);

  // Moved-from instance is also empty and throws on call.
  fixed_function<64, int()> f{[] { return 1; }};
  fixed_function<64, int()> g{std::move(f)};
  CHECK((!f));
  CHECK_THROWS_AS(f(), std::bad_function_call);

  // nullptr-assigned instance throws too.
  g = nullptr;
  CHECK_THROWS_AS(g(), std::bad_function_call);
}

#pragma endregion
#pragma region FixedFunction_FreeFn

TEST_CASE("FreeFn", "[FixedFunction]") {
  // A plain function pointer satisfies MoveConsumable (it is a prvalue).
  fixed_function<64, int(int)> f{&double_it};
  CHECK((static_cast<bool>(f)));
  CHECK((f(21)) == (42));
}

#pragma endregion
#pragma region FixedFunction_Functor

TEST_CASE("Functor", "[FixedFunction]") {
  struct Adder {
    int n;
    int operator()(int x) const { return x + n; }
  };
  fixed_function<64, int(int)> f{Adder{10}};
  CHECK((f(32)) == (42));
}

#pragma endregion
#pragma region FixedFunction_Swap

TEST_CASE("Swap", "[FixedFunction]") {
  using ff = fixed_function<64, int()>;
  ff a{[] { return 1; }};
  ff b{[] { return 2; }};

  // Member swap.
  a.swap(b);
  CHECK((a()) == (2));
  CHECK((b()) == (1));

  // ADL swap (finds the hidden-friend in namespace corvid::meta).
  using std::swap;
  swap(a, b);
  CHECK((a()) == (1));
  CHECK((b()) == (2));

  // Swap a full instance with an empty one.
  ff empty{};
  a.swap(empty);
  CHECK_FALSE((static_cast<bool>(a)));
  CHECK((static_cast<bool>(empty)));
  CHECK((empty()) == (1));

  // Swap two empty instances is a no-op.
  ff empty2{};
  a.swap(empty2);
  CHECK_FALSE((static_cast<bool>(a)));
  CHECK_FALSE((static_cast<bool>(empty2)));
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
