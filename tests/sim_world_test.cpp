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

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../corvid/sim/sim_game.h"
#include "../corvid/sim/sim_json_parse.h"
#include "../corvid/sim/sim_json_wire.h"
#include "minitest.h"

using namespace corvid;
using namespace corvid::sim;

namespace {

struct WorldDelta {
  struct Upsert {
    SimWorld::EntityId id;
    Position pos;
    Appearance app;
    VisualEffects fx;
  };

  std::vector<Upsert> upserts;
  std::vector<SimWorld::EntityId> erased;
};

struct GameDelta {
  std::vector<std::pair<SimWorld::EntityId, Position>> upserts;
  std::vector<SimWorld::EntityId> erased;
  size_t currentWave{};
  WaveTick waveTick{};
  int lives{};
  int resources{};
  std::string_view phase;
  std::optional<bool> placementAllowed;
  std::optional<bool> spawnAllowed;
  bool defenderSelected{};
  std::optional<DefenderSummary> defenderSummary;
};

struct EntitySnapshot {
  SimWorld::EntityId id;
  Position pos;
};

struct GameSnapshot {
  std::vector<EntitySnapshot> entities;
  std::vector<PathJoints> path_points;
};

[[nodiscard]] std::vector<EntitySnapshot> snapshot(SimWorld& world) {
  std::vector<EntitySnapshot> snaps;
  (void)world.markAllDirty();
  (void)world.extractUpdatedEntities(
      [&snaps](SimWorld::EntityId id, const Position& pos, const Appearance&,
          const VisualEffects&) {
        const auto it = std::ranges::find(snaps, id, &EntitySnapshot::id);
        if (it == snaps.end())
          snaps.push_back({id, pos});
        else
          it->pos = pos;
      },
      [](SimWorld::EntityId) {});
  return snaps;
}

[[nodiscard]] std::vector<EntitySnapshot>
filterSnapshot(const std::vector<EntitySnapshot>& all,
    const std::vector<SimWorld::EntityId>& ids) {
  std::vector<EntitySnapshot> filtered;
  filtered.reserve(ids.size());
  std::ranges::copy_if(all, std::back_inserter(filtered),
      [&ids](const EntitySnapshot& snap) {
        return std::ranges::find(ids, snap.id) != ids.end();
      });
  return filtered;
}

[[nodiscard]] WorldDelta extractWorldDelta(SimWorld& world) {
  WorldDelta delta;
  (void)world.extractUpdatedEntities(
      [&delta](SimWorld::EntityId id, const Position& pos, const Appearance&,
          const VisualEffects& fx) {
        delta.upserts.push_back({id, pos, {}, fx});
      },
      [&delta](SimWorld::EntityId id) { delta.erased.push_back(id); });
  return delta;
}

[[nodiscard]] GameSnapshot snapshot(SimGame& game) {
  GameSnapshot snap;
  std::vector<PathJoints> pathsById;

  (void)game.extractFull(
      [&pathsById](PathId pathId, const Position& pos) {
        const auto ndx = static_cast<size_t>(*pathId);
        if (pathsById.size() <= ndx) pathsById.resize(ndx + 1);
        pathsById[ndx].joints.push_back({pos});
      },
      [&snap](SimWorld::EntityId id, const Position& pos, const Appearance&,
          const VisualEffects&) { snap.entities.push_back({id, pos}); },
      [](SimWorld::EntityId) {},
      [](size_t, WaveTick, int, int, std::string_view, const UiState&) {});

  snap.path_points = std::move(pathsById);
  return snap;
}

[[nodiscard]] GameDelta extractGameDelta(SimGame& game) {
  GameDelta delta;
  (void)game.extractDelta(
      [&delta](SimWorld::EntityId id, const Position& pos, const Appearance&,
          const VisualEffects&) { delta.upserts.emplace_back(id, pos); },
      [&delta](SimWorld::EntityId id) { delta.erased.push_back(id); },
      [&delta](size_t currentWave, WaveTick waveTick, int lives, int resources,
          std::string_view phase, const UiState& uiState) {
        delta.currentWave = currentWave;
        delta.waveTick = waveTick;
        delta.lives = lives;
        delta.resources = resources;
        delta.phase = phase;
        delta.placementAllowed = uiState.placementAllowed;
        delta.spawnAllowed = uiState.spawnAllowed;
        delta.defenderSelected = uiState.defenderSelected;
        delta.defenderSummary = uiState.defenderSummary;
      });
  return delta;
}

[[nodiscard]] bool containsId(const auto& entries, SimWorld::EntityId id) {
  return std::ranges::any_of(entries, [id](const auto& entry) {
    if constexpr (requires { entry.id; })
      return entry.id == id;
    else
      return entry.first == id;
  });
}

[[nodiscard]] const Position*
findPosition(const auto& entries, SimWorld::EntityId id) {
  const auto it = std::ranges::find_if(entries, [id](const auto& entry) {
    if constexpr (requires { entry.id; })
      return entry.id == id;
    else
      return entry.first == id;
  });
  if (it == entries.end()) return nullptr;
  if constexpr (requires { it->pos; })
    return &it->pos;
  else
    return &it->second;
}

// Test helpers that register the entity type on demand (idempotent) and spawn
// with placement applied, matching the old `spawnInvaderAlpha` /
// `spawnDefenderAoe` behavior.
[[nodiscard]] SimWorld::Handle
spawnInvaderAlpha(SimWorld& w, PathId pid, float progress = 0.F) {
  WorldScene::megatuple_t tpl{};
  std::get<std::optional<Position>>(tpl) = Position{};
  std::get<std::optional<Appearance>>(tpl) = Appearance{.glyph = U'\u03B1',
      .radius = 30.F,
      .fgColor = 0xFFFFFFFF,
      .bgColor = 0x000000FF};
  std::get<std::optional<VisualEffects>>(tpl) = VisualEffects{};
  std::get<std::optional<Pathing>>(tpl) =
      Pathing{.pathId = PathId::invalid, .progress = 0.F, .speed = 50.F};
  std::get<std::optional<Invader>>(tpl) =
      Invader{.hitCircleRadius = 30.F, .bounty = 10};
  std::get<std::optional<Health>>(tpl) =
      Health{.currentHealth = 100.F, .maxHealth = 100.F, .regen = 10.F};
  (void)w.registerEntity("InvaderAlphaBasic", tpl);
  auto h = w.spawnEntity("InvaderAlphaBasic");
  if (!h) return h;
  if (auto* pat = w.try_get_component<Pathing>(h.id())) {
    pat->pathId = pid;
    pat->progress = progress;
    if (auto* pos = w.try_get_component<Position>(h.id()))
      if (const auto* path = w.getPath(pid))
        *pos = path->calculatePositionFromProgress(progress, progress);
  }
  return h;
}

[[nodiscard]] SimWorld::Handle
spawnDefenderAoe(SimWorld& w, Position spawn_pos) {
  WorldScene::megatuple_t tpl{};
  std::get<std::optional<Position>>(tpl) = Position{};
  std::get<std::optional<Appearance>>(tpl) = Appearance{.glyph = U'A',
      .radius = 30.F,
      .fgColor = 0xFFFFFFFF,
      .bgColor = 0x7F7FFFFF};
  std::get<std::optional<VisualEffects>>(tpl) = VisualEffects{};
  std::get<std::optional<Defender>>(tpl) = Defender{.hitCircleRadius = 30.F,
      .attackRadius = 100.F,
      .rangeColor = 0xFFFF0000,
      .attackDamage = 5.F,
      .cooldown = WorldTick{20},
      .nextAttack = WorldTick{0}};
  std::get<std::optional<DefenderStats>>(tpl) = DefenderStats{};
  std::get<std::optional<Health>>(tpl) =
      Health{.currentHealth = 100.F, .maxHealth = 100.F, .regen = 0.F};
  std::get<std::optional<DefenderAoe>>(tpl) = DefenderAoe{.damageType = 1};
  (void)w.registerEntity("DefenderAoeBasic", tpl);
  auto h = w.spawnEntity("DefenderAoeBasic");
  if (!h) return h;
  if (auto* pos = w.try_get_component<Position>(h.id())) *pos = spawn_pos;
  return h;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

void SimWorld_SpawnAndSnapshot() {
  SimWorld w;
  EXPECT_EQ(w.size(), 0U);

  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{200.F, 0.F}}};
  const auto pid = w.addPath(p);

