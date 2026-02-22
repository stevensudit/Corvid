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
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "../corvid/ecs.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

using int_stable_ids = stable_ids<int>;

void ArchetypeStorage_Basic() {
  using id_enums::entity_id_t;
  using id_enums::store_id_t;
  using reg_t = entity_registry<int>;
  using archetype_t =
      archetype_storage<reg_t, std::tuple<int, float, std::string>>;

  // Default construction and empty state.
  if (true) {
    archetype_t v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
  }

  // reserve() and capacity().
  if (true) {
    archetype_t v;
    v.reserve(10);
    EXPECT_GE(v.capacity(), 10U);
    EXPECT_TRUE(v.empty()); // reserve doesn't change size.
  }

  // shrink_to_fit() on default-constructed storage.
  if (true) {
    archetype_t v;
    v.reserve(100);
    v.shrink_to_fit();
    EXPECT_TRUE(v.empty());
  }
}

void ArchetypeStorage_Registry() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};

  // Registry constructor: store_id() returns correct value.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    EXPECT_EQ(a.store_id(), sid);
    EXPECT_TRUE(a.empty());
    EXPECT_EQ(a.size(), 0U);
  }

  // Registry constructor with limit.
  if (true) {
    reg_t r;
    arch_t a{r, sid, 3};
    EXPECT_EQ(a.limit(), 3U);
  }

  // Registry constructor with do_reserve pre-allocates capacity.
  if (true) {
    reg_t r;
    arch_t a{r, sid, 10, true};
    EXPECT_GE(a.capacity(), 10U);
    EXPECT_TRUE(a.empty());
  }

  // Default limit is the max representable value.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    EXPECT_EQ(a.limit(), *id_t::invalid);
  }
}

void ArchetypeStorage_Add() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // add(id) moves entity from staging into storage; registry location updated.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 1.0f));
    EXPECT_EQ(a.size(), 1U);
    EXPECT_FALSE(a.empty());
    EXPECT_TRUE(a.contains(id0));
    auto loc = r.get_location(id0);
    EXPECT_EQ(loc.store_id, sid);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // add(id) multiple entities assigns sequential indices.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_TRUE(a.add(id2, 3, 3.0f));
    EXPECT_EQ(a.size(), 3U);
    EXPECT_EQ(r.get_location(id0).ndx, 0U);
    EXPECT_EQ(r.get_location(id1).ndx, 1U);
    EXPECT_EQ(r.get_location(id2).ndx, 2U);
  }

  // add(id) fails if entity is not in staging (already in another storage).
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    arch_t b{r, store_id_t{2}};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_FALSE(b.add(id0, 2, 2.0f)); // id0 not in staging
    EXPECT_EQ(b.size(), 0U);
  }

  // add(handle) succeeds for a valid handle whose entity is in staging.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(a.add(h, 99, 1.5f));
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(a.contains(h));
  }

  // add(handle) fails for an invalid (erased) handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    r.erase(h);
    EXPECT_FALSE(a.add(h, 99, 1.5f));
    EXPECT_EQ(a.size(), 0U);
  }

  // add_new creates an entity in the registry and adds its components.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = a.add_new(42, 7, 2.0f);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(r[h.id()], 42);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(a.contains(h));
    EXPECT_EQ(r.get_location(h.id()).store_id, sid);
  }

  // add_new returns an invalid handle when the registry is at its ID limit.
  if (true) {
    reg_t r{id_t{0}};
    arch_t a{r, sid};
    auto h = a.add_new(10, 1, 1.0f);
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(a.size(), 0U);
  }

  // add_new returns an invalid handle and cleans up when the archetype is
  // full.
  if (true) {
    reg_t r;
    arch_t a{r, sid, 1};
    auto h0 = a.add_new(10, 1, 1.0f);
    EXPECT_TRUE(r.is_valid(h0));
    auto h1 = a.add_new(20, 2, 2.0f);
    EXPECT_FALSE(r.is_valid(h1));
    EXPECT_EQ(a.size(), 1U);
    EXPECT_EQ(r.size(), 1U); // second entity cleaned up by RAII owner
  }

  // add with one trailing component omitted: it is default-constructed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7));
    EXPECT_EQ(a[id0].template component<int>(), 7);
    EXPECT_EQ(a[id0].template component<float>(), 0.0f);
  }

  // add with all components omitted: all are default-constructed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0));
    EXPECT_EQ(a[id0].template component<int>(), 0);
    EXPECT_EQ(a[id0].template component<float>(), 0.0f);
  }

  // add_new with one trailing component omitted: it is default-constructed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = a.add_new(42, 7);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(a[h.id()].template component<int>(), 7);
    EXPECT_EQ(a[h.id()].template component<float>(), 0.0f);
  }

  // add_new with all components omitted: all are default-constructed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = a.add_new(42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(a[h.id()].template component<int>(), 0);
    EXPECT_EQ(a[h.id()].template component<float>(), 0.0f);
  }

  // contains(id) returns false for entity in staging, true after add.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_FALSE(a.contains(id0));
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.contains(id0));
  }

  // contains(handle) returns false for invalid handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    r.erase(h);
    EXPECT_FALSE(a.contains(h));
  }
}

void ArchetypeStorage_Remove() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // remove(id) moves entity back to staging; entity remains valid.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.remove(id0));
    EXPECT_EQ(a.size(), 0U);
    EXPECT_FALSE(a.contains(id0));
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_EQ(r.get_location(id0).store_id, store_id_t{});
  }

  // remove(id) returns false for entity not in this storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_FALSE(a.remove(id0)); // still in staging
  }

  // remove(handle) moves entity back to staging.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(a.add(h, 1, 1.0f));
    EXPECT_TRUE(a.remove(h));
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(a.size(), 0U);
  }

  // remove(handle) returns false for invalid handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    r.erase(h);
    EXPECT_FALSE(a.remove(h));
  }

  // remove_all moves all entities back to staging.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_TRUE(a.add(id2, 3, 3.0f));
    a.remove_all();
    EXPECT_EQ(a.size(), 0U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_TRUE(r.is_valid(id2));
    EXPECT_EQ(r.get_location(id0).store_id, store_id_t{});
    EXPECT_EQ(r.get_location(id1).store_id, store_id_t{});
    EXPECT_EQ(r.get_location(id2).store_id, store_id_t{});
  }

  // Swap-and-pop on non-last element updates displaced entity's registry ndx.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_TRUE(a.add(id2, 3, 3.0f));
    // Removing index 0 swaps id2 (last) into slot 0.
    EXPECT_TRUE(a.remove(id0));
    EXPECT_EQ(a.size(), 2U);
    EXPECT_EQ(r.get_location(id2).ndx, 0U);
    EXPECT_EQ(r.get_location(id1).ndx, 1U);
  }
}

void ArchetypeStorage_Erase() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // erase(id) destroys the entity from the registry.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.erase(id0));
    EXPECT_EQ(a.size(), 0U);
    EXPECT_FALSE(r.is_valid(id0));
  }

  // erase(id) returns false for entity not in this storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_FALSE(a.erase(id0));
    EXPECT_TRUE(r.is_valid(id0)); // entity still alive
  }

  // erase(handle) destroys the entity.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(a.add(h, 1, 1.0f));
    EXPECT_TRUE(a.erase(h));
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(a.size(), 0U);
  }

  // erase(handle) returns false for invalid handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    r.erase(h);
    EXPECT_FALSE(a.erase(h));
  }

  // clear() destroys all entities; storage becomes empty.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_TRUE(a.add(id2, 3, 3.0f));
    a.clear();
    EXPECT_EQ(a.size(), 0U);
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2));
  }

  // Swap-and-pop on erase: data of displaced entity moves to erased slot.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 11, 1.1f));
    EXPECT_TRUE(a.add(id1, 22, 2.2f));
    EXPECT_TRUE(a.add(id2, 33, 3.3f));
    // Erase id0 at index 0: id2 (index 2) swaps into index 0.
    EXPECT_TRUE(a.erase(id0));
    EXPECT_EQ(a.size(), 2U);
    EXPECT_EQ(a[id2].id(), id2);
    EXPECT_EQ(a[id2].component<int>(), 33);
    EXPECT_EQ(a[id1].id(), id1);
    EXPECT_EQ(a[id1].component<int>(), 22);
  }
}

void ArchetypeStorage_RowAccess() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // operator[](ndx) returns row_lens; index() and id() are correct.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 1.0f));
    auto row = a[id0];
    EXPECT_EQ(row.index(), 0U);
    EXPECT_EQ(row.id(), id0);
  }

  // row_lens::component<C>() gives mutable access by type.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 2.0f));
    auto row = a[id0];
    EXPECT_EQ(row.component<int>(), 42);
    EXPECT_EQ(row.component<float>(), 2.0f);
    row.component<int>() = 99;
    EXPECT_EQ(row.component<int>(), 99);
  }

  // row_lens::component<Index>() gives mutable access by index.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 3.0f));
    auto row = a[id0];
    EXPECT_EQ(row.component<0>(), 42);
    EXPECT_EQ(row.component<1>(), 3.0f);
    row.component<0>() = 77;
    EXPECT_EQ(row.component<0>(), 77);
  }

  // row_lens::components() returns tuple of mutable references.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 10, 1.0f));
    auto row = a[id0];
    auto [i, f] = row.components();
    EXPECT_EQ(i, 10);
    EXPECT_EQ(f, 1.0f);
    i = 100; // mutates actual data via reference
    EXPECT_EQ(a[id0].component<int>(), 100);
  }

  // const operator[](ndx) returns row_view with read-only access.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 4.0f));
    const auto& ca = a;
    auto row = ca[id0];
    EXPECT_EQ(row.index(), 0U);
    EXPECT_EQ(row.id(), id0);
    EXPECT_EQ(row.component<int>(), 42);
    EXPECT_EQ(row.component<float>(), 4.0f);
  }

  // row_view::component<Index>() const access by index.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 5, 2.0f));
    const auto& ca = a;
    auto row = ca[id0];
    EXPECT_EQ(row.component<0>(), 5);
    EXPECT_EQ(row.component<1>(), 2.0f);
  }

  // row_view::components() returns tuple of const references.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7, 3.0f));
    const auto& ca = a;
    auto [i, f] = ca[id0].components();
    EXPECT_EQ(i, 7);
    EXPECT_EQ(f, 3.0f);
  }

  // row_lens::get_owner() refers back to the owning archetype_storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    auto row = a[id0];
    EXPECT_TRUE(&row.get_owner() == &a);
  }

  // Multiple rows at different indices have independent data.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 11, 1.0f));
    EXPECT_TRUE(a.add(id1, 22, 2.0f));
    EXPECT_EQ(a[id0].component<int>(), 11);
    EXPECT_EQ(a[id1].component<int>(), 22);
    EXPECT_EQ(a[id0].id(), id0);
    EXPECT_EQ(a[id1].id(), id1);
  }

  // row_lens is copy-constructible; copy shares view into same data.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 1.0f));
    auto row = a[id0];
    auto copy = row; // copy construction
    EXPECT_EQ(copy.index(), 0U);
    EXPECT_EQ(copy.id(), id0);
    EXPECT_EQ(copy.component<int>(), 42);
    copy.component<int>() = 99;
    EXPECT_EQ(row.component<int>(), 99); // same underlying data
  }

  // row_lens is move-constructible.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7, 2.0f));
    auto row = a[id0];
    auto moved = std::move(row);
    EXPECT_EQ(moved.index(), 0U);
    EXPECT_EQ(moved.component<int>(), 7);
  }

  // row_view is copy-constructible; copy shares view into same data.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 55, 3.0f));
    const auto& ca = a;
    auto row = ca[id0];
    auto copy = row; // copy construction
    EXPECT_EQ(copy.index(), 0U);
    EXPECT_EQ(copy.id(), id0);
    EXPECT_EQ(copy.component<int>(), 55);
  }
}

void ArchetypeStorage_ComponentAccess() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // component<C>() on row_lens gives mutable access to each row's value.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_EQ(a.size(), 2U);
    EXPECT_EQ(a[id0].component<int>(), 1);
    EXPECT_EQ(a[id1].component<int>(), 2);
    a[id0].component<int>() = 99;
    EXPECT_EQ(a[id0].component<int>(), 99);
  }

  // component<C>() on row_view gives const access.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 5.0f));
    const auto& ca = a;
    EXPECT_EQ(ca.size(), 1U);
    EXPECT_EQ(ca[id0].component<float>(), 5.0f);
  }

  // component<Index>() access by index, mutable and const.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7, 4.0f));
    EXPECT_EQ(a[id0].component<0>(), 7);
    EXPECT_EQ(a[id0].component<1>(), 4.0f);
    a[id0].component<0>() = 77;
    EXPECT_EQ(a[id0].component<0>(), 77);
  }

  // components() returns tuple of mutable references.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 3, 3.0f));
    auto [i, f] = a[id0].components();
    EXPECT_EQ(i, 3);
    EXPECT_EQ(f, 3.0f);
    i = 33;
    EXPECT_EQ(a[id0].component<int>(), 33);
  }

  // components() on const row_view returns tuple of const references.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 6, 6.0f));
    const auto& ca = a;
    auto [i, f] = ca[id0].components();
    EXPECT_EQ(i, 6);
    EXPECT_EQ(f, 6.0f);
  }
}

void ArchetypeStorage_Limit() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Default limit is the max value (effectively unlimited).
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    EXPECT_EQ(a.limit(), *id_t::invalid);
  }

  // Constructor-set limit prevents add() beyond that count.
  if (true) {
    reg_t r;
    arch_t a{r, sid, 2};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_FALSE(a.add(id2, 3, 3.0f)); // at limit
    EXPECT_EQ(a.size(), 2U);
  }

  // set_limit() succeeds when new_limit >= current size.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.set_limit(2U));
    EXPECT_EQ(a.limit(), 2U);
  }

  // set_limit() fails when new_limit < current size.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_FALSE(a.set_limit(1U));
    EXPECT_EQ(a.limit(), *id_t::invalid); // unchanged
  }

  // add_new respects the limit; excess entities are cleaned up automatically.
  if (true) {
    reg_t r;
    arch_t a{r, sid, 1};
    auto h0 = a.add_new(10, 1, 1.0f);
    EXPECT_TRUE(r.is_valid(h0));
    auto h1 = a.add_new(20, 2, 2.0f); // fails: archetype full
    EXPECT_FALSE(r.is_valid(h1));
    EXPECT_EQ(a.size(), 1U);
    EXPECT_EQ(r.size(), 1U);
  }
}

void ArchetypeStorage_SwapAndMove() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid1 = store_id_t{1};
  const auto sid2 = store_id_t{2};
  const loc_t staging{store_id_t{}};

  // swap() exchanges component data and store_ids between two storages.
  if (true) {
    reg_t r;
    arch_t a{r, sid1};
    arch_t b{r, sid2};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 11, 1.0f));
    EXPECT_TRUE(b.add(id1, 22, 2.0f));
    swap(a, b);
    // After swap, a holds what b had and vice versa, including store_ids.
    EXPECT_EQ(a.store_id(), sid2);
    EXPECT_EQ(b.store_id(), sid1);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_EQ(b.size(), 1U);
    EXPECT_EQ(a[id1].component<int>(), 22); // id1's data is now in a
    EXPECT_EQ(b[id0].component<int>(), 11); // id0's data is now in b
  }

  // shrink_to_fit after reserve reduces wasted capacity.
  if (true) {
    reg_t r;
    arch_t a{r, sid1};
    a.reserve(100);
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    a.shrink_to_fit();
    EXPECT_EQ(a.size(), 1U);
    EXPECT_LT(a.capacity(), 100U);
  }

  // Move constructor transfers all data; source is left in a valid empty
  // state. Destructor of the move-destination erases entities from registry.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    {
      arch_t a{r, sid1};
      EXPECT_TRUE(a.add(id0, 42, 1.0f));
      arch_t b{std::move(a)};
      EXPECT_EQ(b.size(), 1U);
      EXPECT_EQ(b.store_id(), sid1);
      EXPECT_EQ(b[id0].component<int>(), 42);
    } // b destructor fires
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Move assignment transfers data from source to destination. Destructor
  // of the move-destination erases entities from registry.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    {
      arch_t a{r, sid1};
      arch_t b{r, sid2};
      EXPECT_TRUE(a.add(id0, 7, 7.0f));
      b = std::move(a);
      EXPECT_EQ(b.size(), 1U);
      EXPECT_EQ(b.store_id(), sid1);
      EXPECT_EQ(b[id0].component<int>(), 7);
    } // b destructor fires
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Destructor clears all entities from the registry (regression guard).
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    {
      arch_t a{r, sid1};
      EXPECT_TRUE(a.add(id0, 1, 1.0f));
      EXPECT_TRUE(a.add(id1, 2, 2.0f));
    } // destructor fires
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
  }
}

void ArchetypeStorage_Iterator() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Range-based for over mutable archetype yields row_lens per row.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_TRUE(a.add(id2, 3, 3.0f));
    int sum = 0;
    for (auto row : a) sum += row.component<int>();
    EXPECT_EQ(sum, 6);
  }

  // Range-based for over const archetype yields row_view per row.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 10, 1.0f));
    EXPECT_TRUE(a.add(id1, 20, 2.0f));
    const auto& ca = a;
    float fsum = 0.0f;
    for (const auto& row : ca) fsum += row.component<float>();
    EXPECT_EQ(fsum, 3.0f);
  }

  // Mutating components via iterator is reflected in storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 0.0f));
    EXPECT_TRUE(a.add(id1, 2, 0.0f));
    for (auto row : a) row.component<int>() *= 10;
    EXPECT_EQ(a[id0].component<int>(), 10);
    EXPECT_EQ(a[id1].component<int>(), 20);
  }

  // Iterator exposes id() matching the entity at that row.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 0, 0.0f));
    EXPECT_TRUE(a.add(id1, 0, 0.0f));
    auto it = a.begin();
    EXPECT_EQ(it->id(), id0);
    ++it;
    EXPECT_EQ(it->id(), id1);
    ++it;
    EXPECT_TRUE(it == a.end());
  }

  // Bidirectional: operator-- steps back correctly.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 5, 0.0f));
    EXPECT_TRUE(a.add(id1, 6, 0.0f));
    auto it = a.end();
    --it;
    EXPECT_EQ((*it).component<int>(), 6);
    --it;
    EXPECT_EQ((*it).component<int>(), 5);
    EXPECT_TRUE(it == a.begin());
  }

  // Empty archetype: begin() == end().
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    EXPECT_TRUE(a.begin() == a.end());
    const auto& ca = a;
    EXPECT_TRUE(ca.begin() == ca.end());
  }

  // cbegin()/cend() return const_iterator; readable on a mutable archetype.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7, 1.5f));
    float fsum = 0.0f;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
      fsum += it->component<float>();
    EXPECT_EQ(fsum, 1.5f);
    static_assert(
        std::is_same_v<decltype(a.cbegin()), arch_t::const_iterator>);
  }
}

