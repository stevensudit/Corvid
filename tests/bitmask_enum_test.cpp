// Corvid20: A general-purpose C++20 library extending std.
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
#include <string_view>

#include "../corvid/meta.h"
#include "../corvid/enums/bitmask_enum.h"
#include "../corvid/containers.h"
#include "AccutestShim.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::enums::bitmask;

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

// TODO: Think of a way to avoid needing to repeat `rgb`.
template<>
constexpr auto registry::enum_spec_v<rgb> =
    make_bitmask_enum_spec<rgb, "red, green, blue">();

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
constexpr auto registry::enum_spec_v<safe_rgb> = make_bitmask_enum_values_spec<
    safe_rgb, "black, blue, green, cyan, red, purple, yellow, white",
    wrapclip::limit>();

// This is not a bitmask class, so it shouldn't work as a bitmap.
enum class tires { none, one, two, three, four, five, six };

void BitMaskTest_Ops() {
  if (true) {
    EXPECT_EQ(valid_bits_v<rgb>, 7);
    EXPECT_EQ(max_value<rgb>(), rgb::white);
    EXPECT_FALSE(bit_clip_v<rgb>);
    EXPECT_EQ(bits_length<rgb>(), 3u)
  }
  if (true) {
    // Does not compile.
    // * auto bad = *tires::none;
    // * auto worse = tires::none | tires::one;

    EXPECT_EQ(valid_bits_v<rgb>, 7);
    EXPECT_EQ(max_value<rgb>(), rgb::white);
    EXPECT_FALSE(bit_clip_v<rgb>);

    EXPECT_EQ(valid_bits_v<safe_rgb>, 7);
    EXPECT_EQ(max_value<safe_rgb>(), safe_rgb::white);
    EXPECT_TRUE(bit_clip_v<safe_rgb>);

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
}

void BitMaskTest_NamedFunctions() {
  if (true) {
    // Does not compile.
    // * auto bad = make<int>(0);
    // * auto worse = set(tires::none, tires::one);

    EXPECT_EQ(make<rgb>(1), rgb::blue);
    EXPECT_NE(make<rgb>(-1), rgb::white);
    EXPECT_EQ(make_safely<rgb>(-1), rgb::white);

    EXPECT_EQ(set(rgb::red, rgb::blue), rgb::purple);
    EXPECT_EQ(set_if(rgb::red, rgb::blue, true), rgb::purple);
    EXPECT_EQ(set_if(rgb::red, rgb::blue, false), rgb::red);

    EXPECT_EQ(clear(rgb::purple, rgb::blue), rgb::red);
    EXPECT_EQ(clear_if(rgb::purple, rgb::blue, true), rgb::red);
    EXPECT_EQ(clear_if(rgb::purple, rgb::blue, false), rgb::purple);

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
    EXPECT_EQ((set_at_if(rgb::black, 3, true)), rgb::red);
    EXPECT_EQ((set_at_if(rgb::black, 3, false)), rgb::black);
    EXPECT_EQ((clear_at(rgb::white, 1)), rgb::yellow);
    EXPECT_EQ((clear_at_if(rgb::white, 1, true)), rgb::yellow);
    EXPECT_EQ((clear_at_if(rgb::white, 2, false)), rgb::white);
    EXPECT_EQ((set_at_to(rgb::black, 1, true)), rgb::blue);
    EXPECT_EQ((set_at_to(rgb::white, 2, false)), rgb::purple);

    size_t c{}, s{};
    for (auto e : make_interval<rgb>()) {
      ++c;
      s += *e;
    }
    EXPECT_EQ(c, 8u);
    EXPECT_EQ(s, 28u);
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

void BitMaskTest_SafeOps() {
  if (true) {
    EXPECT_EQ(valid_bits_v<safe_rgb>, 7);
    EXPECT_EQ(max_value<safe_rgb>(), safe_rgb::white);
    EXPECT_TRUE(bit_clip_v<safe_rgb>);
    EXPECT_EQ(bits_length<safe_rgb>(), 3u)
  }
  if (true) {
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
    EXPECT_EQ(~safe_rgb::black, safe_rgb::white);

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
}

void BitMaskTest_SafeNamedFunctions() {
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
    EXPECT_EQ(c, 8u);
    EXPECT_EQ(s, 28u);
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
constexpr auto registry::enum_spec_v<rgb_unnamed> =
    make_bitmask_enum_spec<rgb_unnamed, rgb_unnamed::white, wrapclip::limit>();

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
constexpr auto registry::enum_spec_v<patchy_rgb> =
    make_bitmask_enum_values_spec<patchy_rgb,
        "-,blue,green,-,red,purple,-,white">();

void BitMaskTest_MoreNamingTests() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<rgb_unnamed>, 7);
    EXPECT_EQ(max_value<rgb_unnamed>(), rgb_unnamed::white);
    EXPECT_TRUE(bit_clip_v<rgb_unnamed>);
    EXPECT_EQ(bits_length<rgb_unnamed>(), 3u)
  }
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
    EXPECT_EQ(valid_bits_v<patchy_rgb>, 7);
    EXPECT_EQ(max_value<patchy_rgb>(), patchy_rgb::white);
    EXPECT_FALSE(bit_clip_v<patchy_rgb>);
    EXPECT_EQ(bits_length<patchy_rgb>(), 3u)
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

void BitMaskTest_StreamingOut() {
  EXPECT_TRUE(OStreamable<rgb>);
  if (true) {
    std::stringstream ss;
    ss << *rgb::red << std::flush;
    EXPECT_EQ(ss.str(), "4");
  }
  if (true) {
    std::stringstream ss;
    ss << rgb::red << std::flush;
    EXPECT_EQ(ss.str(), "red");
  }
}

// RGB, but without the G.
enum class rb {
  black,      // ---
  red = 4,    // r--
  blue = 1,   // --b
  purple = 5, // r-b
};

template<>
constexpr auto registry::enum_spec_v<rb> =
    make_bitmask_enum_spec<rb, "red, , blue">();

void BitMaskTest_NoGreen() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<rb>, 5);
    EXPECT_EQ(max_value<rb>(), rb::purple);
    EXPECT_FALSE(bit_clip_v<rb>);
    EXPECT_EQ(bits_length<rb>(), 3u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(rb(0x00)), "0x00000000");
    EXPECT_EQ(enum_as_string(rb(0x01)), "blue");
    EXPECT_EQ(enum_as_string(rb(0x02)), "0x00000002");
    EXPECT_EQ(enum_as_string(rb(0x03)), "blue + 0x00000002");
    EXPECT_EQ(enum_as_string(rb(0x04)), "red");
    EXPECT_EQ(enum_as_string(rb(0x05)), "red + blue");
    EXPECT_EQ(enum_as_string(rb(0x06)), "red + 0x00000002");
    EXPECT_EQ(enum_as_string(rb(0x07)), "red + blue + 0x00000002");
    EXPECT_EQ(enum_as_string(rb(0x40)), "0x00000040");
    EXPECT_EQ(enum_as_string(rb(0x7 + 0x40)), "red + blue + 0x00000042");
  }
}

