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
#include <string>

#include "../corvid/enums/sequence_enum.h"
#include "../corvid/enums/bitmask_enum.h"
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

TEST_CASE("String format spec is honored", "[EnumFormatterTest]") {
  if (true) {
    // Fill, align, and width come from the inherited string formatter.
    CHECK(std::format("{:>6}", hue::red) == "   red");
    CHECK(std::format("{:*<6}", hue::red) == "red***");
  }
}

TEST_CASE("Wide formatting widens the name", "[EnumFormatterTest]") {
  if (true) {
    CHECK(std::format(L"{}", hue::green) == L"green");
    CHECK(std::format(L"{}", rgb::yellow) == L"red + green");
    CHECK(std::format(L"{}", plain::one) == L"1");
  }
}
