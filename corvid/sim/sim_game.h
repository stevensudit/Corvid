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
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <sys/types.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "../proto/misc/json_parser.h"
#include "sim_world.h"

// SimGame: encapsulates the game rules and flow, using SimWorld for state and
// logic. Owns the current game phase, the wave definitions, and the player's
// lives and resources. Relies on SimWsHandler for I/O.

namespace corvid { inline namespace sim {

// Different phases of the game.
enum class GamePhase : uint8_t {
  invalid,
  build,     // Defender building
  wave,      // Active wave with enemies spawning and moving
  game_over, // Player has no lives left
  victory    // All waves completed with lives remaining
};
consteval auto corvid_enum_spec(GamePhase*) {
  return corvid::enums::sequence::make_sequence_enum_spec<GamePhase,
      "build,wave,game_over,victory", wrapclip{}, GamePhase::build>();
}

// Tick from start of wave.
enum class WaveTick : uint32_t {
  invalid = std::numeric_limits<uint32_t>::max()
};
consteval auto corvid_enum_spec(WaveTick*) {
  return corvid::enums::sequence::make_sequence_enum_spec<WaveTick, "">();
}

// Mouse click events.
enum class UiCanvasEvent : uint8_t {
  click,
  dblclick,
  contextmenu,
  dragstart,
  dragmove,
  dragend
};

consteval auto corvid_enum_spec(UiCanvasEvent*) {
  return corvid::enums::sequence::make_sequence_enum_spec<UiCanvasEvent,
      "click,dblclick,contextmenu,dragstart,dragmove,dragend">();
}

// Mouse buttons.
enum class UiMouseButton : uint8_t { left, middle, right, other };
consteval auto corvid_enum_spec(UiMouseButton*) {
  return corvid::enums::sequence::make_sequence_enum_spec<UiMouseButton,
      "left,middle,right,other">();
}

// Enemy spawn definition for a wave.
struct EnemySpawn {
  WaveTick startTicks;
  std::string label; // matches a label in `SimWorld::registerEntity`
  PathId pathId{};
};

// All of the enemy spawns for a wave.
struct WaveDefinition {
  std::vector<EnemySpawn> enemies; // Sorted by `startTicks`
  uint32_t resourceInflux{};       // Resources rewarded at start of wave
};

// Top-level category in the build menu. Each category has its own `Appearance`
// (including the glyph that doubles as a hotkey) and a `flavorText` shown when
// hovering over the category cell.
struct CategoryDefinition {
  std::string name; // Matches the `category` field on `EntityDefinition`.
  std::string displayName; // Human-readable label.
  std::string flavorText;  // Shown in the info box on hover.
  Appearance appearance;   // Glyph, radius, and colors for the category cell.
};

// Full definition of one entity type, combining display metadata with the
// component template used to register and spawn it.
struct EntityDefinition {
  std::string entityName;  // Unique identifier.
  std::string displayName; // Human-readable name.
  int menuOrder{};         // Display order within the category.
  std::string category;    // Top-level menu category (e.g., "area", "laser").
  std::string flavorText;  // Description of its capabilities.
  // `max` means "not for sale" (all invaders default to this).
  uint32_t resourceCost{std::numeric_limits<uint32_t>::max()};
  WorldScene::megatuple_t megatuple; // Component template.
};

// Collection of `EntityDefinition`s, keyed by `entityName`.
using EntityDefinitions = string_unordered_map<EntityDefinition>;

// Human-focused summary of a defender, used for both the build menu and the
// selected tab.
struct DefenderSummary {
  WorldTick modified{WorldTick::invalid}; // Not used for menu items
  std::string entityName;
  std::string displayName;
  int menuOrder{}; // Used for menu items.
  std::string category;
  std::string flavorText;
  uint32_t resourceCost{};
  Appearance appearance;    // Extracted from megatuple at build time.
  float totalDamageDealt{}; // Live stats; only valid for selected defenders.
  float totalKills{};
};

// Menu of available defenders. Sorted by `menuOrder`, filtered to purchasable
// defenders only. Note that these are not indexed by `entityTemplateIndex`.
using DefenderMenu = std::vector<DefenderSummary>;

// Everything needed to play one map: paths, sprites, entity definitions,
// derived defender menu, and wave schedule. Self-contained so that
// `SimGame` can hold multiple `MapDesign`s, all read in from map files.
struct MapDesign {
  std::vector<CategoryDefinition> categories; // Top-level menu categories.
  std::vector<PathJoints> paths;
  std::string backgroundSpriteFile;
  std::string foregroundSpriteFile;
  EntityDefinitions entityDefs;
  EntityTemplateStore entityTemplateStore;
  DefenderMenu defenderMenu;
  std::vector<WaveDefinition> waves;
};

// Input from the UI canvas, such as mouse clicks and drags. Includes `command`
// and `parameters` for when the mouse location is paired with an action.
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