void ArchetypeStorage_EraseIf() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // erase_if with pred always false: nothing removed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    auto cnt = a.erase_if([](const auto&) { return false; });
    EXPECT_EQ(cnt, 0U);
    EXPECT_EQ(a.size(), 2U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
  }

  // erase_if removes all entities when pred always returns true.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    auto cnt = a.erase_if([](const auto&) { return true; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 0U);
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
  }

  // erase_if removes matching entities; displaced entity keeps correct
  // location.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 11, 1.0f));
    EXPECT_TRUE(a.add(id1, 22, 2.0f));
    EXPECT_TRUE(a.add(id2, 33, 3.0f));
    // Erase entities whose int component is odd.
    auto cnt = a.erase_if([](const auto& row) {
      return row.template component<int>() % 2 != 0;
    });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_FALSE(r.is_valid(id0)); // 11 is odd; erased
    EXPECT_TRUE(r.is_valid(id1));  // 22 is even; kept
    EXPECT_FALSE(r.is_valid(id2)); // 33 is odd; erased
    EXPECT_EQ(a[id1].id(), id1);
    EXPECT_EQ(a[id1].component<int>(), 22);
    EXPECT_EQ(r.get_location(id1).store_id, sid);
    EXPECT_EQ(r.get_location(id1).ndx, 0U);
  }

  // erase_if on a single element storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    auto cnt = a.erase_if([](const auto&) { return true; });
    EXPECT_EQ(cnt, 1U);
    EXPECT_EQ(a.size(), 0U);
    EXPECT_FALSE(r.is_valid(id0));
  }

  // erase_if_component<C> removes by component value predicate.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 10, 1.0f));
    EXPECT_TRUE(a.add(id1, 20, 2.0f));
    EXPECT_TRUE(a.add(id2, 30, 3.0f));
    auto cnt = a.erase_if_component<int>([](int val, auto) {
      return val > 15;
    });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2));
    EXPECT_EQ(a[id0].component<int>(), 10);
  }

  // erase_if_component<Index> removes by component index predicate.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 5, 1.0f));
    EXPECT_TRUE(a.add(id1, 5, 9.0f));
    // Component at index 1 is float; erase where float > 5.
    auto cnt = a.erase_if_component<1>([](float val, auto) {
      return val > 5.0f;
    });
    EXPECT_EQ(cnt, 1U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
  }
}

void StableId_Basic() {
  using V = int_stable_ids;
  using id_t = V::id_t;

  // Empty container.
  if (true) {
    V v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
    EXPECT_EQ(v.find_max_extant_id(), id_t::invalid);
  }

  // push_back and emplace_back assign sequential IDs starting at 0.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(v.size(), 1U);
    EXPECT_FALSE(v.empty());
    auto id1 = v.push_back(20);
    auto id2 = v.emplace_back(30);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(*id2, 2U);
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[id0], 10);
    EXPECT_EQ(v[id1], 20);
    EXPECT_EQ(v[id2], 30);
  }

  // Mutable and const access via operator[] and at().
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    EXPECT_EQ(v[id0], 10);
    EXPECT_EQ(v.at(id0), 10);
    v[id0] = 42;
    EXPECT_EQ(v[id0], 42);
    v.at(id1) = 99;
    EXPECT_EQ(v.at(id1), 99);
    const V& cv = v;
    EXPECT_EQ(cv[id0], 42);
    EXPECT_EQ(cv.at(id1), 99);
  }

  // at(id) throws std::out_of_range for an invalid ID.
  if (true) {
    V v;
    (void)v.push_back(10);
    EXPECT_THROW(v.at(id_t{99}), std::out_of_range);
  }

  // Handles carry generation; get_handle and is_valid agree.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    auto h0 = v.get_handle(id0);
    auto h1 = v.get_handle(id1);
    EXPECT_TRUE(v.is_valid(h0));
    EXPECT_TRUE(v.is_valid(h1));
    EXPECT_EQ(h0.id(), id0);
    EXPECT_EQ(h1.id(), id1);
    EXPECT_EQ(h0.gen(), 0U);
    EXPECT_EQ(h1.gen(), 0U);
  }

  // push_back_handle and emplace_back_handle return handles.
  if (true) {
    V v;
    auto h0 = v.push_back_handle(10);
    auto h1 = v.emplace_back_handle(20);
    EXPECT_TRUE(v.is_valid(h0));
    EXPECT_TRUE(v.is_valid(h1));
    EXPECT_EQ(v.at(h0), 10);
    const V& cv = v;
    EXPECT_EQ(cv.at(h1), 20);
  }

  // erase by ID.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    auto id2 = v.push_back(30);
    EXPECT_EQ(v.size(), 3U);
    EXPECT_TRUE(v.erase(id1));
    EXPECT_EQ(v.size(), 2U);
    EXPECT_FALSE(v.is_valid(id1));
    EXPECT_TRUE(v.is_valid(id0));
    EXPECT_TRUE(v.is_valid(id2));
    EXPECT_EQ(v[id0], 10);
    EXPECT_EQ(v[id2], 30);
  }

  // erase by handle.
  if (true) {
    V v;
    auto h0 = v.push_back_handle(10);
    auto h1 = v.push_back_handle(20);
    EXPECT_TRUE(v.erase(h0));
    EXPECT_FALSE(v.is_valid(h0));
    EXPECT_TRUE(v.is_valid(h1));
    EXPECT_EQ(v.size(), 1U);
  }

  // Erased handle is no longer valid; erase returns false for stale handle.
  if (true) {
    V v;
    auto h = v.push_back_handle(10);
    EXPECT_TRUE(v.is_valid(h));
    EXPECT_TRUE(v.erase(h));
    EXPECT_FALSE(v.is_valid(h));
    EXPECT_FALSE(v.erase(h));
    EXPECT_THROW(v.at(h), std::invalid_argument);
  }

  // ID reuse: erased ID is reused by next insertion (LIFO order).
  if (true) {
    V v;
    (void)v.push_back(10);         // id 0
    auto id1 = v.push_back(20);    // id 1
    (void)v.push_back(30);         // id 2
    v.erase(id1);                  // id 1 is freed
    auto id_new = v.push_back(99); // should reuse id 1
    EXPECT_EQ(id_new, id1);
    EXPECT_EQ(v[id_new], 99);
  }

  // Old handle is invalidated even if ID is reused.
  if (true) {
    V v;
    auto h = v.push_back_handle(10);
    v.erase(h);
    (void)v.push_back(20); // reuse ID
    EXPECT_FALSE(v.is_valid(h));
    auto h_new = v.get_handle(h.id());
    EXPECT_TRUE(v.is_valid(h_new));
    EXPECT_NE(h.gen(), h_new.gen());
  }

  // erase_if removes matching elements.
  if (true) {
    V v;
    (void)v.push_back(5);
    (void)v.push_back(15);
    (void)v.push_back(25);
    (void)v.push_back(10);
    auto cnt = v.erase_if([](int x) { return x > 10; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(v.size(), 2U);
    int sum = 0;
    for (auto x : v) sum += x;
    EXPECT_EQ(sum, 15); // 5 + 10
  }

  // Linear iteration via begin()/end().
  if (true) {
    V v;
    (void)v.push_back(1);
    (void)v.push_back(2);
    (void)v.push_back(3);
    int sum = 0;
    for (auto x : v) sum += x;
    EXPECT_EQ(sum, 6);
  }

  // span() gives mutable access.
  if (true) {
    V v;
    (void)v.push_back(10);
    (void)v.push_back(20);
    auto s = v.span();
    s[0] = 100;
    EXPECT_EQ(v[V::id_t{0}], 100);
  }

  // vector() gives const access.
  if (true) {
    V v;
    (void)v.push_back(10);
    const auto& vec = v.vector();
    EXPECT_EQ(vec.size(), 1U);
    EXPECT_EQ(vec[0], 10);
  }

  // clear() removes all elements; shrink=true also frees memory.
  if (true) {
    V v;
    (void)v.push_back(1);
    (void)v.push_back(2);
    v.clear();
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
    // IDs may still be reused after clear without shrink.
    auto id = v.push_back(99);
    EXPECT_EQ(*id, 0U); // reuses freed id 0
  }

  // clear(true) fully resets.
  if (true) {
    V v;
    (void)v.push_back(1);
    v.clear(true);
    EXPECT_TRUE(v.empty());
    auto id = v.push_back(100);
    EXPECT_EQ(*id, 0U);
    EXPECT_EQ(v.get_handle(id).gen(), 0U);
  }

  // shrink_to_fit compacts the container.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(V::id_t{2});
    v.shrink_to_fit();
    EXPECT_EQ(v.size(), 2U);
  }

  // reserve does not change size.
  if (true) {
    V v;
    v.reserve(100);
    EXPECT_TRUE(v.empty());
  }

  // swap exchanges containers.
  if (true) {
    V a, b;
    (void)a.push_back(1);
    (void)a.push_back(2);
    (void)b.push_back(100);
    swap(a, b);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_EQ(b.size(), 2U);
    EXPECT_EQ(a[V::id_t{0}], 100);
    EXPECT_EQ(b[V::id_t{0}], 1);
  }

  // Move constructor and assignment.
  if (true) {
    V v;
    (void)v.push_back(42);
    V w{std::move(v)};
    EXPECT_EQ(w.size(), 1U);
    EXPECT_EQ(w[V::id_t{0}], 42);
    V x;
    x = std::move(w);
    EXPECT_EQ(x.size(), 1U);
    EXPECT_EQ(x[V::id_t{0}], 42);
  }

  // next_id() returns the ID that will be allocated next.
  if (true) {
    V v;
    EXPECT_EQ(v.next_id(), V::id_t{0});
    (void)v.push_back(10);
    EXPECT_EQ(v.next_id(), V::id_t{1});
    (void)v.push_back(20);
    v.erase(V::id_t{0});
    // LIFO: freed ID 0 is at the tail front, so next reuse is ID 0.
    EXPECT_EQ(v.next_id(), V::id_t{0});
  }

  // max_id() is the high-water mark, not the highest extant ID.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    EXPECT_EQ(v.max_id(), V::id_t{2});
    v.erase(V::id_t{2});
    EXPECT_EQ(v.max_id(), V::id_t{2});
    EXPECT_EQ(v.find_max_extant_id(),
        V::id_t{1}); // contrast: live max dropped
    // Reinserting reuses id 2; high-water mark stays the same.
    (void)v.push_back(99);
    EXPECT_EQ(v.max_id(), V::id_t{2});
    // clear() without shrink keeps the index table intact.
    v.clear();
    EXPECT_EQ(v.max_id(), V::id_t{2});
    // clear(true) frees the index table; max_id resets.
    v.clear(true);
    EXPECT_EQ(v.max_id(), V::id_t::invalid);
  }

  // Allocator constructor produces a usable, empty container.
  if (true) {
    V v{std::allocator<int>{}};
    EXPECT_TRUE(v.empty());
    auto id = v.push_back(42);
    EXPECT_EQ(v[id], 42);
    EXPECT_EQ(v.size(), 1U);
  }
}

enum class small_id_t : std::uint8_t { invalid = 255 };

template<>
constexpr auto corvid::enums::registry::enum_spec_v<small_id_t> =
    corvid::enums::sequence::make_sequence_enum_spec<small_id_t, "">();

using int_stable_small_ids = stable_ids<int, small_id_t>;

using int_stable_ids_fifo =
    stable_ids<int, int_stable_ids::id_t, true, true, std::allocator<int>>;
using int_stable_ids_nogen =
    stable_ids<int, int_stable_ids::id_t, false, false, std::allocator<int>>;
using int_stable_ids_fifo_nogen =
    stable_ids<int, int_stable_ids::id_t, false, true, std::allocator<int>>;
using int_stable_small_ids_fifo =
    stable_ids<int, small_id_t, true, true, std::allocator<int>>;

void StableId_SmallId() {
  using V = int_stable_small_ids;
  using id_t = V::id_t; // small_id_t : uint8_t, invalid = 255

  // Fill to capacity: 255 elements occupy every ID from 0 to 254.
  if (true) {
    V v;
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.size(), 255U);
    EXPECT_EQ(v[id_t{0}], 0);
    EXPECT_EQ(v[id_t{127}], 127);
    EXPECT_EQ(v[id_t{254}], 254);
  }

  // The 256th insertion exceeds the limit; container size is unchanged.
  if (true) {
    V v;
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.size(), 255U);
    EXPECT_THROW(v.push_back(999), std::out_of_range);
    EXPECT_EQ(v.size(), 255U);
  }

  // Erasing one element opens exactly one reuse slot.  After that single
  // reuse the container is full again and the next insertion overflows.
  if (true) {
    V v;
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    auto h100 = v.get_handle(id_t{100});

    v.erase(id_t{100});
    EXPECT_EQ(v.size(), 254U);
    EXPECT_FALSE(v.is_valid(id_t{100}));
    EXPECT_FALSE(v.is_valid(h100));

    // The freed ID 100 is the one that gets reused.
    auto id_reused = v.push_back(999);
    EXPECT_EQ(id_reused, id_t{100});
    EXPECT_EQ(v[id_reused], 999);
    EXPECT_EQ(v.size(), 255U);
    // The old handle is still invalid even though the ID is live again.
    EXPECT_FALSE(v.is_valid(h100));
    auto h100_new = v.get_handle(id_reused);
    EXPECT_TRUE(v.is_valid(h100_new));
    EXPECT_GT(h100_new.gen(), h100.gen());

    // Full again — exceeds limit.
    EXPECT_THROW(v.push_back(0), std::out_of_range);
  }
}

void StableId_NoThrow() {
  using V = int_stable_small_ids;
  using id_t = V::id_t; // small_id_t : uint8_t, invalid = 255

  // Default is to throw.
  if (true) {
    V v;
    EXPECT_TRUE(v.throw_on_insert_failure());
  }

  // Accessor round-trips.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    EXPECT_FALSE(v.throw_on_insert_failure());
    v.throw_on_insert_failure(true);
    EXPECT_TRUE(v.throw_on_insert_failure());
  }

  // push_back returns invalid on overflow instead of throwing.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.size(), 255U);

    auto id = v.push_back(999);
    EXPECT_EQ(id, id_t::invalid);
    EXPECT_EQ(v.size(), 255U);
  }

  // emplace_back returns invalid on overflow instead of throwing.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    for (int i = 0; i < 255; ++i) (void)v.emplace_back(i);
    EXPECT_EQ(v.size(), 255U);

    auto id = v.emplace_back(999);
    EXPECT_EQ(id, id_t::invalid);
    EXPECT_EQ(v.size(), 255U);
  }

  // Re-enabling the flag restores throwing on overflow.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.push_back(999), id_t::invalid);

    v.throw_on_insert_failure(true);
    EXPECT_THROW(v.push_back(999), std::out_of_range);
    EXPECT_EQ(v.size(), 255U);
  }

  // Free-list reuse works normally with the flag off; only exceeding the limit
  // returns invalid.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);

    v.erase(id_t{50});
    EXPECT_EQ(v.size(), 254U);

    auto id = v.push_back(888);
    EXPECT_EQ(id, id_t{50});
    EXPECT_EQ(v[id], 888);
    EXPECT_EQ(v.size(), 255U);

    // Now truly full — returns invalid, does not throw.
    EXPECT_EQ(v.push_back(999), id_t::invalid);
    EXPECT_EQ(v.size(), 255U);
  }
}

