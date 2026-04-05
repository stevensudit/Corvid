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
#include <vector>

#include "../corvid/sim/sim_world.h"
#include "minitest.h"

using namespace corvid;
using namespace corvid::sim;

// NOLINTBEGIN(readability-function-cognitive-complexity)

// Spawn, size, and despawn round-trip.
void SimWorld_SpawnDespawn() {
  sim_world w;
  EXPECT_EQ(w.size(), 0U);

  auto h = w.spawn(Position{10.0, 20.0}, Velocity{1.0, 2.0});
  EXPECT_TRUE(w.is_alive(h));
  EXPECT_EQ(w.size(), 1U);

  auto h2 = w.spawn(Position{30.0, 40.0}, Velocity{-1.0, 0.0});
  EXPECT_EQ(w.size(), 2U);

  EXPECT_TRUE(w.despawn(h));
  EXPECT_EQ(w.size(), 1U);
  EXPECT_FALSE(w.is_alive(h)); // handle is invalidated on success

  EXPECT_TRUE(w.despawn(h2));
  EXPECT_EQ(w.size(), 0U);
}

// Despawning a stale handle returns false without crashing.
void SimWorld_DespawnStale() {
  sim_world w;
  auto h = w.spawn(Position{}, Velocity{});
  EXPECT_TRUE(w.despawn(h));
  EXPECT_FALSE(w.despawn(h)); // second call: handle is dead
}

// tick() advances position by velocity.
void SimWorld_Tick() {
  sim_world w;
  auto h = w.spawn(Position{100.0, 200.0}, Velocity{3.0, -5.0});
  (void)w.tick();

  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_NEAR(snaps[0].pos.x, 103.0, 1e-9);
  EXPECT_NEAR(snaps[0].pos.y, 195.0, 1e-9);
  (void)h;
}

// tick() with a non-null out-vector appends changed entity IDs.
void SimWorld_TickOutParam() {
  sim_world w;
  auto h1 = w.spawn(Position{100.0, 100.0}, Velocity{1.0, 0.0});
  auto h2 = w.spawn(Position{200.0, 200.0}, Velocity{0.0, 1.0});

  std::vector<sim_world::entity_id_t> changed;
  (void)w.tick(&changed);

  EXPECT_EQ(changed.size(), 2U);
  (void)h1;
  (void)h2;
}

// tick() with nullptr out-param does not allocate (just advance state).
void SimWorld_TickNullOut() {
  sim_world w;
  (void)w.spawn(Position{50.0, 50.0}, Velocity{1.0, 1.0});
  (void)w.tick(nullptr); // must not crash
  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_NEAR(snaps[0].pos.x, 51.0, 1e-9);
}

// Entities bounce off the left/top boundary.
void SimWorld_BounceMinEdge() {
  sim_world w;
  const double half_w = sim_world::world_width * 0.5;
  const double half_h = sim_world::world_height * 0.5;
  (void)w.spawn(Position{-half_w + 0.5, -half_h + 0.5}, Velocity{-2.0, -2.0});
  (void)w.tick();

  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_GE(snaps[0].pos.x, -half_w);
  EXPECT_GE(snaps[0].pos.y, -half_h);
}

// Entities bounce off the right/bottom boundary.
void SimWorld_BounceMaxEdge() {
  sim_world w;
  const double half_w = sim_world::world_width * 0.5;
  const double half_h = sim_world::world_height * 0.5;
  (void)w.spawn(Position{half_w - 0.5, half_h - 0.5}, Velocity{2.0, 2.0});
  (void)w.tick();

  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_LE(snaps[0].pos.x, half_w);
  EXPECT_LE(snaps[0].pos.y, half_h);
}

// snapshot() with default since_tick returns all entities.
void SimWorld_SnapshotAll() {
  sim_world w;
  (void)w.spawn(Position{1.0, 2.0}, Velocity{});
  (void)w.spawn(Position{3.0, 4.0}, Velocity{});

  auto snaps = w.snapshot();
  EXPECT_EQ(snaps.size(), 2U);
}

