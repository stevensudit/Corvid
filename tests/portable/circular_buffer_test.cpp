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
#include <string_view>

#include "corvid/containers.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

#pragma region Construction

TEST_CASE("Construction", "[CircularBufferTest]") {
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
    CHECK(cbv.empty());
    CHECK(cbv2.empty());
    CHECK(cba.empty());
    CHECK(cba2.empty());
    CHECK(cbsv.empty());
    CHECK(cbsv2.empty());
  }
  if (true) {
    using CB = circular_buffer<int>;
    std::array<int, 3> a;
    CB cb0;
    CB cb{a};
    CHECK(cb.capacity() == 3U);
    CHECK(cb.size() == 0U);
    cb.push_back(1);
    CB cb2{std::move(cb)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(cb.empty());
    CHECK(cb0.capacity() == 0U);
    CHECK(cb0.size() == 0U);
    CHECK(cb2.capacity() == 3U);
    CHECK(cb2.size() == 1U);
    cb2.clear();
    CHECK(cb2.capacity() == 3U);
    CHECK(cb2.size() == 0U);
    cb2.push_back(2);
  }
}
#pragma endregion

#pragma region Ops

TEST_CASE("Ops", "[CircularBufferTest]") {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cb{v};

  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 0U);
  CHECK(cb.empty());
  CHECK_FALSE(cb.full());

  cb.push_back(1);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 1U);
  CHECK_FALSE(cb.empty());
  CHECK_FALSE(cb.full());
  CHECK(cb.front() == 1);
  CHECK(cb.back() == 1);
  CHECK(cb[0] == 1);

  cb.push_back(2);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 2U);
  CHECK_FALSE(cb.empty());
  CHECK_FALSE(cb.full());
  CHECK(cb.front() == 1);
  CHECK(cb.back() == 2);
  CHECK(cb[0] == 1);
  CHECK(cb[1] == 2);

  cb.push_back(3);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 3U);
  CHECK_FALSE(cb.empty());
  CHECK(cb.full());
  CHECK(cb.front() == 1);
  CHECK(cb.back() == 3);
  CHECK(cb[0] == 1);
  CHECK(cb[1] == 2);
  CHECK(cb[2] == 3);

  cb.push_back(4);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 3U);
  CHECK_FALSE(cb.empty());
  CHECK(cb.full());
  CHECK(cb.front() == 2);
  CHECK(cb.back() == 4);
  CHECK(cb[0] == 2);
  CHECK(cb[1] == 3);
  CHECK(cb[2] == 4);

  cb.push_back(5);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 3U);
  CHECK_FALSE(cb.empty());
  CHECK(cb.full());
  CHECK(cb.front() == 3);
  CHECK(cb.back() == 5);
  CHECK(cb[0] == 3);
  CHECK(cb[1] == 4);
  CHECK(cb[2] == 5);

  cb.push_back(6);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 3U);
  CHECK_FALSE(cb.empty());
  CHECK(cb.full());
  CHECK(cb.front() == 4);
  CHECK(cb.back() == 6);
  CHECK(cb[0] == 4);
  CHECK(cb[1] == 5);
  CHECK(cb[2] == 6);
  cb.clear();
  CHECK(cb.empty());

  cb.push_front(7);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 1U);
  CHECK_FALSE(cb.empty());
  CHECK_FALSE(cb.full());
  CHECK(cb.front() == 7);
  CHECK(cb.back() == 7);
  CHECK(cb[0] == 7);

  cb.push_front(8);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 2U);
  CHECK_FALSE(cb.empty());
  CHECK_FALSE(cb.full());
  CHECK(cb.front() == 8);
  CHECK(cb.back() == 7);
  CHECK(cb[0] == 8);
  CHECK(cb[1] == 7);

  cb.push_front(9);
  CHECK(cb.capacity() == 3U);
  CHECK(cb.size() == 3U);
  CHECK_FALSE(cb.empty());
  CHECK(cb.full());
  CHECK(cb.front() == 9);
  CHECK(cb.back() == 7);
  CHECK(cb[0] == 9);
  CHECK(cb[1] == 8);
  CHECK(cb[2] == 7);

  int* p{};
  p = cb.try_push_back(1);
  CHECK_FALSE(p);
  p = cb.try_push_front(2);
  CHECK_FALSE(p);
  cb.clear();

  p = cb.try_push_back(1);
  CHECK(p);
  CHECK(*p == 1);
  CHECK(cb.size() == 1U);
  p = cb.try_push_front(2);
  CHECK(p);
  CHECK(*p == 2);
  CHECK(cb.size() == 2U);
  CHECK(cb.front() == 2);
  CHECK(cb.back() == 1);
}
#pragma endregion

#pragma region WrapIndex

