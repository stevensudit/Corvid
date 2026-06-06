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

#include <format>
#include <map>
#include <vector>

#include "../corvid/strings/core/cstring_view.h"
#include "../corvid/strings/core/opt_string_view.h"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::literals;

TEST_CASE("Wrapper formats like its string view", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{}", "hi"_csv) == "hi");
    CHECK(std::format("{}", "hi"_osv) == "hi");
    CHECK(std::format("{}", ""_osv).empty());
  }
}

TEST_CASE("Wrapper honors the debug spec", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{:?}", "hi"_csv) == R"("hi")");
    CHECK(std::format("{:?}", "a\tb"_osv) == R"("a\tb")");
    CHECK(std::format("{:?}", ""_osv) == R"("")");
  }
}

TEST_CASE("Wrapper honors fill, align, and width", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{:>5}", "hi"_csv) == "   hi");
    CHECK(std::format("{:*^6}", "hi"_csv) == "**hi**");
    // Width composes with the debug spec for non-null content.
    CHECK(std::format("{:<8?}", "hi"_csv) == R"("hi"    )");
  }
}

TEST_CASE("Null is transparent under {} and marked under {:?}",
    "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{}", 0_osv).empty());
    CHECK(std::format("{:?}", 0_osv) == "(null)");
    // Width applies to a transparent null, padding the empty rendering.
    CHECK(std::format("{:>3}", 0_osv) == "   ");
  }
}

TEST_CASE("Null marker honors fill, align, and width",
    "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{:>10?}", 0_osv) == "    (null)");
    CHECK(std::format("{:10?}", 0_osv) == "(null)    ");
    CHECK(std::format("{:*^10?}", 0_osv) == "**(null)**");
    // A dynamic width binds an arg to the real context, so the marker falls
    // back to unpadded rather than reading the wrong argument.
    CHECK(std::format("{:{}?}", 0_osv, 10) == "(null)");
  }
}

TEST_CASE("Wrapper composes inside std range and map",
    "[StringViewWrapperTest]") {
  if (true) {
    // format_kind is disabled for the children, so they format as strings
    // rather than lists of chars, and the range formatter quotes them.
    std::vector<cstring_view> v{"a"_csv, "b"_csv};
    CHECK(std::format("{}", v) == R"(["a", "b"])");

    // A null element inside a range still shows the marker, driven by the
    // range formatter's set_debug_format rather than a spec `?`.
    std::vector<opt_string_view> nv{opt_string_view{"a"}, 0_osv};
    CHECK(std::format("{}", nv) == R"(["a", (null)])");

    std::map<int, cstring_view> m{{1, "x"_csv}};
    CHECK(std::format("{}", m) == R"({1: "x"})");
  }
}

TEST_CASE("Wide formatting widens the content", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format(L"{}", L"hi"_wcsv) == L"hi");
    CHECK(std::format(L"{:?}", L"hi"_wcsv) == LR"("hi")");
    opt_wstring_view wnull{};
    CHECK(std::format(L"{:?}", wnull) == L"(null)");
    CHECK(std::format(L"{:>10?}", wnull) == L"    (null)");
  }
}