// snapshot(since_tick) returns only entities changed at or after that tick.
void SimWorld_SnapshotSince() {
  sim_world w;
  // Spawn two entities before any tick (their last-change tick = 0).
  (void)w.spawn(Position{10.0, 10.0}, Velocity{1.0, 0.0});
  (void)w.spawn(Position{20.0, 20.0}, Velocity{1.0, 0.0});

  (void)w.tick(); // tick 1: both entities move, metadata set to 1

  // Everything changed at tick 1 or later -> 2 results.
  EXPECT_EQ(w.snapshot(1).size(), 2U);

  // Nothing changed at tick 2 or later -> 0 results.
  EXPECT_EQ(w.snapshot(2).size(), 0U);

  (void)w.tick(); // tick 2: both entities move again, metadata set to 2

  // Now both qualify for since_tick=2.
  EXPECT_EQ(w.snapshot(2).size(), 2U);

  // Still nothing at tick 3.
  EXPECT_EQ(w.snapshot(3).size(), 0U);
}

// snapshot(ids) returns only the requested entities.
void SimWorld_SnapshotByIds() {
  sim_world w;
  auto h1 = w.spawn(Position{1.0, 1.0}, Velocity{});
  auto h2 = w.spawn(Position{2.0, 2.0}, Velocity{});
  auto h3 = w.spawn(Position{3.0, 3.0}, Velocity{});

  std::vector<sim_world::entity_id_t> ids{h1.id(), h3.id()};
  auto snaps = w.snapshot(ids);
  ASSERT_EQ(snaps.size(), 2U);
  // The results should be for h1 and h3, not h2.
  EXPECT_EQ(snaps[0].id, h1.id());
  EXPECT_EQ(snaps[1].id, h3.id());
  (void)h2;
}

// snapshot(ids) silently skips dead entity IDs.
void SimWorld_SnapshotByIdsSkipsDead() {
  sim_world w;
  auto h = w.spawn(Position{5.0, 5.0}, Velocity{});
  auto dead_id = h.id();
  EXPECT_TRUE(w.despawn(h));

  std::vector<sim_world::entity_id_t> ids{dead_id};
  auto snaps = w.snapshot(ids);
  EXPECT_EQ(snaps.size(), 0U);
}

// spawn_background adds a P-only entity visible in snapshots.
void SimWorld_Background() {
  sim_world w;
  auto hb = w.spawn_background(Position{50.0, 60.0});
  EXPECT_TRUE(w.is_alive(hb));
  EXPECT_EQ(w.size(), 1U);

  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_NEAR(snaps[0].pos.x, 50.0, 1e-9);
  EXPECT_NEAR(snaps[0].pos.y, 60.0, 1e-9);

  // Background entity does not move on tick.
  (void)w.tick();
  snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_NEAR(snaps[0].pos.x, 50.0, 1e-9);
}

// Background entity is excluded from snapshot(since_tick) after a tick
// because its metadata was never updated beyond its spawn tick (0).
void SimWorld_BackgroundNotInDeltaAfterTick() {
  sim_world w;
  (void)w.spawn_background(Position{50.0, 60.0});
  (void)w.tick(); // tick 1; background entity metadata stays at 0
  EXPECT_EQ(w.snapshot(1).size(), 0U);
}

// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(SimWorld_SpawnDespawn, SimWorld_DespawnStale, SimWorld_Tick,
    SimWorld_TickOutParam, SimWorld_TickNullOut, SimWorld_BounceMinEdge,
    SimWorld_BounceMaxEdge, SimWorld_SnapshotAll, SimWorld_SnapshotSince,
    SimWorld_SnapshotByIds, SimWorld_SnapshotByIdsSkipsDead,
    SimWorld_Background, SimWorld_BackgroundNotInDeltaAfterTick)
