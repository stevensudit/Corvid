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
#include <cstdlib>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "corvid/sim/sim_game.h"
#include "corvid/sim/sim_json_parse.h"
#include "corvid/sim/sim_json_wire.h"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::sim;

namespace {

// NOLINTBEGIN(bugprone-throwing-static-initialization)
[[maybe_unused]] const bool kSuppressMapEntityCsv = [] {
  return setenv("CORVID_SUPPRESS_MAP_ENTITY_CSV", "1", 1) == 0;
}();
// NOLINTEND(bugprone-throwing-static-initialization)

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
  std::vector<TransientExplosion> transientExplosions;
  std::vector<TransientBeam> transientBeams;
  size_t currentWave{};
  WaveTick waveTick{};
  uint16_t lives{};
  uint16_t resources{};
  std::string_view phase;
  std::optional<bool> placementAllowed;
  std::optional<bool> spawnAllowed;
  std::optional<Position> selectedDefender;
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
          const VisualEffects&, const Health&) {
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
      [&delta](SimWorld::EntityId id, const Position& pos,
          const Appearance& app, const VisualEffects& fx, const Health&) {
        delta.upserts.push_back({id, pos, app, fx});
      },
      [&delta](SimWorld::EntityId id) { delta.erased.push_back(id); });
  return delta;
}

[[nodiscard]] std::vector<TransientExplosion> extractTransientExplosions(
    SimWorld& world) {
  std::vector<TransientExplosion> explosions;
  (void)world.extractTransientExplosions(
      [&explosions](const TransientExplosion& transient) {
        explosions.push_back(transient);
      });
  return explosions;
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
          const VisualEffects&, const Health&) {
        snap.entities.push_back({id, pos});
      },
      [](SimWorld::EntityId) {}, [](const TransientExplosion&) {},
      [](const TransientBeam&) {},
      [](size_t, WaveTick, int, int, std::string_view, const UiState&) {});

  snap.path_points = std::move(pathsById);
  return snap;
}

[[nodiscard]] GameDelta extractGameDelta(SimGame& game) {
  GameDelta delta;
  (void)game.extractDelta(
      [&delta](SimWorld::EntityId id, const Position& pos, const Appearance&,
          const VisualEffects&, const Health&) {
        delta.upserts.emplace_back(id, pos);
      },
      [&delta](SimWorld::EntityId id) { delta.erased.push_back(id); },
      [&delta](const TransientExplosion& transient) {
        delta.transientExplosions.push_back(transient);
      },
      [&delta](const TransientBeam& transient) {
        delta.transientBeams.push_back(transient);
      },
      [&delta](size_t currentWave, WaveTick waveTick, uint16_t lives,
          uint16_t resources, std::string_view phase, const UiState& uiState) {
        delta.currentWave = currentWave;
        delta.waveTick = waveTick;
        delta.lives = lives;
        delta.resources = resources;
        delta.phase = phase;
        delta.placementAllowed = uiState.placementAllowed;
        delta.spawnAllowed = uiState.spawnAllowed;
        delta.selectedDefender = uiState.selectedDefender;
        delta.defenderSummary = uiState.defenderSummary;
      });
  return delta;
}