TEST_CASE("WrapIndex", "[CircularBufferTest]") {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cb{v};
  cb.push_back(1);
  CHECK(cb[0] == 1);
  CHECK(cb[1] == 1);
  CHECK(cb[2] == 1);
  CHECK(cb[3] == 1);
  cb.push_back(2);
  CHECK(cb[0] == 1);
  CHECK(cb[1] == 2);
  CHECK(cb[2] == 1);
  CHECK(cb[3] == 2);
  cb.push_back(3);
  CHECK(cb[0] == 1);
  CHECK(cb[1] == 2);
  CHECK(cb[2] == 3);
  CHECK(cb[3] == 1);
  CHECK(cb[4] == 2);
  CHECK(cb[5] == 3);
}
#pragma endregion

#pragma region PushPop

TEST_CASE("PushPop", "[CircularBufferTest]") {
  std::vector<int> v;
  v.resize(3);
  circular_buffer cb{v};
  cb.push_back(1);
  cb.push_back(2);
  cb.push_back(3);
  CHECK(cb[0] == 1);
  CHECK(cb[1] == 2);
  CHECK(cb[2] == 3);
  CHECK(cb.pop_front() == 1);
  CHECK(cb[0] == 2);
  CHECK(cb[1] == 3);
  CHECK(cb.pop_front() == 2);
  CHECK(cb[0] == 3);
  CHECK(cb.pop_front() == 3);
  CHECK(cb.empty());

  cb.push_back(4);
  CHECK(cb[0] == 4);
  cb.push_back(5);
  CHECK(cb[0] == 4);
  CHECK(cb[1] == 5);
  CHECK(cb.pop_front() == 4);
  CHECK(cb[0] == 5);
  CHECK(cb.pop_front() == 5);
  cb.push_back(6);
  CHECK(cb[0] == 6);
  cb.push_back(7);
  CHECK(cb[0] == 6);
  CHECK(cb[1] == 7);
  CHECK(cb.pop_front() == 6);
  CHECK(cb[0] == 7);
  CHECK(cb.pop_front() == 7);
  CHECK(cb.empty());

  cb.push_front(3);
  CHECK(cb[0] == 3);
  cb.push_front(2);
  CHECK(cb[0] == 2);
  CHECK(cb[1] == 3);
  cb.push_back(4);
  CHECK(cb[0] == 2);
  CHECK(cb[1] == 3);
  CHECK(cb[2] == 4);
}
#pragma endregion

#pragma region Iterate

TEST_CASE("Iterate", "[CircularBufferTest]") {
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
    CHECK(s == "1,2,3");
    auto b = cb.begin();
    *b = 4;
    CHECK(cb[0] == 4);
    circular_buffer<int>::iterator b2 = b;
    CHECK(b2 == b);
  }
  if (true) {
    std::vector<int> v{4, 2, 3};
    const circular_buffer cb{v, 3U};
    std::string s;
    for (auto& i : cb) {
      if (s.size() > 0) s += ","sv;
      s += std::to_string(i);
    }
    CHECK(s == "4,2,3");
    auto b = cb.begin();
    CHECK(*b == 4);
    // *b = 5;
    circular_buffer<int>::const_iterator b2 = b;
    CHECK(b2 == b);

    auto c = cb.cbegin();
    CHECK(*c == 4);
    // *c = 5;
  }
}
#pragma endregion

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

  [[nodiscard]] int value() const { return value_; }

private:
  int value_{};
};

#pragma region Smoke