void StableId_Fifo() {
  using V = int_stable_ids_fifo;
  using id_t = V::id_t;

  // Freed IDs are reused oldest-first (FIFO), not most-recent-first (LIFO).
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{0});      // free list: [0]
    v.erase(id_t{1});      // free list: [0, 1]
    // LIFO would give 1 then 0; FIFO gives 0 then 1.
    EXPECT_EQ(v.push_back(100), id_t{0});
    EXPECT_EQ(v.push_back(200), id_t{1});
  }

  // FIFO reuse order matches erase order, not ID order.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    (void)v.push_back(40); // id 3
    (void)v.push_back(50); // id 4
    v.erase(id_t{2});
    v.erase(id_t{0});
    v.erase(id_t{3});
    // Erase order was 2, 0, 3; reuse must follow that order.
    EXPECT_EQ(v.push_back(100), id_t{2});
    EXPECT_EQ(v.push_back(200), id_t{0});
    EXPECT_EQ(v.push_back(300), id_t{3});
  }

  // Interleaved free and alloc: each alloc pops the oldest free.
  if (true) {
    V v;
    (void)v.push_back(10);      // id 0
    (void)v.push_back(20);      // id 1
    (void)v.push_back(30);      // id 2
    v.erase(id_t{0});           // free: [0]
    v.erase(id_t{1});           // free: [0, 1]
    auto r0 = v.push_back(100); // pops 0; free: [1]
    EXPECT_EQ(r0, id_t{0});
    EXPECT_EQ(v[r0], 100);
    v.erase(id_t{2});           // free: [1, 2]
    auto r1 = v.push_back(200); // pops 1; free: [2]
    EXPECT_EQ(r1, id_t{1});
    EXPECT_EQ(v[r1], 200);
    auto r2 = v.push_back(300); // pops 2; free: []
    EXPECT_EQ(r2, id_t{2});
    EXPECT_EQ(v[r2], 300);
    // All live; next insert gets a fresh ID.
    EXPECT_EQ(v.push_back(400), id_t{3});
  }

  // next_id returns 0 on empty, the FIFO head when IDs are free, or the
  // next sequential value when the free list is empty.
  if (true) {
    V v;
    EXPECT_EQ(v.next_id(), id_t{0});
    (void)v.push_back(10);           // id 0
    (void)v.push_back(20);           // id 1
    (void)v.push_back(30);           // id 2
    EXPECT_EQ(v.next_id(), id_t{3}); // no free IDs
    v.erase(id_t{1});
    EXPECT_EQ(v.next_id(), id_t{1}); // head is 1
    v.erase(id_t{0});
    EXPECT_EQ(v.next_id(), id_t{1}); // head is still 1 (oldest freed)
  }

  // Handles are invalidated on FIFO reuse; gen is bumped on erase.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    auto h0 = v.get_handle(id0);
    EXPECT_EQ(h0.gen(), 0U);
    v.erase(id0);
    EXPECT_FALSE(v.is_valid(h0));
    auto id0_reused = v.push_back(99);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_FALSE(v.is_valid(h0)); // stale handle stays invalid
    auto h0_new = v.get_handle(id0_reused);
    EXPECT_TRUE(v.is_valid(h0_new));
    EXPECT_GT(h0_new.gen(), h0.gen());
  }

  // push_back_handle returns a correct handle after FIFO reuse.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    v.erase(id_t{0});
    auto h = v.push_back_handle(99);
    EXPECT_EQ(h.id(), id_t{0});
    EXPECT_TRUE(v.is_valid(h));
    EXPECT_EQ(h.gen(), 1U); // bumped once on erase
  }

  // Free all elements; reuse order matches erase order.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{2});
    v.erase(id_t{1});
    v.erase(id_t{0});
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.push_back(100), id_t{2});
    EXPECT_EQ(v.push_back(200), id_t{1});
    EXPECT_EQ(v.push_back(300), id_t{0});
  }

  // erase_if frees matching elements; subsequent allocs reuse them in
  // the order erase_if processed them (data-index scan, swap-and-pop).
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(25); // id 1
    (void)v.push_back(30); // id 2
    (void)v.push_back(5);  // id 3
    (void)v.push_back(15); // id 4
    auto cnt = v.erase_if([](int x) { return x > 20; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(v.size(), 3U);
    int sum{};
    for (auto val : v) sum += val;
    EXPECT_EQ(sum, 30); // 10 + 5 + 15
    // erase_if hits id 1 (val 25) first at data-index 1, then id 2
    // (val 30) at data-index 2 after the swap brings it into range.
    // FIFO reuses them in that order.
    EXPECT_EQ(v.push_back(100), id_t{1});
    EXPECT_EQ(v.push_back(200), id_t{2});
    EXPECT_EQ(v.size(), 5U);
  }

  // clear() without shrink rebuilds the FIFO list in position order;
  // with no prior swaps that matches ID order 0, 1, 2.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    auto h0 = v.get_handle(id_t{0});
    v.clear();
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.is_valid(h0));
    EXPECT_EQ(v.push_back(100), id_t{0});
    EXPECT_EQ(v.push_back(200), id_t{1});
    EXPECT_EQ(v.push_back(300), id_t{2});
    EXPECT_EQ(v.get_handle(id_t{0}).gen(), 1U); // bumped once by clear
  }

  // clear(true) frees all storage; next insert starts fresh.
  if (true) {
    V v;
    (void)v.push_back(10);
    (void)v.push_back(20);
    v.erase(id_t{0});
    v.clear(true);
    EXPECT_TRUE(v.empty());
    auto id = v.push_back(42);
    EXPECT_EQ(*id, 0U);
    EXPECT_EQ(v.get_handle(id).gen(), 0U);
  }

  // shrink_to_fit rebuilds the FIFO list; only free IDs below the new
  // table size survive.  IDs beyond max-live are discarded entirely.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    (void)v.push_back(40); // id 3
    (void)v.push_back(50); // id 4
    v.erase(id_t{3});
    v.erase(id_t{4});
    v.erase(id_t{0});
    // Live: ids 1, 2.  shrink trims to max(1,2)+1 = 3; only id 0 is a
    // free slot that fits.  Ids 3 and 4 are beyond the new table size.
    v.shrink_to_fit();
    EXPECT_EQ(v.size(), 2U);
    EXPECT_EQ(v[id_t{1}], 20);
    EXPECT_EQ(v[id_t{2}], 30);
    auto id_new = v.push_back(99);
    EXPECT_EQ(id_new, id_t{0});
    EXPECT_EQ(v[id_new], 99);
  }

  // swap exchanges the complete FIFO free-list state between containers.
  if (true) {
    V a, b;
    (void)a.push_back(10); // a: id 0
    (void)a.push_back(20); // a: id 1
    a.erase(id_t{0});      // a free list: [0]
    (void)b.push_back(30); // b: id 0
    (void)b.push_back(40); // b: id 1
    (void)b.push_back(50); // b: id 2
    b.erase(id_t{1});      // b free list: [1]
    b.erase(id_t{0});      // b free list: [1, 0]
    swap(a, b);
    // a now has b's old free list [1, 0]; oldest free is 1.
    EXPECT_EQ(a.push_back(100), id_t{1});
    EXPECT_EQ(a.push_back(200), id_t{0});
    // b now has a's old free list [0].
    EXPECT_EQ(b.push_back(300), id_t{0});
  }

  // Move construction transfers the FIFO free-list intact.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{0});      // free: [0]
    v.erase(id_t{2});      // free: [0, 2]
    V w{std::move(v)};
    EXPECT_EQ(w.push_back(100), id_t{0});
    EXPECT_EQ(w.push_back(200), id_t{2});
  }

  // FIFO reuse at small_id_t capacity limit.
  if (true) {
    using SV = int_stable_small_ids_fifo;
    using sid_t = SV::id_t;
    SV v;
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.size(), 255U);
    v.erase(sid_t{10});
    v.erase(sid_t{20});
    v.erase(sid_t{30});
    // FIFO order matches erase order: 10, 20, 30.
    EXPECT_EQ(v.push_back(100), sid_t{10});
    EXPECT_EQ(v.push_back(200), sid_t{20});
    EXPECT_EQ(v.push_back(300), sid_t{30});
    EXPECT_EQ(v.size(), 255U);
  }
}

void StableId_NoGen() {
  using V = int_stable_ids_nogen;
  using id_t = V::id_t;

  // Basic push, access, and size without generation tracking.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(v[id0], 10);
    EXPECT_EQ(v[id1], 20);
    EXPECT_EQ(v.size(), 2U);
  }

  // handle_t is exactly sizeof(id_t): the gen field is zero-size via
  // [[no_unique_address]].  Smaller than the default (gen-enabled) handle.
  if (true) {
    static_assert(sizeof(V::handle_t) == sizeof(V::id_t));
    using WithGen = int_stable_ids;
    static_assert(sizeof(V::handle_t) < sizeof(WithGen::handle_t));
  }

  // is_valid detects free IDs; erase makes them invalid.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    EXPECT_TRUE(v.is_valid(id0));
    v.erase(id0);
    EXPECT_FALSE(v.is_valid(id0));
  }

  // A handle for a free (not-yet-reused) ID is detected as invalid.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    auto h0 = v.get_handle(id0);
    v.erase(id0);
    EXPECT_FALSE(v.is_valid(h0));
    EXPECT_THROW(v.at(h0), std::invalid_argument);
  }

  // Without gen, a stale handle for a *reused* ID is indistinguishable
  // from a fresh one: is_valid returns true, at() returns the new value.
  // This is the documented trade-off of UseGen=false.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    auto h0 = v.get_handle(id0); // snapshot while id 0 holds 10
    v.erase(id0);
    (void)v.push_back(99);       // reuses id 0 (LIFO)
    EXPECT_TRUE(v.is_valid(h0)); // indistinguishable: ID is live
    EXPECT_EQ(v.at(h0), 99);     // returns new value, not original 10
  }

  // LIFO reuse: most recently freed ID is reused first.  Contrast with
  // the FIFO variant where the same erase order would yield 0 then 1.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{0});      // freed first
    v.erase(id_t{1});      // freed second (most recent)
    // LIFO: id 1 freed last, so it's reused first.
    EXPECT_EQ(v.push_back(100), id_t{1});
    EXPECT_EQ(v.push_back(200), id_t{0});
  }

  // Erase-reinsert cycle: values and IDs stay consistent.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{1});
    auto r = v.push_back(99);
    EXPECT_EQ(r, id_t{1});
    EXPECT_EQ(v[r], 99);
    EXPECT_EQ(v[id_t{0}], 10);
    EXPECT_EQ(v[id_t{2}], 30);
    EXPECT_EQ(v.size(), 3U);
  }

  // clear() without shrink: all IDs become reusable; no gen to bump.
  if (true) {
    V v;
    (void)v.push_back(10);
    (void)v.push_back(20);
    v.clear();
    EXPECT_TRUE(v.empty());
    auto id0 = v.push_back(100);
    auto id1 = v.push_back(200);
    EXPECT_EQ(id0, id_t{0});
    EXPECT_EQ(id1, id_t{1});
    EXPECT_EQ(v[id0], 100);
    EXPECT_EQ(v[id1], 200);
  }

  // clear(true) resets the container entirely.
  if (true) {
    V v;
    (void)v.push_back(10);
    v.clear(true);
    EXPECT_TRUE(v.empty());
    auto id = v.push_back(42);
    EXPECT_EQ(*id, 0U);
    EXPECT_EQ(v[id], 42);
  }
}

void StableId_FifoNoGen() {
  using V = int_stable_ids_fifo_nogen;
  using id_t = V::id_t;

  // FIFO reuse order is maintained without generation tracking.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{0});      // free: [0]
    v.erase(id_t{2});      // free: [0, 2]
    EXPECT_EQ(v.push_back(100), id_t{0});
    EXPECT_EQ(v.push_back(200), id_t{2});
  }

  // Interleaved free and alloc follow FIFO order without gen.
  if (true) {
    V v;
    (void)v.push_back(10);      // id 0
    (void)v.push_back(20);      // id 1
    (void)v.push_back(30);      // id 2
    (void)v.push_back(40);      // id 3
    v.erase(id_t{1});           // free: [1]
    v.erase(id_t{3});           // free: [1, 3]
    auto r0 = v.push_back(100); // pops 1
    EXPECT_EQ(r0, id_t{1});
    v.erase(id_t{0});           // free: [3, 0]
    auto r1 = v.push_back(200); // pops 3
    EXPECT_EQ(r1, id_t{3});
    auto r2 = v.push_back(300); // pops 0
    EXPECT_EQ(r2, id_t{0});
  }

  // Values are correct after FIFO reuse.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{1});
    auto r = v.push_back(99);
    EXPECT_EQ(r, id_t{1});
    EXPECT_EQ(v[r], 99);
    EXPECT_EQ(v[id_t{0}], 10);
    EXPECT_EQ(v[id_t{2}], 30);
  }

  // handle_t is sizeof(id_t): neither gen nor the FIFO next-pointer
  // appears in it.  The next-pointer lives in the internal slot_t only.
  if (true) { static_assert(sizeof(V::handle_t) == sizeof(V::id_t)); }

  // Without gen, a stale handle for a reused ID is indistinguishable.
  // FIFO increases the reuse delay but is not a correctness guard.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    auto h0 = v.get_handle(id0);
    v.erase(id0);
    v.erase(id_t{1}); // id 0 is oldest; next alloc reuses it
    (void)v.push_back(99);
    EXPECT_TRUE(v.is_valid(h0)); // indistinguishable: ID is live again
    EXPECT_EQ(v.at(h0), 99);
  }

  // clear() without shrink rebuilds the FIFO list in position order.
  if (true) {
    V v;
    (void)v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    v.clear();
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.push_back(100), id_t{0});
    EXPECT_EQ(v.push_back(200), id_t{1});
    EXPECT_EQ(v.push_back(300), id_t{2});
  }
}

// Test the max_id() setting to limit ID allocation.
void StableId_MaxId() {
  using id_t = int_stable_ids::id_t;
  using V = stable_ids<int, id_t>;

  // Can allocate up to max.
  if (true) {
    // Limit to 3 IDs (0, 1, 2).
    V v{id_t{3}};
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    auto id2 = v.push_back(30);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(*id2, 2U);
    EXPECT_EQ(v.size(), 3U);
  }

  // The 4th insertion overflows.
  if (true) {
    V v{id_t{3}};
    (void)v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    EXPECT_THROW(v.push_back(40), std::out_of_range);
    EXPECT_EQ(v.size(), 3U);
  }

  // With throw disabled, returns invalid.
  if (true) {
    V v{id_t{3}};
    v.throw_on_insert_failure(false);
    (void)v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    auto id3 = v.push_back(40);
    EXPECT_EQ(id3, id_t::invalid);
    EXPECT_EQ(v.size(), 3U);
  }

  // Erasing frees a slot for reuse.
  if (true) {
    V v{id_t{3}};
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    EXPECT_EQ(v.size(), 3U);

    v.erase(id0);
    EXPECT_EQ(v.size(), 2U);

    // Can now insert again, reusing the freed ID.
    auto id_reused = v.push_back(40);
    EXPECT_EQ(id_reused, id0);
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[id_reused], 40);

    // Full again — exceeds limit.
    EXPECT_THROW(v.push_back(50), std::out_of_range);
  }

  // set_id_limit on empty container always succeeds.
  if (true) {
    V v;
    EXPECT_EQ(v.id_limit(), id_t::invalid);
    EXPECT_TRUE(v.set_id_limit(id_t{2}));
    EXPECT_EQ(v.id_limit(), id_t{2});

    (void)v.push_back(10);
    (void)v.push_back(20);
    EXPECT_THROW(v.push_back(30), std::out_of_range);
  }

  // set_id_limit fails if it would invalidate live IDs.
  if (true) {
    V v;
    (void)v.push_back(10); // ID 0
    (void)v.push_back(20); // ID 1
    (void)v.push_back(30); // ID 2

    // Can't set limit to 2 because ID 2 is live (limit means IDs 0..limit-1).
    EXPECT_FALSE(v.set_id_limit(id_t{2}));
    EXPECT_EQ(v.id_limit(), id_t::invalid); // Unchanged.

    // Can set limit to 3 (IDs 0,1,2 are valid).
    EXPECT_TRUE(v.set_id_limit(id_t{3}));
    EXPECT_EQ(v.id_limit(), id_t{3});

    // Can raise the limit.
    EXPECT_TRUE(v.set_id_limit(id_t{10}));
    EXPECT_EQ(v.id_limit(), id_t{10});
  }

  // set_id_limit with freed slots beyond the new limit triggers shrink.
  if (true) {
    V v;
    (void)v.push_back(10); // ID 0
    (void)v.push_back(20); // ID 1
    (void)v.push_back(30); // ID 2
    v.erase(id_t{2});      // Free ID 2, max_id() still 2.

    EXPECT_EQ(v.max_id(), id_t{2});
    EXPECT_EQ(v.find_max_extant_id(), id_t{1});

    // Setting limit to 2 should succeed and shrink (ID 2 is freed).
    EXPECT_TRUE(v.set_id_limit(id_t{2}));
    EXPECT_EQ(v.id_limit(), id_t{2});
    // After shrink, max_id() should equal find_max_extant_id().
    EXPECT_EQ(v.max_id(), id_t{1});
  }

  // set_id_limit on empty container with freed slots clears them.
  if (true) {
    V v;
    (void)v.push_back(10); // ID 0
    (void)v.push_back(20); // ID 1
    v.erase(id_t{0});
    v.erase(id_t{1});
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.max_id(), id_t{1}); // High-water mark is still 1.

    // Setting a lower limit should clear the freed slots.
    EXPECT_TRUE(v.set_id_limit(id_t{1}));
    EXPECT_EQ(v.id_limit(), id_t{1});
  }

  // Prefill constructor pre-allocates slots.
  if (true) {
    V v{id_t{5}, true};
    EXPECT_EQ(v.id_limit(), id_t{5});
    // Slots are pre-allocated, so push_back won't allocate indexes_/reverse_.
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
  }
}

void EntityRegistry_Basic() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};
  const loc_t loc1{store_id_t{0}, 1};
  const loc_t loc2{store_id_t{0}, 2};
  const loc_t loc_s1{store_id_t{1}, 0};

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

  // create with invalid store_id fails.
  if (true) {
    reg_t r;
    auto id = r.create_id(loc_t{});
    EXPECT_EQ(id, id_t::invalid);
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
    EXPECT_EQ(*loc.store_id, 1U);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // set_location by ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    r.set_location(id0, loc_s1);
    const auto& loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 1U);
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
    auto cnt = r.erase_if([](auto& rec) { return rec.metadata > 10; });
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
  const loc_t loc0{store_id_t{0}, 0};
  const loc_t loc1{store_id_t{0}, 1};

  // create_with_handle returns a valid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t{0});
    EXPECT_EQ(h.gen(), 0U);
  }

  // create_with_handle with invalid store_id returns invalid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc_t{}, 42);
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t::invalid);
  }

  // get_handle for valid ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
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
    auto id0 = r.create_id(loc0, 10);
    auto h = r.get_handle(id0);
    r.erase(id0);
    EXPECT_FALSE(r.is_valid(h));
  }

  // Stale handle is invalid even after ID reuse (gen mismatch).
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h_old = r.get_handle(id0);
    EXPECT_EQ(h_old.gen(), 0U);
    r.erase(id0);
    auto id0_reused = r.create_id(loc0, 99);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_FALSE(r.is_valid(h_old));
    auto h_new = r.get_handle(id0_reused);
    EXPECT_TRUE(r.is_valid(h_new));
    EXPECT_GT(h_new.gen(), h_old.gen());
  }

  // erase by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    EXPECT_TRUE(r.erase(h));
    EXPECT_FALSE(r.is_valid(h));
    // Double erase by handle fails.
    EXPECT_FALSE(r.erase(h));
  }

  // Metadata access by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    EXPECT_EQ(r.at(h), 42);
    r.at(h) = 100;
    EXPECT_EQ(r.at(h), 100);
    const auto& cr = r;
    EXPECT_EQ(cr.at(h), 100);
  }

  // Metadata access by invalid handle throws.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    r.erase(h);
    EXPECT_THROW(r.at(h), std::invalid_argument);
  }

  // get_location by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 0U);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // set_location by handle.
  if (true) {
    reg_t r;
    loc_t loc_new{store_id_t{2}, 5};
    auto h = r.create_handle(loc0, 10);
    r.set_location(h, loc_new);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 2U);
    EXPECT_EQ(loc.ndx, 5U);
  }

  // set_location by invalid handle is a no-op.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
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
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Freed IDs are reused in FIFO order (oldest first).
  // Detailed FIFO behavior is tested in StableId_Fifo.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc0, 20);
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id0);                // free: [0]
    r.erase(id1);                // free: [0, 1]
    // FIFO: 0 was freed first, so it's reused first.
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 200), id_t{1});
  }
}