  // Convenience accessor for the first parameter.
  [[nodiscard]] std::string_view parameter() const {
    std::string_view found;
    if (!parameters.empty()) found = parameters[0];
    return found;
  }
};

// Input from the UI for actions, such as form submissions.
struct UiActionField {
  std::string key;
  std::string value;
};

// Represents an action input from the UI, such as pressing a button. Can
// include related data, such as form fields.
struct UiActionInput {
  uint64_t seq{};
  std::string action;
  std::vector<UiActionField> fields;
};

// Current state of the UI, for inclusion in a `WorldDelta` to the client.
struct UiState {
  std::optional<bool> placementAllowed;
  std::optional<bool> spawnAllowed;
  std::optional<Position> selectedDefender;
  std::optional<DefenderSummary> defenderSummary;
};

// Game simulation.
class SimGame {
public:
  explicit SimGame() { (void)resetMap(); }

  // Load all maps from the maps directory and activate the first one.
  [[nodiscard]] bool loadMap() {
    (void)resetMap();
    if (loadedMaps().empty()) return false;
    return selectMap(loadedMaps().begin()->first);
  }

  // Human-readable CSV dump of the currently selected map's invader and
  // defender definitions.
  [[nodiscard]] std::string buildCurrentMapEntityCsvReport() const {
    return buildMapEntityCsvReport(*mapDesign_);
  }

  // Resets all map information.
  [[nodiscard]] bool resetMap() {
    world_.clear();
    phase_ = GamePhase::build;
    mapDesign_ = nullptr;
    currentWave_ = 0;
    waveTick_ = {};
    nextSpawnIndex_ = 0;
    lives_ = 20;
    resources_ = 1000;
    uiState_ = {};
    pendingPlacementIntent_.reset();
    pendingSpawnIntent_.reset();
    pendingSelectIntent_.reset();
    pendingActionIntent_.reset();
    pendingMoveIntent_.reset();
    selectedDefender_ = {};
    return true;
  }

  // Run all physics and game logic for the current tick, without advancing the
  // counter. To advance the counter, call `tick` after streaming the state to
  // clients.
  [[nodiscard]] bool next() {
    (void)evaluatePendingUiIntents();
    if (phase_ == GamePhase::build || phase_ == GamePhase::wave)
      (void)world_.next();

    if (phase_ == GamePhase::wave) {
      ++waveTick_;
      spawnPendingWaveEnemies();

      auto lives = lives_;
      (void)world_.resolveEscapees(
          [&lives](SimWorld::EntityId, const Position&, const Pathing&) {
            --lives;
            // TODO: Treat front-escapees differently from rear-escapees, and
            // treat different enemies differently in terms of how many lives
            // they consume.
            return true;
          });
      lives_ = lives;

      if (lives_ <= 0) {
        phase_ = GamePhase::game_over;
        lives_ = 0;
        (void)refreshSelectedDefenderState();
        return true;
      }

      auto resources = resources_;
      (void)world_.resolveKills(
          [&resources](SimWorld::EntityId, const Position&,
              const Invader& inv) {
            resources += inv.bounty;
            return true;
          });
      resources_ = resources;

      // Wave is over when all enemies have been spawned and none remain.
      const auto& wave = mapDesign_->waves[currentWave_];
      if (nextSpawnIndex_ >= wave.enemies.size() &&
          !world_.hasActiveInvaders())
      {
        ++currentWave_;
        phase_ =
            (currentWave_ >= mapDesign_->waves.size())
                ? GamePhase::victory
                : GamePhase::build;
      }
    }
    (void)refreshSelectedDefenderState();
    return true;
  }

  // Advance the tick counter and return the new value. Call this at the end
  // of each frame, after `next` and streaming.
  [[nodiscard]] WorldTick tick() { return world_.tick(); }

  // Return the current tick counter without advancing it.
  [[nodiscard]] WorldTick currentTick() const { return world_.currentTick(); }

  // Player action: start the next wave.
  [[nodiscard]] bool start_wave() {
    if (phase_ != GamePhase::build) return false;
    resources_ += mapDesign_->waves[currentWave_].resourceInflux;
    phase_ = GamePhase::wave;
    waveTick_ = {};
    nextSpawnIndex_ = 0;
    return true;
  }

  // Record most recent action intent from the UI canvas.
  [[nodiscard]] bool handleUiCanvas(const UiCanvasInput& input) {
    // Drag a defender ghost.
    if (input.command == "placing") {
      if (input.parameters.empty() ||
          (input.event != UiCanvasEvent::dragstart &&
              input.event != UiCanvasEvent::dragmove &&
              input.event != UiCanvasEvent::dragend))
        return false;

      pendingPlacementIntent_ = input;
      return true;
    }

    // Drag the selected defender to a new position.
    if (input.command == "moving") {
      if (input.event != UiCanvasEvent::dragstart &&
          input.event != UiCanvasEvent::dragmove &&
          input.event != UiCanvasEvent::dragend)
        return false;

      pendingMoveIntent_ = input;
      return true;
    }

    // Click to spawn a defender.
    if (input.command == "spawn") {
      if (input.event != UiCanvasEvent::click || input.parameters.empty())
        return false;

      pendingSpawnIntent_ = input;
      return true;
    }

    // Click to select a defender.
    if (input.button == UiMouseButton::left &&
        input.event == UiCanvasEvent::click)
    {
      pendingSelectIntent_ = input;
      return true;
    }

    return true;
  }