[[nodiscard]] bool containsId(const auto& entries, SimWorld::EntityId id) {
  return std::ranges::any_of(entries, [id](const auto& entry) {
    if constexpr (requires { entry.id; })
      return entry.id == id;
    else if constexpr (requires { entry.first; })
      return entry.first == id;
    else
      return entry == id;
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

// Shared template store for all test helpers. Populated lazily, once per
// label, across all test cases.
EntityTemplateStore testStore;

// Set the world's template store and ensure the world points at `testStore`.
void ensureTestStore(SimWorld& w) { w.setEntityTemplateStore(&testStore); }

// Test helpers that register the entity type on demand (idempotent) and spawn
// with placement applied.
[[nodiscard]] SimWorld::Handle
spawnInvaderAlpha(SimWorld& w, PathId pid, float progress = 0.F) {
  ensureTestStore(w);
  if (!testStore.templates.contains("InvaderAlphaBasic")) {
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
    (void)testStore.registerEntity("InvaderAlphaBasic", tpl);
  }
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
  ensureTestStore(w);
  if (!testStore.templates.contains("DefenderAoeBasic")) {
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
    (void)testStore.registerEntity("DefenderAoeBasic", tpl);
  }
  auto h = w.spawnEntity("DefenderAoeBasic");
  if (!h) return h;
  if (auto* pos = w.try_get_component<Position>(h.id())) *pos = spawn_pos;
  return h;
}

[[nodiscard]] SimWorld::Handle
spawnDefenderShooter(SimWorld& w, Position spawn_pos) {
  ensureTestStore(w);
  if (!testStore.templates.contains("DefenderShooterBasic")) {
    WorldScene::megatuple_t tpl{};
    std::get<std::optional<Position>>(tpl) = Position{};
    std::get<std::optional<Appearance>>(tpl) = Appearance{.glyph = U'S',
        .radius = 25.F,
        .fgColor = 0xFFFFFFFF,
        .bgColor = 0x7FFF7F3F,
        .attackRadius = 150.F};
    std::get<std::optional<VisualEffects>>(tpl) = VisualEffects{};
    std::get<std::optional<Defender>>(tpl) = Defender{.hitCircleRadius = 25.F,
        .attackRadius = 150.F,
        .rangeColor = 0xFF00FF00,
        .attackDamage = 15.F,
        .cooldown = WorldTick{30},
        .nextAttack = WorldTick{0}};
    std::get<std::optional<DefenderStats>>(tpl) = DefenderStats{};
    std::get<std::optional<Health>>(tpl) =
        Health{.currentHealth = 80.F, .maxHealth = 80.F, .regen = 0.F};
    std::get<std::optional<DefenderShooter>>(tpl) = DefenderShooter{
        .bulletTemplate = DefenderBullet{.expiry = WorldTick{60},
            .hitCircleRadius = 8.F,
            .speed = 200.F,
            .directDamage = 15.F,
            .projectileType = 1},
        .fireRate = 0.033F};
    (void)testStore.registerEntity("DefenderShooterBasic", tpl);
  }
  auto h = w.spawnEntity("DefenderShooterBasic");
  if (!h) return h;
  if (auto* pos = w.try_get_component<Position>(h.id())) *pos = spawn_pos;
  return h;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region World_SpawnAndSnapshot

TEST_CASE("SpawnAndSnapshot", "[SimWorld]") {
  SimWorld w;
  CHECK(w.size() == 0U);

  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{200.F, 0.F}}};
  const auto pid = w.addPath(p);

  const auto invader = spawnInvaderAlpha(w, pid);
  const auto defender = spawnDefenderAoe(w, Position{30.F, 40.F});

  CHECK(w.size() == 2U);

  const auto all = snapshot(w);
  REQUIRE(all.size() == 2U);
  CHECK(filterSnapshot(all, std::vector<SimWorld::EntityId>{invader.id()})
            .size() == 1U);
  CHECK(filterSnapshot(all,
            std::vector<SimWorld::EntityId>{invader.id(), defender.id()})
            .size() == 2U);
}

#pragma endregion
#pragma region World_NextMovesInvaderAlpha

TEST_CASE("NextMovesInvaderAlpha", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{200.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto invader = spawnInvaderAlpha(w, pid);

  (void)w.next();

  const auto snaps = snapshot(w);
  CHECK(*w.tick() == 1U);
  REQUIRE(snaps.size() == 1U);
  CHECK(snaps[0].id == invader.id());
  CHECK(std::abs((snaps[0].pos.x) - (50.0)) <= 1e-6);
  CHECK(std::abs((snaps[0].pos.y) - (0.0)) <= 1e-6);
}

#pragma endregion
#pragma region World_ExtractUpdatedEntitiesReportsMovedInvaderOncePerExtraction

TEST_CASE(
    "SimWorld_ExtractUpdatedEntitiesReportsMovedInvaderOncePerExtraction",
    "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{300.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto invader = spawnInvaderAlpha(w, pid);

  (void)w.next();
  (void)w.tick();
  const auto delta = extractWorldDelta(w);

  CHECK(containsId(delta.upserts, invader.id()));
  CHECK(delta.erased.empty());

  const auto* pos = findPosition(delta.upserts, invader.id());
  REQUIRE(pos != nullptr);
  CHECK(std::abs((pos->x) - (50.0)) <= 1e-6);
  CHECK(std::abs((pos->y) - (0.0)) <= 1e-6);

  const auto empty_delta = extractWorldDelta(w);
  CHECK(empty_delta.upserts.empty());
  CHECK(empty_delta.erased.empty());
}

#pragma endregion
#pragma region World_DefenderInRangeFlashesItselfAndInvader

TEST_CASE("DefenderInRangeFlashesItselfAndInvader", "[SimWorld]") {
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
  REQUIRE(delta.upserts.size() == 2U);
  CHECK(containsId(delta.upserts, defender.id()));
  CHECK(containsId(delta.upserts, invader.id()));

  const auto defender_it =
      std::ranges::find(delta.upserts, defender.id(), &WorldDelta::Upsert::id);
  REQUIRE(defender_it != delta.upserts.end());
  CHECK(defender_it->fx.flashColor == 0xFFFFFFFFU);
  CHECK(defender_it->fx.flashExpiry == WorldTick{6});

  const auto invader_it =
      std::ranges::find(delta.upserts, invader.id(), &WorldDelta::Upsert::id);
  REQUIRE(invader_it != delta.upserts.end());
  CHECK(invader_it->fx.flashColor == 0xFF7F7FFFU);
  CHECK(invader_it->fx.flashExpiry == WorldTick{6});
  CHECK(std::abs((invader_it->pos.x) - (50.0)) <= 1e-6);
}

#pragma endregion
#pragma region World_DefenderAoeAttackEmitsPulseExplosion

TEST_CASE("DefenderAoeAttackEmitsPulseExplosion", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{500.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto defender = spawnDefenderAoe(w, {0.F, 0.F});
  (void)spawnInvaderAlpha(w, pid);

  (void)extractWorldDelta(w);
  (void)w.tick();
  (void)w.next();

  const auto explosions = extractTransientExplosions(w);
  REQUIRE(explosions.size() == 1U);
  CHECK(std::abs((explosions[0].circle.x) - (0.0)) <= 1e-6);
  CHECK(std::abs((explosions[0].circle.y) - (0.0)) <= 1e-6);
  CHECK(explosions[0].expiry == WorldTick{2});
  CHECK(explosions[0].primaryColor == 0xFFFF0030U);
  CHECK(explosions[0].secondaryColor == 0xFFFF0010U);
  CHECK(std::abs((explosions[0].circle.radius) - (100.0)) <= 1e-6);

  const auto drained = extractTransientExplosions(w);
  CHECK(drained.empty());

  CHECK(w.try_get_component<Position>(defender.id()) != nullptr);
}

#pragma endregion
#pragma region World_DefenderShooterSpawnsVisibleBullet

TEST_CASE("DefenderShooterSpawnsVisibleBullet", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{500.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto defender = spawnDefenderShooter(w, {0.F, 0.F});
  (void)spawnInvaderAlpha(w, pid);

  (void)extractWorldDelta(w);
  (void)w.next();

  const auto delta = extractWorldDelta(w);
  REQUIRE(delta.upserts.size() == 1U);
  const auto bulletIt = std::ranges::find_if(delta.upserts,
      [&](const WorldDelta::Upsert& upsert) {
        return upsert.id != defender.id() && upsert.app.glyph == U'*';
      });
  REQUIRE(bulletIt != delta.upserts.end());
  CHECK(bulletIt->app.glyph == U'*');
  CHECK(std::abs((bulletIt->app.radius) - (8.0)) <= 1e-6);
  CHECK(std::abs((bulletIt->pos.x) - (0.0)) <= 1e-6);
  CHECK(std::abs((bulletIt->pos.y) - (0.0)) <= 1e-6);
}

#pragma endregion
#pragma region World_DefenderShooterBulletHitsInvaderOnNextStep

TEST_CASE("DefenderShooterBulletHitsInvaderOnNextStep", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{500.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto defender = spawnDefenderShooter(w, {0.F, 0.F});
  const auto invader = spawnInvaderAlpha(w, pid);

  (void)extractWorldDelta(w);
  (void)w.next();
  const auto bulletSpawnDelta = extractWorldDelta(w);
  const auto bulletIt = std::ranges::find_if(bulletSpawnDelta.upserts,
      [&](const WorldDelta::Upsert& upsert) {
        return upsert.id != defender.id() && upsert.id != invader.id();
      });
  REQUIRE(bulletIt != bulletSpawnDelta.upserts.end());
  const auto bulletId = bulletIt->id;
  (void)w.tick();

  (void)w.next();

  const auto delta = extractWorldDelta(w);
  CHECK(containsId(delta.upserts, invader.id()));
  CHECK(containsId(delta.erased, bulletId));

  const auto* hp = w.try_get_component<Health>(invader.id());
  REQUIRE(hp != nullptr);
  CHECK(std::abs((hp->currentHealth) - (85.0)) <= 1e-6);

  const auto* stats = w.try_get_component<DefenderStats>(defender.id());
  REQUIRE(stats != nullptr);
  CHECK(std::abs((stats->totalDamageDealt) - (15.0)) <= 1e-6);

  const auto explosions = extractTransientExplosions(w);
  REQUIRE(explosions.size() == 1U);
  CHECK(std::abs((explosions[0].circle.x) - (100.0)) <= 1e-6);
  CHECK(std::abs((explosions[0].circle.y) - (0.0)) <= 1e-6);
}

#pragma endregion
#pragma region World_DefenderShooterBulletHitsFirstInvaderAlongPath

TEST_CASE("DefenderShooterBulletHitsFirstInvaderAlongPath", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{500.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto defender = spawnDefenderShooter(w, {0.F, 0.F});
  const auto nearInvader = spawnInvaderAlpha(w, pid, 0.F);
  const auto farInvader = spawnInvaderAlpha(w, pid, 100.F);

  (void)extractWorldDelta(w);
  (void)w.next();
  (void)extractWorldDelta(w);
  (void)w.tick();

  (void)w.next();

  const auto* nearHp = w.try_get_component<Health>(nearInvader.id());
  const auto* farHp = w.try_get_component<Health>(farInvader.id());
  REQUIRE(nearHp != nullptr);
  REQUIRE(farHp != nullptr);
  CHECK(std::abs((nearHp->currentHealth) - (85.0)) <= 1e-6);
  CHECK(std::abs((farHp->currentHealth) - (100.0)) <= 1e-6);

  const auto* stats = w.try_get_component<DefenderStats>(defender.id());
  REQUIRE(stats != nullptr);
  CHECK(std::abs((stats->totalDamageDealt) - (15.0)) <= 1e-6);
}

#pragma endregion
#pragma region World_ExplosiveBulletDetonatesOnExpiry

TEST_CASE("ExplosiveBulletDetonatesOnExpiry", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{500.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto defender = spawnDefenderShooter(w, {0.F, 0.F});
  const auto invader = spawnInvaderAlpha(w, pid, 50.F);
  auto* shooter = w.try_get_component<DefenderShooter>(defender.id());
  REQUIRE(shooter != nullptr);
  shooter->bulletTemplate.splashRadius = 250.F;
  shooter->bulletTemplate.expiry = WorldTick{1};

  (void)extractWorldDelta(w);
  (void)w.next();
  const auto bulletSpawnDelta = extractWorldDelta(w);
  const auto bulletIt = std::ranges::find_if(bulletSpawnDelta.upserts,
      [&](const WorldDelta::Upsert& upsert) {
        return upsert.id != defender.id() && upsert.id != invader.id();
      });
  REQUIRE(bulletIt != bulletSpawnDelta.upserts.end());
  const auto bulletId = bulletIt->id;
  (void)w.tick();
  (void)w.next();

  const auto delta = extractWorldDelta(w);
  CHECK(containsId(delta.erased, bulletId));

  const auto* hp = w.try_get_component<Health>(invader.id());
  REQUIRE(hp != nullptr);
  CHECK(std::abs((hp->currentHealth) - (85.0)) <= 1e-6);

  const auto* stats = w.try_get_component<DefenderStats>(defender.id());
  REQUIRE(stats != nullptr);
  CHECK(std::abs((stats->totalDamageDealt) - (15.0)) <= 1e-6);

  const auto explosions = extractTransientExplosions(w);
  REQUIRE(explosions.size() == 1U);
  CHECK(std::abs((explosions[0].circle.x) - (200.0)) <= 1e-6);
  CHECK(std::abs((explosions[0].circle.y) - (0.0)) <= 1e-6);
  CHECK(std::abs((explosions[0].circle.radius) - (250.0)) <= 1e-6);
}

#pragma endregion
#pragma region World_SnapshotSinceTracksChanges

TEST_CASE("SnapshotSinceTracksChanges", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{500.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto invader = spawnInvaderAlpha(w, pid);
  (void)spawnDefenderAoe(w, Position{500.F, 500.F});

  // Drain the spawn-time dirty list; both entities visible at tick_=0.
  const auto initial = extractWorldDelta(w);
  CHECK(initial.upserts.size() == 2U);

  const auto unchanged = extractWorldDelta(w);
  CHECK(unchanged.upserts.empty());
  CHECK(unchanged.erased.empty());

  // Advance tick first so next() physics runs at a new tick value and can
  // re-mark the mover (last_updated=0 != tick_=1).
  (void)w.tick();
  (void)w.next();

  const auto moved = extractWorldDelta(w);
  CHECK(moved.upserts.size() == 1U);
  CHECK(moved.erased.empty());
  CHECK(containsId(moved.upserts, invader.id()));

  (void)w.tick();

  const auto stable = extractWorldDelta(w);
  CHECK(stable.upserts.empty());
  CHECK(stable.erased.empty());
}

#pragma endregion
#pragma region World_DefenderDoesNotAppearAsChangedAfterTick

TEST_CASE("DefenderDoesNotAppearAsChangedAfterTick", "[SimWorld]") {
  SimWorld w;
  const auto defender = spawnDefenderAoe(w, Position{50.F, 60.F});

  const auto initial = extractWorldDelta(w);
  REQUIRE(initial.upserts.size() == 1U);
  CHECK(containsId(initial.upserts, defender.id()));

  (void)w.tick();
  (void)w.next();

  const auto snaps = snapshot(w);
  REQUIRE(snaps.size() == 1U);
  CHECK(std::abs((snaps[0].pos.x) - (50.0)) <= 1e-6);
  CHECK(std::abs((snaps[0].pos.y) - (60.0)) <= 1e-6);

  const auto delta = extractWorldDelta(w);
  CHECK(delta.upserts.empty());
  CHECK(delta.erased.empty());
}

#pragma endregion
#pragma region BakePath_TwoJoints

TEST_CASE("TwoJoints", "[BakePath]") {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{3.F, 4.F}}};

  const auto bp = SegmentedPath::fromJoints(p);

  REQUIRE(bp.segments.size() == 1U);
  CHECK(std::abs((bp.segments[0].cumulativeStart) - (0.0)) <= 1e-6);
  CHECK(std::abs((bp.segments[0].length) - (5.0)) <= 1e-6);
  CHECK(std::abs((bp.totalLength) - (5.0)) <= 1e-6);
}

#pragma endregion
#pragma region BakePath_ThreeJoints

TEST_CASE("ThreeJoints", "[BakePath]") {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{3.F, 4.F}}, {{6.F, 8.F}}};

  const auto bp = SegmentedPath::fromJoints(p);

  REQUIRE(bp.segments.size() == 2U);
  CHECK(std::abs((bp.segments[0].cumulativeStart) - (0.0)) <= 1e-6);
  CHECK(std::abs((bp.segments[0].length) - (5.0)) <= 1e-6);
  CHECK(std::abs((bp.segments[1].cumulativeStart) - (5.0)) <= 1e-6);
  CHECK(std::abs((bp.segments[1].length) - (5.0)) <= 1e-6);
  CHECK(std::abs((bp.totalLength) - (10.0)) <= 1e-6);
}

#pragma endregion
#pragma region PathPosition_Endpoints

TEST_CASE("Endpoints", "[PathPosition]") {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto bp = SegmentedPath::fromJoints(p);

  const auto start = bp.calculatePositionFromProgress(0.F, 0.F);
  const auto end =
      bp.calculatePositionFromProgress(bp.totalLength, bp.totalLength);

  CHECK(std::abs((start.x) - (0.0)) <= 1e-6);
  CHECK(std::abs((start.y) - (0.0)) <= 1e-6);
  CHECK(std::abs((end.x) - (10.0)) <= 1e-6);
  CHECK(std::abs((end.y) - (0.0)) <= 1e-6);
}

#pragma endregion
#pragma region PathPosition_Midpoint

TEST_CASE("Midpoint", "[PathPosition]") {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto bp = SegmentedPath::fromJoints(p);

  const auto mid = bp.calculatePositionFromProgress(5.F, 5.F);

  CHECK(std::abs((mid.x) - (5.0)) <= 1e-6);
  CHECK(std::abs((mid.y) - (0.0)) <= 1e-6);
}

#pragma endregion
#pragma region PathPosition_CrossingSegmentBoundaryEmitsJoint

TEST_CASE("CrossingSegmentBoundaryEmitsJoint", "[PathPosition]") {
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}, {{10.F, 10.F}}};
  const auto bp = SegmentedPath::fromJoints(p);

  const auto corner = bp.calculatePositionFromProgress(12.F, 8.F);

  CHECK(std::abs((corner.x) - (10.0)) <= 1e-6);
  CHECK(std::abs((corner.y) - (0.0)) <= 1e-6);
}

#pragma endregion
#pragma region World_EnemyAdvancesOnTick

TEST_CASE("EnemyAdvancesOnTick", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{100.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto enemy = spawnInvaderAlpha(w, pid);

  CHECK(static_cast<bool>(enemy));
  CHECK(w.size() == 1U);

  (void)w.next();
  (void)w.tick();

  const auto delta = extractWorldDelta(w);
  CHECK(containsId(delta.upserts, enemy.id()));
  REQUIRE(delta.upserts.size() == 1U);
  CHECK(std::abs((delta.upserts[0].pos.x) - (50.0)) <= 1e-5);
  CHECK(std::abs((delta.upserts[0].pos.y) - (0.0)) <= 1e-5);
}

#pragma endregion
#pragma region World_ResolveEscapeesVisitsEscapedEnemy

TEST_CASE("ResolveEscapeesVisitsEscapedEnemy", "[SimWorld]") {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto enemy = spawnInvaderAlpha(w, pid, 8.F);

  (void)w.next();
  CHECK(w.size() == 1U);

  size_t resolved = 0;
  (void)w.resolveEscapees(
      [&](SimWorld::EntityId id, const Position& pos, const Pathing& pf) {
        ++resolved;
        CHECK(id == enemy.id());
        CHECK(std::abs((pos.x) - (8.0)) <= 1e-6);
        CHECK(std::abs((pos.y) - (0.0)) <= 1e-6);
        CHECK(std::abs((pf.progress) - (58.0)) <= 1e-6);
        CHECK(std::abs((pf.speed) - (50.0)) <= 1e-6);
        return true;
      });

  CHECK(resolved == 1U);
  CHECK(w.size() == 0U);

  const auto delta = extractWorldDelta(w);
  CHECK(delta.upserts.empty());
  CHECK(delta.erased.size() == 1U);
  CHECK(delta.erased[0] == enemy.id());

  const auto snaps = snapshot(w);
  CHECK(snaps.empty());
  (void)w.tick();
}

#pragma endregion
#pragma region World_ResolveEscapeesCanLeaveEnemyAlive

TEST_CASE("ResolveEscapeesCanLeaveEnemyAlive", "[SimWorld]") {
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
        CHECK(id == enemy.id());
        return false;
      });

  CHECK(resolved == 1U);
  CHECK(w.size() == 1U);

  const auto snaps = snapshot(w);
  REQUIRE(snaps.size() == 1U);
  CHECK(snaps[0].id == enemy.id());
  CHECK(std::abs((snaps[0].pos.x) - (8.0)) <= 1e-6);
  (void)w.tick();
}

#pragma endregion
#pragma region World_GetPathOutOfRange

TEST_CASE("GetPathOutOfRange", "[SimWorld]") {
  SimWorld w;
  CHECK(w.getPath(PathId{0}) == nullptr);

  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{1.F, 0.F}}};
  (void)w.addPath(p);

  CHECK(w.getPath(PathId{0}) != nullptr);
  CHECK(w.getPath(PathId{1}) == nullptr);
}

