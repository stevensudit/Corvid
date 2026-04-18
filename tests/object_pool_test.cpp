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

#include "../corvid/containers/object_pool.h"
#include "minitest.h"

using namespace corvid;

namespace {

static_assert(std::is_same_v<object_pool<int, 4>::index_t, uint8_t>,
    "Small object pools should use uint8_t indices");
static_assert(std::is_same_v<object_pool<int, 256>::index_t, uint16_t>,
    "Pools at 256 entries should widen to uint16_t indices");
static_assert(std::is_same_v<object_pool<int, 65536>::index_t, uint32_t>,
    "Pools at 65536 entries should widen to uint32_t indices");

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

void ObjectPool_BorrowAndReturn() {
  // Borrow a slot and verify it's valid.
  if (true) {
    object_pool<int, 4> pool;
    EXPECT_EQ(pool.capacity(), 4U);

    auto h = pool.borrow();
    EXPECT_TRUE(h);
    *h = 42;
    EXPECT_EQ(*h, 42);
    EXPECT_EQ(h.value(), 42);
  }

  // Slot is returned on handle destruction; the freed slot can be re-borrowed.
  if (true) {
    object_pool<int, 1> pool;
    {
      auto h = pool.borrow();
      EXPECT_TRUE(h);
      EXPECT_FALSE(pool.borrow()); // pool full
    }
    EXPECT_TRUE(pool.borrow()); // slot was returned on destruction
  }

  // Explicit `reset()` returns the slot early.
  if (true) {
    object_pool<int, 1> pool;
    auto h = pool.borrow();
    EXPECT_TRUE(h);
    EXPECT_FALSE(pool.borrow()); // pool full
    h.reset();
    EXPECT_FALSE(h);
    EXPECT_TRUE(pool.borrow()); // slot returned
  }
}

void ObjectPool_FullPool() {
  // Borrowing past capacity returns an empty `borrowed`.
  if (true) {
    object_pool<int, 2> pool;
    auto h0 = pool.borrow();
    auto h1 = pool.borrow();
    EXPECT_TRUE(h0);
    EXPECT_TRUE(h1);

    auto h2 = pool.borrow();
    EXPECT_FALSE(h2);
    EXPECT_TRUE(!h2);
  }
}

void ObjectPool_LIFOOrder() {
  // Slots are returned in LIFO order: last-returned is first-borrowed.
  if (true) {
    object_pool<int, 4> pool;
    auto h0 = pool.borrow();
    auto h1 = pool.borrow();
    int* p0 = h0.get();
    int* p1 = h1.get();

    h1.reset();
    h0.reset();

    // `p0` was returned last, so it should be borrowed first.
    auto h2 = pool.borrow();
    EXPECT_EQ(h2.get(), p0);

    auto h3 = pool.borrow();
    EXPECT_EQ(h3.get(), p1);
  }
}

void ObjectPool_MoveHandle() {
  // Move construction transfers ownership; original becomes empty.
  if (true) {
    object_pool<int, 4> pool;
    auto h = pool.borrow();
    EXPECT_TRUE(h);

    auto h2 = std::move(h);
    EXPECT_FALSE(h);
    EXPECT_TRUE(h2);
  }

  // Slot is returned when the moved-to handle is destroyed.
  if (true) {
    object_pool<int, 1> pool;
    {
      auto outer = pool.borrow();
      {
        auto inner = std::move(outer);
        EXPECT_TRUE(inner);
        EXPECT_FALSE(pool.borrow()); // slot still held by inner
      }
      EXPECT_TRUE(pool.borrow()); // inner destroyed, slot returned
    }
  }

  // Move assignment transfers ownership.
  if (true) {
    object_pool<int, 4> pool;
    auto h = pool.borrow();
    object_pool<int, 4>::borrowed h2;
    h2 = std::move(h);
    EXPECT_FALSE(h);
    EXPECT_TRUE(h2);
  }
}

void ObjectPool_MultipleSlots() {
  // All slots can be borrowed and individually returned.
  if (true) {
    constexpr size_t cap = 8;
    object_pool<int, cap> pool;

    std::array<std::optional<object_pool<int, cap>::borrowed>, cap> handles;
    for (size_t i = 0; i < cap; ++i) {
      handles[i] = pool.borrow();
      EXPECT_TRUE(handles[i].has_value());
      **handles[i] = static_cast<int>(i);
    }
    EXPECT_FALSE(pool.borrow()); // all slots in use

    // Return every other slot.
    for (size_t i = 0; i < cap; i += 2) handles[i]->reset();

    // Re-borrow the returned slots.
    for (size_t i = 0; i < cap; i += 2) {
      handles[i] = pool.borrow();
      EXPECT_TRUE(handles[i].has_value());
    }
    EXPECT_FALSE(pool.borrow()); // all slots in use again
  }
}

void ObjectPool_Callbacks() {
  // BorrowCb is called on each borrow; ReturnCb is called on each return.
  if (true) {
    int borrow_count{};
    int return_count{};
    auto on_borrow = [&](int& v) noexcept { v = ++borrow_count; };
    auto on_return = [&](int&) noexcept { ++return_count; };
    object_pool<int, 4, decltype(on_borrow), decltype(on_return)> store{
        on_borrow, on_return};

    auto h0 = store.borrow();
    EXPECT_EQ(*h0, 1);
    EXPECT_EQ(borrow_count, 1);
    EXPECT_EQ(return_count, 0);

    auto h1 = store.borrow();
    EXPECT_EQ(*h1, 2);
    EXPECT_EQ(borrow_count, 2);

    h0.reset();
    EXPECT_EQ(return_count, 1);
    h1.reset();
    EXPECT_EQ(return_count, 2);
  }

  // ReturnCb runs before the slot re-enters the free list; re-borrowing
  // that slot gives whatever BorrowCb writes, not the stale return value.
  if (true) {
    auto on_borrow = [](int& v) noexcept { v = 99; };
    auto on_return = [](int& v) noexcept { v = 0; };
    object_pool<int, 1, decltype(on_borrow), decltype(on_return)> store{
        on_borrow, on_return};

    auto h = store.borrow();
    EXPECT_EQ(*h, 99);
    h.reset();

    auto h2 = store.borrow();
    EXPECT_EQ(*h2, 99);
  }

  // Default no-op callbacks still compile and work correctly.
  if (true) {
    object_pool<int, 4> pool;
    auto h = pool.borrow();
    EXPECT_TRUE(h);
  }
}

void ObjectPool_CreateHelper() {
  // `create` deduces callback types from lambdas and wires both callbacks in.
  if (true) {
    int borrow_count{};
    int return_count{};

    auto pool = object_pool_factory::create<int, 2>(
        [&](int& v) noexcept { v = ++borrow_count; },
        [&](int&) noexcept { ++return_count; });

    auto h0 = pool.borrow();
    EXPECT_TRUE(h0);
    EXPECT_EQ(*h0, 1);
    EXPECT_EQ(borrow_count, 1);
    EXPECT_EQ(return_count, 0);

    h0.reset();
    EXPECT_EQ(return_count, 1);

    auto h1 = pool.borrow();
    EXPECT_TRUE(h1);
    EXPECT_EQ(*h1, 2);
    EXPECT_EQ(borrow_count, 2);
  }
}

void ObjectPool_DetachAndReattach() {
  // `detach` releases ownership from the handle without returning the slot.
  if (true) {
    object_pool<int, 1> pool;
    auto h = pool.borrow();
    EXPECT_TRUE(h);
    int* item = h.get();

    auto detached = pool.detach(h);
    EXPECT_FALSE(h);
    EXPECT_EQ(detached, item);
    EXPECT_FALSE(pool.borrow()); // detached slot is still out of the pool

    auto h2 = pool.reattach(detached);
    EXPECT_TRUE(h2);
    EXPECT_EQ(h2.get(), item);

    h2.reset();
    EXPECT_TRUE(pool.borrow()); // slot returned after reattached handle resets
  }

  // `reattach` fails for pointers that do not belong to the pool.
  if (true) {
    object_pool<int, 1> pool;
    int outside{};
    auto h = pool.reattach(&outside);
    EXPECT_FALSE(h);
  }
}

MAKE_TEST_LIST(ObjectPool_BorrowAndReturn, ObjectPool_FullPool,
    ObjectPool_LIFOOrder, ObjectPool_MoveHandle, ObjectPool_MultipleSlots,
    ObjectPool_Callbacks, ObjectPool_CreateHelper,
    ObjectPool_DetachAndReattach);

// NOLINTEND(readability-function-cognitive-complexity)