  // Record most recent action intent from the UI action buttons.
  [[nodiscard]] bool handleUiAction(const UiActionInput& input) {
    pendingActionIntent_ = input;
    return true;
  }

  // Access the current map design (for streaming and inspection).
  [[nodiscard]] const MapDesign& mapDesign() const { return *mapDesign_; }
  [[nodiscard]] const UiState& uiState() const { return uiState_; }

  // Extract a snapshot of the paths for `WorldSnapshot`, calling back
  // `cbPath(PathId, Position)` for each joint.
  [[nodiscard]] bool extractPaths(auto&& cbPath) const {
    (void)world_.obtainPaths(cbPath);
    return true;
  }

  // Destructively extract a delta of the game state, for `WorldDelta`. The
  // `cbUpserts(EntityId, Position, Appearance, VisualEffects, Health)` and
  // `cbErased(EntityId)` callbacks will be interleaved.
  // `cbExplosion(TransientExplosion)` and `cbBeam(TransientBeam)` are invoked
  // for each fire-and-forget transient emitted this frame. The
  // `cbState(currentWave, waveTick, lives, resources, phase, uiState)`
  // callback is invoked last.
  [[nodiscard]] bool extractDelta(auto&& cbUpserts, auto&& cbErased,
      auto&& cbExplosion, auto&& cbBeam, auto&& cbState) {
    // Clear the selected defender if it is no longer valid.
    if (world_.getId(selectedDefender_) == SimWorld::EntityId::invalid)
      (void)clearSelectedDefender();

    (void)world_.extractUpdatedEntities(cbUpserts, cbErased);
    (void)world_.extractTransientExplosions(cbExplosion);
    (void)world_.extractTransientBeams(cbBeam);
    (void)cbState(currentWave_, waveTick_, lives_, resources_,
        sequence::enum_as_view(phase_), uiState_);

    uiState_.placementAllowed.reset();
    uiState_.spawnAllowed.reset();

    return true;
  }

  // Destructively extract a full snapshot of the game state. The `cbPath`
  // callback will be invoked first, with `cbUpserts(EntityId, Position,
  // Appearance, VisualEffects, Health)` and `cbErased(EntityId)` invoked
  // afterwards, and interleaved. `cbExplosion(TransientExplosion)` and
  // `cbBeam(TransientBeam)` are invoked for each fire-and-forget transient
  // emitted this frame. The `cbState(currentWave, waveTick, lives, resources,
  // phase, uiState)` callback is invoked last.
  [[nodiscard]] bool extractFull(auto&& cbPath, auto&& cbUpserts,
      auto&& cbErased, auto&& cbExplosion, auto&& cbBeam, auto&& cbState) {
    (void)extractPaths(cbPath);
    (void)markAllDirty();
    (void)extractDelta(cbUpserts, cbErased, cbExplosion, cbBeam, cbState);
    return true;
  }

  // See underlying method.
  [[nodiscard]] bool markAllDirty(
      update_strategy strategy = update_strategy::incremental) {
    (void)world_.markAllDirty(strategy);
    return true;
  }

private:
  // Find `EntityDefinition` by name.
  [[nodiscard]] const EntityDefinition* findEntityDef(
      std::string_view entityName) const {
    return find_opt(mapDesign_->entityDefs, entityName);
  }

  // Determine whether an entity can be placed at a given position. We do not
  // allow placement over a path or on top of another defender.
  [[nodiscard]] bool
  canPlaceDefender(const EntityDefinition* def, const Position& pos) const {
    if (!def) return false;
    if (phase_ != GamePhase::build) return false;
    // Reject if the player cannot afford this defender.
    if (resources_ < def->resourceCost) return false;
    // Look up initial defender radius.
    const auto& def_opt = std::get<std::optional<Defender>>(def->megatuple);
    if (!def_opt) return false;
    return !world_.isDefenderPlacementBlocked(pos, def_opt->hitCircleRadius);
  }

  // Determine whether a named entity can be placed at a given position.
  [[nodiscard]] bool
  canPlaceDefender(std::string_view entityName, const Position& pos) const {
    return canPlaceDefender(findEntityDef(entityName), pos);
  }

  // Apply a move-drag intent: validate placement and, on dragend, relocate
  // the selected defender.
  [[nodiscard]] bool applyMoveIntent(const UiCanvasInput& input) {
    const auto newPos = Position{input.x, input.y};
    const bool valid = canMoveDefender(newPos);
    uiState_.placementAllowed = valid;
    if (!valid || input.event != UiCanvasEvent::dragend) return true;
    const auto selectedId = world_.getId(selectedDefender_);
    if (selectedId == SimWorld::EntityId::invalid) return true;
    if (auto* pos = world_.try_get_component<Position>(selectedId)) {
      *pos = newPos;
      (void)world_.markDirty(selectedId);
      uiState_.selectedDefender = newPos;
    }
    return true;
  }

