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

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "../corvid/ecs/id_container.h"
#include "minitest.h"

using namespace corvid;

// Use entity_id_t as the test ID type: a SequentialEnum backed by size_t.
// Aliased to eid_t to avoid collision with the POSIX ::id_t from sys/types.h.
using eid_t = corvid::ecs::id_enums::entity_id_t;
using container_t = id_container<int, eid_t>;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

void IdContainer_DefaultConstruct() {
  // Default-constructed container is empty with maximum limit.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.empty());
    EXPECT_EQ(c.size(), 0U);
    EXPECT_EQ(c.size_as_enum(), eid_t{0});
    EXPECT_EQ(c.id_limit(), eid_t::invalid);
  }
}

void IdContainer_PushBack() {
  // push_back appends values accessible by slot index.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(10));
    EXPECT_TRUE(c.push_back(20));
    EXPECT_TRUE(c.push_back(30));
    EXPECT_EQ(c.size(), 3U);
    EXPECT_FALSE(c.empty());
    EXPECT_EQ(c[eid_t{0}], 10);
    EXPECT_EQ(c[eid_t{1}], 20);
    EXPECT_EQ(c[eid_t{2}], 30);
  }

  // push_back with rvalue.
  if (true) {
    container_t c;
    int val = 42;
    EXPECT_TRUE(c.push_back(std::move(val)));
    EXPECT_EQ(c[eid_t{0}], 42);
  }

  // push_back returns false when the limit is reached.
  if (true) {
    container_t c{eid_t{2}};
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.push_back(2));
    EXPECT_FALSE(c.push_back(3));
    EXPECT_EQ(c.size(), 2U);
  }
}

void IdContainer_EmplaceBack() {
  // emplace_back constructs in-place and returns a pointer to the new element.
  if (true) {
    container_t c;
    auto* ptr = c.emplace_back(99);
    EXPECT_TRUE(ptr != nullptr);
    EXPECT_EQ(*ptr, 99);
    EXPECT_EQ(c[eid_t{0}], 99);
    EXPECT_EQ(c.size(), 1U);
  }

  // emplace_back returns nullptr when the limit is reached.
  if (true) {
    container_t c{eid_t{1}};
    auto* p1 = c.emplace_back(10);
    EXPECT_TRUE(p1 != nullptr);
    auto* p2 = c.emplace_back(20);
    EXPECT_TRUE(p2 == nullptr);
    EXPECT_EQ(c.size(), 1U);
  }
}

void IdContainer_PopBack() {
  // pop_back removes the last slot.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.push_back(2));
    c.pop_back();
    EXPECT_EQ(c.size(), 1U);
    EXPECT_EQ(c[eid_t{0}], 1);
  }
}

void IdContainer_FrontBack() {
  // front() and back() return the first and last elements.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(11));
    EXPECT_TRUE(c.push_back(22));
    EXPECT_TRUE(c.push_back(33));
    EXPECT_EQ(c.front(), 11);
    EXPECT_EQ(c.back(), 33);
  }

  // front() and back() are mutable.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.push_back(2));
    c.front() = 100;
    c.back() = 200;
    EXPECT_EQ(c[eid_t{0}], 100);
    EXPECT_EQ(c[eid_t{1}], 200);
  }
}

void IdContainer_Subscript() {
  // operator[] returns a mutable reference.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(5));
    c[eid_t{0}] = 50;
    EXPECT_EQ(c[eid_t{0}], 50);
  }

  // const operator[] returns a const reference.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(7));
    const container_t& cc = c;
    EXPECT_EQ(cc[eid_t{0}], 7);
  }
}

void IdContainer_At() {
  // at() provides bounds-checked access; throws std::out_of_range.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(3));
    EXPECT_TRUE(c.push_back(6));
    EXPECT_EQ(c.at(eid_t{0}), 3);
    EXPECT_EQ(c.at(eid_t{1}), 6);
    TEST_EXCEPTION(c.at(eid_t{2}), std::out_of_range);
  }

  // const at().
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(9));
    const container_t& cc = c;
    EXPECT_EQ(cc.at(eid_t{0}), 9);
    TEST_EXCEPTION(cc.at(eid_t{1}), std::out_of_range);
  }

  // at() returns a mutable reference.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    c.at(eid_t{0}) = 100;
    EXPECT_EQ(c[eid_t{0}], 100);
  }
}

