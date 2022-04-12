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
#include <LibCorvid/includes/Bitmask.h>

#include <iostream>
#include <map>
#include <string>

using namespace corvid::bitmask_ops;

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
constexpr size_t corvid::bitmask::bit_count_v<rgb> = 3;

// This is not a bitmask class, so it shouldn't work as a bitmap.
enum class tires { none, one, two, three, four, five, six };

TEST(BitMaskTest, Ops) {
  auto c = rgb::red;
  EXPECT_NE(c, rgb::green);

  // Does not compile.
  // * auto bad = tires::none | tires::one;

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
}

TEST(BitMaskTest, NamedFunctions) {
  EXPECT_EQ(corvid::bitmask::make<rgb>(1), rgb::blue);
  EXPECT_NE(corvid::bitmask::make<rgb>(-1), rgb::white);
  EXPECT_EQ(corvid::bitmask::make_safely<rgb>(-1), rgb::white);

  EXPECT_EQ(corvid::bitmask::set(rgb::red, rgb::blue), rgb::purple);
  EXPECT_EQ(corvid::bitmask::clear(rgb::purple, rgb::blue), rgb::red);
  EXPECT_EQ(corvid::bitmask::flip(rgb::white), rgb::black);

  EXPECT_TRUE(corvid::bitmask::overlaps(rgb::purple, rgb::blue));
  EXPECT_TRUE(corvid::bitmask::overlaps(rgb::purple, rgb::white));
  EXPECT_FALSE(corvid::bitmask::overlaps(rgb::purple, rgb::green));
  EXPECT_FALSE(corvid::bitmask::overlaps(rgb::purple, rgb::black));

  EXPECT_TRUE(corvid::bitmask::contains(rgb::purple, rgb::blue));
  EXPECT_FALSE(corvid::bitmask::contains(rgb::purple, rgb::white));
  EXPECT_FALSE(corvid::bitmask::contains(rgb::purple, rgb::green));
  EXPECT_TRUE(corvid::bitmask::contains(rgb::purple, rgb::black));
}
