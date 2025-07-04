// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include <string_view>

#include "../corvid/containers.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;

void CircularBufferTest_Construction() {
  if (true) {
    std::vector<int> v;
    v.resize(3);
    circular_buffer cbv{v};
    circular_buffer cbv2{v, 0U};
    std::array<int, 3> a;
    circular_buffer cba{a};
    circular_buffer cba2{a, 0U};
    auto sv = std::span{v};
    circular_buffer cbsv{sv};
    circular_buffer cbsv2{sv, 0U};
    EXPECT_TRUE(cbv.empty());
    EXPECT_TRUE(cbv2.empty());
    EXPECT_TRUE(cba.empty());
    EXPECT_TRUE(cba2.empty());
    EXPECT_TRUE(cbsv.empty());
    EXPECT_TRUE(cbsv2.empty());
  }
  if (true) {
    using CB = circular_buffer<int>;
    std::array<int, 3> a;
    CB cb0;
    CB cb{a};
    EXPECT_EQ(cb.capacity(), 3U);
    EXPECT_EQ(cb.size(), 0U);
    cb.push_back(1);
    CB cb2{std::move(cb)};
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_TRUE(cb.empty());
    EXPECT_EQ(cb0.capacity(), 0U);
    EXPECT_EQ(cb0.size(), 0U);
    EXPECT_EQ(cb2.capacity(), 3U);
    EXPECT_EQ(cb2.size(), 1U);
    cb2.clear();
    EXPECT_EQ(cb2.capacity(), 3U);
    EXPECT_EQ(cb2.size(), 0U);
    cb2.push_back(2);
  }
}

void CircularBufferTest_Ops() {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cb{v};

  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 0U);
  EXPECT_TRUE(cb.empty());
  EXPECT_FALSE(cb.full());

  cb.push_back(1);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 1U);
  EXPECT_FALSE(cb.empty());
  EXPECT_FALSE(cb.full());
  EXPECT_EQ(cb.front(), 1);
  EXPECT_EQ(cb.back(), 1);
  EXPECT_EQ(cb[0], 1);

  cb.push_back(2);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 2U);
  EXPECT_FALSE(cb.empty());
  EXPECT_FALSE(cb.full());
  EXPECT_EQ(cb.front(), 1);
  EXPECT_EQ(cb.back(), 2);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 2);

  cb.push_back(3);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 3U);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 1);
  EXPECT_EQ(cb.back(), 3);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 2);
  EXPECT_EQ(cb[2], 3);

  cb.push_back(4);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 3U);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 2);
  EXPECT_EQ(cb.back(), 4);
  EXPECT_EQ(cb[0], 2);
  EXPECT_EQ(cb[1], 3);
  EXPECT_EQ(cb[2], 4);

  cb.push_back(5);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 3U);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 3);
  EXPECT_EQ(cb.back(), 5);
  EXPECT_EQ(cb[0], 3);
  EXPECT_EQ(cb[1], 4);
  EXPECT_EQ(cb[2], 5);

  cb.push_back(6);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 3U);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 4);
  EXPECT_EQ(cb.back(), 6);
  EXPECT_EQ(cb[0], 4);
  EXPECT_EQ(cb[1], 5);
  EXPECT_EQ(cb[2], 6);
  cb.clear();
  EXPECT_TRUE(cb.empty());

  cb.push_front(7);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 1U);
  EXPECT_FALSE(cb.empty());
  EXPECT_FALSE(cb.full());
  EXPECT_EQ(cb.front(), 7);
  EXPECT_EQ(cb.back(), 7);
  EXPECT_EQ(cb[0], 7);

  cb.push_front(8);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 2U);
  EXPECT_FALSE(cb.empty());
  EXPECT_FALSE(cb.full());
  EXPECT_EQ(cb.front(), 8);
  EXPECT_EQ(cb.back(), 7);
  EXPECT_EQ(cb[0], 8);
  EXPECT_EQ(cb[1], 7);

  cb.push_front(9);
  EXPECT_EQ(cb.capacity(), 3U);
  EXPECT_EQ(cb.size(), 3U);
  EXPECT_FALSE(cb.empty());
  EXPECT_TRUE(cb.full());
  EXPECT_EQ(cb.front(), 9);
  EXPECT_EQ(cb.back(), 7);
  EXPECT_EQ(cb[0], 9);
  EXPECT_EQ(cb[1], 8);
  EXPECT_EQ(cb[2], 7);

  int* p{};
  p = cb.try_push_back(1);
  EXPECT_FALSE(p);
  p = cb.try_push_front(2);
  EXPECT_FALSE(p);
  cb.clear();

  p = cb.try_push_back(1);
  EXPECT_TRUE(p);
  EXPECT_EQ(*p, 1);
  EXPECT_EQ(cb.size(), 1U);
  p = cb.try_push_front(2);
  EXPECT_TRUE(p);
  EXPECT_EQ(*p, 2);
  EXPECT_EQ(cb.size(), 2U);
  EXPECT_EQ(cb.front(), 2);
  EXPECT_EQ(cb.back(), 1);
}

