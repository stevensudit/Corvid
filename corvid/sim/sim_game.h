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
#pragma once
#include <sys/types.h>

#include <string>
#include <string_view>
#include <utility>

#include "sim_world.h"

// SimGame: encapsulates the game rules and flow, using SimWorld for state and
// logic. Owns the current game phase, the wave definitions, and the player's
// lives and resources. Relies on SimWsHandler for I/O.

namespace corvid { inline namespace sim {

// Concepts:
// - A map is a persistent layout defined by a collection of paths and build
// slots, as well as resources, background graphics, and other metadata.
// - The player selects a map and faces subsequent waves until the end of the
// game run. The run ends with defeat or victory. Potentially, runs could be
// grouped into campaigns or other higher-level structures.
// - A player's towers, score, lives, and resources persist throughout the game
// run, albeit with modifications from wave to wave.
// - A wave is a collection of enemy spawns. At the end of the wave, the player
// gets a chance to build or upgrade before the next wave starts.
// - Within a wave, spawns are enemies that appear on the track at fixed times
// from the start of the wave.

// Snapshot of the game state for sending to clients. Contains all information
// needed to render the current state of the world, but no information about
// the rules or game flow.

enum class GamePhase : uint8_t {
  invalid,
  build,     // Tower building
  wave,      // Active wave with enemies spawning and moving
  game_over, // Player has no lives left
  victory,   // All waves completed with lives remaining
};

// Tick from start of wave.
enum class WaveTick : uint32_t {
  invalid = std::numeric_limits<uint32_t>::max()
};

enum class UiCanvasEvent : uint8_t {
  click,
  dblclick,
  contextmenu,
  dragstart,
  dragmove,
  dragend,
};

enum class UiMouseButton : uint8_t {
  left,
  middle,
  right,
  other,
};

enum class TargetMode : uint8_t { first, last, closest, strongest, weakest };

}} // namespace corvid::sim

template<>
constexpr auto corvid::enums::registry::enum_spec_v<corvid::sim::GamePhase> =
    corvid::enums::sequence::make_sequence_enum_spec<corvid::sim::GamePhase,
        "invalid, build, wave, game_over, victory">();

template<>
constexpr auto corvid::enums::registry::enum_spec_v<corvid::sim::WaveTick> =
    corvid::enums::sequence::make_sequence_enum_spec<corvid::sim::WaveTick,
        "">();

template<>
constexpr auto
    corvid::enums::registry::enum_spec_v<corvid::sim::UiCanvasEvent> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::sim::UiCanvasEvent,
            "click, dblclick, contextmenu, dragstart, dragmove, dragend">();

template<>
constexpr auto
    corvid::enums::registry::enum_spec_v<corvid::sim::UiMouseButton> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::sim::UiMouseButton, "left, middle, right, other">();

template<>
constexpr auto corvid::enums::registry::enum_spec_v<corvid::sim::TargetMode> =
    corvid::enums::sequence::make_sequence_enum_spec<corvid::sim::TargetMode,
        "first, last, closest, strongest, weakest">();

namespace corvid { inline namespace sim {

// Enemy spawn definition for a wave.
struct EnemySpawn {
  WaveTick startTicks;
  std::string label; // matches a label in `SimWorld::registerEntity`
  PathId pathId{};
};

// All of the enemy spawns for a wave.
struct WaveDefinition {
  std::vector<EnemySpawn> enemies;
  int resourceInflux{}; // Resources rewarded at start of wave
};

// Full definition of one entity type, combining display metadata with the
// component template used to register and spawn it.
struct EntityDefinition {
  std::string entityName;
  std::string displayName;
  int menuOrder{}; // Display order.
  std::string flavorText;
  // NaN means "not for sale" (all invaders default to this).
  float resourceCost{std::numeric_limits<float>::quiet_NaN()};
  WorldScene::megatuple_t megatuple;
};

// Keyed by `entityName` for O(1) lookup.
using EntityDefinitions = string_unordered_map<EntityDefinition>;

// One entry in the build menu streamed to the client. Already sorted by
// `menuOrder` before streaming; `menuOrder` itself is not streamed.
struct DefenderMenuEntry {
  std::string entityName;
  std::string displayName;
  int menuOrder{};
  std::string flavorText;
  float resourceCost{};
  Appearance appearance; // extracted from megatuple at build time
};

// Sorted by `menuOrder`, filtered to purchasable defenders only.
using DefenderMenu = std::vector<DefenderMenuEntry>;

// Everything needed to play one map: paths, sprites, entity definitions,
// derived defender menu, and wave schedule. Self-contained so that
// `SimGame` can hold multiple `MapDesign`s in future.
struct MapDesign {
  std::vector<PathJoints> paths;
  std::string backgroundSpriteFile;
  std::string foregroundSpriteFile;
  EntityDefinitions entityDefs;
  DefenderMenu defenderMenu;
  std::vector<WaveDefinition> waves;
};

struct UiCanvasInput {
  uint64_t seq{};
  UiCanvasEvent event{UiCanvasEvent::click};
  UiMouseButton button{UiMouseButton::left};
  uint32_t buttons{};
  float x{};
  float y{};
  float canvasX{};
  float canvasY{};
  bool shift{};
  bool ctrl{};
  bool alt{};
  bool meta{};
  std::string command;
  std::vector<std::string> parameters;
};

struct UiActionField {
  std::string key;
  std::string value;
};

struct UiActionInput {
  uint64_t seq{};
  std::string action;
  std::vector<UiActionField> fields;
};

// Game simulation.
class SimGame {
public:
  explicit SimGame() = default;

