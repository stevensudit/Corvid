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
#include <memory>
#include <stdexcept>

#include "../corvid/ecs/entity_registry.h"
#include "minitest.h"

using namespace corvid;
using namespace corvid::bool_enums;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

void EntityRegistry_Basic() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{1}, 0};
  const loc_t loc1{store_id_t{1}, 1};
  const loc_t loc2{store_id_t{1}, 2};
  const loc_t loc_s1{store_id_t{2}, 0};

  // Default template parameters.
  if (true) {
    static_assert(std::is_same_v<reg_t::id_t, entity_id_t>);
    static_assert(std::is_same_v<reg_t::store_id_t, store_id_t>);
    static_assert(std::is_same_v<reg_t::size_type, size_t>);
    static_assert(std::is_same_v<reg_t::allocator_type, std::allocator<int>>);
  }

  // handle_t default construction.
  if (true) {
    using handle_t = reg_t::handle_t;
    handle_t h;
    EXPECT_EQ(h.id(), entity_id_t::invalid);
    EXPECT_EQ(h.gen(), *entity_id_t::invalid);
  }

  // handle_t copy construction and assignment.
  if (true) {
    using handle_t = reg_t::handle_t;
    handle_t h1;
    handle_t h2{h1};
    EXPECT_TRUE(h1 == h2);
    handle_t h3;
    h3 = h1;
    EXPECT_TRUE(h1 == h3);
  }

  // handle_t comparison operators.
  if (true) {
    using handle_t = reg_t::handle_t;
    handle_t h1;
    handle_t h2;
    EXPECT_TRUE(h1 == h2);
    EXPECT_FALSE(h1 != h2);
    EXPECT_FALSE(h1 < h2);
    EXPECT_TRUE(h1 <= h2);
    EXPECT_FALSE(h1 > h2);
    EXPECT_TRUE(h1 >= h2);
  }

  // Default construction: empty registry.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.id_limit(), id_t::invalid);
    EXPECT_FALSE(r.is_valid(id_t{0}));
  }

  // Allocator constructor.
  if (true) {
    reg_t r{std::allocator<int>{}};
    EXPECT_FALSE(r.is_valid(id_t{0}));
  }

  // create returns sequential IDs.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc1, 20);
    auto id2 = r.create_id(loc2, 30);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(*id2, 2U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_TRUE(r.is_valid(id2));
  }

  // create with defaulted store_id works.
  if (true) {
    reg_t r;
    auto id = r.create_id();
    EXPECT_NE(id, id_t::invalid);
  }

  // create stores metadata.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    auto id1 = r.create_id(loc1, 99);
    EXPECT_EQ(r[id0], 42);
    EXPECT_EQ(r[id1], 99);
  }

  // Mutable metadata access via operator[].
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    r[id0] = 777;
    EXPECT_EQ(r[id0], 777);
  }

  // Const metadata access via operator[].
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    const auto& cr = r;
    EXPECT_EQ(cr[id0], 42);
  }

  // create stores location.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc_s1, 10);
    const auto& loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 2U);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // set_location by ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    r.set_location(id0, loc_s1);
    const auto& loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 2U);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // set_location to invalid erases the entity.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    EXPECT_TRUE(r.is_valid(id0));
    r.set_location(id0, loc_t{});
    EXPECT_FALSE(r.is_valid(id0));
  }

  // erase by ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc1, 20);
    EXPECT_TRUE(r.erase(id0));
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    // Double erase fails.
    EXPECT_FALSE(r.erase(id0));
  }

  // erase invalid ID returns false.
  if (true) {
    reg_t r;
    EXPECT_FALSE(r.erase(id_t{0}));
    EXPECT_FALSE(r.erase(id_t::invalid));
  }

  // erase_if removes matching entities.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 5);
    (void)r.create_id(loc1, 15);
    (void)r.create_id(loc2, 25);
    auto cnt = r.erase_if([](auto, auto& rec) { return rec.metadata > 10; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{1}));
    EXPECT_FALSE(r.is_valid(id_t{2}));
  }

  // at(id_t) success.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    EXPECT_EQ(r.at(id0), 42);
    r.at(id0) = 99;
    EXPECT_EQ(r.at(id0), 99);
  }

  // at(id_t) const.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    const auto& cr = r;
    EXPECT_EQ(cr.at(id0), 42);
  }

  // at(id_t) throws for invalid ID.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10);
    EXPECT_THROW(r.at(id_t{99}), std::out_of_range);
  }

  // at(id_t) throws for erased ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    r.erase(id0);
    EXPECT_THROW(r.at(id0), std::out_of_range);
  }

  // size() tracks living entities.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.size(), 0U);
    auto id0 = r.create_id(loc0, 10);
    EXPECT_EQ(r.size(), 1U);
    (void)r.create_id(loc1, 20);
    EXPECT_EQ(r.size(), 2U);
    r.erase(id0);
    EXPECT_EQ(r.size(), 1U);
  }

  // max_id() is the high-water mark.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc1, 20); // id 1
    (void)r.create_id(loc2, 30); // id 2
    EXPECT_EQ(r.max_id(), id_t{2});
    r.erase(id_t{2});
    EXPECT_EQ(r.max_id(), id_t{2}); // still 2, high-water mark
  }
}

void EntityRegistry_Handle() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{1}, 0};
  const loc_t loc1{store_id_t{1}, 1};

  // create_with_handle returns a valid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t{0});
    EXPECT_EQ(h.gen(), 0U);
  }

  // create_with_handle with defaulted store_id returns valid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle({}, 42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_NE(h.id(), id_t::invalid);
  }

  // get_handle for valid ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10);
    auto h = r.get_handle(id0);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id0);
    EXPECT_EQ(h.gen(), 0U);
  }

  // get_handle for invalid ID returns invalid handle.
  if (true) {
    reg_t r;
    auto h = r.get_handle(id_t{99});
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t::invalid);
  }

  // Handle is invalidated after erase.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10);
    auto h = r.get_handle(id0);
    r.erase(id0);
    EXPECT_FALSE(r.is_valid(h));
  }

  // Stale handle is invalid even after ID reuse (gen mismatch).
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10);
    auto h_old = r.get_handle(id0);
    EXPECT_EQ(h_old.gen(), 0U);
    r.erase(id0);
    auto id0_reused = r.create_id({}, 99);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_FALSE(r.is_valid(h_old));
    auto h_new = r.get_handle(id0_reused);
    EXPECT_TRUE(r.is_valid(h_new));
    EXPECT_GT(h_new.gen(), h_old.gen());
  }

  // erase by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle({}, 10);
    EXPECT_TRUE(r.erase(h));
    EXPECT_FALSE(r.is_valid(h));
    // Double erase by handle fails.
    EXPECT_FALSE(r.erase(h));
  }

  // Metadata access by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle({}, 42);
    EXPECT_EQ(r.at(h), 42);
    r.at(h) = 100;
    EXPECT_EQ(r.at(h), 100);
    const auto& cr = r;
    EXPECT_EQ(cr.at(h), 100);
  }

  // Metadata access by invalid handle throws.
  if (true) {
    reg_t r;
    auto h = r.create_handle({}, 42);
    r.erase(h);
    EXPECT_THROW(r.at(h), std::invalid_argument);
  }

  // get_location by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle({}, 10);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 0U);
    EXPECT_EQ(loc.ndx, *id_t::invalid);
  }

  // set_location by handle.
  if (true) {
    reg_t r;
    loc_t loc_new{store_id_t{2}, 5};
    auto h = r.create_handle({}, 10);
    r.set_location(h, loc_new);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 2U);
    EXPECT_EQ(loc.ndx, 5U);
  }

  // set_location by invalid handle is a no-op.
  if (true) {
    reg_t r;
    auto h = r.create_handle({}, 10);
    r.erase(h);
    r.set_location(h, loc1); // Should not crash.
  }

  // Handle comparison: handles from different IDs compare by ID.
  if (true) {
    reg_t r;
    auto h0 = r.create_handle(loc0, 10);
    auto h1 = r.create_handle(loc1, 20);
    EXPECT_TRUE(h0 != h1);
    EXPECT_TRUE(h0 < h1);
  }

  // Handle comparison: same ID, different gen compares unequal.
  if (true) {
    reg_t r;
    auto h_old = r.create_handle(loc0, 10);
    r.erase(h_old);
    auto h_new = r.create_handle(loc0, 20);
    EXPECT_EQ(h_old.id(), h_new.id());
    EXPECT_TRUE(h_old != h_new);
  }

  // get_location by invalid handle returns invalid location.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.erase(h);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(loc.store_id, store_id_t::invalid);
  }

  // set_location by handle to invalid erases the entity.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    EXPECT_TRUE(r.is_valid(h));
    r.set_location(h, loc_t{});
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(r.size(), 0U);
  }

  // at(handle_t) const.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    const auto& cr = r;
    EXPECT_EQ(cr.at(h), 42);
  }

  // at(handle_t) const throws for invalid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    r.erase(h);
    const auto& cr = r;
    EXPECT_THROW(cr.at(h), std::invalid_argument);
  }
}