#pragma endregion
#pragma region World_ObtainPathIncludesTerminalJoint

TEST_CASE("ObtainPathIncludesTerminalJoint", "[SimWorld]") {
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

  REQUIRE(points.size() == 3U);
  CHECK(std::abs((points[0].x) - (0.0)) <= 1e-6);
  CHECK(std::abs((points[0].y) - (0.0)) <= 1e-6);
  CHECK(std::abs((points[1].x) - (10.0)) <= 1e-6);
  CHECK(std::abs((points[1].y) - (0.0)) <= 1e-6);
  CHECK(std::abs((points[2].x) - (10.0)) <= 1e-6);
  CHECK(std::abs((points[2].y) - (5.0)) <= 1e-6);
}

#pragma endregion
#pragma region Game_LoadMapInitialSnapshotAndState

TEST_CASE("LoadMapInitialSnapshotAndState", "[SimGame]") {
  SimGame game;
  (void)game.loadMap();

  const auto snap = snapshot(game);
  REQUIRE(snap.entities.size() == 0U);
  REQUIRE(snap.path_points.size() == 1U);
  CHECK(snap.path_points[0].joints.front().pos.x == 0.F);
  CHECK(snap.path_points[0].joints.front().pos.y == 0.F);
  CHECK((snap.path_points[0].joints.size()) > (2U));

  const auto delta = extractGameDelta(game);
  CHECK(delta.currentWave == 0U);
  CHECK(delta.waveTick == WaveTick{});
  CHECK(delta.lives == 20);
  CHECK(delta.resources == 1000);
  CHECK(delta.phase == std::string_view{"build"});
  CHECK(delta.upserts.empty());
  CHECK(delta.erased.empty());
}

