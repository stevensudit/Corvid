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
#include <map>
#include <set>
#include <string_view>

#include "../corvid/meta.h"
#include "../corvid/enums.h"
#include "../corvid/enums/enum_conversion.h"
#include "../corvid/containers.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::enums::bitmask;

// NOLINTBEGIN(readability-function-cognitive-complexity)

enum class rgb : std::int8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};
consteval auto corvid_enum_spec(rgb*) {
  return make_bitmask_enum_spec<rgb, "red,green,blue">();
}

// Same thing, but safe due to clipping.
enum class safe_rgb : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};
consteval auto corvid_enum_spec(safe_rgb*) {
  return make_bitmask_enum_values_spec<safe_rgb,
      "black,blue,green,cyan,red,purple,yellow,white", wrapclip::limit>();
}

// This is not a bitmask class, so it shouldn't work as a bitmap.
enum class tires : std::uint8_t { none, one, two, three, four, five, six };

#pragma region Ops

TEST_CASE("Ops", "[BitMaskTest]") {
  if (true) {
    CHECK(!rgb{});
    CHECK(valid_bits_v<rgb> == 7);
    CHECK(max_value<rgb>() == rgb::white);
    CHECK_FALSE(bit_clip_v<rgb>);
    CHECK(bits_length<rgb>() == 3U);
  }
  if (true) {
    // Does not compile.
    // * auto bad = *tires::none;
    // * auto worse = tires::none | tires::one;

    CHECK(valid_bits_v<rgb> == 7);
    CHECK(max_value<rgb>() == rgb::white);
    CHECK_FALSE(bit_clip_v<rgb>);

    CHECK(valid_bits_v<safe_rgb> == 7);
    CHECK(max_value<safe_rgb>() == safe_rgb::white);
    CHECK(bit_clip_v<safe_rgb>);

    auto c = rgb::red;
    CHECK(c != rgb::green);

    CHECK((rgb::red | rgb::green) == rgb::yellow);
    CHECK((rgb::red | rgb::red) == rgb::red);
    CHECK((rgb::yellow & rgb::green) == rgb::green);
    CHECK((rgb::red & rgb::green) == rgb::black);
    CHECK((rgb::red ^ rgb::green) == rgb::yellow);
    CHECK((rgb::yellow ^ rgb::green) == rgb::red);
    CHECK(~rgb::white != rgb::black);
    CHECK((~rgb::white & rgb::white) == rgb::black);
    CHECK((~rgb::black & rgb::white) == rgb::white);

    c = rgb::yellow;
    CHECK((c &= rgb::green) == rgb::green);
    CHECK(c == rgb::green);
    CHECK((c |= rgb::red) == rgb::yellow);
    CHECK(c == rgb::yellow);
    CHECK((c ^= rgb::cyan) == rgb::purple);
    CHECK(c == rgb::purple);

    c = rgb::red;
    c += rgb::green;
    CHECK(c == rgb::yellow);
    c -= rgb::red;
    CHECK(c == rgb::green);
  }
}
#pragma endregion

#pragma region NamedFunctions

TEST_CASE("NamedFunctions", "[BitMaskTest]") {
  if (true) {
    // Does not compile.
    // * auto bad = make<int>(0);
    // * auto worse = set(tires::none, tires::one);

    CHECK(make<rgb>(1) == rgb::blue);
    CHECK(make<rgb>(-1) != rgb::white);
    CHECK(make_safely<rgb>(-1) == rgb::white);

    CHECK(set(rgb::red, rgb::blue) == rgb::purple);
    CHECK(set_if(rgb::red, rgb::blue, true) == rgb::purple);
    CHECK(set_if(rgb::red, rgb::blue, false) == rgb::red);

    CHECK(clear(rgb::purple, rgb::blue) == rgb::red);
    CHECK(clear_if(rgb::purple, rgb::blue, true) == rgb::red);
    CHECK(clear_if(rgb::purple, rgb::blue, false) == rgb::purple);

    CHECK(flip(rgb::white) == rgb::black);

    CHECK(has(rgb::purple, rgb::blue));
    CHECK(has(rgb::purple, rgb::white));
    CHECK_FALSE(has(rgb::purple, rgb::green));
    CHECK_FALSE(has(rgb::purple, rgb::black));

    CHECK(has_all(rgb::purple, rgb::blue));
    CHECK_FALSE(has_all(rgb::purple, rgb::white));
    CHECK_FALSE(has_all(rgb::purple, rgb::green));
    CHECK(has_all(rgb::purple, rgb::black));

    CHECK(set_at(rgb::black, 1) == rgb::blue);
    CHECK(set_at(rgb::black, 2) == rgb::green);
    CHECK(set_at_if(rgb::black, 3, true) == rgb::red);
    CHECK(set_at_if(rgb::black, 3, false) == rgb::black);
    CHECK(clear_at(rgb::white, 1) == rgb::yellow);
    CHECK(clear_at_if(rgb::white, 1, true) == rgb::yellow);
    CHECK(clear_at_if(rgb::white, 2, false) == rgb::white);
    CHECK(set_at_to(rgb::black, 1, true) == rgb::blue);
    CHECK(set_at_to(rgb::white, 2, false) == rgb::purple);

    size_t c{};
    size_t s{};
    for (auto e : make_interval<rgb>()) {
      ++c;
      s += *e;
    }
    CHECK(c == 8U);
    CHECK(s == 28U);
  }
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(rgb::red) == "red");
    CHECK(enum_as_string(rgb::green) == "green");
    CHECK(enum_as_string(rgb::blue) == "blue");
    CHECK(enum_as_string(rgb::black) == "0x00");
    CHECK(enum_as_string(rgb::yellow) == "red + green");
    CHECK(enum_as_string(rgb::purple) == "red + blue");
    CHECK(enum_as_string(rgb::cyan) == "green + blue");
    CHECK(enum_as_string(rgb::white) == "red + green + blue");
    CHECK(enum_as_string(rgb(0x40)) == "0x40");
    CHECK(enum_as_string(rgb(7 + 0x40)) == "red + green + blue + 0x40");
  }
}
#pragma endregion