void EntityRegistry_Clear() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // clear() without shrink: IDs reusable, gens bumped.
  // Detailed clear behavior is tested in StableId_Basic and StableId_Fifo.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    auto h0 = r.get_handle(id_t{0});
    r.clear();
    EXPECT_FALSE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(h0));
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    EXPECT_EQ(r.get_handle(id_t{0}).gen(), 1U);
  }

  // clear(true) with shrink: fully resets storage and gens.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    r.clear(true);
    EXPECT_EQ(r.size(), 0U);
    EXPECT_FALSE(r.is_valid(id_t{0}));
    auto id0 = r.create_id(loc0, 100);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(r.get_handle(id0).gen(), 0U); // gen reset
  }
}

void EntityRegistry_Reserve() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // reserve without prefill: just reserves capacity, no IDs allocated.
  if (true) {
    reg_t r;
    r.reserve(10);
    EXPECT_FALSE(r.is_valid(id_t{0}));
    auto id0 = r.create_id(loc0, 42);
    EXPECT_EQ(*id0, 0U);
  }

  // reserve with prefill: pre-creates free slots.
  if (true) {
    reg_t r;
    r.reserve(5, true);
    // Slots exist but are not valid (free).
    EXPECT_FALSE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{4}));
    // create reuses prefilled slots in order.
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 30), id_t{2});
    EXPECT_EQ(r.create_id(loc0, 40), id_t{3});
    EXPECT_EQ(r.create_id(loc0, 50), id_t{4});
  }

  // Prefill constructor.
  if (true) {
    reg_t r{id_t{5}, true};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
  }

  // shrink_to_fit trims trailing dead records.
  // Detailed shrink behavior is tested in StableId_Basic and StableId_Fifo.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
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
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Constructor with limit and overflow.
  // Detailed id_limit behavior is tested in StableId_MaxId.
  if (true) {
    reg_t r{id_t{3}};
    EXPECT_EQ(r.id_limit(), id_t{3});
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 30), id_t{2});
    // 4th creation fails.
    EXPECT_EQ(r.create_id(loc0, 40), id_t::invalid);
  }

  // set_id_limit on empty registry.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.id_limit(), id_t::invalid);
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t{2});
    (void)r.create_id(loc0, 10);
    (void)r.create_id(loc0, 20);
    EXPECT_EQ(r.create_id(loc0, 30), id_t::invalid);
  }
}

void EntityRegistry_NoGen() {
  using namespace id_enums;
  using reg_t = entity_registry<int, entity_id_t, store_id_t, false>;
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
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc0, 20);
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

  // create_with_handle with invalid store_id returns invalid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc_t{}, 42);
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t::invalid);
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
    auto id0 = r.create_id(loc0, 10);
    auto h_old = r.get_handle(id0);
    r.erase(id0);
    EXPECT_FALSE(r.is_valid(h_old));
    auto id0_reused = r.create_id(loc0, 99);
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
  const loc_t loc0{store_id_t{0}, 0};
  const loc_t loc1{store_id_t{0}, 1};

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
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    (void)r.create_id(loc0); // id 2
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.create_id(loc0), id_t{0});
    EXPECT_EQ(r.create_id(loc0), id_t{1});
  }

  // Handles work with void metadata.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.gen(), 0U);
    r.erase(h);
    EXPECT_FALSE(r.is_valid(h));
  }

  // Location operations.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0);
    const auto& loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 0U);
    EXPECT_EQ(loc.ndx, 0U);
    r.set_location(id0, loc1);
    const auto& loc_after = r.get_location(id0);
    EXPECT_EQ(loc_after.ndx, 1U);
  }

  // clear and reserve with prefill.
  if (true) {
    reg_t r;
    r.reserve(5, true);
    EXPECT_EQ(r.create_id(loc0), id_t{0});
    EXPECT_EQ(r.create_id(loc0), id_t{1});
    r.clear();
    EXPECT_EQ(r.create_id(loc0), id_t{0});
  }

  // erase_if with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    (void)r.create_id(loc0); // id 2
    auto cnt = r.erase_if([](auto& rec) { return rec.location.ndx == 0; });
    // All have ndx 0 from loc0, so all erased.
    EXPECT_EQ(cnt, 3U);
    EXPECT_EQ(r.size(), 0U);
  }

  // shrink_to_fit with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    (void)r.create_id(loc0); // id 2
    r.erase(id_t{2});
    r.shrink_to_fit();
    EXPECT_EQ(r.max_id(), id_t{1});
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_TRUE(r.is_valid(id_t{1}));
  }

  // set_id_limit with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    (void)r.create_id(loc0); // id 2
    r.erase(id_t{2});
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    // id 2 was trimmed by set_id_limit, and limit prevents new id >= 2.
    EXPECT_EQ(r.id_limit(), id_t{2});
    EXPECT_EQ(r.size(), 2U);
  }

  // Handle with generation tracking (void metadata).
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0);
    auto h_old = r.get_handle(id0);
    EXPECT_EQ(h_old.gen(), 0U);
    r.erase(id0);
    auto id0_reused = r.create_id(loc0);
    EXPECT_EQ(id0_reused, id0);
    auto h_new = r.get_handle(id0_reused);
    EXPECT_EQ(h_new.gen(), 1U);
    EXPECT_FALSE(r.is_valid(h_old));
    EXPECT_TRUE(r.is_valid(h_new));
  }
}

void EntityRegistry_VoidNoGen() {
  using namespace id_enums;
  using reg_t = entity_registry<void, entity_id_t, store_id_t, false>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Minimal footprint: no metadata, no gen.
  if (true) { static_assert(sizeof(reg_t::handle_t) == sizeof(id_t)); }

  // Create and validate.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0);
    auto id1 = r.create_id(loc0);
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
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.create_id(loc0), id_t{0});
    EXPECT_EQ(r.create_id(loc0), id_t{1});
  }
}

void EntityRegistry_IdLimitAdvanced() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // set_id_limit fails when live IDs exist at or past the new limit.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    EXPECT_FALSE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t::invalid); // unchanged
  }

  // set_id_limit succeeds when only dead IDs exist past the limit; triggers
  // shrink.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{2});
    EXPECT_EQ(r.max_id(), id_t{2});
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t{2});
    EXPECT_EQ(r.max_id(), id_t{1}); // trimmed
  }

  // Raising the limit always succeeds.
  if (true) {
    reg_t r{id_t{3}};
    (void)r.create_id(loc0, 10);
    (void)r.create_id(loc0, 20);
    (void)r.create_id(loc0, 30);
    EXPECT_EQ(r.create_id(loc0, 40), id_t::invalid); // at limit
    EXPECT_TRUE(r.set_id_limit(id_t{5}));
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id(loc0, 40), id_t{3}); // now succeeds
  }

  // set_id_limit on empty registry with freed slots beyond limit.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.size(), 0U);
    EXPECT_TRUE(r.set_id_limit(id_t{1}));
    EXPECT_EQ(r.id_limit(), id_t{1});
    // Only id 0 should be available for reuse.
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 200), id_t::invalid); // at limit
  }
}

void EntityRegistry_FifoAdvanced() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Interleaved erase/create: each create pops the oldest free.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10);      // id 0
    (void)r.create_id(loc0, 20);      // id 1
    (void)r.create_id(loc0, 30);      // id 2
    r.erase(id_t{0});                 // free: [0]
    r.erase(id_t{1});                 // free: [0, 1]
    auto r0 = r.create_id(loc0, 100); // pops 0; free: [1]
    EXPECT_EQ(r0, id_t{0});
    r.erase(id_t{2});                 // free: [1, 2]
    auto r1 = r.create_id(loc0, 200); // pops 1; free: [2]
    EXPECT_EQ(r1, id_t{1});
    auto r2 = r.create_id(loc0, 300); // pops 2; free: []
    EXPECT_EQ(r2, id_t{2});
    // All live; next gets fresh ID.
    EXPECT_EQ(r.create_id(loc0, 400), id_t{3});
  }

  // FIFO reuse order matches erase order, not ID order.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    (void)r.create_id(loc0, 50); // id 4
    r.erase(id_t{2});
    r.erase(id_t{0});
    r.erase(id_t{3});
    EXPECT_EQ(r.create_id(loc0, 100), id_t{2});
    EXPECT_EQ(r.create_id(loc0, 200), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 300), id_t{3});
  }

  // Free all then re-create: FIFO order matches erase order.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{2});
    r.erase(id_t{1});
    r.erase(id_t{0});
    EXPECT_TRUE(r.size() == 0U);
    EXPECT_EQ(r.create_id(loc0, 100), id_t{2});
    EXPECT_EQ(r.create_id(loc0, 200), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 300), id_t{0});
  }

  // FIFO order after clear() without shrink: rebuild scans in order.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.clear();
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 200), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 300), id_t{2});
  }
}

void EntityRegistry_EdgeCases() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // max_id() on empty registry returns invalid.
  if (true) {
    reg_t r;
    // records_.size() is 0, so size()-1 underflows to max value = invalid.
    EXPECT_EQ(r.max_id(), id_t::invalid);
  }

  // create with default metadata parameter.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0); // uses default metadata_t{}
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_EQ(r[id0], 0); // default-initialized int
  }

  // erase_if with no matches returns 0.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 5);
    (void)r.create_id(loc0, 10);
    auto cnt = r.erase_if([](auto& rec) { return rec.metadata > 100; });
    EXPECT_EQ(cnt, 0U);
    EXPECT_EQ(r.size(), 2U);
  }

  // erase_if on empty registry.
  if (true) {
    reg_t r;
    auto cnt = r.erase_if([](auto&) { return true; });
    EXPECT_EQ(cnt, 0U);
  }

  // shrink_to_fit with interior dead: only trims trailing dead.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{1});            // interior dead
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
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
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
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
  }

  // Prefill constructor with prefill=false: just sets limit, no slots.
  if (true) {
    reg_t r{id_t{5}, false};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
  }

  // clear() bumps generation counters.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h = r.get_handle(id0);
    EXPECT_EQ(h.gen(), 0U);
    r.clear();
    EXPECT_FALSE(r.is_valid(h));
    auto id0_new = r.create_id(loc0, 20);
    EXPECT_EQ(id0_new, id0);
    auto h_new = r.get_handle(id0_new);
    EXPECT_EQ(h_new.gen(), 1U);
    EXPECT_TRUE(h != h_new);
  }

  // Multiple erase/reuse cycles bump gen each time.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h0 = r.get_handle(id0);
    EXPECT_EQ(h0.gen(), 0U);
    r.erase(id0);
    auto id0_r1 = r.create_id(loc0, 20);
    EXPECT_EQ(id0_r1, id0);
    auto h1 = r.get_handle(id0_r1);
    EXPECT_EQ(h1.gen(), 1U);
    r.erase(id0_r1);
    auto id0_r2 = r.create_id(loc0, 30);
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
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    (void)r.create_id(loc0, 50); // id 4
    r.erase(id_t{3});
    r.erase(id_t{4});
    r.erase(id_t{0});
    // Live: 1, 2. Trailing dead: 3, 4. Interior dead: 0.
    r.shrink_to_fit();
    // After shrink: records 0..2; 3 and 4 trimmed.
    // Free list rebuilt: only id 0 is free.
    EXPECT_EQ(r.size(), 2U);
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    // No more free; next gets fresh id 3.
    EXPECT_EQ(r.create_id(loc0, 200), id_t{3});
  }

  // size() and is_valid consistency after mixed operations.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.size(), 0U);
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc0, 20);
    auto id2 = r.create_id(loc0, 30);
    EXPECT_EQ(r.size(), 3U);
    r.erase(id1);
    EXPECT_EQ(r.size(), 2U);
    auto id3 = r.create_id(loc0, 40); // reuses id 1
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
    reg_t r{id_t{5}, true, alloc};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
  }

  // Prefill constructor with prefill=false: just sets limit.
  if (true) {
    reg_t r{id_t::invalid, false};
    EXPECT_EQ(r.id_limit(), id_t::invalid);
  }

  // Prefill constructor with id_limit{0}: early return, can't create.
  if (true) {
    reg_t r{id_t{0}};
    EXPECT_EQ(r.id_limit(), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 10), id_t::invalid);
  }

  // set_id_limit to 0: no IDs allowed.
  if (true) {
    reg_t r;
    EXPECT_TRUE(r.set_id_limit(id_t{0}));
    EXPECT_EQ(r.id_limit(), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 10), id_t::invalid);
  }

  // set_id_limit to same value: no-op.
  if (true) {
    reg_t r{id_t{3}};
    (void)r.create_id(loc0, 10);
    EXPECT_TRUE(r.set_id_limit(id_t{3}));
    EXPECT_EQ(r.id_limit(), id_t{3});
    EXPECT_TRUE(r.is_valid(id_t{0}));
  }

  // erase_if erases all when all match.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 5);
    (void)r.create_id(loc0, 10);
    (void)r.create_id(loc0, 15);
    auto cnt = r.erase_if([](auto&) { return true; });
    EXPECT_EQ(cnt, 3U);
    EXPECT_EQ(r.size(), 0U);
  }

  // shrink_to_fit on all-dead registry trims to empty.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10);
    (void)r.create_id(loc0, 20);
    r.erase(id_t{0});
    r.erase(id_t{1});
    r.shrink_to_fit();
    EXPECT_EQ(r.size(), 0U);
    // After full trim, fresh IDs start from 0.
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
  }
}

void EntityRegistry_MetadataCleanup() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Metadata is cleared on erase; re-creation with default gets 0, not stale.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    EXPECT_EQ(r[id0], 42);
    r.erase(id0);
    auto id0_reused = r.create_id(loc0);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_EQ(r[id0_reused], 0);
  }

  // Metadata is cleared on erase even when re-created with explicit value.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 999);
    r.erase(id0);
    auto id0_reused = r.create_id(loc0, 7);
    EXPECT_EQ(r[id0_reused], 7);
  }

  // Metadata is cleared on clear(false); re-creation with default gets 0.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 100);
    (void)r.create_id(loc0, 200);
    (void)r.create_id(loc0, 300);
    r.clear();
    EXPECT_EQ(r.create_id(loc0), id_t{0});
    EXPECT_EQ(r[id_t{0}], 0);
    EXPECT_EQ(r.create_id(loc0), id_t{1});
    EXPECT_EQ(r[id_t{1}], 0);
    EXPECT_EQ(r.create_id(loc0), id_t{2});
    EXPECT_EQ(r[id_t{2}], 0);
  }

  // Metadata is cleared on clear(true); re-creation with default gets 0.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 100);
    (void)r.create_id(loc0, 200);
    r.clear(true);
    auto id0 = r.create_id(loc0);
    EXPECT_EQ(r[id0], 0);
  }
}

void EntityRegistry_EraseIfPredicate() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Predicate is only called for valid (live) records, not dead ones.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    r.erase(id_t{1});
    r.erase(id_t{3});
    // 2 live, 2 dead. Predicate should be called exactly twice.
    size_t call_count = 0;
    auto cnt = r.erase_if([&](auto& rec) {
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
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Reducing limit trims free-list entries while interior live records remain.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    (void)r.create_id(loc0, 50); // id 4
    r.erase(id_t{1});            // interior dead
    r.erase(id_t{3});            // will be trimmed
    r.erase(id_t{4});            // will be trimmed
    // Live: 0, 2. Dead: 1, 3, 4. Trim to limit 3 removes 3 and 4.
    EXPECT_TRUE(r.set_id_limit(id_t{3}));
    EXPECT_EQ(r.size(), 2U);
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{1}));
    EXPECT_TRUE(r.is_valid(id_t{2}));
    // Free list should only contain id 1.
    EXPECT_EQ(r.create_id(loc0, 60), id_t{1});
    // At limit now: 3 live, limit is 3.
    EXPECT_EQ(r.create_id(loc0, 70), id_t::invalid);
  }
}

void EntityRegistry_ReservePrefillExisting() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // reserve with prefill on a registry that already has live entities.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10); // id 0
    auto id1 = r.create_id(loc0, 20); // id 1
    EXPECT_EQ(r.size(), 2U);
    r.reserve(5, true); // adds free slots 2, 3, 4
    // Existing entities are undisturbed.
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_EQ(r[id0], 10);
    EXPECT_EQ(r[id1], 20);
    // New creates should use the prefilled free slots.
    EXPECT_EQ(r.create_id(loc0, 30), id_t{2});
    EXPECT_EQ(r.create_id(loc0, 40), id_t{3});
    EXPECT_EQ(r.create_id(loc0, 50), id_t{4});
    // Next create expands.
    EXPECT_EQ(r.create_id(loc0, 60), id_t{5});
  }

  // reserve with prefill when some slots are already free.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{1});            // free: [1]
    r.reserve(5, true);          // adds free slots 3, 4
    // Free list should be: 1 (existing), then 3, 4 (new).
    EXPECT_EQ(r.create_id(loc0, 40), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 50), id_t{3});
    EXPECT_EQ(r.create_id(loc0, 60), id_t{4});
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

  // make_owner factory method.
  if (true) {
    reg_t r;
    auto o = r.make_owner(staging, 77);
    EXPECT_TRUE(bool(o));
    EXPECT_NE(o.id(), id_t::invalid);
    EXPECT_EQ(r[o.id()], 77);
  }

  // Creating constructor: failure (registry at limit) → owner is empty.
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
      auto o = r.make_owner(staging, 10);
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
    auto o = r.make_owner(staging, 10);
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
    auto o = r.make_owner(staging, 10);
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
    auto o1 = r.make_owner(staging, 10);
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
    auto o1 = r.make_owner(staging, 10);
    auto o2 = r.make_owner(staging, 20);
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
    auto o1 = r.make_owner(staging, 10);
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
    auto o = r.make_owner(staging, 10);
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
    auto o = r.make_owner(staging, 10);
    EXPECT_TRUE(&o.registry() == &r);
  }

  // const registry() returns a const reference.
  if (true) {
    reg_t r;
    const auto o = r.make_owner(staging, 10);
    const reg_t& cr = o.registry();
    EXPECT_TRUE(&cr == &r);
  }

  // RAII pattern: early return erases entity; success path releases it.
  if (true) {
    reg_t r;
    id_t saved_id = id_t::invalid;

    auto do_work = [&](bool succeed) -> bool {
      auto o = r.make_owner(staging, 10);
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
    auto o = r.make_owner(vstaging);
    EXPECT_TRUE(bool(o));
    auto id = o.id();
    EXPECT_TRUE(r.is_valid(id));
    o.reset();
    EXPECT_FALSE(r.is_valid(id));
  }
}