  const auto invader = spawnInvaderAlpha(w, pid);
  const auto defender = spawnDefenderAoe(w, Position{30.F, 40.F});

  EXPECT_EQ(w.size(), 2U);

  const auto all = snapshot(w);
  ASSERT_EQ(all.size(), 2U);
  EXPECT_EQ(filterSnapshot(all, std::vector<SimWorld::EntityId>{invader.id()})
                .size(),
      1U);
  EXPECT_EQ(
      filterSnapshot(all,
          std::vector<SimWorld::EntityId>{invader.id(), defender.id()})
          .size(),
      2U);
}

void SimWorld_NextMovesInvaderAlpha() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{200.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto invader = spawnInvaderAlpha(w, pid);

  (void)w.next();

  const auto snaps = snapshot(w);
  EXPECT_EQ(*w.tick(), 1U);
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_EQ(snaps[0].id, invader.id());
  EXPECT_NEAR(snaps[0].pos.x, 50.0, 1e-6);
  EXPECT_NEAR(snaps[0].pos.y, 0.0, 1e-6);
}

void SimWorld_ExtractUpdatedEntitiesReportsMovedInvaderOncePerExtraction() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{300.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto invader = spawnInvaderAlpha(w, pid);

  (void)w.next();
  (void)w.tick();
  const auto delta = extractWorldDelta(w);

  EXPECT_TRUE(containsId(delta.upserts, invader.id()));
  EXPECT_TRUE(delta.erased.empty());

  const auto* pos = findPosition(delta.upserts, invader.id());
  ASSERT_TRUE(pos != nullptr);
  EXPECT_NEAR(pos->x, 50.0, 1e-6);
  EXPECT_NEAR(pos->y, 0.0, 1e-6);

  const auto empty_delta = extractWorldDelta(w);
  EXPECT_TRUE(empty_delta.upserts.empty());
  EXPECT_TRUE(empty_delta.erased.empty());
}