void EntityRegistry_Fifo() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // Freed IDs are reused in FIFO order (oldest first).
  // Detailed FIFO behavior is tested in StableId_Fifo.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    (void)r.create_id({}, 30); // id 2
    r.erase(id0);              // free: [0]
    r.erase(id1);              // free: [0, 1]
    // FIFO: 0 was freed first, so it's reused first.
    EXPECT_EQ(r.create_id({}, 100), id_t{0});
    EXPECT_EQ(r.create_id({}, 200), id_t{1});
  }
}

void EntityRegistry_Clear() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // clear() without shrink: IDs reusable, gens bumped.
  // Detailed clear behavior is tested in StableId_Basic and StableId_Fifo.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    auto h0 = r.get_handle(id_t{0});
    r.clear();
    EXPECT_FALSE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(h0));
    EXPECT_EQ(r.create_id({}, 100), id_t{0});
    EXPECT_EQ(r.get_handle(id_t{0}).gen(), 1U);
  }

  // clear(true) with shrink: fully resets storage and gens.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    r.clear(deallocation_policy::release);
    EXPECT_EQ(r.size(), 0U);
    EXPECT_FALSE(r.is_valid(id_t{0}));
    auto id0 = r.create_id({}, 100);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(r.get_handle(id0).gen(), 0U); // gen reset
  }
}

void EntityRegistry_Reserve() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // reserve without prefill: just reserves capacity, no IDs allocated.
  if (true) {
    reg_t r;
    r.reserve(10);
    EXPECT_FALSE(r.is_valid(id_t{0}));
    auto id0 = r.create_id({}, 42);
    EXPECT_EQ(*id0, 0U);
  }

  // reserve with prefill: pre-creates free slots.
  if (true) {
    reg_t r;
    r.reserve(5, allocation_policy::eager);
    // Slots exist but are not valid (free).
    EXPECT_FALSE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{4}));
    // create reuses prefilled slots in order.
    EXPECT_EQ(r.create_id({}, 10), id_t{0});
    EXPECT_EQ(r.create_id({}, 20), id_t{1});
    EXPECT_EQ(r.create_id({}, 30), id_t{2});
    EXPECT_EQ(r.create_id({}, 40), id_t{3});
    EXPECT_EQ(r.create_id({}, 50), id_t{4});
  }

  // Prefill constructor.
  if (true) {
    reg_t r{id_t{5}, allocation_policy::eager};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id({}, 10), id_t{0});
    EXPECT_EQ(r.create_id({}, 20), id_t{1});
  }

  // shrink_to_fit trims trailing dead records.
  // Detailed shrink behavior is tested in StableId_Basic and StableId_Fifo.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.erase(id_t{2});
    r.shrink_to_fit();
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_TRUE(r.is_valid(id_t{1}));
  }
}

void EntityRegistry_IdLimit() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // Constructor with limit and overflow.
  // Detailed id_limit behavior is tested in StableId_MaxId.
  if (true) {
    reg_t r{id_t{3}};
    EXPECT_EQ(r.id_limit(), id_t{3});
    EXPECT_EQ(r.create_id({}, 10), id_t{0});
    EXPECT_EQ(r.create_id({}, 20), id_t{1});
    EXPECT_EQ(r.create_id({}, 30), id_t{2});
    // 4th creation fails.
    EXPECT_EQ(r.create_id({}, 40), id_t::invalid);
  }

  // set_id_limit on empty registry.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.id_limit(), id_t::invalid);
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t{2});
    (void)r.create_id({}, 10);
    (void)r.create_id({}, 20);
    EXPECT_EQ(r.create_id({}, 30), id_t::invalid);
  }
}

void EntityRegistry_NoGen() {
  using namespace id_enums;
  using reg_t = entity_registry<int, entity_id_t, store_id_t,
      generation_scheme::unversioned>;
  using id_t = reg_t::id_t;
  using handle_t = reg_t::handle_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};
  const loc_t loc1{store_id_t{0}, 1};

  // handle_t has no get_gen() when UseGen=false.
  // Detailed no-gen behavior is tested in StableId_NoGen and
  // StableId_FifoNoGen.
  if (true) { static_assert(sizeof(reg_t::handle_t) == sizeof(id_t)); }

  // Basic create and access.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(r[id0], 10);
    EXPECT_EQ(r[id1], 20);
  }

  // create_with_handle and handle validity.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t{0});
    EXPECT_EQ(r.at(h), 42);
  }

  // create_with_handle with defaulted store_id returns valid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle({}, 42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_NE(h.id(), id_t::invalid);
  }

  // erase by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    EXPECT_TRUE(r.erase(h));
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_FALSE(r.erase(h)); // double erase
  }

  // get_location and set_location by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 0U);
    EXPECT_EQ(loc.ndx, 0U);
    r.set_location(h, loc1);
    EXPECT_EQ(r.get_location(h).ndx, 1U);
  }

  // get_location by invalid handle returns invalid location.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.erase(h);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(loc.store_id, store_id_t::invalid);
  }

  // set_location by invalid handle is a no-op.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.erase(h);
    r.set_location(h, loc1); // should not crash
  }

  // set_location by handle to invalid erases the entity.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.set_location(h, loc_t{});
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(r.size(), 0U);
  }

  // Handle comparison.
  if (true) {
    reg_t r;
    auto h0 = r.create_handle(loc0, 10);
    auto h1 = r.create_handle(loc1, 20);
    EXPECT_TRUE(h0 != h1);
    EXPECT_TRUE(h0 < h1);
    EXPECT_TRUE(h0 == h0);
  }

  // Without gen, stale handles are falsely valid after ID reuse.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10);
    auto h_old = r.get_handle(id0);
    r.erase(id0);
    EXPECT_FALSE(r.is_valid(h_old));
    auto id0_reused = r.create_id({}, 99);
    EXPECT_EQ(id0_reused, id0);
    // Without gen protection, old handle becomes valid again.
    EXPECT_TRUE(r.is_valid(h_old));
  }

  // Handle default construction.
  if (true) {
    handle_t h;
    EXPECT_EQ(h.id(), id_t::invalid);
  }

  // Handle copy construction and assignment.
  if (true) {
    handle_t h1;
    handle_t h2{h1};
    EXPECT_TRUE(h1 == h2);
    handle_t h3;
    h3 = h1;
    EXPECT_TRUE(h1 == h3);
  }

  // at(handle_t) throws for invalid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    r.erase(h);
    EXPECT_THROW(r.at(h), std::invalid_argument);
  }

  // at(handle_t) const.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    const auto& cr = r;
    EXPECT_EQ(cr.at(h), 42);
  }
}