// RGB, but without the B.
enum class rg {
  black,      // ---
  red = 4,    // r--
  green = 2,  // -g-
  yellow = 6, // rg-
};

template<>
constexpr auto registry::enum_spec_v<rg> =
    make_bitmask_enum_spec<rg, "red, green, -">();

void BitMaskTest_NoBlue() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<rg>, 6);
    EXPECT_EQ(max_value<rg>(), rg::yellow);
    EXPECT_FALSE(bit_clip_v<rg>);
    EXPECT_EQ(bits_length<rg>(), 3u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(rg(0x00)), "0x00000000");
    EXPECT_EQ(enum_as_string(rg(0x01)), "0x00000001");
    EXPECT_EQ(enum_as_string(rg(0x02)), "green");
    EXPECT_EQ(enum_as_string(rg(0x03)), "green + 0x00000001");
    EXPECT_EQ(enum_as_string(rg(0x04)), "red");
    EXPECT_EQ(enum_as_string(rg(0x05)), "red + 0x00000001");
    EXPECT_EQ(enum_as_string(rg(0x06)), "red + green");
    EXPECT_EQ(enum_as_string(rg(0x07)), "red + green + 0x00000001");
    EXPECT_EQ(enum_as_string(rg(0x40)), "0x00000040");
    EXPECT_EQ(enum_as_string(rg(0x7 + 0x40)), "red + green + 0x00000041");
  }
}

// RGB, but without the R.
enum class gb {
  black,      // ---
  green = 2,  // -g-
  blue = 1,   // --b
  yellow = 3, // -gb
};

// Note: A leading comma here would trigger a static assert, whether or not a
// hyphen was prefixed. In contrast, we would allow a leading wildcard
// placeholder
template<>
constexpr auto registry::enum_spec_v<gb> =
    make_bitmask_enum_spec<gb, "green, blue">();