void ComponentStorage_Basic() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Construction.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0U);
    EXPECT_EQ(*s.store_id(), *sid);
  }

  // Construction with invalid store_id throws.
  if (true) {
    reg_t r;
    EXPECT_THROW(storage_t(r, store_id_t::invalid), std::invalid_argument);
    EXPECT_THROW(storage_t(r, store_id_t{}), std::invalid_argument);
  }

  // Default construction.
  if (true) {
    storage_t s;
    EXPECT_TRUE(s.empty());
  }

  // Add and lookup.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_EQ(s.size(), 2U);
    EXPECT_EQ(s[id0], 1.0f);
    EXPECT_EQ(s[id1], 2.0f);
    EXPECT_EQ(s.at(id0), 1.0f);
    EXPECT_EQ(s.at(id1), 2.0f);
  }

  // Const access.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 3.14f));
    const auto& cs = s;
    EXPECT_EQ(cs[id0], 3.14f);
    EXPECT_EQ(cs.at(id0), 3.14f);
  }

  // Mutable access via operator[].
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    s[id0] = 99.0f;
    EXPECT_EQ(s[id0], 99.0f);
  }

  // contains by ID.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_FALSE(s.contains(id0));
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.contains(id0));
    EXPECT_FALSE(s.contains(id_t{99}));
  }

  // at() throws for entity not in this storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_THROW(s.at(id_t{0}), std::out_of_range);
  }

  // Registry location updated on add.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    const auto loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, *sid);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // add returns false for entity not at store 0.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_FALSE(s.add(id0, 2.0f));
  }

  // add_new creates entity and adds component; returns its handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h0 = s.add_new(1.0f, 42);
    EXPECT_TRUE(bool(h0));
    EXPECT_EQ(s.size(), 1U);
    EXPECT_EQ(s[h0.id()], 1.0f);
    EXPECT_EQ(r[h0.id()], 42);
    EXPECT_TRUE(s.contains(h0));
    const auto loc = r.get_location(h0);
    EXPECT_EQ(*loc.store_id, *sid);
  }

  // add_new with default metadata.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h0 = s.add_new(5.5f);
    EXPECT_TRUE(bool(h0));
    EXPECT_EQ(s[h0.id()], 5.5f);
    EXPECT_EQ(r[h0.id()], 0);
  }

  // reserve.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.reserve(100);
    EXPECT_TRUE(s.empty());
  }

  // add_new(metadata, component) metadata-first overload.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h0 = s.add_new(42, 2.5f);
    EXPECT_TRUE(bool(h0));
    EXPECT_EQ(s[h0.id()], 2.5f);
    EXPECT_EQ(r[h0.id()], 42);
  }

  // add_new(metadata) with default-constructed component.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h0 = s.add_new(99);
    EXPECT_TRUE(bool(h0));
    EXPECT_EQ(s[h0.id()], 0.0f);
    EXPECT_EQ(r[h0.id()], 99);
  }

  // tag_t alias matches the template tag parameter.
  if (true) {
    struct MyTag {};
    using tagged_t = component_storage<reg_t, float, MyTag>;
    static_assert(std::is_same_v<tagged_t::tag_t, MyTag>);
    static_assert(std::is_same_v<storage_t::tag_t, void>);
    // Tagged and untagged storages are distinct types.
    static_assert(!std::is_same_v<tagged_t, storage_t>);
  }
}

void ComponentStorage_Handle() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // add by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 1.0f));
    EXPECT_EQ(s.size(), 1U);
    EXPECT_EQ(s[h.id()], 1.0f);
  }

  // add by invalid handle returns false.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    r.erase(h);
    EXPECT_FALSE(s.add(h, 1.0f));
  }

  // contains by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_FALSE(s.contains(h));
    EXPECT_TRUE(s.add(h, 1.0f));
    EXPECT_TRUE(s.contains(h));
  }

  // contains by stale handle returns false.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 1.0f));
    auto eid = h.id();
    (void)s.erase(eid);
    EXPECT_FALSE(s.contains(h));
  }

  // at by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 3.14f));
    EXPECT_EQ(s.at(h), 3.14f);
    s.at(h) = 99.0f;
    EXPECT_EQ(s.at(h), 99.0f);
  }

  // at by handle const.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 3.14f));
    const auto& cs = s;
    EXPECT_EQ(cs.at(h), 3.14f);
  }

  // at by invalid handle throws.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_THROW(s.at(h), std::invalid_argument);
  }

  // remove by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 1.0f));
    EXPECT_TRUE(s.remove(h));
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(r.is_valid(h));
    const auto loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 0U);
  }

  // remove by invalid handle returns false.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_FALSE(s.remove(h));
  }

  // erase by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 1.0f));
    EXPECT_TRUE(s.erase(h));
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(r.is_valid(h));
  }

  // erase by invalid handle returns false.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_FALSE(s.erase(h));
  }
}

void ComponentStorage_Remove() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Remove moves entity to store 0, entity remains valid.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.remove(id0));
    EXPECT_FALSE(s.contains(id0));
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(r.is_valid(id0));
    const auto loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 0U);
  }

  // Remove returns false for entity not in this storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_FALSE(s.remove(id_t{0}));
  }

  // Swap-and-pop: remove non-last element.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    EXPECT_TRUE(s.remove(id0));
    EXPECT_EQ(s.size(), 2U);
    EXPECT_EQ(s[id2], 3.0f);
    EXPECT_EQ(r.get_location(id2).ndx, 0U);
    EXPECT_EQ(s[id1], 2.0f);
    EXPECT_EQ(r.get_location(id1).ndx, 1U);
  }

  // Remove last element (no swap needed).
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.remove(id1));
    EXPECT_EQ(s.size(), 1U);
    EXPECT_EQ(s[id0], 1.0f);
  }

  // Remove allows re-add.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.remove(id0));
    EXPECT_TRUE(s.add(id0, 99.0f));
    EXPECT_EQ(s[id0], 99.0f);
  }
}

void ComponentStorage_RemoveAll() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // remove_all moves all entities to store 0.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    s.remove_all();
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_TRUE(r.is_valid(id2));
    EXPECT_EQ(*r.get_location(id0).store_id, 0U);
  }

  // remove_all on empty is a no-op.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.remove_all();
    EXPECT_TRUE(s.empty());
  }

  // Can re-add after remove_all.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    s.remove_all();
    EXPECT_TRUE(s.add(id0, 99.0f));
    EXPECT_EQ(s[id0], 99.0f);
  }
}

void ComponentStorage_Erase() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Erase removes component and destroys entity in registry.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.erase(id0));
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Erase with swap-and-pop.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.erase(id0));
    EXPECT_EQ(s.size(), 1U);
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_EQ(s[id1], 2.0f);
    EXPECT_EQ(r.get_location(id1).ndx, 0U);
  }

  // Erase returns false for invalid entity.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto bad_id = id_t{0};
    EXPECT_FALSE(s.erase(bad_id));
  }
}

void ComponentStorage_EraseIf() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // erase_if removes matching entities.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 5.0f));
    EXPECT_TRUE(s.add(id2, 10.0f));
    auto cnt = s.erase_if([](float val, auto) { return val > 3.0f; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(s.size(), 1U);
    EXPECT_TRUE(s.contains(id0));
    EXPECT_FALSE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2));
  }

  // erase_if with no matches.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    auto cnt = s.erase_if([](float, auto) { return false; });
    EXPECT_EQ(cnt, 0U);
    EXPECT_EQ(s.size(), 1U);
  }

  // erase_if on empty storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto cnt = s.erase_if([](float, auto) { return true; });
    EXPECT_EQ(cnt, 0U);
  }
}

void ComponentStorage_Clear() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Clear removes all components and erases entities from registry.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2));
  }

  // Clear on empty is a no-op.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.clear();
    EXPECT_TRUE(s.empty());
  }
}

void ComponentStorage_SwapAndMove() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid1 = store_id_t{1};
  const auto sid2 = store_id_t{2};
  const loc_t staging{store_id_t{}};

  // Member swap.
  if (true) {
    reg_t r;
    storage_t s1{r, sid1};
    storage_t s2{r, sid2};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s1.add(id0, 1.0f));
    EXPECT_TRUE(s2.add(id1, 2.0f));
    s1.swap(s2);
    EXPECT_EQ(*s1.store_id(), *sid2);
    EXPECT_EQ(*s2.store_id(), *sid1);
    EXPECT_EQ(s1.size(), 1U);
    EXPECT_EQ(s2.size(), 1U);
  }

  // Friend swap.
  if (true) {
    reg_t r;
    storage_t s1{r, sid1};
    storage_t s2{r, sid2};
    using std::swap;
    swap(s1, s2);
    EXPECT_EQ(*s1.store_id(), *sid2);
    EXPECT_EQ(*s2.store_id(), *sid1);
  }

  // Move constructor.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    {
      storage_t s1{r, sid1};
      EXPECT_TRUE(s1.add(id0, 1.0f));
      storage_t s2{std::move(s1)};
      EXPECT_EQ(s2.size(), 1U);
      EXPECT_EQ(s2[id0], 1.0f);
      EXPECT_EQ(*s2.store_id(), *sid1);
    }
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Move assignment.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    {
      storage_t s1{r, sid1};
      EXPECT_TRUE(s1.add(id0, 1.0f));
      storage_t s2;
      s2 = std::move(s1);
      EXPECT_EQ(s2.size(), 1U);
      EXPECT_EQ(s2[id0], 1.0f);
    }
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Destructor clears all entities from the registry (regression guard).
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    {
      storage_t s{r, sid1};
      EXPECT_TRUE(s.add(id0, 1.0f));
      EXPECT_TRUE(s.add(id1, 2.0f));
    } // destructor fires
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
  }
}

void ComponentStorage_LimitAndReserve() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Default limit is effectively unlimited.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_EQ(s.limit(), *id_t::invalid);
  }

  // Constructor with limit.
  if (true) {
    reg_t r;
    storage_t s{r, sid, 3};
    EXPECT_EQ(s.limit(), 3U);
    EXPECT_TRUE(s.empty());
  }

  // Constructor with limit and reserve.
  if (true) {
    reg_t r;
    storage_t s{r, sid, 5, true};
    EXPECT_EQ(s.limit(), 5U);
    EXPECT_TRUE(s.empty());
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_EQ(s[id0], 1.0f);
  }

  // Constructor with limit enforces on add.
  if (true) {
    reg_t r;
    storage_t s{r, sid, 1};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_FALSE(s.add(id1, 2.0f));
  }

  // Constructor with default limit and reserve=true is a no-op reserve.
  if (true) {
    reg_t r;
    storage_t s{r, sid, *id_t::invalid, true};
    EXPECT_EQ(s.limit(), *id_t::invalid);
    EXPECT_TRUE(s.empty());
  }

  // set_limit on empty storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(3));
    EXPECT_EQ(s.limit(), 3U);
  }

  // set_limit enforced on add.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(2));
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_FALSE(s.add(id2, 3.0f));
    EXPECT_EQ(s.size(), 2U);
    // id2 should still be in staging.
    EXPECT_TRUE(r.is_valid(id2));
    EXPECT_EQ(*r.get_location(id2).store_id, 0U);
  }

  // set_limit enforced on add by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(1));
    auto h0 = r.create_handle(staging, 10);
    auto h1 = r.create_handle(staging, 20);
    EXPECT_TRUE(s.add(h0, 1.0f));
    EXPECT_FALSE(s.add(h1, 2.0f));
    EXPECT_EQ(s.size(), 1U);
  }

  // set_limit enforced on add_new.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(1));
    auto h0 = s.add_new(1.0f, 10);
    EXPECT_TRUE(bool(h0));
    auto h1 = s.add_new(2.0f, 20);
    EXPECT_FALSE(bool(h1));
    EXPECT_EQ(s.size(), 1U);
    // The failed add_new should have cleaned up the entity it created.
    EXPECT_EQ(r.size(), 1U);
  }

  // set_limit fails if current size exceeds new limit.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_FALSE(s.set_limit(1));
    EXPECT_EQ(s.limit(), *id_t::invalid); // unchanged
  }

  // set_limit succeeds when equal to current size.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.set_limit(1));
    EXPECT_EQ(s.limit(), 1U);
  }

  // Remove frees a slot under the limit.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(2));
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_FALSE(s.add(id2, 3.0f)); // at limit
    EXPECT_TRUE(s.remove(id0));
    EXPECT_TRUE(s.add(id2, 3.0f)); // now succeeds
    EXPECT_EQ(s.size(), 2U);
  }

  // set_limit to 0: no adds allowed.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(0));
    auto id0 = r.create_id(staging, 10);
    EXPECT_FALSE(s.add(id0, 1.0f));
  }

  // Raising the limit allows more adds.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(1));
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_FALSE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.set_limit(2));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_EQ(s.size(), 2U);
  }

  // shrink_to_fit reduces capacity.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.reserve(100);
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    s.shrink_to_fit();
    EXPECT_EQ(s.size(), 1U);
    EXPECT_EQ(s[id0], 1.0f);
  }

  // shrink_to_fit on empty storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.shrink_to_fit(); // should not crash
    EXPECT_TRUE(s.empty());
  }

  // Limit is preserved across swap.
  if (true) {
    reg_t r;
    storage_t s1{r, store_id_t{1}};
    storage_t s2{r, store_id_t{2}};
    EXPECT_TRUE(s1.set_limit(5));
    EXPECT_TRUE(s2.set_limit(10));
    s1.swap(s2);
    EXPECT_EQ(s1.limit(), 10U);
    EXPECT_EQ(s2.limit(), 5U);
  }
}

void ComponentStorage_Iterator() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<reg_t, float>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // begin == end on empty storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.begin() == s.end());
    EXPECT_TRUE(s.cbegin() == s.cend());
  }

  // Dereference yields component reference; id() yields entity ID.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    auto it = s.begin();
    EXPECT_EQ(*it, 1.0f);
    EXPECT_EQ(it.id(), id0);
    ++it;
    EXPECT_EQ(*it, 2.0f);
    EXPECT_EQ(it.id(), id1);
    ++it;
    EXPECT_TRUE(it == s.end());
  }

  // Mutable iteration: modify components through iterator.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    *s.begin() = 99.0f;
    EXPECT_EQ(s[id0], 99.0f);
  }

  // Const iteration.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 3.14f));
    const auto& cs = s;
    auto it = cs.begin();
    EXPECT_EQ(*it, 3.14f);
    EXPECT_EQ(it.id(), id0);
  }

  // Range-for loop to sum components.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    float sum = 0.0f;
    for (auto val : s) sum += val;
    EXPECT_EQ(sum, 6.0f);
  }

  // Range-for with id() access via explicit iterator.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    std::vector<id_t> ids;
    for (auto it = s.begin(); it != s.end(); ++it) ids.push_back(it.id());
    EXPECT_EQ(ids.size(), 2U);
    EXPECT_EQ(ids[0], id0);
    EXPECT_EQ(ids[1], id1);
  }

  // Random access: operator[], +, -, +=, -=.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    auto it = s.begin();
    EXPECT_EQ(it[0], 1.0f);
    EXPECT_EQ(it[2], 3.0f);
    auto it2 = it + 2;
    EXPECT_EQ(*it2, 3.0f);
    EXPECT_EQ(it2.id(), id2);
    EXPECT_EQ(it2 - it, 2);
    it2 -= 1;
    EXPECT_EQ(*it2, 2.0f);
  }

  // Comparison operators.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    auto a = s.begin();
    auto b = s.begin() + 1;
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a <= a);
    EXPECT_TRUE(a != b);
  }

  // Post-increment and post-decrement.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    auto it = s.begin();
    auto prev = it++;
    EXPECT_EQ(*prev, 1.0f);
    EXPECT_EQ(*it, 2.0f);
    auto next = it--;
    EXPECT_EQ(*next, 2.0f);
    EXPECT_EQ(*it, 1.0f);
  }

  // n + iterator (commutative).
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    auto it = 1 + s.begin();
    EXPECT_EQ(*it, 2.0f);
  }
}

// ============================================================
// chunked_archetype_storage tests
// Use ChunkSize=4 so chunk boundaries appear at indices 4, 8, …
// ============================================================

void ChunkedArchetypeStorage_Basic() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid = store_id_t{1};

  // Default construction.
  if (true) {
    arch_t a;
    EXPECT_TRUE(a.empty());
    EXPECT_EQ(a.size(), 0U);
  }

  // reserve() and capacity().
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    a.reserve(8);
    EXPECT_GE(a.capacity(), 8U);
    EXPECT_TRUE(a.empty());
  }

  // shrink_to_fit() on empty storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    a.reserve(100);
    a.shrink_to_fit();
    EXPECT_TRUE(a.empty());
  }

  // store_id() and limit() defaults.
  if (true) {
    using id_t = reg_t::id_t;
    reg_t r;
    arch_t a{r, sid};
    EXPECT_EQ(a.store_id(), sid);
    EXPECT_EQ(a.limit(), *id_t::invalid);
  }

  // Constructor with limit and do_reserve.
  if (true) {
    reg_t r;
    arch_t a{r, sid, 8, true};
    EXPECT_GE(a.capacity(), 8U);
    EXPECT_TRUE(a.empty());
    EXPECT_EQ(a.limit(), 8U);
  }

  // tag_t alias matches the template tag parameter.
  if (true) {
    struct MyTag {};
    using tagged_t =
        chunked_archetype_storage<reg_t, std::tuple<int, float>, 4, MyTag>;
    static_assert(std::is_same_v<tagged_t::tag_t, MyTag>);
    static_assert(std::is_same_v<arch_t::tag_t, void>);
    // Tagged and untagged storages are distinct types.
    static_assert(!std::is_same_v<tagged_t, arch_t>);
  }
}