void EntityRegistry_VoidMeta() {
  using namespace id_enums;
  using reg_t = entity_registry<void>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{1}, 0};
  const loc_t loc1{store_id_t{1}, 1};

  // Create and validate without metadata.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0);
    auto id1 = r.create_id(loc1);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
  }

  // Erase and FIFO reuse.
  if (true) {
    reg_t r;
    (void)r.create_id(); // id 0
    (void)r.create_id(); // id 1
    (void)r.create_id(); // id 2
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.create_id(), id_t{0});
    EXPECT_EQ(r.create_id(), id_t{1});
  }

  // Handles work with void metadata.
  if (true) {
    reg_t r;
    auto h = r.create_handle({});
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.gen(), 0U);
    r.erase(h);
    EXPECT_FALSE(r.is_valid(h));
  }

  // Location operations.
  if (true) {
    reg_t r;
    auto id0 = r.create_id();
    const auto& loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 0U);
    EXPECT_EQ(loc.ndx, *id_t::invalid);
    r.set_location(id0, loc1);
    const auto& loc_after = r.get_location(id0);
    EXPECT_EQ(loc_after.ndx, 1U);
  }

  // clear and reserve with prefill.
  if (true) {
    reg_t r;
    r.reserve(5, allocation_policy::eager);
    EXPECT_EQ(r.create_id(), id_t{0});
    EXPECT_EQ(r.create_id(), id_t{1});
    r.clear();
    EXPECT_EQ(r.create_id(), id_t{0});
  }

  // erase_if with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(); // id 0
    (void)r.create_id(); // id 1
    (void)r.create_id(); // id 2
    auto cnt = r.erase_if([](auto, auto& rec) {
      return rec.location.ndx() == *id_t::invalid;
    });
    // All have ndx *id_t::invalid, so all erased.
    EXPECT_EQ(cnt, 3U);
    EXPECT_EQ(r.size(), 0U);
  }

  // shrink_to_fit with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(); // id 0
    (void)r.create_id(); // id 1
    (void)r.create_id(); // id 2
    r.erase(id_t{2});
    r.shrink_to_fit();
    EXPECT_EQ(r.max_id(), id_t{1});
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_TRUE(r.is_valid(id_t{1}));
  }

  // set_id_limit with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(); // id 0
    (void)r.create_id(); // id 1
    (void)r.create_id(); // id 2
    r.erase(id_t{2});
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    // id 2 was trimmed by set_id_limit, and limit prevents new id >= 2.
    EXPECT_EQ(r.id_limit(), id_t{2});
    EXPECT_EQ(r.size(), 2U);
  }

  // Handle with generation tracking (void metadata).
  if (true) {
    reg_t r;
    auto id0 = r.create_id();
    auto h_old = r.get_handle(id0);
    EXPECT_EQ(h_old.gen(), 0U);
    r.erase(id0);
    auto id0_reused = r.create_id();
    EXPECT_EQ(id0_reused, id0);
    auto h_new = r.get_handle(id0_reused);
    EXPECT_EQ(h_new.gen(), 1U);
    EXPECT_FALSE(r.is_valid(h_old));
    EXPECT_TRUE(r.is_valid(h_new));
  }
}

void EntityRegistry_VoidNoGen() {
  using namespace id_enums;
  using reg_t = entity_registry<void, entity_id_t, store_id_t,
      generation_scheme::unversioned>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Minimal footprint: no metadata, no gen.
  if (true) { static_assert(sizeof(reg_t::handle_t) == sizeof(id_t)); }

  // Create and validate.
  if (true) {
    reg_t r;
    auto id0 = r.create_id();
    auto id1 = r.create_id();
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_TRUE(r.is_valid(id0));
  }

  // Handle operations.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_TRUE(r.erase(h));
    EXPECT_FALSE(r.is_valid(h));
  }

  // Erase and FIFO reuse.
  if (true) {
    reg_t r;
    (void)r.create_id(); // id 0
    (void)r.create_id(); // id 1
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.create_id(), id_t{0});
    EXPECT_EQ(r.create_id(), id_t{1});
  }
}

void EntityRegistry_IdLimitAdvanced() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // set_id_limit fails when live IDs exist at or past the new limit.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    EXPECT_FALSE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t::invalid); // unchanged
  }

  // set_id_limit succeeds when only dead IDs exist past the limit; triggers
  // shrink.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.erase(id_t{2});
    EXPECT_EQ(r.max_id(), id_t{2});
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t{2});
    EXPECT_EQ(r.max_id(), id_t{1}); // trimmed
  }

  // Raising the limit always succeeds.
  if (true) {
    reg_t r{id_t{3}};
    (void)r.create_id({}, 10);
    (void)r.create_id({}, 20);
    (void)r.create_id({}, 30);
    EXPECT_EQ(r.create_id({}, 40), id_t::invalid); // at limit
    EXPECT_TRUE(r.set_id_limit(id_t{5}));
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id({}, 40), id_t{3}); // now succeeds
  }

  // set_id_limit on empty registry with freed slots beyond limit.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.size(), 0U);
    EXPECT_TRUE(r.set_id_limit(id_t{1}));
    EXPECT_EQ(r.id_limit(), id_t{1});
    // Only id 0 should be available for reuse.
    EXPECT_EQ(r.create_id({}, 100), id_t{0});
    EXPECT_EQ(r.create_id({}, 200), id_t::invalid); // at limit
  }
}

void EntityRegistry_FifoAdvanced() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // Interleaved erase/create: each create pops the oldest free.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10);      // id 0
    (void)r.create_id({}, 20);      // id 1
    (void)r.create_id({}, 30);      // id 2
    r.erase(id_t{0});               // free: [0]
    r.erase(id_t{1});               // free: [0, 1]
    auto r0 = r.create_id({}, 100); // pops 0; free: [1]
    EXPECT_EQ(r0, id_t{0});
    r.erase(id_t{2});               // free: [1, 2]
    auto r1 = r.create_id({}, 200); // pops 1; free: [2]
    EXPECT_EQ(r1, id_t{1});
    auto r2 = r.create_id({}, 300); // pops 2; free: []
    EXPECT_EQ(r2, id_t{2});
    // All live; next gets fresh ID.
    EXPECT_EQ(r.create_id({}, 400), id_t{3});
  }

  // FIFO reuse order matches erase order, not ID order.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    (void)r.create_id({}, 40); // id 3
    (void)r.create_id({}, 50); // id 4
    r.erase(id_t{2});
    r.erase(id_t{0});
    r.erase(id_t{3});
    EXPECT_EQ(r.create_id({}, 100), id_t{2});
    EXPECT_EQ(r.create_id({}, 200), id_t{0});
    EXPECT_EQ(r.create_id({}, 300), id_t{3});
  }

  // Free all then re-create: FIFO order matches erase order.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.erase(id_t{2});
    r.erase(id_t{1});
    r.erase(id_t{0});
    EXPECT_TRUE(r.size() == 0U);
    EXPECT_EQ(r.create_id({}, 100), id_t{2});
    EXPECT_EQ(r.create_id({}, 200), id_t{1});
    EXPECT_EQ(r.create_id({}, 300), id_t{0});
  }

  // FIFO order after clear() without shrink: rebuild scans in order.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.clear();
    EXPECT_EQ(r.create_id({}, 100), id_t{0});
    EXPECT_EQ(r.create_id({}, 200), id_t{1});
    EXPECT_EQ(r.create_id({}, 300), id_t{2});
  }
}

