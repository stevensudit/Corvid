// Corvid: A general-purpose C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022 Steven Sudit
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
#include "pch.h"
#include <LibCorvid/includes/StringUtils.h>

#include <iostream>
#include <cstdint>

using namespace std::literals;
using namespace corvid::strings;

TEST(StringUtilsTest, extract_piece) {
  std::string_view sv;
  EXPECT_EQ(extract_piece(sv, ","), "");
  EXPECT_EQ(extract_piece(sv, ","), "");
  sv = "1,2";
  EXPECT_EQ(extract_piece(sv, ","), "1");
  EXPECT_EQ(extract_piece(sv, ","), "2");
  EXPECT_EQ(extract_piece(sv, ","), "");
  sv = ",";
  EXPECT_EQ(sv.size(), 1);
  EXPECT_EQ(extract_piece(sv, ","), "");
  EXPECT_EQ(extract_piece(sv, ","), "");

  sv = "1,2,3,4";
  EXPECT_EQ(extract_piece<std::string>(sv, ","), "1");
}

TEST(StringUtilsTest, more_pieces) {
  std::string_view w, part;
  w = "1,2";
  EXPECT_FALSE(w.empty());
  EXPECT_TRUE(more_pieces(part, w, ","));
  EXPECT_EQ(part, "1");
  EXPECT_FALSE(w.empty());
  EXPECT_FALSE(more_pieces(part, w, ","));
  EXPECT_EQ(part, "2");
  EXPECT_TRUE(w.empty());
  EXPECT_FALSE(more_pieces(part, w, ","));

  w = "1,";
  EXPECT_FALSE(w.empty());
  EXPECT_TRUE(more_pieces(part, w, ","));
  EXPECT_EQ(part, "1");
  EXPECT_TRUE(w.empty());
  EXPECT_FALSE(more_pieces(part, w, ","));
  EXPECT_EQ(part, "");
}

TEST(StringUtilsTest, Split) {
  if (true) {
    using V = std::vector<std::string_view>;

    EXPECT_EQ(split(""sv, ","), (V{}));
    EXPECT_EQ(split("1"sv, ","), (V{"1"}));
    EXPECT_EQ(split("1,"sv, ","), (V{"1", ""}));
    EXPECT_EQ(split(",1"sv, ","), (V{"", "1"}));
    EXPECT_EQ(split(",,"sv, ","), (V{"", "", ""}));
    EXPECT_EQ(split("1,2"sv, ","), (V{"1", "2"}));
    EXPECT_EQ(split("1,2,3"sv, ","), (V{"1", "2", "3"}));
    EXPECT_EQ(split("11"sv, ","), (V{"11"}));
    EXPECT_EQ(split("11,"sv, ","), (V{"11", ""}));
    EXPECT_EQ(split(",11"sv, ","), (V{"", "11"}));
    EXPECT_EQ(split("11,22"sv, ","), (V{"11", "22"}));
    EXPECT_EQ(split("11,22,33"sv, ","), (V{"11", "22", "33"}));
  }
  if (true) {
    using V = std::vector<std::string>;

    EXPECT_EQ(split("", ","), (V{}));
    EXPECT_EQ(split("1", ","), (V{"1"}));
    EXPECT_EQ(split("1,", ","), (V{"1", ""}));
    EXPECT_EQ(split(",1", ","), (V{"", "1"}));
    EXPECT_EQ(split(",,", ","), (V{"", "", ""}));
    EXPECT_EQ(split("1,2", ","), (V{"1", "2"}));
    EXPECT_EQ(split("1,2,3", ","), (V{"1", "2", "3"}));
    EXPECT_EQ(split("11", ","), (V{"11"}));
    EXPECT_EQ(split("11,", ","), (V{"11", ""}));
    EXPECT_EQ(split(",11", ","), (V{"", "11"}));
    EXPECT_EQ(split("11,22", ","), (V{"11", "22"}));
    EXPECT_EQ(split("11,22,33", ","), (V{"11", "22", "33"}));
  }
  if (true) {
    using V = std::vector<std::string_view>;
    using S = std::vector<std::string>;
    auto w = "1,2,3,4"sv;
    std::string s{w};
    EXPECT_EQ(split(w, ","), (V{"1", "2", "3", "4"}));
    EXPECT_EQ(split(s, ","), (V{"1", "2", "3", "4"}));
    EXPECT_EQ(split(std::move(s), ","), (S{"1", "2", "3", "4"}));
  }
  if (true) {
    using R = std::string;
    using V = std::vector<R>;

    EXPECT_EQ(split<R>("11,22,33", ","), (V{"11", "22", "33"}));
    EXPECT_EQ(split<R>(R{"11,22,33"}, ","), (V{"11", "22", "33"}));
  }
}