void ChunkedArchetypeStorage_Add() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // add(id) stores entity and updates registry.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 1.0f));
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(a.contains(id0));
    auto loc = r.get_location(id0);
    EXPECT_EQ(loc.store_id, sid);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // add(id) fails when entity is not in staging.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    arch_t b{r, store_id_t{2}};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_FALSE(b.add(id0, 2, 2.0f));
  }

  // add(handle) succeeds for valid staging handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(a.add(h, 99, 1.5f));
    EXPECT_TRUE(a.contains(h));
  }

  // add(handle) fails for invalid handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    r.erase(h);
    EXPECT_FALSE(a.add(h, 1, 1.0f));
  }

  // add_new creates and adds entity atomically.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = a.add_new(42, 7, 2.0f);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(r[h.id()], 42);
    EXPECT_TRUE(a.contains(h));
    EXPECT_EQ(r.get_location(h.id()).store_id, sid);
  }

  // Limit prevents add() beyond threshold.
  if (true) {
    reg_t r;
    arch_t a{r, sid, 2};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_FALSE(a.add(id2, 3, 3.0f));
    EXPECT_EQ(a.size(), 2U);
  }

  // set_limit() respects current size.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    EXPECT_FALSE(a.set_limit(1U));
    EXPECT_TRUE(a.set_limit(3U));
    EXPECT_EQ(a.limit(), 3U);
  }

  // add with one trailing component omitted: it is default-constructed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7));
    EXPECT_EQ(a[id0].template component<int>(), 7);
    EXPECT_EQ(a[id0].template component<float>(), 0.0f);
  }

  // add with all components omitted: all are default-constructed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0));
    EXPECT_EQ(a[id0].template component<int>(), 0);
    EXPECT_EQ(a[id0].template component<float>(), 0.0f);
  }

  // add_new with one trailing component omitted: it is default-constructed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = a.add_new(42, 7);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(a[h.id()].template component<int>(), 7);
    EXPECT_EQ(a[h.id()].template component<float>(), 0.0f);
  }

  // add_new with all components omitted: all are default-constructed.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = a.add_new(42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(a[h.id()].template component<int>(), 0);
    EXPECT_EQ(a[h.id()].template component<float>(), 0.0f);
  }
}

void ChunkedArchetypeStorage_RemoveAndErase() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // remove(id) moves entity back to staging.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.remove(id0));
    EXPECT_EQ(a.size(), 0U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_EQ(r.get_location(id0).store_id, store_id_t{});
  }

  // remove_all moves all entities back to staging.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    a.remove_all();
    EXPECT_EQ(a.size(), 0U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_EQ(r.get_location(id0).store_id, store_id_t{});
    EXPECT_EQ(r.get_location(id1).store_id, store_id_t{});
  }

  // erase(id) destroys the entity.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.erase(id0));
    EXPECT_EQ(a.size(), 0U);
    EXPECT_FALSE(r.is_valid(id0));
  }

  // erase(handle) destroys entity; invalid handle returns false.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(a.add(h, 1, 1.0f));
    EXPECT_TRUE(a.erase(h));
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_FALSE(a.erase(h));
  }

  // clear() destroys all entities.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    a.clear();
    EXPECT_EQ(a.size(), 0U);
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
  }

  // remove(handle) moves entity back to staging; invalid handle returns false.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(a.add(h, 5, 5.0f));
    EXPECT_TRUE(a.remove(h));
    EXPECT_EQ(a.size(), 0U);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(r.get_location(h).store_id, store_id_t{});
    // Stale / invalid handle returns false.
    r.erase(h);
    EXPECT_FALSE(a.remove(h));
  }

  // Swap-and-pop: displaced entity gets correct registry index.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 11, 1.1f));
    EXPECT_TRUE(a.add(id1, 22, 2.2f));
    EXPECT_TRUE(a.add(id2, 33, 3.3f));
    EXPECT_TRUE(a.erase(id0)); // id2 swaps into slot 0
    EXPECT_EQ(a.size(), 2U);
    EXPECT_EQ(a[id2].id(), id2);
    EXPECT_EQ(a[id2].component<int>(), 33);
    EXPECT_EQ(a[id1].id(), id1);
    EXPECT_EQ(r.get_location(id2).ndx, 0U);
    EXPECT_EQ(r.get_location(id1).ndx, 1U);
  }
}

void ChunkedArchetypeStorage_RowAndIterator() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // operator[] returns row_lens with correct index/id/components.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 2.0f));
    auto row = a[id0];
    EXPECT_EQ(row.index(), 0U);
    EXPECT_EQ(row.id(), id0);
    EXPECT_EQ(row.component<int>(), 42);
    EXPECT_EQ(row.component<float>(), 2.0f);
    row.component<int>() = 99;
    EXPECT_EQ(a[id0].component<int>(), 99);
  }

  // component<Index>() access by index.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 5, 3.0f));
    EXPECT_EQ(a[id0].component<0>(), 5);
    EXPECT_EQ(a[id0].component<1>(), 3.0f);
    a[id0].component<0>() = 77;
    EXPECT_EQ(a[id0].component<0>(), 77);
  }

  // components() returns tuple of mutable references.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 10, 1.0f));
    auto [i, f] = a[id0].components();
    EXPECT_EQ(i, 10);
    i = 100;
    EXPECT_EQ(a[id0].component<int>(), 100);
  }

  // const operator[] returns row_view.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7, 4.0f));
    const auto& ca = a;
    auto [i, f] = ca[id0].components();
    EXPECT_EQ(i, 7);
    EXPECT_EQ(f, 4.0f);
  }

  // Range-based for over mutable storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 1, 0.0f));
    EXPECT_TRUE(a.add(id1, 2, 0.0f));
    EXPECT_TRUE(a.add(id2, 3, 0.0f));
    int sum = 0;
    for (auto row : a) sum += row.component<int>();
    EXPECT_EQ(sum, 6);
  }

  // Range-based for over const storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 0, 1.0f));
    EXPECT_TRUE(a.add(id1, 0, 2.0f));
    const auto& ca = a;
    float fsum = 0.0f;
    for (const auto& row : ca) fsum += row.component<float>();
    EXPECT_EQ(fsum, 3.0f);
  }

  // Bidirectional iterator.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 5, 0.0f));
    EXPECT_TRUE(a.add(id1, 6, 0.0f));
    auto it = a.end();
    --it;
    EXPECT_EQ((*it).component<int>(), 6);
    --it;
    EXPECT_EQ((*it).component<int>(), 5);
    EXPECT_TRUE(it == a.begin());
  }

  // Empty storage: begin() == end().
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    EXPECT_TRUE(a.begin() == a.end());
  }

  // Mutation through iterator is reflected in storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 0.0f));
    EXPECT_TRUE(a.add(id1, 2, 0.0f));
    for (auto row : a) row.component<int>() *= 10;
    EXPECT_EQ(a[id0].component<int>(), 10);
    EXPECT_EQ(a[id1].component<int>(), 20);
  }

  // cbegin()/cend() return const_iterator; readable on a mutable storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7, 1.5f));
    float fsum = 0.0f;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
      fsum += it->component<float>();
    EXPECT_EQ(fsum, 1.5f);
    static_assert(
        std::is_same_v<decltype(a.cbegin()), arch_t::const_iterator>);
  }
}

void ChunkedArchetypeStorage_EraseIf() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // erase_if removes matching entities; others keep correct locations.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 11, 1.0f));
    EXPECT_TRUE(a.add(id1, 22, 2.0f));
    EXPECT_TRUE(a.add(id2, 33, 3.0f));
    auto cnt = a.erase_if([](const auto& row) {
      return row.template component<int>() % 2 != 0;
    });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2));
    EXPECT_EQ(a[id1].component<int>(), 22);
  }

  // erase_if_component<C> removes by component predicate.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 10, 1.0f));
    EXPECT_TRUE(a.add(id1, 20, 2.0f));
    EXPECT_TRUE(a.add(id2, 30, 3.0f));
    auto cnt = a.erase_if_component<int>([](int v, auto) { return v > 15; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_EQ(a[id0].component<int>(), 10);
  }

  // erase_if_component<Index> removes by index.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 5, 1.0f));
    EXPECT_TRUE(a.add(id1, 5, 9.0f));
    auto cnt = a.erase_if_component<1>([](float v, auto) { return v > 5.0f; });
    EXPECT_EQ(cnt, 1U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
  }
}

void ChunkedArchetypeStorage_ChunkBoundary() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  // ChunkSize=4: chunk 0 = indices 0-3, chunk 1 = indices 4-7.
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  auto make_ids = [&](reg_t& r, int n) {
    std::vector<reg_t::id_t> ids;
    for (int i = 0; i < n; ++i) ids.push_back(r.create_id(staging, i * 10));
    return ids;
  };

  // Filling exactly one chunk (4 entities): all in chunk 0.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto ids = make_ids(r, 4);
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(a.add(ids[i], i + 1, float(i)));
    EXPECT_EQ(a.size(), 4U);
    for (int i = 0; i < 4; ++i) {
      EXPECT_EQ(a[ids[i]].component<int>(), i + 1);
      EXPECT_EQ(r.get_location(ids[i]).ndx, size_t(i));
    }
  }

  // Crossing into the second chunk (5th entity lands in chunk 1 slot 0).
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto ids = make_ids(r, 5);
    for (int i = 0; i < 5; ++i) EXPECT_TRUE(a.add(ids[i], i + 1, float(i)));
    EXPECT_EQ(a.size(), 5U);
    EXPECT_EQ(a[ids[4]].component<int>(), 5);
    EXPECT_EQ(r.get_location(ids[4]).ndx, 4U);
  }

  // Removing the only element of chunk 1 (index 4) pops that chunk.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto ids = make_ids(r, 5);
    for (int i = 0; i < 5; ++i) EXPECT_TRUE(a.add(ids[i], i, float(i)));
    EXPECT_TRUE(a.erase(ids[4])); // removes slot 0 of chunk 1
    EXPECT_EQ(a.size(), 4U);
    // Remaining entities are all in chunk 0; indices 0-3 intact.
    for (int i = 0; i < 4; ++i) {
      EXPECT_TRUE(a.contains(ids[i]));
      EXPECT_EQ(r.get_location(ids[i]).ndx, size_t(i));
    }
  }

  // Removing from chunk 0 when chunk 1 exists: last entity (index 4) swaps
  // into the removed slot; chunk 1 is then empty and gets popped.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto ids = make_ids(r, 5);
    for (int i = 0; i < 5; ++i) EXPECT_TRUE(a.add(ids[i], i * 10, float(i)));
    // Remove ids[0] (index 0); ids[4] (index 4, chunk 1 slot 0) swaps in.
    EXPECT_TRUE(a.erase(ids[0]));
    EXPECT_EQ(a.size(), 4U);
    EXPECT_FALSE(r.is_valid(ids[0]));
    EXPECT_EQ(a[ids[4]].id(), ids[4]);
    EXPECT_EQ(a[ids[4]].component<int>(), 40);
    EXPECT_EQ(r.get_location(ids[4]).ndx, 0U);
    // All survivors are now in chunk 0.
    for (auto& row : a) EXPECT_TRUE(r.is_valid(row.id()));
  }

  // Removing from a non-zero slot within the last chunk: no chunk pop.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto ids = make_ids(r, 6);
    for (int i = 0; i < 6; ++i) EXPECT_TRUE(a.add(ids[i], i, float(i)));
    // Remove ids[4] (index 4, chunk 1 slot 0); ids[5] (index 5, slot 1)
    // swaps in. Size becomes 5; chunk 1 still has one occupant at slot 0.
    EXPECT_TRUE(a.erase(ids[4]));
    EXPECT_EQ(a.size(), 5U);
    EXPECT_EQ(a[ids[5]].id(), ids[5]);
    EXPECT_EQ(r.get_location(ids[5]).ndx, 4U);
    EXPECT_EQ(a[ids[5]].component<int>(), 5);
  }

  // Range-based for across two chunks yields all entities in order.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto ids = make_ids(r, 7);
    for (int i = 0; i < 7; ++i) EXPECT_TRUE(a.add(ids[i], i + 1, float(i)));
    int sum = 0;
    for (auto row : a) sum += row.component<int>();
    EXPECT_EQ(sum, 28); // 1+2+3+4+5+6+7
  }

  // erase_if across chunk boundary: erased entities from both chunks,
  // displaced entities get correct locations.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto ids = make_ids(r, 6);
    for (int i = 0; i < 6; ++i) EXPECT_TRUE(a.add(ids[i], i, float(i)));
    // Erase even-valued ints: 0 (idx 0), 2 (idx 2), 4 (idx 4).
    auto cnt = a.erase_if([](const auto& row) {
      return row.template component<int>() % 2 == 0;
    });
    EXPECT_EQ(cnt, 3U);
    EXPECT_EQ(a.size(), 3U);
    // All survivors have odd int components.
    for (auto row : a) EXPECT_EQ(row.component<int>() % 2, 1);
    // Registry locations are consistent.
    for (auto row : a) EXPECT_EQ(r.get_location(row.id()).ndx, row.index());
  }
}

// ---- Scene tests ----
//
// Component types used across all scene tests.
struct Position {
  float x{}, y{};
};
struct Velocity {
  float vx{}, vy{};
};
struct Health {
  int hp{100};
};

// Registry and storage aliases reused across scene tests.
using scene_reg_t = entity_registry<int>;
using scene_sid_t = scene_reg_t::store_id_t;
using arch_pv_t =
    archetype_storage<scene_reg_t, std::tuple<Position, Velocity>>;
using arch_pvh_t =
    archetype_storage<scene_reg_t, std::tuple<Position, Velocity, Health>>;
using arch_h_t = archetype_storage<scene_reg_t, std::tuple<Health>>;
using two_storage_scene_t = scene<scene_reg_t, arch_pv_t, arch_pvh_t>;
using three_storage_scene_t =
    scene<scene_reg_t, arch_pv_t, arch_pvh_t, arch_h_t>;

// Basic construction, type queries, storage access.
void Scene_Basic() {
  // Default construction: empty, zero size.
  if (true) {
    two_storage_scene_t s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0U);
    EXPECT_EQ(two_storage_scene_t::storage_count_v, 2U);
  }

  // storage<N>() returns the correct storage type.
  if (true) {
    two_storage_scene_t s;
    // storage{1} is arch_pv_t, storage{2} is arch_pvh_t.
    EXPECT_TRUE(s.storage<scene_sid_t{1}>().empty());
    EXPECT_TRUE(s.storage<scene_sid_t{2}>().empty());
    // Each storage has a distinct, sequential store_id.
    EXPECT_EQ(s.storage<scene_sid_t{1}>().store_id(), scene_sid_t{1});
    EXPECT_EQ(s.storage<scene_sid_t{2}>().store_id(), scene_sid_t{2});
  }

  // add_new inserts into the chosen storage.
  if (true) {
    two_storage_scene_t s;
    auto h =
        s.add_new<scene_sid_t{1}>({}, Position{1.f, 2.f}, Velocity{3.f, 4.f});
    EXPECT_TRUE(h);
    EXPECT_EQ(s.size(), 1U);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.storage<scene_sid_t{1}>().size(), 1U);
    EXPECT_EQ(s.storage<scene_sid_t{2}>().size(), 0U);
    // Component values round-trip correctly.
    auto row = s.storage<scene_sid_t{1}>()[h.id()];
    EXPECT_EQ(row.component<Position>().x, 1.f);
    EXPECT_EQ(row.component<Velocity>().vx, 3.f);
  }

  // size() sums across all storages.
  if (true) {
    two_storage_scene_t s;
    (void)s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.add_new<scene_sid_t{2}>({}, Position{}, Velocity{}, Health{50});
    EXPECT_EQ(s.size(), 3U);
    EXPECT_EQ(s.storage<scene_sid_t{1}>().size(), 2U);
    EXPECT_EQ(s.storage<scene_sid_t{2}>().size(), 1U);
  }
}

// scene::erase and scene::remove dispatch to the correct storage.
void Scene_EraseRemove() {
  // erase(id) on an entity in storage 0.
  if (true) {
    two_storage_scene_t s;
    auto h0 = s.add_new<scene_sid_t{1}>({}, Position{1.f, 0.f}, Velocity{});
    auto h1 = s.add_new<scene_sid_t{1}>({}, Position{2.f, 0.f}, Velocity{});
    auto id0 = h0.id();
    EXPECT_TRUE(s.erase(id0));
    EXPECT_EQ(id0, scene_reg_t::id_t::invalid); // invalidated on success
    EXPECT_EQ(s.storage<scene_sid_t{1}>().size(), 1U);
    // Remaining entity still accessible.
    EXPECT_TRUE(s.registry().is_valid(h1.id()));
  }

  // erase(id) on an entity in storage 1.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{2}>({}, Position{}, Velocity{}, Health{42});
    auto id = h.id();
    EXPECT_TRUE(s.erase(id));
    EXPECT_EQ(id, scene_reg_t::id_t::invalid);
    EXPECT_EQ(s.storage<scene_sid_t{2}>().size(), 0U);
  }

  // erase(handle) resets the handle on success.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    EXPECT_TRUE(s.erase(h));
    EXPECT_FALSE(h); // handle invalidated
    EXPECT_EQ(s.size(), 0U);
  }

  // erase(handle) returns false for an invalid handle (handle overload
  // validates before calling into erase(id_t&), so no precondition violation).
  if (true) {
    two_storage_scene_t s;
    auto h = scene_reg_t::handle_t{};
    EXPECT_FALSE(s.erase(h));
  }

  // remove(id) moves entity back to staging.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    auto id = h.id();
    EXPECT_TRUE(s.remove(id));
    EXPECT_EQ(s.storage<scene_sid_t{1}>().size(), 0U);
    // Entity still alive (staged), handle still valid.
    EXPECT_TRUE(s.registry().is_valid(h));
    const auto loc = s.registry().get_location(id);
    EXPECT_EQ(loc.store_id, scene_reg_t::store_id_t{});
  }

  // remove on already-staged entity returns false.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    auto id = h.id();
    (void)s.remove(id);        // now staged
    EXPECT_TRUE(s.remove(id)); // already staged
  }

  // remove(handle) overload.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{2}>({}, Position{}, Velocity{}, Health{});
    EXPECT_TRUE(s.remove(h));
    EXPECT_EQ(s.storage<scene_sid_t{2}>().size(), 0U);
  }
}

