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
#include "catch2_main.h"

using namespace corvid;

// Use entity_id_t as the test ID type: a SequentialEnum backed by size_t.
// Aliased to eid_t to avoid collision with the POSIX ::id_t from sys/types.h.
using eid_t = corvid::ecs::id_enums::entity_id_t;
using container_t = id_container<int, eid_t>;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

#pragma region DefaultConstruct

TEST_CASE("IdContainer_DefaultConstruct", "[IdContainer]") {
  // Default-constructed container is empty with maximum limit.
  if (true) {
    container_t c;
    CHECK((c.empty()));
    CHECK((c.size()) == (0U));
    CHECK((c.size_as_enum()) == (eid_t{0}));
    CHECK((c.id_limit()) == (eid_t::invalid));
  }
}

#pragma endregion
#pragma region PushBack

TEST_CASE("IdContainer_PushBack", "[IdContainer]") {
  // push_back appends values accessible by slot index.
  if (true) {
    container_t c;
    CHECK((c.push_back(10)));
    CHECK((c.push_back(20)));
    CHECK((c.push_back(30)));
    CHECK((c.size()) == (3U));
    CHECK_FALSE((c.empty()));
    CHECK((c[eid_t{0}]) == (10));
    CHECK((c[eid_t{1}]) == (20));
    CHECK((c[eid_t{2}]) == (30));
  }

  // push_back with rvalue.
  if (true) {
    container_t c;
    int val = 42;
    CHECK((c.push_back(std::move(val))));
    CHECK((c[eid_t{0}]) == (42));
  }

  // push_back returns false when the limit is reached.
  if (true) {
    container_t c{eid_t{2}};
    CHECK((c.push_back(1)));
    CHECK((c.push_back(2)));
    CHECK_FALSE((c.push_back(3)));
    CHECK((c.size()) == (2U));
  }
}

#pragma endregion
#pragma region EmplaceBack

TEST_CASE("IdContainer_EmplaceBack", "[IdContainer]") {
  // emplace_back constructs in-place and returns a pointer to the new element.
  if (true) {
    container_t c;
    auto* ptr = c.emplace_back(99);
    CHECK((ptr != nullptr));
    CHECK((*ptr) == (99));
    CHECK((c[eid_t{0}]) == (99));
    CHECK((c.size()) == (1U));
  }

  // emplace_back returns nullptr when the limit is reached.
  if (true) {
    container_t c{eid_t{1}};
    auto* p1 = c.emplace_back(10);
    CHECK((p1 != nullptr));
    auto* p2 = c.emplace_back(20);
    CHECK((p2 == nullptr));
    CHECK((c.size()) == (1U));
  }
}

#pragma endregion
#pragma region PopBack

TEST_CASE("IdContainer_PopBack", "[IdContainer]") {
  // pop_back removes the last slot.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    CHECK((c.push_back(2)));
    c.pop_back();
    CHECK((c.size()) == (1U));
    CHECK((c[eid_t{0}]) == (1));
  }
}

#pragma endregion
#pragma region FrontBack

TEST_CASE("IdContainer_FrontBack", "[IdContainer]") {
  // front() and back() return the first and last elements.
  if (true) {
    container_t c;
    CHECK((c.push_back(11)));
    CHECK((c.push_back(22)));
    CHECK((c.push_back(33)));
    CHECK((c.front()) == (11));
    CHECK((c.back()) == (33));
  }

  // front() and back() are mutable.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    CHECK((c.push_back(2)));
    c.front() = 100;
    c.back() = 200;
    CHECK((c[eid_t{0}]) == (100));
    CHECK((c[eid_t{1}]) == (200));
  }
}

#pragma endregion
#pragma region Subscript

TEST_CASE("IdContainer_Subscript", "[IdContainer]") {
  // operator[] returns a mutable reference.
  if (true) {
    container_t c;
    CHECK((c.push_back(5)));
    c[eid_t{0}] = 50;
    CHECK((c[eid_t{0}]) == (50));
  }

  // const operator[] returns a const reference.
  if (true) {
    container_t c;
    CHECK((c.push_back(7)));
    const container_t& cc = c;
    CHECK((cc[eid_t{0}]) == (7));
  }
}

#pragma endregion
#pragma region At

