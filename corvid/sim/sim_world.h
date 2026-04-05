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

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "../ecs.h"

namespace corvid { inline namespace sim {

// Convert Euclidean vector (x, y) to polar form (length, direction).
// `direction` is in radians,
inline std::pair<float, float> cartesian_to_polar(float x, float y) {
  float length = std::sqrt((x * x) + (y * y));
  float direction = std::atan2(y, x);
  return {length, direction};
}

// Convert a Euclidean vector in polar form to Cartesian form. `direction` is
// in radians.
inline std::pair<float, float>
polar_to_cartesian(float length, float direction) {
  float x = length * std::cos(direction);
  float y = length * std::sin(direction);
  return {x, y};
}

// 2D Cartesian position component, in world-space pixels.
struct Position {
  float x{};
  float y{};

  static Position from_polar(float radius, float angle) {
    auto [x, y] = polar_to_cartesian(radius, angle);
    return {x, y};
  }
};

// 2D Cartesian velocity component, in world-space pixels per frame.
struct Velocity {
  float vx{};
  float vy{};

  [[nodiscard]] static Velocity from_polar(float speed, float angle) {
    auto [vx, vy] = polar_to_cartesian(speed, angle);
    return {vx, vy};
  }
};

// ECS types for the simulation world.
//
// Registry metadata (`uint64_t`) stores each entity's last-change tick count,
// enabling delta snapshots without a separate dirty set.
//
// Storage layout (store_id assignment is positional, 1-based):
//   `p_sid`  = 1  -> `arch_p_t`  : background / retired entities (Position)
//   `pv_sid` = 2  -> `arch_pv_t` : moving entities (Position + Velocity)
//
// Background storage comes first so that retiring a mobile entity is a
// natural `archetype_scene::migrate_entity` call rather than erase+re-add.
using tick_t = uint64_t;
using world_reg_t = entity_registry<tick_t>;
using world_sid_t = world_reg_t::store_id_t;
using arch_p_t = archetype_storage<world_reg_t, std::tuple<Position>>;
using arch_pv_t =
    archetype_storage<world_reg_t, std::tuple<Position, Velocity>>;
using world_scene_t = archetype_scene<world_reg_t, arch_p_t, arch_pv_t>;

// Simulation world: encapsulates all ECS entity state for the game.
//
// Background entities (Position only) live in `p_sid`. Moving entities
// (Position + Velocity) live in `pv_sid`. Each `tick()` advances positions by
// velocity and bounces off the world boundary. The registry metadata records
// the tick count at each entity's last state change so callers can request
// delta snapshots starting from any past tick.
class sim_world {
public:
  using entity_id_t = world_reg_t::id_t;
  using handle_t = world_reg_t::handle_t;

  static constexpr world_sid_t p_sid{1};
  static constexpr world_sid_t pv_sid{2};

  static constexpr float world_width = 1920.0;
  static constexpr float world_height = 1080.0;

  // State snapshot for a single entity.
  struct entity_snapshot {
    entity_id_t id;
    Position pos;
  };

  // Spawn a moving entity with the given initial position and velocity.
  // Returns a handle for later `despawn()`. The entity's last-change tick
  // is set to the current tick count.
  [[nodiscard]] handle_t spawn(Position pos, Velocity vel) {
    return scene_.store_new_entity<pv_sid>(tick_n_, pos, vel);
  }

  // Spawn a static background entity. Returns a handle for later `despawn()`.
  [[nodiscard]] handle_t spawn_background(Position pos) {
    return scene_.store_new_entity<p_sid>(tick_n_, pos);
  }

  // Returns true if the handle refers to a live entity.
  [[nodiscard]] bool is_alive(handle_t h) const {
    return scene_.registry().is_valid(h);
  }

  // Erase an entity by handle. Invalidates the handle. Returns false if the
  // handle is no longer valid (entity already dead).
  bool despawn(handle_t& h) { return scene_.erase_entity(h); }

  // Advance one simulation frame. For every PV entity: add velocity to
  // position, then bounce off the world boundary by reversing the relevant
  // velocity component and clamping the position. Sets each moved entity's
  // registry metadata to the new tick count.
  //
  // If `out` is non-null, the IDs of all entities whose state changed are
  // appended to `*out`. Pass `nullptr` (the default) when only the side
  // effects are needed, to avoid the allocation.
  [[nodiscard]] tick_t tick(std::vector<entity_id_t>* out = nullptr) {
    ++tick_n_;
    scene_.for_each<Position, Velocity>([&](auto id, auto comps) {
      auto& [pos, vel] = comps;

      pos.x += vel.vx;
      pos.y += vel.vy;

      apply_boundary(pos.x, vel.vx, world_width);
      apply_boundary(pos.y, vel.vy, world_height);

      scene_.registry()[id] = tick_n_;
      if (out) out->push_back(id);
      return true;
    });
    return tick_n_;
  }

  // Return snapshots for every entity whose last-change tick is >=
  // `since_tick`. Pass 0 (the default) to return all entities. Visits both
  // `p_sid` and `pv_sid` storages since both carry `Position`.
  [[nodiscard]] std::vector<entity_snapshot> snapshot(
      tick_t since_tick = 0) const {
    std::vector<entity_snapshot> result;
    result.reserve(scene_.size());
    scene_.for_each<Position>([&](auto id, auto comps) {
      auto& [pos] = comps;
      if (scene_.registry()[id] >= since_tick) result.push_back({id, pos});
      return true;
    });
    return result;
  }

  // Return snapshots for a specific list of entity IDs. Invalid or dead IDs
  // are silently skipped.
  [[nodiscard]] std::vector<entity_snapshot> snapshot(
      const std::vector<entity_id_t>& ids) const {
    std::vector<entity_snapshot> result;
    result.reserve(ids.size());
    for (auto id : ids) {
      if (const auto* pos = scene_.try_get_component<Position>(id))
        result.push_back({id, *pos});
    }
    return result;
  }

  // Total number of entities in all storages (does not count staged entities).
  [[nodiscard]] std::size_t size() const { return scene_.size(); }

private:
  world_scene_t scene_;
  tick_t tick_n_{0};

  // Clamp `pos` to `[-limit/2, +limit/2]` and, if it was out of range, negate
  // `vel` so the entity bounces off that boundary wall.
  static void apply_boundary(float& pos, float& vel, float limit) {
    const float half = limit * 0.5F;
    if (pos < -half) {
      pos = -half;
      vel = -vel;
    } else if (pos > half) {
      pos = half;
      vel = -vel;
    }
  }
};

}} // namespace corvid::sim