void SimWorld_DefenderInRangeFlashesItselfAndInvader() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{500.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto defender = spawnDefenderAoe(w, {0.F, 0.F});
  const auto invader = spawnInvaderAlpha(w, pid);

  (void)extractWorldDelta(w);
  (void)w.tick();
  (void)w.next();

  const auto delta = extractWorldDelta(w);
  ASSERT_EQ(delta.upserts.size(), 2U);
  EXPECT_TRUE(containsId(delta.upserts, defender.id()));
  EXPECT_TRUE(containsId(delta.upserts, invader.id()));

  const auto defender_it =
      std::ranges::find(delta.upserts, defender.id(), &WorldDelta::Upsert::id);
  ASSERT_TRUE(defender_it != delta.upserts.end());
  EXPECT_EQ(defender_it->fx.flashColor, 0xFFFFFFFFU);
  EXPECT_EQ(defender_it->fx.flashExpiry, WorldTick{6});

  const auto invader_it =
      std::ranges::find(delta.upserts, invader.id(), &WorldDelta::Upsert::id);
  ASSERT_TRUE(invader_it != delta.upserts.end());
  EXPECT_EQ(invader_it->fx.flashColor, 0xFF7F7FFFU);
  EXPECT_EQ(invader_it->fx.flashExpiry, WorldTick{6});
  EXPECT_NEAR(invader_it->pos.x, 50.0, 1e-6);
}

void SimWorld_SnapshotSinceTracksChanges() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{500.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto invader = spawnInvaderAlpha(w, pid);
  (void)spawnDefenderAoe(w, Position{500.F, 500.F});

  // Drain the spawn-time dirty list; both entities visible at tick_=0.
  const auto initial = extractWorldDelta(w);
  EXPECT_EQ(initial.upserts.size(), 2U);

  const auto unchanged = extractWorldDelta(w);
  EXPECT_TRUE(unchanged.upserts.empty());
  EXPECT_TRUE(unchanged.erased.empty());

  // Advance tick first so next() physics runs at a new tick value and can
  // re-mark the mover (last_updated=0 != tick_=1).
  (void)w.tick();
  (void)w.next();

  const auto moved = extractWorldDelta(w);
  EXPECT_EQ(moved.upserts.size(), 1U);
  EXPECT_TRUE(moved.erased.empty());
  EXPECT_TRUE(containsId(moved.upserts, invader.id()));

  (void)w.tick();

  const auto stable = extractWorldDelta(w);
  EXPECT_TRUE(stable.upserts.empty());
  EXPECT_TRUE(stable.erased.empty());
}

void SimWorld_DefenderDoesNotAppearAsChangedAfterTick() {
  SimWorld w;
  const auto defender = spawnDefenderAoe(w, Position{50.F, 60.F});

  const auto initial = extractWorldDelta(w);
  ASSERT_EQ(initial.upserts.size(), 1U);
  EXPECT_TRUE(containsId(initial.upserts, defender.id()));

  (void)w.tick();
  (void)w.next();

  const auto snaps = snapshot(w);
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_NEAR(snaps[0].pos.x, 50.0, 1e-6);
  EXPECT_NEAR(snaps[0].pos.y, 60.0, 1e-6);

  const auto delta = extractWorldDelta(w);
  EXPECT_TRUE(delta.upserts.empty());
  EXPECT_TRUE(delta.erased.empty());
}

void BakePath_TwoJoints() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{3.F, 4.F}}};

  const auto bp = SegmentedPath::fromJoints(p);

  ASSERT_EQ(bp.segments.size(), 1U);
  EXPECT_NEAR(bp.segments[0].cumulativeStart, 0.0, 1e-6);
  EXPECT_NEAR(bp.segments[0].length, 5.0, 1e-6);
  EXPECT_NEAR(bp.totalLength, 5.0, 1e-6);
}

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

void BakePath_Degenerate() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}};

  const auto bp = SegmentedPath::fromJoints(p);

  EXPECT_TRUE(bp.segments.empty());
  EXPECT_NEAR(bp.totalLength, 0.0, 1e-6);
}

void PathPosition_Endpoints() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto bp = SegmentedPath::fromJoints(p);

  const auto start = bp.calculatePositionFromProgress(0.F, 0.F);
  const auto end =
      bp.calculatePositionFromProgress(bp.totalLength, bp.totalLength);

  EXPECT_NEAR(start.x, 0.0, 1e-6);
  EXPECT_NEAR(start.y, 0.0, 1e-6);
  EXPECT_NEAR(end.x, 10.0, 1e-6);
  EXPECT_NEAR(end.y, 0.0, 1e-6);
}

void PathPosition_Midpoint() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto bp = SegmentedPath::fromJoints(p);

  const auto mid = bp.calculatePositionFromProgress(5.F, 5.F);

  EXPECT_NEAR(mid.x, 5.0, 1e-6);
  EXPECT_NEAR(mid.y, 0.0, 1e-6);
}

void PathPosition_CrossingSegmentBoundaryEmitsJoint() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}, {{10.F, 10.F}}};
  const auto bp = SegmentedPath::fromJoints(p);

  const auto corner = bp.calculatePositionFromProgress(12.F, 8.F);

  EXPECT_NEAR(corner.x, 10.0, 1e-6);
  EXPECT_NEAR(corner.y, 0.0, 1e-6);
}

void SimWorld_EnemyAdvancesOnTick() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{100.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto enemy = spawnInvaderAlpha(w, pid);

  EXPECT_TRUE(static_cast<bool>(enemy));
  EXPECT_EQ(w.size(), 1U);

  (void)w.next();
  (void)w.tick();

  const auto delta = extractWorldDelta(w);
  EXPECT_TRUE(containsId(delta.upserts, enemy.id()));
  ASSERT_EQ(delta.upserts.size(), 1U);
  EXPECT_NEAR(delta.upserts[0].pos.x, 50.0, 1e-5);
  EXPECT_NEAR(delta.upserts[0].pos.y, 0.0, 1e-5);
}