#pragma endregion
#pragma region Game_HandleUiActionStartWaveTransitionsToWavePhase

TEST_CASE("HandleUiActionStartWaveTransitionsToWavePhase", "[SimGame]") {
  SimGame game;
  (void)game.loadMap();

  (void)game.handleUiAction(
      UiActionInput{.seq = 1, .action = "start_wave", .fields = {}});

  const auto before = extractGameDelta(game);
  CHECK(before.phase == std::string_view{"build"});

  (void)game.next();

  const auto delta = extractGameDelta(game);
  CHECK(delta.phase == std::string_view{"wave"});
  CHECK(delta.waveTick == WaveTick{1});
}

#pragma endregion
#pragma region Game_HandleUiCanvasSpawnsDefenderButKeepsBuildPhase

TEST_CASE("HandleUiCanvasSpawnsDefenderButKeepsBuildPhase", "[SimGame]") {
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
  CHECK(pending.upserts.empty());
  CHECK_FALSE(pending.spawnAllowed.has_value());

  (void)game.next();

  const auto after = extractGameDelta(game);
  CHECK(before.phase == std::string_view{"build"});
  CHECK(after.phase == std::string_view{"build"});
  REQUIRE(after.spawnAllowed.has_value());
  CHECK(after.spawnAllowed.value());
  REQUIRE(after.upserts.size() == 1U);
  CHECK(std::abs((after.upserts[0].second.x) - (300.0)) <= 1e-6);
  CHECK(std::abs((after.upserts[0].second.y) - (100.0)) <= 1e-6);
  CHECK(after.erased.empty());
}