  // Load the chosen map, resetting all game state.
  void loadMap() {
    // TODO: Have multiple maps.
    (void)doLoadMap1();
  }

  // Resets all map information.
  void resetMap() {
    world_.clear();
    phase_ = GamePhase::build;
    mapDesign_ = {};
    currentWave_ = 0;
    waveTick_ = {};
    nextSpawnIndex_ = 0;
    lives_ = 20;
    resources_ = 100;
  }

  // Run all physics and game logic for the current tick, without advancing the
  // counter. Call `tick()` after streaming the state to clients.
  [[nodiscard]] bool next() {
    (void)world_.next();
    if (phase_ == GamePhase::wave) {
      ++waveTick_;
      spawnPendingWaveEnemies();

      auto lives = lives_;
      (void)world_.resolveEscapees(
          [&lives](SimWorld::EntityId, const Position&, const Pathing&) {
            --lives;
            // TODO: Treat front-escapees different from rear-escapees, and
            // treat different enemies differently in terms of how many lives
            // they consume.
            return true;
          });
      lives_ = lives;

      if (lives_ <= 0) phase_ = GamePhase::game_over;
    }
    return true;
  }

  // Advance the tick counter and return the new value. Call this at the end
  // of each frame, after `next()` and streaming.
  [[nodiscard]] WorldTick tick() { return world_.tick(); }

  // Return the current tick counter without advancing it.
  [[nodiscard]] WorldTick currentTick() const { return world_.currentTick(); }

  // Player action: start the next wave.
  void start_wave() {
    if (phase_ != GamePhase::build) return;
    phase_ = GamePhase::wave;
    waveTick_ = {};
    nextSpawnIndex_ = 0;
  }

  void handleUiCanvas(const UiCanvasInput& input) {
    // Spawn command from build menu confirmation double-click.
    if (input.event == UiCanvasEvent::click &&
        input.button == UiMouseButton::left && input.command == "spawn" &&
        !input.parameters.empty() && phase_ == GamePhase::build)
    {
      auto h = world_.spawnEntity(input.parameters[0]);
      auto* pos = world_.try_get_component<Position>(h.id());
      *pos = {input.x, input.y};
      return;
    }

    // Select tower on click.
    if (input.event == UiCanvasEvent::click &&
        input.button == UiMouseButton::left)
    {
      // Deselect the previously selected tower, if it still exists.
      if (auto* fx = world_.changeVisualEffects(world_.getId(selected_tower_)))
      {
        fx->selectionColor = 0;
        fx->rangeRadius = 0.F;
        fx->rangeColor = 0;
        selected_tower_ = {};
      }

      selected_tower_ =
          world_.getHandle(world_.findTowerAt({input.x, input.y}));
      if (selected_tower_.id() != SimWorld::EntityId::invalid) {
        auto [pos, app, fx, tower] = world_.getTower(selected_tower_.id());
        if (pos) {
          fx->selectionColor = 0xFFF2B63FU;
          fx->rangeRadius = tower->attackRadius;
          fx->rangeColor = 0xFFFF007F;
          fx->modified = world_.currentTick();
          (void)world_.markDirty(selected_tower_.id());
        }
      }
    }

    if (input.event == UiCanvasEvent::click &&
        input.button == UiMouseButton::right)
    {
      (void)world_.flashEntity(world_.findEntityAt({input.x, input.y}),
          0xFF7F7FAF, WorldTick{5});
    }
  }

  void handleUiAction(const UiActionInput& input) {
    if (input.action == "start_wave") {
      start_wave();
      return;
    }
  }

  void placeTower(/* later */);