void SimWorld_ResolveEscapeesVisitsEscapedEnemy() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto enemy = spawnInvaderAlpha(w, pid, 8.F);

  (void)w.next();
  EXPECT_EQ(w.size(), 1U);

  size_t resolved = 0;
  (void)w.resolveEscapees(
      [&](SimWorld::EntityId id, const Position& pos, const Pathing& pf) {
        ++resolved;
        EXPECT_EQ(id, enemy.id());
        EXPECT_NEAR(pos.x, 8.0, 1e-6);
        EXPECT_NEAR(pos.y, 0.0, 1e-6);
        EXPECT_NEAR(pf.progress, 58.0, 1e-6);
        EXPECT_NEAR(pf.speed, 50.0, 1e-6);
        return true;
      });

  EXPECT_EQ(resolved, 1U);
  EXPECT_EQ(w.size(), 0U);

  const auto delta = extractWorldDelta(w);
  EXPECT_TRUE(delta.upserts.empty());
  EXPECT_EQ(delta.erased.size(), 1U);
  EXPECT_EQ(delta.erased[0], enemy.id());

  const auto snaps = snapshot(w);
  EXPECT_TRUE(snaps.empty());
  (void)w.tick();
}

void SimWorld_ResolveEscapeesCanLeaveEnemyAlive() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto enemy = spawnInvaderAlpha(w, pid, 8.F);

  (void)w.next();

  size_t resolved = 0;
  (void)w.resolveEscapees(
      [&](SimWorld::EntityId id, const Position&, const Pathing&) {
        ++resolved;
        EXPECT_EQ(id, enemy.id());
        return false;
      });

  EXPECT_EQ(resolved, 1U);
  EXPECT_EQ(w.size(), 1U);

  const auto snaps = snapshot(w);
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_EQ(snaps[0].id, enemy.id());
  EXPECT_NEAR(snaps[0].pos.x, 8.0, 1e-6);
  (void)w.tick();
}

void SimWorld_GetPathOutOfRange() {
  SimWorld w;
  EXPECT_TRUE(w.getPath(PathId{0}) == nullptr);

  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{1.F, 0.F}}};
  (void)w.addPath(p);

  EXPECT_TRUE(w.getPath(PathId{0}) != nullptr);
  EXPECT_TRUE(w.getPath(PathId{1}) == nullptr);
}

void SimWorld_ObtainPathIncludesTerminalJoint() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}, {{10.F, 5.F}}};
  const auto pid = w.addPath(p);

  std::vector<Position> points;
  (void)w.obtainPath(
      [&points](PathId, const Position& pos) {
        points.push_back(pos);
        return true;
      },
      pid);

  ASSERT_EQ(points.size(), 3U);
  EXPECT_NEAR(points[0].x, 0.0, 1e-6);
  EXPECT_NEAR(points[0].y, 0.0, 1e-6);
  EXPECT_NEAR(points[1].x, 10.0, 1e-6);
  EXPECT_NEAR(points[1].y, 0.0, 1e-6);
  EXPECT_NEAR(points[2].x, 10.0, 1e-6);
  EXPECT_NEAR(points[2].y, 5.0, 1e-6);
}

void SimWorld_FromJointsThrowsWhenJointIsOutOfBounds() {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{(SimWorld::widthOfWorld / 2.F) + 1.F, 0.F}}};

  EXPECT_THROW(SegmentedPath::fromJoints(p), std::runtime_error);
}

void SimGame_LoadMapInitialSnapshotAndState() {
  SimGame game;
  (void)game.loadMap();

  const auto snap = snapshot(game);
  ASSERT_EQ(snap.entities.size(), 0U);
  ASSERT_EQ(snap.path_points.size(), 1U);
  EXPECT_EQ(snap.path_points[0].joints.front().pos.x, 0.F);
  EXPECT_EQ(snap.path_points[0].joints.front().pos.y, 0.F);
  EXPECT_GT(snap.path_points[0].joints.size(), 2U);

  const auto delta = extractGameDelta(game);
  EXPECT_EQ(delta.currentWave, 0U);
  EXPECT_EQ(delta.waveTick, WaveTick{});
  EXPECT_EQ(delta.lives, 20);
  EXPECT_EQ(delta.resources, 100);
  EXPECT_EQ(delta.phase, std::string_view{"build"});
  EXPECT_TRUE(delta.upserts.empty());
  EXPECT_TRUE(delta.erased.empty());
}

void SimGame_HandleUiActionStartWaveTransitionsToWavePhase() {
  SimGame game;
  (void)game.loadMap();

  (void)game.handleUiAction(
      UiActionInput{.seq = 1, .action = "start_wave", .fields = {}});

  const auto before = extractGameDelta(game);
  EXPECT_EQ(before.phase, std::string_view{"build"});

  (void)game.next();

  const auto delta = extractGameDelta(game);
  EXPECT_EQ(delta.phase, std::string_view{"wave"});
  EXPECT_EQ(delta.waveTick, WaveTick{1});
}

