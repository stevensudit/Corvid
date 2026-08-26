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

#include "corvid/meta.h"
#include "catch2_main.h"

#include <bit>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>

// #include "Interval.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(readability-function-size)

// OStreamDerived

auto& stream_out(OStreamDerived auto& os, const OStreamable auto& osb) {
  return os << osb;
}

#pragma region OStreamdDerived

TEST_CASE("OStreamdDerived", "[MetaTest]") {
  std::ostringstream oss;
  stream_out(oss, 1);
  CHECK(oss.str() == "1");
#ifdef NOT_SUPPOSED_TO_COMPILE
  std::string s{"Hello"};
  foo(s, 42);
  stream_out(oss, oss);
#endif
}

#pragma endregion
#pragma region EnumBitWidth

TEST_CASE("EnumBitWidth", "[MetaTest]") {
  CHECK(std::bit_width(0ULL) == 0);
  CHECK(std::bit_width(1ULL) == 1);
  CHECK(std::bit_width(2ULL) == 2);
  CHECK(std::bit_width(3ULL) == 2);
  CHECK(std::bit_width(4ULL) == 3);
  CHECK(std::bit_width(7ULL) == 3);
  CHECK(std::bit_width(8ULL) == 4);
  CHECK(std::bit_width(15ULL) == 4);
  CHECK(std::bit_width(16ULL) == 5);
  CHECK(std::bit_width(31ULL) == 5);
  CHECK(std::bit_width(32ULL) == 6);
  CHECK(std::bit_width(63ULL) == 6);
  CHECK(std::bit_width(64ULL) == 7);
  CHECK(std::bit_width(127ULL) == 7);
  CHECK(std::bit_width(128ULL) == 8);
  CHECK(std::bit_width(255ULL) == 8);
  CHECK(std::bit_width(256ULL) == 9);
  CHECK(std::bit_width(511ULL) == 9);
  CHECK(std::bit_width(512ULL) == 10);
  CHECK(std::bit_width(1023ULL) == 10);
  CHECK(std::bit_width(1024ULL) == 11);
  CHECK(std::bit_width(2047ULL) == 11);
  CHECK(std::bit_width(2048ULL) == 12);
  CHECK(std::bit_width(4095ULL) == 12);
  CHECK(std::bit_width(4096ULL) == 13);
  CHECK(std::bit_width(8191ULL) == 13);
  CHECK(std::bit_width(8192ULL) == 14);
  CHECK(std::bit_width(16383ULL) == 14);
  CHECK(std::bit_width(16384ULL) == 15);
  CHECK(std::bit_width(32767ULL) == 15);
  CHECK(std::bit_width(32768ULL) == 16);
  CHECK(std::bit_width(65535ULL) == 16);
  CHECK(std::bit_width(65536ULL) == 17);
  CHECK(std::bit_width(131071ULL) == 17);
  CHECK(std::bit_width(131072ULL) == 18);
  CHECK(std::bit_width(262143ULL) == 18);
  CHECK(std::bit_width(262144ULL) == 19);
  CHECK(std::bit_width(524287ULL) == 19);
  CHECK(std::bit_width(524288ULL) == 20);
  CHECK(std::bit_width(1048575ULL) == 20);
  CHECK(std::bit_width(1048576ULL) == 21);
  CHECK(std::bit_width(2097151ULL) == 21);
  CHECK(std::bit_width(2097152ULL) == 22);
  CHECK(std::bit_width(4194303ULL) == 22);
  CHECK(std::bit_width(4194304ULL) == 23);
  CHECK(std::bit_width(8388607ULL) == 23);
  CHECK(std::bit_width(8388608ULL) == 24);
  CHECK(std::bit_width(16777215ULL) == 24);
  CHECK(std::bit_width(16777216ULL) == 25);
  CHECK(std::bit_width(33554431ULL) == 25);
  CHECK(std::bit_width(33554432ULL) == 26);
  CHECK(std::bit_width(67108863ULL) == 26);
  CHECK(std::bit_width(67108864ULL) == 27);
  CHECK(std::bit_width(134217727ULL) == 27);
  CHECK(std::bit_width(134217728ULL) == 28);
  CHECK(std::bit_width(268435455ULL) == 28);
  CHECK(std::bit_width(268435456ULL) == 29);
  CHECK(std::bit_width(536870911ULL) == 29);
  CHECK(std::bit_width(536870912ULL) == 30);
  CHECK(std::bit_width(1073741823ULL) == 30);
  CHECK(std::bit_width(1073741824ULL) == 31);
  CHECK(std::bit_width(2147483647ULL) == 31);
  CHECK(std::bit_width(2147483648ULL) == 32);
  CHECK(std::bit_width(4294967295ULL) == 32);
  CHECK(std::bit_width(4294967296ULL) == 33);
  CHECK(std::bit_width(8589934591ULL) == 33);
  CHECK(std::bit_width(8589934592ULL) == 34);
  CHECK(std::bit_width(17179869183ULL) == 34);
  CHECK(std::bit_width(17179869184ULL) == 35);
  CHECK(std::bit_width(34359738367ULL) == 35);
  CHECK(std::bit_width(34359738368ULL) == 36);
  CHECK(std::bit_width(68719476735ULL) == 36);
  CHECK(std::bit_width(68719476736ULL) == 37);
  CHECK(std::bit_width(137438953471ULL) == 37);
  CHECK(std::bit_width(137438953472ULL) == 38);
  CHECK(std::bit_width(274877906943ULL) == 38);
  CHECK(std::bit_width(274877906944ULL) == 39);
  CHECK(std::bit_width(549755813887ULL) == 39);
  CHECK(std::bit_width(549755813888ULL) == 40);
  CHECK(std::bit_width(1099511627775ULL) == 40);
  CHECK(std::bit_width(1099511627776ULL) == 41);
  CHECK(std::bit_width(2199023255551ULL) == 41);
  CHECK(std::bit_width(2199023255552ULL) == 42);
  CHECK(std::bit_width(4398046511103ULL) == 42);
  CHECK(std::bit_width(4398046511104ULL) == 43);
  CHECK(std::bit_width(8796093022207ULL) == 43);
  CHECK(std::bit_width(8796093022208ULL) == 44);
  CHECK(std::bit_width(17592186044415ULL) == 44);
  CHECK(std::bit_width(17592186044416ULL) == 45);
  CHECK(std::bit_width(35184372088831ULL) == 45);
  CHECK(std::bit_width(35184372088832ULL) == 46);
  CHECK(std::bit_width(70368744177663ULL) == 46);
  CHECK(std::bit_width(70368744177664ULL) == 47);
  CHECK(std::bit_width(140737488355327ULL) == 47);
  CHECK(std::bit_width(140737488355328ULL) == 48);
  CHECK(std::bit_width(281474976710655ULL) == 48);
  CHECK(std::bit_width(281474976710656ULL) == 49);
  CHECK(std::bit_width(562949953421311ULL) == 49);
  CHECK(std::bit_width(562949953421312ULL) == 50);
  CHECK(std::bit_width(1125899906842623ULL) == 50);
  CHECK(std::bit_width(1125899906842624ULL) == 51);
  CHECK(std::bit_width(2251799813685247ULL) == 51);
  CHECK(std::bit_width(2251799813685248ULL) == 52);
  CHECK(std::bit_width(4503599627370495ULL) == 52);
  CHECK(std::bit_width(4503599627370496ULL) == 53);
  CHECK(std::bit_width(9007199254740991ULL) == 53);
  CHECK(std::bit_width(9007199254740992ULL) == 54);
  CHECK(std::bit_width(18014398509481983ULL) == 54);
  CHECK(std::bit_width(18014398509481984ULL) == 55);
  CHECK(std::bit_width(36028797018963967ULL) == 55);
  CHECK(std::bit_width(36028797018963968ULL) == 56);
  CHECK(std::bit_width(72057594037927935ULL) == 56);
  CHECK(std::bit_width(72057594037927936ULL) == 57);
  CHECK(std::bit_width(144115188075855871ULL) == 57);
  CHECK(std::bit_width(144115188075855872ULL) == 58);
  CHECK(std::bit_width(288230376151711743ULL) == 58);
  CHECK(std::bit_width(288230376151711744ULL) == 59);
  CHECK(std::bit_width(576460752303423487ULL) == 59);
  CHECK(std::bit_width(576460752303423488ULL) == 60);
  CHECK(std::bit_width(1152921504606846975ULL) == 60);
  CHECK(std::bit_width(1152921504606846976ULL) == 61);
  CHECK(std::bit_width(2305843009213693951ULL) == 61);
  CHECK(std::bit_width(2305843009213693952ULL) == 62);
  CHECK(std::bit_width(4611686018427387903ULL) == 62);
  CHECK(std::bit_width(4611686018427387904ULL) == 63);
  CHECK(std::bit_width(9223372036854775807ULL) == 63);
  CHECK(std::bit_width(9223372036854775808ULL) == 64);
  CHECK(std::bit_width(18446744073709551615ULL) == 64);
}