TEST_CASE("Smoke", "[CircularBufferTest]") {
  if (true) {
    std::vector<int> v;
    v.resize(3);
    circular_buffer cb{v};
    cb.clear();
    int i = 1;
    cb.push_back(i);
    CHECK(cb[0] == 1);
    cb.clear();
    cb.emplace_back(i);
    CHECK(cb[0] == 1);
    cb.clear();
    cb.push_front(i);
    CHECK(cb[0] == 1);
    cb.clear();
    cb.emplace_front(i);
    CHECK(cb[0] == 1);
    cb.clear();

    (void)cb.try_push_front(1);
    CHECK(cb[0] == 1);
    cb.clear();
    (void)cb.try_push_back(1);
    CHECK(cb[0] == 1);
    cb.clear();
    (void)cb.try_emplace_front(1);
    CHECK(cb[0] == 1);
    cb.clear();
    (void)cb.try_emplace_back(1);
    CHECK(cb[0] == 1);
    cb.clear();

    cb.push_back(1);
    CHECK(cb.front() == 1);
    CHECK(cb.back() == 1);
    CHECK(cb[0] == 1);
    CHECK(cb[1] == 1);
    cb.clear();

    cb.push_front(1);
    CHECK(cb[0] == 1);
    i = cb.pop_front();
    CHECK(i == 1);
    CHECK(cb.empty());
    cb.clear();

    cb.push_back(1);
    CHECK(cb[0] == 1);
    i = cb.pop_back();
    CHECK(i == 1);
    CHECK(cb.empty());
    cb.clear();

    cb.push_back(1);
    CHECK(cb.front() == 1);
    CHECK(cb.back() == 1);
    CHECK(cb[0] == 1);
    CHECK(cb[1] == 1);
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
    CHECK(cb[0].value() == 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    cb.emplace_back(1);
    CHECK(cb[0].value() == 1);
    cb.clear();
    //* cb.emplace_back(moi);
    cb.emplace_back(std::move(moi));
    CHECK(cb[0].value() == 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    //* cb.push_front(moi);
    cb.emplace_back(1, 2);
    CHECK(cb[0].value() == 3);
    cb.clear();
    //* cb.emplace_back({1, 2});
    //* cb.push_back({1, 2});

    //* cb.try_push_back(moi);
    (void)cb.try_push_back(std::move(moi));
    CHECK(cb[0].value() == 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    cb.emplace_back(1);
    CHECK(cb[0].value() == 1);
    cb.clear();
    //* cb.emplace_back(moi);
    cb.emplace_back(std::move(moi));
    CHECK(cb[0].value() == 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    //* cb.try_push_front(moi);
    cb.emplace_back(1, 2);
    CHECK(cb[0].value() == 3);
    cb.clear();
    //* cb.emplace_back({1, 2});
    //* cb.try_push_back({1, 2});

    //* cb.push_front(moi);
    cb.push_front(std::move(moi));
    CHECK(cb[0].value() == 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    cb.emplace_front(1);
    CHECK(cb[0].value() == 1);
    cb.clear();
    //* cb.emplace_front(moi);
    cb.emplace_front(std::move(moi));
    CHECK(cb[0].value() == 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    //* cb.push_front(moi);
    cb.emplace_front(1, 2);
    CHECK(cb[0].value() == 3);
    cb.clear();
    //* cb.emplace_front({1, 2});
    //* cb.push_front({1, 2});

    //* cb.try_push_front(moi);
    (void)cb.try_push_front(std::move(moi));
    CHECK(cb[0].value() == 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    cb.emplace_front(1);
    CHECK(cb[0].value() == 1);
    cb.clear();
    //* cb.emplace_front(moi);
    cb.emplace_front(std::move(moi));
    CHECK(cb[0].value() == 1);
    moi = MoveOnlyInt{1};
    cb.clear();
    //* cb.try_push_front(moi);
    cb.emplace_front(1, 2);
    CHECK(cb[0].value() == 3);
    cb.clear();
    //* cb.emplace_front({1, 2});
    //* cb.try_push_front({1, 2});

    cb.emplace_back(1);
    CHECK(cb.front().value() == 1);
    CHECK(cb.back().value() == 1);
    CHECK(cb[0].value() == 1);
    CHECK(cb[1].value() == 1);
    cb.clear();

    cb.emplace_front(1);
    CHECK(cb[0].value() == 1);
    //* moi = cb.pop_front();
    moi = std::move(cb.pop_front());
    CHECK(moi.value() == 1);
    CHECK(cb.empty());
    cb.clear();

    cb.emplace_back(1);
    CHECK(cb[0].value() == 1);
    //* moi = cb.pop_back();
    moi = std::move(cb.pop_back());
    CHECK(moi.value() == 1);
    CHECK(cb.empty());
    cb.clear();

    cb.emplace_back(1);
    CHECK(cb[0].value() == 1);
    //* moi = cb[0];
    moi = std::move(cb[0]);
    CHECK(moi.value() == 1);
    CHECK(cb[0].value() == 0);
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
    CHECK(cb[0] == 1);
    CHECK(cb[1] == 2);
    CHECK(cb[2] == 3);
    CHECK(ccb[0] == 1);
    CHECK(ccb[1] == 2);
    CHECK(ccb[2] == 3);
    cb[0] = 42;
    CHECK(cb[0] == 42);
    CHECK(ccb[0] == 42);
    //*ccb[0] = 43;
  }
}
#pragma endregion
#pragma region Format

TEST_CASE("Format", "[CircularBufferTest]") {
  std::vector<int> v{0, 0, 0};
  circular_buffer cb{v};
  cb.push_back(1);
  cb.push_back(2);
  cb.push_back(3);

  // A circular_buffer is a std range, so it formats for free via the range
  // formatter, in logical front-to-back order, narrow and wide.
  CHECK(std::format("{}", cb) == "[1, 2, 3]");
  CHECK(std::format("{:n}", cb) == "1, 2, 3");
  CHECK(std::format(L"{}", cb) == L"[1, 2, 3]");

  // Wrapping overwrites the frontmost element; the format reflects live order.
  cb.push_back(4);
  CHECK(std::format("{}", cb) == "[2, 3, 4]");

  // A const buffer formats too.
  const auto& ccb = cb;
  CHECK(std::format("{}", ccb) == "[2, 3, 4]");
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