void SimGame_HandleUiCanvasSpawnsDefenderButKeepsBuildPhase() {
  SimGame game;
  (void)game.loadMap();
  const auto before = extractGameDelta(game);

  (void)game.handleUiCanvas(UiCanvasInput{.seq = 1,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::left,
      .buttons = 1,
      .x = 300.F,
      .y = 100.F,
      .canvasX = 100.F,
      .canvasY = 200.F,
      .command = "spawn",
      .parameters = {"DefenderAoeBasic"}});

  const auto pending = extractGameDelta(game);
  EXPECT_TRUE(pending.upserts.empty());
  EXPECT_FALSE(pending.spawnAllowed.has_value());

  (void)game.next();

  const auto after = extractGameDelta(game);
  EXPECT_EQ(before.phase, std::string_view{"build"});
  EXPECT_EQ(after.phase, std::string_view{"build"});
  ASSERT_TRUE(after.spawnAllowed.has_value());
  EXPECT_TRUE(*after.spawnAllowed);
  ASSERT_EQ(after.upserts.size(), 1U);
  EXPECT_NEAR(after.upserts[0].second.x, 300.0, 1e-6);
  EXPECT_NEAR(after.upserts[0].second.y, 100.0, 1e-6);
  EXPECT_TRUE(after.erased.empty());
}

void SimGame_HandleUiCanvasRightClickSpawnPlacesDefender() {
  SimGame game;
  (void)game.loadMap();

  (void)game.handleUiCanvas(UiCanvasInput{.seq = 1,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::right,
      .buttons = 2,
      .x = 300.F,
      .y = 100.F,
      .canvasX = 120.F,
      .canvasY = 210.F,
      .command = "spawn",
      .parameters = {"DefenderAoeBasic"}});

  const auto pending = extractGameDelta(game);
  EXPECT_TRUE(pending.upserts.empty());

  (void)game.next();

  const auto after = extractGameDelta(game);
  EXPECT_EQ(after.phase, std::string_view{"build"});
  ASSERT_TRUE(after.spawnAllowed.has_value());
  EXPECT_TRUE(*after.spawnAllowed);
  ASSERT_EQ(after.upserts.size(), 1U);
  EXPECT_NEAR(after.upserts[0].second.x, 300.0, 1e-6);
  EXPECT_NEAR(after.upserts[0].second.y, 100.0, 1e-6);
  EXPECT_TRUE(after.erased.empty());
}

void SimGame_HandleUiCanvasSpawnsShooterDefender() {
  SimGame game;
  (void)game.loadMap();
  const auto before = extractGameDelta(game);

  (void)game.handleUiCanvas(UiCanvasInput{.seq = 1,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::right,
      .buttons = 2,
      .x = 300.F,
      .y = 100.F,
      .canvasX = 100.F,
      .canvasY = 200.F,
      .command = "spawn",
      .parameters = {"DefenderShooterBasic"}});

  const auto pending = extractGameDelta(game);
  EXPECT_TRUE(pending.upserts.empty());
  EXPECT_FALSE(pending.spawnAllowed.has_value());

  (void)game.next();

  const auto after = extractGameDelta(game);
  EXPECT_EQ(before.phase, std::string_view{"build"});
  EXPECT_EQ(after.phase, std::string_view{"build"});
  ASSERT_TRUE(after.spawnAllowed.has_value());
  EXPECT_TRUE(*after.spawnAllowed);
  ASSERT_EQ(after.upserts.size(), 1U);
  EXPECT_NEAR(after.upserts[0].second.x, 300.0, 1e-6);
  EXPECT_NEAR(after.upserts[0].second.y, 100.0, 1e-6);
  EXPECT_TRUE(after.erased.empty());
}

void SimGame_HandleUiCanvasPlacingIntentRejectsPathOverlapOnNextTick() {
  SimGame game;
  (void)game.loadMap();

  (void)game.handleUiCanvas(UiCanvasInput{.seq = 7,
      .event = UiCanvasEvent::dragmove,
      .button = UiMouseButton::left,
      .buttons = 1,
      .x = 10.F,
      .y = 20.F,
      .canvasX = 100.F,
      .canvasY = 200.F,
      .command = "placing",
      .parameters = {"DefenderAoeBasic"}});

  const auto pending = extractGameDelta(game);
  EXPECT_FALSE(pending.placementAllowed.has_value());

  (void)game.next();

  const auto delta = extractGameDelta(game);
  ASSERT_TRUE(delta.placementAllowed.has_value());
  EXPECT_FALSE(*delta.placementAllowed);
}

void SimGame_HandleUiCanvasRejectsBlockedDefenderSpawnOnNextTick() {
  SimGame game;
  (void)game.loadMap();

  (void)game.handleUiCanvas(UiCanvasInput{.seq = 1,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::right,
      .buttons = 2,
      .x = 10.F,
      .y = 20.F,
      .canvasX = 100.F,
      .canvasY = 200.F,
      .command = "spawn",
      .parameters = {"DefenderAoeBasic"}});

  (void)game.next();

  const auto delta = extractGameDelta(game);
  EXPECT_TRUE(delta.upserts.empty());
  ASSERT_TRUE(delta.spawnAllowed.has_value());
  EXPECT_FALSE(*delta.spawnAllowed);
}