#pragma endregion
#pragma region EnumHighestValueInNBits

TEST_CASE("EnumHighestValueInNBits", "[MetaTest]") {
  CHECK(meta::highest_value_in_n_bits(0ULL) == 0ULL);
  CHECK(meta::highest_value_in_n_bits(1ULL) == 1ULL);
  CHECK(meta::highest_value_in_n_bits(2ULL) == 3ULL);
  CHECK(meta::highest_value_in_n_bits(3ULL) == 7ULL);
  CHECK(meta::highest_value_in_n_bits(4ULL) == 15ULL);
  CHECK(meta::highest_value_in_n_bits(5ULL) == 31ULL);
  CHECK(meta::highest_value_in_n_bits(6ULL) == 63ULL);
  CHECK(meta::highest_value_in_n_bits(7ULL) == 127ULL);
  CHECK(meta::highest_value_in_n_bits(8ULL) == 255ULL);
  CHECK(meta::highest_value_in_n_bits(9ULL) == 511ULL);
  CHECK(meta::highest_value_in_n_bits(10ULL) == 1023ULL);
  CHECK(meta::highest_value_in_n_bits(11ULL) == 2047ULL);
  CHECK(meta::highest_value_in_n_bits(12ULL) == 4095ULL);
  CHECK(meta::highest_value_in_n_bits(13ULL) == 8191ULL);
  CHECK(meta::highest_value_in_n_bits(14ULL) == 16383ULL);
  CHECK(meta::highest_value_in_n_bits(15ULL) == 32767ULL);
  CHECK(meta::highest_value_in_n_bits(16ULL) == 65535ULL);
  CHECK(meta::highest_value_in_n_bits(17ULL) == 131071ULL);
  CHECK(meta::highest_value_in_n_bits(18ULL) == 262143ULL);
  CHECK(meta::highest_value_in_n_bits(19ULL) == 524287ULL);
  CHECK(meta::highest_value_in_n_bits(20ULL) == 1048575ULL);
  CHECK(meta::highest_value_in_n_bits(21ULL) == 2097151ULL);
  CHECK(meta::highest_value_in_n_bits(22ULL) == 4194303ULL);
  CHECK(meta::highest_value_in_n_bits(23ULL) == 8388607ULL);
  CHECK(meta::highest_value_in_n_bits(24ULL) == 16777215ULL);
  CHECK(meta::highest_value_in_n_bits(25ULL) == 33554431ULL);
  CHECK(meta::highest_value_in_n_bits(26ULL) == 67108863ULL);
  CHECK(meta::highest_value_in_n_bits(27ULL) == 134217727ULL);
  CHECK(meta::highest_value_in_n_bits(28ULL) == 268435455ULL);
  CHECK(meta::highest_value_in_n_bits(29ULL) == 536870911ULL);
  CHECK(meta::highest_value_in_n_bits(30ULL) == 1073741823ULL);
  CHECK(meta::highest_value_in_n_bits(31ULL) == 2147483647ULL);
  CHECK(meta::highest_value_in_n_bits(32ULL) == 4294967295ULL);
  CHECK(meta::highest_value_in_n_bits(33ULL) == 8589934591ULL);
  CHECK(meta::highest_value_in_n_bits(34ULL) == 17179869183ULL);
  CHECK(meta::highest_value_in_n_bits(35ULL) == 34359738367ULL);
  CHECK(meta::highest_value_in_n_bits(36ULL) == 68719476735ULL);
  CHECK(meta::highest_value_in_n_bits(37ULL) == 137438953471ULL);
  CHECK(meta::highest_value_in_n_bits(38ULL) == 274877906943ULL);
  CHECK(meta::highest_value_in_n_bits(39ULL) == 549755813887ULL);
  CHECK(meta::highest_value_in_n_bits(40ULL) == 1099511627775ULL);
  CHECK(meta::highest_value_in_n_bits(41ULL) == 2199023255551ULL);
  CHECK(meta::highest_value_in_n_bits(42ULL) == 4398046511103ULL);
  CHECK(meta::highest_value_in_n_bits(43ULL) == 8796093022207ULL);
  CHECK(meta::highest_value_in_n_bits(44ULL) == 17592186044415ULL);
  CHECK(meta::highest_value_in_n_bits(45ULL) == 35184372088831ULL);
  CHECK(meta::highest_value_in_n_bits(46ULL) == 70368744177663ULL);
  CHECK(meta::highest_value_in_n_bits(47ULL) == 140737488355327ULL);
  CHECK(meta::highest_value_in_n_bits(48ULL) == 281474976710655ULL);
  CHECK(meta::highest_value_in_n_bits(49ULL) == 562949953421311ULL);
  CHECK(meta::highest_value_in_n_bits(50ULL) == 1125899906842623ULL);
  CHECK(meta::highest_value_in_n_bits(51ULL) == 2251799813685247ULL);
  CHECK(meta::highest_value_in_n_bits(52ULL) == 4503599627370495ULL);
  CHECK(meta::highest_value_in_n_bits(53ULL) == 9007199254740991ULL);
  CHECK(meta::highest_value_in_n_bits(54ULL) == 18014398509481983ULL);
  CHECK(meta::highest_value_in_n_bits(55ULL) == 36028797018963967ULL);
  CHECK(meta::highest_value_in_n_bits(56ULL) == 72057594037927935ULL);
  CHECK(meta::highest_value_in_n_bits(57ULL) == 144115188075855871ULL);
  CHECK(meta::highest_value_in_n_bits(58ULL) == 288230376151711743ULL);
  CHECK(meta::highest_value_in_n_bits(59ULL) == 576460752303423487ULL);
  CHECK(meta::highest_value_in_n_bits(60ULL) == 1152921504606846975ULL);
  CHECK(meta::highest_value_in_n_bits(61ULL) == 2305843009213693951ULL);
  CHECK(meta::highest_value_in_n_bits(62ULL) == 4611686018427387903ULL);
  CHECK(meta::highest_value_in_n_bits(63ULL) == 9223372036854775807ULL);
  CHECK(meta::highest_value_in_n_bits(64ULL) == 18446744073709551615ULL);
}

#pragma endregion
#pragma region EnumPow2

TEST_CASE("EnumPow2", "[MetaTest]") {
  CHECK(meta::pow2(0) == 1ULL);
  CHECK(meta::pow2(1) == 2ULL);
  CHECK(meta::pow2(2) == 4ULL);
  CHECK(meta::pow2(3) == 8ULL);
  CHECK(meta::pow2(4) == 16ULL);
  CHECK(meta::pow2(5) == 32ULL);
  CHECK(meta::pow2(6) == 64ULL);
  CHECK(meta::pow2(7) == 128ULL);
  CHECK(meta::pow2(8) == 256ULL);
  CHECK(meta::pow2(9) == 512ULL);
  CHECK(meta::pow2(10) == 1024ULL);
  CHECK(meta::pow2(11) == 2048ULL);
  CHECK(meta::pow2(12) == 4096ULL);
  CHECK(meta::pow2(13) == 8192ULL);
  CHECK(meta::pow2(14) == 16384ULL);
  CHECK(meta::pow2(15) == 32768ULL);
  CHECK(meta::pow2(16) == 65536ULL);
  CHECK(meta::pow2(17) == 131072ULL);
  CHECK(meta::pow2(18) == 262144ULL);
  CHECK(meta::pow2(19) == 524288ULL);
  CHECK(meta::pow2(20) == 1048576ULL);
  CHECK(meta::pow2(21) == 2097152ULL);
  CHECK(meta::pow2(22) == 4194304ULL);
  CHECK(meta::pow2(23) == 8388608ULL);
  CHECK(meta::pow2(24) == 16777216ULL);
  CHECK(meta::pow2(25) == 33554432ULL);
  CHECK(meta::pow2(26) == 67108864ULL);
  CHECK(meta::pow2(27) == 134217728ULL);
  CHECK(meta::pow2(28) == 268435456ULL);
  CHECK(meta::pow2(29) == 536870912ULL);
  CHECK(meta::pow2(30) == 1073741824ULL);
  CHECK(meta::pow2(31) == 2147483648ULL);
  CHECK(meta::pow2(32) == 4294967296ULL);
  CHECK(meta::pow2(33) == 8589934592ULL);
  CHECK(meta::pow2(34) == 17179869184ULL);
  CHECK(meta::pow2(35) == 34359738368ULL);
  CHECK(meta::pow2(36) == 68719476736ULL);
  CHECK(meta::pow2(37) == 137438953472ULL);
  CHECK(meta::pow2(38) == 274877906944ULL);
  CHECK(meta::pow2(39) == 549755813888ULL);
  CHECK(meta::pow2(40) == 1099511627776ULL);
  CHECK(meta::pow2(41) == 2199023255552ULL);
  CHECK(meta::pow2(42) == 4398046511104ULL);
  CHECK(meta::pow2(43) == 8796093022208ULL);
  CHECK(meta::pow2(44) == 17592186044416ULL);
  CHECK(meta::pow2(45) == 35184372088832ULL);
  CHECK(meta::pow2(46) == 70368744177664ULL);
  CHECK(meta::pow2(47) == 140737488355328ULL);
  CHECK(meta::pow2(48) == 281474976710656ULL);
  CHECK(meta::pow2(49) == 562949953421312ULL);
  CHECK(meta::pow2(50) == 1125899906842624ULL);
  CHECK(meta::pow2(51) == 2251799813685248ULL);
  CHECK(meta::pow2(52) == 4503599627370496ULL);
  CHECK(meta::pow2(53) == 9007199254740992ULL);
  CHECK(meta::pow2(54) == 18014398509481984ULL);
  CHECK(meta::pow2(55) == 36028797018963968ULL);
  CHECK(meta::pow2(56) == 72057594037927936ULL);
  CHECK(meta::pow2(57) == 144115188075855872ULL);
  CHECK(meta::pow2(58) == 288230376151711744ULL);
  CHECK(meta::pow2(59) == 576460752303423488ULL);
  CHECK(meta::pow2(60) == 1152921504606846976ULL);
  CHECK(meta::pow2(61) == 2305843009213693952ULL);
  CHECK(meta::pow2(62) == 4611686018427387904ULL);
  CHECK(meta::pow2(63) == 9223372036854775808ULL);
  CHECK(meta::pow2(64) == 0ULL);
}

