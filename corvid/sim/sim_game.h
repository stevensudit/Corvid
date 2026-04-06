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

namespace corvid { inline namespace sim {

enum class GamePhase : uint8_t {
  build,
  wave,
  game_over,
  victory,
};

struct WaveEnemy {
  Tick start_delay;
  int enemy_type;
};

struct WaveDefinition {
  std::vector<WaveEnemy> enemies;
};

struct GameSnapshot {
  std::vector<SimWorld::EntitySnapshot> entities;
  std::vector<PathJoints> path_points;
};

class SimGame {
public:
  explicit SimGame(/* maybe level data */) = default;

  void load_level(/* level or map id/data */) {
    world_.clear();
    do_load_level_1();
  }

  void reset() {
    world_.clear();
    phase_ = GamePhase::build;
    current_wave_index_ = 0;
    wave_tick_ = 0;
    next_spawn_index_ = 0;
    lives_ = 20;
    resources_ = 100;
  }

  Tick step() {
    if (phase_ == GamePhase::wave) {
      ++wave_tick_;
      spawn_pending_wave_enemies();
    }

    const auto tick = world_.tick();
    return tick;
  }

  void start_wave() {
    if (phase_ != GamePhase::build) return;
    phase_ = GamePhase::wave;
    wave_tick_ = 0;
    next_spawn_index_ = 0;
  }

  void place_tower(/* later */);

  [[nodiscard]] GameSnapshot snapshot() const {
    GameSnapshot snap;
    snap.entities = world_.snapshot();
    snap.path_points = world_.path_snapshot();
    return snap;
  }

private:
  void update_build_phase();
  void update_wave_phase();
  void spawn_pending_wave_enemies() {
    const auto& wave = waves_[current_wave_index_];
    const auto& enemies = wave.enemies;
    for (; next_spawn_index_ < wave.enemies.size(); ++next_spawn_index_) {
      const auto& enemy_def = wave.enemies[next_spawn_index_];
      if (enemy_def.start_delay > wave_tick_) break;
      (void)world_.spawn_enemy(PathId{0}, 20.F, 0.F);
    }
  }

  void do_load_level_1() {
    // Example level: one path and one wave of three enemies.
    world_.clear();
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

  std::vector<WaveDefinition> waves_;
  int current_wave_index_ = 0;

  Tick wave_tick_ = 0;
  size_t next_spawn_index_ = 0;

  int lives_ = 20;
  int resources_ = 100;
};
}} // namespace corvid::sim