  // Access the current map design (for streaming and inspection).
  [[nodiscard]] const MapDesign& mapDesign() const { return mapDesign_; }

  // Extract a snapshot of the paths by calling `cbPath(pathId, Position)` for
  // all joints of all paths.
  [[nodiscard]] bool extractPaths(auto&& cbPath) const {
    (void)world_.obtainPaths(cbPath);
    return true;
  }

  // Extract a delta of the game state. The `cbUpserts(EntityId, Position,
  // Appearance, VisualEffects)` and `cbErased(EntityId)` callbacks will be
  // interleaved. The `cbState(currentWave, waveTick, lives, resources)`
  // callback is invoked last.
  [[nodiscard]] bool
  extractDelta(auto&& cbUpserts, auto&& cbErased, auto&& cbState) {
    (void)world_.extractUpdatedEntities(cbUpserts, cbErased);
    (void)cbState(currentWave_, waveTick_, lives_, resources_,
        sequence::enum_as_view(phase_));

    return true;
  }

  // Extract a full snapshot of the game state. The `cbPath` callback will be
  // invoked first, with `cbUpserts` and `cbErased` invoked afterwards and
  // interleaved. `cbUpserts` receives an optional `VisualEffects` pointer. The
  // `cbState` callback is invoked last.
  [[nodiscard]] bool extractFull(auto&& cbPath, auto&& cbUpserts,
      auto&& cbErased, auto&& cbState) {
    (void)extractPaths(cbPath);
    (void)markAllDirty();
    (void)extractDelta(cbUpserts, cbErased, cbState);
    return true;
  }

  // See underlying method.
  [[nodiscard]] bool markAllDirty(
      update_strategy strategy = update_strategy::incremental) {
    (void)world_.markAllDirty(strategy);
    return true;
  }

private:
  // Register all entities and build the defender menu from
  // `mapDesign_.entityDefs`.
  [[nodiscard]] bool processEntityDefs() {
    auto& defs = mapDesign_.entityDefs;
    auto& menu = mapDesign_.defenderMenu;
    menu.clear();
    for (const auto& [name, def] : defs) {
      world_.registerEntity(def.entityName, def.megatuple);
      if (!std::isnan(def.resourceCost)) {
        const auto& app_opt =
            std::get<std::optional<Appearance>>(def.megatuple);
        menu.push_back({.entityName = def.entityName,
            .displayName = def.displayName,
            .menuOrder = def.menuOrder,
            .flavorText = def.flavorText,
            .resourceCost = def.resourceCost,
            .appearance = *app_opt});
      }
    }
    std::ranges::sort(menu,
        [](const DefenderMenuEntry& a, const DefenderMenuEntry& b) {
          return a.menuOrder < b.menuOrder;
        });
    return true;
  }

  // Add each `PathJoints` from `mapDesign_.paths` to the world.
  [[nodiscard]] bool processPaths() {
    for (const auto& pj : mapDesign_.paths) (void)world_.addPath(pj);
    return true;
  }

  // Spawn all the enemies slated for this wave tick.
  void spawnPendingWaveEnemies() {
    const auto& wave = mapDesign_.waves[currentWave_];
    const auto& enemies = wave.enemies;
    for (; nextSpawnIndex_ < enemies.size(); ++nextSpawnIndex_) {
      const auto& enemyDef = enemies[nextSpawnIndex_];
      if (enemyDef.startTicks > waveTick_) break;
      auto h = world_.spawnEntity(enemyDef.label);
      if (!h) continue;

      // Set placement-specific fields not encoded in the template.
      auto [pos, pat] = world_.try_get_components<Position, Pathing>(h.id());
      if (pos) {
        pat->pathId = enemyDef.pathId;
        const auto* path = world_.getPath(pat->pathId);
        assert(path);
        *pos = path->calculatePositionFromProgress(0.F, 0.F);
      }
    }
  }