#pragma endregion
#pragma region SpanConstness

TEST_CASE("SpanConstness", "[MetaTest]") {
  CHECK((Span<std::span<char>, char>));
  CHECK((Span<std::span<char>, const char>));
  CHECK_FALSE((Span<std::span<const char>, char>));
  CHECK((Span<std::span<const char>, const char>));
}

#pragma endregion
#pragma region CharArray

TEST_CASE("CharArray", "[MetaTest]") {
  CHECK(CharArray<char[5]>);
  CHECK(CharArray<const char[5]>);
  CHECK(CharArray<char (&)[5]>);
  CHECK(CharArray<const char (&)[5]>);
  CHECK_FALSE(CharArray<char*>);
  CHECK_FALSE(CharArray<int[5]>);
}

#pragma endregion
#pragma region NullPtr

TEST_CASE("NullPtr", "[MetaTest]") {
  CHECK(NullPtr<std::nullptr_t>);
  CHECK(NullPtr<const std::nullptr_t&>);
  CHECK_FALSE(NullPtr<void*>);
}

#pragma endregion
#pragma region FunctionVoidReturn

TEST_CASE("FunctionVoidReturn", "[MetaTest]") {
  using FNV0 = std::function<void()>;
  using FNV1 = std::function<void(int)>;
  using FNI0 = std::function<int()>;
  using FNI1 = std::function<int(int)>;

  CHECK(CallableReturningVoid<FNV0>);
  CHECK((CallableReturningVoid<FNV1, int>));
  CHECK_FALSE(CallableReturningVoid<FNI0>);
  CHECK_FALSE((CallableReturningVoid<FNI1, int>));

  CHECK_FALSE(CallableReturningNonVoid<FNV0>);
  CHECK_FALSE((CallableReturningNonVoid<FNV1, int>));
  CHECK(CallableReturningNonVoid<FNI0>);
  CHECK((CallableReturningNonVoid<FNI1, int>));
}

#pragma endregion

// Helper types for specialization tests
struct Foo {};

template<typename T>
struct Goo {};

#pragma region Specialization

TEST_CASE("Specialization", "[MetaTest]") {
  CHECK((is_specialization_of_v<std::vector<int>, std::vector>));
  CHECK_FALSE((is_specialization_of_v<std::vector<int>, std::map>));
  CHECK_FALSE((is_specialization_of_v<int, std::map>));
  CHECK_FALSE((is_specialization_of_v<int, Goo>));
  CHECK((is_specialization_of_v<Goo<int>, Goo>));

  // Note: These would fail to compile:
  // - is_specialization_of_v<int, char> (char is not a template)
  // - is_specialization_of_v<std::array<int, 4>, std::array> (non-type params)
}

#pragma endregion
#pragma region PointerElement

TEST_CASE("PointerElement", "[MetaTest]") {
  CHECK((std::is_same_v<int, pointer_element_t<int*>>));
  CHECK((std::is_same_v<int, pointer_element_t<std::unique_ptr<int>>>));
  CHECK((std::is_same_v<void, pointer_element_t<int>>));
}

#pragma endregion
#pragma region Dereferenceable

TEST_CASE("Dereferenceable", "[MetaTest]") {
  CHECK(Dereferenceable<int*>);
  CHECK(Dereferenceable<std::unique_ptr<int>>);
  CHECK_FALSE(Dereferenceable<int>);
  CHECK(Dereferenceable<decltype(std::optional<int>())>);
}

#pragma endregion
#pragma region IsPair

TEST_CASE("IsPair", "[MetaTest]") {
  CHECK((is_pair_v<std::pair<int, int>>));
  CHECK_FALSE((is_pair_v<std::tuple<int, int>>));
  CHECK_FALSE(is_pair_v<int>);

  // PairConvertible concept replaces the old is_pair_like_v trait.
  // Note: std::tuple<F, S> should be usable to construct std::pair<F, S>,
  // though some standard libraries do not model that as an implicit
  // conversion.
  CHECK((PairConvertible<std::pair<int, int>>));
  CHECK(
      (PairConvertible<std::tuple<int, int>>)); // tuple<2> is pair-convertible
  CHECK_FALSE(PairConvertible<int>);

  // Test with type aliases and cv-qualifiers
  using T = std::pair<int, int>;
  CHECK(PairConvertible<T>);
  using U = const std::pair<int, int>&;
  CHECK(PairConvertible<U>);
  using V = std::pair<int, int>&;
  CHECK(PairConvertible<V>);
  using W = const std::pair<int, int>;
  CHECK(PairConvertible<W>);

  // Note: Tests with intervals::interval skipped (requires Interval.h)
}

#pragma endregion
#pragma region ContainerElement

TEST_CASE("ContainerElement", "[MetaTest]") {
  // Test with pair - extracts the second element (value)
  {
    std::pair<int, int> kv{1, 2};
    auto p = &kv;
    CHECK(container_element_v(p) == 2);
  }

  // Test with plain value - returns the value itself
  {
    int v{2};
    auto p = &v;
    CHECK(container_element_v(p) == 2);
  }

  // Test with string element pointer
  {
    std::string s{"abc"};
    CHECK(container_element_v(&s[1]) == 'b');
  }
}

#pragma endregion
#pragma region KeyFind

TEST_CASE("KeyFind", "[MetaTest]") {
  // has_key_find_v checks if container has find(key_type) method
  using M = std::map<int, Foo>;
  CHECK(has_key_find_v<M>);

  using S = std::set<Foo>;
  CHECK(has_key_find_v<S>);

  using V = std::vector<int>;
  CHECK_FALSE(has_key_find_v<V>);

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
  CHECK(type_name<T>() == type_name<T>());

  // Different cv-qualifiers produce different names
  CHECK(type_name<T>() != type_name<U>());
  CHECK(type_name<U>() != type_name<V>());
  CHECK(type_name<V>() != type_name<W>());

  // Value-based overload matches type-based version
  CHECK(type_name<T>() == type_name(T{}));

  // Friendly names collapse spaced closing brackets at any nesting depth,
  // not just pairs.
  CHECK(friendly_type_name<std::vector<std::vector<std::vector<int>>>>() ==
        "std::vector<std::vector<std::vector<int, std::allocator<int>>, "
        "std::allocator<std::vector<int, std::allocator<int>>>>, "
        "std::allocator<std::vector<std::vector<int, std::allocator<int>>, "
        "std::allocator<std::vector<int, std::allocator<int>>>>>>");

  // An incomplete collapse used to leave a space that broke the
  // `std::string` contraction for nested strings.
  CHECK(friendly_type_name<std::vector<std::string>>() ==
        "std::vector<std::string, std::allocator<std::string>>");

  // The space before east const is not bracket spacing and must survive the
  // collapse. (The tail spelling is platform-dependent, so no exact golden.)
  const auto ptr = friendly_type_name<const std::vector<int>*>();
  CHECK(ptr.contains("> const"));
  CHECK_FALSE(ptr.contains("> >"));

  // Top-level cv on a pointer is spelled trailing: a leading const would
  // name a different type (pointer-to-const). Pointer spacing is
  // platform-dependent, so predicates rather than goldens.
  const auto cptr = friendly_type_name<int* const>();
  CHECK(cptr.ends_with(" const"));
  CHECK_FALSE(cptr.starts_with("const"));
  CHECK_FALSE(cptr.contains("__ptr64"));
  const auto cvptr = friendly_type_name<const int* const>();
  CHECK(cvptr.ends_with(" const"));
  CHECK_FALSE(cvptr.starts_with("const"));
  // Member pointers get the trailing spelling too.
  const auto mptr = friendly_type_name<int Foo::* const>();
  CHECK(mptr.ends_with(" const"));
  CHECK_FALSE(mptr.starts_with("const"));

  // MSVC calling-convention annotations are stripped; the commas are
  // normalized on every platform.
  CHECK(
      friendly_type_name<void (*)(int, double)>() == "void (*)(int, double)");
}

