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
struct GameSnapshot {
  std::vector<SimWorld::EntitySnapshot> entities;
  std::vector<PathJoints> path_points;
};

enum class GamePhase : uint8_t {
  build,     // Tower building
  wave,      // Active wave with enemies spawning and moving
  game_over, // Player has no lives left
  victory,   // All waves completed with lives remaining
};

// Enemy spawn definition for a wave.
struct EnemySpawn {
  Tick start_delay;
  int enemy_type;
};

// All of the enemy spawns for a wave.
struct WaveDefinition {
  std::vector<EnemySpawn> enemies;
  int resource_influx = 0; // Resources rewarded at start of wave
};

// Game simulation.
class SimGame {
public:
  explicit SimGame() = default;

  // Load the chosen map, resetting all game state.
  void load_map() {
    // TODO: Have multiple maps.
    do_load_map_1();
  }

  // Resets all map information.
  void reset_map() {
    world_.clear();
    phase_ = GamePhase::build;
    waves_.clear();
    current_wave = 0;
    wave_tick_ = 0;
    next_spawn_index_ = 0;
    lives_ = 20;
    resources_ = 100;
  }

  // Advance the simulation one tick. Returns the new tick count.
  Tick step() {
    if (phase_ == GamePhase::wave) {
      ++wave_tick_;
      spawn_pending_wave_enemies();

      auto lives = lives_;
      (void)world_.resolve_escapes(
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

    const auto tick = world_.tick();
    return tick;
  }

  // Player action: start the next wave.
  void start_wave() {
    if (phase_ != GamePhase::build) return;
    phase_ = GamePhase::wave;
    wave_tick_ = 0;
    next_spawn_index_ = 0;
  }

  void place_tower(/* later */);

  // Get a snapshot of the current game state for sending to clients.
  [[nodiscard]] GameSnapshot snapshot() const {
    GameSnapshot snap;
    snap.entities = world_.snapshot();
    snap.path_points = world_.path_snapshot();
    return snap;
  }

private:
  // Spawn all the enemies slated for this wave tick.
  void spawn_pending_wave_enemies() {
    const auto& wave = waves_[current_wave];
    const auto& enemies = wave.enemies;
    for (; next_spawn_index_ < enemies.size(); ++next_spawn_index_) {
      const auto& enemy_def = enemies[next_spawn_index_];
      if (enemy_def.start_delay > wave_tick_) break;
      (void)world_.spawn_enemy(PathId{0}, 20.F, 0.F);
    }
  }

  void do_load_map_1() {
    reset_map();
    // For now, a hardcoded spiral.
    PathJoints p;
    p.joints.push_back({Position{0.0, 0.0}});
    constexpr float kStepSize = 80.0;
    constexpr float kAspect = SimWorld::world_width / SimWorld::world_height;
    constexpr float kXStepSize = kStepSize * kAspect;
    float x = 0.0;
    float y = 0.0;
    float x_run = kXStepSize;
    float y_run = kStepSize;
    for (int i = 0; i < 100; ++i) {
      x += x_run;
      p.joints.push_back({Position{x, y}});
      y += y_run;
      p.joints.push_back({Position{x, y}});
      x_run += kXStepSize;
      x -= x_run;
      p.joints.push_back({Position{x, y}});
      y_run += kStepSize;
      y -= y_run;
      p.joints.push_back({Position{x, y}});
      x_run += kXStepSize;
      y_run += kStepSize;
    }
    (void)world_.add_path(p);

    WaveDefinition wave;
    for (uint64_t i = 0; i < 20; ++i) wave.enemies.push_back({i * 20, 0});

    waves_.push_back(std::move(wave));
  }

private:
  SimWorld world_;

  GamePhase phase_ = GamePhase::build;

  // Tick counter for the current wave, used to trigger spawns at the right
  // times. This is reset with each wave, so it is not the same tick counter as
  // the world's, and may not even run at the same speed.
  Tick wave_tick_ = 0;

  // Wave definitions for the current map.
  std::vector<WaveDefinition> waves_;

  // Current wave.
  size_t current_wave = 0;

  // Index of the next spawn in the current `WaveDefinition`, which is checked
  // against `wave_tick_`.
  size_t next_spawn_index_ = 0;

  int lives_ = 20;
  int resources_ = 100;
};
}} // namespace corvid::sim