  // Determine whether the currently selected defender can be moved to `pos`.
  // No resource check: relocation is free. The defender's own footprint is
  // excluded from the overlap test so it can be "placed" at its origin.
  [[nodiscard]] bool canMoveDefender(const Position& pos) {
    if (phase_ != GamePhase::build) return false;
    const auto selectedId = world_.getId(selectedDefender_);
    if (selectedId == SimWorld::EntityId::invalid) return false;
    const auto& [_pos, _app, _fx, defender] = world_.getDefender(selectedId);
    if (!defender) return false;
    return !world_.isDefenderPlacementBlocked(pos, defender->hitCircleRadius,
        selectedId);
  }

  // Build `DefenderSummary` for a selected defender by entity definition.
  [[nodiscard]] std::optional<DefenderSummary> buildSelectedDefenderSummary(
      const EntityDefinition* def) const {
    if (!def) return std::nullopt;
    const auto& app_opt = std::get<std::optional<Appearance>>(def->megatuple);
    assert(app_opt);
    if (def->resourceCost == std::numeric_limits<uint32_t>::max())
      return std::nullopt;
    return DefenderSummary{.modified = world_.currentTick(),
        .entityName = def->entityName,
        .displayName = def->displayName,
        .category = def->category,
        .flavorText = def->flavorText,
        .resourceCost = def->resourceCost,
        .appearance = *app_opt};
  }

  // Build `DefenderSummary` for a selected defender by entity name.
  [[nodiscard]] std::optional<DefenderSummary> buildSelectedDefenderSummary(
      std::string_view entityName) const {
    return buildSelectedDefenderSummary(findEntityDef(entityName));
  }

  // Build `DefenderSummary` for a selected defender by `Defender` component.
  [[nodiscard]] std::optional<DefenderSummary> buildSelectedDefenderSummary(
      const Defender& defender) const {
    return buildSelectedDefenderSummary(
        world_.getEntityTemplateLabel(defender.entityTemplateIndex));
  }

  // Apply pending UI intents such as placement, spawn, selection, and actions.
  [[nodiscard]] bool evaluatePendingUiIntents() {
    if (pendingPlacementIntent_) {
      const auto& input = *pendingPlacementIntent_;
      uiState_.placementAllowed =
          canPlaceDefender(input.parameter(), {input.x, input.y});
      pendingPlacementIntent_.reset();
    }

    if (pendingSpawnIntent_) {
      const auto& input = *pendingSpawnIntent_;
      const auto parameter = input.parameter();
      auto def = findEntityDef(parameter);
      auto pos = Position{input.x, input.y};
      bool spawnAllowed = canPlaceDefender(def, pos);
      if (spawnAllowed) {
        // We need to spawn the entity using the registered template label, not
        // the map definition.
        auto h = world_.spawnEntity(parameter);
        if (h) {
          *world_.try_get_component<Position>(h.id()) = pos;
          if (def) resources_ -= def->resourceCost;
        } else
          spawnAllowed = false;
      }
      uiState_.spawnAllowed = spawnAllowed;
      pendingSpawnIntent_.reset();
    }

    if (pendingSelectIntent_) {
      const auto& input = *pendingSelectIntent_;
      (void)selectDefenderAt({input.x, input.y});
      pendingSelectIntent_.reset();
    }

    if (pendingMoveIntent_) {
      (void)applyMoveIntent(*pendingMoveIntent_);
      pendingMoveIntent_.reset();
    }

    if (pendingActionIntent_) {
      const auto& input = *pendingActionIntent_;
      if (input.action == "start_wave" && phase_ == GamePhase::build)
        (void)start_wave();
      pendingActionIntent_.reset();
    }
    return true;
  }

  // Attempt to select a defender at the given position. Unselects the current
  // selection, regardless.
  [[nodiscard]] bool selectDefenderAt(const Position& targetPos) {
    (void)clearSelectedDefender();
    auto selected = world_.getHandle(world_.findDefenderAt(targetPos));
    const auto selectedId = world_.getId(selected);
    const auto& [pos, _, fx, defender] = world_.getDefender(selectedId);
    if (!pos || !defender || !fx) return false;

    // Add selection visual effects.
    fx->selectionColor = 0xFFF2B63FU;
    fx->rangeRadius = defender->attackRadius;
    fx->rangeColor = 0xFFFF007F;
    fx->modified = world_.currentTick();
    (void)world_.markDirty(selectedId);

    selectedDefender_ = selected;
    uiState_.selectedDefender = *pos;
    uiState_.defenderSummary = buildSelectedDefenderSummary(*defender);
    return true;
  }

  // Ensure that the selected defender has the correct values.
  [[nodiscard]] bool refreshSelectedDefenderState() {
    const auto selectedId = world_.getId(selectedDefender_);
    if (selectedId == SimWorld::EntityId::invalid)
      return clearSelectedDefender();

    const auto& [pos, _, fx, defender] = world_.getDefender(selectedId);
    if (!pos || !defender || !fx) return clearSelectedDefender();

    uiState_.selectedDefender = *pos;

    // Refresh live stats into the summary, stamping `modified` only when
    // the values actually change so the wire layer sends it selectively.
    if (uiState_.defenderSummary) {
      auto& summary = *uiState_.defenderSummary;
      const auto* stats = world_.try_get_component<DefenderStats>(selectedId);
      assert(stats);
      if (stats->totalDamageDealt != summary.totalDamageDealt ||
          stats->totalKills != summary.totalKills)
      {
        summary.totalDamageDealt = stats->totalDamageDealt;
        summary.totalKills = stats->totalKills;
        summary.modified = world_.currentTick();
      }
    }
    return true;
  }