void SimGame_StartWaveSpawnsFirstEnemyOnFirstStep() {
  SimGame game;
  (void)game.loadMap();
  (void)game.start_wave();

  (void)game.next();

  const auto snap = snapshot(game);
  EXPECT_EQ(*game.tick(), 1U);
  ASSERT_EQ(snap.entities.size(), 1U);
  EXPECT_NEAR(snap.entities[0].pos.x, 0.0, 1e-6);
  EXPECT_NEAR(snap.entities[0].pos.y, 0.0, 1e-6);

  const auto delta = extractGameDelta(game);
  EXPECT_EQ(delta.currentWave, 0U);
  EXPECT_EQ(delta.waveTick, WaveTick{1});
  EXPECT_EQ(delta.lives, 20);
  EXPECT_EQ(delta.resources, 100);
  EXPECT_EQ(delta.phase, std::string_view{"wave"});
  EXPECT_TRUE(delta.upserts.empty());
  EXPECT_TRUE(delta.erased.empty());
}

void SimGame_ExtractDeltaConsumesWorldUpdatesButNotState() {
  SimGame game;
  (void)game.loadMap();
  (void)game.start_wave();

  (void)game.next();
  (void)game.tick();
  (void)game.next();
  (void)game.tick();

  const auto delta = extractGameDelta(game);
  EXPECT_TRUE(!delta.upserts.empty());
  EXPECT_EQ(delta.erased.size(), 0U);
  EXPECT_EQ(delta.waveTick, WaveTick{2});
  EXPECT_EQ(delta.phase, std::string_view{"wave"});

  const auto* pos = findPosition(delta.upserts, delta.upserts.front().first);
  ASSERT_TRUE(pos != nullptr);
  EXPECT_GT(pos->x, 0.F);

  const auto empty_world_delta = extractGameDelta(game);
  EXPECT_TRUE(empty_world_delta.upserts.empty());
  EXPECT_TRUE(empty_world_delta.erased.empty());
  EXPECT_EQ(empty_world_delta.waveTick, WaveTick{2});
  EXPECT_EQ(empty_world_delta.phase, std::string_view{"wave"});
}

void SimGame_ExtractFullIncludesPathsAndState() {
  SimGame game;
  (void)game.loadMap();

  size_t path_points = 0;
  size_t upserts = 0;
  size_t erased = 0;
  size_t currentWave = 99;
  WaveTick waveTick{99};
  int lives = -1;
  int resources = -1;
  std::string_view phase = "unknown";
  UiState uiState;

  (void)game.extractFull(
      [&path_points](PathId, const Position&) { ++path_points; },
      [&upserts](SimWorld::EntityId, const Position&, const Appearance&,
          const VisualEffects&) { ++upserts; },
      [&erased](SimWorld::EntityId) { ++erased; },
      [&currentWave, &waveTick, &lives, &resources, &phase,
          &uiState](size_t wave, WaveTick tick, int newLives, int newResources,
          std::string_view newPhase, const UiState& newUiState) {
        currentWave = wave;
        waveTick = tick;
        lives = newLives;
        resources = newResources;
        phase = newPhase;
        uiState = newUiState;
      });

  EXPECT_GT(path_points, 0U);
  EXPECT_EQ(upserts, 0U);
  EXPECT_EQ(erased, 0U);
  EXPECT_EQ(currentWave, 0U);
  EXPECT_EQ(waveTick, WaveTick{0});
  EXPECT_EQ(lives, 20);
  EXPECT_EQ(resources, 100);
  EXPECT_EQ(phase, std::string_view{"build"});
  EXPECT_FALSE(uiState.defenderSelected);
}

void SimJson_ParseUiCanvasMessage() {
  const auto msg = parseSimClientMessageRoot(R"({
    "type": "ui_canvas",
    "seq": 42,
    "event": "dragmove",
    "button": "right",
    "buttons": 2,
    "x": 10.5,
    "y": -3.25,
    "canvasX": 100.0,
    "canvasY": 200.5,
    "shift": true,
    "ctrl": false,
    "alt": true,
    "meta": false
  })");

  ASSERT_TRUE(msg.has_value());
  EXPECT_EQ(classifySimClientMessage(*msg), SimClientMessageKind::ui_canvas);

  const auto input = parseUiCanvasMessage(*msg);
  ASSERT_TRUE(input.has_value());
  EXPECT_EQ(input->seq, 42U);
  EXPECT_EQ(input->event, UiCanvasEvent::dragmove);
  EXPECT_EQ(input->button, UiMouseButton::right);
  EXPECT_EQ(input->buttons, 2U);
  EXPECT_NEAR(input->x, 10.5, 1e-6);
  EXPECT_NEAR(input->y, -3.25, 1e-6);
  EXPECT_NEAR(input->canvasX, 100.0, 1e-6);
  EXPECT_NEAR(input->canvasY, 200.5, 1e-6);
  EXPECT_TRUE(input->shift);
  EXPECT_FALSE(input->ctrl);
  EXPECT_TRUE(input->alt);
  EXPECT_FALSE(input->meta);
}

void SimJson_ParseUiActionMessageFields() {
  const auto input = parseUiActionMessage(
      R"({"type":"ui_action","seq":7,"action":"start_wave","fields":{"defender/kind":"ice","note":"line\nbreak"}})");

  ASSERT_TRUE(input.has_value());
  EXPECT_EQ(input->seq, 7U);
  EXPECT_EQ(input->action, "start_wave");
  ASSERT_EQ(input->fields.size(), 2U);
  const auto defender_kind =
      std::ranges::find(input->fields, "defender/kind", &UiActionField::key);
  ASSERT_TRUE(defender_kind != input->fields.end());
  EXPECT_EQ(defender_kind->value, "ice");

  const auto note =
      std::ranges::find(input->fields, "note", &UiActionField::key);
  ASSERT_TRUE(note != input->fields.end());
  EXPECT_EQ(note->value, std::string("line\nbreak"));
}