void EntityRegistry_LifoAdvanced() {
  using namespace id_enums;
  using reg_t = entity_registry<int, entity_id_t, store_id_t,
      generation_scheme::versioned, 1, reuse_order::lifo>;
  using id_t = reg_t::id_t;

  static_assert(!reg_t::is_fifo_v);
  static_assert(reg_t::is_lifo_v);

  // LIFO reuse: most recently freed is reallocated first.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.erase(id_t{0});          // stack: [0]
    r.erase(id_t{1});          // stack: [1, 0]
    // pops 1 (most recently freed), then 0
    EXPECT_EQ(r.create_id({}, 100), id_t{1});
    EXPECT_EQ(r.create_id({}, 200), id_t{0});
    // All live; next gets fresh ID.
    EXPECT_EQ(r.create_id({}, 300), id_t{3});
  }

  // LIFO erase order preserved across interleaving.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    (void)r.create_id({}, 40); // id 3
    (void)r.create_id({}, 50); // id 4
    r.erase(id_t{2});
    r.erase(id_t{0});
    r.erase(id_t{3});
    // LIFO pops in reverse erase order: 3, 0, 2
    EXPECT_EQ(r.create_id({}, 100), id_t{3});
    EXPECT_EQ(r.create_id({}, 200), id_t{0});
    EXPECT_EQ(r.create_id({}, 300), id_t{2});
  }

  // After clear(), free list is rebuilt in LIFO-scan order (highest ID first).
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.clear();
    // Scanning forward and pushing to head yields reverse order: 2, 1, 0.
    EXPECT_EQ(r.create_id({}, 100), id_t{2});
    EXPECT_EQ(r.create_id({}, 200), id_t{1});
    EXPECT_EQ(r.create_id({}, 300), id_t{0});
  }
}

void EntityRegistry_EdgeCases() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // max_id() on empty registry returns invalid.
  if (true) {
    reg_t r;
    // records_.size() is 0, so size()-1 underflows to max value = invalid.
    EXPECT_EQ(r.max_id(), id_t::invalid);
  }

  // create with default metadata parameter.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(); // uses default metadata_t{}
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_EQ(r[id0], 0); // default-initialized int
  }

  // erase_if with no matches returns 0.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 5);
    (void)r.create_id({}, 10);
    auto cnt = r.erase_if([](auto, auto& rec) { return rec.metadata > 100; });
    EXPECT_EQ(cnt, 0U);
    EXPECT_EQ(r.size(), 2U);
  }

  // erase_if on empty registry.
  if (true) {
    reg_t r;
    auto cnt = r.erase_if([](auto, auto&) { return true; });
    EXPECT_EQ(cnt, 0U);
  }

  // shrink_to_fit with interior dead: only trims trailing dead.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.erase(id_t{1});          // interior dead
    r.shrink_to_fit();
    // id 2 is still live so no trimming happens.
    EXPECT_EQ(r.max_id(), id_t{2});
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{1}));
    EXPECT_TRUE(r.is_valid(id_t{2}));
  }

  // shrink_to_fit trims multiple trailing dead records.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    (void)r.create_id({}, 40); // id 3
    r.erase(id_t{2});
    r.erase(id_t{3});
    r.shrink_to_fit();
    EXPECT_EQ(r.max_id(), id_t{1}); // trimmed to 2 records
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_TRUE(r.is_valid(id_t{1}));
  }

  // shrink_to_fit on empty registry.
  if (true) {
    reg_t r;
    r.shrink_to_fit(); // should not crash
  }

  // reserve without prefill does not affect create order.
  if (true) {
    reg_t r;
    r.reserve(100);
    EXPECT_EQ(r.size(), 0U);
    EXPECT_EQ(r.create_id({}, 10), id_t{0});
    EXPECT_EQ(r.create_id({}, 20), id_t{1});
  }

  // Prefill constructor with prefill=false: just sets limit, no slots.
  if (true) {
    reg_t r{id_t{5}, allocation_policy::lazy};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id({}, 10), id_t{0});
    EXPECT_EQ(r.create_id({}, 20), id_t{1});
  }

  // clear() bumps generation counters.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10);
    auto h = r.get_handle(id0);
    EXPECT_EQ(h.gen(), 0U);
    r.clear();
    EXPECT_FALSE(r.is_valid(h));
    auto id0_new = r.create_id({}, 20);
    EXPECT_EQ(id0_new, id0);
    auto h_new = r.get_handle(id0_new);
    EXPECT_EQ(h_new.gen(), 1U);
    EXPECT_TRUE(h != h_new);
  }

  // Multiple erase/reuse cycles bump gen each time.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10);
    auto h0 = r.get_handle(id0);
    EXPECT_EQ(h0.gen(), 0U);
    r.erase(id0);
    auto id0_r1 = r.create_id({}, 20);
    EXPECT_EQ(id0_r1, id0);
    auto h1 = r.get_handle(id0_r1);
    EXPECT_EQ(h1.gen(), 1U);
    r.erase(id0_r1);
    auto id0_r2 = r.create_id({}, 30);
    EXPECT_EQ(id0_r2, id0);
    auto h2 = r.get_handle(id0_r2);
    EXPECT_EQ(h2.gen(), 2U);
    EXPECT_FALSE(r.is_valid(h0));
    EXPECT_FALSE(r.is_valid(h1));
    EXPECT_TRUE(r.is_valid(h2));
  }

  // FIFO free-list rebuilt correctly after shrink_to_fit.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    (void)r.create_id({}, 40); // id 3
    (void)r.create_id({}, 50); // id 4
    r.erase(id_t{3});
    r.erase(id_t{4});
    r.erase(id_t{0});
    // Live: 1, 2. Trailing dead: 3, 4. Interior dead: 0.
    r.shrink_to_fit();
    // After shrink: records 0..2; 3 and 4 trimmed.
    // Free list rebuilt: only id 0 is free.
    EXPECT_EQ(r.size(), 2U);
    EXPECT_EQ(r.create_id({}, 100), id_t{0});
    // No more free; next gets fresh id 3.
    EXPECT_EQ(r.create_id({}, 200), id_t{3});
  }

  // size() and is_valid consistency after mixed operations.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.size(), 0U);
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    auto id2 = r.create_id({}, 30);
    EXPECT_EQ(r.size(), 3U);
    r.erase(id1);
    EXPECT_EQ(r.size(), 2U);
    auto id3 = r.create_id({}, 40); // reuses id 1
    EXPECT_EQ(r.size(), 3U);
    EXPECT_EQ(id3, id1);
    r.erase(id0);
    r.erase(id2);
    r.erase(id3);
    EXPECT_EQ(r.size(), 0U);
  }

  // Constructor with id_limit and custom allocator.
  if (true) {
    std::allocator<int> alloc;
    reg_t r{id_t{5}, allocation_policy::eager, alloc};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id({}, 10), id_t{0});
  }

  // Prefill constructor with prefill=false: just sets limit.
  if (true) {
    reg_t r{id_t::invalid, allocation_policy::lazy};
    EXPECT_EQ(r.id_limit(), id_t::invalid);
  }

  // Prefill constructor with id_limit{0}: early return, can't create.
  if (true) {
    reg_t r{id_t{0}};
    EXPECT_EQ(r.id_limit(), id_t{0});
    EXPECT_EQ(r.create_id({}, 10), id_t::invalid);
  }

  // set_id_limit to 0: no IDs allowed.
  if (true) {
    reg_t r;
    EXPECT_TRUE(r.set_id_limit(id_t{0}));
    EXPECT_EQ(r.id_limit(), id_t{0});
    EXPECT_EQ(r.create_id({}, 10), id_t::invalid);
  }

  // set_id_limit to same value: no-op.
  if (true) {
    reg_t r{id_t{3}};
    (void)r.create_id({}, 10);
    EXPECT_TRUE(r.set_id_limit(id_t{3}));
    EXPECT_EQ(r.id_limit(), id_t{3});
    EXPECT_TRUE(r.is_valid(id_t{0}));
  }

  // erase_if erases all when all match.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 5);
    (void)r.create_id({}, 10);
    (void)r.create_id({}, 15);
    auto cnt = r.erase_if([](auto, auto&) { return true; });
    EXPECT_EQ(cnt, 3U);
    EXPECT_EQ(r.size(), 0U);
  }

  // shrink_to_fit on all-dead registry trims to empty.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10);
    (void)r.create_id({}, 20);
    r.erase(id_t{0});
    r.erase(id_t{1});
    r.shrink_to_fit();
    EXPECT_EQ(r.size(), 0U);
    // After full trim, fresh IDs start from 0.
    EXPECT_EQ(r.create_id({}, 100), id_t{0});
  }
}