#pragma region SafeOps

TEST_CASE("SafeOps", "[BitMaskTest]") {
  if (true) {
    CHECK(valid_bits_v<safe_rgb> == 7);
    CHECK(max_value<safe_rgb>() == safe_rgb::white);
    CHECK(bit_clip_v<safe_rgb>);
    CHECK(bits_length<safe_rgb>() == 3U);
  }
  if (true) {
    auto c = safe_rgb::red;
    CHECK(c != safe_rgb::green);

    CHECK((safe_rgb::red | safe_rgb::green) == safe_rgb::yellow);
    CHECK((safe_rgb::red | safe_rgb::red) == safe_rgb::red);
    CHECK((safe_rgb::yellow & safe_rgb::green) == safe_rgb::green);
    CHECK((safe_rgb::red & safe_rgb::green) == safe_rgb::black);
    CHECK((safe_rgb::red ^ safe_rgb::green) == safe_rgb::yellow);
    CHECK((safe_rgb::yellow ^ safe_rgb::green) == safe_rgb::red);
    CHECK(~safe_rgb::white == safe_rgb::black);
    CHECK((~safe_rgb::white & safe_rgb::white) == safe_rgb::black);
    CHECK((~safe_rgb::black & safe_rgb::white) == safe_rgb::white);
    CHECK(~safe_rgb::black == safe_rgb::white);

    c = safe_rgb::yellow;
    CHECK((c &= safe_rgb::green) == safe_rgb::green);
    CHECK(c == safe_rgb::green);
    CHECK((c |= safe_rgb::red) == safe_rgb::yellow);
    CHECK(c == safe_rgb::yellow);
    CHECK((c ^= safe_rgb::cyan) == safe_rgb::purple);
    CHECK(c == safe_rgb::purple);

    c = safe_rgb::red;
    c += safe_rgb::green;
    CHECK(c == safe_rgb::yellow);
    c -= safe_rgb::red;
    CHECK(c == safe_rgb::green);
  }
}
#pragma endregion

#pragma region SafeNamedFunctions