TEST_CASE("IdContainer_At", "[IdContainer]") {
  // at() provides bounds-checked access; throws std::out_of_range.
  if (true) {
    container_t c;
    CHECK((c.push_back(3)));
    CHECK((c.push_back(6)));
    CHECK((c.at(eid_t{0})) == (3));
    CHECK((c.at(eid_t{1})) == (6));
    CHECK_THROWS_AS(c.at(eid_t{2}), std::out_of_range);
  }

  // const at().
  if (true) {
    container_t c;
    CHECK((c.push_back(9)));
    const container_t& cc = c;
    CHECK((cc.at(eid_t{0})) == (9));
    CHECK_THROWS_AS(cc.at(eid_t{1}), std::out_of_range);
  }

  // at() returns a mutable reference.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    c.at(eid_t{0}) = 100;
    CHECK((c[eid_t{0}]) == (100));
  }
}

#pragma endregion
#pragma region SizeAsEnum

TEST_CASE("IdContainer_SizeAsEnum", "[IdContainer]") {
  // size_as_enum() returns the size as the id_t type.
  if (true) {
    container_t c;
    CHECK((c.size_as_enum() == eid_t{0}));
    CHECK((c.push_back(1)));
    CHECK((c.size_as_enum() == eid_t{1}));
    CHECK((c.push_back(2)));
    CHECK((c.size_as_enum() == eid_t{2}));
  }
}

#pragma endregion
#pragma region Reserve

TEST_CASE("IdContainer_Reserve", "[IdContainer]") {
  // reserve() pre-allocates capacity without changing size.
  if (true) {
    container_t c;
    c.reserve(100);
    CHECK((c.size()) == (0U));
    CHECK((c.empty()));
    CHECK((c.capacity()) >= (100U));
  }
}

#pragma endregion
#pragma region Resize

TEST_CASE("IdContainer_Resize", "[IdContainer]") {
  // resize(n) expands or shrinks the slot count.
  if (true) {
    container_t c;
    c.resize(5);
    CHECK((c.size()) == (5U));
    CHECK_FALSE((c.empty()));
  }

  // resize(n, value) fills new slots with value.
  if (true) {
    container_t c;
    c.resize(3, 77);
    CHECK((c.size()) == (3U));
    CHECK((c[eid_t{0}]) == (77));
    CHECK((c[eid_t{1}]) == (77));
    CHECK((c[eid_t{2}]) == (77));
  }

  // resize to smaller discards tail slots.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    CHECK((c.push_back(2)));
    CHECK((c.push_back(3)));
    c.resize(2);
    CHECK((c.size()) == (2U));
    CHECK((c[eid_t{0}]) == (1));
    CHECK((c[eid_t{1}]) == (2));
  }
}

#pragma endregion
#pragma region Clear

TEST_CASE("IdContainer_Clear", "[IdContainer]") {
  // clear() empties the container without releasing capacity.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    CHECK((c.push_back(2)));
    c.clear();
    CHECK((c.empty()));
    CHECK((c.size()) == (0U));
  }
}

#pragma endregion
#pragma region ShrinkToFit

TEST_CASE("IdContainer_ShrinkToFit", "[IdContainer]") {
  // shrink_to_fit() reduces capacity to match size.
  if (true) {
    container_t c;
    c.reserve(100);
    CHECK((c.push_back(42)));
    c.shrink_to_fit();
    CHECK((c.size()) == (1U));
    // Capacity is implementation-defined but should not exceed size by much.
    CHECK((c.capacity()) >= (1U));
  }
}

#pragma endregion
#pragma region Limit