void EntityRegistry_MetadataCleanup() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // Metadata is cleared on erase; re-creation with default gets 0, not stale.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 42);
    EXPECT_EQ(r[id0], 42);
    r.erase(id0);
    auto id0_reused = r.create_id();
    EXPECT_EQ(id0_reused, id0);
    EXPECT_EQ(r[id0_reused], 0);
  }

  // Metadata is cleared on erase even when re-created with explicit value.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 999);
    r.erase(id0);
    auto id0_reused = r.create_id({}, 7);
    EXPECT_EQ(r[id0_reused], 7);
  }

  // Metadata is cleared on clear(false); re-creation with default gets 0.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 100);
    (void)r.create_id({}, 200);
    (void)r.create_id({}, 300);
    r.clear();
    EXPECT_EQ(r.create_id({}, 0), id_t{0});
    EXPECT_EQ(r[id_t{0}], 0);
    EXPECT_EQ(r.create_id({}, 0), id_t{1});
    EXPECT_EQ(r[id_t{1}], 0);
    EXPECT_EQ(r.create_id({}, 0), id_t{2});
    EXPECT_EQ(r[id_t{2}], 0);
  }

  // Metadata is cleared on clear(true); re-creation with default gets 0.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 100);
    (void)r.create_id({}, 200);
    r.clear(deallocation_policy::release);
    auto id0 = r.create_id();
    EXPECT_EQ(r[id0], 0);
  }
}

void EntityRegistry_EraseIfPredicate() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // Predicate is only called for valid (live) records, not dead ones.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    (void)r.create_id({}, 40); // id 3
    r.erase(id_t{1});
    r.erase(id_t{3});
    // 2 live, 2 dead. Predicate should be called exactly twice.
    size_t call_count = 0;
    auto cnt = r.erase_if([&](auto, auto& rec) {
      ++call_count;
      return rec.metadata > 20;
    });
    EXPECT_EQ(call_count, 2U); // only live records
    EXPECT_EQ(cnt, 1U);        // only id 2 (metadata 30) matches
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{2}));
  }
}

void EntityRegistry_IdLimitFreeList() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // Reducing limit trims free-list entries while interior live records remain.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    (void)r.create_id({}, 40); // id 3
    (void)r.create_id({}, 50); // id 4
    r.erase(id_t{1});          // interior dead
    r.erase(id_t{3});          // will be trimmed
    r.erase(id_t{4});          // will be trimmed
    // Live: 0, 2. Dead: 1, 3, 4. Trim to limit 3 removes 3 and 4.
    EXPECT_TRUE(r.set_id_limit(id_t{3}));
    EXPECT_EQ(r.size(), 2U);
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{1}));
    EXPECT_TRUE(r.is_valid(id_t{2}));
    // Free list should only contain id 1.
    EXPECT_EQ(r.create_id({}, 60), id_t{1});
    // At limit now: 3 live, limit is 3.
    EXPECT_EQ(r.create_id({}, 70), id_t::invalid);
  }
}

void EntityRegistry_ReservePrefillExisting() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;

  // reserve with prefill on a registry that already has live entities.
  if (true) {
    reg_t r;
    auto id0 = r.create_id({}, 10); // id 0
    auto id1 = r.create_id({}, 20); // id 1
    EXPECT_EQ(r.size(), 2U);
    r.reserve(5, allocation_policy::eager); // adds free slots 2, 3, 4
    // Existing entities are undisturbed.
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_EQ(r[id0], 10);
    EXPECT_EQ(r[id1], 20);
    // New creates should use the prefilled free slots.
    EXPECT_EQ(r.create_id({}, 30), id_t{2});
    EXPECT_EQ(r.create_id({}, 40), id_t{3});
    EXPECT_EQ(r.create_id({}, 50), id_t{4});
    // Next create expands.
    EXPECT_EQ(r.create_id({}, 60), id_t{5});
  }

  // reserve with prefill when some slots are already free.
  if (true) {
    reg_t r;
    (void)r.create_id({}, 10);              // id 0
    (void)r.create_id({}, 20);              // id 1
    (void)r.create_id({}, 30);              // id 2
    r.erase(id_t{1});                       // free: [1]
    r.reserve(5, allocation_policy::eager); // adds free slots 3, 4
    // Free list should be: 1 (existing), then 3, 4 (new).
    EXPECT_EQ(r.create_id({}, 40), id_t{1});
    EXPECT_EQ(r.create_id({}, 50), id_t{3});
    EXPECT_EQ(r.create_id({}, 60), id_t{4});
  }
}