TEST(StringUtilsTest, Trim) {
  if (true) {
    EXPECT_EQ(trim_left(""), "");
    EXPECT_EQ(trim_left("1"), "1");
    EXPECT_EQ(trim_left("12"), "12");
    EXPECT_EQ(trim_left("123"), "123");
    EXPECT_EQ(trim_left(" "), "");
    EXPECT_EQ(trim_left(" 1"), "1");
    EXPECT_EQ(trim_left(" 12"), "12");
    EXPECT_EQ(trim_left(" 123"), "123");
    EXPECT_EQ(trim_left("  "), "");
    EXPECT_EQ(trim_left("  1"), "1");
    EXPECT_EQ(trim_left("  12"), "12");
    EXPECT_EQ(trim_left("  123"), "123");
    EXPECT_EQ(trim_left("  1  "), "1  ");

    EXPECT_EQ(trim_right(""), "");
    EXPECT_EQ(trim_right("1"), "1");
    EXPECT_EQ(trim_right("12"), "12");
    EXPECT_EQ(trim_right("123"), "123");
    EXPECT_EQ(trim_right(" "), "");
    EXPECT_EQ(trim_right("1 "), "1");
    EXPECT_EQ(trim_right("12 "), "12");
    EXPECT_EQ(trim_right("123 "), "123");
    EXPECT_EQ(trim_right("  "), "");
    EXPECT_EQ(trim_right("1  "), "1");
    EXPECT_EQ(trim_right("12  "), "12");
    EXPECT_EQ(trim_right("123  "), "123");
    EXPECT_EQ(trim_right("  1  "), "  1");

    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim("1"), "1");
    EXPECT_EQ(trim("12"), "12");
    EXPECT_EQ(trim("123"), "123");
    EXPECT_EQ(trim("  "), "");
    EXPECT_EQ(trim(" 1 "), "1");
    EXPECT_EQ(trim(" 12 "), "12");
    EXPECT_EQ(trim(" 123 "), "123");
    EXPECT_EQ(trim("    "), "");
    EXPECT_EQ(trim("  1  "), "1");
    EXPECT_EQ(trim("  12  "), "12");
    EXPECT_EQ(trim("  123  "), "123");
    EXPECT_EQ(trim("  1  "), "1");

    EXPECT_EQ(trim_braces("[]"), "");
    EXPECT_EQ(trim_braces("[1]"), "1");
    EXPECT_EQ(trim_braces("[12]"), "12");
    EXPECT_EQ(trim_braces("12]"), "12]");
    EXPECT_EQ(trim_braces("[12]"), "12");
    EXPECT_EQ(trim_braces("'12'", "'"), "12");
  }
  if (true) {
    auto w = " 1, 2, 3  , 4 "s;
    auto v = split(w, ",");
    EXPECT_EQ(v[0], " 1");
    //* auto x = trim(v);
    trim(v);
    EXPECT_EQ(v[0], "1");
    EXPECT_EQ((trim(split(w, ","))[0]), "1");
    auto x = trim(split<std::string>(w, ","));
    EXPECT_EQ(x[0], "1");
  }
}

