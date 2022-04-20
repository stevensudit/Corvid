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

#include <cstdint>

using namespace std::literals;
using namespace corvid;

TEST(StringUtilsTest, ExtractPiece) {
  std::string_view sv;
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  sv = "1,2";
  EXPECT_EQ(strings::extract_piece(sv, ","), "1");
  EXPECT_EQ(strings::extract_piece(sv, ","), "2");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  sv = ",";
  EXPECT_EQ(sv.size(), 1);
  EXPECT_EQ(strings::extract_piece(sv, ","), "");
  EXPECT_EQ(strings::extract_piece(sv, ","), "");

  sv = "1,2,3,4";
  EXPECT_EQ(strings::extract_piece<std::string>(sv, ","), "1");
}

TEST(StringUtilsTest, MorePieces) {
  std::string_view w, part;
  w = "1,2";
  EXPECT_FALSE(w.empty());
  EXPECT_TRUE(strings::more_pieces(part, w, ","));
  EXPECT_EQ(part, "1");
  EXPECT_FALSE(w.empty());
  EXPECT_FALSE(strings::more_pieces(part, w, ","));
  EXPECT_EQ(part, "2");
  EXPECT_TRUE(w.empty());
  EXPECT_FALSE(strings::more_pieces(part, w, ","));

  w = "1,";
  EXPECT_FALSE(w.empty());
  EXPECT_TRUE(strings::more_pieces(part, w, ","));
  EXPECT_EQ(part, "1");
  EXPECT_TRUE(w.empty());
  EXPECT_FALSE(strings::more_pieces(part, w, ","));
  EXPECT_EQ(part, "");
}

TEST(StringUtilsTest, Split) {
  if (true) {
    using V = std::vector<std::string_view>;

    EXPECT_EQ(strings::split(""sv, ","), (V{}));
    EXPECT_EQ(strings::split("1"sv, ","), (V{"1"}));
    EXPECT_EQ(strings::split("1,"sv, ","), (V{"1", ""}));
    EXPECT_EQ(strings::split(",1"sv, ","), (V{"", "1"}));
    EXPECT_EQ(strings::split(",,"sv, ","), (V{"", "", ""}));
    EXPECT_EQ(strings::split("1,2"sv, ","), (V{"1", "2"}));
    EXPECT_EQ(strings::split("1,2,3"sv, ","), (V{"1", "2", "3"}));
    EXPECT_EQ(strings::split("11"sv, ","), (V{"11"}));
    EXPECT_EQ(strings::split("11,"sv, ","), (V{"11", ""}));
    EXPECT_EQ(strings::split(",11"sv, ","), (V{"", "11"}));
    EXPECT_EQ(strings::split("11,22"sv, ","), (V{"11", "22"}));
    EXPECT_EQ(strings::split("11,22,33"sv, ","), (V{"11", "22", "33"}));
  }
  if (true) {
    using V = std::vector<std::string>;

    EXPECT_EQ(strings::split("", ","), (V{}));
    EXPECT_EQ(strings::split("1", ","), (V{"1"}));
    EXPECT_EQ(strings::split("1,", ","), (V{"1", ""}));
    EXPECT_EQ(strings::split(",1", ","), (V{"", "1"}));
    EXPECT_EQ(strings::split(",,", ","), (V{"", "", ""}));
    EXPECT_EQ(strings::split("1,2", ","), (V{"1", "2"}));
    EXPECT_EQ(strings::split("1,2,3", ","), (V{"1", "2", "3"}));
    EXPECT_EQ(strings::split("11", ","), (V{"11"}));
    EXPECT_EQ(strings::split("11,", ","), (V{"11", ""}));
    EXPECT_EQ(strings::split(",11", ","), (V{"", "11"}));
    EXPECT_EQ(strings::split("11,22", ","), (V{"11", "22"}));
    EXPECT_EQ(strings::split("11,22,33", ","), (V{"11", "22", "33"}));
  }
  if (true) {
    using V = std::vector<std::string_view>;
    using S = std::vector<std::string>;
    auto w = "1,2,3,4"sv;
    std::string s{w};
    EXPECT_EQ(strings::split(w, ","), (V{"1", "2", "3", "4"}));
    EXPECT_EQ(strings::split(s, ","), (V{"1", "2", "3", "4"}));
    EXPECT_EQ(strings::split(std::move(s), ","), (S{"1", "2", "3", "4"}));
  }
  if (true) {
    using R = std::string;
    using V = std::vector<R>;

    EXPECT_EQ(strings::split<R>("11,22,33", ","), (V{"11", "22", "33"}));
    EXPECT_EQ(strings::split<R>(R{"11,22,33"}, ","), (V{"11", "22", "33"}));
  }
}