TEST_CASE("SafeNamedFunctions", "[BitMaskTest]") {
  if (true) {
    CHECK(make<safe_rgb>(1) == safe_rgb::blue);
    CHECK(make<safe_rgb>(-1) == safe_rgb::white);
    CHECK(make_safely<safe_rgb>(-1) == safe_rgb::white);

    CHECK(set(safe_rgb::red, safe_rgb::blue) == safe_rgb::purple);
    CHECK(clear(safe_rgb::purple, safe_rgb::blue) == safe_rgb::red);
    CHECK(flip(safe_rgb::white) == safe_rgb::black);

    CHECK(has(safe_rgb::purple, safe_rgb::blue));
    CHECK(has(safe_rgb::purple, safe_rgb::white));
    CHECK_FALSE(has(safe_rgb::purple, safe_rgb::green));
    CHECK_FALSE(has(safe_rgb::purple, safe_rgb::black));

    CHECK(has_all(safe_rgb::purple, safe_rgb::blue));
    CHECK_FALSE(has_all(safe_rgb::purple, safe_rgb::white));
    CHECK_FALSE(has_all(safe_rgb::purple, safe_rgb::green));
    CHECK(has_all(safe_rgb::purple, safe_rgb::black));

    size_t c{};
    size_t s{};
    for (auto e : make_interval<safe_rgb>()) {
      ++c;
      s += *e;
    }
    CHECK(c == 8U);
    CHECK(s == 28U);
  }
  if (true) {
    using namespace strings;
    CHECK(enum_as_string(safe_rgb::black) == "black");
    CHECK(enum_as_string(safe_rgb::red) == "red");
    CHECK(enum_as_string(safe_rgb::green) == "green");
    CHECK(enum_as_string(safe_rgb::blue) == "blue");
    CHECK(enum_as_string(safe_rgb::yellow) == "yellow");
    CHECK(enum_as_string(safe_rgb::purple) == "purple");
    CHECK(enum_as_string(safe_rgb::cyan) == "cyan");
    CHECK(enum_as_string(safe_rgb::white) == "white");
    CHECK(enum_as_string(safe_rgb(0x40)) == "black + 0x40");
    CHECK(enum_as_string(safe_rgb(7 + 0x40)) == "white + 0x40");
  }
}
#pragma endregion

enum class rgb_unnamed : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};
consteval auto corvid_enum_spec(rgb_unnamed*) {
  return make_bitmask_enum_spec<rgb_unnamed, rgb_unnamed::white,
      wrapclip::limit>();
}

enum class patchy_rgb : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};
consteval auto corvid_enum_spec(patchy_rgb*) {
  return make_bitmask_enum_values_spec<patchy_rgb,
      "-,blue,green,-,red,purple,-,white">();
}

#pragma region MoreNamingTests

TEST_CASE("MoreNamingTests", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<rgb_unnamed> == 7);
    CHECK(max_value<rgb_unnamed>() == rgb_unnamed::white);
    CHECK(bit_clip_v<rgb_unnamed>);
    CHECK(bits_length<rgb_unnamed>() == 3U);
  }
  if (true) {
    CHECK(enum_as_string(rgb_unnamed(0x00)) == "0x00");
    CHECK(enum_as_string(rgb_unnamed(0x01)) == "0x01");
    CHECK(enum_as_string(rgb_unnamed(0x02)) == "0x02");
    CHECK(enum_as_string(rgb_unnamed(0x03)) == "0x03");
    CHECK(enum_as_string(rgb_unnamed(0x04)) == "0x04");
    CHECK(enum_as_string(rgb_unnamed(0x05)) == "0x05");
    CHECK(enum_as_string(rgb_unnamed(0x06)) == "0x06");
    CHECK(enum_as_string(rgb_unnamed(0x07)) == "0x07");
    CHECK(enum_as_string(rgb_unnamed(0x40)) == "0x40");
    CHECK(enum_as_string(rgb_unnamed(0x7 + 0x40)) == "0x47");
  }
  if (true) {
    CHECK(valid_bits_v<patchy_rgb> == 7);
    CHECK(max_value<patchy_rgb>() == patchy_rgb::white);
    CHECK_FALSE(bit_clip_v<patchy_rgb>);
    CHECK(bits_length<patchy_rgb>() == 3U);
  }
  if (true) {
    CHECK(enum_as_string(patchy_rgb::black) == "0x00");
    CHECK(enum_as_string(patchy_rgb::red) == "red");
    CHECK(enum_as_string(patchy_rgb::green) == "green");
    CHECK(enum_as_string(patchy_rgb::blue) == "blue");
    CHECK(enum_as_string(patchy_rgb::yellow) == "red + green");
    CHECK(enum_as_string(patchy_rgb::purple) == "purple");
    CHECK(enum_as_string(patchy_rgb::cyan) == "green + blue");
    CHECK(enum_as_string(patchy_rgb::white) == "white");
    CHECK(enum_as_string(patchy_rgb(0x40)) == "0x40");
    CHECK(enum_as_string(patchy_rgb(7 + 0x40)) == "white + 0x40");
  }
}
#pragma endregion

#pragma region StreamingOut

TEST_CASE("StreamingOut", "[BitMaskTest]") {
  CHECK(OStreamable<rgb>);
  if (true) {
    std::stringstream ss;
    int i = *rgb::red;
    ss << i << std::flush;
    CHECK(ss.str() == "4");
  }
  if (true) {
    std::stringstream ss;
    ss << rgb::red << std::flush;
    CHECK(ss.str() == "red");
  }
}
#pragma endregion

// RGB, but without the G.
enum class rb : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  blue = 1,   // --b
  purple = 5, // r-b
};
consteval auto corvid_enum_spec(rb*) {
  return make_bitmask_enum_spec<rb, "red, , blue">();
}