void IdContainer_SizeAsEnum() {
  // size_as_enum() returns the size as the id_t type.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.size_as_enum() == eid_t{0});
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.size_as_enum() == eid_t{1});
    EXPECT_TRUE(c.push_back(2));
    EXPECT_TRUE(c.size_as_enum() == eid_t{2});
  }
}

void IdContainer_Reserve() {
  // reserve() pre-allocates capacity without changing size.
  if (true) {
    container_t c;
    c.reserve(100);
    EXPECT_EQ(c.size(), 0U);
    EXPECT_TRUE(c.empty());
    EXPECT_GE(c.capacity(), 100U);
  }
}

void IdContainer_Resize() {
  // resize(n) expands or shrinks the slot count.
  if (true) {
    container_t c;
    c.resize(5);
    EXPECT_EQ(c.size(), 5U);
    EXPECT_FALSE(c.empty());
  }

  // resize(n, value) fills new slots with value.
  if (true) {
    container_t c;
    c.resize(3, 77);
    EXPECT_EQ(c.size(), 3U);
    EXPECT_EQ(c[eid_t{0}], 77);
    EXPECT_EQ(c[eid_t{1}], 77);
    EXPECT_EQ(c[eid_t{2}], 77);
  }

  // resize to smaller discards tail slots.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.push_back(2));
    EXPECT_TRUE(c.push_back(3));
    c.resize(2);
    EXPECT_EQ(c.size(), 2U);
    EXPECT_EQ(c[eid_t{0}], 1);
    EXPECT_EQ(c[eid_t{1}], 2);
  }
}

void IdContainer_Clear() {
  // clear() empties the container without releasing capacity.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.push_back(2));
    c.clear();
    EXPECT_TRUE(c.empty());
    EXPECT_EQ(c.size(), 0U);
  }
}

void IdContainer_ShrinkToFit() {
  // shrink_to_fit() reduces capacity to match size.
  if (true) {
    container_t c;
    c.reserve(100);
    EXPECT_TRUE(c.push_back(42));
    c.shrink_to_fit();
    EXPECT_EQ(c.size(), 1U);
    // Capacity is implementation-defined but should not exceed size by much.
    EXPECT_GE(c.capacity(), 1U);
  }
}

void IdContainer_Limit() {
  // id_limit() defaults to the maximum representable value.
  if (true) {
    container_t c;
    EXPECT_EQ(c.id_limit(), eid_t::invalid);
  }

  // Constructor with explicit limit.
  if (true) {
    container_t c{eid_t{100}};
    EXPECT_TRUE(c.id_limit() == eid_t{100});
  }

  // set_id_limit() changes the limit.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.set_id_limit(eid_t{50}));
    EXPECT_TRUE(c.id_limit() == eid_t{50});
    EXPECT_TRUE(c.set_id_limit(eid_t{200}));
    EXPECT_TRUE(c.id_limit() == eid_t{200});
  }

  // set_id_limit() fails when the new limit would be below current size.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.push_back(2));
    EXPECT_FALSE(c.set_id_limit(eid_t{1}));
    EXPECT_EQ(c.id_limit(), eid_t::invalid);
  }

  // Constructor with eager prefill reserves capacity up front.
  if (true) {
    container_t c{eid_t{50}, allocation_policy::eager};
    EXPECT_TRUE(c.empty());
    EXPECT_GE(c.capacity(), 50U);
    EXPECT_EQ(c.id_limit(), eid_t{50});
  }

  // set_id_limit() with eager prefill reserves capacity.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.set_id_limit(eid_t{30}, allocation_policy::eager));
    EXPECT_GE(c.capacity(), 30U);
    EXPECT_EQ(c.id_limit(), eid_t{30});
  }
}