#pragma endregion
#pragma region StringViewConvertible

TEST_CASE("StringViewConvertible", "[MetaTest]") {
  // StringViewConvertible concept (replaces is_string_view_convertible_v)
  CHECK(StringViewConvertible<std::string_view>);
  CHECK(StringViewConvertible<std::string>);
  CHECK(StringViewConvertible<char*>);
  CHECK(StringViewConvertible<char[]>);
  CHECK_FALSE(StringViewConvertible<int>);
  CHECK_FALSE(StringViewConvertible<std::nullptr_t>);
  CHECK(StringViewConvertible<std::string&>);
  CHECK((StringViewConvertible<std::string&&>));

  // Range concept (replaces can_ranged_for_v)
  CHECK_FALSE(Range<int>);
  CHECK(Range<std::vector<int>>);
  CHECK(Range<std::string>);
  CHECK(Range<int[4]>);
  CHECK(Range<char[4]>);
  CHECK_FALSE(Range<char*>);

  // Container concept (replaces is_container_v)
  CHECK_FALSE(Container<int>);
  CHECK(Container<std::vector<int>>);
  CHECK_FALSE(Container<std::string>); // Excluded (StringViewConvertible)
  CHECK((Container<std::array<int, 2>>));
  CHECK(Container<int[4]>);
  CHECK_FALSE(Container<char[4]>); // Excluded (StringViewConvertible)
  CHECK_FALSE(Container<char*>);
}

#pragma endregion
#pragma region Number

TEST_CASE("Number", "[MetaTest]") {
  // Integer concept (integral excluding bool)
  CHECK(Integer<char>);
  CHECK(Integer<int>);
  CHECK_FALSE(Integer<float>);
  CHECK_FALSE(Integer<double>);

  // Floating point check
  CHECK(std::floating_point<float>);
  CHECK(std::floating_point<double>);

  // std::byte is an enum, not arithmetic
  CHECK_FALSE(Integer<std::byte>);
  CHECK(std::is_enum_v<std::byte>);
  CHECK_FALSE(std::is_enum_v<const std::byte&>);
  CHECK(StdEnum<const std::byte&>); // StdEnum strips cvref

  // Arithmetic checks
  CHECK(std::is_arithmetic_v<int>);
  CHECK_FALSE(std::is_arithmetic_v<int&>);

  // Bool is arithmetic but not Integer
  CHECK(std::is_arithmetic_v<bool>);
  CHECK_FALSE(Integer<bool>);
  CHECK(is_bool_v<bool>);

  // The irregular initializers and the width are both deliberate here.
  // NOLINTBEGIN(performance-enum-size,readability-enum-initial-value)
  enum ColorEnum { red, green = 20, blue };
  enum class ColorClass { red, green = 20, blue };
  // NOLINTEND(performance-enum-size,readability-enum-initial-value)

  CHECK(StdEnum<ColorClass>);
  CHECK(StdEnum<ColorEnum>);

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
  CHECK(std::tuple_size_v<T2> == 2);
  CHECK(std::tuple_size_v<T0> == 0);
  CHECK(std::tuple_size_v<PI> == 2);
  CHECK(std::tuple_size_v<I2> == 2);

  // is_std_array_v trait
  CHECK(is_std_array_v<I2>);
  CHECK_FALSE(is_std_array_v<T2>);

  // TupleLike concept (replaces is_tuple_like_v)
  // Note: TupleLike = StdTuple || PairConvertible (excludes array)
  CHECK_FALSE(TupleLike<I2>); // array is NOT tuple-like
  CHECK_FALSE(TupleLike<std::string>);

  // is_tuple_v trait
  CHECK_FALSE(is_tuple_v<int>);
  CHECK(is_tuple_v<T0>);
  CHECK(is_tuple_v<T2>);
  CHECK_FALSE(is_tuple_v<PI>);
  CHECK(TupleLike<PI>); // pair is tuple-like
}

#pragma endregion
#pragma region Detection

TEST_CASE("Detection", "[MetaTest]") {
  // initializer_list detection
  {
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores): decltype only.
    auto il = {1, 2, 3};
    CHECK(is_initializer_list_v<decltype(il)>);
  }

  // variant detection
  {
    std::variant<int, float> va = 42;
    CHECK(is_variant_v<decltype(va)>);
  }

  // OptionalLike concept (replaces is_optional_like_v)
  {
    CHECK(OptionalLike<std::optional<int>>);
    CHECK(OptionalLike<int*>);
    CHECK_FALSE(OptionalLike<void*>); // void* not dereferenceable to value
    CHECK_FALSE(OptionalLike<const char*>); // string-like
  }

  // char pointer detection
  {
    CHECK(is_char_ptr_v<char*>);
    CHECK(is_char_ptr_v<const char*>);
    CHECK(is_char_ptr_v<char[]>);
    CHECK(is_char_ptr_v<const char[]>);
    CHECK(is_char_ptr_v<char* const>);
    CHECK(is_char_ptr_v<const char* const>);
    CHECK_FALSE(is_char_ptr_v<void*>);
    CHECK_FALSE(is_char_ptr_v<int*>);
    CHECK_FALSE(is_char_ptr_v<char>);
    const char* psz{};
    CHECK(is_char_ptr_v<decltype(psz)>);
  }

  // Note: is_void_ptr_v trait no longer exists
}

#pragma endregion
#pragma region Underlying

TEST_CASE("Underlying", "[MetaTest]") {
  // The underlying type is the point of these fixtures.
  // NOLINTBEGIN(performance-enum-size)
  enum class X : size_t { x1 = 1, x2 };
  enum class Y : int64_t { ylow = -1 };
  enum Z { z1 = 1 };
  // NOLINTEND(performance-enum-size)

  // as_underlying converts scoped enum to underlying type
  auto x = as_underlying(X::x1);
  CHECK(x == 1UL);
  CHECK((std::is_same_v<size_t, decltype(x)>));

  auto y = as_underlying(Y::ylow);
  CHECK(y == -1);
  CHECK((std::is_same_v<int64_t, decltype(y)>));

  // as_underlying works for unscoped enums too
  auto z0 = Z::z1;
  auto z = as_underlying(z1);
  CHECK(z0 == 1U);
  CHECK(z == 1U);
  // The underlying type of an unscoped enum is implementation-defined
  // (unsigned int on the Itanium ABI, int on the MSVC ABI), so check that
  // as_underlying yields the underlying type rather than a fixed spelling.
  CHECK((std::is_same_v<std::underlying_type_t<Z>, decltype(z)>));
}

#pragma endregion
#pragma region Streamable

TEST_CASE("Streamable", "[MetaTest]") {
  // OStreamable concept (replaces can_stream_out_v)
  CHECK(OStreamable<int>);
  CHECK_FALSE(OStreamable<Foo>);
}

#pragma endregion
#pragma region MaybeTypes

TEST_CASE("MaybeTypes", "[MetaTest]") {
  CHECK(std::is_empty_v<empty_t>);
  CHECK((std::is_same_v<maybe_t<int, true>, int>));
  CHECK((std::is_same_v<maybe_t<int, false>, empty_t>));

  CHECK((std::is_same_v<maybe_void_t<int>, int>));
  CHECK((std::is_same_v<maybe_void_t<void>, empty_t>));
  CHECK((std::is_same_v<maybe_void_t<>, empty_t>));

  struct NoExtraSpace {
    CORVID_NO_UNIQUE_ADDRESS maybe_t<int, false> maybe{42};
    int value{};
  };
  struct Baseline {
    int value{};
  };
  CHECK(sizeof(NoExtraSpace) == sizeof(Baseline));
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
  CHECK(t.forwarding_address_ptr(Trackable::raw::allow) == nullptr);
}

#pragma endregion
#pragma region AddressForwarder_Track

TEST_CASE("Track", "[AddressForwarder]") {
  Trackable* ptr{};
  {
    Trackable t{7};
    ptr = &t; // set initial value manually
    t.forwarding_address_ptr(Trackable::raw::allow) =
        &ptr; // register for future updates
    CHECK(ptr == &t);
  }
  // Destruction writes nullptr through the registered pointer.
  CHECK(ptr == nullptr);
}