#pragma region NoGreen

TEST_CASE("NoGreen", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<rb> == 5);
    CHECK(max_value<rb>() == rb::purple);
    CHECK_FALSE(bit_clip_v<rb>);
    CHECK(bits_length<rb>() == 3U);
  }
  if (true) {
    CHECK(enum_as_string(rb(0x00)) == "0x00");
    CHECK(enum_as_string(rb(0x01)) == "blue");
    CHECK(enum_as_string(rb(0x02)) == "0x02");
    CHECK(enum_as_string(rb(0x03)) == "blue + 0x02");
    CHECK(enum_as_string(rb(0x04)) == "red");
    CHECK(enum_as_string(rb(0x05)) == "red + blue");
    CHECK(enum_as_string(rb(0x06)) == "red + 0x02");
    CHECK(enum_as_string(rb(0x07)) == "red + blue + 0x02");
    CHECK(enum_as_string(rb(0x40)) == "0x40");
    CHECK(enum_as_string(rb(0x7 + 0x40)) == "red + blue + 0x42");
  }
}
#pragma endregion

// RGB, but without the B.
enum class rg : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  yellow = 6, // rg-
};
consteval auto corvid_enum_spec(rg*) {
  return make_bitmask_enum_spec<rg, "red, green, -">();
}

#pragma region NoBlue

TEST_CASE("NoBlue", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<rg> == 6);
    CHECK(max_value<rg>() == rg::yellow);
    CHECK_FALSE(bit_clip_v<rg>);
    CHECK(bits_length<rg>() == 3U);
  }
  if (true) {
    CHECK(enum_as_string(rg(0x00)) == "0x00");
    CHECK(enum_as_string(rg(0x01)) == "0x01");
    CHECK(enum_as_string(rg(0x02)) == "green");
    CHECK(enum_as_string(rg(0x03)) == "green + 0x01");
    CHECK(enum_as_string(rg(0x04)) == "red");
    CHECK(enum_as_string(rg(0x05)) == "red + 0x01");
    CHECK(enum_as_string(rg(0x06)) == "red + green");
    CHECK(enum_as_string(rg(0x07)) == "red + green + 0x01");
    CHECK(enum_as_string(rg(0x40)) == "0x40");
    CHECK(enum_as_string(rg(0x7 + 0x40)) == "red + green + 0x41");
  }
}
#pragma endregion

// RGB, but without the R.
enum class gb : std::uint8_t {
  black = 0,  // ---
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 3, // -gb
};

// Note: A leading comma here would trigger a static assert, whether or not a
// hyphen was prefixed. In contrast, we would allow a leading wildcard
// placeholder
consteval auto corvid_enum_spec(gb*) {
  return make_bitmask_enum_spec<gb, "green,blue">();
}

#pragma region NoRed

TEST_CASE("NoRed", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<gb> == 3);
    CHECK(max_value<gb>() == gb::yellow);
    CHECK_FALSE(bit_clip_v<gb>);
    CHECK(bits_length<gb>() == 2U);
  }
  if (true) {
    CHECK(enum_as_string(gb(0x00)) == "0x00");
    CHECK(enum_as_string(gb(0x01)) == "blue");
    CHECK(enum_as_string(gb(0x02)) == "green");
    CHECK(enum_as_string(gb(0x03)) == "green + blue");
    CHECK(enum_as_string(gb(0x04)) == "0x04");
    CHECK(enum_as_string(gb(0x05)) == "blue + 0x04");
    CHECK(enum_as_string(gb(0x06)) == "green + 0x04");
    CHECK(enum_as_string(gb(0x07)) == "green + blue + 0x04");
    CHECK(enum_as_string(gb(0x40)) == "0x40");
    CHECK(enum_as_string(gb(0x7 + 0x40)) == "green + blue + 0x44");
  }
}
#pragma endregion

// Same thing, but safe due to clipping.
enum class safe_rb : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  blue = 1,   // --b
  purple = 5, // r-b
};
consteval auto corvid_enum_spec(safe_rb*) {
  return make_bitmask_enum_values_spec<safe_rb, "black,blue,,,red,purple,,",
      wrapclip::limit>();
}

#pragma region SafeNoGreen