void IdContainer_Iteration() {
  // Range-for iterates over all slots in index order.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(10));
    EXPECT_TRUE(c.push_back(20));
    EXPECT_TRUE(c.push_back(30));

    std::vector<int> got;
    for (auto v : c) got.push_back(v);
    EXPECT_EQ(got.size(), 3U);
    EXPECT_EQ(got[0], 10);
    EXPECT_EQ(got[1], 20);
    EXPECT_EQ(got[2], 30);
  }

  // Empty container yields empty range.
  if (true) {
    container_t c;
    std::size_t count{};
    for ([[maybe_unused]] auto v : c) ++count;
    EXPECT_EQ(count, 0U);
  }

  // cbegin/cend match begin/end.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    EXPECT_EQ(*c.cbegin(), *c.begin());
    EXPECT_TRUE(c.cend() == c.end());
  }

  // Mutable iteration allows modifying values.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.push_back(2));
    for (auto& v : c) v *= 10;
    EXPECT_EQ(c[eid_t{0}], 10);
    EXPECT_EQ(c[eid_t{1}], 20);
  }
}

void IdContainer_Underlying() {
  // underlying() returns the inner std::vector<T>.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(5));
    EXPECT_TRUE(c.push_back(6));
    auto& vec = c.underlying();
    EXPECT_EQ(vec.size(), 2U);
    EXPECT_EQ(vec[0], 5);
    EXPECT_EQ(vec[1], 6);
  }

  // Mutations through underlying() are visible via operator[].
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(0));
    c.underlying()[0] = 99;
    EXPECT_EQ(c[eid_t{0}], 99);
  }
}

void IdContainer_Data() {
  // data() returns a pointer to the first element.
  if (true) {
    container_t c;
    EXPECT_TRUE(c.push_back(1));
    EXPECT_TRUE(c.push_back(2));
    const int* p = c.data();
    EXPECT_EQ(p[0], 1);
    EXPECT_EQ(p[1], 2);
  }
}

void IdContainer_Allocator() {
  // get_allocator() returns the stored allocator.
  if (true) {
    container_t c;
    [[maybe_unused]] auto alloc = c.get_allocator();
    // Compilation success is sufficient: allocator is default-constructible.
  }

  // Constructor with explicit allocator.
  if (true) {
    std::allocator<int> alloc;
    container_t c{alloc};
    EXPECT_TRUE(c.empty());
  }

  // Constructor with limit and explicit allocator.
  if (true) {
    std::allocator<int> alloc;
    container_t c{eid_t{10}, allocation_policy::lazy, alloc};
    EXPECT_TRUE(c.id_limit() == eid_t{10});
    EXPECT_TRUE(c.empty());
  }
}

void IdContainer_NonIntType() {
  // id_container works with non-int value types.
  if (true) {
    id_container<double, eid_t> c;
    EXPECT_TRUE(c.push_back(1.5));
    EXPECT_TRUE(c.push_back(2.5));
    EXPECT_EQ(c.size(), 2U);
    EXPECT_EQ(c[eid_t{0}], 1.5);
    EXPECT_EQ(c[eid_t{1}], 2.5);
  }

  if (true) {
    struct point_t {
      int x{};
      int y{};
    };
    id_container<point_t, eid_t> c;
    auto* ptr = c.emplace_back(point_t{3, 4});
    EXPECT_TRUE(ptr != nullptr);
    EXPECT_EQ(c[eid_t{0}].x, 3);
    EXPECT_EQ(c[eid_t{0}].y, 4);
  }
}

MAKE_TEST_LIST(IdContainer_DefaultConstruct, IdContainer_PushBack,
    IdContainer_EmplaceBack, IdContainer_PopBack, IdContainer_FrontBack,
    IdContainer_Subscript, IdContainer_At, IdContainer_SizeAsEnum,
    IdContainer_Reserve, IdContainer_Resize, IdContainer_Clear,
    IdContainer_ShrinkToFit, IdContainer_Limit, IdContainer_Iteration,
    IdContainer_Underlying, IdContainer_Data, IdContainer_Allocator,
    IdContainer_NonIntType);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