#pragma endregion
#pragma region AddressForwarder_MoveConstruct

TEST_CASE("MoveConstruct", "[AddressForwarder]") {
  Trackable* ptr{};
  Trackable a{1};
  ptr = &a;
  a.forwarding_address_ptr(Trackable::raw::allow) = &ptr;
  CHECK(ptr == &a);

  Trackable b{std::move(a)};
  // Move construction updates `ptr` to the new location.
  CHECK(ptr == &b);
  // Source no longer holds the forwarding address slot.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK(a.forwarding_address_ptr(Trackable::raw::allow) == nullptr);
}

#pragma endregion
#pragma region AddressForwarder_MoveAssign

TEST_CASE("MoveAssign", "[AddressForwarder]") {
  Trackable* ptr{};
  Trackable a{2};
  ptr = &a;
  a.forwarding_address_ptr(Trackable::raw::allow) = &ptr;
  CHECK(ptr == &a);

  Trackable b{99};
  b = std::move(a);
  CHECK(ptr == &b);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK(a.forwarding_address_ptr(Trackable::raw::allow) == nullptr);

  // Clearing the forwarding address on `b` stops future tracking, but does
  // not touch the external `ptr` variable itself.
  b.forwarding_address_ptr(Trackable::raw::allow) = nullptr;
  CHECK(ptr == &b);
}

TEST_CASE("SelfAssign", "[AddressForwarder]") {
  Trackable* ptr{};
  Trackable a{3};
  ptr = &a;
  a.forwarding_address_ptr(Trackable::raw::allow) = &ptr;

  // Self-assignment must not corrupt state.
  PRAGMA_DIAG(push)
  PRAGMA_IGNORED("-Wself-move")
  a = std::move(a);
  PRAGMA_DIAG(pop)
  // After self-move `ptr` still points to `a`.
  CHECK(ptr == &a);
}

#pragma endregion
#pragma region AddressForwarder_DestroySource

TEST_CASE("DestroySource", "[AddressForwarder]") {
  Trackable* ptr{};
  Trackable b{0};
  {
    Trackable a{5};
    ptr = &a;
    a.forwarding_address_ptr(Trackable::raw::allow) = &ptr;
    b = std::move(a);
    // `a` no longer owns the slot; destroying it must not null `ptr`.
  }
  CHECK(ptr == &b);
  b.forwarding_address_ptr(Trackable::raw::allow) = nullptr;
}

#pragma endregion

// Derived type with a custom move constructor that explicitly moves the base
// subobject. The cast, rather than `std::move`, both says exactly that and
// keeps the later `o.value` read honest: the base move touches no derived
// members.
struct Trackable2: public address_forwarder<Trackable2> {
  int value{};
  explicit Trackable2(int v) : value{v} {}
  Trackable2(Trackable2&& o) noexcept
      : address_forwarder{static_cast<address_forwarder&&>(o)},
        value{o.value} {}
  Trackable2& operator=(Trackable2&&) = default;
  friend std::ostream& operator<<(std::ostream& os, const Trackable2& t) {
    return os << "Trackable2{" << t.value << "}";
  }
};

#pragma region AddressForwarder_CustomMoveCtor

TEST_CASE("CustomMoveCtor", "[AddressForwarder]") {
  Trackable2* ptr{};
  Trackable2 a{8};
  ptr = &a;
  a.forwarding_address_ptr(Trackable2::raw::allow) = &ptr;
  CHECK(ptr == &a);

  Trackable2 b{std::move(a)};
  CHECK(ptr == &b);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK(a.forwarding_address_ptr(Trackable2::raw::allow) == nullptr);
}

#pragma endregion
#pragma region AddressForwarder_BoundFunction

TEST_CASE("BoundFunction", "[AddressForwarder]") {
  // Primary use case: an object is moved into a `std::function` closure, and
  // a pointer registered before the move chain tracks it to its final home.
  Trackable* ptr{};
  Trackable t{99};
  t.forwarding_address_ptr(Trackable::raw::allow) = &ptr;
  // ptr is still null here; each move in the chain below will update it.

  std::function<int()> fn = [t = std::move(t)]() mutable { return t.value; };

  // ptr now points to `t` inside fn's internal storage.
  CHECK(ptr != nullptr);
  CHECK(ptr->value == 99);

  // fn() and ptr refer to the same object.
  CHECK(fn() == 99);
  ptr->value = 42;
  CHECK(fn() == 42);

  // Clear forwarding so fn's destructor does not write through the soon-to-be
  // dangling &ptr.
  ptr->forwarding_address_ptr(Trackable::raw::allow) = nullptr;
}

#pragma endregion
#pragma region AddressForwarder_ForwardedAddress

TEST_CASE("ForwardedAddress", "[AddressForwarder]") {
  // Registration, pointer-like access, and tracking across target moves.
  if (true) {
    Trackable a{1};
    forwarded_address fa{a};
    REQUIRE(static_cast<bool>(fa));
    CHECK(fa.get() == &a);
    CHECK(fa->value == 1);
    CHECK((*fa).value == 1);
    Trackable b{std::move(a)};
    CHECK(fa.get() == &b);
  }

  // A dying target nulls its handle instead of leaving it dangling.
  if (true) {
    auto t = std::make_unique<Trackable>(2);
    forwarded_address fa{*t};
    CHECK(fa.get() == t.get());
    t.reset();
    CHECK_FALSE(static_cast<bool>(fa));
    // Resetting a null handle is a harmless no-op.
    fa.reset();
    CHECK_FALSE(static_cast<bool>(fa));
  }

  // A dying handle unregisters, so the target may move or die freely.
  if (true) {
    Trackable t{3};
    if (true) {
      forwarded_address fa{t};
      CHECK(t.forwarding_address_ptr(Trackable::raw::allow) != nullptr);
    }
    CHECK(t.forwarding_address_ptr(Trackable::raw::allow) == nullptr);
    Trackable u{std::move(t)};
    CHECK(u.value == 3);
  }

  // Moving the handle re-registers it at its new address; RHS goes null.
  if (true) {
    Trackable t{4};
    forwarded_address fa{t};
    forwarded_address fb{std::move(fa)};
    // NOLINTNEXTLINE(bugprone-use-after-move): moved-from state is the point.
    CHECK_FALSE(static_cast<bool>(fa));
    CHECK(fb.get() == &t);
    Trackable u{std::move(t)};
    CHECK(fb.get() == &u);
  }

  // Move assignment unregisters from the old target and adopts the new one.
  if (true) {
    Trackable t{5};
    Trackable u{6};
    forwarded_address fa{t};
    forwarded_address fb{u};
    fb = std::move(fa);
    CHECK(fb.get() == &t);
    // NOLINTNEXTLINE(bugprone-use-after-move): moved-from state is the point.
    CHECK_FALSE(static_cast<bool>(fa));
    CHECK(u.forwarding_address_ptr(Trackable::raw::allow) == nullptr);
  }

  // Reset unregisters and goes null.
  if (true) {
    Trackable t{7};
    forwarded_address fa{t};
    fa.reset();
    CHECK_FALSE(static_cast<bool>(fa));
    CHECK(t.forwarding_address_ptr(Trackable::raw::allow) == nullptr);
  }

  // Last one wins: a newer handle displaces the older, which reads null.
  if (true) {
    Trackable t{8};
    forwarded_address first{t};
    forwarded_address second{t};
    CHECK_FALSE(static_cast<bool>(first));
    CHECK(second.get() == &t);
    Trackable u{std::move(t)};
    CHECK(second.get() == &u);
  }
}

TEST_CASE("ForwardedAddressBoundFunction", "[AddressForwarder]") {
  // The bound-function scenario with no manual disarm: the handle follows
  // the payload into the closure, and either side may safely die first.
  Trackable t{99};
  forwarded_address fa{t};
  if (true) {
    std::function<int()> fn = [t = std::move(t)]() mutable { return t.value; };
    REQUIRE(static_cast<bool>(fa));
    CHECK(fa->value == 99);
    fa->value = 42;
    CHECK(fn() == 42);
  }
  // The closure died; the handle went null on its own.
  CHECK_FALSE(static_cast<bool>(fa));
}

#pragma endregion

// fixed_function compile-time size checks
static_assert(sizeof(fixed_function<int(), 64>) == 64);
static_assert(sizeof(fixed_function<void(int, double), 32>) == 32);
static_assert(sizeof(fixed_function<int(int, int), 128>) == 128);

// `padded_size` rounds a size up to its alignment, turning would-be padding
// into usable storage.
static_assert(padded_size(1) == alignof(std::max_align_t));
static_assert(
    padded_size(alignof(std::max_align_t)) == alignof(std::max_align_t));
static_assert(
    padded_size(alignof(std::max_align_t) + 1) ==
    2 * alignof(std::max_align_t));