TEST_CASE("SafeNoGreen", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<safe_rb> == 5);
    CHECK(max_value<safe_rb>() == safe_rb::purple);
    CHECK(bit_clip_v<safe_rb>);
    CHECK(bits_length<safe_rb>() == 3U);
  }
  if (true) {
    CHECK(enum_as_string(safe_rb(0x00)) == "black");
    CHECK(enum_as_string(safe_rb(0x01)) == "blue");
    CHECK(enum_as_string(safe_rb(0x02)) == "black + 0x02");
    CHECK(enum_as_string(safe_rb(0x03)) == "blue + 0x02");
    CHECK(enum_as_string(safe_rb(0x04)) == "red");
    CHECK(enum_as_string(safe_rb(0x05)) == "purple");
    CHECK(enum_as_string(safe_rb(0x06)) == "red + 0x02");
    CHECK(enum_as_string(safe_rb(0x07)) == "purple + 0x02");
    CHECK(enum_as_string(safe_rb(0x40)) == "black + 0x40");
    CHECK(enum_as_string(safe_rb(0x7 + 0x40)) == "purple + 0x42");
  }
}
#pragma endregion

// Same thing, but safe due to clipping.
enum class safe_rg : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  yellow = 6, // rg-
};
consteval auto corvid_enum_spec(safe_rg*) {
  return make_bitmask_enum_values_spec<safe_rg, "black,,green,,red,,yellow,",
      wrapclip::limit>();
}

#pragma region SafeNoBlue

TEST_CASE("SafeNoBlue", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<safe_rg> == 6);
    CHECK(max_value<safe_rg>() == safe_rg::yellow);
    CHECK(bit_clip_v<safe_rg>);
    CHECK(bits_length<safe_rg>() == 3U);
  }
  if (true) {
    CHECK(enum_as_string(safe_rg(0x00)) == "black");
    CHECK(enum_as_string(safe_rg(0x01)) == "black + 0x01");
    CHECK(enum_as_string(safe_rg(0x02)) == "green");
    CHECK(enum_as_string(safe_rg(0x03)) == "green + 0x01");
    CHECK(enum_as_string(safe_rg(0x04)) == "red");
    CHECK(enum_as_string(safe_rg(0x05)) == "red + 0x01");
    CHECK(enum_as_string(safe_rg(0x06)) == "yellow");
    CHECK(enum_as_string(safe_rg(0x07)) == "yellow + 0x01");
    CHECK(enum_as_string(safe_rg(0x40)) == "black + 0x40");
    CHECK(enum_as_string(safe_rg(0x7 + 0x40)) == "yellow + 0x41");
  }
}
#pragma endregion

// Same thing, but safe due to clipping.
enum class safe_gb : std::uint8_t {
  black = 0, // ---
  green = 2, // -g-
  blue = 1,  // --b
  cyan = 3,  // -gb
};
consteval auto corvid_enum_spec(safe_gb*) {
  return make_bitmask_enum_values_spec<safe_gb, "black,blue,green,cyan,,,,",
      wrapclip::limit>();
}

#pragma region SafeNoRed

TEST_CASE("SafeNoRed", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<safe_gb> == 3);
    CHECK(max_value<safe_gb>() == safe_gb::cyan);
    CHECK(bit_clip_v<safe_gb>);
    CHECK(bits_length<safe_gb>() == 2U);
  }
  if (true) {
    CHECK(enum_as_string(safe_gb(0x00)) == "black");
    CHECK(enum_as_string(safe_gb(0x01)) == "blue");
    CHECK(enum_as_string(safe_gb(0x02)) == "green");
    CHECK(enum_as_string(safe_gb(0x03)) == "cyan");
    CHECK(enum_as_string(safe_gb(0x04)) == "black + 0x04");
    CHECK(enum_as_string(safe_gb(0x05)) == "blue + 0x04");
    CHECK(enum_as_string(safe_gb(0x06)) == "green + 0x04");
    CHECK(enum_as_string(safe_gb(0x07)) == "cyan + 0x04");
    CHECK(enum_as_string(safe_gb(0x40)) == "black + 0x40");
    CHECK(enum_as_string(safe_gb(0x7 + 0x40)) == "cyan + 0x44");
  }
}
#pragma endregion

// All three bits are valid, but we have no name for green, so we use a
// placeholder space. Contrast it with rb, which has no space and therefore
// considers green invalid.
enum class rskipb : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};

consteval auto corvid_enum_spec(rskipb*) {
  return make_bitmask_enum_spec<rskipb, "red,*,blue">();
}

// All three bits are valid, but we have no names for colors with green.
// Contrast this with safe_rb, which considers the green bit invalid.
enum class safe_rskipb : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};
consteval auto corvid_enum_spec(safe_rskipb*) {
  return make_bitmask_enum_values_spec<safe_rskipb,
      "black, blue,*  ,  ?,red, purple,?, ?", wrapclip::limit>();
}