TEST(StringUtilsTest, Trim) {
  if (true) {
    EXPECT_EQ(strings::trim_left(""), "");
    EXPECT_EQ(strings::trim_left("1"), "1");
    EXPECT_EQ(strings::trim_left("12"), "12");
    EXPECT_EQ(strings::trim_left("123"), "123");
    EXPECT_EQ(strings::trim_left(" "), "");
    EXPECT_EQ(strings::trim_left(" 1"), "1");
    EXPECT_EQ(strings::trim_left(" 12"), "12");
    EXPECT_EQ(strings::trim_left(" 123"), "123");
    EXPECT_EQ(strings::trim_left("  "), "");
    EXPECT_EQ(strings::trim_left("  1"), "1");
    EXPECT_EQ(strings::trim_left("  12"), "12");
    EXPECT_EQ(strings::trim_left("  123"), "123");
    EXPECT_EQ(strings::trim_left("  1  "), "1  ");

    EXPECT_EQ(strings::trim_right(""), "");
    EXPECT_EQ(strings::trim_right("1"), "1");
    EXPECT_EQ(strings::trim_right("12"), "12");
    EXPECT_EQ(strings::trim_right("123"), "123");
    EXPECT_EQ(strings::trim_right(" "), "");
    EXPECT_EQ(strings::trim_right("1 "), "1");
    EXPECT_EQ(strings::trim_right("12 "), "12");
    EXPECT_EQ(strings::trim_right("123 "), "123");
    EXPECT_EQ(strings::trim_right("  "), "");
    EXPECT_EQ(strings::trim_right("1  "), "1");
    EXPECT_EQ(strings::trim_right("12  "), "12");
    EXPECT_EQ(strings::trim_right("123  "), "123");
    EXPECT_EQ(strings::trim_right("  1  "), "  1");

    EXPECT_EQ(strings::trim(""), "");
    EXPECT_EQ(strings::trim("1"), "1");
    EXPECT_EQ(strings::trim("12"), "12");
    EXPECT_EQ(strings::trim("123"), "123");
    EXPECT_EQ(strings::trim("  "), "");
    EXPECT_EQ(strings::trim(" 1 "), "1");
    EXPECT_EQ(strings::trim(" 12 "), "12");
    EXPECT_EQ(strings::trim(" 123 "), "123");
    EXPECT_EQ(strings::trim("    "), "");
    EXPECT_EQ(strings::trim("  1  "), "1");
    EXPECT_EQ(strings::trim("  12  "), "12");
    EXPECT_EQ(strings::trim("  123  "), "123");
    EXPECT_EQ(strings::trim("  1  "), "1");

    EXPECT_EQ(strings::trim_braces("[]"), "");
    EXPECT_EQ(strings::trim_braces("[1]"), "1");
    EXPECT_EQ(strings::trim_braces("[12]"), "12");
    EXPECT_EQ(strings::trim_braces("12]"), "12]");
    EXPECT_EQ(strings::trim_braces("[12]"), "12");
    EXPECT_EQ(strings::trim_braces("'12'", "'"), "12");
  }
  if (true) {
    auto w = " 1, 2, 3  , 4 "s;

    auto vsv = strings::split(w, ",");
    EXPECT_EQ(vsv[0], " 1");
    //* auto x = string::trim(v);
    strings::trim(vsv);
    EXPECT_EQ(vsv[0], "1");

    auto vs = strings::split<std::string>(w, ",");
    strings::trim(vs);
    EXPECT_EQ(vsv[0], "1");

    EXPECT_EQ((strings::trim(strings::split(w, ","))[0]), "1");
    auto x = strings::trim(strings::split<std::string>(w, ","));
    EXPECT_EQ(x[0], "1");

    vsv = strings::split(w, ",");
    std::map<int, std::string> mss;
    for (int i = 0; i < vsv.size(); ++i) {
      mss[i] = vsv[i];
    }
    EXPECT_EQ(mss[0], " 1");
    strings::trim(mss);
    EXPECT_EQ(mss[0], "1");
  }
}