#pragma endregion
#pragma region Game_HandleUiCanvasRightClickSpawnPlacesDefender

TEST_CASE("HandleUiCanvasRightClickSpawnPlacesDefender", "[SimGame]") {
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
  CHECK(pending.upserts.empty());

  (void)game.next();

  const auto after = extractGameDelta(game);
  CHECK(after.phase == std::string_view{"build"});
  REQUIRE(after.spawnAllowed.has_value());
  CHECK(after.spawnAllowed.value());
  REQUIRE(after.upserts.size() == 1U);
  CHECK(std::abs((after.upserts[0].second.x) - (300.0)) <= 1e-6);
  CHECK(std::abs((after.upserts[0].second.y) - (100.0)) <= 1e-6);
  CHECK(after.erased.empty());
}

#pragma endregion
#pragma region Game_HandleUiCanvasSelectingDefenderReportsSelectedPosition

TEST_CASE("HandleUiCanvasSelectingDefenderReportsSelectedPosition",
    "[SimGame]") {
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
  (void)game.next();
  (void)extractGameDelta(game);

  (void)game.handleUiCanvas(UiCanvasInput{.seq = 2,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::left,
      .buttons = 1,
      .x = 300.F,
      .y = 100.F,
      .canvasX = 120.F,
      .canvasY = 210.F,
      .command{},
      .parameters{}});
  (void)game.next();

  const auto delta = extractGameDelta(game);
  REQUIRE(delta.selectedDefender.has_value());
  CHECK(std::abs((delta.selectedDefender.value().x) - (300.0)) <= 1e-6);
  CHECK(std::abs((delta.selectedDefender.value().y) - (100.0)) <= 1e-6);
  REQUIRE(delta.defenderSummary.has_value());
}