  // Clear the currently selected defender.
  [[nodiscard]] bool clearSelectedDefender() {
    if (auto* fx = world_.changeVisualEffects(world_.getId(selectedDefender_)))
    {
      fx->selectionColor = 0;
      fx->rangeRadius = 0.F;
      fx->rangeColor = 0;
    }
    selectedDefender_ = {};
    uiState_.selectedDefender.reset();
    uiState_.defenderSummary.reset();
    return true;
  }

  // Populate `entityTemplateStore` and `defenderMenu` from `entityDefs`.
  // Called once per map at load time, before the map enters the singleton.
  static void finalizeMapDesign(MapDesign& design) {
    for (const auto& [name, def] : design.entityDefs) {
      if (!design.entityTemplateStore.registerEntity(def.entityName,
              def.megatuple))
        throw std::runtime_error(
            "Failed to register entity: " + def.entityName);
      if (def.resourceCost == std::numeric_limits<uint32_t>::max()) continue;
      const auto& app_opt = std::get<std::optional<Appearance>>(def.megatuple);
      assert(app_opt);
      design.defenderMenu.push_back({.entityName = def.entityName,
          .displayName = def.displayName,
          .menuOrder = def.menuOrder,
          .category = def.category,
          .flavorText = def.flavorText,
          .resourceCost = def.resourceCost,
          .appearance = *app_opt});
    }
    std::ranges::sort(design.defenderMenu,
        [](const DefenderSummary& a, const DefenderSummary& b) {
          return a.menuOrder < b.menuOrder;
        });
  }

  // Add each `PathJoints` from `mapDesign_->paths` to the world. Previous
  // paths must first be cleared by `resetMap`.
  [[nodiscard]] bool registerPaths() {
    for (const auto& pj : mapDesign_->paths) (void)world_.addPath(pj);
    return true;
  }

  // Spawn all the enemies slated for this wave tick.
  void spawnPendingWaveEnemies() {
    const auto& wave = mapDesign_->waves[currentWave_];
    const auto& enemies = wave.enemies;
    for (; nextSpawnIndex_ < enemies.size(); ++nextSpawnIndex_) {
      const auto& enemyDef = enemies[nextSpawnIndex_];
      if (enemyDef.startTicks > waveTick_) break;
      auto h = world_.spawnEntity(enemyDef.label);
      if (!h) continue;

      // Set placement-specific fields not encoded in the template.
      auto [pos, pat] = world_.try_get_components<Position, Pathing>(h.id());
      assert(pos);
      pat->pathId = enemyDef.pathId;
      const auto* path = world_.getPath(pat->pathId);
      assert(path);
      *pos = path->calculatePositionFromProgress(0.F, 0.F);
    }
  }

private:
  SimWorld world_;

  GamePhase phase_ = GamePhase::build;

  // Tick counter for the current wave, used to trigger spawns at the right
  // times. This is reset with each wave, so it is not the same tick counter
  // as the world's, and may not even run at the same speed.
  WaveTick waveTick_{};

  // All design-time data for the currently loaded map.
  const MapDesign* mapDesign_{};

  // Current wave.
  size_t currentWave_{};

  // Index of the next spawn in the current `WaveDefinition`, which is
  // checked against `wave_tick_`.
  size_t nextSpawnIndex_{};

  int16_t lives_{20};
  uint32_t resources_{100};
  UiState uiState_;

  std::optional<UiCanvasInput> pendingPlacementIntent_;
  std::optional<UiCanvasInput> pendingSpawnIntent_;
  std::optional<UiCanvasInput> pendingSelectIntent_;
  std::optional<UiActionInput> pendingActionIntent_;
  std::optional<UiCanvasInput> pendingMoveIntent_;

  // Handle to the currently selected defender, used to clear its range
  // circle when deselected.
  SimWorld::Handle selectedDefender_;

  // Walk up from the executable to find the `corvid/sim/maps` directory.
  [[nodiscard]] static std::filesystem::path findSimMapsDir() {
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    for (auto dir = exe.parent_path(); dir != dir.parent_path();
        dir = dir.parent_path())
    {
      auto candidate = dir / "corvid/sim/maps";
      if (std::filesystem::is_directory(candidate, ec)) return candidate;
    }
    return {};
  }

  // Parse a single map JSON file into `out`. Returns false on any error.
  [[nodiscard]] static bool
  loadMapFromJson(const std::filesystem::path& file, MapDesign& out);

  [[nodiscard]] static std::string buildMapEntityCsvReport(
      const MapDesign& design);

  [[nodiscard]] static bool shouldEmitMapEntityCsvReport() {
    return std::getenv("CORVID_SUPPRESS_MAP_ENTITY_CSV") == nullptr;
  }

