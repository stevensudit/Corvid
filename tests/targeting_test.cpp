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
#include <ostream>
#include <sstream>
#include <string>

#include "../corvid/strings/core/targeting.h"

#include "catch2_main.h"

using namespace std::literals;
using namespace corvid::strings;

#pragma region AppenderString

TEST_CASE("AppenderString", "[targeting]") {
  SECTION("char") {
    std::string s;
    appender{s}.append("ab"sv).append('c').append(size_t{2}, 'd');
    CHECK(s == "abcdd");
  }
  SECTION("char16_t") {
    std::u16string s;
    appender{s}.append(u"ab").append(u'c').append(size_t{2}, u'd');
    CHECK(s == u"abcdd");
  }
  SECTION("char32_t") {
    std::u32string s;
    appender{s}.append(U"xy").append(U'z');
    CHECK(s == U"xyz");
  }
  SECTION("two-arg pointer append") {
    std::u16string s;
    const char16_t buf[] = {u'h', u'i', u'!'};
    appender{s}.append(buf, 2);
    CHECK(s == u"hi");
  }
}

#pragma endregion
#pragma region AppenderStream

TEST_CASE("AppenderStream", "[targeting]") {
  SECTION("char") {
    std::ostringstream os;
    appender{os}.append("ab"sv).append('c');
    CHECK(os.str() == "abc");
  }
  SECTION("wchar_t") {
    std::wostringstream os;
    appender{os}.append(L"ab").append(L'c');
    CHECK(os.str() == L"abc");
  }
}

#pragma endregion