static_assert(padded_size(17, 8) == 24);
static_assert(padded_size(24, 8) == 24);

// `fixed_function` rounds `Size` up to the storage alignment, since a lesser
// instance would occupy the padded size anyway; `sizeof` reports the result,
// and a `padded_size` output is taken as is.
static_assert(
    sizeof(fixed_function<int(), padded_size(17)>) == padded_size(17));
static_assert(sizeof(fixed_function<int(), 17>) == padded_size(17));

#pragma region FixedFunction_Basic

// The `nullptr` constructor is explicit, unlike the std wrappers'.
static_assert(
    std::is_constructible_v<fixed_function<int(), 64>, std::nullptr_t>);
static_assert(
    !std::is_convertible_v<std::nullptr_t, fixed_function<int(), 64>>);

TEST_CASE("Basic", "[FixedFunction]") {
  fixed_function<int(), 64> f{[] { return 42; }};
  CHECK(f() == 42);

  // Constructing from `nullptr` yields an empty function, like assigning it.
  fixed_function<int(), 64> fnull{nullptr};
  CHECK_FALSE(static_cast<bool>(fnull));
}

#pragma endregion
#pragma region FixedFunction_Args

TEST_CASE("Args", "[FixedFunction]") {
  fixed_function<int(int, int), 64> add{[](int x, int y) { return x + y; }};
  CHECK(add(3, 4) == 7);
  CHECK(add(10, -3) == 7);
}

#pragma endregion
#pragma region FixedFunction_DiscardedReturn

TEST_CASE("DiscardedReturn", "[FixedFunction]") {
  // A value-returning callable stored under a void signature has its result
  // discarded, matching `std::function`.
  int calls{};
  fixed_function<void(int), 64> f{[&calls](int n) { return calls += n; }};
  f(2);
  f(3);
  CHECK(calls == 5);
}

#pragma endregion
#pragma region FixedFunction_MoveAcrossSizes

TEST_CASE("MoveAcrossSizes", "[FixedFunction]") {
  // A same-signature sibling of another size transplants the stored
  // callable: one relocation of the payload, not a nested wrapper.
  int moves{};
  struct mover {
    int* cnt;
    explicit mover(int* n) : cnt{n} {}
    mover(mover&& o) noexcept : cnt{o.cnt} { ++*cnt; }
    int operator()() const { return 42; }
  };
  fixed_function<int(), 64> small{mover{&moves}};
  CHECK(small.size() == sizeof(mover));
  CHECK(small.capacity() == fixed_function<int(), 64>::inline_size);
  CHECK(small.size() <= small.capacity());
  const int base = moves;
  fixed_function<int(), 96> big{std::move(small)};
  CHECK(moves == base + 1);
  CHECK(big() == 42);
  // NOLINTNEXTLINE(bugprone-use-after-move): moved-from state is the point.
  CHECK_FALSE(static_cast<bool>(small));
  CHECK(small.size() == 0);
  CHECK(big.size() == sizeof(mover));

  // Moving an empty sibling yields empty.
  fixed_function<int(), 64> empty_small;
  fixed_function<int(), 96> empty_big{std::move(empty_small)};
  CHECK_FALSE(static_cast<bool>(empty_big));

  // Downsizing checks the payload at runtime: this one fits.
  fixed_function<int(), 96> roomy{mover{&moves}};
  fixed_function<int(), 64> back{std::move(roomy)};
  CHECK(back() == 42);

  // A payload too large for the destination throws, leaving the source
  // whole.
  fixed_function<int(), 96> fat{[pad = std::array<std::byte, 64>{}] {
    return static_cast<int>(pad.size());
  }};
  CHECK(fat.size() > fixed_function<int(), 64>::inline_size);
  CHECK_THROWS_AS((fixed_function<int(), 64>{std::move(fat)}),
      std::length_error);
  // NOLINTNEXTLINE(bugprone-use-after-move): kept whole on failure.
  REQUIRE(static_cast<bool>(fat));
  CHECK(fat() == 64);

  // A refused downsizing assignment leaves both sides whole. Checking
  // `size` against `capacity` up front predicts the refusal.
  fixed_function<int(), 64> keeper{[] { return 3; }};
  // NOLINTNEXTLINE(bugprone-use-after-move): kept whole on failure.
  CHECK(fat.size() > keeper.capacity());
  // NOLINTNEXTLINE(bugprone-use-after-move): the failed move kept it whole.
  CHECK_THROWS_AS(keeper = std::move(fat), std::length_error);
  CHECK(keeper() == 3);
  // NOLINTNEXTLINE(bugprone-use-after-move): kept whole on failure.
  CHECK(fat() == 64);

  // Converting move assignment follows the same rules.
  fixed_function<int(), 96> target{[] { return 0; }};
  fixed_function<int(), 64> donor{[] { return 5; }};
  target = std::move(donor);
  CHECK(target() == 5);

  // The assignment transplants the payload directly, relocating it exactly
  // once rather than staging it through a temporary.
  fixed_function<int(), 64> counted{mover{&moves}};
  const int before = moves;
  target = std::move(counted);
  CHECK(moves == before + 1);
  CHECK(target() == 42);
}

#pragma endregion
#pragma region FixedFunction_WrapAcrossSignatures

TEST_CASE("WrapAcrossSignatures", "[FixedFunction]") {
  // A different-signature fixed_function stores as an ordinary callable,
  // but, matching `std::function`, wrapping an empty one produces an empty
  // function rather than a truthy shell that throws when called.
  fixed_function<int(int), 64> inner{[](int n) { return n * 2; }};
  fixed_function<int(short), 96> outer{std::move(inner)};
  CHECK(static_cast<bool>(outer));
  CHECK(outer(short{4}) == 8);

  fixed_function<int(int), 64> empty_inner;
  fixed_function<int(short), 96> empty_outer{std::move(empty_inner)};
  CHECK_FALSE(static_cast<bool>(empty_outer));
  CHECK_THROWS_AS(empty_outer(short{1}), std::bad_function_call);
}

#pragma endregion
#pragma region FixedFunction_WrapStdFunction

TEST_CASE("WrapStdFunction", "[FixedFunction]") {
  using ff = fixed_function<int(), 128>;

  // The wrapper-detection trait lives in traits.h and sees through neither
  // cvref nor inheritance: it matches the std wrappers themselves.
  static_assert(is_std_function_wrapper_v<std::function<int()>>);
  static_assert(!is_std_function_wrapper_v<int (*)()>);
  static_assert(!is_std_function_wrapper_v<ff>);
#ifdef __cpp_lib_move_only_function
  static_assert(is_std_function_wrapper_v<std::move_only_function<int()>>);
#endif

  // Wrapping a `std::function` is the explicit escape hatch: the shell
  // stores inline while its payload may live on the heap, at the cost of
  // double indirection. No implicit conversion, and no moving from lvalues.
  static_assert(std::is_constructible_v<ff, std::function<int()>&&>);
  static_assert(!std::is_convertible_v<std::function<int()>&&, ff>);
  static_assert(!std::is_constructible_v<ff, std::function<int()>&>);

  // The shell stores inline and calls through.
  ff f{std::function<int()>{[] { return 12; }}};
  CHECK(f() == 12);
  CHECK(f.size() == sizeof(std::function<int()>));

  // A functor too large to store directly fits once `std::function` holds
  // it on the heap.
  auto big = [pad = std::array<std::byte, 256>{}] {
    return static_cast<int>(pad.size());
  };
  static_assert(sizeof(big) > ff::inline_size);
  ff g{std::function<int()>{std::move(big)}};
  CHECK(g() == 256);

  // A compatible cross-signature `std::function` goes through the same
  // explicit door.
  fixed_function<int(short), 128> h{
      std::function<int(int)>{[](int n) { return n * 3; }}};
  CHECK(h(short{7}) == 21);

#ifdef __cpp_lib_move_only_function
  // `std::move_only_function` gets the same treatment, and unlike
  // `std::function` it can carry a move-only payload.
  static_assert(std::is_constructible_v<ff, std::move_only_function<int()>&&>);
  static_assert(!std::is_convertible_v<std::move_only_function<int()>&&, ff>);
  ff m{std::move_only_function<int()>{[p = std::make_unique<int>(7)] {
    return *p;
  }}};
  CHECK(m() == 7);
  CHECK(m.size() == sizeof(std::move_only_function<int()>));

  // Wrapping an empty one produces an empty function here too, which is an
  // upgrade: calling it throws where the empty wrapper's own call would be
  // undefined behavior.
  ff me{std::move_only_function<int()>{}};
  CHECK_FALSE(static_cast<bool>(me));

  // Ref-qualified signatures follow the lvalue-invocation rule: `&` works,
  // `&&` is rejected.
  static_assert(
      std::is_constructible_v<ff, std::move_only_function<int() &>&&>);
  static_assert(
      !std::is_constructible_v<ff, std::move_only_function<int() &&>&&>);
#endif

  // Matching `std::function`, wrapping an empty one produces an empty
  // function rather than a truthy shell that throws when called.
  ff e{std::function<int()>{}};
  CHECK_FALSE(static_cast<bool>(e));
  CHECK(e.size() == 0);
}