void EntityRegistry_HandleOwner() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using owner_t = reg_t::handle_owner;
  const loc_t staging{store_id_t{}};

  // Default-constructed owner holds no entity.
  if (true) {
    owner_t o;
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
  }

  // Constructor from existing valid handle takes ownership.
  if (true) {
    reg_t r;
    auto h = r.create_handle(staging, 42);
    owner_t o{r, h};
    EXPECT_TRUE(bool(o));
    EXPECT_EQ(o.id(), h.id());
    EXPECT_TRUE(o.handle() == h);
  }

  // Constructor from existing invalid handle: owner is empty.
  if (true) {
    reg_t r;
    auto h = r.create_handle(staging, 42);
    r.erase(h);
    owner_t o{r, h};
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
  }

  // Creating constructor: creates entity and takes ownership.
  if (true) {
    reg_t r;
    owner_t o{r, staging, 99};
    EXPECT_TRUE(bool(o));
    EXPECT_NE(o.id(), id_t::invalid);
    EXPECT_EQ(r[o.id()], 99);
    EXPECT_TRUE(r.is_valid(o.id()));
  }

  // create_owner factory method.
  if (true) {
    reg_t r;
    auto o = r.create_owner(staging, 77);
    EXPECT_TRUE(bool(o));
    EXPECT_NE(o.id(), id_t::invalid);
    EXPECT_EQ(r[o.id()], 77);
  }

  // Creating constructor: failure (registry at limit) -> owner is empty.
  if (true) {
    reg_t r{id_t{0}};
    owner_t o{r, staging, 10};
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
  }

  // Destructor erases entity when owner goes out of scope.
  if (true) {
    reg_t r;
    id_t saved_id;
    {
      auto o = r.create_owner(staging, 10);
      saved_id = o.id();
      EXPECT_TRUE(r.is_valid(saved_id));
    }
    EXPECT_FALSE(r.is_valid(saved_id));
  }

  // Destructor on empty owner is a no-op.
  if (true) {
    owner_t o; // destructs without touching any registry
  }

  // release() returns handle, leaves entity alive, and empties owner.
  if (true) {
    reg_t r;
    auto o = r.create_owner(staging, 10);
    auto id = o.id();
    auto h = o.release();
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
    EXPECT_EQ(h.id(), id);
    EXPECT_TRUE(r.is_valid(h));
    r.erase(h); // manual cleanup
  }

  // reset() erases entity and empties owner.
  if (true) {
    reg_t r;
    auto o = r.create_owner(staging, 10);
    auto id = o.id();
    o.reset();
    EXPECT_FALSE(bool(o));
    EXPECT_FALSE(r.is_valid(id));
  }

  // reset() on empty owner is a no-op.
  if (true) {
    owner_t o;
    o.reset();
    EXPECT_FALSE(bool(o));
  }

  // Move constructor transfers ownership; source becomes empty.
  if (true) {
    reg_t r;
    auto o1 = r.create_owner(staging, 10);
    auto id = o1.id();
    owner_t o2{std::move(o1)};
    EXPECT_FALSE(bool(o1));
    EXPECT_EQ(o1.id(), id_t::invalid);
    EXPECT_TRUE(bool(o2));
    EXPECT_EQ(o2.id(), id);
    EXPECT_TRUE(r.is_valid(id));
  }

  // Move assignment: erases current entity, takes ownership of source.
  if (true) {
    reg_t r;
    auto o1 = r.create_owner(staging, 10);
    auto o2 = r.create_owner(staging, 20);
    auto id1 = o1.id();
    auto id2 = o2.id();
    o2 = std::move(o1);
    EXPECT_FALSE(bool(o1));
    EXPECT_TRUE(bool(o2));
    EXPECT_EQ(o2.id(), id1);
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2)); // erased by move assignment
  }

  // Move assignment into empty owner.
  if (true) {
    reg_t r;
    auto o1 = r.create_owner(staging, 10);
    auto id = o1.id();
    owner_t o2;
    o2 = std::move(o1);
    EXPECT_FALSE(bool(o1));
    EXPECT_TRUE(bool(o2));
    EXPECT_EQ(o2.id(), id);
  }

  // Move self-assignment is a no-op (entity stays intact).
  if (true) {
    reg_t r;
    auto o = r.create_owner(staging, 10);
    auto id = o.id();
    // Use pointer indirection to avoid -Wself-move.
    owner_t* p = &o;
    o = std::move(*p);
    EXPECT_TRUE(bool(o));
    EXPECT_EQ(o.id(), id);
    EXPECT_TRUE(r.is_valid(id));
  }

  // handle() accessor returns the underlying handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(staging, 10);
    owner_t o{r, h};
    EXPECT_TRUE(o.handle() == h);
    EXPECT_EQ(o.handle().id(), h.id());
    EXPECT_EQ(o.handle().gen(), h.gen());
  }

  // registry() returns a reference to the associated registry.
  if (true) {
    reg_t r;
    auto o = r.create_owner(staging, 10);
    EXPECT_TRUE(&o.registry() == &r);
  }

  // const registry() returns a const reference.
  if (true) {
    reg_t r;
    const auto o = r.create_owner(staging, 10);
    const reg_t& cr = o.registry();
    EXPECT_TRUE(&cr == &r);
  }

  // RAII pattern: early return erases entity; success path releases it.
  if (true) {
    reg_t r;
    id_t saved_id = id_t::invalid;

    auto do_work = [&](bool succeed) -> bool {
      auto o = r.create_owner(staging, 10);
      if (!o) return false;
      saved_id = o.id();
      if (!succeed) return false; // destructor fires, entity erased
      (void)o.release();
      return true;
    };

    EXPECT_FALSE(do_work(false));
    EXPECT_FALSE(r.is_valid(saved_id)); // erased on early return

    EXPECT_TRUE(do_work(true));
    EXPECT_TRUE(r.is_valid(saved_id)); // survived via release()
    r.erase(saved_id);
  }

  // handle_owner with void metadata.
  if (true) {
    using vreg_t = entity_registry<void>;
    const vreg_t::location_t vstaging{store_id_t{}};
    vreg_t r;
    auto o = r.create_owner(vstaging);
    EXPECT_TRUE(bool(o));
    auto id = o.id();
    EXPECT_TRUE(r.is_valid(id));
    o.reset();
    EXPECT_FALSE(r.is_valid(id));
  }
}

void EntityRegistry_GetAllocator() {
  using reg_t = entity_registry<int>;

  // get_allocator() returns allocator_type.
  if (true) {
    reg_t r;
    auto alloc = r.get_allocator();
    static_assert(std::is_same_v<decltype(alloc), reg_t::allocator_type>);
    // Two calls return equal allocators.
    EXPECT_TRUE(alloc == r.get_allocator());
  }

  // Const-correct: callable on a const reference.
  if (true) {
    const reg_t r;
    auto alloc = r.get_allocator();
    static_assert(std::is_same_v<decltype(alloc), reg_t::allocator_type>);
    (void)alloc;
  }
}

// Component-mode tests.

