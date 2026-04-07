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
  SimWorld w;
  EXPECT_EQ(w.size(), 0U);

  auto h = w.spawnMover(Position{10.0, 20.0}, Velocity{1.0, 2.0});
  EXPECT_TRUE(w.is_alive(h));
  EXPECT_EQ(w.size(), 1U);

  auto h2 = w.spawnMover(Position{30.0, 40.0}, Velocity{-1.0, 0.0});
  EXPECT_EQ(w.size(), 2U);

  EXPECT_TRUE(w.despawn(h));
  EXPECT_EQ(w.size(), 1U);
  EXPECT_FALSE(w.is_alive(h)); // handle is invalidated on success

  EXPECT_TRUE(w.despawn(h2));
  EXPECT_EQ(w.size(), 0U);
}

// Despawning a stale handle returns false without crashing.
void SimWorld_DespawnStale() {
  SimWorld w;
  auto h = w.spawnMover(Position{}, Velocity{});
  EXPECT_TRUE(w.despawn(h));
  EXPECT_FALSE(w.despawn(h)); // second call: handle is dead
}

// tick() advances position by velocity.
void SimWorld_Tick() {
  SimWorld w;
  auto h = w.spawnMover(Position{100.0, 200.0}, Velocity{3.0, -5.0});
  (void)w.tick();

  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_NEAR(snaps[0].pos.x, 103.0, 1e-9);
  EXPECT_NEAR(snaps[0].pos.y, 195.0, 1e-9);
  (void)h;
}

// tick() with a non-null out-vector appends changed entity IDs.
void SimWorld_TickOutParam() {
  SimWorld w;
  auto h1 = w.spawnMover(Position{100.0, 100.0}, Velocity{1.0, 0.0});
  auto h2 = w.spawnMover(Position{200.0, 200.0}, Velocity{0.0, 1.0});

  std::vector<SimWorld::EntityId> changed;
  (void)w.tick();

  EXPECT_EQ(changed.size(), 2U);
  (void)h1;
  (void)h2;
}

// tick() with nullptr out-param does not allocate (just advance state).
void SimWorld_TickNullOut() {
  SimWorld w;
  (void)w.spawnMover(Position{50.0, 50.0}, Velocity{1.0, 1.0});
  (void)w.tick(); // must not crash
  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_NEAR(snaps[0].pos.x, 51.0, 1e-9);
}

// Entities bounce off the left/top boundary.
void SimWorld_BounceMinEdge() {
  SimWorld w;
  const float half_w = SimWorld::widthOfWorld * 0.5F;
  const float half_h = SimWorld::heightOfWorld * 0.5F;
  (void)w.spawnMover(Position{-half_w + 0.5F, -half_h + 0.5F},
      Velocity{-2.0F, -2.0F});
  (void)w.tick();

  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_GE(snaps[0].pos.x, -half_w);
  EXPECT_GE(snaps[0].pos.y, -half_h);
}

// Entities bounce off the right/bottom boundary.
void SimWorld_BounceMaxEdge() {
  SimWorld w;
  const float half_w = SimWorld::widthOfWorld * 0.5F;
  const float half_h = SimWorld::heightOfWorld * 0.5F;
  (void)w.spawnMover(Position{half_w - 0.5F, half_h - 0.5F},
      Velocity{2.0F, 2.0F});
  (void)w.tick();

  auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_LE(snaps[0].pos.x, half_w);
  EXPECT_LE(snaps[0].pos.y, half_h);
}

// snapshot() with default since_tick returns all entities.
void SimWorld_SnapshotAll() {
  SimWorld w;
  (void)w.spawnMover(Position{1.0, 2.0}, Velocity{});
  (void)w.spawnMover(Position{3.0, 4.0}, Velocity{});

  auto snaps = w.snapshot();
  EXPECT_EQ(snaps.size(), 2U);
}

// snapshot(since_tick) returns only entities changed at or after that tick.
void SimWorld_SnapshotSince() {
  SimWorld w;
  // Spawn two entities before any tick (their last-change tick = 0).
  (void)w.spawnMover(Position{10.0, 10.0}, Velocity{1.0, 0.0});
  (void)w.spawnMover(Position{20.0, 20.0}, Velocity{1.0, 0.0});

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
  SimWorld w;
  auto h1 = w.spawnMover(Position{1.0, 1.0}, Velocity{});
  auto h2 = w.spawnMover(Position{2.0, 2.0}, Velocity{});
  auto h3 = w.spawnMover(Position{3.0, 3.0}, Velocity{});

  std::vector<SimWorld::EntityId> ids{h1.id(), h3.id()};
  auto snaps = w.snapshot(ids);
  ASSERT_EQ(snaps.size(), 2U);
  // The results should be for h1 and h3, not h2.
  EXPECT_EQ(snaps[0].id, h1.id());
  EXPECT_EQ(snaps[1].id, h3.id());
  (void)h2;
}

// snapshot(ids) silently skips dead entity IDs.
void SimWorld_SnapshotByIdsSkipsDead() {
  SimWorld w;
  auto h = w.spawnMover(Position{5.0, 5.0}, Velocity{});
  auto dead_id = h.id();
  EXPECT_TRUE(w.despawn(h));

  std::vector<SimWorld::EntityId> ids{dead_id};
  auto snaps = w.snapshot(ids);
  EXPECT_EQ(snaps.size(), 0U);
}