#pragma endregion
#pragma region FixedFunction_Bool

TEST_CASE("Bool", "[FixedFunction]") {
  fixed_function<int(), 64> a{[] { return 1; }};
  CHECK(static_cast<bool>(a));
  fixed_function<int(), 64> b{std::move(a)};
  // NOLINTNEXTLINE(bugprone-use-after-move): moved-from state is the point.
  CHECK_FALSE(static_cast<bool>(a));
  CHECK(static_cast<bool>(b));
}

#pragma endregion
#pragma region FixedFunction_Move

TEST_CASE("Move", "[FixedFunction]") {
  fixed_function<int(), 64> a{[] { return 7; }};
  CHECK(static_cast<bool>(a));
  fixed_function<int(), 64> b{std::move(a)};
  // NOLINTNEXTLINE(bugprone-use-after-move): moved-from state is the point.
  CHECK_FALSE(static_cast<bool>(a));
  CHECK(static_cast<bool>(b));
  CHECK(b() == 7);
}

#pragma endregion
#pragma region FixedFunction_MoveAssign

TEST_CASE("MoveAssign", "[FixedFunction]") {
  fixed_function<int(), 64> a{[] { return 99; }};
  fixed_function<int(), 64> b{[] { return 0; }};
  b = std::move(a);
  // NOLINTNEXTLINE(bugprone-use-after-move): moved-from state is the point.
  CHECK_FALSE(static_cast<bool>(a));
  CHECK(static_cast<bool>(b));
  CHECK(b() == 99);

  b = nullptr;
  CHECK_FALSE(static_cast<bool>(b));
}

#pragma endregion
#pragma region FixedFunction_Destructor

TEST_CASE("Destructor", "[FixedFunction]") {
  // `Counted` does not null `count_` on move, so every `~Counted` call
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
    fixed_function<void(), 64> f{Counted{&n}};
    CHECK(n == 1); // temporary destroyed after move into storage
    {
      fixed_function<void(), 64> g{std::move(f)};
      CHECK(n == 2); // move ctor immediately destructs f's storage
    } // g destroyed: Counted in g.storage_ destructed
    CHECK(n == 3);
  } // f destroyed: manage_ is null, nothing happens
  CHECK(n == 3);
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

// This case transcribes the cppreference `std::function` example set to
// show `fixed_function` accepts the same callables, so the `std::bind`
// expressions are the subject and the fixture keeps the reference shape.
// NOLINTBEGIN(modernize-avoid-bind)
// NOLINTBEGIN(modernize-use-nodiscard)
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
  fixed_function<int(int), 64> f_display{&cpref_num};
  CHECK(f_display(-9) == -9);

  // store a lambda
  fixed_function<int(), 64> f_display_42{[] { return cpref_num(42); }};
  CHECK(f_display_42() == 42);

  // store the result of a call to std::bind
  fixed_function<int(), 64> f_display_31337{std::bind(cpref_num, 31337)};
  CHECK(f_display_31337() == 31337);

  // store a call to a member function
  fixed_function<int(const Foo&, int), 64> f_add_display{&Foo::add};
  const Foo foo{314159};
  CHECK(f_add_display(foo, 1) == 314160);
  CHECK(f_add_display(314159, 1) == 314160); // implicit Foo from int

  // store a call to a data member accessor
  fixed_function<int(const Foo&), 64> f_num{&Foo::num_};
  CHECK(f_num(foo) == 314159);

  // store a call to a member function and object
  using std::placeholders::_1;
  fixed_function<int(int), 64> f_add_display2{std::bind(&Foo::add, foo, _1)};
  CHECK(f_add_display2(2) == 314161);

  // store a call to a member function and object ptr
  fixed_function<int(int), 64> f_add_display3{std::bind(&Foo::add, &foo, _1)};
  CHECK(f_add_display3(3) == 314162);

  // store a call to a function object
  fixed_function<int(int), 64> f_display_obj{PrintNum{}};
  CHECK(f_display_obj(18) == 18);

  // recursive lambda: same self-referential pattern as the cppreference
  // factorial example, using fixed_function instead of std::function
  auto factorial = [](int n) {
    fixed_function<int(int), 64> fac;
    fac = fixed_function<int(int), 64>{[&fac](int k) -> int {
      return (k < 2) ? 1 : k * fac(k - 1);
    }};
    return fac(n);
  };
  CHECK(factorial(5) == 120);
  CHECK(factorial(6) == 720);
  CHECK(factorial(7) == 5040);
}
// NOLINTEND(modernize-use-nodiscard)
// NOLINTEND(modernize-avoid-bind)

#pragma endregion
#pragma region FixedFunction_RefReturn

TEST_CASE("RefReturn", "[FixedFunction]") {
  // Callables that return an actual reference are safe.
  fixed_function<int&(), 64> f{[&]() -> int& { return g_ref_val; }};
  CHECK(f() == 42);
  f() = 99;
  CHECK(g_ref_val == 99);
  g_ref_val = 42; // restore

  fixed_function<const int&(), 64> g{[&]() -> const int& {
    return g_ref_val;
  }};
  CHECK(g() == 42);

  fixed_function<int&&(), 64> h{[]() -> int&& {
    return static_cast<int&&>(g_ref_val);
  }};
  CHECK(h() == 42);

#ifdef NOT_SUPPOSED_TO_COMPILE
  // Both of these trigger the static_assert: callable returns a prvalue `int`
  // but the declared return type is a reference, so every call would dangle.
  fixed_function<int&(), 64> bad1{[] { return 42; }};
  fixed_function<const int&(), 64> bad2{[] { return 42; }};
#endif
}

#pragma endregion
#pragma region FixedFunction_EmptyThrows

TEST_CASE("EmptyThrows", "[FixedFunction]") {
  // Default-constructed instance is empty and throws on call.
  fixed_function<int(), 64> empty{};
  CHECK(!empty);
  CHECK_THROWS_AS(empty(), std::bad_function_call);

  // Moved-from instance is also empty and throws on call.
  fixed_function<int(), 64> f{[] { return 1; }};
  fixed_function<int(), 64> g{std::move(f)};
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK(!f);
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move): same moved-from check.
  CHECK_THROWS_AS(f(), std::bad_function_call);

  // nullptr-assigned instance throws too.
  g = nullptr;
  CHECK_THROWS_AS(g(), std::bad_function_call);
}

#pragma endregion
#pragma region FixedFunction_FreeFn

TEST_CASE("FreeFn", "[FixedFunction]") {
  // A plain function pointer satisfies MoveConsumable (it is a prvalue).
  fixed_function<int(int), 64> f{&double_it};
  CHECK(static_cast<bool>(f));
  CHECK(f(21) == 42);
}

#pragma endregion
#pragma region FixedFunction_Functor

TEST_CASE("Functor", "[FixedFunction]") {
  struct Adder {
    int n;
    int operator()(int x) const { return x + n; }
  };
  fixed_function<int(int), 64> f{Adder{10}};
  CHECK(f(32) == 42);
}

#pragma endregion
#pragma region FixedFunction_Swap

TEST_CASE("Swap", "[FixedFunction]") {
  using ff = fixed_function<int(), 64>;
  ff a{[] { return 1; }};
  ff b{[] { return 2; }};

  // Member swap.
  a.swap(b);
  CHECK(a() == 2);
  CHECK(b() == 1);

  // ADL swap (finds the hidden-friend in namespace corvid::meta).
  using std::swap;
  swap(a, b);
  CHECK(a() == 1);
  CHECK(b() == 2);

  // Swap a full instance with an empty one.
  ff empty{};
  a.swap(empty);
  CHECK_FALSE(static_cast<bool>(a));
  CHECK(static_cast<bool>(empty));
  CHECK(empty() == 1);

  // Swap two empty instances is a no-op.
  ff empty2{};
  a.swap(empty2);
  CHECK_FALSE(static_cast<bool>(a));
  CHECK_FALSE(static_cast<bool>(empty2));
}

#pragma endregion
#pragma region TupleMetafunctions