TEST(StringUtilsTest, Append) {
  std::string s;

  s.clear();
  strings::append(s, "1"); // is_string_view_convertible_v
  strings::append(s, "2"s);
  strings::append(s, "3"sv);
  EXPECT_EQ(s, "123");

  s.clear();
  strings::append(s, '1');       // char (number)
  strings::append(s, "2", "3");  // many
  strings::append(s, (size_t)4); // append size_t
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append<2>(s, 0xaa); // append binary
  EXPECT_EQ(s, "10101010");
  s.clear();
  strings::append<16>(s, 0xaa); // append hex
  EXPECT_EQ(s, "aa");

  s.clear();
  strings::append(s, true);           // append bool
  strings::append(s, (float)2);       // append float
  strings::append(s, (double)3);      // append float
  strings::append(s, (long double)4); // append float
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.456789012345");
  s.clear();
  strings::append<std::chars_format::scientific>(s, (double)123.456789012345);
  EXPECT_EQ(s, "1.23456789012345e+02");
  s.clear();
  strings::append<std::chars_format::fixed, 1>(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.5");
  s.clear();
  strings::append<std::chars_format::fixed, 6>(s, (double)123.456789012345);
  EXPECT_EQ(s, "123.456789");

  const int i = 42;
  s.clear();
  strings::append(s, i);
  EXPECT_EQ(s, "42");
  s.clear();
  strings::append(s, &i);
  EXPECT_EQ(s, "42");
  s.clear();
  strings::append(s, reinterpret_cast<intptr_t>(&i));
  EXPECT_NE(s, "42");
  s.clear();
  strings::append(s, reinterpret_cast<const void*>(&i));
  EXPECT_NE(s, "42");
  auto oi = std::make_optional(i);
  s.clear();
  strings::append(s, oi);
  EXPECT_EQ(s, "42");
  oi.reset();
  s.clear();
  strings::append(s, oi);
  EXPECT_EQ(s, "");

  // None of these char-like types are char so they're all treated as ints.
  EXPECT_FALSE((std::is_same_v<char, signed char>));
  EXPECT_FALSE((std::is_same_v<char, unsigned char>));
  EXPECT_FALSE((std::is_same_v<signed char, unsigned char>));

  s.clear();
  strings::append(s, (signed char)1);
  strings::append(s, (unsigned char)2);
  strings::append(s, (wchar_t)3);
  strings::append(s, (char16_t)4);
  strings::append(s, (char32_t)5);
  EXPECT_EQ(s, "12345");
  // C++20's char8_t is also distinct.

  // Might as well test all the integral types, if only for regression.
  s.clear();
  strings::append(s, (short int)1);
  strings::append(s, (unsigned short int)2);
  ;
  strings::append(s, (int)3);
  strings::append(s, (unsigned int)4);
  strings::append(s, (long int)5);
  strings::append(s, (unsigned long int)6);
  strings::append(s, (long long int)7);
  strings::append(s, (unsigned long long int)8);
  EXPECT_EQ(s, "12345678");

  s.clear();
  strings::append(s, (int8_t)1);
  strings::append(s, (int16_t)2);
  strings::append(s, (int32_t)3);
  strings::append(s, (int64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (uint8_t)1);
  strings::append(s, (uint16_t)2);
  strings::append(s, (uint32_t)3);
  strings::append(s, (uint64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (int_fast8_t)1);
  strings::append(s, (int_fast16_t)2);
  strings::append(s, (int_fast32_t)3);
  strings::append(s, (int_fast64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (uint_fast8_t)1);
  strings::append(s, (uint_fast16_t)2);
  strings::append(s, (uint_fast32_t)3);
  strings::append(s, (uint_fast64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (int_least8_t)1);
  strings::append(s, (int_least16_t)2);
  strings::append(s, (int_least32_t)3);
  strings::append(s, (int_least64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (uint_least8_t)1);
  strings::append(s, (uint_least16_t)2);
  strings::append(s, (uint_least32_t)3);
  strings::append(s, (uint_least64_t)4);
  EXPECT_EQ(s, "1234");

  s.clear();
  strings::append(s, (intmax_t)1);
  strings::append(s, (intptr_t)2);
  strings::append(s, (uintmax_t)3);
  strings::append(s, (uintptr_t)4);
  EXPECT_EQ(s, "1234");

  enum ColorEnum { red, green = 20, blue };
  enum class ColorClass { red, green = 20, blue };

  s.clear();
  strings::append(s, green);
  strings::append(s, ColorClass::blue);
  EXPECT_EQ(s, "2021");
  s.clear();
  // This just tests enum support.
  strings::append(s, strings::braces_opt::flat);
  EXPECT_EQ(s, "1");
  s.clear();
  strings::append(s, strings::braces_opt(32));
  EXPECT_EQ(s, "32"); // not hex!

  std::map<std::string, int> msi{{"a", 1}, {"c", 2}};
  std::set<int> si{3, 4, 5};
  s.clear();
  strings::append(s, msi);
  strings::append(s, si);
  EXPECT_EQ(s, "12345");

  std::variant<int, std::map<std::string, int>> va;
  va = 52;
  s.clear();
  strings::append(s, va);
  EXPECT_EQ(s, "52");
  EXPECT_EQ(strings::concat(va), "52");
  va = msi;
  s.clear();
  strings::append(s, va);
  EXPECT_EQ(s, "12");

  std::map<std::string, std::set<int>> mssi{{"c", {5, 4}}, {"a", {3, 2, 1}}};
  s.clear();
  strings::append(s, mssi);
  EXPECT_EQ(s, "12345");

  EXPECT_FALSE((std::is_same_v<int8_t, char>));
  EXPECT_EQ(strings::from_num(42), "42");
  EXPECT_EQ(strings::from_num<16>(10), "a");
  EXPECT_EQ(strings::from_num('a'), "97");
  EXPECT_EQ(strings::from_num(123.0l), "123");
  EXPECT_EQ(strings::from_num(12.3l), "12.3");

  // TODO: Nested container torture test.

  EXPECT_EQ(strings::concat("1", "2"sv, "3"s), "123");
  EXPECT_EQ(strings::concat(1, 2.0, 3ull), "123");
  EXPECT_EQ(strings::concat(true, std::byte{2}, 3), "123");
  EXPECT_EQ((strings::concat("1", "2")), "12");

  auto t = std::make_tuple("1"s, 2, 3.0);
  auto l = {"1"s, "2"s, "3"s};
  auto p = std::make_pair("1"s, "2, 3");
  auto a = std::array<int, 3>{1, 2, 3};
  std::vector<std::string> v = l;
  EXPECT_EQ(strings::concat(l, v), "123123");
  EXPECT_EQ(strings::concat(t), "123");
  EXPECT_EQ(strings::concat(p), "12, 3");
  EXPECT_EQ(strings::concat(a), "123");

  s.clear();
  strings::append_join_with(s, ";", "123"); // single
  strings::append_join_with<strings::braces_opt::braced, '`', '\''>(s, ";",
      "456");
  EXPECT_EQ(s, "123;`456'");
  s.clear();
  strings::append_join_with(s, ";", v); // container
  EXPECT_EQ(s, "[1;2;3]");
  s = "a";
  strings::append_join_with(s, ";", v);
  EXPECT_EQ(s, "a;[1;2;3]");
  s = "v=";
  strings::append_join_with<strings::braces_opt::merged>(s, ";", v);
  EXPECT_EQ(s, "v=[1;2;3]");
  s.clear();
  strings::append_join_with(s, ";", t); // tuple
  EXPECT_EQ(s, "{1;2;3}");
  s.clear();
  strings::append_join_with<strings::braces_opt::flat>(s, ";", t); // tuple
  EXPECT_EQ(s, "1;2;3");

  s.clear();
  strings::append_join_with(s, ";", intptr_t(42));
  EXPECT_NE(s, "42");
  s.clear();
  strings::append_join_with(s, ";", reinterpret_cast<const void*>(&i));
  EXPECT_NE(s, "42");
  s.clear();
  strings::append_join_with(s, ";", reinterpret_cast<const void*>(NULL));
  EXPECT_EQ(s, "0");
  s.clear();
  const char* psz{};
  strings::append_join_with(s, ";", psz);
  EXPECT_EQ(s, "");

  va = 52;
  s.clear();
  strings::append_join_with(s, ";", va);
  EXPECT_EQ(s, "52");
  EXPECT_EQ(strings::concat(va), "52");
  va = msi;
  s.clear();
  strings::append_join_with(s, ";", va);
  EXPECT_EQ(s, "[1;2]");

  s.clear();
  strings::append_join_with(s, ";", mssi);
  EXPECT_EQ(s, "[[1;2;3];[4;5]]");
  auto om = std::make_optional(mssi);
  s.clear();
  strings::append_join_with(s, ";", om);
  EXPECT_EQ(s, "[[1;2;3];[4;5]]");
  s.clear();
  strings::append_join<strings::braces_opt::keyed>(s, mssi);
  EXPECT_EQ(s, "[(a, [1, 2, 3]), (c, [4, 5])]");

  s.clear();
  auto il = {3, 1, 4};
  strings::append_join_with(s, ";", il); // Initializer list.
  // Unbound initializer lists can't be deduced by templates.
  // * strings::append_join_with(s, ";", {1, 2, 3});
  EXPECT_EQ(s, "[3;1;4]");

  EXPECT_EQ(strings::join(1, 2, 3), "1, 2, 3");
  EXPECT_EQ(strings::join("1"s, "2"s, "3"s), "1, 2, 3");
  EXPECT_EQ(strings::join(t), "{1, 2, 3}");
  EXPECT_EQ(strings::join(p), "(1, 2, 3)");
  EXPECT_EQ(strings::join(a), "[1, 2, 3]");

  EXPECT_EQ(strings::join<strings::braces_opt::flat>(t), "1, 2, 3");
  EXPECT_EQ(strings::join<strings::braces_opt::flat>(p), "1, 2, 3");
  EXPECT_EQ(strings::join<strings::braces_opt::flat>(a), "1, 2, 3");

  EXPECT_EQ(strings::concat('1', '2', '3'), "123");

  EXPECT_EQ(strings::join(mssi), "[[1, 2, 3], [4, 5]]");
  EXPECT_EQ(strings::join<strings::braces_opt::flat>(mssi), "1, 2, 3, 4, 5");

  // TODO: test plain array, std::array, map, set, pair, tuple

  // TODO: Test objects that aren't strings but do have implicit conversion to
  // string or string_view.
}

TEST(StringUtilsTest, Print) {
  if (true) {
    std::stringstream ss;
    strings::stream_out(ss, "abc=", 5, ';');
    EXPECT_EQ(ss.str(), "abc=5;");
    strings::stream_out(ss);
    EXPECT_EQ(ss.str(), "abc=5;");
    strings::stream_out(ss, '\n');
    EXPECT_EQ(ss.str(), "abc=5;\n");
  }
  if (true) {
    std::stringstream ss;
    strings::stream_out_with(ss, ",", "abc", 5, "def", true);
    EXPECT_EQ(ss.str(), "abc,5,def,1");

    ss.str("");
    strings::ostream_redirector clog_to_ss(std::clog, ss);
    strings::log("a=", 5);
    EXPECT_EQ(ss.str(), "a=5\n");
  }
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::println("a=", 5);
    EXPECT_EQ(ss.str(), "a=5\n");
    ss.str("");
    strings::println_with(", ", 'a', 5, "bc", 5.5);
    EXPECT_EQ(ss.str(), "a, 5, bc, 5.5\n");
  }
}

TEST(StringUtilsTest, ParseNum) {
  if (true) {
    std::string_view sv;
    sv = "123";
    int64_t t;
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 123);
    EXPECT_TRUE(sv.empty());

    sv = "123 456";
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 123);
    EXPECT_FALSE(sv.empty());
    EXPECT_EQ(sv, " 456");
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 456);
    EXPECT_TRUE(sv.empty());

    sv = "abc";
    EXPECT_FALSE(strings::extract_num(t, sv));
    EXPECT_EQ(sv, "abc");
    EXPECT_TRUE(strings::extract_num<16>(t, sv));
    EXPECT_EQ(t, 0xabc);
    EXPECT_TRUE(sv.empty());

    sv = "123";
    auto r = strings::extract_num(sv);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 123);
    EXPECT_TRUE(sv.empty());
    r = strings::extract_num(sv);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(42, r.value_or(42));

    sv = "123";
    t = strings::parse_num(sv, -1);
    EXPECT_EQ(t, 123);
    sv = "abc";
    t = strings::parse_num(sv, -1);
    EXPECT_EQ(t, -1);
    t = strings::parse_num<int64_t, 16>(sv, -1);
    EXPECT_EQ(t, 0xabc);
  }
  if (true) {
    std::string_view sv;
    sv = "12.3";
    double t;
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 12.3);
    EXPECT_TRUE(sv.empty());

    sv = "12.3 45.6";
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 12.3);
    EXPECT_FALSE(sv.empty());
    EXPECT_EQ(sv, " 45.6");
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(t, 45.6);
    EXPECT_TRUE(sv.empty());

    std::string s;
    strings::append<std::chars_format::hex>(s, 12.3l);
    sv = s;
    EXPECT_EQ(sv, "1.899999999999ap+3");
    // Succeed with totally wrong answer.
    EXPECT_TRUE(strings::extract_num(t, sv));
    EXPECT_EQ(sv, "ap+3");
    sv = s;
    EXPECT_EQ(t, 1.8999999999990000l);
    EXPECT_TRUE(strings::extract_num<std::chars_format::hex>(t, sv));
    EXPECT_EQ(t, 12.3l);
    EXPECT_TRUE(sv.empty());

    sv = "12.3";
    auto r = strings::extract_num<double>(sv);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 12.3);
    EXPECT_TRUE(sv.empty());
    r = strings::extract_num<double>(sv);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(4.2, r.value_or(4.2));

    sv = "12.3";
    t = strings::parse_num<double>(sv, -1.2);
    EXPECT_EQ(t, 12.3);
    sv = "xyz";
    t = strings::parse_num<double>(sv, -1.2);
    EXPECT_EQ(t, -1.2);
  }
}

TEST(StringUtilsTest, Replace) {
  std::string s;

  s = "abcdefghij";
  EXPECT_EQ(0, strings::replace(s, "bac", "yyy"));
  EXPECT_EQ(s, "abcdefghij");
  EXPECT_EQ(1, strings::replace(s, "abc", "yyy"));
  EXPECT_EQ(s, "yyydefghij");
  EXPECT_EQ(3, strings::replace(s, "y", "z"));
  EXPECT_EQ(s, "zzzdefghij");
  EXPECT_EQ(3, strings::replace(s, 'z', 'x'));
  EXPECT_EQ(s, "xxxdefghij");
}

TEST(StringUtilsTest, Case) {
  auto s = "abcdefghij"s;
  strings::to_upper(s);
  EXPECT_EQ(s, "ABCDEFGHIJ");
  strings::to_lower(s);
  EXPECT_EQ(s, "abcdefghij");
  EXPECT_EQ(strings::as_lower("ABCDEFGHIJ"), "abcdefghij");
  EXPECT_EQ(strings::as_upper("abcdefghij"), "ABCDEFGHIJ");
}