  [[nodiscard]] bool doLoadMap1() {
    resetMap();

    // Entity definitions. This is the single site that will later be replaced
    // by CSV/JSON parsing.
    {
      EntityDefinition def;
      def.entityName = "InvaderAlphaBasic";
      // `resourceCost` stays NaN (not for sale).
      auto& tpl = def.megatuple;
      std::get<std::optional<Position>>(tpl) = Position{};
      std::get<std::optional<Appearance>>(
          tpl) = Appearance{.glyph = U'\u03B1', // Greek alpha
          .radius = 30.F,
          .fgColor = 0xFFFFFFFF,
          .bgColor = 0x000000FF};
      std::get<std::optional<VisualEffects>>(tpl) = VisualEffects{};
      std::get<std::optional<Pathing>>(tpl) =
          Pathing{.pathId = PathId::invalid, .progress = 0.F, .speed = 50.F};
      std::get<std::optional<Invader>>(tpl) =
          Invader{.invaderType = 1, .hitCircleRadius = 30.F, .bounty = 10};
      std::get<std::optional<Health>>(tpl) =
          Health{.currentHealth = 100.F, .maxHealth = 100.F, .regen = 10.F};
      mapDesign_.entityDefs.try_emplace(def.entityName, std::move(def));
    }
    {
      EntityDefinition def;
      def.entityName = "DefenderAoeBasic";
      def.displayName = "AoE Defender";
      def.menuOrder = 1;
      def.flavorText = "Damages all enemies in range.";
      def.resourceCost = 50.F;
      auto& tpl = def.megatuple;
      std::get<std::optional<Position>>(tpl) = Position{};
      std::get<std::optional<Appearance>>(tpl) = Appearance{.glyph = U'A',
          .radius = 30.F,
          .fgColor = 0xFFFFFFFF,
          .bgColor = 0x7F7FFFFF,
          .attackRadius = 100.F};
      std::get<std::optional<VisualEffects>>(tpl) =
          VisualEffects{.flashColor = 0xFF7F7FFF, .flashExpiry = WorldTick{5}};
      std::get<std::optional<Defender>>(tpl) = Defender{.defenderType = 1,
          .hitCircleRadius = 30.F,
          .attackRadius = 100.F,
          .rangeColor = 0xFFFF0000,
          .attackDamage = 5.F,
          .cooldown = WorldTick{20},
          .nextAttack = WorldTick{0}};
      std::get<std::optional<DefenderStats>>(tpl) = DefenderStats{};
      std::get<std::optional<Health>>(tpl) =
          Health{.currentHealth = 100.F, .maxHealth = 100.F, .regen = 0.F};
      std::get<std::optional<DefenderAoe>>(tpl) = DefenderAoe{.damageType = 1};
      mapDesign_.entityDefs.try_emplace(def.entityName, std::move(def));
    }

    // Path geometry (sprite files empty until sprites are added).
    {
      PathJoints p;
      p.joints.push_back({Position{0.0, 0.0}});
      constexpr float kHalfWidth = SimWorld::widthOfWorld / 2.F;
      constexpr float kHalfHeight = SimWorld::heightOfWorld / 2.F;
      constexpr float kStepSize = 80.0;
      constexpr float kAspect =
          SimWorld::widthOfWorld / SimWorld::heightOfWorld;
      constexpr float kXStepSize = kStepSize * kAspect;
      float x = 0.0;
      float y = 0.0;
      float x_run = kXStepSize;
      float y_run = kStepSize;
      auto append_segment = [&](float dx, float dy) {
        x = std::clamp(x + dx, -kHalfWidth, kHalfWidth);
        y = std::clamp(y + dy, -kHalfHeight, kHalfHeight);
        p.joints.push_back({Position{x, y}});
        return x > -kHalfWidth && x < kHalfWidth && y > -kHalfHeight &&
               y < kHalfHeight;
      };
      while (true) {
        if (!append_segment(x_run, 0.F)) break;
        if (!append_segment(0.F, y_run)) break;
        x_run += kXStepSize;
        if (!append_segment(-x_run, 0.F)) break;
        y_run += kStepSize;
        if (!append_segment(0.F, -y_run)) break;
        x_run += kXStepSize;
        y_run += kStepSize;
      }
      mapDesign_.paths.push_back(std::move(p));
    }

    // Wave definitions.
    {
      WaveDefinition wave;
      for (uint32_t i = 0; i < 20; ++i)
        wave.enemies.push_back({WaveTick{i * 20}, "InvaderAlphaBasic"});
      mapDesign_.waves.push_back(std::move(wave));
    }

    return processEntityDefs() && processPaths();
  }

private:
  SimWorld world_;

  GamePhase phase_ = GamePhase::build;

  // Tick counter for the current wave, used to trigger spawns at the right
  // times. This is reset with each wave, so it is not the same tick counter
  // as the world's, and may not even run at the same speed.
  WaveTick waveTick_{};

  // All design-time data for the currently loaded map.
  MapDesign mapDesign_;

  // Current wave.
  size_t currentWave_{};

  // Index of the next spawn in the current `WaveDefinition`, which is
  // checked against `wave_tick_`.
  size_t nextSpawnIndex_{};

  int lives_{20};
  int resources_{100};

  // Handle to the currently selected tower, used to clear its range circle
  // when deselected. A default-constructed handle is invalid and indicates
  // no selection.
  SimWorld::Handle selected_tower_;
};
}} // namespace corvid::sim
