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
#include <LibCorvid/includes/BitmaskEnum.h>

using namespace corvid;
using namespace corvid::bitmask;

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
constexpr size_t bit_count_v<rgb> = 3;

template<>
constexpr auto strings::enum_printer_v<rgb> = make_enum_printer<rgb>(
    {"red", "green", "blue"});

// Same thing, but safe due to clipping.
enum class safe_rgb {
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
constexpr size_t bit_count_v<safe_rgb> = 3;

template<>
constexpr bool bit_clip_v<safe_rgb> = true;

template<>
constexpr auto strings::enum_printer_v<safe_rgb> = make_enum_printer<safe_rgb>(
    {"black", "blue", "green", "cyan", "red", "purple", "yellow", "white"});

// This is not a bitmask class, so it shouldn't work as a bitmap.
enum class tires { none, one, two, three, four, five, six };

TEST(BitMaskTest, Ops) {
  // Does not compile.
  // * auto bad = *tires::none;
  // * auto worse = tires::none | tires::one;

  auto c = rgb::red;
  EXPECT_NE(c, rgb::green);

  EXPECT_EQ(rgb::red | rgb::green, rgb::yellow);
  EXPECT_EQ(rgb::red | rgb::red, rgb::red);
  EXPECT_EQ(rgb::yellow & rgb::green, rgb::green);
  EXPECT_EQ(rgb::red & rgb::green, rgb::black);
  EXPECT_EQ(rgb::red ^ rgb::green, rgb::yellow);
  EXPECT_EQ(rgb::yellow ^ rgb::green, rgb::red);
  EXPECT_NE(~rgb::white, rgb::black);
  EXPECT_EQ(~rgb::white & rgb::white, rgb::black);
  EXPECT_EQ(~rgb::black & rgb::white, rgb::white);

  c = rgb::yellow;
  EXPECT_EQ(c &= rgb::green, rgb::green);
  EXPECT_EQ(c, rgb::green);
  EXPECT_EQ(c |= rgb::red, rgb::yellow);
  EXPECT_EQ(c, rgb::yellow);
  EXPECT_EQ(c ^= rgb::cyan, rgb::purple);
  EXPECT_EQ(c, rgb::purple);

  c = rgb::red;
  c += rgb::green;
  EXPECT_EQ(c, rgb::yellow);
  c -= rgb::red;
  EXPECT_EQ(c, rgb::green);
}

TEST(BitMaskTest, NamedFunctions) {
  if (true) {
    // Does not compile.
    // * auto bad = make<int>(0);
    // * auto worse = set(tires::none, tires::one);

    EXPECT_EQ(make<rgb>(1), rgb::blue);
    EXPECT_NE(make<rgb>(-1), rgb::white);
    EXPECT_EQ(make_safely<rgb>(-1), rgb::white);

    EXPECT_EQ(set(rgb::red, rgb::blue), rgb::purple);
    EXPECT_EQ(clear(rgb::purple, rgb::blue), rgb::red);
    EXPECT_EQ(flip(rgb::white), rgb::black);

    EXPECT_TRUE(has(rgb::purple, rgb::blue));
    EXPECT_TRUE(has(rgb::purple, rgb::white));
    EXPECT_FALSE(has(rgb::purple, rgb::green));
    EXPECT_FALSE(has(rgb::purple, rgb::black));

    EXPECT_TRUE(has_all(rgb::purple, rgb::blue));
    EXPECT_FALSE(has_all(rgb::purple, rgb::white));
    EXPECT_FALSE(has_all(rgb::purple, rgb::green));
    EXPECT_TRUE(has_all(rgb::purple, rgb::black));

    EXPECT_EQ((set_at(rgb::black, 1)), rgb::blue);
    EXPECT_EQ((set_at(rgb::black, 2)), rgb::green);
    EXPECT_EQ((set_at(rgb::black, 3)), rgb::red);

    size_t c{}, s{};
    for (auto e : make_interval<rgb>()) {
      ++c;
      s += *e;
    }
    EXPECT_EQ(c, 8);
    EXPECT_EQ(s, 28);
  }
  if (true) {
    using namespace strings;
    EXPECT_EQ(enum_as_string(rgb::red), "red");
    EXPECT_EQ(enum_as_string(rgb::green), "green");
    EXPECT_EQ(enum_as_string(rgb::blue), "blue");
    EXPECT_EQ(enum_as_string(rgb::black), "0x00000000");
    EXPECT_EQ(enum_as_string(rgb::yellow), "red + green");
    EXPECT_EQ(enum_as_string(rgb::purple), "red + blue");
    EXPECT_EQ(enum_as_string(rgb::cyan), "green + blue");
    EXPECT_EQ(enum_as_string(rgb::white), "red + green + blue");
    EXPECT_EQ(enum_as_string(rgb(0x40)), "0x00000040");
    EXPECT_EQ(enum_as_string(rgb(7 + 0x40)),
        "red + green + blue + 0x00000040");
  }
}

TEST(BitMaskTest, SafeOps) {
  auto c = safe_rgb::red;
  EXPECT_NE(c, safe_rgb::green);

  EXPECT_EQ(safe_rgb::red | safe_rgb::green, safe_rgb::yellow);
  EXPECT_EQ(safe_rgb::red | safe_rgb::red, safe_rgb::red);
  EXPECT_EQ(safe_rgb::yellow & safe_rgb::green, safe_rgb::green);
  EXPECT_EQ(safe_rgb::red & safe_rgb::green, safe_rgb::black);
  EXPECT_EQ(safe_rgb::red ^ safe_rgb::green, safe_rgb::yellow);
  EXPECT_EQ(safe_rgb::yellow ^ safe_rgb::green, safe_rgb::red);
  EXPECT_EQ(~safe_rgb::white, safe_rgb::black);
  EXPECT_EQ(~safe_rgb::white & safe_rgb::white, safe_rgb::black);
  EXPECT_EQ(~safe_rgb::black & safe_rgb::white, safe_rgb::white);

  c = safe_rgb::yellow;
  EXPECT_EQ(c &= safe_rgb::green, safe_rgb::green);
  EXPECT_EQ(c, safe_rgb::green);
  EXPECT_EQ(c |= safe_rgb::red, safe_rgb::yellow);
  EXPECT_EQ(c, safe_rgb::yellow);
  EXPECT_EQ(c ^= safe_rgb::cyan, safe_rgb::purple);
  EXPECT_EQ(c, safe_rgb::purple);

  c = safe_rgb::red;
  c += safe_rgb::green;
  EXPECT_EQ(c, safe_rgb::yellow);
  c -= safe_rgb::red;
  EXPECT_EQ(c, safe_rgb::green);
}

TEST(BitMaskTest, SafeNamedFunctions) {
  if (true) {
    EXPECT_EQ(make<safe_rgb>(1), safe_rgb::blue);
    EXPECT_EQ(make<safe_rgb>(-1), safe_rgb::white);
    EXPECT_EQ(make_safely<safe_rgb>(-1), safe_rgb::white);

    EXPECT_EQ(set(safe_rgb::red, safe_rgb::blue), safe_rgb::purple);
    EXPECT_EQ(clear(safe_rgb::purple, safe_rgb::blue), safe_rgb::red);
    EXPECT_EQ(flip(safe_rgb::white), safe_rgb::black);

    EXPECT_TRUE(has(safe_rgb::purple, safe_rgb::blue));
    EXPECT_TRUE(has(safe_rgb::purple, safe_rgb::white));
    EXPECT_FALSE(has(safe_rgb::purple, safe_rgb::green));
    EXPECT_FALSE(has(safe_rgb::purple, safe_rgb::black));

    EXPECT_TRUE(has_all(safe_rgb::purple, safe_rgb::blue));
    EXPECT_FALSE(has_all(safe_rgb::purple, safe_rgb::white));
    EXPECT_FALSE(has_all(safe_rgb::purple, safe_rgb::green));
    EXPECT_TRUE(has_all(safe_rgb::purple, safe_rgb::black));

    size_t c{}, s{};
    for (auto e : make_interval<safe_rgb>()) {
      ++c;
      s += *e;
    }
    EXPECT_EQ(c, 8);
    EXPECT_EQ(s, 28);
  }
  if (true) {
    using namespace strings;
    EXPECT_EQ(enum_as_string(safe_rgb::black), "black");
    EXPECT_EQ(enum_as_string(safe_rgb::red), "red");
    EXPECT_EQ(enum_as_string(safe_rgb::green), "green");
    EXPECT_EQ(enum_as_string(safe_rgb::blue), "blue");
    EXPECT_EQ(enum_as_string(safe_rgb::yellow), "yellow");
    EXPECT_EQ(enum_as_string(safe_rgb::purple), "purple");
    EXPECT_EQ(enum_as_string(safe_rgb::cyan), "cyan");
    EXPECT_EQ(enum_as_string(safe_rgb::white), "white");
    EXPECT_EQ(enum_as_string(safe_rgb(0x40)), "black + 0x00000040");
    EXPECT_EQ(enum_as_string(safe_rgb(7 + 0x40)), "white + 0x00000040");
  }
}

enum class rgb_unnamed {
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
constexpr size_t bit_count_v<rgb_unnamed> = 3;

template<>
constexpr auto
    strings::enum_printer_v<rgb_unnamed> = make_enum_printer<rgb_unnamed>();

enum class patchy_rgb {
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
constexpr size_t bit_count_v<patchy_rgb> = 3;

template<>
constexpr auto
    strings::enum_printer_v<patchy_rgb> = make_enum_printer<patchy_rgb>(
        {"", "blue", "green", "", "red", "purple", "", "white"});

TEST(BitMaskTest, MoreNamingTests) {
  using namespace strings;
  if (true) {
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x00)), "0x00000000");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x01)), "0x00000001");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x02)), "0x00000002");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x03)), "0x00000003");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x04)), "0x00000004");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x05)), "0x00000005");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x06)), "0x00000006");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x07)), "0x00000007");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x40)), "0x00000040");
    EXPECT_EQ(enum_as_string(rgb_unnamed(0x7 + 0x40)), "0x00000047");
  }
  if (true) {
    EXPECT_EQ(enum_as_string(patchy_rgb::black), "0x00000000");
    EXPECT_EQ(enum_as_string(patchy_rgb::red), "red");
    EXPECT_EQ(enum_as_string(patchy_rgb::green), "green");
    EXPECT_EQ(enum_as_string(patchy_rgb::blue), "blue");
    EXPECT_EQ(enum_as_string(patchy_rgb::yellow), "red + green");
    EXPECT_EQ(enum_as_string(patchy_rgb::purple), "purple");
    EXPECT_EQ(enum_as_string(patchy_rgb::cyan), "green + blue");
    EXPECT_EQ(enum_as_string(patchy_rgb::white), "white");
    EXPECT_EQ(enum_as_string(patchy_rgb(0x40)), "0x00000040");
    EXPECT_EQ(enum_as_string(patchy_rgb(7 + 0x40)), "white + 0x00000040");
  }
}