#pragma endregion
#pragma region Game_HandleUiCanvasSpawnsShooterDefender

TEST_CASE("HandleUiCanvasSpawnsShooterDefender", "[SimGame]") {
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
      .parameters = {"DefenderShooterPistol"}});

  const auto pending = extractGameDelta(game);
  CHECK(pending.upserts.empty());
  CHECK_FALSE(pending.spawnAllowed.has_value());

  (void)game.next();

  const auto after = extractGameDelta(game);
  CHECK(before.phase == std::string_view{"build"});
  CHECK(after.phase == std::string_view{"build"});
  REQUIRE(after.spawnAllowed.has_value());
  CHECK(after.spawnAllowed.value());
  REQUIRE(after.upserts.size() == 1U);
  CHECK(std::abs((after.upserts[0].second.x) - (300.0)) <= 1e-6);
  CHECK(std::abs((after.upserts[0].second.y) - (100.0)) <= 1e-6);
  CHECK(after.erased.empty());
}

#pragma endregion
#pragma region Game_HandleUiCanvasPlacingIntentRejectsPathOverlapOnNextTick

TEST_CASE("HandleUiCanvasPlacingIntentRejectsPathOverlapOnNextTick",
    "[SimGame]") {
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
  CHECK_FALSE(pending.placementAllowed.has_value());

  (void)game.next();

  const auto delta = extractGameDelta(game);
  REQUIRE(delta.placementAllowed.has_value());
  CHECK_FALSE(delta.placementAllowed.value());
}

#pragma endregion
#pragma region Game_HandleUiCanvasRejectsBlockedDefenderSpawnOnNextTick

TEST_CASE("HandleUiCanvasRejectsBlockedDefenderSpawnOnNextTick", "[SimGame]") {
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
  CHECK(delta.upserts.empty());
  REQUIRE(delta.spawnAllowed.has_value());
  CHECK_FALSE(delta.spawnAllowed.value());
}

#pragma endregion
#pragma region Game_StartWaveSpawnsFirstEnemyOnFirstStep

TEST_CASE("StartWaveSpawnsFirstEnemyOnFirstStep", "[SimGame]") {
  SimGame game;
  (void)game.loadMap();
  (void)game.start_wave();

  (void)game.next();

  const auto snap = snapshot(game);
  CHECK(*game.tick() == 1U);
  REQUIRE(snap.entities.size() == 1U);
  CHECK(std::abs((snap.entities[0].pos.x) - (0.0)) <= 1e-6);
  CHECK(std::abs((snap.entities[0].pos.y) - (0.0)) <= 1e-6);

  const auto delta = extractGameDelta(game);
  CHECK(delta.currentWave == 0U);
  CHECK(delta.waveTick == WaveTick{1});
  CHECK(delta.lives == 20);
  CHECK(delta.resources == 1000);
  CHECK(delta.phase == std::string_view{"wave"});
  CHECK(delta.upserts.empty());
  CHECK(delta.erased.empty());
}

#pragma endregion
#pragma region Game_ExtractDeltaConsumesWorldUpdatesButNotState

TEST_CASE("ExtractDeltaConsumesWorldUpdatesButNotState", "[SimGame]") {
  SimGame game;
  (void)game.loadMap();
  (void)game.start_wave();

  (void)game.next();
  (void)game.tick();
  (void)game.next();
  (void)game.tick();

  const auto delta = extractGameDelta(game);
  CHECK(!delta.upserts.empty());
  CHECK(delta.erased.size() == 0U);
  CHECK(delta.waveTick == WaveTick{2});
  CHECK(delta.phase == std::string_view{"wave"});

  const auto* pos = findPosition(delta.upserts, delta.upserts.front().first);
  REQUIRE(pos != nullptr);
  CHECK((pos->x) > (0.F));

  const auto empty_world_delta = extractGameDelta(game);
  CHECK(empty_world_delta.upserts.empty());
  CHECK(empty_world_delta.erased.empty());
  CHECK(empty_world_delta.waveTick == WaveTick{2});
  CHECK(empty_world_delta.phase == std::string_view{"wave"});
}

#pragma endregion
#pragma region Game_ReachesGameOverAsSoonAsLivesAreExhausted

TEST_CASE("ReachesGameOverAsSoonAsLivesAreExhausted", "[SimGame]") {
  SimGame game;
  (void)game.loadMap();
  (void)game.start_wave();

  bool sawZeroLives = false;
  for (uint16_t i = 0; i < 2000; ++i) {
    (void)game.next();
    const auto delta = extractGameDelta(game);
    if (delta.lives <= 0) {
      sawZeroLives = true;
      CHECK(delta.phase == std::string_view{"game_over"});
      break;
    }
    (void)game.tick();
  }

  CHECK(sawZeroLives);
}

#pragma endregion
#pragma region Game_GameOverFreezesRemainingInvaders

TEST_CASE("GameOverFreezesRemainingInvaders", "[SimGame]") {
  SimGame game;
  (void)game.loadMap();
  (void)game.start_wave();

  GameDelta terminalDelta;
  GameSnapshot terminalSnapshot;
  bool reachedGameOver = false;
  for (int i = 0; i < 2000; ++i) {
    (void)game.next();
    terminalDelta = extractGameDelta(game);
    if (terminalDelta.phase == std::string_view{"game_over"}) {
      terminalSnapshot = snapshot(game);
      reachedGameOver = true;
      break;
    }
    (void)game.tick();
  }

  REQUIRE(reachedGameOver);

  (void)game.tick();
  (void)game.next();

  const auto afterSnapshot = snapshot(game);
  REQUIRE(afterSnapshot.entities.size() == terminalSnapshot.entities.size());
  for (size_t i = 0; i < terminalSnapshot.entities.size(); ++i) {
    CHECK(afterSnapshot.entities[i].id == terminalSnapshot.entities[i].id);
    CHECK(std::abs((afterSnapshot.entities[i].pos.x) -
                   (terminalSnapshot.entities[i].pos.x)) <= 1e-6);
    CHECK(std::abs((afterSnapshot.entities[i].pos.y) -
                   (terminalSnapshot.entities[i].pos.y)) <= 1e-6);
  }
}

