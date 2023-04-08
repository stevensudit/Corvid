// Corvid20: A general-purpose C++ 20 library extending std.
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

#include "../includes/strings.h"
#include "AccutestShim.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::enums::sequence;
using namespace corvid::enums::bitmask;

void StringUtilsTest_ExtractPiece() {
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

void StringUtilsTest_MorePieces() {
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

void StringUtilsTest_Split() {
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

void StringUtilsTest_Case() {
  auto s = "abcdefghij"s;
  strings::to_upper(s);
  EXPECT_EQ(s, "ABCDEFGHIJ");
  strings::to_lower(s);
  EXPECT_EQ(s, "abcdefghij");
  EXPECT_EQ(strings::as_lower("ABCDEFGHIJ"), "abcdefghij");
  EXPECT_EQ(strings::as_upper("abcdefghij"), "ABCDEFGHIJ");
  char a[] = "abcdefghij";
  strings::to_upper(a);
  EXPECT_EQ(a, "ABCDEFGHIJ"sv);
}

void StringUtilsTest_Replace() {
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
  EXPECT_EQ(strings::replaced("abcdef", "abc", "yyy"), "yyydef");
}

template<AppendTarget T>
auto& test_append(T& target, std::string_view sv) {
  return *strings::appender{target}
              .append(sv)
              .append(sv[0])
              .append(sv.data(), sv.size())
              .append(4, sv[0]);
}

void StringUtilsTest_Target() {
  if (true) {
    std::ostringstream oss;
    EXPECT_EQ(test_append(oss, "abc").str(), "abcaabcaaaa");
  }
  if (true) {
    std::string s;
    EXPECT_EQ(test_append(s, "abc"), "abcaabcaaaa");
    strings::appender(s).reserve(500);
  }
#ifdef WILL_NOT_COMPILE
  if (true) {
    int i;
    EXPECT_EQ(test_append(i, "abc").str(), "abcaabcaaaa");
    strings::appender(i).append("abc");
  }
#endif
}

struct NotStreamable {};

void StringUtilsTest_Print() {
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
    strings::ostream_redirector cerr_to_ss(std::cerr, ss);
    strings::report("a=", 5);
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
#ifdef WILL_NOT_COMPILE
  if (true) {
    std::stringstream ss;
    strings::ostream_redirector cout_to_ss(std::cout, ss);
    strings::print("a=", NotStreamable{});
    EXPECT_EQ(ss.str(), "a=5");
  }
#endif
}

void StringUtilsTest_Trim() {
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

void StringUtilsTest_ParseNum() {
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

#ifdef ONLY_WORKED_ON_MSVC
    std::string s;
    // strings::append<std::chars_format::hex>(s, 12.3l);
    s = "1.899999999999ap+3";
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
#endif

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

void StringUtilsTest_AppendNum() {
  if (true) {
    EXPECT_EQ(strings::num_as_string(1), "1");
    EXPECT_EQ(strings::num_as_string(0), "0");
    EXPECT_EQ((strings::num_as_string<10, 5>(0)), "    0");
    EXPECT_EQ((strings::num_as_string<16>(uint8_t(0))), "0x00");
    EXPECT_EQ((strings::num_as_string<16>(uint16_t(0))), "0x0000");
    EXPECT_EQ((strings::num_as_string<16>(uint32_t(0))), "0x00000000");
    EXPECT_EQ((strings::num_as_string<16>(uint64_t(0))), "0x0000000000000000");
    EXPECT_EQ((strings::num_as_string(float(0.25f))), "0.25");
    EXPECT_EQ((strings::num_as_string(double(0.25f))), "0.25");
    EXPECT_EQ((strings::num_as_string<std::chars_format::hex>(double(0.25))),
        "1p-2");
    EXPECT_EQ(
        (strings::num_as_string<std::chars_format::fixed>(double(65536.25))),
        "65536.25");
    EXPECT_EQ((strings::num_as_string<std::chars_format::scientific>(
                  double(65536.25))),
        "6.553625e+04");
    EXPECT_EQ(
        (strings::num_as_string<std::chars_format::hex>(double(65536.25))),
        "1.00004p+16");
    EXPECT_EQ(
        (strings::num_as_string<std::chars_format::general>(double(65536.25))),
        "65536.25");
  }
}

void StringUtilsTest_Append() {
  using strings::join_opt;
  std::string s;

  s.clear();
  strings::append(s, "1"); // is_string_view_convertible_v
  strings::append(s, "2"s);
  strings::append(s, "3"sv);
  EXPECT_EQ(s, "123");
  s.clear();
  strings::append(s, '1');       // char (digit)
  strings::append(s, "2", "3");  // many
  strings::append(s, (char*){}); // char nullptr
  strings::append(s, (size_t)4); // append size_t
  strings::append(s, nullptr);   // nullptr
  EXPECT_EQ(s, "123null4null");

  s.clear();
  strings::append<2>(s, 0xaa); // append binary
  EXPECT_EQ(s, "10101010");
  s.clear();
  strings::append<16>(s, 0xaa); // append hex
  EXPECT_EQ(s, "0x000000aa");

  s.clear();
  strings::append(s, false);          // append bool
  strings::append(s, true);           // append bool
  strings::append(s, (float)2);       // append float
  strings::append(s, (double)3);      // append float
  strings::append(s, (long double)4); // append float
  EXPECT_EQ(s, "falsetrue234");

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
  EXPECT_FALSE(s.starts_with("0x"));
  s.clear();
  strings::append(s, (void*){});
  EXPECT_EQ(s, "0x0000000000000000");
  s.clear();
  strings::append(s, reinterpret_cast<const void*>(&i));
  EXPECT_NE(s, "42");
  EXPECT_TRUE(s.starts_with("0x"));
  auto oi = std::make_optional(i);
  s.clear();
  strings::append(s, oi);
  EXPECT_EQ(s, "42");
  oi.reset();
  s.clear();
  strings::append(s, oi);
  EXPECT_EQ(s, "null");

  // None of these char-like types are char so they're all treated as ints.
  EXPECT_FALSE((std::is_same_v<char, signed char>));
  EXPECT_FALSE((std::is_same_v<char, unsigned char>));
  EXPECT_FALSE((std::is_same_v<char, char8_t>));
  EXPECT_FALSE((std::is_same_v<signed char, unsigned char>));

  s.clear();
  strings::append(s, (signed char)1);
  strings::append(s, (unsigned char)2);
  strings::append(s, (wchar_t)3);
  strings::append(s, (char8_t)4);
  strings::append(s, (char16_t)5);
  strings::append(s, (char32_t)6);
  EXPECT_EQ(s, "123456");

  // Might as well test all the integral types, if only for regression.
  s.clear();
  strings::append(s, (short int)1);
  strings::append(s, (unsigned short int)2);
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

  strings::append(s, ColorClass::green);
  EXPECT_EQ(s, "20");
  s.clear();
  strings::append(s, ColorClass(20));
  EXPECT_EQ(s, "20"); // not hex!

  std::map<std::string, int> msi{{"a", 1}, {"c", 2}};
  std::set<int> si{3, 4, 5};
  s.clear();
  strings::append(s, msi);
  strings::append(s, si);
  EXPECT_EQ(s, "12345");

  std::variant<std::monostate, int, std::map<std::string, int>> va;
  s.clear();
  strings::append(s, va);
  EXPECT_EQ(s, "null");
  va = 52;
  s.clear();
  strings::append(s, va);
  EXPECT_EQ(s, "52");

  va = msi;
  s.clear();
  strings::append(s, va);
  EXPECT_EQ(s, "12");

  std::map<std::string, std::set<int>> mssi{{"c", {5, 4}}, {"a", {3, 2, 1}}};
  s.clear();
  strings::append(s, mssi);
  EXPECT_EQ(s, "12345");

  EXPECT_FALSE((std::is_same_v<int8_t, char>));
  EXPECT_EQ(strings::num_as_string(42), "42");
  EXPECT_EQ(strings::num_as_string<16>(10), "0x0000000a");
  EXPECT_EQ(strings::num_as_string('a'), "97");
  EXPECT_EQ(strings::num_as_string(123.0l), "123");
  EXPECT_EQ(strings::num_as_string(12.3l), "12.3");

  // TODO: Add nested container torture test.

  EXPECT_EQ(strings::concat("1", "2"sv, "3"s), "123");
  EXPECT_EQ(strings::concat(1, 2.0, 3ull), "123");
  EXPECT_EQ(strings::concat(true, std::byte{2}, 3), "true23");
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

  EXPECT_EQ((strings::join<join_opt::quoted>(l)), R"(["1", "2", "3"])");

  s.clear();
  strings::append_join_with(s, ";", "123"); // single
  EXPECT_EQ(s, "123");
  strings::append_join_with(s, ";", std::optional<std::string>{});
  strings::append_join_with(s, ";", "456");
  EXPECT_EQ(s, "123null456");
  strings::append_join_with<join_opt::prefixed, '`', '\''>(s, ";", "789");
  EXPECT_EQ(s, "123null456;`789'");

  s.clear();
  strings::append_join_with(s, ";", v); // container
  EXPECT_EQ(s, "[1;2;3]");

  s = "a";
  strings::append_join_with<join_opt::prefixed>(s, ";", v);
  EXPECT_EQ(s, "a;[1;2;3]");
  s = "v=";
  strings::append_join_with(s, ";", v);
  EXPECT_EQ(s, "v=[1;2;3]");

  s.clear();
  strings::append_join_with(s, ";", t); // tuple
  EXPECT_EQ(s, "{1;2;3}");

  s.clear();
  strings::append_join_with<join_opt::flat>(s, ";", t); // tuple
  EXPECT_EQ(s, "1;2;3");

  s.clear();
  strings::append_join_with(s, ";", intptr_t(42));
  EXPECT_EQ(s, "42");

  s.clear();
  strings::append_join_with(s, ";", reinterpret_cast<const void*>(&i));
  EXPECT_NE(s, "42");

  s.clear();
  strings::append_join_with(s, ";", reinterpret_cast<const void*>(NULL));
  EXPECT_EQ(s, "0x0000000000000000");

  s.clear();
  const char* psz{};
  strings::append_join_with(s, ";", psz);
  EXPECT_EQ(s, "null");

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
  strings::append_join_with(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, "1");

  s.clear();
  strings::append_join_with<join_opt::flat>(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, "1");

  s.clear();
  strings::append_join_with<join_opt::keyed>(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, "{a,1}");

  s.clear();
  strings::append_join_with<join_opt::flat_keyed>(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, "a,1");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ",", std::pair{"a", 1});
  EXPECT_EQ(s, R"("a": 1)");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ",", std::pair{1, "a"});
  EXPECT_EQ(s, R"("1": "a")");

  s.clear();
  strings::append_join_with(s, ",", mssi);
  EXPECT_EQ(s, "[[1,2,3],[4,5]]");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ", ", mssi);
  EXPECT_EQ(s, R"({"a": [1, 2, 3], "c": [4, 5]})");

  auto om = std::make_optional(mssi);
  s.clear();
  strings::append_join_with(s, ",", om);
  EXPECT_EQ(s, "[[1,2,3],[4,5]]");

  s.clear();
  strings::append_join<join_opt::json>(s, mssi);
  EXPECT_EQ(s, R"({"a": [1, 2, 3], "c": [4, 5]})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<std::string, int>{{"b", 2}, {"a", 1}});
  EXPECT_EQ(s, R"({"a": 1, "b": 2})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<std::string, std::string>{{"b", "2"}, {"a", "1"}});
  EXPECT_EQ(s, R"({"a": "1", "b": "2"})");

  s.clear();
  strings::append_join<join_opt::json>(s,
      std::map<int, std::string>{{2, "2"}, {1, "1"}});
  EXPECT_EQ(s, R"({"1": "1", "2": "2"})");

  s.clear();
  auto il = {3, 1, 4};
  strings::append_join_with(s, ";", il); // Initializer list.
  // Unbound initializer lists can't be deduced by templates.
  // * strings::append_join_with(s, ";", {1, 2, 3});
  EXPECT_EQ(s, "[3;1;4]");

  EXPECT_EQ(strings::join(1, 2, 3), "[1, 2, 3]");
  EXPECT_EQ(strings::join("1"s, "2"s, "3"s), "[1, 2, 3]");
  EXPECT_EQ(strings::join(t), "{1, 2, 3}");
  EXPECT_EQ(strings::join<join_opt::keyed>(p), "{1, 2, 3}");
  s.clear();
  strings::append_join_with<join_opt::keyed>(s, ",", std::pair{1, 2});
  EXPECT_EQ(s, "{1,2}");
  EXPECT_EQ(strings::join(a), "[1, 2, 3]");

  EXPECT_EQ(strings::join<join_opt::flat_keyed>(t), "1, 2, 3");
  EXPECT_EQ(strings::join<join_opt::flat_keyed>(p), "1, 2, 3");
  EXPECT_EQ(strings::join<join_opt::flat_keyed>(a), "1, 2, 3");

  EXPECT_EQ(strings::concat('1', '2', '3'), "123");

  EXPECT_EQ(strings::join(mssi), "[[1, 2, 3], [4, 5]]");
  EXPECT_EQ(strings::join<join_opt::flat>(mssi), "1, 2, 3, 4, 5");

  // More JSON A/B tests.
  s.clear();
  strings::append_join_with(s, ", ", std::vector{1, 2, 3});
  EXPECT_EQ(s, "[1, 2, 3]");

  s.clear();
  strings::append_join_with<join_opt::json>(s, ", ", std::vector{1, 2, 3});
  EXPECT_EQ(s, "[1, 2, 3]");

  s.clear();
  strings::append_join_with(s, ", ", std::vector<int>{});
  EXPECT_EQ(s, "[]");

  s = strings::join_json(std::vector<int>{});
  EXPECT_EQ(s, "[]");

  s.clear();
  strings::append_join_with(s, ", ", std::vector{"a", "b", "c"});
  EXPECT_EQ(s, "[a, b, c]");

  s = strings::join_json(std::vector{"a", "b", "c"});
  EXPECT_EQ(s, R"(["a", "b", "c"])");

  s = strings::join_json(std::vector{"a", "b", "c"},
      std::vector{"d", "e", "f"});
  EXPECT_EQ(s, R"([["a", "b", "c"], ["d", "e", "f"]])");

  // TODO: test plain array, std::array, map, set, pair, tuple

  // TODO: Test objects that aren't strings but do have implicit conversion
  // to string or string_view.
}

void StringUtilsTest_Edges() {
  using strings::join_opt;

  std::vector<int> a{1, 2, 3};
  std::vector<int> b{4, 5, 6};
  EXPECT_EQ(strings::join(a, b), "[[1, 2, 3], [4, 5, 6]]");
  EXPECT_EQ(strings::join<join_opt::flat>(a, b), "1, 2, 3, 4, 5, 6");
  EXPECT_EQ((strings::join<join_opt::braced, '(', ')'>(a, b)),
      "([1, 2, 3], [4, 5, 6])");
  EXPECT_EQ((strings::join<join_opt::prefixed, '{', '}'>(a, b)),
      ", {[1, 2, 3], [4, 5, 6]}");
  EXPECT_EQ(strings::join<join_opt::prefixed>(a, b),
      ", [[1, 2, 3], [4, 5, 6]]");
}

void StringUtilsTest_Streams() {
  using strings::join_opt;

  std::vector<int> a{1, 2, 3};
  std::vector<int> b{4, 5, 6};

  if (true) {
    std::stringstream s;
    strings::append_join(s, a, b);
    EXPECT_EQ(s.str(), "[[1, 2, 3], [4, 5, 6]]");
  }
}

enum class rgb {
  black,      // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};

template<>
constexpr auto registry::enum_spec_v<rgb> =
    make_bitmask_enum_spec<rgb, "red,green,blue">();

void StringUtilsTest_AppendEnum() {
  std::string s;
  EXPECT_EQ((strings::concat(rgb::yellow)), "red + green");
  EXPECT_EQ((strings::join(rgb::yellow, rgb::cyan)),
      "[red + green, green + blue]");
  s = (strings::join<strings::join_opt::braced, -1, -1>(rgb::yellow,
      rgb::cyan));
  EXPECT_EQ(s, "red + green, green + blue");
}

enum class marine_rank {
  Civilian,
  Private,
  PrivateFirstClass,
  LanceCorporal,
  Sergeant,
  StaffSergeant,
  GunnerySergeant,
  MasterSergeant,
  FirstSergeant,
  MasterGunnerySergeant,
  SergeantMajor,
  SergeantMajorOfTheMarineCorps
};

template<>
constexpr auto registry::enum_spec_v<marine_rank> =
    make_sequence_enum_spec<marine_rank, wrapclip::limit>({"Civilian",
        "Private", "PrivateFirstClass", "LanceCorporal", "Sergeant",
        "StaffSergeant", "GunnerySergeant", "MasterSergeant", "FirstSergeant",
        "MasterGunnerySergeant", "SergeantMajor",
        "SergeantMajorOfTheMarineCorps"});

struct soldier {
  std::string name;
  marine_rank rank;
  int64_t serial_number;

  friend std::ostream& operator<<(std::ostream& os, const soldier& s) {
    return os << "[" << s.name << ", " << s.rank << ", " << s.serial_number
              << "]";
  }
};

template<>
constexpr bool strings::stream_append_v<soldier> = true;

struct person {
  std::string last;
  std::string first;

  template<AppendTarget A>
  static auto& append(A& target, const person& p) {
    return corvid::strings::append(target, p.last, ", ", p.first);
  }

  template<auto opt = strings::join_opt::braced, char open = 0, char close = 0,
      AppendTarget A>
  static A& append_join_with(A& target, strings::delim d, const person& p) {
    constexpr auto is_json = strings::decode::json_v<opt>;
    constexpr char next_open = open ? open : (is_json ? '[' : 0);
    constexpr char next_close = close ? close : (is_json ? ']' : 0);
    return corvid::strings::append_join_with<opt, next_open, next_close>(
        target, d, p.last, p.first);
  }
};

template<AppendTarget A>
constexpr auto strings::append_override_fn<A, person> = person::append<A>;

template<strings::join_opt opt, char open, char close, AppendTarget A>
constexpr auto strings::append_join_override_fn<opt, open, close, A, person> =
    person::append_join_with<opt, open, close, A>;

void StringUtilsTest_AppendStream() {
  using strings::join_opt;
  soldier pyle{"Gomer", marine_rank::Private, 12345678};
  soldier carter{"Vince", marine_rank::GunnerySergeant, 23456789};
  person doe{"Doe", "John"};
  if (true) {
    std::stringstream os;
    os << pyle;
    EXPECT_EQ(os.str(), "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::stringstream os;
    strings::append_stream(os, pyle);
    EXPECT_EQ(os.str(), "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append_stream(s, pyle);
    EXPECT_EQ(s, "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append(s, pyle);
    EXPECT_EQ(s, "[Gomer, Private, 12345678]");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, pyle);
    EXPECT_EQ(s, "[Gomer, Private, 12345678]");
  }
  if (true) {
    auto v = std::vector{std::make_pair(pyle, 30'000.15),
        std::make_pair(carter, 40'000.21)};
    std::string s;
    strings::append_join<join_opt::keyed>(s, v);
    EXPECT_EQ(s,
        "[{[Gomer, Private, 12345678], 30000.15}, {[Vince, GunnerySergeant, "
        "23456789], 40000.21}]");

    s.clear();
    strings::append_join<strings::join_opt::json>(s, v);
    EXPECT_EQ(s,
        R"({"[Gomer, Private, 12345678]": 30000.15, "[Vince, GunnerySergeant, 23456789]": 40000.21})");
  }
  if (true) {
    std::string s;
    strings::append(s, doe);
    EXPECT_EQ(s, "Doe, John");

    s.clear();
    strings::append_join(s, doe);
    EXPECT_EQ(s, "[Doe, John]");
  }
  if (true) {
    std::string s;
    strings::append_join_with<strings::join_opt::flat>(s, "; ", doe);
    EXPECT_EQ(s, "Doe; John");
  }
  if (true) {
    std::string s;
    strings::append_join_with<strings::join_opt::quoted>(s, "; ", doe);
    EXPECT_EQ(s, R"(["Doe"; "John"])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, doe);
    EXPECT_EQ(s, R"(["Doe", "John"])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::json>(s, doe, doe, doe);
    EXPECT_EQ(s, R"([["Doe", "John"], ["Doe", "John"], ["Doe", "John"]])");
  }
  if (true) {
    std::string s;
    strings::append_join<strings::join_opt::flat>(s, doe, doe, doe);
    EXPECT_EQ(s, "Doe, John, Doe, John, Doe, John");
  }
}

// TODO: Add a test for printing a null cstring_view.

MAKE_TEST_LIST(StringUtilsTest_ExtractPiece, StringUtilsTest_MorePieces,
    StringUtilsTest_Split, StringUtilsTest_ParseNum, StringUtilsTest_Case,
    StringUtilsTest_Replace, StringUtilsTest_Target, StringUtilsTest_Print,
    StringUtilsTest_Trim, StringUtilsTest_AppendNum, StringUtilsTest_Append,
    StringUtilsTest_Edges, StringUtilsTest_Streams, StringUtilsTest_AppendEnum,
    StringUtilsTest_AppendStream);