void EntityRegistry_ComponentMode_Basic() {
  using namespace id_enums;
  // OWN_COUNT = 64 selects component mode (64-bit bitmap), versioned.
  using creg_t = entity_registry<int, entity_id_t, store_id_t,
      generation_scheme::versioned, 64>;
  using id_t = creg_t::id_t;

  // Static checks: is_component_v, not is_archetype_v.
  static_assert(creg_t::is_component_v);
  static_assert(!creg_t::is_archetype_v);

  // Default construction: empty registry.
  if (true) {
    creg_t r;
    EXPECT_EQ(r.id_limit(), id_t::invalid);
    EXPECT_FALSE(r.is_valid(id_t{0}));
    EXPECT_EQ(r.size(), 0U);
  }

  // create_id returns sequential IDs.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    auto id2 = r.create_id({}, 30);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(*id2, 2U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_TRUE(r.is_valid(id2));
    EXPECT_EQ(r.size(), 3U);
  }

  // Metadata is stored and retrieved.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 42);
    auto id1 = r.create_id({}, 99);
    EXPECT_EQ(r[id0], 42);
    EXPECT_EQ(r[id1], 99);
    r[id0] = 777;
    EXPECT_EQ(r[id0], 777);
  }

  // at() with valid and invalid IDs.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 42);
    EXPECT_EQ(r.at(id0), 42);
    r.at(id0) = 99;
    EXPECT_EQ(r.at(id0), 99);
    EXPECT_THROW(r.at(id_t{99}), std::out_of_range);
    r.erase(id0);
    EXPECT_THROW(r.at(id0), std::out_of_range);
  }

  // erase invalidates the ID.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    EXPECT_TRUE(r.erase(id0));
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_FALSE(r.erase(id0)); // double erase
    EXPECT_EQ(r.size(), 1U);
  }

  // erase invalid ID returns false.
  if (true) {
    creg_t r;
    EXPECT_FALSE(r.erase(id_t{0}));
    EXPECT_FALSE(r.erase(id_t::invalid));
  }

  // create_handle and handle validity.
  if (true) {
    creg_t r;
    auto h = r.create_handle({store_id_t{}}, 42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t{0});
    EXPECT_EQ(h.gen(), 0U);
    EXPECT_EQ(r.at(h), 42);
  }

  // get_handle for valid/invalid IDs.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto h = r.get_handle(id0);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id0);
    auto h_bad = r.get_handle(id_t{99});
    EXPECT_FALSE(r.is_valid(h_bad));
    EXPECT_EQ(h_bad.id(), id_t::invalid);
  }

  // Stale handle is invalid after erase + reuse.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto h_old = r.get_handle(id0);
    EXPECT_EQ(h_old.gen(), 0U);
    r.erase(id0);
    auto id0_reused = r.create_id({}, 99);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_FALSE(r.is_valid(h_old));
    auto h_new = r.get_handle(id0_reused);
    EXPECT_TRUE(r.is_valid(h_new));
    EXPECT_GT(h_new.gen(), h_old.gen());
  }

  // erase by handle.
  if (true) {
    creg_t r;
    auto h = r.create_handle({store_id_t{}}, 10);
    EXPECT_TRUE(r.erase(h));
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_FALSE(r.erase(h));
  }

  // size() and max_id().
  if (true) {
    creg_t r;
    EXPECT_EQ(r.size(), 0U);
    EXPECT_EQ(r.max_id(), id_t::invalid);
    (void)r.create_id({}, 10);
    (void)r.create_id({}, 20);
    EXPECT_EQ(r.size(), 2U);
    EXPECT_EQ(r.max_id(), id_t{1});
  }

  // Metadata default value on reuse.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 42);
    r.erase(id0);
    auto id0_reused = r.create_id(); // default metadata
    EXPECT_EQ(id0_reused, id0);
    EXPECT_EQ(r[id0_reused], 0);
  }

  // id_limit enforcement.
  if (true) {
    creg_t r{id_t{2}};
    EXPECT_EQ(r.create_id({}, 10), id_t{0});
    EXPECT_EQ(r.create_id({}, 20), id_t{1});
    EXPECT_EQ(r.create_id({}, 30), id_t::invalid);
  }
}

void EntityRegistry_ComponentMode_Bitmap() {
  using namespace id_enums;
  using creg_t = entity_registry<int, entity_id_t, store_id_t,
      generation_scheme::versioned, 64>;
  // get_location shows staging bit (bit 0) set for a newly created entity.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    const auto& bm = r.get_location(id0);
    EXPECT_TRUE(bm.test(store_id_t{0}));
    EXPECT_FALSE(bm.test(store_id_t{1}));
  }

  // add_location clears the staging bit; is_in_location reads presence.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{0}));  // staging bit set
    EXPECT_FALSE(r.is_in_location(id0, store_id_t{1})); // not in any storage
    r.add_location(id0, store_id_t{1});                 // leaves staging
    EXPECT_FALSE(r.is_in_location(id0, store_id_t{0})); // staging cleared
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{1}));
    r.add_location(id0, store_id_t{63});
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{63}));
    EXPECT_FALSE(r.is_in_location(id0, store_id_t{2}));
  }

  // remove_location: preserving entity (back to staging), then erasing it.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    r.add_location(id0, store_id_t{1}); // leaves staging
    r.add_location(id0, store_id_t{2});
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{1}));
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{2}));
    // Remove one; entity still alive via bit 2.
    r.remove_location(id0, store_id_t{1}, removal_mode::preserve);
    EXPECT_FALSE(r.is_in_location(id0, store_id_t{1}));
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{2}));
    // Remove last; preserve puts entity back in staging.
    r.remove_location(id0, store_id_t{2}, removal_mode::preserve);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{0})); // staging restored
    // Add again, then remove with mode::remove to erase.
    r.add_location(id0, store_id_t{1});
    r.remove_location(id0, store_id_t{1}, removal_mode::remove);
    EXPECT_FALSE(r.is_valid(id0));
  }

  // add_location/remove_location round-trip via get_location (read-only).
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    r.add_location(id0, store_id_t{5});
    EXPECT_TRUE(r.get_location(id0).test(store_id_t{5}));
    r.remove_location(id0, store_id_t{5}, removal_mode::preserve);
    EXPECT_FALSE(r.get_location(id0).test(store_id_t{5}));
    EXPECT_TRUE(r.is_valid(id0)); // entity in staging, still alive
  }

  // Staging bit (bit 0) is restored when entity is reallocated.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    r.add_location(id0, store_id_t{2}); // leaves staging, sets bit 2
    r.erase(id0);
    auto id0_reused = r.create_id({}, 20);
    EXPECT_EQ(id0_reused, id0);
    // Only staging bit is set; bit 2 from before is cleared.
    EXPECT_TRUE(r.get_location(id0_reused).test(store_id_t{0}));
    EXPECT_FALSE(r.get_location(id0_reused).test(store_id_t{2}));
  }

  // Multiple entities have independent bitmaps.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    r.add_location(id0, store_id_t{1});
    r.add_location(id1, store_id_t{2});
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{1}));
    EXPECT_FALSE(r.is_in_location(id0, store_id_t{2}));
    EXPECT_FALSE(r.is_in_location(id1, store_id_t{1}));
    EXPECT_TRUE(r.is_in_location(id1, store_id_t{2}));
  }

  // Const get_location is callable on const reference.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    r.add_location(id0, store_id_t{5});
    const auto& cr = r;
    EXPECT_TRUE(cr.get_location(id0).test(store_id_t{5})); // bit 5
  }

  // erase_if can use bitmap to filter by storage bit.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    auto id2 = r.create_id({}, 30);
    r.add_location(id0, store_id_t{1});
    r.add_location(id2, store_id_t{1});
    // Erase all entities in `store_id_t{1}` (bit 1 set).
    auto cnt = r.erase_if([](auto, auto& rec) {
      return rec.location.get_store_ids().test(store_id_t{1});
    });
    EXPECT_EQ(cnt, 2U);
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2));
  }
}