#pragma endregion
#pragma region Game_ExtractFullIncludesPathsAndState

TEST_CASE("ExtractFullIncludesPathsAndState", "[SimGame]") {
  SimGame game;
  (void)game.loadMap();

  size_t path_points = 0;
  size_t upserts = 0;
  size_t erased = 0;
  size_t currentWave = 99;
  WaveTick waveTick{99};
  uint16_t lives = -1;
  uint16_t resources = -1;
  std::string_view phase = "unknown";
  UiState uiState;

  (void)game.extractFull(
      [&path_points](PathId, const Position&) { ++path_points; },
      [&upserts](SimWorld::EntityId, const Position&, const Appearance&,
          const VisualEffects&, const Health&) { ++upserts; },
      [&erased](SimWorld::EntityId) { ++erased; },
      [](const TransientExplosion&) {}, [](const TransientBeam&) {},
      [&currentWave, &waveTick, &lives, &resources, &phase, &uiState](
          size_t wave, WaveTick tick, uint16_t newLives, uint16_t newResources,
          std::string_view newPhase, const UiState& newUiState) {
        currentWave = wave;
        waveTick = tick;
        lives = newLives;
        resources = newResources;
        phase = newPhase;
        uiState = newUiState;
      });

  CHECK((path_points) > (0U));
  CHECK(upserts == 0U);
  CHECK(erased == 0U);
  CHECK(currentWave == 0U);
  CHECK(waveTick == WaveTick{0});
  CHECK(lives == 20);
  CHECK(resources == 1000);
  CHECK(phase == std::string_view{"build"});
  CHECK_FALSE(uiState.selectedDefender.has_value());
}

#pragma endregion
#pragma region Json_ParseUiCanvasMessage

TEST_CASE("ParseUiCanvasMessage", "[SimJson]") {
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

  REQUIRE(msg.has_value());
  CHECK(classifySimClientMessage(msg.value()) ==
        SimClientMessageKind::ui_canvas);

  const auto input_opt = parseUiCanvasMessage(msg.value());
  REQUIRE(input_opt.has_value());
  const auto& input = input_opt.value();
  CHECK(input.seq == 42U);
  CHECK(input.event == UiCanvasEvent::dragmove);
  CHECK(input.button == UiMouseButton::right);
  CHECK(input.buttons == 2U);
  CHECK(std::abs((input.x) - (10.5)) <= 1e-6);
  CHECK(std::abs((input.y) - (-3.25)) <= 1e-6);
  CHECK(std::abs((input.canvasX) - (100.0)) <= 1e-6);
  CHECK(std::abs((input.canvasY) - (200.5)) <= 1e-6);
  CHECK(input.shift);
  CHECK_FALSE(input.ctrl);
  CHECK(input.alt);
  CHECK_FALSE(input.meta);
}

#pragma endregion
#pragma region Json_ParseUiActionMessageFields

TEST_CASE("ParseUiActionMessageFields", "[SimJson]") {
  const auto input_opt = parseUiActionMessage(
      R"({"type":"ui_action","seq":7,"action":"start_wave","fields":{"defender/kind":"ice","note":"line\nbreak"}})");

  REQUIRE(input_opt.has_value());
  const auto& input = input_opt.value();
  CHECK(input.seq == 7U);
  CHECK(input.action == "start_wave");
  REQUIRE(input.fields.size() == 2U);
  const auto defender_kind =
      std::ranges::find(input.fields, "defender/kind", &UiActionField::key);
  REQUIRE(defender_kind != input.fields.end());
  CHECK(defender_kind->value == "ice");

  const auto note =
      std::ranges::find(input.fields, "note", &UiActionField::key);
  REQUIRE(note != input.fields.end());
  CHECK(note->value == std::string("line\nbreak"));
}

#pragma endregion
#pragma region Json_BuildHelloAckJson

TEST_CASE("BuildHelloAckJson", "[SimJson]") {
  CHECK((buildSimHelloAckJson()) ==
        (std::string(R"({"type":"hello_ack","message":"connected"})")));
}

#pragma endregion
#pragma region Json_BuildWorldDeltaJsonShapeAndFormatting