// migrate with a user-supplied build callback.
void Scene_Migrate_Manual() {
  // Promote from arch_pv to arch_pvh, providing a new Health component.
  if (true) {
    two_storage_scene_t s;
    auto h =
        s.add_new<scene_sid_t{1}>({}, Position{1.f, 2.f}, Velocity{3.f, 4.f});
    auto id = h.id();
    EXPECT_TRUE(s.storage<scene_sid_t{1}>().contains(id));

    auto build1 = [](const auto& row) {
      return std::tuple<Position, Velocity, Health>{
          row.template component<Position>(),
          row.template component<Velocity>(), Health{99}};
    };
    bool ok = s.migrate(id, scene_sid_t{2}, build1);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(s.storage<scene_sid_t{1}>().contains(id));
    EXPECT_TRUE(s.storage<scene_sid_t{2}>().contains(id));
    // Component values preserved and new one set.
    auto row = s.storage<scene_sid_t{2}>()[id];
    EXPECT_EQ(row.component<Position>().x, 1.f);
    EXPECT_EQ(row.component<Velocity>().vx, 3.f);
    EXPECT_EQ(row.component<Health>().hp, 99);
  }

  // Migrate via handle overload.
  if (true) {
    two_storage_scene_t s;
    auto h =
        s.add_new<scene_sid_t{1}>({}, Position{5.f, 6.f}, Velocity{7.f, 8.f});
    auto build2 = [](const auto& row) {
      return std::tuple<Position, Velocity, Health>{
          row.template component<Position>(),
          row.template component<Velocity>(), Health{50}};
    };
    bool ok = s.migrate(h, scene_sid_t{2}, build2);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(s.storage<scene_sid_t{2}>().contains(h.id()));
    EXPECT_EQ(s.storage<scene_sid_t{2}>()[h.id()].component<Health>().hp, 50);
  }

  // Migrate fails if entity is staged (not in any storage).
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.remove(h.id()); // move back to staging
    auto build3 = [](const auto&) {
      return std::tuple<Position, Velocity, Health>{};
    };
    bool ok = s.migrate(h.id(), scene_sid_t{2}, build3);
    EXPECT_FALSE(ok); // entity is staged, not in any storage
    EXPECT_FALSE(s.storage<scene_sid_t{2}>().contains(h.id())); // still staged
  }
}

// migrate with automatic type-based component mapping.
void Scene_Migrate_Auto() {
  // Promote: arch_pv (Position, Velocity) → arch_pvh (Position, Velocity,
  // Health). Health should be default-constructed (hp=100).
  if (true) {
    two_storage_scene_t s;
    auto h =
        s.add_new<scene_sid_t{1}>({}, Position{1.f, 2.f}, Velocity{3.f, 4.f});
    auto id = h.id();
    bool ok_promote = s.migrate(id, scene_sid_t{2});
    EXPECT_TRUE(ok_promote);
    EXPECT_FALSE(s.storage<scene_sid_t{1}>().contains(id));
    EXPECT_TRUE(s.storage<scene_sid_t{2}>().contains(id));
    auto row = s.storage<scene_sid_t{2}>()[id];
    EXPECT_EQ(row.component<Position>().x, 1.f);
    EXPECT_EQ(row.component<Velocity>().vx, 3.f);
    EXPECT_EQ(row.component<Health>().hp, 100); // default-constructed
  }

  // Demotion: arch_pvh → arch_pv; Health is dropped, Position and Velocity
  // are copied.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{2}>({}, Position{5.f, 6.f},
        Velocity{7.f, 8.f}, Health{42});
    auto id = h.id();
    bool ok_demote = s.migrate(id, scene_sid_t{1});
    EXPECT_TRUE(ok_demote);
    EXPECT_FALSE(s.storage<scene_sid_t{2}>().contains(id));
    EXPECT_TRUE(s.storage<scene_sid_t{1}>().contains(id));
    auto row = s.storage<scene_sid_t{1}>()[id];
    EXPECT_EQ(row.component<Position>().x, 5.f);
    EXPECT_EQ(row.component<Velocity>().vx, 7.f);
  }

  // Auto-migrate via handle overload.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{9.f, 0.f}, Velocity{});
    bool ok_h = s.migrate(h, scene_sid_t{2});
    EXPECT_TRUE(ok_h);
    EXPECT_TRUE(s.storage<scene_sid_t{2}>().contains(h.id()));
  }

  // Migrate to a completely non-overlapping archetype: Health only. All
  // components default-constructed.
  if (true) {
    three_storage_scene_t s;
    auto h =
        s.add_new<scene_sid_t{1}>({}, Position{1.f, 1.f}, Velocity{2.f, 2.f});
    auto id = h.id();
    bool ok_cross = s.migrate(id, scene_sid_t{3}); // arch_pv → arch_h
    EXPECT_TRUE(ok_cross);
    EXPECT_TRUE(s.storage<scene_sid_t{3}>().contains(id));
    // Health default-constructed because source has no Health.
    EXPECT_EQ(s.storage<scene_sid_t{3}>()[id].component<Health>().hp, 100);
  }
}

// erase_staged removes all staged entities.
void Scene_EraseStaged() {
  // Entities placed in a storage are not staged and are not affected.
  if (true) {
    two_storage_scene_t s;
    auto h0 = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    auto h1 = s.add_new<scene_sid_t{2}>({}, Position{}, Velocity{}, Health{});
    EXPECT_EQ(s.erase_staged(), 0U);
    EXPECT_TRUE(s.registry().is_valid(h0));
    EXPECT_TRUE(s.registry().is_valid(h1));
  }

  // Entities returned to staging via remove() are erased by erase_staged().
  if (true) {
    two_storage_scene_t s;
    auto h0 = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    auto h1 = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.remove(h0.id());
    (void)s.remove(h1.id());
    EXPECT_EQ(s.erase_staged(), 2U);
    EXPECT_FALSE(s.registry().is_valid(h0));
    EXPECT_FALSE(s.registry().is_valid(h1));
  }

  // After a failed migration the entity is staged; erase_staged() cleans it.
  if (true) {
    two_storage_scene_t s;
    // Set storage<1> limit to 0 so add will fail.
    (void)s.storage<scene_sid_t{2}>().set_limit(0);
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    auto id = h.id();
    // Auto-migrate promotes to storage 1 — but storage 1 is full.
    // The entity ends up in staging after remove() from storage 0 succeeds
    // but add() to storage 1 fails.
    (void)s.migrate(id, scene_sid_t{2});
    // Entity may be stranded in staging; erase_staged cleans it up.
    EXPECT_EQ(s.erase_staged(), 1U);
    EXPECT_FALSE(s.registry().is_valid(id));
  }

  // Entities directly erased are not in staging and are unaffected.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    auto id = h.id();
    (void)s.erase(id);
    EXPECT_EQ(s.erase_staged(), 0U);
  }
}

// scene::clear empties everything.
void Scene_Clear() {
  // clear(true) — fast path: all entities gone, registry empty.
  if (true) {
    two_storage_scene_t s;
    (void)s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.add_new<scene_sid_t{2}>({}, Position{}, Velocity{}, Health{});
    EXPECT_EQ(s.size(), 3U);
    s.clear(); // fast=true by default
    EXPECT_EQ(s.size(), 0U);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.registry().size(), 0U);
  }

  // clear(true) also removes staged entities.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.remove(h.id()); // now staged
    EXPECT_EQ(s.registry().size(), 1U);
    s.clear(true);
    EXPECT_EQ(s.registry().size(), 0U);
  }

  // clear(false) — slow path: all entities gone, registry empty.
  if (true) {
    two_storage_scene_t s;
    (void)s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.add_new<scene_sid_t{2}>({}, Position{}, Velocity{}, Health{});
    EXPECT_EQ(s.size(), 2U);
    s.clear(false);
    EXPECT_EQ(s.size(), 0U);
    EXPECT_EQ(s.registry().size(), 0U);
  }

  // clear(false) also removes staged entities.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.remove(h.id()); // now staged
    EXPECT_EQ(s.registry().size(), 1U);
    s.clear(false);
    EXPECT_EQ(s.registry().size(), 0U);
  }

  // clear(true) resets generation counters: after re-creating an entity the
  // old handle appears valid again (gen was reset to 0 rather than
  // incremented). This is the documented trade-off of the fast path.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    EXPECT_EQ(h.gen(), 0U); // initial generation
    s.clear(true);
    EXPECT_FALSE(s.registry().is_valid(h)); // entity gone
    // Re-create: gen starts at 0 again because records were wiped.
    auto h2 = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    EXPECT_EQ(h2.gen(), 0U); // generation was reset, not incremented
    // Old handle (gen=0) matches the new record's gen=0: appears valid.
    EXPECT_TRUE(s.registry().is_valid(h));
  }

  // clear(false) does NOT reset generation counters: after re-creating an
  // entity the old handle remains invalid because gen was incremented.
  if (true) {
    two_storage_scene_t s;
    auto h = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    EXPECT_EQ(h.gen(), 0U); // initial generation
    s.clear(false);
    EXPECT_FALSE(s.registry().is_valid(h)); // entity gone
    // Re-create: gen is 1 because the slow-path erase incremented it.
    auto h2 = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    EXPECT_EQ(h2.gen(), 1U); // generation incremented, not reset
    // Old handle (gen=0) no longer matches record's gen=1: still invalid.
    EXPECT_FALSE(s.registry().is_valid(h));
  }
}

// Multiple storages with different component sets; scene-level dispatch
// correctly targets each one by store_id.
void Scene_MultiStorage() {
  if (true) {
    three_storage_scene_t s;
    // Add entities to each of the three storages.
    auto h0 = s.add_new<scene_sid_t{1}>({}, Position{1.f, 0.f}, Velocity{});
    auto h1 = s.add_new<scene_sid_t{2}>({}, Position{2.f, 0.f}, Velocity{},
        Health{50});
    auto h2 = s.add_new<scene_sid_t{3}>({}, Health{75});
    EXPECT_EQ(s.size(), 3U);
    EXPECT_EQ(s.storage<scene_sid_t{1}>().size(), 1U);
    EXPECT_EQ(s.storage<scene_sid_t{2}>().size(), 1U);
    EXPECT_EQ(s.storage<scene_sid_t{3}>().size(), 1U);

    // scene::erase dispatches to storage 0.
    auto id0 = h0.id();
    EXPECT_TRUE(s.erase(id0));
    EXPECT_EQ(id0, scene_reg_t::id_t::invalid);
    EXPECT_EQ(s.storage<scene_sid_t{1}>().size(), 0U);
    EXPECT_EQ(s.size(), 2U);

    // scene::remove dispatches to storage 2.
    EXPECT_TRUE(s.remove(h2.id()));
    EXPECT_EQ(s.storage<scene_sid_t{3}>().size(), 0U);
    EXPECT_TRUE(s.registry().is_valid(h2.id())); // still alive (staged)

    // scene::erase dispatches to storage 1.
    EXPECT_TRUE(s.erase(h1));
    EXPECT_FALSE(h1); // handle reset
    EXPECT_EQ(s.size(), 0U);

    // One staged entity remains; clean up.
    EXPECT_EQ(s.erase_staged(), 1U);
    EXPECT_EQ(s.registry().size(), 0U);
  }
}

// Mixed-storage scene: one archetype, one chunked, one component_storage.
using chunked_h_t = chunked_archetype_storage<scene_reg_t, std::tuple<Health>>;
using comp_pos_t = component_storage<scene_reg_t, Position>;
using mixed_scene_t = scene<scene_reg_t, arch_pv_t, chunked_h_t, comp_pos_t>;

void Scene_MixedStorages() {
  // add_new into each storage type; size() sums all three.
  if (true) {
    mixed_scene_t s;
    auto h0 =
        s.add_new<scene_sid_t{1}>({}, Position{1.f, 2.f}, Velocity{3.f, 4.f});
    auto h1 = s.add_new<scene_sid_t{2}>({}, Health{50});
    auto h2 = s.add_new<scene_sid_t{3}>({}, Position{5.f, 6.f});
    EXPECT_TRUE(h0);
    EXPECT_TRUE(h1);
    EXPECT_TRUE(h2);
    EXPECT_EQ(s.size(), 3U);
    EXPECT_EQ(s.storage<scene_sid_t{1}>().size(), 1U);
    EXPECT_EQ(s.storage<scene_sid_t{2}>().size(), 1U);
    EXPECT_EQ(s.storage<scene_sid_t{3}>().size(), 1U);
    // Component values round-trip correctly through each storage type.
    EXPECT_EQ(s.storage<scene_sid_t{1}>()[h0.id()].component<Position>().x,
        1.f);
    EXPECT_EQ(s.storage<scene_sid_t{2}>()[h1.id()].component<Health>().hp, 50);
    EXPECT_EQ(s.storage<scene_sid_t{3}>()[h2.id()].x, 5.f);
  }

  // erase dispatches to the correct storage regardless of type.
  if (true) {
    mixed_scene_t s;
    auto h0 = s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    auto h1 = s.add_new<scene_sid_t{2}>({}, Health{});
    auto h2 = s.add_new<scene_sid_t{3}>({}, Position{});
    auto id0 = h0.id();
    auto id1 = h1.id();
    auto id2 = h2.id();
    EXPECT_TRUE(s.erase(id0));
    EXPECT_TRUE(s.erase(id1));
    EXPECT_TRUE(s.erase(id2));
    EXPECT_TRUE(s.empty());
  }

  // Migrate arch_pv (Position, Velocity) → comp_pos (Position).
  // Position is copied; Velocity is dropped (not in dst).
  if (true) {
    mixed_scene_t s;
    auto h =
        s.add_new<scene_sid_t{1}>({}, Position{7.f, 8.f}, Velocity{9.f, 10.f});
    auto id = h.id();
    bool ok = s.migrate(id, scene_sid_t{3});
    EXPECT_TRUE(ok);
    EXPECT_FALSE(s.storage<scene_sid_t{1}>().contains(id));
    EXPECT_TRUE(s.storage<scene_sid_t{3}>().contains(id));
    // Position carried over; component_storage's direct C& access works.
    EXPECT_EQ(s.storage<scene_sid_t{3}>()[id].x, 7.f);
  }

  // Migrate chunked_h (Health) → comp_pos (Position).
  // No components overlap; Position is default-constructed.
  if (true) {
    mixed_scene_t s;
    auto h = s.add_new<scene_sid_t{2}>({}, Health{99});
    auto id = h.id();
    bool ok = s.migrate(id, scene_sid_t{3});
    EXPECT_TRUE(ok);
    EXPECT_FALSE(s.storage<scene_sid_t{2}>().contains(id));
    EXPECT_TRUE(s.storage<scene_sid_t{3}>().contains(id));
    EXPECT_EQ(s.storage<scene_sid_t{3}>()[id].x, 0.f); // default Position{}.x
  }

  // Migrate comp_pos (Position) → arch_pv (Position, Velocity).
  // Position is copied; Velocity is default-constructed.
  if (true) {
    mixed_scene_t s;
    auto h = s.add_new<scene_sid_t{3}>({}, Position{3.f, 4.f});
    auto id = h.id();
    bool ok = s.migrate(id, scene_sid_t{1});
    EXPECT_TRUE(ok);
    EXPECT_FALSE(s.storage<scene_sid_t{3}>().contains(id));
    EXPECT_TRUE(s.storage<scene_sid_t{1}>().contains(id));
    auto row = s.storage<scene_sid_t{1}>()[id];
    EXPECT_EQ(row.component<Position>().x, 3.f);
    EXPECT_EQ(row.component<Velocity>().vx, 0.f); // default-constructed
  }

  // clear() empties all three storage types.
  if (true) {
    mixed_scene_t s;
    (void)s.add_new<scene_sid_t{1}>({}, Position{}, Velocity{});
    (void)s.add_new<scene_sid_t{2}>({}, Health{});
    (void)s.add_new<scene_sid_t{3}>({}, Position{});
    EXPECT_EQ(s.size(), 3U);
    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.registry().size(), 0U);
  }
}

void ArchetypeStorage_At() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // at(id_t) mutable: succeeds and allows mutation.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 1.5f));
    auto row = a.at(id0);
    EXPECT_EQ(row.component<int>(), 42);
    EXPECT_EQ(row.component<float>(), 1.5f);
    row.component<int>() = 99;
    EXPECT_EQ(a[id0].component<int>(), 99);
  }

  // at(id_t) const: read-only access.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7, 2.0f));
    const auto& ca = a;
    auto row = ca.at(id0);
    EXPECT_EQ(row.component<int>(), 7);
    EXPECT_EQ(row.component<float>(), 2.0f);
  }

  // at(id_t) throws std::out_of_range when entity is not in this storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10); // staged, not in a
    EXPECT_THROW(a.at(id0), std::out_of_range);
    const auto& ca = a;
    EXPECT_THROW(ca.at(id0), std::out_of_range);
  }

  // at(handle_t) mutable: succeeds with valid handle in this storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 5);
    EXPECT_TRUE(a.add(h.id(), 3, 0.5f));
    auto row = a.at(h);
    EXPECT_EQ(row.component<int>(), 3);
    row.component<float>() = 9.9f;
    EXPECT_EQ(a[h.id()].component<float>(), 9.9f);
    const auto& ca = a;
    auto crow = ca.at(h);
    EXPECT_EQ(crow.component<float>(), 9.9f);
  }

  // at(handle_t) throws std::invalid_argument for an invalid handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    reg_t::handle_t bad{};
    EXPECT_THROW(a.at(bad), std::invalid_argument);
    const auto& ca = a;
    EXPECT_THROW(ca.at(bad), std::invalid_argument);
  }

  // at(handle_t) throws when entity is in a different storage.
  if (true) {
    reg_t r;
    arch_t a1{r, sid};
    arch_t a2{r, store_id_t{2}};
    auto h = r.create_handle(staging, 5);
    EXPECT_TRUE(a1.add(h.id(), 1, 1.0f));
    EXPECT_THROW(a2.at(h), std::invalid_argument);
    const auto& ca2 = a2;
    EXPECT_THROW(ca2.at(h), std::invalid_argument);
  }
}