TEST_CASE("TupleMetafunctions", "[MetaTest]") {
  using tup_t = std::tuple<int, float, char>;

  // Membership.
  static_assert(tuple_contains_v<int, tup_t>);
  static_assert(tuple_contains_v<char, tup_t>);
  static_assert(!tuple_contains_v<double, tup_t>);
  static_assert(!tuple_contains_v<int, std::tuple<>>);

  // Index of first occurrence.
  static_assert(tuple_index_v<int, tup_t> == 0UZ);
  static_assert(tuple_index_v<char, tup_t> == 2UZ);

  // Union preserves first-seen order and drops duplicates; empty tuples
  // contribute nothing.
  static_assert(std::is_same_v<
      tuple_union_t<std::tuple<int, float>, std::tuple<int, char>>,
      std::tuple<int, float, char>>);
  static_assert(std::is_same_v<tuple_union_t<>, std::tuple<>>);
  static_assert(std::is_same_v<tuple_union_t<std::tuple<>, std::tuple<int>>,
      std::tuple<int>>);

  // Optional-wrapping preserves order.
  static_assert(std::is_same_v<wrap_optionals_t<tup_t>,
      std::tuple<std::optional<int>, std::optional<float>,
          std::optional<char>>>);

  // Runtime anchor so the case is not assert-only.
  CHECK(tuple_index_v<float, tup_t> == 1UZ);
}

#pragma endregion

#pragma region SignatureTraits

// Whether `signature_traits<T>` is specialized, which is what admits `T` as
// a signature.
template<typename T>
concept HasSignatureTraits = requires {
  typename signature_traits<T>::result_t;
};

// Whether `Sig` decomposes to `int(char)` plus exactly the given qualifiers.
template<typename Sig, const_qual Const, ref_qual Ref, noexcept_spec Noex>
constexpr bool decomposes_v =
    std::is_same_v<signature_function_t<Sig>, int(char)> &&
    (signature_traits<Sig>::const_qualifier == Const) &&
    (signature_traits<Sig>::ref_qualifier == Ref) &&
    (signature_traits<Sig>::noexcept_specifier == Noex);

TEST_CASE("SignatureTraits", "[MetaTest]") {
  // The twelve qualified variants, decomposed.
  static_assert(decomposes_v<int(char), const_qual::none, ref_qual::none,
      noexcept_spec::none>);
  static_assert(decomposes_v<int(char) noexcept, const_qual::none,
      ref_qual::none, noexcept_spec::present>);
  static_assert(decomposes_v<int(char) const, const_qual::present,
      ref_qual::none, noexcept_spec::none>);
  static_assert(decomposes_v<int(char) const noexcept, const_qual::present,
      ref_qual::none, noexcept_spec::present>);
  static_assert(decomposes_v<int(char) &, const_qual::none, ref_qual::lvalue,
      noexcept_spec::none>);
  static_assert(decomposes_v<int(char) & noexcept, const_qual::none,
      ref_qual::lvalue, noexcept_spec::present>);
  static_assert(decomposes_v<int(char) const&, const_qual::present,
      ref_qual::lvalue, noexcept_spec::none>);
  static_assert(decomposes_v<int(char) const & noexcept, const_qual::present,
      ref_qual::lvalue, noexcept_spec::present>);
  static_assert(decomposes_v<int(char) &&, const_qual::none, ref_qual::rvalue,
      noexcept_spec::none>);
  // clang-format misreads `&& noexcept` in a template argument list as an
  // operator, so the two rvalue noexcept variants go through aliases.
  using rref_noex_sig = int(char) && noexcept;
  using const_rref_noex_sig = int(char) const&& noexcept;
  static_assert(decomposes_v<rref_noex_sig, const_qual::none, ref_qual::rvalue,
      noexcept_spec::present>);
  static_assert(decomposes_v<int(char) const&&, const_qual::present,
      ref_qual::rvalue, noexcept_spec::none>);
  static_assert(decomposes_v<const_rref_noex_sig, const_qual::present,
      ref_qual::rvalue, noexcept_spec::present>);

  // Result and parameters come through unchanged, references included.
  using st = signature_traits<void(int&, double&&) const&&>;
  static_assert(std::is_void_v<st::result_t>);
  static_assert(std::is_same_v<st::args_t, std::tuple<int&, double&&>>);
  static_assert(std::is_same_v<st::function_t, void(int&, double&&)>);

  // The two-valued axes, restated as bools.
  static_assert(st::is_const && !st::is_noexcept);
  static_assert(!signature_traits<int() noexcept>::is_const);
  static_assert(signature_traits<int() noexcept>::is_noexcept);

  // The const axis once more as an access mode, which is what
  // `conditional_const_t` keys on.
  static_assert(st::access == access_mode::as_const);
  static_assert(
      signature_traits<int() noexcept>::access == access_mode::as_mutable);
  static_assert(
      std::is_same_v<conditional_const_t<st::access, int>, const int>);
  static_assert(std::is_same_v<
      conditional_const_t<signature_traits<int() noexcept>::access, int>,
      int>);
  static_assert(std::is_same_v<
      conditional_const_t<access_mode::as_const, void>*, const void*>);

  // Only the twelve variants qualify: not a non-function, a pointer to one,
  // a `volatile`-qualified one, or a C-variadic one.
  static_assert(HasSignatureTraits<int(char)>);
  static_assert(!HasSignatureTraits<int>);
  static_assert(!HasSignatureTraits<int (*)(char)>);
  static_assert(!HasSignatureTraits<int(char) volatile>);
  static_assert(!HasSignatureTraits<int(char, ...)>);

  // Runtime anchor so the case is not assert-only.
  CHECK(
      signature_traits<int(char)>::noexcept_specifier == noexcept_spec::none);
}

#pragma endregion

#pragma region EmptyCallTraits

// Result types for the empty-call table: one that cannot be
// value-initialized, and one whose value-initialization may throw (its
// defaulted constructor allocates through the member initializer).
struct nondefault_result {
  explicit nondefault_result(int) {}
};
struct throwing_result {
  std::vector<int> pad = std::vector<int>(1);
};
static_assert(!std::is_nothrow_default_constructible_v<throwing_result>);

TEST_CASE("EmptyCallTraits", "[MetaTest]") {
  using plain = invocables::empty_call_traits<int>;
  using none = invocables::empty_call_traits<void>;
  using ref = invocables::empty_call_traits<int&>;
  using nondefault = invocables::empty_call_traits<nondefault_result>;
  using throwing = invocables::empty_call_traits<throwing_result>;

  // Silencing needs a value-initializable result, nothrow for a subset.
  static_assert(plain::is_silenceable && plain::is_nothrow_silenceable);
  static_assert(none::is_silenceable && none::is_nothrow_silenceable);
  static_assert(!ref::is_silenceable && !ref::is_nothrow_silenceable);
  static_assert(!nondefault::is_silenceable);
  static_assert(throwing::is_silenceable && !throwing::is_nothrow_silenceable);

  // `admits`: silent follows silenceability (nothrow under noexcept), raise
  // needs a throwing call, terminate is always admitted.
  static_assert(plain::admits(on_empty::silent, noexcept_spec::none));
  static_assert(plain::admits(on_empty::silent, noexcept_spec::present));
  static_assert(!ref::admits(on_empty::silent, noexcept_spec::none));
  static_assert(throwing::admits(on_empty::silent, noexcept_spec::none));
  static_assert(!throwing::admits(on_empty::silent, noexcept_spec::present));
  static_assert(plain::admits(on_empty::raise, noexcept_spec::none));
  static_assert(!plain::admits(on_empty::raise, noexcept_spec::present));
  static_assert(ref::admits(on_empty::terminate, noexcept_spec::present));

  // `resolve_floor`: the mildest admitted behavior at or above the floor.
  static_assert(
      plain::resolve_floor(on_empty::silent, noexcept_spec::none) ==
      on_empty::silent);
  static_assert(
      plain::resolve_floor(on_empty::raise, noexcept_spec::none) ==
      on_empty::raise);
  static_assert(
      plain::resolve_floor(on_empty::terminate, noexcept_spec::none) ==
      on_empty::terminate);
  static_assert(
      ref::resolve_floor(on_empty::silent, noexcept_spec::none) ==
      on_empty::raise);
  static_assert(
      ref::resolve_floor(on_empty::silent, noexcept_spec::present) ==
      on_empty::terminate);
  static_assert(
      throwing::resolve_floor(on_empty::silent, noexcept_spec::present) ==
      on_empty::terminate);
  static_assert(
      plain::resolve_floor(on_empty::raise, noexcept_spec::present) ==
      on_empty::terminate);

  // `invoke`: noexcept exactly when the behavior cannot throw.
  static_assert(noexcept(plain::invoke<on_empty::silent>()));
  static_assert(!noexcept(throwing::invoke<on_empty::silent>()));
  static_assert(!noexcept(plain::invoke<on_empty::raise>()));
  static_assert(noexcept(ref::invoke<on_empty::terminate>()));

  // The silent and raise calls, performed.
  CHECK(plain::invoke<on_empty::silent>() == 0);
  CHECK_NOTHROW(none::invoke<on_empty::silent>());
  CHECK_THROWS_AS(plain::invoke<on_empty::raise>(), std::bad_function_call);
}

#pragma endregion

// NOLINTEND(readability-function-size)
// NOLINTEND(readability-function-cognitive-complexity)