  // Load all `.json` files from the maps directory and return them keyed by
  // stem filename (e.g., "map1"), sorted alphabetically.
  [[nodiscard]] static string_map<MapDesign> doLoadMaps() {
    string_map<MapDesign> maps;
    const auto dir = findSimMapsDir();
    if (dir.empty()) {
      std::cerr << "Maps directory not found\n";
      return maps;
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      if (entry.path().extension() != ".json") continue;
      MapDesign design;
      if (!loadMapFromJson(entry.path(), design)) {
        std::cerr << "Failed to load map: " << entry.path() << "\n";
        continue;
      }
      finalizeMapDesign(design);
      if (shouldEmitMapEntityCsvReport())
        std::cout << buildMapEntityCsvReport(design);
      maps.emplace(entry.path().stem().string(), std::move(design));
    }
    return maps;
  }

  // All maps loaded from disk, keyed by stem filename (e.g., "map1").
  // Sorted alphabetically so the first entry is deterministic.
  [[nodiscard]] static const string_map<MapDesign>& loadedMaps() {
    static const auto maps = doLoadMaps();
    return maps;
  }

  // Copy the named map into `mapDesign_` and register its entities and paths.
  [[nodiscard]] bool selectMap(std::string_view name) {
    auto found = find_opt(loadedMaps(), name);
    if (!found) return false;
    mapDesign_ = found;
    world_.setEntityTemplateStore(&mapDesign_->entityTemplateStore);
    return registerPaths();
  }
};

// Parse a single map JSON file into `out`. The caller is responsible for
// calling `finalizeMapDesign` and `registerPaths` afterward.
// NOLINTBEGIN(readability-function-cognitive-complexity)
[[nodiscard]] inline bool
SimGame::loadMapFromJson(const std::filesystem::path& file, MapDesign& out) {
  // Slurp the file.
  std::ifstream ifs{file};
  if (!ifs.is_open()) {
    std::cerr << "Cannot open map file: " << file << "\n";
    return false;
  }
  const std::string content{std::istreambuf_iterator<char>(ifs),
      std::istreambuf_iterator<char>()};

  // Parse top-level object.
  json_value_view root;
  if (!parse_json(content, root) || !root.is_object()) {
    std::cerr << "JSON parse error in: " << file << "\n";
    return false;
  }
  const auto obj = root.as_object();

  // Decode an optional string key into `result` (unchanged if absent).
  auto opt_str =
      [](json_object_view o, std::string_view key, std::string& result) {
        if (const auto v = o.find(key)) (void)v.decode_string(result);
      };

  // Parse a color value from `key` into `result`. Accepts a "0xRRGGBBAA"
  // hex string or a plain decimal integer. Returns false if key is absent or
  // the value cannot be parsed; `result` is unchanged on failure.
  auto parse_color =
      [](json_object_view o, std::string_view key, uint32_t& result) -> bool {
    const auto v = o.find(key);
    if (!v) return false;
    if (const auto sv = v.string_view_if_plain()) {
      auto s = *sv;
      if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        uint32_t parsed{};
        auto [ptr, ec] =
            std::from_chars(s.data(), s.data() + s.size(), parsed, 16);
        if (ec == std::errc{} && ptr == s.data() + s.size()) {
          result = parsed;
          return true;
        }
        return false;
      }
    }
    if (const auto n = v.as_number<uint32_t>()) {
      result = *n;
      return true;
    }
    return false;
  };

  // Parse "categories" array.
  if (const auto cats = obj.get_array("categories")) {
    for (const auto elem : cats) {
      if (!elem.is_object()) continue;
      const auto co = elem.as_object();
      CategoryDefinition cat;
      opt_str(co, "name", cat.name);
      if (cat.name.empty()) continue;
      opt_str(co, "displayName", cat.displayName);
      opt_str(co, "flavorText", cat.flavorText);
      if (const auto app = co.get_object("appearance")) {
        uint32_t glyph{};
        (void)app.parse_number("glyph", glyph);
        cat.appearance.glyph = static_cast<char32_t>(glyph);
        (void)app.parse_number("radius", cat.appearance.radius);
        (void)parse_color(app, "fgColor", cat.appearance.fgColor);
        (void)parse_color(app, "bgColor", cat.appearance.bgColor);
        if (const auto v = app.get_number<float>("attackRadius"))
          cat.appearance.attackRadius = *v;
      }
      out.categories.push_back(std::move(cat));
    }
  }

