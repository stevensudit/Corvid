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

}} // namespace corvid::sim

template<>
constexpr auto corvid::enums::registry::enum_spec_v<corvid::sim::GamePhase> =
    corvid::enums::sequence::make_sequence_enum_spec<corvid::sim::GamePhase,
        "invalid, build, wave, game_over, victory">();

template<>
constexpr auto corvid::enums::registry::enum_spec_v<corvid::sim::WaveTick> =
    corvid::enums::sequence::make_sequence_enum_spec<corvid::sim::WaveTick,
        "">();

namespace corvid { inline namespace sim {

// Enemy spawn definition for a wave.
struct EnemySpawn {
  WaveTick startTicks;
  int enemyType;
};

// All of the enemy spawns for a wave.
struct WaveDefinition {
  std::vector<EnemySpawn> enemies;
  int resourceInflux = 0; // Resources rewarded at start of wave
};

// Game simulation.
class SimGame {
public:
  explicit SimGame() = default;

  // Load the chosen map, resetting all game state.
  void loadMap() {
    // TODO: Have multiple maps.
    doLoadMap1();
  }

  // Resets all map information.
  void resetMap() {
    world_.clear();
    phase_ = GamePhase::build;
    waves_.clear();
    currentWave_ = 0;
    waveTick = {};
    nextSpawnIndex_ = 0;
    lives_ = 20;
    resources_ = 100;
  }

  // Advance the simulation one tick. Returns the new tick count.
  WorldTick step() {
    const auto tick = world_.tick();
    if (phase_ == GamePhase::wave) {
      ++waveTick;
      spawn_pending_wave_enemies();

      auto lives = lives_;
      (void)world_.resolveEscapees(
          [&lives](SimWorld::EntityId, const Position&, const PathFollower&) {
            --lives;
            // TODO: Treat front-escapees different from rear-escapees, and
            // treat different enemies differently in terms of how many lives
            // they consume.
            return true;
          });
      lives_ = lives;

      if (lives_ <= 0) phase_ = GamePhase::game_over;
    }

    return tick;
  }

  // Player action: start the next wave.
  void start_wave() {
    if (phase_ != GamePhase::build) return;
    phase_ = GamePhase::wave;
    waveTick = {};
    nextSpawnIndex_ = 0;
  }

  void placeTower(/* later */);

  // Extract a snapshot of the paths by calling `cbPath(pathId, Position)` for
  // all joints of all paths.
  [[nodiscard]] bool extractPaths(auto&& cbPath) const {
    (void)world_.obtainPaths(cbPath);
    return true;
  }

  // Extract a delta of the game state. The `cbUpserts(EntityId, Position,
  // Appearance)` and `cbErased(EntityId)` callbacks will be interleaved. The
  // `cbState(currentWave, waveTick, lives, resources)` callback is invoked
  // last.
  [[nodiscard]] bool
  extractDelta(auto&& cbUpserts, auto&& cbErased, auto&& cbState) {
    (void)world_.extractUpdatedEntities(cbUpserts, cbErased);
    (void)cbState(currentWave_, waveTick, lives_, resources_,
        sequence::enum_as_view(phase_));

    return true;
  }

  // Extract a full snapshot of the game state. The `cbPath` callback will be
  // invoked first, with `cbUpserts` and `cbErased` invoked afterwards and
  // interleaved. The `cbState` callback is invoked last.
  [[nodiscard]] bool extractFull(auto&& cbPath, auto&& cbUpserts,
      auto&& cbErased, auto&& cbState) {
    (void)extractPaths(cbPath);
    (void)markAllDirty();
    (void)extractDelta(cbUpserts, cbErased, cbState);
    return true;
  }

  // See underlying method.
  [[nodiscard]]
  bool markAllDirty(update_strategy strategy = update_strategy::incremental) {
    (void)world_.markAllDirty(strategy);
    return true;
  }

private:
  // Spawn all the enemies slated for this wave tick.
  void spawn_pending_wave_enemies() {
    const auto& wave = waves_[currentWave_];
    const auto& enemies = wave.enemies;
    for (; nextSpawnIndex_ < enemies.size(); ++nextSpawnIndex_) {
      const auto& enemy_def = enemies[nextSpawnIndex_];
      if (enemy_def.startTicks > waveTick) break;
      (void)world_.spawnEnemy(PathId{0}, 20.F, 0.F);
    }
  }

  void doLoadMap1() {
    resetMap();
    // For now, a hardcoded spiral.
    PathJoints p;
    p.joints.push_back({Position{0.0, 0.0}});
    constexpr float kHalfWidth = SimWorld::widthOfWorld / 2.F;
    constexpr float kHalfHeight = SimWorld::heightOfWorld / 2.F;
    constexpr float kStepSize = 80.0;
    constexpr float kAspect = SimWorld::widthOfWorld / SimWorld::heightOfWorld;
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
    (void)world_.addPath(p);

    WaveDefinition wave;
    for (uint32_t i = 0; i < 20; ++i)
      wave.enemies.push_back({WaveTick{i * 20}, 0});

    waves_.push_back(std::move(wave));
  }

private:
  SimWorld world_;

  GamePhase phase_ = GamePhase::build;

  // Tick counter for the current wave, used to trigger spawns at the right
  // times. This is reset with each wave, so it is not the same tick counter as
  // the world's, and may not even run at the same speed.
  WaveTick waveTick{};

  // Wave definitions for the current map.
  std::vector<WaveDefinition> waves_;

  // Current wave.
  size_t currentWave_{};

  // Index of the next spawn in the current `WaveDefinition`, which is checked
  // against `wave_tick_`.
  size_t nextSpawnIndex_{};

  int lives_{20};
  int resources_{100};
};
}} // namespace corvid::sim