TEST_CASE("IdContainer_Limit", "[IdContainer]") {
  // id_limit() defaults to the maximum representable value.
  if (true) {
    container_t c;
    CHECK((c.id_limit()) == (eid_t::invalid));
  }

  // Constructor with explicit limit.
  if (true) {
    container_t c{eid_t{100}};
    CHECK((c.id_limit() == eid_t{100}));
  }

  // set_id_limit() changes the limit.
  if (true) {
    container_t c;
    CHECK((c.set_id_limit(eid_t{50})));
    CHECK((c.id_limit() == eid_t{50}));
    CHECK((c.set_id_limit(eid_t{200})));
    CHECK((c.id_limit() == eid_t{200}));
  }

  // set_id_limit() fails when the new limit would be below current size.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    CHECK((c.push_back(2)));
    CHECK_FALSE((c.set_id_limit(eid_t{1})));
    CHECK((c.id_limit()) == (eid_t::invalid));
  }

  // Constructor with eager prefill reserves capacity up front.
  if (true) {
    container_t c{eid_t{50}, allocation_policy::eager};
    CHECK((c.empty()));
    CHECK((c.capacity()) >= (50U));
    CHECK((c.id_limit()) == (eid_t{50}));
  }

  // set_id_limit() with eager prefill reserves capacity.
  if (true) {
    container_t c;
    CHECK((c.set_id_limit(eid_t{30}, allocation_policy::eager)));
    CHECK((c.capacity()) >= (30U));
    CHECK((c.id_limit()) == (eid_t{30}));
  }
}

#pragma endregion
#pragma region Iteration

TEST_CASE("IdContainer_Iteration", "[IdContainer]") {
  // Range-for iterates over all slots in index order.
  if (true) {
    container_t c;
    CHECK((c.push_back(10)));
    CHECK((c.push_back(20)));
    CHECK((c.push_back(30)));

    std::vector<int> got;
    for (auto v : c) got.push_back(v);
    CHECK((got.size()) == (3U));
    CHECK((got[0]) == (10));
    CHECK((got[1]) == (20));
    CHECK((got[2]) == (30));
  }

  // Empty container yields empty range.
  if (true) {
    container_t c;
    std::size_t count{};
    for ([[maybe_unused]] auto v : c) ++count;
    CHECK((count) == (0U));
  }

  // cbegin/cend match begin/end.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    CHECK((*c.cbegin()) == (*c.begin()));
    CHECK((c.cend() == c.end()));
  }

  // Mutable iteration allows modifying values.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    CHECK((c.push_back(2)));
    for (auto& v : c) v *= 10;
    CHECK((c[eid_t{0}]) == (10));
    CHECK((c[eid_t{1}]) == (20));
  }
}

#pragma endregion
#pragma region Underlying

TEST_CASE("IdContainer_Underlying", "[IdContainer]") {
  // underlying() returns the inner std::vector<T>.
  if (true) {
    container_t c;
    CHECK((c.push_back(5)));
    CHECK((c.push_back(6)));
    auto& vec = c.underlying();
    CHECK((vec.size()) == (2U));
    CHECK((vec[0]) == (5));
    CHECK((vec[1]) == (6));
  }

  // Mutations through underlying() are visible via operator[].
  if (true) {
    container_t c;
    CHECK((c.push_back(0)));
    c.underlying()[0] = 99;
    CHECK((c[eid_t{0}]) == (99));
  }
}

#pragma endregion
#pragma region Data

TEST_CASE("IdContainer_Data", "[IdContainer]") {
  // data() returns a pointer to the first element.
  if (true) {
    container_t c;
    CHECK((c.push_back(1)));
    CHECK((c.push_back(2)));
    const int* p = c.data();
    CHECK((p[0]) == (1));
    CHECK((p[1]) == (2));
  }
}

#pragma endregion
#pragma region Allocator

TEST_CASE("IdContainer_Allocator", "[IdContainer]") {
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
    CHECK((c.empty()));
  }

  // Constructor with limit and explicit allocator.
  if (true) {
    std::allocator<int> alloc;
    container_t c{eid_t{10}, allocation_policy::lazy, alloc};
    CHECK((c.id_limit() == eid_t{10}));
    CHECK((c.empty()));
  }
}

#pragma endregion
#pragma region NonIntType

TEST_CASE("IdContainer_NonIntType", "[IdContainer]") {
  // id_container works with non-int value types.
  if (true) {
    id_container<double, eid_t> c;
    CHECK((c.push_back(1.5)));
    CHECK((c.push_back(2.5)));
    CHECK((c.size()) == (2U));
    CHECK((c[eid_t{0}]) == (1.5));
    CHECK((c[eid_t{1}]) == (2.5));
  }

  if (true) {
    struct point_t {
      int x{};
      int y{};
    };
    id_container<point_t, eid_t> c;
    auto* ptr = c.emplace_back(point_t{3, 4});
    CHECK((ptr != nullptr));
    CHECK((c[eid_t{0}].x) == (3));
    CHECK((c[eid_t{0}].y) == (4));
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