TEST_CASE("BuildWorldDeltaJsonShapeAndFormatting", "[SimJson]") {
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
  CHECK(state.body.contains(R"("x":300.0)"));
  CHECK(state.body.contains(R"("radius":30.000)"));
  CHECK(state.body.contains(R"("vfx")"));
  CHECK(state.body.contains(R"("flashExpiryTick":5)"));
  CHECK(state.body.contains(R"("uiState")"));
  CHECK(state.body.contains(R"("spawnAllowed":true)"));
  CHECK_FALSE(state.body.contains(R"("selectedDefender")"));
  CHECK_FALSE(state.body.contains(R"("defenderSummary")"));
  CHECK_FALSE(state.body.contains(R"("modified")"));
  CHECK_FALSE(state.body.contains(R"("flash_expiry")"));
  CHECK_FALSE(state.body.contains(R"("glow")"));

  json_value_view root;
  REQUIRE(parse_json(state.body, root));
  const auto obj = root.as_object();
  REQUIRE(obj);
  const auto tick_value = obj.get_number<uint32_t>("tick");
  const auto phase_value = obj.get_string_view_if_plain("phase");
  REQUIRE(tick_value.has_value());
  REQUIRE(phase_value.has_value());
  CHECK(tick_value.value() == 0U);
  CHECK(phase_value.value() == std::string_view{"build"});

  const auto upserts = obj.get_array("upserts");
  REQUIRE(upserts);
  size_t count = 0;
  for (const auto item : upserts) {
    const auto entry = item.as_object();
    REQUIRE(entry);
    const auto pos = entry.get_object("pos");
    REQUIRE(pos);
    const auto x = pos.get_number<float>("x");
    REQUIRE(x.has_value());
    CHECK(std::abs((x.value()) - (300.0)) <= 1e-6);

    const auto app = entry.get_object("app");
    REQUIRE(app);
    REQUIRE(app.get_number<uint32_t>("glyph").has_value());
    REQUIRE(app.get_number<float>("radius").has_value());
    REQUIRE(!app.get_number<uint32_t>("modified").has_value());

    const auto vfx = entry.get_object("vfx");
    REQUIRE(vfx);
    REQUIRE(vfx.get_number<uint32_t>("selection").has_value());
    REQUIRE(vfx.get_number<float>("rangeRadius").has_value());
    REQUIRE(vfx.get_number<uint32_t>("range").has_value());
    REQUIRE(vfx.get_number<uint32_t>("flash").has_value());
    REQUIRE(vfx.get_number<uint32_t>("flashExpiryTick").has_value());
    ++count;
  }
  CHECK(count == 1U);
}

#pragma endregion
#pragma region Json_BuildWorldDeltaIncludesFlashVisualEffects

TEST_CASE("BuildWorldDeltaIncludesFlashVisualEffects", "[SimJson]") {
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
      .button = UiMouseButton::left,
      .buttons = 1,
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
  REQUIRE(parse_json(state.body, root));
  const auto obj = root.as_object();
  REQUIRE(obj);

  const auto upserts = obj.get_array("upserts");
  REQUIRE(upserts);
  size_t count = 0;
  for (const auto item : upserts) {
    const auto entry = item.as_object();
    REQUIRE(entry);
    const auto pos = entry.get_object("pos");
    REQUIRE(pos);
    const auto x = pos.get_number<float>("x");
    REQUIRE(x.has_value());
    CHECK(std::abs((x.value()) - (300.0)) <= 1e-6);

    const auto vfx = entry.get_object("vfx");
    REQUIRE(vfx);
    const auto flash = vfx.get_number<uint32_t>("flash");
    const auto flash_expiry_tick = vfx.get_number<uint32_t>("flashExpiryTick");
    REQUIRE(flash.has_value());
    REQUIRE(flash_expiry_tick.has_value());
    CHECK(flash.value() == 0xFF7F7FFFU);
    CHECK(flash_expiry_tick.value() == 5U);
    ++count;
  }
  CHECK(count == 1U);
}

#pragma endregion
#pragma region Json_FlashExpiryTickReturnsAbsoluteTick

TEST_CASE("FlashExpiryTickReturnsAbsoluteTick", "[SimJson]") {
  VisualEffects fx{
      .modified = WorldTick{12},
      .selectionColor = 0,
      .rangeRadius = 0.F,
      .rangeColor = 0,
      .flashColor = 0xFF0000FF,
      .flashExpiry = WorldTick{15},
  };

  CHECK(flashExpiryTick(fx) == 15U);

  fx.flashColor = 0;
  CHECK(flashExpiryTick(fx) == 0U);
}

#pragma endregion
#pragma region Json_BuildWorldSnapshotJsonShape

TEST_CASE("BuildWorldSnapshotJsonShape", "[SimJson]") {
  SimGame game;
  (void)game.loadMap();

  SimGameStateJson state;
  (void)buildSimGameStateJson(state, game, update_strategy::full);
  CHECK(state.body.contains(R"("type":"world_snapshot")"));
  CHECK(state.body.contains(R"("x":0.0)"));

  json_value_view root;
  REQUIRE(parse_json(state.body, root));
  const auto obj = root.as_object();
  REQUIRE(obj);
  const auto root_type = obj.get_string_view_if_plain("type");
  REQUIRE(root_type.has_value());
  CHECK(root_type.value() == std::string_view{"world_snapshot"});

  const auto map_design = obj.get_object("mapDesign");
  REQUIRE(map_design);
  const auto paths = map_design.get_array("paths");
  REQUIRE(paths);
  size_t path_points = 0;
  for (const auto point : paths) {
    const auto entry = point.as_object();
    REQUIRE(entry);
    REQUIRE(entry.get_number<float>("x").has_value());
    REQUIRE(entry.get_number<float>("y").has_value());
    ++path_points;
  }
  CHECK((path_points) > (0U));

  const auto delta = obj.get_object("delta");
  REQUIRE(delta);
  const auto delta_type = delta.get_string_view_if_plain("type");
  const auto delta_phase = delta.get_string_view_if_plain("phase");
  REQUIRE(delta_type.has_value());
  CHECK(delta_type.value() == std::string_view{"world_delta"});
  REQUIRE(delta_phase.has_value());
  CHECK(delta_phase.value() == std::string_view{"build"});
}

#pragma endregion
#pragma region Game_BuildCurrentMapEntityCsvReport

TEST_CASE("BuildCurrentMapEntityCsvReport", "[SimGame]") {
  SimGame game;
  REQUIRE(game.loadMap());

  const auto csv = game.buildCurrentMapEntityCsvReport();
  CHECK(csv.contains("entityName,Radius,Speed,Radius,Health,Regen,Bounty\n"));
  CHECK(csv.contains("InvaderAlphaBasic,30,50,30,50,10,10\n"));
  CHECK(csv.contains("InvaderBetaBasic,40,30,40,120,12,25\n"));
  CHECK(csv.contains(
      "\nentityName,resourceCost,radius,attackRadius,"
      "attackDamage,cooldown\n"));
  CHECK(csv.contains("DefenderAoeBasic,50,30,100,6,20\n"));
  CHECK(csv.contains("DefenderHitscanBasic,100,25,200,30,25\n"));
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