void ArchetypeStorage_RemoveIf() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // remove_if: matching entities go to staging; non-matching stay in storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 11, 1.0f));
    EXPECT_TRUE(a.add(id1, 22, 2.0f));
    EXPECT_TRUE(a.add(id2, 33, 3.0f));
    auto cnt = a.remove_if([](const auto& row) {
      return row.template component<int>() % 2 != 0;
    });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 1U);
    // Removed entities stay valid but move to staging.
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id2));
    EXPECT_FALSE(a.contains(id0));
    EXPECT_FALSE(a.contains(id2));
    EXPECT_EQ(r.get_location(id0).store_id, store_id_t{});
    EXPECT_EQ(r.get_location(id2).store_id, store_id_t{});
    EXPECT_TRUE(a.contains(id1));
    EXPECT_EQ(a[id1].component<int>(), 22);
  }

  // remove_if pred always false: nothing moved.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 5, 0.5f));
    auto cnt = a.remove_if([](const auto&) { return false; });
    EXPECT_EQ(cnt, 0U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(a.contains(id0));
  }

  // remove_if pred always true: all entities staged; can be re-added.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_TRUE(a.add(id1, 2, 2.0f));
    auto cnt = a.remove_if([](const auto&) { return true; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 0U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    // Staged entities can be re-added.
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    EXPECT_EQ(a.size(), 1U);
  }

  // remove_if_component<C>: filter by component type.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 10, 1.0f));
    EXPECT_TRUE(a.add(id1, 20, 2.0f));
    EXPECT_TRUE(a.add(id2, 30, 3.0f));
    auto cnt = a.remove_if_component<int>([](int v, auto) { return v > 15; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(a.contains(id0));
    EXPECT_TRUE(r.is_valid(id1)); // staged, not erased
    EXPECT_TRUE(r.is_valid(id2)); // staged, not erased
    EXPECT_EQ(r.get_location(id1).store_id, store_id_t{});
    EXPECT_EQ(r.get_location(id2).store_id, store_id_t{});
  }

  // remove_if_component<Index>: filter by tuple index (index 1 = float).
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 5, 1.0f));
    EXPECT_TRUE(a.add(id1, 5, 9.0f));
    auto cnt = a.remove_if_component<1>([](float v, auto) {
      return v > 5.0f;
    });
    EXPECT_EQ(cnt, 1U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(a.contains(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_EQ(r.get_location(id1).store_id, store_id_t{});
  }
}

void ArchetypeStorage_IteratorPostIncDec() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = archetype_storage<reg_t, std::tuple<int, float>>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Mutable iterator post-increment returns prior position.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 1, 0.0f));
    EXPECT_TRUE(a.add(id1, 2, 0.0f));
    auto it = a.begin();
    auto prev = it++;
    EXPECT_EQ((*prev).component<int>(), 1);
    EXPECT_EQ((*it).component<int>(), 2);
    prev = it++;
    EXPECT_EQ((*prev).component<int>(), 2);
    EXPECT_TRUE(it == a.end());
  }

  // Mutable iterator post-decrement returns prior position.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 5, 0.0f));
    EXPECT_TRUE(a.add(id1, 6, 0.0f));
    auto it = a.end();
    auto prev = it--;
    EXPECT_TRUE(prev == a.end());
    EXPECT_EQ((*it).component<int>(), 6);
    prev = it--;
    EXPECT_EQ((*prev).component<int>(), 6);
    EXPECT_EQ((*it).component<int>(), 5);
    EXPECT_TRUE(it == a.begin());
  }

  // Const iterator post-increment.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 7, 0.0f));
    EXPECT_TRUE(a.add(id1, 8, 0.0f));
    const auto& ca = a;
    auto it = ca.begin();
    auto prev = it++;
    EXPECT_EQ((*prev).component<int>(), 7);
    EXPECT_EQ((*it).component<int>(), 8);
  }

  // Const iterator post-decrement.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 3, 0.0f));
    EXPECT_TRUE(a.add(id1, 4, 0.0f));
    const auto& ca = a;
    auto it = ca.end();
    auto prev = it--;
    EXPECT_TRUE(prev == ca.end());
    EXPECT_EQ((*it).component<int>(), 4);
  }
}

void ChunkedArchetypeStorage_At() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // at(id_t) mutable: succeeds and allows mutation.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 42, 1.5f));
    auto row = a.at(id0);
    EXPECT_EQ(row.component<int>(), 42);
    row.component<int>() = 99;
    EXPECT_EQ(a[id0].component<int>(), 99);
  }

  // at(id_t) const: read-only access.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 7, 2.0f));
    const auto& ca = a;
    auto row = ca.at(id0);
    EXPECT_EQ(row.component<int>(), 7);
  }

  // at(id_t) throws std::out_of_range when entity is not in this storage.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_THROW(a.at(id0), std::out_of_range);
    const auto& ca = a;
    EXPECT_THROW(ca.at(id0), std::out_of_range);
  }

  // at(handle_t) mutable and const: succeeds with valid handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto h = r.create_handle(staging, 5);
    EXPECT_TRUE(a.add(h.id(), 3, 0.5f));
    auto row = a.at(h);
    EXPECT_EQ(row.component<int>(), 3);
    const auto& ca = a;
    auto crow = ca.at(h);
    EXPECT_EQ(crow.component<int>(), 3);
  }

  // at(handle_t) throws std::invalid_argument for an invalid handle.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    reg_t::handle_t bad{};
    EXPECT_THROW(a.at(bad), std::invalid_argument);
    const auto& ca = a;
    EXPECT_THROW(ca.at(bad), std::invalid_argument);
  }
}

void ChunkedArchetypeStorage_RemoveIf() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // remove_if: matching entities go to staging; remain valid.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 11, 1.0f));
    EXPECT_TRUE(a.add(id1, 22, 2.0f));
    EXPECT_TRUE(a.add(id2, 33, 3.0f));
    auto cnt = a.remove_if([](const auto& row) {
      return row.template component<int>() % 2 != 0;
    });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id2));
    EXPECT_EQ(r.get_location(id0).store_id, store_id_t{});
    EXPECT_EQ(r.get_location(id2).store_id, store_id_t{});
    EXPECT_TRUE(a.contains(id1));
  }

  // remove_if_component<C>: filter by component type.
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(a.add(id0, 10, 1.0f));
    EXPECT_TRUE(a.add(id1, 20, 2.0f));
    EXPECT_TRUE(a.add(id2, 30, 3.0f));
    auto cnt = a.remove_if_component<int>([](int v, auto) { return v > 15; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(a.contains(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_TRUE(r.is_valid(id2));
  }

  // remove_if_component<Index>: filter by tuple index (index 1 = float).
  if (true) {
    reg_t r;
    arch_t a{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 5, 1.0f));
    EXPECT_TRUE(a.add(id1, 5, 9.0f));
    auto cnt = a.remove_if_component<1>([](float v, auto) {
      return v > 5.0f;
    });
    EXPECT_EQ(cnt, 1U);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_TRUE(a.contains(id0));
    EXPECT_TRUE(r.is_valid(id1));
  }
}

void ComponentStorage_RowView() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using cs_t = component_storage<reg_t, float>;
  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // row_view::component<T>() uniform accessor.
  if (true) {
    reg_t r;
    cs_t cs{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(cs.add(id0, 3.14f));
    const auto& ccs = cs;
    EXPECT_EQ(ccs[id0].component<float>(), 3.14f);
  }

  // row_view::id() returns the entity ID.
  if (true) {
    reg_t r;
    cs_t cs{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(cs.add(id0, 1.0f));
    EXPECT_TRUE(cs.add(id1, 2.0f));
    const auto& ccs = cs;
    EXPECT_EQ(ccs[id0].id(), id0);
    EXPECT_EQ(ccs[id1].id(), id1);
  }

  // at(id_t) mutable: returns component_t& and allows mutation.
  if (true) {
    reg_t r;
    cs_t cs{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(cs.add(id0, 2.5f));
    EXPECT_EQ(cs.at(id0), 2.5f);
    cs.at(id0) = 9.9f;
    EXPECT_EQ(cs[id0], 9.9f);
    // at(id_t) throws std::out_of_range for absent entity.
    auto id1 = r.create_id(staging, 20);
    EXPECT_THROW(cs.at(id1), std::out_of_range);
    const auto& ccs = cs;
    EXPECT_THROW(ccs.at(id1), std::out_of_range);
  }

  // at(handle_t): mutable returns component_t&; const returns row_view.
  if (true) {
    reg_t r;
    cs_t cs{r, sid};
    auto h = r.create_handle(staging, 5);
    EXPECT_TRUE(cs.add(h.id(), 7.0f));
    EXPECT_EQ(cs.at(h), 7.0f);
    const auto& ccs = cs;
    EXPECT_EQ(ccs.at(h).component<float>(), 7.0f);
    EXPECT_EQ(ccs.at(h).id(), h.id());
    reg_t::handle_t bad{};
    EXPECT_THROW(cs.at(bad), std::invalid_argument);
    EXPECT_THROW(ccs.at(bad), std::invalid_argument);
  }
}

void Scene_StorageTypeAccess() {
  // storage<STORAGE>() type-based access refers to the same object as
  // storage<SID>() enum-based access.
  if (true) {
    two_storage_scene_t s;
    auto& by_id = s.storage<scene_sid_t{1}>();
    auto& by_type = s.storage<arch_pv_t>();
    EXPECT_TRUE(&by_id == &by_type);
    EXPECT_EQ(by_type.store_id(), scene_sid_t{1});

    auto& by_id2 = s.storage<scene_sid_t{2}>();
    auto& by_type2 = s.storage<arch_pvh_t>();
    EXPECT_TRUE(&by_id2 == &by_type2);
    EXPECT_EQ(by_type2.store_id(), scene_sid_t{2});
  }

  // Const access: both overloads are const-correct.
  if (true) {
    two_storage_scene_t s;
    const auto& cs = s;
    const auto& s1 = cs.storage<arch_pv_t>();
    EXPECT_EQ(s1.store_id(), scene_sid_t{1});
    EXPECT_TRUE(s1.empty());
    const auto& s2 = cs.storage<arch_pvh_t>();
    EXPECT_EQ(s2.store_id(), scene_sid_t{2});
  }

  // Data is visible through both access paths after insertion.
  if (true) {
    two_storage_scene_t s;
    auto h =
        s.add_new<scene_sid_t{1}>({}, Position{1.f, 2.f}, Velocity{3.f, 4.f});
    const auto& st = s.storage<arch_pv_t>();
    EXPECT_EQ(st.size(), 1U);
    EXPECT_EQ(st[h.id()].component<Position>().x, 1.f);
  }
}

struct TagA {};
struct TagB {};

void ArchetypeStorage_Tag() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using arch_a_t = archetype_storage<reg_t, std::tuple<int, float>, TagA>;
  using arch_b_t = archetype_storage<reg_t, std::tuple<int, float>, TagB>;

  // tag_t alias matches the template tag parameter.
  static_assert(std::is_same_v<arch_a_t::tag_t, TagA>);
  static_assert(std::is_same_v<arch_b_t::tag_t, TagB>);
  using arch_default_t = archetype_storage<reg_t, std::tuple<int, float>>;
  static_assert(std::is_same_v<arch_default_t::tag_t, void>);

  // Two tagged storages with identical components are distinct types.
  static_assert(!std::is_same_v<arch_a_t, arch_b_t>);

  // Both coexist in a scene; type-based and enum-based access agree.
  using tagged_scene_t = scene<reg_t, arch_a_t, arch_b_t>;
  if (true) {
    tagged_scene_t s;
    EXPECT_EQ(tagged_scene_t::storage_count_v, 2U);
    auto& sa = s.storage<arch_a_t>();
    auto& sb = s.storage<arch_b_t>();
    EXPECT_EQ(sa.store_id(), scene_sid_t{1});
    EXPECT_EQ(sb.store_id(), scene_sid_t{2});
    // Entities are inserted into and retrieved from the correct typed storage.
    auto ha = s.add_new<scene_sid_t{1}>({}, 10, 1.0f);
    auto hb = s.add_new<scene_sid_t{2}>({}, 20, 2.0f);
    EXPECT_EQ(s.size(), 2U);
    EXPECT_EQ(sa[ha.id()].component<int>(), 10);
    EXPECT_EQ(sb[hb.id()].component<int>(), 20);
  }
}

void StableId_ReservePrefill() {
  // reserve(n, true) extends the ID space without inserting elements.
  if (true) {
    int_stable_ids ids;
    EXPECT_EQ(ids.max_id(), int_stable_ids::id_t::invalid);
    ids.reserve(5, true);
    EXPECT_EQ(ids.size(), 0U);                        // no elements inserted
    EXPECT_EQ(ids.max_id(), int_stable_ids::id_t{4}); // ID space set to [0,4]
  }

  // reserve(n, false) does not extend the ID space.
  if (true) {
    int_stable_ids ids;
    ids.reserve(5, false);
    EXPECT_EQ(ids.size(), 0U);
    EXPECT_EQ(ids.max_id(), int_stable_ids::id_t::invalid);
  }

  // After reserve(n, true), push_back uses the pre-filled slots correctly.
  if (true) {
    int_stable_ids ids;
    ids.reserve(3, true);
    auto id0 = ids.push_back(10);
    auto id1 = ids.push_back(20);
    auto id2 = ids.push_back(30);
    EXPECT_TRUE(ids.is_valid(id0));
    EXPECT_TRUE(ids.is_valid(id1));
    EXPECT_TRUE(ids.is_valid(id2));
    EXPECT_EQ(ids[id0], 10);
    EXPECT_EQ(ids[id1], 20);
    EXPECT_EQ(ids[id2], 30);
    EXPECT_EQ(ids.size(), 3U);
  }
}

void ChunkedArchetypeStorage_SwapAndMove() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using arch_t = chunked_archetype_storage<reg_t, std::tuple<int, float>, 4>;
  const auto sid1 = store_id_t{1};
  const auto sid2 = store_id_t{2};
  const loc_t staging{store_id_t{}};

  // swap() (member) exchanges component data and store_ids.
  if (true) {
    reg_t r;
    arch_t a{r, sid1};
    arch_t b{r, sid2};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 11, 1.0f));
    EXPECT_TRUE(b.add(id1, 22, 2.0f));
    a.swap(b);
    EXPECT_EQ(a.store_id(), sid2);
    EXPECT_EQ(b.store_id(), sid1);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_EQ(b.size(), 1U);
    EXPECT_EQ(a[id1].component<int>(), 22);
    EXPECT_EQ(b[id0].component<int>(), 11);
  }

  // swap() (free function) exchanges component data and store_ids.
  if (true) {
    reg_t r;
    arch_t a{r, sid1};
    arch_t b{r, sid2};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(a.add(id0, 11, 1.0f));
    EXPECT_TRUE(b.add(id1, 22, 2.0f));
    swap(a, b);
    EXPECT_EQ(a.store_id(), sid2);
    EXPECT_EQ(b.store_id(), sid1);
    EXPECT_EQ(a[id1].component<int>(), 22);
    EXPECT_EQ(b[id0].component<int>(), 11);
  }

  // shrink_to_fit after reserve reduces wasted capacity.
  if (true) {
    reg_t r;
    arch_t a{r, sid1};
    a.reserve(100);
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(a.add(id0, 1, 1.0f));
    a.shrink_to_fit();
    EXPECT_EQ(a.size(), 1U);
    EXPECT_LT(a.capacity(), 100U);
  }

  // Move constructor transfers all data; source is left valid and empty.
  // Destructor of the move-destination erases entities from registry.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    {
      arch_t a{r, sid1};
      EXPECT_TRUE(a.add(id0, 42, 1.0f));
      arch_t b{std::move(a)};
      EXPECT_EQ(b.size(), 1U);
      EXPECT_EQ(b.store_id(), sid1);
      EXPECT_EQ(b[id0].component<int>(), 42);
    } // b destructor fires
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Move assignment transfers data from source to destination. Destructor
  // of the move-destination erases entities from registry.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    {
      arch_t a{r, sid1};
      arch_t b{r, sid2};
      EXPECT_TRUE(a.add(id0, 7, 7.0f));
      b = std::move(a);
      EXPECT_EQ(b.size(), 1U);
      EXPECT_EQ(b.store_id(), sid1);
      EXPECT_EQ(b[id0].component<int>(), 7);
    } // b destructor fires
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Destructor clears all entities from the registry (regression guard).
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    {
      arch_t a{r, sid1};
      EXPECT_TRUE(a.add(id0, 1, 1.0f));
      EXPECT_TRUE(a.add(id1, 2, 2.0f));
    } // destructor fires
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
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

MAKE_TEST_LIST(ArchetypeStorage_Basic, ArchetypeStorage_Registry,
    ArchetypeStorage_Add, ArchetypeStorage_Remove, ArchetypeStorage_Erase,
    ArchetypeStorage_RowAccess, ArchetypeStorage_ComponentAccess,
    ArchetypeStorage_Limit, ArchetypeStorage_SwapAndMove,
    ArchetypeStorage_Iterator, ArchetypeStorage_EraseIf, ArchetypeStorage_At,
    ArchetypeStorage_RemoveIf, ArchetypeStorage_IteratorPostIncDec,
    ArchetypeStorage_Tag, ChunkedArchetypeStorage_Basic,
    ChunkedArchetypeStorage_Add, ChunkedArchetypeStorage_RemoveAndErase,
    ChunkedArchetypeStorage_RowAndIterator, ChunkedArchetypeStorage_EraseIf,
    ChunkedArchetypeStorage_ChunkBoundary, ChunkedArchetypeStorage_At,
    ChunkedArchetypeStorage_RemoveIf, ChunkedArchetypeStorage_SwapAndMove,
    StableId_Basic, StableId_SmallId,
    StableId_NoThrow, StableId_Fifo, StableId_NoGen, StableId_FifoNoGen,
    StableId_MaxId, StableId_ReservePrefill, EntityRegistry_Basic,
    EntityRegistry_Handle, EntityRegistry_Fifo, EntityRegistry_Clear,
    EntityRegistry_Reserve, EntityRegistry_IdLimit, EntityRegistry_NoGen,
    EntityRegistry_VoidMeta, EntityRegistry_VoidNoGen,
    EntityRegistry_IdLimitAdvanced, EntityRegistry_FifoAdvanced,
    EntityRegistry_EdgeCases, EntityRegistry_MetadataCleanup,
    EntityRegistry_EraseIfPredicate, EntityRegistry_IdLimitFreeList,
    EntityRegistry_ReservePrefillExisting, EntityRegistry_HandleOwner,
    EntityRegistry_GetAllocator, ComponentStorage_Basic,
    ComponentStorage_Handle, ComponentStorage_Remove,
    ComponentStorage_RemoveAll, ComponentStorage_Erase,
    ComponentStorage_EraseIf, ComponentStorage_Clear,
    ComponentStorage_SwapAndMove, ComponentStorage_LimitAndReserve,
    ComponentStorage_Iterator, ComponentStorage_RowView, Scene_Basic,
    Scene_EraseRemove, Scene_Migrate_Manual, Scene_Migrate_Auto,
    Scene_EraseStaged, Scene_Clear, Scene_MultiStorage, Scene_MixedStorages,
    Scene_StorageTypeAccess);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