void CircularBufferTest_WrapIndex() {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cb{v};
  cb.push_back(1);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 1);
  EXPECT_EQ(cb[2], 1);
  EXPECT_EQ(cb[3], 1);
  cb.push_back(2);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 2);
  EXPECT_EQ(cb[2], 1);
  EXPECT_EQ(cb[3], 2);
  cb.push_back(3);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 2);
  EXPECT_EQ(cb[2], 3);
  EXPECT_EQ(cb[3], 1);
  EXPECT_EQ(cb[4], 2);
  EXPECT_EQ(cb[5], 3);
}

void CircularBufferTest_PushPop() {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cb{v};
  cb.push_back(1);
  cb.push_back(2);
  cb.push_back(3);
  EXPECT_EQ(cb[0], 1);
  EXPECT_EQ(cb[1], 2);
  EXPECT_EQ(cb[2], 3);
  EXPECT_EQ(cb.pop_front(), 1);
  EXPECT_EQ(cb[0], 2);
  EXPECT_EQ(cb[1], 3);
  EXPECT_EQ(cb.pop_front(), 2);
  EXPECT_EQ(cb[0], 3);
  EXPECT_EQ(cb.pop_front(), 3);
  EXPECT_TRUE(cb.empty());

  cb.push_back(4);
  EXPECT_EQ(cb[0], 4);
  cb.push_back(5);
  EXPECT_EQ(cb[0], 4);
  EXPECT_EQ(cb[1], 5);
  EXPECT_EQ(cb.pop_front(), 4);
  EXPECT_EQ(cb[0], 5);
  EXPECT_EQ(cb.pop_front(), 5);
  cb.push_back(6);
  EXPECT_EQ(cb[0], 6);
  cb.push_back(7);
  EXPECT_EQ(cb[0], 6);
  EXPECT_EQ(cb[1], 7);
  EXPECT_EQ(cb.pop_front(), 6);
  EXPECT_EQ(cb[0], 7);
  EXPECT_EQ(cb.pop_front(), 7);
  EXPECT_TRUE(cb.empty());

  cb.push_front(3);
  EXPECT_EQ(cb[0], 3);
  cb.push_front(2);
  EXPECT_EQ(cb[0], 2);
  EXPECT_EQ(cb[1], 3);
  cb.push_back(4);
  EXPECT_EQ(cb[0], 2);
  EXPECT_EQ(cb[1], 3);
  EXPECT_EQ(cb[2], 4);
}

void CircularBufferTest_Iterate() {
  if (true) {
    std::vector<int> v;
    v.resize(3);
    circular_buffer cb{v};
    cb.push_back(1);
    cb.push_back(2);
    cb.push_back(3);
    std::string s;
    for (auto& i : cb) {
      if (s.size() > 0) s += ","sv;
      s += std::to_string(i);
    }
    EXPECT_EQ(s, "1,2,3");
    auto b = cb.begin();
    *b = 4;
    EXPECT_EQ(cb[0], 4);
    circular_buffer<int>::iterator b2 = b;
    EXPECT_TRUE(b2 == b);
  }
  if (true) {
    std::vector<int> v{4, 2, 3};
    const circular_buffer cb{v, 3U};
    std::string s;
    for (auto& i : cb) {
      if (s.size() > 0) s += ","sv;
      s += std::to_string(i);
    }
    EXPECT_EQ(s, "4,2,3");
    auto b = cb.begin();
    EXPECT_EQ(*b, 4);
    // *b = 5;
    circular_buffer<int>::const_iterator b2 = b;
    EXPECT_TRUE(b2 == b);

    auto c = cb.cbegin();
    EXPECT_EQ(*c, 4);
    // *c = 5;
  }
}