  // Parse "entities" array.
  if (const auto ents = obj.get_array("entities")) {
    for (const auto elem : ents) {
      if (!elem.is_object()) continue;
      const auto eo = elem.as_object();

      EntityDefinition def;
      opt_str(eo, "entityName", def.entityName);
      if (def.entityName.empty()) continue;
      opt_str(eo, "displayName", def.displayName);
      opt_str(eo, "category", def.category);
      opt_str(eo, "flavorText", def.flavorText);
      if (const auto v = eo.get_number<int>("menuOrder")) def.menuOrder = *v;
      // Absent `resourceCost` leaves the default `max` = not for sale.
      if (const auto v = eo.get_number<uint32_t>("resourceCost"))
        def.resourceCost = *v;

      auto& tpl = def.megatuple;

      // Position (always present when the entity has world placement).
      if (eo.find("position"))
        std::get<std::optional<Position>>(tpl) = Position{};

      // Appearance.
      if (const auto app = eo.get_object("appearance")) {
        Appearance a;
        uint32_t glyph{};
        (void)app.parse_number("glyph", glyph);
        a.glyph = static_cast<char32_t>(glyph);
        (void)app.parse_number("radius", a.radius);
        (void)parse_color(app, "fgColor", a.fgColor);
        (void)parse_color(app, "bgColor", a.bgColor);
        if (const auto v = app.get_number<float>("attackRadius"))
          a.attackRadius = *v;
        std::get<std::optional<Appearance>>(tpl) = a;
      }

      // VisualEffects (key presence required; value may be empty `{}`).
      if (eo.find("visualEffects")) {
        VisualEffects vfx;
        if (const auto vo = eo.get_object("visualEffects")) {
          (void)parse_color(vo, "flashColor", vfx.flashColor);
          if (const auto v = vo.get_number<uint32_t>("flashExpiry"))
            vfx.flashExpiry = WorldTick{*v};
        }
        std::get<std::optional<VisualEffects>>(tpl) = vfx;
      }

      // Pathing (invaders only).
      if (const auto po = eo.get_object("pathing")) {
        Pathing p{.pathId = PathId::invalid};
        if (const auto v = po.get_number<float>("speed")) p.speed = *v;
        std::get<std::optional<Pathing>>(tpl) = p;
      }

      // Invader component.
      if (const auto io = eo.get_object("invader")) {
        Invader inv;
        if (const auto v = io.get_number<float>("hitCircleRadius"))
          inv.hitCircleRadius = *v;
        if (const auto v = io.get_number<uint32_t>("bounty")) inv.bounty = *v;
        std::get<std::optional<Invader>>(tpl) = inv;
      }

      // Health.
      if (const auto ho = eo.get_object("health")) {
        Health h;
        if (const auto v = ho.get_number<float>("currentHealth"))
          h.currentHealth = *v;
        if (const auto v = ho.get_number<float>("maxHealth")) h.maxHealth = *v;
        if (const auto v = ho.get_number<float>("regen")) h.regen = *v;
        std::get<std::optional<Health>>(tpl) = h;
      }

      // Defender base component.
      if (const auto dfo = eo.get_object("defender")) {
        Defender d;
        if (const auto v = dfo.get_number<float>("hitCircleRadius"))
          d.hitCircleRadius = *v;
        if (const auto v = dfo.get_number<float>("attackRadius"))
          d.attackRadius = *v;
        (void)parse_color(dfo, "rangeColor", d.rangeColor);
        if (const auto v = dfo.get_number<float>("attackDamage"))
          d.attackDamage = *v;
        if (const auto v = dfo.get_number<uint32_t>("cooldown"))
          d.cooldown = WorldTick{*v};
        std::get<std::optional<Defender>>(tpl) = d;
      }

      // DefenderStats (key presence is sufficient; value is empty `{}`).
      if (eo.find("defenderStats"))
        std::get<std::optional<DefenderStats>>(tpl) = DefenderStats{};

      // Area-of-effect attack.
      if (const auto ao = eo.get_object("defenderAoe")) {
        DefenderAoe a;
        if (const auto v = ao.get_number<int>("damageType")) a.damageType = *v;
        std::get<std::optional<DefenderAoe>>(tpl) = a;
      }

      // Projectile attack.
      if (const auto so = eo.get_object("defenderShooter")) {
        DefenderShooter s;
        if (const auto v = so.get_number<float>("fireRate")) s.fireRate = *v;
        if (const auto bo = so.get_object("bullet")) {
          if (const auto v = bo.get_number<float>("hitCircleRadius"))
            s.bulletTemplate.hitCircleRadius = *v;
          if (const auto v = bo.get_number<float>("speed"))
            s.bulletTemplate.speed = *v;
          if (const auto v = bo.get_number<float>("directDamage"))
            s.bulletTemplate.directDamage = *v;
          if (const auto v = bo.get_number<int>("projectileType"))
            s.bulletTemplate.projectileType = *v;
          if (const auto v = bo.get_number<uint32_t>("expiry"))
            s.bulletTemplate.expiry = WorldTick{*v};
        }
        if (const auto mo = so.get_object("muzzleFlash")) {
          if (const auto v = mo.get_number<float>("radius"))
            s.muzzleFlashTemplate.circle.radius = *v;
          if (const auto v = mo.get_number<uint32_t>("expiry"))
            s.muzzleFlashTemplate.expiry = WorldTick{*v};
          (void)parse_color(mo, "primaryColor",
              s.muzzleFlashTemplate.primaryColor);
          if (const auto v = mo.get_number<float>("halfAngleDeg"))
            s.muzzleFlashTemplate.halfAngleDeg = *v;
          if (const auto v = mo.get_number<float>("coneRadius"))
            s.muzzleFlashTemplate.coneRadius = *v;
        }
        std::get<std::optional<DefenderShooter>>(tpl) = s;
      }

      // Hitscan (instant-beam) attack.
      if (const auto hso = eo.get_object("defenderHitscan")) {
        TransientBeam beam;
        if (const auto v = hso.get_number<float>("radius"))
          beam.circle.radius = *v;
        if (const auto v = hso.get_number<uint32_t>("expiry"))
          beam.expiry = WorldTick{*v};
        (void)parse_color(hso, "primaryColor", beam.primaryColor);
        (void)parse_color(hso, "secondaryColor", beam.secondaryColor);
        if (const auto v = hso.get_number<float>("lineWidth"))
          beam.lineWidth = *v;
        std::get<std::optional<DefenderHitscan>>(tpl).emplace(beam);
      }

      const auto def_name = def.entityName;
      out.entityDefs.try_emplace(def_name, std::move(def));
    }
  }