void BitMaskTest_NoRed() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<gb>, 3);
    EXPECT_EQ(max_value<gb>(), gb::yellow);
    EXPECT_FALSE(bit_clip_v<gb>);
    EXPECT_EQ(bits_length<gb>(), 2u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(gb(0x00)), "0x00000000");
    EXPECT_EQ(enum_as_string(gb(0x01)), "blue");
    EXPECT_EQ(enum_as_string(gb(0x02)), "green");
    EXPECT_EQ(enum_as_string(gb(0x03)), "green + blue");
    EXPECT_EQ(enum_as_string(gb(0x04)), "0x00000004");
    EXPECT_EQ(enum_as_string(gb(0x05)), "blue + 0x00000004");
    EXPECT_EQ(enum_as_string(gb(0x06)), "green + 0x00000004");
    EXPECT_EQ(enum_as_string(gb(0x07)), "green + blue + 0x00000004");
    EXPECT_EQ(enum_as_string(gb(0x40)), "0x00000040");
    EXPECT_EQ(enum_as_string(gb(0x7 + 0x40)), "green + blue + 0x00000044");
  }
}

// Same thing, but safe due to clipping.
enum class safe_rb {
  black,      // ---
  red = 4,    // r--
  blue = 1,   // --b
  purple = 5, // r-b
};

template<>
constexpr auto registry::enum_spec_v<safe_rb> = make_bitmask_enum_values_spec<
    safe_rb, "black,blue,,,red,purple,,", wrapclip::limit>();

void BitMaskTest_SafeNoGreen() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<safe_rb>, 5);
    EXPECT_EQ(max_value<safe_rb>(), safe_rb::purple);
    EXPECT_TRUE(bit_clip_v<safe_rb>);
    EXPECT_EQ(bits_length<safe_rb>(), 3u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(safe_rb(0x00)), "black");
    EXPECT_EQ(enum_as_string(safe_rb(0x01)), "blue");
    EXPECT_EQ(enum_as_string(safe_rb(0x02)), "black + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_rb(0x03)), "blue + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_rb(0x04)), "red");
    EXPECT_EQ(enum_as_string(safe_rb(0x05)), "purple");
    EXPECT_EQ(enum_as_string(safe_rb(0x06)), "red + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_rb(0x07)), "purple + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_rb(0x40)), "black + 0x00000040");
    EXPECT_EQ(enum_as_string(safe_rb(0x7 + 0x40)), "purple + 0x00000042");
  }
}

// Same thing, but safe due to clipping.
enum class safe_rg {
  black,      // ---
  red = 4,    // r--
  green = 2,  // -g-
  yellow = 6, // rg-
};

template<>
constexpr auto registry::enum_spec_v<safe_rg> = make_bitmask_enum_values_spec<
    safe_rg, "black,,green,,red,,yellow,", wrapclip::limit>();

void BitMaskTest_SafeNoBlue() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<safe_rg>, 6);
    EXPECT_EQ(max_value<safe_rg>(), safe_rg::yellow);
    EXPECT_TRUE(bit_clip_v<safe_rg>);
    EXPECT_EQ(bits_length<safe_rg>(), 3u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(safe_rg(0x00)), "black");
    EXPECT_EQ(enum_as_string(safe_rg(0x01)), "black + 0x00000001");
    EXPECT_EQ(enum_as_string(safe_rg(0x02)), "green");
    EXPECT_EQ(enum_as_string(safe_rg(0x03)), "green + 0x00000001");
    EXPECT_EQ(enum_as_string(safe_rg(0x04)), "red");
    EXPECT_EQ(enum_as_string(safe_rg(0x05)), "red + 0x00000001");
    EXPECT_EQ(enum_as_string(safe_rg(0x06)), "yellow");
    EXPECT_EQ(enum_as_string(safe_rg(0x07)), "yellow + 0x00000001");
    EXPECT_EQ(enum_as_string(safe_rg(0x40)), "black + 0x00000040");
    EXPECT_EQ(enum_as_string(safe_rg(0x7 + 0x40)), "yellow + 0x00000041");
  }
}

// Same thing, but safe due to clipping.
enum class safe_gb {
  black,     // ---
  green = 2, // -g-
  blue = 1,  // --b
  cyan = 3,  // -gb
};

template<>
constexpr auto registry::enum_spec_v<safe_gb> = make_bitmask_enum_values_spec<
    safe_gb, "black,blue,green,cyan,,,,", wrapclip::limit>();