TEST(StringUtilsTest, Append) {
  std::string s;

  s.clear();
  append(s, "1"); // is_string_view_convertible_v
  append(s, "2"s);
  append(s, "3"sv);
  EXPECT_EQ(s, "123");

  s.clear();
  append(s, '1');       // char (number)
  append(s, "2", "3");  // many
  append(s, (size_t)4); // append size_t
  EXPECT_EQ(s, "1234");

  s.clear();
  append<2>(s, 0xaa); // append binary
  EXPECT_EQ(s, "10101010");
  s.clear();
  append<16>(s, 0xaa); // append hex
  EXPECT_EQ(s, "aa");

  s.clear();
  append(s, true);           // append bool
  append(s, (float)2);       // append float
  append(s, (double)3);      // append float
  append(s, (long double)4); // append float
  EXPECT_EQ(s, "1234");

  s.clear();
  append(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.456789012345");
  s.clear();
  append<std::chars_format::scientific>(s, (double)123.456789012345);
  EXPECT_EQ(s, "1.23456789012345e+02");
  s.clear();
  append<std::chars_format::fixed, 1>(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.5");
  s.clear();
  append<std::chars_format::fixed, 6>(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.456789");

  const int i = 42;
  s.clear();
  append(s, i);
  EXPECT_EQ(s, "42");
  s.clear();
  append(s, &i);
  EXPECT_NE(s, "42");
  s.clear();
  append(s, &i);
  EXPECT_NE(s, "42");

  // None of these char-like types are char so they're all treated as ints.
  EXPECT_FALSE((std::is_same_v<char, signed char>));
  EXPECT_FALSE((std::is_same_v<char, unsigned char>));
  EXPECT_FALSE((std::is_same_v<signed char, unsigned char>));

  s.clear();
  append(s, (signed char)1);
  append(s, (unsigned char)2);
  append(s, (wchar_t)3);
  append(s, (char16_t)4);
  append(s, (char32_t)5);
  EXPECT_EQ(s, "12345");
  // C++20's char8_t is also distinct.

  // Might as well test all the integral types, if only for regression.
  s.clear();
  append(s, (short int)1);
  append(s, (unsigned short int)2);
  ;
  append(s, (int)3);
  append(s, (unsigned int)4);
  append(s, (long int)5);
  append(s, (unsigned long int)6);
  append(s, (long long int)7);
  append(s, (unsigned long long int)8);
  EXPECT_EQ(s, "12345678");

  s.clear();
  append(s, (int8_t)1);
  append(s, (int16_t)2);
  append(s, (int32_t)3);
  append(s, (int64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  append(s, (uint8_t)1);
  append(s, (uint16_t)2);
  append(s, (uint32_t)3);
  append(s, (uint64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  append(s, (int_fast8_t)1);
  append(s, (int_fast16_t)2);
  append(s, (int_fast32_t)3);
  append(s, (int_fast64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  append(s, (uint_fast8_t)1);
  append(s, (uint_fast16_t)2);
  append(s, (uint_fast32_t)3);
  append(s, (uint_fast64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  append(s, (int_least8_t)1);
  append(s, (int_least16_t)2);
  append(s, (int_least32_t)3);
  append(s, (int_least64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  append(s, (uint_least8_t)1);
  append(s, (uint_least16_t)2);
  append(s, (uint_least32_t)3);
  append(s, (uint_least64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  append(s, (intmax_t)1);
  append(s, (intptr_t)2);
  append(s, (uintmax_t)3);
  append(s, (uintptr_t)4);
  EXPECT_EQ(s, "1234");

  enum ColorEnum { red, green = 20, blue };
  enum class ColorClass { red, green = 20, blue };

  s.clear();
  append(s, green);
  append(s, ColorClass::blue);
  EXPECT_EQ(s, "2021");
  s.clear();
  append(s, braces_opt::flat);
  EXPECT_EQ(s, "4");
  s.clear();
  append(s, braces_opt(32));
  EXPECT_EQ(s, "32"); // not hex!

  std::map<std::string, int> msi{{"a", 1}, {"c", 2}};
  std::set<int> si{3, 4, 5};
  s.clear();
  append(s, msi);
  append(s, si);
  EXPECT_EQ(s, "12345");

  std::map<std::string, std::set<int>> mssi{{"c", {5, 4}}, {"a", {3, 2, 1}}};
  s.clear();
  append(s, mssi);
  EXPECT_EQ(s, "12345");

  // TODO: Nested container torture test.

  EXPECT_EQ(concat("1", "2"sv, "3"s), "123");
  EXPECT_EQ(concat(1, 2.0, 3ull), "123");
  EXPECT_EQ(concat(true, std::byte{2}, 3), "123");
  EXPECT_EQ((concat("1", "2")), "12");

  auto t = std::make_tuple("1"s, 2, 3.0);
  auto l = {"1"s, "2"s, "3"s};
  auto p = std::make_pair("1"s, "2, 3");
  auto a = std::array<int, 3>{1, 2, 3};
  std::vector<std::string> v = l;
  EXPECT_EQ(concat(l, v), "123123");
  EXPECT_EQ(concat(t), "123");
  EXPECT_EQ(concat(p), "12, 3");
  EXPECT_EQ(concat(a), "123");

  s.clear();
  append_join_with(s, ";", "123"); // single
  append_join_with(s, ";", "456");
  EXPECT_EQ(s, "123;456");
  s.clear();
  append_join_with(s, ";", v); // container
  EXPECT_EQ(s, "1;2;3");
  s.clear();
  append_join_with(s, ";", t); // tuple
  EXPECT_EQ(s, "1;2;3");
  s.clear();
  append_join_with<braces_opt::forced>(s, ";", t); // tuple
  EXPECT_EQ(s, "{1;2;3}");

  EXPECT_EQ(join(1, 2, 3), "1, 2, 3");
  EXPECT_EQ(join("1"s, "2"s, "3"s), "1, 2, 3");
  EXPECT_EQ(join<braces_opt::forced>(t), "{1, 2, 3}");
  EXPECT_EQ(join<braces_opt::forced>(p), "{1, 2, 3}");
  EXPECT_EQ(join<braces_opt::forced>(a), "[1, 2, 3]");

  EXPECT_EQ(join<braces_opt::flat>(t), "1, 2, 3");
  EXPECT_EQ(join<braces_opt::flat>(p), "1, 2, 3");
  EXPECT_EQ(join<braces_opt::flat>(a), "1, 2, 3");

  EXPECT_EQ(concat('1', '2', '3'), "123");

  EXPECT_EQ(join(mssi), "[1, 2, 3], [4, 5]");
  EXPECT_EQ(join<braces_opt::forced>(mssi), "[[1, 2, 3], [4, 5]]");

  // TODO: test plain array, std::array, map, set, pair, tuple

  // TODO: Test objects that aren't strings but do have implicit conversion to
  // string or string_view.

  // TODO: expose from_chars
}