  // Parse "paths" array.
  if (const auto paths = obj.get_array("paths")) {
    for (const auto elem : paths) {
      if (!elem.is_object()) continue;
      const auto po = elem.as_object();
      PathJoints pj;
      if (const auto v = po.get_number<float>("width")) pj.width = *v;
      if (const auto joints = po.get_array("joints")) {
        for (const auto jelem : joints) {
          if (!jelem.is_object()) continue;
          const auto jo = jelem.as_object();
          Position pos;
          if (const auto v = jo.get_number<float>("x")) pos.x = *v;
          if (const auto v = jo.get_number<float>("y")) pos.y = *v;
          pj.joints.push_back({pos});
        }
      }
      out.paths.push_back(std::move(pj));
    }
  }

  // Parse "waves" array.
  if (const auto waves = obj.get_array("waves")) {
    for (const auto elem : waves) {
      if (!elem.is_object()) continue;
      const auto wo = elem.as_object();
      WaveDefinition wave;
      if (const auto v = wo.get_number<int>("resourceInflux"))
        wave.resourceInflux = *v;
      if (const auto enemies = wo.get_array("enemies")) {
        for (const auto eelem : enemies) {
          if (!eelem.is_object()) continue;
          const auto eo2 = eelem.as_object();
          EnemySpawn spawn;
          if (const auto v = eo2.get_number<uint32_t>("tick"))
            spawn.startTicks = WaveTick{*v};
          opt_str(eo2, "label", spawn.label);
          if (const auto v = eo2.get_number<uint32_t>("pathId"))
            spawn.pathId = static_cast<PathId>(*v);
          wave.enemies.push_back(std::move(spawn));
        }
      }
      std::ranges::sort(wave.enemies,
          [](const EnemySpawn& a, const EnemySpawn& b) {
            return a.startTicks < b.startTicks;
          });
      out.waves.push_back(std::move(wave));
    }
  }

  return true;
}
// NOLINTEND(readability-function-cognitive-complexity)

[[nodiscard]] inline std::string SimGame::buildMapEntityCsvReport(
    const MapDesign& design) {
  auto csv_escape = [](std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    const bool needs_quotes =
        value.contains(',') || value.contains('"') || value.contains('\n');
    if (!needs_quotes) return std::string{value};
    out.push_back('"');
    for (const char ch : value) {
      if (ch == '"') out.push_back('"');
      out.push_back(ch);
    }
    out.push_back('"');
    return out;
  };

  std::vector<const EntityDefinition*> invaders;
  std::vector<const EntityDefinition*> defenders;
  for (const auto& [_, def] : design.entityDefs)
    if (std::get<std::optional<Defender>>(def.megatuple))
      defenders.push_back(&def);
    else if (std::get<std::optional<Invader>>(def.megatuple))
      invaders.push_back(&def);

  auto by_name = [](const EntityDefinition* lhs, const EntityDefinition* rhs) {
    return lhs->entityName < rhs->entityName;
  };
  std::ranges::sort(invaders, by_name);
  std::ranges::sort(defenders, by_name);

  std::ostringstream oss;
  oss << "entityName,Radius,Speed,Radius,Health,Regen,Bounty\n";
  for (const auto* def : invaders) {
    const auto& pathing = std::get<std::optional<Pathing>>(def->megatuple);
    const auto& invader = std::get<std::optional<Invader>>(def->megatuple);
    const auto& app = std::get<std::optional<Appearance>>(def->megatuple);
    const auto& health = std::get<std::optional<Health>>(def->megatuple);
    assert(pathing && invader && app && health);
    oss << csv_escape(def->entityName) << ',' << invader->hitCircleRadius
        << ',' << pathing->speed << ',' << app->radius << ','
        << health->currentHealth << ',' << health->regen << ','
        << invader->bounty << '\n';
  }

  oss << "\nentityName,resourceCost,radius,attackRadius,attackDamage,"
         "cooldown\n";
  for (const auto* def : defenders) {
    const auto& defender = std::get<std::optional<Defender>>(def->megatuple);
    const auto& app = std::get<std::optional<Appearance>>(def->megatuple);
    assert(defender && app);
    oss << csv_escape(def->entityName) << ',' << def->resourceCost << ','
        << app->radius << ',' << defender->attackRadius << ','
        << defender->attackDamage << ',' << *defender->cooldown << '\n';
  }
  return oss.str();
}

}} // namespace corvid::sim