void BitMaskTest_SafeNoRed() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<safe_gb>, 3);
    EXPECT_EQ(max_value<safe_gb>(), safe_gb::cyan);
    EXPECT_TRUE(bit_clip_v<safe_gb>);
    EXPECT_EQ(bits_length<safe_gb>(), 2u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(safe_gb(0x00)), "black");
    EXPECT_EQ(enum_as_string(safe_gb(0x01)), "blue");
    EXPECT_EQ(enum_as_string(safe_gb(0x02)), "green");
    EXPECT_EQ(enum_as_string(safe_gb(0x03)), "cyan");
    EXPECT_EQ(enum_as_string(safe_gb(0x04)), "black + 0x00000004");
    EXPECT_EQ(enum_as_string(safe_gb(0x05)), "blue + 0x00000004");
    EXPECT_EQ(enum_as_string(safe_gb(0x06)), "green + 0x00000004");
    EXPECT_EQ(enum_as_string(safe_gb(0x07)), "cyan + 0x00000004");
    EXPECT_EQ(enum_as_string(safe_gb(0x40)), "black + 0x00000040");
    EXPECT_EQ(enum_as_string(safe_gb(0x7 + 0x40)), "cyan + 0x00000044");
  }
}

// All three bits are valid, but we have no name for green, so we use a
// placeholder space. Contrast it with rb, which has no space and therefore
// considers green invalid.
enum class rskipb {
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
constexpr auto registry::enum_spec_v<rskipb> =
    make_bitmask_enum_spec<rskipb, "red,*,blue">();

// All three bits are valid, but we have no names for colors with green.
// Contrast this with safe_rb, which considers the green bit invalid.
enum class safe_rskipb {
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
constexpr auto registry::enum_spec_v<safe_rskipb> =
    make_bitmask_enum_values_spec<safe_rskipb,
        "black, blue,*  ,  ?,red, purple,?, ?", wrapclip::limit>();

void BitMaskTest_SkipBlue() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<rskipb>, 7);
    EXPECT_EQ(max_value<rskipb>(), rskipb::white);
    EXPECT_FALSE(bit_clip_v<rskipb>);
    EXPECT_EQ(bits_length<rskipb>(), 3u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(rskipb(0x00)), "0x00000000");
    EXPECT_EQ(enum_as_string(rskipb(0x01)), "blue");
    EXPECT_EQ(enum_as_string(rskipb(0x02)), "0x00000002");
    EXPECT_EQ(enum_as_string(rskipb(0x03)), "blue + 0x00000002");
    EXPECT_EQ(enum_as_string(rskipb(0x04)), "red");
    EXPECT_EQ(enum_as_string(rskipb(0x05)), "red + blue");
    EXPECT_EQ(enum_as_string(rskipb(0x06)), "red + 0x00000002");
    EXPECT_EQ(enum_as_string(rskipb(0x07)), "red + blue + 0x00000002");
    EXPECT_EQ(enum_as_string(rskipb(0x40)), "0x00000040");
    EXPECT_EQ(enum_as_string(rskipb(0x7 + 0x40)), "red + blue + 0x00000042");
  }
  if (true) {
    EXPECT_EQ(valid_bits_v<safe_rskipb>, 7);
    EXPECT_EQ(max_value<safe_rskipb>(), safe_rskipb(0x07));
    EXPECT_TRUE(bit_clip_v<safe_rskipb>);
    EXPECT_EQ(bits_length<safe_rskipb>(), 3u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(safe_rskipb(0x00)), "black");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x01)), "blue");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x02)), "black + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x03)), "blue + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x04)), "red");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x05)), "purple");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x06)), "red + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x07)), "purple + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x40)), "black + 0x00000040");
    EXPECT_EQ(enum_as_string(safe_rskipb(0x7 + 0x40)), "purple + 0x00000042");
  }
}

// Note: safe_b is impossible because we need at least one valid bit.

// Same thing, but safe due to clipping.
enum class safe_bw { black, white };

template<>
constexpr auto registry::enum_spec_v<safe_bw> = make_bitmask_enum_values_spec<
    safe_bw, "color::black,color::white", wrapclip::limit>();

