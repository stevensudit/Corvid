// Corvid20: A general-purpose C++ 20 library extending std.
// https://github.com/stevensudit/Corvid
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

#include "../includes/meta.h"
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
#ifdef WILL_NOT_COMPILE
  std::string s{"Hello"};
  foo(s, 42);
  stream_out(oss, oss);
#endif
}

MAKE_TEST_LIST(MetaTest_OStreamdDerived);

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
  EXPECT_TRUE((has_find_v<M, int>));
  EXPECT_FALSE((has_find_v<M, Foo>));
  EXPECT_TRUE(
      (std::is_same_v<finding::details::find_ret_t<M, int>, M::iterator>));

  using S = std::set<Foo>;
  EXPECT_TRUE((has_find_v<S, Foo>));
  EXPECT_FALSE((has_find_v<S, int>));
  EXPECT_TRUE(
      (std::is_same_v<finding::details::find_ret_t<S, Foo>, S::iterator>));

  using V = std::vector<int>;
  EXPECT_FALSE((has_find_v<V, int>));
  EXPECT_FALSE((has_find_v<V, Foo>));
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

  EXPECT_TRUE(is_array_v<I2>);
  EXPECT_FALSE(is_array_v<T2>);
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