void SimJson_BuildHelloAckJson() {
  EXPECT_EQ(buildSimHelloAckJson(),
      std::string(R"({"type":"hello_ack","message":"connected"})"));
}

void SimJson_BuildWorldDeltaJsonShapeAndFormatting() {
  SimGame game;
  (void)game.loadMap();
  (void)game.handleUiCanvas(UiCanvasInput{.seq = 1,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::left,
      .buttons = 1,
      .x = 300.F,
      .y = 100.F,
      .canvasX = 100.F,
      .canvasY = 200.F,
      .command = "spawn",
      .parameters = {"DefenderAoeBasic"}});
  (void)game.next();

  SimGameStateJson state;
  (void)buildSimGameStateJson(state, game);
  EXPECT_TRUE(state.body.contains(R"("x":300.0)"));
  EXPECT_TRUE(state.body.contains(R"("radius":30.000)"));
  EXPECT_TRUE(state.body.contains(R"("vfx")"));
  EXPECT_TRUE(state.body.contains(R"("flashExpiryMs":250)"));
  EXPECT_TRUE(state.body.contains(R"("uiState")"));
  EXPECT_TRUE(state.body.contains(R"("spawnAllowed":true)"));
  EXPECT_FALSE(state.body.contains(R"("modified")"));
  EXPECT_FALSE(state.body.contains(R"("flash_expiry")"));
  EXPECT_FALSE(state.body.contains(R"("glow")"));

  json_value_view root;
  ASSERT_TRUE(parse_json(state.body, root));
  const auto obj = root.as_object();
  ASSERT_TRUE(obj);
  const auto tick_value = obj.get_number<uint32_t>("tick");
  const auto wave_tick_value = obj.get_number<uint32_t>("waveTick");
  const auto phase_value = obj.get_string_view_if_plain("phase");
  ASSERT_TRUE(tick_value.has_value());
  ASSERT_TRUE(wave_tick_value.has_value());
  ASSERT_TRUE(phase_value.has_value());
  EXPECT_EQ(*tick_value, 0U);
  EXPECT_EQ(*wave_tick_value, 0U);
  EXPECT_EQ(*phase_value, std::string_view{"build"});

  const auto upserts = obj.get_array("upserts");
  ASSERT_TRUE(upserts);
  size_t count = 0;
  for (const auto item : upserts) {
    const auto entry = item.as_object();
    ASSERT_TRUE(entry);
    const auto pos = entry.get_object("pos");
    ASSERT_TRUE(pos);
    const auto x = pos.get_number<float>("x");
    ASSERT_TRUE(x.has_value());
    EXPECT_NEAR(*x, 300.0, 1e-6);

    const auto app = entry.get_object("app");
    ASSERT_TRUE(app);
    ASSERT_TRUE(app.get_number<uint32_t>("glyph").has_value());
    ASSERT_TRUE(app.get_number<float>("radius").has_value());
    ASSERT_TRUE(!app.get_number<uint32_t>("modified").has_value());

    const auto vfx = entry.get_object("vfx");
    ASSERT_TRUE(vfx);
    ASSERT_TRUE(vfx.get_number<uint32_t>("selection").has_value());
    ASSERT_TRUE(vfx.get_number<float>("rangeRadius").has_value());
    ASSERT_TRUE(vfx.get_number<uint32_t>("range").has_value());
    ASSERT_TRUE(vfx.get_number<uint32_t>("flash").has_value());
    ASSERT_TRUE(vfx.get_number<uint32_t>("flashExpiryMs").has_value());
    ++count;
  }
  EXPECT_EQ(count, 1U);
}

void SimJson_BuildWorldDeltaIncludesFlashVisualEffects() {
  SimGame game;
  (void)game.loadMap();
  (void)game.handleUiCanvas(UiCanvasInput{.seq = 1,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::left,
      .buttons = 1,
      .x = 300.F,
      .y = 100.F,
      .canvasX = 100.F,
      .canvasY = 200.F,
      .command = "spawn",
      .parameters = {"DefenderAoeBasic"}});
  (void)game.next();

  SimGameStateJson initial_state;
  (void)buildSimGameStateJson(initial_state, game);
  (void)game.tick();

  (void)game.handleUiCanvas(UiCanvasInput{.seq = 2,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::right,
      .buttons = 2,
      .x = 300.F,
      .y = 100.F,
      .canvasX = 100.F,
      .canvasY = 200.F,
      .command{},
      .parameters{}});
  (void)game.next();

  SimGameStateJson state;
  (void)buildSimGameStateJson(state, game);

  json_value_view root;
  ASSERT_TRUE(parse_json(state.body, root));
  const auto obj = root.as_object();
  ASSERT_TRUE(obj);

  const auto upserts = obj.get_array("upserts");
  ASSERT_TRUE(upserts);
  size_t count = 0;
  for (const auto item : upserts) {
    const auto entry = item.as_object();
    ASSERT_TRUE(entry);
    const auto pos = entry.get_object("pos");
    ASSERT_TRUE(pos);
    const auto x = pos.get_number<float>("x");
    ASSERT_TRUE(x.has_value());
    EXPECT_NEAR(*x, 300.0, 1e-6);

    const auto vfx = entry.get_object("vfx");
    ASSERT_TRUE(vfx);
    const auto flash = vfx.get_number<uint32_t>("flash");
    const auto flash_expiry_ms = vfx.get_number<uint32_t>("flashExpiryMs");
    ASSERT_TRUE(flash.has_value());
    ASSERT_TRUE(flash_expiry_ms.has_value());
    EXPECT_EQ(*flash, 0xFF7F7FAFU);
    EXPECT_EQ(*flash_expiry_ms, 250U);
    ++count;
  }
  EXPECT_EQ(count, 1U);
}