void BitMaskTest_SafeBlackWhite() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<safe_bw>, 1);
    EXPECT_EQ(max_value<safe_bw>(), safe_bw::white);
    EXPECT_TRUE(bit_clip_v<safe_bw>);
    EXPECT_EQ(bits_length<safe_bw>(), 1u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(safe_bw(0x00)), "color::black");
    EXPECT_EQ(enum_as_string(safe_bw(0x01)), "color::white");
    EXPECT_EQ(enum_as_string(safe_bw(0x02)), "color::black + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_bw(0x03)), "color::white + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_bw(0x04)), "color::black + 0x00000004");
    EXPECT_EQ(enum_as_string(safe_bw(0x05)), "color::white + 0x00000004");
    EXPECT_EQ(enum_as_string(safe_bw(0x06)), "color::black + 0x00000006");
    EXPECT_EQ(enum_as_string(safe_bw(0x07)), "color::white + 0x00000006");
    EXPECT_EQ(enum_as_string(safe_bw(0x40)), "color::black + 0x00000040");
    EXPECT_EQ(enum_as_string(safe_bw(0x7 + 0x40)),
        "color::white + 0x00000046");
  }
}

// Same thing, but safe due to clipping.
enum class safe_w { white = 1 };

template<>
constexpr auto registry::enum_spec_v<safe_w> =
    make_bitmask_enum_values_spec<safe_w, ",color::white", wrapclip::limit>();

void BitMaskTest_SafeWhite() {
  using namespace strings;
  if (true) {
    EXPECT_EQ(valid_bits_v<safe_w>, 1);
    EXPECT_EQ(max_value<safe_w>(), safe_w::white);
    EXPECT_TRUE(bit_clip_v<safe_w>);
    EXPECT_EQ(bits_length<safe_w>(), 1u)
  }
  if (true) {
    EXPECT_EQ(enum_as_string(safe_w(0x00)), "0x00000000");
    EXPECT_EQ(enum_as_string(safe_w(0x01)), "color::white");
    EXPECT_EQ(enum_as_string(safe_w(0x02)), "0x00000002");
    EXPECT_EQ(enum_as_string(safe_w(0x03)), "color::white + 0x00000002");
    EXPECT_EQ(enum_as_string(safe_w(0x04)), "0x00000004");
    EXPECT_EQ(enum_as_string(safe_w(0x05)), "color::white + 0x00000004");
    EXPECT_EQ(enum_as_string(safe_w(0x06)), "0x00000006");
    EXPECT_EQ(enum_as_string(safe_w(0x07)), "color::white + 0x00000006");
    EXPECT_EQ(enum_as_string(safe_w(0x40)), "0x00000040");
    EXPECT_EQ(enum_as_string(safe_w(0x7 + 0x40)), "color::white + 0x00000046");
  }
}

template<strings::fixed_string W>
consteval auto cvbfbn() {
  return bitmask::details::calc_valid_bits_from_bit_names<W>();
}

void BitMaskTest_EnumCalcBitNames() {
  static_assert(cvbfbn<"r">() == 1);
  static_assert(cvbfbn<"r,">() == 2);
  static_assert(cvbfbn<"r,g">() == 3);
  static_assert(cvbfbn<"r,,">() == 4);
  static_assert(cvbfbn<"r,,b">() == 5);
  static_assert(cvbfbn<"r,g,">() == 6);
  static_assert(cvbfbn<"r,g,b">() == 7);
  static_assert(
      cvbfbn<"a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,aa,ab,ac,ad,"
             "ae,af,ag,ah,ai,aj,ak,al,am,an,ao,ap,aq,ar,as,at,au,av,aw,ax,ay,"
             "az,ba,bb,bc,bd,be,bf,bg,bh,bi,bj,bk,bl,bm,bn,bo,bp,bq,br,bs,bt,"
             "bu,bv,bw,bx,by,bz">() == 18446744073709551615ull);
}

template<strings::fixed_string W>
consteval auto cvbfvn() {
  return bitmask::details::calc_valid_bits_from_value_names<W>();
}

void BitMaskTest_EnumCalcValueNames() {
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

// TODO: Add test with make_interval<byte> to show how to use it correctly.
// It'll fail by default, so you have to specify a larger underlying type.

// TODO: Add tests for op~ and flip for bit masks with holes.

MAKE_TEST_LIST(BitMaskTest_Ops, BitMaskTest_NamedFunctions,
    BitMaskTest_SafeOps, BitMaskTest_SafeNamedFunctions,
    BitMaskTest_MoreNamingTests, BitMaskTest_StreamingOut, BitMaskTest_NoGreen,
    BitMaskTest_NoBlue, BitMaskTest_NoRed, BitMaskTest_SafeNoGreen,
    BitMaskTest_SafeNoBlue, BitMaskTest_SafeNoRed, BitMaskTest_SkipBlue,
    BitMaskTest_SafeBlackWhite, BitMaskTest_EnumCalcBitNames,
    BitMaskTest_EnumCalcValueNames, BitMaskTest_SafeWhite);