// Like rskipb but uses question-mark placeholder, confirming identical
// behavior to asterisk: green bit is valid but unnamed, so it appears in hex
// residual.
enum class rqb : std::uint8_t {
  black = 0,  // ---
  red = 4,    // r--
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 6, // rg-
  purple = 5, // r-b
  cyan = 3,   // -gb
  white = 7   // rgb
};
consteval auto corvid_enum_spec(rqb*) {
  return make_bitmask_enum_spec<rqb, "red,?,blue">();
}

// Like safe_rb but uses hyphen for invalid values instead of empty element,
// confirming identical behavior: invalid values appear in hex residual.
enum class safe_rb_h : std::uint8_t {
  black = 0,  // ---
  blue = 1,   // --b
  red = 4,    // r--
  purple = 5, // r-b
};
consteval auto corvid_enum_spec(safe_rb_h*) {
  return make_bitmask_enum_values_spec<safe_rb_h,
      "black,blue,-,-,red,purple,-,-", wrapclip::limit>();
}

#pragma region Placeholders

TEST_CASE("Placeholders", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    // Question-mark placeholder in bit-name mode: bit is valid, unnamed.
    CHECK(valid_bits_v<rqb> == 7);
    CHECK(enum_as_string(rqb(0x00)) == "0x00");
    CHECK(enum_as_string(rqb(0x01)) == "blue");
    CHECK(enum_as_string(rqb(0x02)) == "0x02"); // unnamed green -> hex
    CHECK(enum_as_string(rqb(0x03)) == "blue + 0x02");
    CHECK(enum_as_string(rqb(0x04)) == "red");
    CHECK(enum_as_string(rqb(0x05)) == "red + blue");
    CHECK(enum_as_string(rqb(0x06)) == "red + 0x02");
    CHECK(enum_as_string(rqb(0x07)) == "red + blue + 0x02");
  }
  if (true) {
    // Hyphen placeholder in value-name mode: value is invalid, unnamed.
    CHECK(valid_bits_v<safe_rb_h> == 5);
    CHECK(enum_as_string(safe_rb_h(0x00)) == "black");
    CHECK(enum_as_string(safe_rb_h(0x01)) == "blue");
    CHECK((enum_as_string(safe_rb_h(0x02))) ==
          ("black + 0x02")); // invalid -> hex
    CHECK(enum_as_string(safe_rb_h(0x03)) == "blue + 0x02");
    CHECK(enum_as_string(safe_rb_h(0x04)) == "red");
    CHECK(enum_as_string(safe_rb_h(0x05)) == "purple");
    CHECK(enum_as_string(safe_rb_h(0x06)) == "red + 0x02");
    CHECK(enum_as_string(safe_rb_h(0x07)) == "purple + 0x02");
  }
}
#pragma endregion

#pragma region SkipBlue

TEST_CASE("SkipBlue", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<rskipb> == 7);
    CHECK(max_value<rskipb>() == rskipb::white);
    CHECK_FALSE(bit_clip_v<rskipb>);
    CHECK(bits_length<rskipb>() == 3U);
  }
  if (true) {
    CHECK(enum_as_string(rskipb(0x00)) == "0x00");
    CHECK(enum_as_string(rskipb(0x01)) == "blue");
    CHECK(enum_as_string(rskipb(0x02)) == "0x02");
    CHECK(enum_as_string(rskipb(0x03)) == "blue + 0x02");
    CHECK(enum_as_string(rskipb(0x04)) == "red");
    CHECK(enum_as_string(rskipb(0x05)) == "red + blue");
    CHECK(enum_as_string(rskipb(0x06)) == "red + 0x02");
    CHECK(enum_as_string(rskipb(0x07)) == "red + blue + 0x02");
    CHECK(enum_as_string(rskipb(0x40)) == "0x40");
    CHECK(enum_as_string(rskipb(0x7 + 0x40)) == "red + blue + 0x42");
  }
  if (true) {
    CHECK(valid_bits_v<safe_rskipb> == 7);
    CHECK(max_value<safe_rskipb>() == safe_rskipb(0x07));
    CHECK(bit_clip_v<safe_rskipb>);
    CHECK(bits_length<safe_rskipb>() == 3U);
  }
  if (true) {
    CHECK(enum_as_string(safe_rskipb(0x00)) == "black");
    CHECK(enum_as_string(safe_rskipb(0x01)) == "blue");
    CHECK(enum_as_string(safe_rskipb(0x02)) == "black + 0x02");
    CHECK(enum_as_string(safe_rskipb(0x03)) == "blue + 0x02");
    CHECK(enum_as_string(safe_rskipb(0x04)) == "red");
    CHECK(enum_as_string(safe_rskipb(0x05)) == "purple");
    CHECK(enum_as_string(safe_rskipb(0x06)) == "red + 0x02");
    CHECK(enum_as_string(safe_rskipb(0x07)) == "purple + 0x02");
    CHECK(enum_as_string(safe_rskipb(0x40)) == "black + 0x40");
    CHECK(enum_as_string(safe_rskipb(0x7 + 0x40)) == "purple + 0x42");
  }
}
#pragma endregion

