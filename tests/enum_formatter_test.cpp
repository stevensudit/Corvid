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

#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "../corvid/enums.h"
#include "../corvid/strings/format/enum_formatter.h"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::enums;
using namespace corvid::enums::sequence;
using namespace corvid::enums::bitmask;

// A registered sequence enum: formats by name.
enum class hue : std::uint8_t { red, green, blue };
consteval auto corvid_enum_spec(hue*) {
  return make_sequence_enum_spec<hue, "red,green,blue">();
}

// A registered bitmask enum: formats as its "a + b + c" combination.
enum class rgb : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  white = 7   // rgb
};
consteval auto corvid_enum_spec(rgb*) {
  return make_bitmask_enum_spec<rgb, "red,green,blue">();
}

// An unregistered scoped enum: formats as its numeric underlying value.
enum class plain : std::int8_t { zero, one, two };

// A sequence enum whose names contain characters that the debug spec escapes:
// an embedded quote, backslash, and tab (internal, so trimming keeps it).
enum class weird : std::uint8_t { norm, quote, slash, tab };
consteval auto corvid_enum_spec(weird*) {
  return make_sequence_enum_spec<weird, "ok,q\"x,b\\y,a\tb">();
}

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Sequence enum formats by name", "[EnumFormatterTest]") {
  if (true) {
    CHECK(std::format("{}", hue::red) == "red");
    CHECK(std::format("{}", hue::blue) == "blue");
  }
}

TEST_CASE("Bitmask enum formats as combination", "[EnumFormatterTest]") {
  if (true) {
    CHECK(std::format("{}", rgb::red) == "red");
    CHECK(std::format("{}", rgb::yellow) == "red + green");
    CHECK(std::format("{}", rgb::white) == "red + green + blue");
    CHECK(std::format("{}", rgb::black) == "0x00");
  }
}

TEST_CASE("Unregistered scoped enum formats numerically",
    "[EnumFormatterTest]") {
  if (true) {
    CHECK(std::format("{}", plain::zero) == "0");
    CHECK(std::format("{}", plain::two) == "2");
  }
}

TEST_CASE("Wide formatting widens the name", "[EnumFormatterTest]") {
  if (true) {
    CHECK(std::format(L"{}", hue::green) == L"green");
    CHECK(std::format(L"{}", rgb::yellow) == L"red + green");
    CHECK(std::format(L"{}", plain::one) == L"1");
  }
}

TEST_CASE("Debug spec quotes the rendering", "[EnumFormatterTest]") {
  if (true) {
    CHECK(std::format("{:?}", hue::red) == R"("red")");
    CHECK(std::format("{:?}", rgb::yellow) == R"("red + green")");
    CHECK(std::format("{:?}", rgb::black) == R"("0x00")");
    CHECK(std::format("{:?}", plain::two) == R"("2")");
  }
}

TEST_CASE("Debug spec escapes special characters", "[EnumFormatterTest]") {
  if (true) {
    CHECK(std::format("{:?}", weird::quote) == R"("q\"x")");
    CHECK(std::format("{:?}", weird::slash) == R"("b\\y")");
    CHECK(std::format("{:?}", weird::tab) == R"("a\tb")");
    CHECK(std::format(L"{:?}", weird::quote) == LR"("q\"x")");
  }
}

TEST_CASE("Rejects unsupported format specs", "[EnumFormatterTest]") {
  if (true) {
    // A bad spec in a literal format string is a compile error, so the throw
    // path is reachable only through a runtime format string. The only
    // accepted specs are the empty spec and `?`.
    hue h = hue::red;
    CHECK(std::vformat("{:?}", std::make_format_args(h)) == R"("red")");
    CHECK_THROWS_AS(std::vformat("{:0?}", std::make_format_args(h)),
        std::format_error);
    CHECK_THROWS_AS(std::vformat("{:x}", std::make_format_args(h)),
        std::format_error);
    CHECK_THROWS_AS(std::vformat("{:?x}", std::make_format_args(h)),
        std::format_error);
  }
}

TEST_CASE("Composes inside std range and map", "[EnumFormatterTest]") {
  if (true) {
    // The range and map formatters auto-enable debug on elements that have
    // set_debug_format, so enums quote the same way std strings do.
    std::vector<hue> v{hue::red, hue::green};
    CHECK(std::format("{}", v) == R"(["red", "green"])");
    CHECK(std::format("{::?}", v) == R"(["red", "green"])");
    std::map<int, hue> m{{1, hue::red}, {2, hue::blue}};
    CHECK(std::format("{}", m) == R"({1: "red", 2: "blue"})");
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
