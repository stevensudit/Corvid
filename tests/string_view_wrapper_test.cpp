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

namespace {

// A null wrapper has null `data`; an empty one has non-null `data` and zero
// size.
constexpr opt_string_view null_osv{};
constexpr opt_string_view empty_osv{""};

} // namespace

TEST_CASE("Wrapper formats like its string view", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{}", cstring_view{"hi"}) == "hi");
    CHECK(std::format("{}", opt_string_view{"hi"}) == "hi");
    CHECK(std::format("{}", empty_osv).empty());
  }
}

TEST_CASE("Wrapper honors the debug spec", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{:?}", cstring_view{"hi"}) == R"("hi")");
    CHECK(std::format("{:?}", opt_string_view{"a\tb"}) == R"("a\tb")");
    CHECK(std::format("{:?}", empty_osv) == R"("")");
  }
}

TEST_CASE("Wrapper honors fill, align, and width", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{:>5}", cstring_view{"hi"}) == "   hi");
    CHECK(std::format("{:*^6}", cstring_view{"hi"}) == "**hi**");
    // Width composes with the debug spec for non-null content.
    CHECK(std::format("{:<8?}", cstring_view{"hi"}) == R"("hi"    )");
  }
}

TEST_CASE("Null is transparent under {} and marked under {:?}",
    "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{}", null_osv).empty());
    CHECK(std::format("{:?}", null_osv) == "(null)");
    // Width applies to a transparent null, padding the empty rendering.
    CHECK(std::format("{:>3}", null_osv) == "   ");
  }
}

TEST_CASE("Wrapper composes inside std range and map",
    "[StringViewWrapperTest]") {
  if (true) {
    // format_kind is disabled for the children, so they format as strings
    // rather than lists of chars, and the range formatter quotes them.
    std::vector<cstring_view> v{cstring_view{"a"}, cstring_view{"b"}};
    CHECK(std::format("{}", v) == R"(["a", "b"])");

    // A null element inside a range still shows the marker, driven by the
    // range formatter's set_debug_format rather than a spec `?`.
    std::vector<opt_string_view> nv{opt_string_view{"a"}, null_osv};
    CHECK(std::format("{}", nv) == R"(["a", (null)])");

    std::map<int, cstring_view> m{{1, cstring_view{"x"}}};
    CHECK(std::format("{}", m) == R"({1: "x"})");
  }
}

TEST_CASE("Wide formatting widens the content", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format(L"{}", wcstring_view{L"hi"}) == L"hi");
    CHECK(std::format(L"{:?}", wcstring_view{L"hi"}) == LR"("hi")");
    basic_opt_string_view<void, wchar_t> wnull{};
    CHECK(std::format(L"{:?}", wnull) == L"(null)");
  }
}