// Note: safe_b is impossible because we need at least one valid bit.

// Same thing, but safe due to clipping.
enum class safe_bw : std::uint8_t { black, white };
consteval auto corvid_enum_spec(safe_bw*) {
  return make_bitmask_enum_values_spec<safe_bw, "color::black,color::white",
      wrapclip::limit>();
}

#pragma region SafeBlackWhite

TEST_CASE("SafeBlackWhite", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<safe_bw> == 1);
    CHECK(max_value<safe_bw>() == safe_bw::white);
    CHECK(bit_clip_v<safe_bw>);
    CHECK(bits_length<safe_bw>() == 1U);
  }
  if (true) {
    CHECK(enum_as_string(safe_bw(0x00)) == "color::black");
    CHECK(enum_as_string(safe_bw(0x01)) == "color::white");
    CHECK(enum_as_string(safe_bw(0x02)) == "color::black + 0x02");
    CHECK(enum_as_string(safe_bw(0x03)) == "color::white + 0x02");
    CHECK(enum_as_string(safe_bw(0x04)) == "color::black + 0x04");
    CHECK(enum_as_string(safe_bw(0x05)) == "color::white + 0x04");
    CHECK(enum_as_string(safe_bw(0x06)) == "color::black + 0x06");
    CHECK(enum_as_string(safe_bw(0x07)) == "color::white + 0x06");
    CHECK(enum_as_string(safe_bw(0x40)) == "color::black + 0x40");
    CHECK(enum_as_string(safe_bw(0x7 + 0x40)) == "color::white + 0x46");
  }
}
#pragma endregion

// Same thing, but safe due to clipping.
enum class safe_w : std::uint8_t { white = 1 };
consteval auto corvid_enum_spec(safe_w*) {
  return make_bitmask_enum_values_spec<safe_w, ",color::white",
      wrapclip::limit>();
}

#pragma region SafeWhite

TEST_CASE("SafeWhite", "[BitMaskTest]") {
  using namespace strings;
  if (true) {
    CHECK(valid_bits_v<safe_w> == 1);
    CHECK(max_value<safe_w>() == safe_w::white);
    CHECK(bit_clip_v<safe_w>);
    CHECK(bits_length<safe_w>() == 1U);
  }
  if (true) {
    CHECK(enum_as_string(safe_w(0x00)) == "0x00");
    CHECK(enum_as_string(safe_w(0x01)) == "color::white");
    CHECK(enum_as_string(safe_w(0x02)) == "0x02");
    CHECK(enum_as_string(safe_w(0x03)) == "color::white + 0x02");
    CHECK(enum_as_string(safe_w(0x04)) == "0x04");
    CHECK(enum_as_string(safe_w(0x05)) == "color::white + 0x04");
    CHECK(enum_as_string(safe_w(0x06)) == "0x06");
    CHECK(enum_as_string(safe_w(0x07)) == "color::white + 0x06");
    CHECK(enum_as_string(safe_w(0x40)) == "0x40");
    CHECK(enum_as_string(safe_w(0x7 + 0x40)) == "color::white + 0x46");
  }
}
#pragma endregion

template<strings::fixed_string W>
consteval auto cvbfbn() {
  return bitmask::details::calc_valid_bits_from_bit_names<W>();
}

#pragma region EnumCalcBitNames

TEST_CASE("EnumCalcBitNames", "[BitMaskTest]") {
  static_assert(cvbfbn<"r">() == 1);
  static_assert(cvbfbn<"r,">() == 2);
  static_assert(cvbfbn<"r,g">() == 3);
  static_assert(cvbfbn<"r,,">() == 4);
  static_assert(cvbfbn<"r,,b">() == 5);
  static_assert(cvbfbn<"r,g,">() == 6);
  static_assert(cvbfbn<"r,g,b">() == 7);
  // Test with exactly 64 bits (maximum allowed).
  static_assert(
      cvbfbn<"a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,aa,ab,ac,ad,"
             "ae,af,ag,ah,ai,aj,ak,al,am,an,ao,ap,aq,ar,as,at,au,av,aw,ax,ay,"
             "az,ba,bb,bc,bd,be,bf,bg,bh,bi,bj,bk,bl">() ==
      18446744073709551615ULL);
}
#pragma endregion

template<strings::fixed_string W>
consteval auto cvbfvn() {
  return bitmask::details::calc_valid_bits_from_value_names<W>();
}