void SimJson_FlashExpiryDelayMsUsesCurrentTickRelativeTiming() {
  VisualEffects fx{
      .modified = WorldTick{12},
      .selectionColor = 0,
      .rangeRadius = 0.F,
      .rangeColor = 0,
      .flashColor = 0xFF0000FF,
      .flashExpiry = WorldTick{15},
  };

  EXPECT_EQ(flashExpiryDelayMs(fx, WorldTick{10}), 250U);
  EXPECT_EQ(flashExpiryDelayMs(fx, WorldTick{15}), 0U);
  EXPECT_EQ(flashExpiryDelayMs(fx, WorldTick{16}), 0U);

  fx.flashColor = 0;
  EXPECT_EQ(flashExpiryDelayMs(fx, WorldTick{10}), 0U);
}

void SimJson_BuildWorldSnapshotJsonShape() {
  SimGame game;
  (void)game.loadMap();

  SimGameStateJson state;
  (void)buildSimGameStateJson(state, game, update_strategy::full);
  EXPECT_TRUE(state.body.contains(R"("type":"world_snapshot")"));
  EXPECT_TRUE(state.body.contains(R"("x":0.0)"));

  json_value_view root;
  ASSERT_TRUE(parse_json(state.body, root));
  const auto obj = root.as_object();
  ASSERT_TRUE(obj);
  const auto root_type = obj.get_string_view_if_plain("type");
  ASSERT_TRUE(root_type.has_value());
  EXPECT_EQ(*root_type, std::string_view{"world_snapshot"});

  const auto map_design = obj.get_object("mapDesign");
  ASSERT_TRUE(map_design);
  const auto paths = map_design.get_array("paths");
  ASSERT_TRUE(paths);
  size_t path_points = 0;
  for (const auto point : paths) {
    const auto entry = point.as_object();
    ASSERT_TRUE(entry);
    ASSERT_TRUE(entry.get_number<float>("x").has_value());
    ASSERT_TRUE(entry.get_number<float>("y").has_value());
    ++path_points;
  }
  EXPECT_GT(path_points, 0U);

  const auto delta = obj.get_object("delta");
  ASSERT_TRUE(delta);
  const auto delta_type = delta.get_string_view_if_plain("type");
  const auto delta_phase = delta.get_string_view_if_plain("phase");
  ASSERT_TRUE(delta_type.has_value());
  EXPECT_EQ(*delta_type, std::string_view{"world_delta"});
  ASSERT_TRUE(delta_phase.has_value());
  EXPECT_EQ(*delta_phase, std::string_view{"build"});
}

// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(SimWorld_SpawnAndSnapshot, SimWorld_NextMovesInvaderAlpha,
    SimWorld_ExtractUpdatedEntitiesReportsMovedInvaderOncePerExtraction,
    SimWorld_DefenderInRangeFlashesItselfAndInvader,
    SimWorld_SnapshotSinceTracksChanges,
    SimWorld_DefenderDoesNotAppearAsChangedAfterTick, BakePath_TwoJoints,
    BakePath_ThreeJoints, BakePath_Degenerate, PathPosition_Endpoints,
    PathPosition_Midpoint, PathPosition_CrossingSegmentBoundaryEmitsJoint,
    SimWorld_EnemyAdvancesOnTick, SimWorld_ResolveEscapeesVisitsEscapedEnemy,
    SimWorld_ResolveEscapeesCanLeaveEnemyAlive, SimWorld_GetPathOutOfRange,
    SimWorld_ObtainPathIncludesTerminalJoint,
    SimWorld_FromJointsThrowsWhenJointIsOutOfBounds,
    SimGame_LoadMapInitialSnapshotAndState,
    SimGame_HandleUiActionStartWaveTransitionsToWavePhase,
    SimGame_HandleUiCanvasSpawnsDefenderButKeepsBuildPhase,
    SimGame_HandleUiCanvasRightClickSpawnPlacesDefender,
    SimGame_HandleUiCanvasSpawnsShooterDefender,
    SimGame_HandleUiCanvasPlacingIntentRejectsPathOverlapOnNextTick,
    SimGame_HandleUiCanvasRejectsBlockedDefenderSpawnOnNextTick,
    SimGame_StartWaveSpawnsFirstEnemyOnFirstStep,
    SimGame_ExtractDeltaConsumesWorldUpdatesButNotState,
    SimGame_ExtractFullIncludesPathsAndState, SimJson_ParseUiCanvasMessage,
    SimJson_ParseUiActionMessageFields, SimJson_BuildHelloAckJson,
    SimJson_BuildWorldDeltaJsonShapeAndFormatting,
    SimJson_BuildWorldDeltaIncludesFlashVisualEffects,
    SimJson_FlashExpiryDelayMsUsesCurrentTickRelativeTiming,
    SimJson_BuildWorldSnapshotJsonShape)