class MoveOnlyInt {
public:
  MoveOnlyInt() = default;
  explicit MoveOnlyInt(int value) : value_(value) {}
  explicit MoveOnlyInt(int a, int b) : value_(a + b) {}

  MoveOnlyInt(MoveOnlyInt&& other) noexcept : value_(other.value_) {
    other.value_ = 0;
  }

  MoveOnlyInt& operator=(MoveOnlyInt&& other) noexcept {
    if (this != &other) {
      value_ = other.value_;
      other.value_ = 0;
    }
    return *this;
  }

  MoveOnlyInt(const MoveOnlyInt&) = delete;
  MoveOnlyInt& operator=(const MoveOnlyInt&) = delete;

  int value() const { return value_; }

private:
  int value_{};
};

void CircularBufferTest_Smoke() {
  if (true) {
    std::vector<int> v;
    v.resize(3);
    circular_buffer cb{v};
    cb.clear();
    int i = 1;
    cb.push_back(i);
    EXPECT_EQ(cb[0], 1);
    cb.clear();
    cb.emplace_back(i);
    EXPECT_EQ(cb[0], 1);
    cb.clear();
    cb.push_front(i);
    EXPECT_EQ(cb[0], 1);
    cb.clear();
    cb.emplace_front(i);
    EXPECT_EQ(cb[0], 1);
    cb.clear();

    (void)cb.try_push_front(1);
    EXPECT_EQ(cb[0], 1);
    cb.clear();
    (void)cb.try_push_back(1);
    EXPECT_EQ(cb[0], 1);
    cb.clear();
    (void)cb.try_emplace_front(1);
    EXPECT_EQ(cb[0], 1);
    cb.clear();
    (void)cb.try_emplace_back(1);
    EXPECT_EQ(cb[0], 1);
    cb.clear();

    cb.push_back(1);
    EXPECT_EQ(cb.front(), 1);
    EXPECT_EQ(cb.back(), 1);
    EXPECT_EQ(cb[0], 1);
    EXPECT_EQ(cb[1], 1);
    cb.clear();

    cb.push_front(1);
    EXPECT_EQ(cb[0], 1);
    i = cb.pop_front();
    EXPECT_EQ(i, 1);
    EXPECT_TRUE(cb.empty());
    cb.clear();

    cb.push_back(1);
    EXPECT_EQ(cb[0], 1);
    i = cb.pop_back();
    EXPECT_EQ(i, 1);
    EXPECT_TRUE(cb.empty());
    cb.clear();

    cb.push_back(1);
    EXPECT_EQ(cb.front(), 1);
    EXPECT_EQ(cb.back(), 1);
    EXPECT_EQ(cb[0], 1);
    EXPECT_EQ(cb[1], 1);
    cb.clear();
  }

  if (true) {
    std::vector<MoveOnlyInt> v2;
    v2.resize(3);
    circular_buffer cb{v2};
    cb.clear();
    MoveOnlyInt moi{1};

    //* cb.push_back(moi);
    cb.push_back(std::move(moi));
    EXPECT_EQ(cb[0].value(), 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    cb.emplace_back(1);
    EXPECT_EQ(cb[0].value(), 1);
    cb.clear();
    //* cb.emplace_back(moi);
    cb.emplace_back(std::move(moi));
    EXPECT_EQ(cb[0].value(), 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    //* cb.push_front(moi);
    cb.emplace_back(1, 2);
    EXPECT_EQ(cb[0].value(), 3);
    cb.clear();
    //* cb.emplace_back({1, 2});
    //* cb.push_back({1, 2});

    //* cb.try_push_back(moi);
    (void)cb.try_push_back(std::move(moi));
    EXPECT_EQ(cb[0].value(), 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    cb.emplace_back(1);
    EXPECT_EQ(cb[0].value(), 1);
    cb.clear();
    //* cb.emplace_back(moi);
    cb.emplace_back(std::move(moi));
    EXPECT_EQ(cb[0].value(), 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    //* cb.try_push_front(moi);
    cb.emplace_back(1, 2);
    EXPECT_EQ(cb[0].value(), 3);
    cb.clear();
    //* cb.emplace_back({1, 2});
    //* cb.try_push_back({1, 2});

    //* cb.push_front(moi);
    cb.push_front(std::move(moi));
    EXPECT_EQ(cb[0].value(), 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    cb.emplace_front(1);
    EXPECT_EQ(cb[0].value(), 1);
    cb.clear();
    //* cb.emplace_front(moi);
    cb.emplace_front(std::move(moi));
    EXPECT_EQ(cb[0].value(), 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    //* cb.push_front(moi);
    cb.emplace_front(1, 2);
    EXPECT_EQ(cb[0].value(), 3);
    cb.clear();
    //* cb.emplace_front({1, 2});
    //* cb.push_front({1, 2});

    //* cb.try_push_front(moi);
    (void)cb.try_push_front(std::move(moi));
    EXPECT_EQ(cb[0].value(), 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    cb.emplace_front(1);
    EXPECT_EQ(cb[0].value(), 1);
    cb.clear();
    //* cb.emplace_front(moi);
    cb.emplace_front(std::move(moi));
    EXPECT_EQ(cb[0].value(), 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    //* cb.try_push_front(moi);
    cb.emplace_front(1, 2);
    EXPECT_EQ(cb[0].value(), 3);
    cb.clear();
    //* cb.emplace_front({1, 2});
    //* cb.try_push_front({1, 2});

    cb.emplace_back(1);
    EXPECT_EQ(cb.front().value(), 1);
    EXPECT_EQ(cb.back().value(), 1);
    EXPECT_EQ(cb[0].value(), 1);
    EXPECT_EQ(cb[1].value(), 1);
    cb.clear();

    cb.emplace_front(1);
    EXPECT_EQ(cb[0].value(), 1);
    //* moi = cb.pop_front();
    moi = std::move(cb.pop_front());
    EXPECT_EQ(moi.value(), 1);
    EXPECT_TRUE(cb.empty());
    cb.clear();

    cb.emplace_back(1);
    EXPECT_EQ(cb[0].value(), 1);
    //* moi = cb.pop_back();
    moi = std::move(cb.pop_back());
    EXPECT_EQ(moi.value(), 1);
    EXPECT_TRUE(cb.empty());
    cb.clear();

    cb.emplace_back(1);
    EXPECT_EQ(cb[0].value(), 1);
    //* moi = cb[0];
    moi = std::move(cb[0]);
    EXPECT_EQ(moi.value(), 1);
    EXPECT_EQ(cb[0].value(), 0);
    cb.clear();
  }

  if (true) {
    std::vector<int> v;
    v.resize(3);
    circular_buffer cb{v};
    cb.push_back(1);
    cb.push_back(2);
    cb.push_back(3);
    const auto& ccb = cb;
    EXPECT_EQ(cb[0], 1);
    EXPECT_EQ(cb[1], 2);
    EXPECT_EQ(cb[2], 3);
    EXPECT_EQ(ccb[0], 1);
    EXPECT_EQ(ccb[1], 2);
    EXPECT_EQ(ccb[2], 3);
    cb[0] = 42;
    EXPECT_EQ(cb[0], 42);
    EXPECT_EQ(ccb[0], 42);
    //*ccb[0] = 43;
  }
}

MAKE_TEST_LIST(CircularBufferTest_Construction, CircularBufferTest_WrapIndex,
    CircularBufferTest_Ops, CircularBufferTest_PushPop,
    CircularBufferTest_Iterate, CircularBufferTest_Smoke);