#pragma region EnumCalcValueNames

TEST_CASE("EnumCalcValueNames", "[BitMaskTest]") {
  static_assert(cvbfvn<",">() == 0);
  static_assert(cvbfvn<"black,">() == 0);
  static_assert(cvbfvn<"black,blue">() == 1);
  static_assert(cvbfvn<"black,blue,green,cyan">() == 3);
  static_assert(cvbfvn<"black,blue,green,cyan,red">() == 7);
  static_assert(cvbfvn<"black,blue,green,cyan,red,purple">() == 7);
  static_assert(cvbfvn<"black,blue,green,cyan,red,purple,yellow">() == 7);
  static_assert(
      cvbfvn<"black,blue,green,cyan,red,purple,yellow,white">() == 7);
  static_assert(cvbfvn<"black,blue,green,cyan,,,,">() == 3);
  static_assert(cvbfvn<"black,blue,,,red,purple,,,">() == 5);
  static_assert(cvbfvn<"black,,green,,red,,yellow,">() == 6);
}
#pragma endregion

#pragma region ExtractEnum

TEST_CASE("ExtractEnum", "[BitMaskTest]") {
  using namespace corvid::strings;
  if (true) {
    rgb e{};
    std::string_view sv;

    sv = "0";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == rgb::black);

    sv = "red";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == rgb::red);

    sv = "green";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == rgb::green);

    sv = "blue";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == rgb::blue);

    sv = "  blue  ";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == rgb::blue);

    sv = "  blue ;xyz";
    CHECK(extract_enum(e, sv));
    CHECK(sv == "xyz");
    CHECK(e == rgb::blue);

    sv = " red   +  blue  ";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == (rgb::red + rgb::blue));
    auto s = enum_as_string(e);
    sv = s;
    CHECK(sv == "red + blue");

    sv = " 2 + red";
    CHECK(extract_enum(e, sv));
    CHECK(sv.empty());
    CHECK(e == (rgb::red + rgb::green));

    sv = "";
    CHECK_FALSE(extract_enum(e, sv));
    sv = " + ";
    CHECK_FALSE(extract_enum(e, sv));

    // Empty pieces are rejected, whether leading, trailing, or doubled.
    sv = "+red";
    CHECK_FALSE(extract_enum(e, sv));
    sv = "red+";
    CHECK_FALSE(extract_enum(e, sv));
    sv = "red++blue";
    CHECK_FALSE(extract_enum(e, sv));
  }
}
#pragma endregion

#pragma region HoleyOps

TEST_CASE("HoleyOps", "[BitMaskTest]") {
  // Tests for op~ and flip with bit masks that have holes (non-contiguous
  // valid bits). rb has valid bits 101 (red and blue, no green).
  if (true) {
    // op~ inverts all bits, including invalid ones.
    // For rb (no clipping), ~black sets all bits including invalid ones.
    CHECK(~rb::black != rb::purple); // Not equal because invalid bits set
    CHECK((~rb::black & rb::purple) == rb::purple); // Valid bits all set
  }
  if (true) {
    // flip only flips valid bits (rb::purple has valid_bits = 5 = 0b101).
    CHECK(flip(rb::black) == rb::purple);
    CHECK(flip(rb::red) == rb::blue);
    CHECK(flip(rb::blue) == rb::red);
    CHECK(flip(rb::purple) == rb::black);
  }
  if (true) {
    // For safe_rb (with wrapclip::limit), op~ behaves like flip.
    CHECK(~safe_rb::black == safe_rb::purple);
    CHECK(~safe_rb::red == safe_rb::blue);
    CHECK(~safe_rb::blue == safe_rb::red);
    CHECK(~safe_rb::purple == safe_rb::black);
  }
}
#pragma endregion

#pragma region MakeAt

TEST_CASE("MakeAt", "[BitMaskTest]") {
  // make_at uses 1-based indexing.
  if (true) {
    CHECK(make_at<rgb>(1) == rgb::blue);  // bit 0 (lsb)
    CHECK(make_at<rgb>(2) == rgb::green); // bit 1
    CHECK(make_at<rgb>(3) == rgb::red);   // bit 2
  }
  if (true) {
    // Verify set_at and clear_at use the same indexing.
    CHECK(set_at(rgb::black, 1) == rgb::blue);
    CHECK(set_at(rgb::black, 2) == rgb::green);
    CHECK(set_at(rgb::black, 3) == rgb::red);
    CHECK(clear_at(rgb::white, 1) == rgb::yellow);
    CHECK(clear_at(rgb::white, 2) == rgb::purple);
    CHECK(clear_at(rgb::white, 3) == rgb::cyan);
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