// spawn_background adds a P-only entity visible in snapshots.
void SimWorld_Background() {
  SimWorld w;
  auto hb = w.spawnBackground(Position{50.0, 60.0});
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
  SimWorld w;
  (void)w.spawnBackground(Position{50.0, 60.0});
  (void)w.tick(); // tick 1; background entity metadata stays at 0
  EXPECT_EQ(w.snapshot(1).size(), 0U);
}

// --- Path geometry tests ---

// bake_path with two joints produces one segment with the correct length.
void BakePath_TwoJoints() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{3.F, 4.F}}};
  const auto bp = SegmentedPath::fromJoints(p);
  ASSERT_EQ(bp.segments.size(), 1U);
  EXPECT_NEAR(bp.segments[0].cumulativeStart, 0.0, 1e-6);
  EXPECT_NEAR(bp.segments[0].length, 5.0, 1e-6);
  EXPECT_NEAR(bp.totalLength, 5.0, 1e-6);
}

// bake_path with three joints produces two segments with correct cumulative
// distances.
void BakePath_ThreeJoints() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{3.F, 4.F}}, {{6.F, 8.F}}};
  const auto bp = SegmentedPath::fromJoints(p);
  ASSERT_EQ(bp.segments.size(), 2U);
  EXPECT_NEAR(bp.segments[0].cumulativeStart, 0.0, 1e-6);
  EXPECT_NEAR(bp.segments[0].length, 5.0, 1e-6);
  EXPECT_NEAR(bp.segments[1].cumulativeStart, 5.0, 1e-6);
  EXPECT_NEAR(bp.segments[1].length, 5.0, 1e-6);
  EXPECT_NEAR(bp.totalLength, 10.0, 1e-6);
}

// bake_path with fewer than two joints returns an empty baked_path.
void BakePath_Degenerate() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}};
  const auto bp = SegmentedPath::fromJoints(p);
  EXPECT_TRUE(bp.segments.empty());
  EXPECT_NEAR(bp.totalLength, 0.0, 1e-6);
}

// path_position at progress 0 returns the first joint; at total_length
// returns the last joint.
void PathPosition_Endpoints() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto bp = SegmentedPath::fromJoints(p);

  const auto start = bp.calculatePositionFromProgress(0.F);
  EXPECT_NEAR(start.x, 0.0, 1e-6);
  EXPECT_NEAR(start.y, 0.0, 1e-6);

  const auto end = bp.calculatePositionFromProgress(bp.totalLength);
  EXPECT_NEAR(end.x, 10.0, 1e-6);
  EXPECT_NEAR(end.y, 0.0, 1e-6);
}

// path_position at the midpoint returns the correctly interpolated position.
void PathPosition_Midpoint() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto bp = SegmentedPath::fromJoints(p);

  const auto mid = bp.calculatePositionFromProgress(5.F);
  EXPECT_NEAR(mid.x, 5.0, 1e-6);
  EXPECT_NEAR(mid.y, 0.0, 1e-6);
}

// Spawning an enemy and ticking once advances its position by `speed`.
void SimWorld_EnemyAdvancesOnTick() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{100.F, 0.F}}};
  const auto pid = w.addPath(p);

  auto h = w.spawnEnemy(pid, 10.F);
  EXPECT_TRUE(w.is_alive(h));

  (void)w.tick();

  const auto snaps = w.snapshot();
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_NEAR(snaps[0].pos.x, 10.0, 1e-5);
  EXPECT_NEAR(snaps[0].pos.y, 0.0, 1e-5);
}

// An enemy that reaches the end of the path is despawned automatically.
void SimWorld_EnemyDespawnsAtEnd() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto pid = w.addPath(p);

  // Start near the end; one tick will push progress past total_length.
  auto h = w.spawnEnemy(pid, 5.F, 8.F);
  EXPECT_EQ(w.size(), 1U);

  (void)w.tick();

  EXPECT_EQ(w.size(), 0U);
  EXPECT_FALSE(w.is_alive(h));
}

// get_path returns nullptr for an out-of-range index.
void SimWorld_GetPathOutOfRange() {
  SimWorld w;
  EXPECT_TRUE(w.getPath(PathId{0}) == nullptr);

  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{1.F, 0.F}}};
  (void)w.addPath(p);

  EXPECT_TRUE(w.getPath(PathId{0}) != nullptr);
  EXPECT_TRUE(w.getPath(PathId{1}) == nullptr);
}

// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(SimWorld_SpawnDespawn, SimWorld_DespawnStale, SimWorld_Tick,
    SimWorld_TickOutParam, SimWorld_TickNullOut, SimWorld_BounceMinEdge,
    SimWorld_BounceMaxEdge, SimWorld_SnapshotAll, SimWorld_SnapshotSince,
    SimWorld_SnapshotByIds, SimWorld_SnapshotByIdsSkipsDead,
    SimWorld_Background, SimWorld_BackgroundNotInDeltaAfterTick,
    BakePath_TwoJoints, BakePath_ThreeJoints, BakePath_Degenerate,
    PathPosition_Endpoints, PathPosition_Midpoint,
    SimWorld_EnemyAdvancesOnTick, SimWorld_EnemyDespawnsAtEnd,
    SimWorld_GetPathOutOfRange)