void EntityRegistry_ComponentMode_Fifo() {
  using namespace id_enums;
  using creg_t = entity_registry<int, entity_id_t, store_id_t,
      generation_scheme::versioned, 64>;
  using id_t = creg_t::id_t;

  // Freed IDs are reused in FIFO order.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    (void)r.create_id({}, 30); // id 2
    r.erase(id0);              // free: [0]
    r.erase(id1);              // free: [0, 1]
    EXPECT_EQ(r.create_id({}, 100), id_t{0});
    EXPECT_EQ(r.create_id({}, 200), id_t{1});
  }

  // FIFO reuse order matches erase order, not ID order.
  if (true) {
    creg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    (void)r.create_id({}, 40); // id 3
    (void)r.create_id({}, 50); // id 4
    r.erase(id_t{2});
    r.erase(id_t{0});
    r.erase(id_t{3});
    EXPECT_EQ(r.create_id({}, 100), id_t{2});
    EXPECT_EQ(r.create_id({}, 200), id_t{0});
    EXPECT_EQ(r.create_id({}, 300), id_t{3});
  }

  // clear() resets and re-queues all IDs in order.
  if (true) {
    creg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.clear();
    EXPECT_EQ(r.size(), 0U);
    EXPECT_EQ(r.create_id({}, 100), id_t{0});
    EXPECT_EQ(r.create_id({}, 200), id_t{1});
    EXPECT_EQ(r.create_id({}, 300), id_t{2});
  }

  // shrink_to_fit trims trailing dead records.
  if (true) {
    creg_t r;
    (void)r.create_id({}, 10); // id 0
    (void)r.create_id({}, 20); // id 1
    (void)r.create_id({}, 30); // id 2
    r.erase(id_t{2});
    r.shrink_to_fit();
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_TRUE(r.is_valid(id_t{1}));
    EXPECT_EQ(r.max_id(), id_t{1});
  }
}

void EntityRegistry_ComponentMode_HandleOwner() {
  using namespace id_enums;
  using creg_t = entity_registry<int, entity_id_t, store_id_t,
      generation_scheme::versioned, 64>;
  using id_t = creg_t::id_t;
  using owner_t = creg_t::handle_owner;

  // Default-constructed owner holds no entity.
  if (true) {
    owner_t o;
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
  }

  // Constructor from existing valid handle.
  if (true) {
    creg_t r;
    auto h = r.create_handle({store_id_t{}}, 42);
    owner_t o{r, h};
    EXPECT_TRUE(bool(o));
    EXPECT_EQ(o.id(), h.id());
    EXPECT_TRUE(o.handle() == h);
  }

  // Constructor from existing invalid handle: owner is empty.
  if (true) {
    creg_t r;
    auto h = r.create_handle({store_id_t{}}, 42);
    r.erase(h);
    owner_t o{r, h};
    EXPECT_FALSE(bool(o));
  }

  // create_owner creates and takes ownership.
  if (true) {
    creg_t r;
    auto o = r.create_owner(77);
    EXPECT_TRUE(bool(o));
    EXPECT_NE(o.id(), id_t::invalid);
    EXPECT_EQ(r[o.id()], 77);
  }

  // Destructor erases entity.
  if (true) {
    creg_t r;
    id_t saved_id;
    {
      auto o = r.create_owner(10);
      saved_id = o.id();
      EXPECT_TRUE(r.is_valid(saved_id));
    }
    EXPECT_FALSE(r.is_valid(saved_id));
  }

  // release() leaves entity alive.
  if (true) {
    creg_t r;
    auto o = r.create_owner(10);
    [[maybe_unused]] auto id = o.id();
    auto h = o.release();
    EXPECT_FALSE(bool(o));
    EXPECT_TRUE(r.is_valid(h));
    r.erase(h);
  }

  // Move semantics work correctly.
  if (true) {
    creg_t r;
    auto o1 = r.create_owner(10);
    auto id = o1.id();
    owner_t o2{std::move(o1)};
    EXPECT_FALSE(bool(o1));
    EXPECT_TRUE(bool(o2));
    EXPECT_EQ(o2.id(), id);
    EXPECT_TRUE(r.is_valid(id));
  }
}

void EntityRegistry_ComponentMode_NoGen() {
  using namespace id_enums;
  using creg_t = entity_registry<int, entity_id_t, store_id_t,
      generation_scheme::unversioned, 64>;
  using id_t = creg_t::id_t;
  using handle_t = creg_t::handle_t;

  // No gen: handle is same size as id_t.
  static_assert(sizeof(handle_t) == sizeof(id_t));

  // Basic create and erase.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto id1 = r.create_id({}, 20);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(r[id0], 10);
    EXPECT_TRUE(r.erase(id0));
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Without gen, stale handles falsely valid after ID reuse.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    auto h_old = r.get_handle(id0);
    r.erase(id0);
    EXPECT_FALSE(r.is_valid(h_old));
    auto id0_reused = r.create_id({}, 99);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_TRUE(r.is_valid(h_old)); // stale but valid w/o gen
  }

  // Location operations still work in unversioned mode.
  if (true) {
    creg_t r;
    auto id0 = r.create_id({}, 10);
    r.add_location(id0, store_id_t{1});
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{1}));
    r.remove_location(id0, store_id_t{1}, removal_mode::preserve);
    EXPECT_FALSE(r.is_in_location(id0, store_id_t{1}));
  }
}

void EntityRegistry_ComponentMode_VoidMeta() {
  using namespace id_enums;
  using creg_t = entity_registry<void, entity_id_t, store_id_t,
      generation_scheme::versioned, 64>;
  using id_t = creg_t::id_t;

  // Create and validate without metadata.
  if (true) {
    creg_t r;
    auto id0 = r.create_id();
    auto id1 = r.create_id();
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
  }

  // Erase and FIFO reuse.
  if (true) {
    creg_t r;
    (void)r.create_id(); // id 0
    (void)r.create_id(); // id 1
    (void)r.create_id(); // id 2
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.create_id(), id_t{0});
    EXPECT_EQ(r.create_id(), id_t{1});
  }

  // Handles and generation.
  if (true) {
    creg_t r;
    auto h = r.create_handle();
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.gen(), 0U);
    r.erase(h);
    EXPECT_FALSE(r.is_valid(h));
  }

  // Bitmap operations with void metadata.
  if (true) {
    creg_t r;
    auto id0 = r.create_id();
    EXPECT_TRUE(r.is_in_location(id0, store_id_t{0})); // staging bit set
    auto cnt = r.erase_if([](auto, auto& rec) {
      return rec.location.get_store_ids().test(store_id_t{0});
    });
    EXPECT_EQ(cnt, 1U);
    EXPECT_EQ(r.size(), 0U);
  }
}

MAKE_TEST_LIST(EntityRegistry_Basic, EntityRegistry_Handle,
    EntityRegistry_Fifo, EntityRegistry_Clear, EntityRegistry_Reserve,
    EntityRegistry_IdLimit, EntityRegistry_NoGen, EntityRegistry_VoidMeta,
    EntityRegistry_VoidNoGen, EntityRegistry_IdLimitAdvanced,
    EntityRegistry_FifoAdvanced, EntityRegistry_LifoAdvanced,
    EntityRegistry_EdgeCases, EntityRegistry_MetadataCleanup,
    EntityRegistry_EraseIfPredicate, EntityRegistry_IdLimitFreeList,
    EntityRegistry_ReservePrefillExisting, EntityRegistry_HandleOwner,
    EntityRegistry_GetAllocator, EntityRegistry_ComponentMode_Basic,
    EntityRegistry_ComponentMode_Bitmap, EntityRegistry_ComponentMode_Fifo,
    EntityRegistry_ComponentMode_HandleOwner,
    EntityRegistry_ComponentMode_NoGen, EntityRegistry_ComponentMode_VoidMeta);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
