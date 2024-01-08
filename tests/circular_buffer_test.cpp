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
#include <string_view>

#include "../corvid/containers.h"
#include "AccutestShim.h"

using namespace std::literals;
using namespace corvid;

void CircularBufferTest_Deduct() {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cbv{v};
  std::array<int, 3> a;
  circular_buffer cba{a};
  auto sv = std::span{v};
  circular_buffer cbsv{sv};
  EXPECT_TRUE(cbv.empty());
  EXPECT_TRUE(cba.empty());
  EXPECT_TRUE(cbsv.empty());
}

void CircularBufferTest_Ops() {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cb{v};

  EXPECT_EQ(cb.capacity(), 3u);
  EXPECT_EQ(cb.size(), 0u);
  EXPECT_TRUE(cb.empty());
  EXPECT_FALSE(cb.full());

  cb.push_back(1);
  EXPECT_EQ(cb.capacity(), 3u);
  EXPECT_EQ(cb.size(), 1u);
  EXPECT_FALSE(cb.empty());
  EXPECT_FALSE(cb.full());
  EXPECT_EQ(cb.front(), 1);
  EXPECT_EQ(cb.back(), 1);
  EXPECT_EQ(cb[0], 1);

  cb.push_back(2);
  EXPECT_EQ(cb.capacity(), 3u);
  EXPECT_EQ(cb.size(), 2u);
  EXPECT_FALSE(cb.empty());
  EXPECT_FALSE(cb.full());
  EXPECT_EQ(cb.front(), 1);
  EXPECT_EQ(cb.back(), 2);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 2);

  cb.push_back(3);
  EXPECT_EQ(cb.capacity(), 3u);
  EXPECT_EQ(cb.size(), 3u);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 1);
  EXPECT_EQ(cb.back(), 3);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 2);
  EXPECT_EQ(cb[2], 3);

  cb.push_back(4);
  EXPECT_EQ(cb.capacity(), 3u);
  EXPECT_EQ(cb.size(), 3u);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 2);
  EXPECT_EQ(cb.back(), 4);
  EXPECT_EQ(cb[0], 2);
  EXPECT_EQ(cb[1], 3);
  EXPECT_EQ(cb[2], 4);

  cb.push_back(5);
  EXPECT_EQ(cb.capacity(), 3u);
  EXPECT_EQ(cb.size(), 3u);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 3);
  EXPECT_EQ(cb.back(), 5);
  EXPECT_EQ(cb[0], 3);
  EXPECT_EQ(cb[1], 4);
  EXPECT_EQ(cb[2], 5);

  cb.push_back(6);
  EXPECT_EQ(cb.capacity(), 3u);
  EXPECT_EQ(cb.size(), 3u);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 4);
  EXPECT_EQ(cb.back(), 6);
  EXPECT_EQ(cb[0], 4);
  EXPECT_EQ(cb[1], 5);
  EXPECT_EQ(cb[2], 6);
}

void CircularBufferTest_WrapIndex() {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cb{v};
  cb.push_back(1);
  cb.push_back(2);
  cb.push_back(3);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 2);
  EXPECT_EQ(cb[2], 3);
  EXPECT_EQ(cb[3], 1);
  EXPECT_EQ(cb[4], 2);
  EXPECT_EQ(cb[5], 3);
}

// TODO: Add tests for pop_front and pop_back.

// TODO: Write tests for push_front, and for mixes of front and back and push
// and pop.

MAKE_TEST_LIST(CircularBufferTest_Deduct, CircularBufferTest_WrapIndex,
    CircularBufferTest_Ops);
