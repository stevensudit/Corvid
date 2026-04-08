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
  std::vector<std::pair<SimWorld::EntityId, Position>> upserts;
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
      [&snaps](SimWorld::EntityId id, const Position& pos, const Appearance&) {
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
snapshot(SimWorld& world, const std::vector<SimWorld::EntityId>& ids) {
  const auto all = snapshot(world);
  std::vector<EntitySnapshot> filtered;
  filtered.reserve(ids.size());
  std::ranges::copy_if(all, std::back_inserter(filtered),
      [&ids](const EntitySnapshot& snap) {
        return std::ranges::find(ids, snap.id) != ids.end();
      });
  return filtered;
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
      [&delta](SimWorld::EntityId id, const Position& pos, const Appearance&) {
        delta.upserts.emplace_back(id, pos);
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
      [&snap](SimWorld::EntityId id, const Position& pos, const Appearance&) {
        snap.entities.push_back({id, pos});
      },
      [](SimWorld::EntityId) {},
      [](size_t, WaveTick, int, int, std::string_view) {});

  snap.path_points = std::move(pathsById);
  return snap;
}

[[nodiscard]] GameDelta extractGameDelta(SimGame& game) {
  GameDelta delta;
  (void)game.extractDelta(
      [&delta](SimWorld::EntityId id, const Position& pos, const Appearance&) {
        delta.upserts.emplace_back(id, pos);
      },
      [&delta](SimWorld::EntityId id) { delta.erased.push_back(id); },
      [&delta](size_t currentWave, WaveTick waveTick, int lives, int resources,
          std::string_view phase) {
        delta.currentWave = currentWave;
        delta.waveTick = waveTick;
        delta.lives = lives;
        delta.resources = resources;
        delta.phase = phase;
      });
  return delta;
}

[[nodiscard]] bool containsId(const auto& entries, SimWorld::EntityId id) {
  return std::ranges::any_of(entries, [id](const auto& entry) {
    return entry.first == id;
  });
}

[[nodiscard]] const Position*
findPosition(const auto& entries, SimWorld::EntityId id) {
  const auto it = std::ranges::find_if(entries, [id](const auto& entry) {
    return entry.first == id;
  });
  return it == entries.end() ? nullptr : &it->second;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

void SimWorld_SpawnAndSnapshot() {
  SimWorld w;
  EXPECT_EQ(w.size(), 0U);

  const auto mover = w.spawnMover(Position{10.F, 20.F}, Velocity{1.F, 2.F});
  const auto background = w.spawnBackground(Position{30.F, 40.F});

  EXPECT_EQ(w.size(), 2U);

  const auto all = snapshot(w);
  ASSERT_EQ(all.size(), 2U);
  EXPECT_EQ(
      filterSnapshot(all, std::vector<SimWorld::EntityId>{mover.id()}).size(),
      1U);
  EXPECT_EQ(
      filterSnapshot(all,
          std::vector<SimWorld::EntityId>{mover.id(), background.id()})
          .size(),
      2U);
}

void SimWorld_TickMovesMover() {
  SimWorld w;
  const auto mover = w.spawnMover(Position{100.F, 200.F}, Velocity{3.F, -5.F});

  EXPECT_EQ(*w.tick(), 1U);

  const auto snaps = snapshot(w);
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_EQ(snaps[0].id, mover.id());
  EXPECT_NEAR(snaps[0].pos.x, 103.0, 1e-6);
  EXPECT_NEAR(snaps[0].pos.y, 195.0, 1e-6);
}

void SimWorld_ExtractUpdatedEntitiesReportsMovedMoverOncePerExtraction() {
  SimWorld w;
  const auto mover = w.spawnMover(Position{0.F, 0.F}, Velocity{2.F, 3.F});

  (void)w.tick();
  const auto delta = extractWorldDelta(w);

  EXPECT_TRUE(containsId(delta.upserts, mover.id()));
  EXPECT_TRUE(delta.erased.empty());

  const auto* pos = findPosition(delta.upserts, mover.id());
  ASSERT_TRUE(pos != nullptr);
  EXPECT_NEAR(pos->x, 2.0, 1e-6);
  EXPECT_NEAR(pos->y, 3.0, 1e-6);

  const auto empty_delta = extractWorldDelta(w);
  EXPECT_TRUE(empty_delta.upserts.empty());
  EXPECT_TRUE(empty_delta.erased.empty());
}

void SimWorld_BounceMinEdge() {
  SimWorld w;
  const float half_w = SimWorld::widthOfWorld * 0.5F;
  const float half_h = SimWorld::heightOfWorld * 0.5F;
  const auto mover = w.spawnMover(Position{-half_w + 0.5F, -half_h + 0.5F},
      Velocity{-2.F, -2.F});

  (void)w.tick();

  const auto snaps = snapshot(w, std::vector<SimWorld::EntityId>{mover.id()});
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_EQ(snaps[0].id, mover.id());
  EXPECT_GE(snaps[0].pos.x, -half_w);
  EXPECT_GE(snaps[0].pos.y, -half_h);
}

void SimWorld_BounceMaxEdge() {
  SimWorld w;
  const float half_w = SimWorld::widthOfWorld * 0.5F;
  const float half_h = SimWorld::heightOfWorld * 0.5F;
  const auto mover =
      w.spawnMover(Position{half_w - 0.5F, half_h - 0.5F}, Velocity{2.F, 2.F});

  (void)w.tick();

  const auto snaps = snapshot(w, std::vector<SimWorld::EntityId>{mover.id()});
  ASSERT_EQ(snaps.size(), 1U);
  EXPECT_EQ(snaps[0].id, mover.id());
  EXPECT_LE(snaps[0].pos.x, half_w);
  EXPECT_LE(snaps[0].pos.y, half_h);
}

void SimWorld_SnapshotSinceTracksChanges() {
  SimWorld w;
  (void)w.spawnMover(Position{10.F, 10.F}, Velocity{1.F, 0.F});
  (void)w.spawnBackground(Position{20.F, 20.F});

  const auto initial = snapshot(w);
  EXPECT_EQ(initial.size(), 2U);

  const auto unchanged = extractWorldDelta(w);
  EXPECT_TRUE(unchanged.upserts.empty());
  EXPECT_TRUE(unchanged.erased.empty());

  (void)w.tick();

  const auto moved = extractWorldDelta(w);
  EXPECT_EQ(moved.upserts.size(), 1U);
  EXPECT_TRUE(moved.erased.empty());

  const auto stable = extractWorldDelta(w);
  EXPECT_TRUE(stable.upserts.empty());
  EXPECT_TRUE(stable.erased.empty());
}

void SimWorld_BackgroundDoesNotAppearAsChangedAfterTick() {
  SimWorld w;
  (void)w.spawnBackground(Position{50.F, 60.F});

  (void)w.tick();

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
  const auto enemy = w.spawnEnemy(pid, 10.F);

  EXPECT_TRUE(static_cast<bool>(enemy));
  EXPECT_EQ(w.size(), 1U);

  (void)w.tick();

  const auto delta = extractWorldDelta(w);
  EXPECT_TRUE(containsId(delta.upserts, enemy.id()));
  ASSERT_EQ(delta.upserts.size(), 1U);
  EXPECT_NEAR(delta.upserts[0].second.x, 10.0, 1e-5);
  EXPECT_NEAR(delta.upserts[0].second.y, 0.0, 1e-5);
}

void SimWorld_ResolveEscapeesVisitsEscapedEnemy() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto enemy = w.spawnEnemy(pid, 5.F, 8.F);

  (void)w.tick();
  EXPECT_EQ(w.size(), 1U);

  size_t resolved = 0;
  (void)w.resolveEscapees(
      [&](SimWorld::EntityId id, const Position& pos, const PathFollower& pf) {
        ++resolved;
        EXPECT_EQ(id, enemy.id());
        EXPECT_NEAR(pos.x, 8.0, 1e-6);
        EXPECT_NEAR(pos.y, 0.0, 1e-6);
        EXPECT_NEAR(pf.progress, 13.0, 1e-6);
        EXPECT_NEAR(pf.speed, 5.0, 1e-6);
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
}

void SimWorld_ResolveEscapeesCanLeaveEnemyAlive() {
  SimWorld w;
  PathJoints p;
  p.joints = {{{0.F, 0.F}}, {{10.F, 0.F}}};
  const auto pid = w.addPath(p);
  const auto enemy = w.spawnEnemy(pid, 5.F, 8.F);

  (void)w.tick();

  size_t resolved = 0;
  (void)w.resolveEscapees(
      [&](SimWorld::EntityId id, const Position&, const PathFollower&) {
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
  game.loadMap();

  const auto snap = snapshot(game);
  ASSERT_EQ(snap.entities.size(), 0U);
  ASSERT_EQ(snap.path_points.size(), 1U);
  EXPECT_EQ(snap.path_points[0].joints.front().p.x, 0.F);
  EXPECT_EQ(snap.path_points[0].joints.front().p.y, 0.F);
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
  game.loadMap();

  game.handle_UiAction(
      UiActionInput{.seq = 1, .action = "start_wave", .fields = {}});

  const auto delta = extractGameDelta(game);
  EXPECT_EQ(delta.phase, std::string_view{"wave"});
  EXPECT_EQ(delta.waveTick, WaveTick{0});
}

void SimGame_HandleUiCanvasDoesNotChangeStateYet() {
  SimGame game;
  game.loadMap();
  const auto before = extractGameDelta(game);

  game.handleUiCanvas(UiCanvasInput{.seq = 1,
      .event = UiCanvasEvent::click,
      .button = UiMouseButton::left,
      .buttons = 1,
      .x = 10.F,
      .y = 20.F,
      .canvasX = 100.F,
      .canvasY = 200.F});

  const auto after = extractGameDelta(game);
  EXPECT_EQ(before.phase, std::string_view{"build"});
  EXPECT_EQ(after.phase, std::string_view{"build"});
  EXPECT_TRUE(after.upserts.empty());
  EXPECT_TRUE(after.erased.empty());
}

void SimGame_StartWaveSpawnsFirstEnemyOnFirstStep() {
  SimGame game;
  game.loadMap();
  game.start_wave();

  EXPECT_EQ(*game.step(), 1U);

  const auto snap = snapshot(game);
  EXPECT_TRUE(snap.entities.empty());

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
  game.loadMap();
  game.start_wave();

  (void)game.step();
  (void)game.step();

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
  game.loadMap();

  size_t path_points = 0;
  size_t upserts = 0;
  size_t erased = 0;
  size_t currentWave = 99;
  WaveTick waveTick{99};
  int lives = -1;
  int resources = -1;
  std::string_view phase = "unknown";

  (void)game.extractFull(
      [&path_points](PathId, const Position&) { ++path_points; },
      [&upserts](SimWorld::EntityId, const Position&, const Appearance&) {
        ++upserts;
      },
      [&erased](SimWorld::EntityId) { ++erased; },
      [&currentWave, &waveTick, &lives, &resources, &phase](size_t wave,
          WaveTick tick, int newLives, int newResources,
          std::string_view newPhase) {
        currentWave = wave;
        waveTick = tick;
        lives = newLives;
        resources = newResources;
        phase = newPhase;
      });

  EXPECT_GT(path_points, 0U);
  EXPECT_EQ(upserts, 0U);
  EXPECT_EQ(erased, 0U);
  EXPECT_EQ(currentWave, 0U);
  EXPECT_EQ(waveTick, WaveTick{0});
  EXPECT_EQ(lives, 20);
  EXPECT_EQ(resources, 100);
  EXPECT_EQ(phase, std::string_view{"build"});
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
      R"({"type":"ui_action","seq":7,"action":"start_wave","fields":{"tower\/kind":"ice","note":"line\nbreak"}})");

  ASSERT_TRUE(input.has_value());
  EXPECT_EQ(input->seq, 7U);
  EXPECT_EQ(input->action, "start_wave");
  ASSERT_EQ(input->fields.size(), 2U);
  EXPECT_EQ(input->fields[0].key, "tower/kind");
  EXPECT_EQ(input->fields[0].value, "ice");
  EXPECT_EQ(input->fields[1].key, "note");
  EXPECT_EQ(input->fields[1].value, std::string("line\nbreak"));
}

void SimJson_BuildHelloAckJson() {
  EXPECT_EQ(build_sim_hello_ack_json(),
      std::string(R"({"type":"hello_ack","message":"connected"})"));
}

void SimJson_BuildWorldDeltaJsonShapeAndFormatting() {
  SimGame game;
  game.loadMap();
  game.start_wave();
  (void)game.step();
  const auto tick = game.step();

  sim_game_state_json state;
  (void)build_sim_game_state_json(state, game, tick);
  EXPECT_TRUE(state.body_highwater >= state.body.size());
  EXPECT_TRUE(state.body.contains(R"("x":20.0)"));
  EXPECT_TRUE(state.body.contains(R"("scale":2.000)"));

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
  EXPECT_EQ(*tick_value, 2U);
  EXPECT_EQ(*wave_tick_value, 2U);
  EXPECT_EQ(*phase_value, std::string_view{"wave"});

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
    EXPECT_NEAR(*x, 20.0, 1e-6);
    ++count;
  }
  EXPECT_EQ(count, 1U);
}

void SimJson_BuildWorldSnapshotJsonShape() {
  SimGame game;
  game.loadMap();

  sim_game_state_json state;
  (void)build_sim_game_state_json(state, game, WorldTick{},
      update_strategy::full);
  EXPECT_TRUE(state.body.contains(R"("type":"world_snapshot")"));
  EXPECT_TRUE(state.body.contains(R"("x":0.0)"));

  json_value_view root;
  ASSERT_TRUE(parse_json(state.body, root));
  const auto obj = root.as_object();
  ASSERT_TRUE(obj);
  const auto root_type = obj.get_string_view_if_plain("type");
  ASSERT_TRUE(root_type.has_value());
  EXPECT_EQ(*root_type, std::string_view{"world_snapshot"});

  const auto paths = obj.get_array("paths");
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

MAKE_TEST_LIST(SimWorld_SpawnAndSnapshot, SimWorld_TickMovesMover,
    SimWorld_ExtractUpdatedEntitiesReportsMovedMoverOncePerExtraction,
    SimWorld_BounceMinEdge, SimWorld_BounceMaxEdge,
    SimWorld_SnapshotSinceTracksChanges,
    SimWorld_BackgroundDoesNotAppearAsChangedAfterTick, BakePath_TwoJoints,
    BakePath_ThreeJoints, BakePath_Degenerate, PathPosition_Endpoints,
    PathPosition_Midpoint, SimWorld_EnemyAdvancesOnTick,
    SimWorld_ResolveEscapeesVisitsEscapedEnemy,
    SimWorld_ResolveEscapeesCanLeaveEnemyAlive, SimWorld_GetPathOutOfRange,
    SimGame_LoadMapInitialSnapshotAndState,
    SimGame_HandleUiActionStartWaveTransitionsToWavePhase,
    SimGame_HandleUiCanvasDoesNotChangeStateYet,
    SimGame_StartWaveSpawnsFirstEnemyOnFirstStep,
    SimGame_ExtractDeltaConsumesWorldUpdatesButNotState,
    SimGame_ExtractFullIncludesPathsAndState, SimJson_ParseUiCanvasMessage,
    SimJson_ParseUiActionMessageFields, SimJson_BuildHelloAckJson,
    SimJson_BuildWorldDeltaJsonShapeAndFormatting,
    SimJson_BuildWorldSnapshotJsonShape)
