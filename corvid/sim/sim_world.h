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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "../ecs.h"

namespace corvid { inline namespace sim {

// Convert Cartesian coordinates (x, y) to a Euclidean vector, in polar form
// (length, direction). `direction` is in radians,
[[nodiscard]] constexpr std::pair<float, float>
cartesian_to_polar(float x, float y) noexcept {
  float length = std::sqrt((x * x) + (y * y));
  float direction = std::atan2(y, x);
  return {length, direction};
}

// Convert a Euclidean vector, in polar form, to Cartesian coordinates.
// `direction` is in radians.
[[nodiscard]] constexpr std::pair<float, float>
polar_to_cartesian(float length, float direction) noexcept {
  float x = length * std::cos(direction);
  float y = length * std::sin(direction);
  return {x, y};
}

// Linear interpolation calculator
[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept {
  return a + ((b - a) * t);
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

// --- Path geometry ---

// `path_joints` defines a path in terms of the coordinates of its joints. This
// is the authored path, a compact format suitable for defining the path, but
// not ideal for runtime use, which is why it's converted to `segmented_path`.
struct path_joints {
  struct joint {
    Position p;
  };

  std::vector<joint> joints;
  float width{40.F};
};

// `segmented_path` is built from a `path_joints` and consists of pre-computed
// segments with cumulative distances, enabling efficient runtime mapping from
// distance traveled to world position. Essentially, it's an indexed vertex
// buffer for the path geometry.
struct segmented_path {
  struct segment {
    Position front;
    Position back;
    float length;           // Euclidean distance from `front` to `back`.
    float cumulative_start; // Sum of lengths of all preceding segments.
  };

  std::vector<segment> segments;
  float total_length{};
  float width{};

  // Map a distance traveled (`progress`) to a world-space `Position`.
  // `progress` is clamped to `[0, total_length]`. Returns the origin if
  // `segments` is empty.
  [[nodiscard]] Position position_from_progress(float progress) const {
    if (segments.empty()) return {};
    progress = std::clamp(progress, 0.F, total_length);

    // Find the last segment whose cumulative_start <= progress.
    auto it = std::ranges::upper_bound(segments, progress, {},
        &segmented_path::segment::cumulative_start);
    if (it != segments.begin()) --it;
    const auto& seg = *it;

    // Calculate the interpolation factor `t` along this segment, then lerp the
    // front and back positions to get the world position.
    float t = 0.F;
    if (seg.length > 0.F) t = (progress - seg.cumulative_start) / seg.length;

    return {lerp(seg.front.x, seg.back.x, t),
        lerp(seg.front.y, seg.back.y, t)};
  }

  // Convert authored `path_joints` into a `segmented_path`. Requires at
  // least two joints; returns an empty `segmented_path` if the input is
  // degenerate.
  static segmented_path from_joints(const path_joints& p) {
    if (p.joints.size() < 2) return {};
    segmented_path sp;
    sp.width = p.width;
    sp.segments.reserve(p.joints.size() - 1);
    float cumulative{};

    for (std::size_t i = 0; i + 1 < p.joints.size(); ++i) {
      const Position& front = p.joints[i].p;
      const Position& back = p.joints[i + 1].p;
      const float dx = back.x - front.x;
      const float dy = back.y - front.y;
      const float len = std::hypot(dx, dy);
      sp.segments.push_back({front, back, len, cumulative});
      cumulative += len;
    }
    sp.total_length = cumulative;
    return sp;
  }

  [[nodiscard]] path_joints to_joints() const {
    path_joints pj;
    pj.width = width;
    pj.joints.reserve(segments.size() + 1);
    for (const auto& seg : segments) pj.joints.push_back({seg.front});
    if (!segments.empty()) { pj.joints.push_back({segments.back().back}); }
    return pj;
  }
};

// Path-following component. Stored alongside `Position` in the enemy
// archetype. Each tick, `progress` advances by `speed`; the entity's
// `Position` is re-derived from the segmented path geometry.
struct PathFollower {
  uint8_t path_id{}; // Index into `sim_world::paths_`.
  float progress{};  // Distance traveled along the path so far.
  float speed{};     // Distance per tick.
};

// ECS types for the simulation world.
//
// Registry metadata (`uint64_t`) stores each entity's last-change tick count,
// enabling delta snapshots without a separate dirty set.
//
// Storage layout (store_id assignment is positional, 1-based):
//   `p_sid`      = 1  -> `arch_p_t`      : background / destructible / retired
//                                          (Position)

//   `pv_sid`     = 2  -> `arch_pv_t`     : moving entities
//                                          (Position + Velocity)

//   `enemy_sid`  = 3  -> `arch_enemy_t`  : path-following enemies
//                                          (Position + PathFollower)
//
// Background storage comes first so that retiring a mobile entity is a
// natural `archetype_scene::migrate_entity` call rather than erase+re-add.
using tick_t = uint64_t;
using world_reg_t = entity_registry<tick_t>;
using world_sid_t = world_reg_t::store_id_t;
using arch_p_t = archetype_storage<world_reg_t, std::tuple<Position>>;
using arch_pv_t =
    archetype_storage<world_reg_t, std::tuple<Position, Velocity>>;
using arch_enemy_t =
    archetype_storage<world_reg_t, std::tuple<Position, PathFollower>>;
using world_scene_t =
    archetype_scene<world_reg_t, arch_p_t, arch_pv_t, arch_enemy_t>;

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
  static constexpr world_sid_t enemy_sid{3};

  static constexpr float world_width = 1920.0;
  static constexpr float world_height = 1080.0;

  // State snapshot for a single entity. This is the serialization format for
  // the entity state sent to clients.
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

  // Bake and store a path. Returns the index used as `PathFollower::path_id`.
  // The index is stable for the lifetime of the world.
  [[nodiscard]] uint8_t add_path(const path_joints& p) {
    paths_.push_back(segmented_path::from_joints(p));
    return static_cast<uint8_t>(paths_.size() - 1);
  }

  // Return a pointer to the baked path at `id`, or `nullptr` if out of range.
  [[nodiscard]] const segmented_path* get_path(uint8_t id) const {
    if (id >= paths_.size()) return nullptr;
    return &paths_[id];
  }

  // Return snapshots for all authored paths so the client can render the
  // original joints while runtime movement follows the baked geometry.
  [[nodiscard]] std::vector<path_joints> path_snapshot() const {
    std::vector<path_joints> result;
    result.reserve(paths_.size() + 1);
    for (const auto& p : paths_) result.push_back(p.to_joints());
    return result;
  }

  // Spawn a path-following enemy. Initial position is derived from `progress`
  // on the named path. Returns a handle for later `despawn()`.
  [[nodiscard]] handle_t
  spawn_enemy(uint8_t path_id, float speed, float progress = 0.F) {
    if (path_id >= paths_.size()) return {};
    const auto pos = paths_[path_id].position_from_progress(progress);
    return scene_.store_new_entity<enemy_sid>(tick_n_, pos,
        PathFollower{path_id, progress, speed});
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

    // Advance velocity-driven entities.
    scene_.for_each<Position, Velocity>([&](auto id, auto comps) {
      auto& [pos, vel] = comps;
      // Skip over currently immobile entities.
      if (vel.vx == 0.F && vel.vy == 0.F) return true;

      // TODO: Consider refactoring the increment and bounce into a single
      // method that does both. For that matter, the current algorithm is
      // wrong: an entity hitting a boundary shouldn't be clipped to the
      // boundary and its velocity reversed. Rather, part of its movement
      // should be reflected back. So, for example, if dx is 20 and the entity
      // is 5 pixels from the right edge, it should spend 5 of those 20 moving
      // to the right, and then 15 moving back. Its velocity should be reversed
      // starting at the edge. The only reason the current algorithm works at
      // all is that velocities are small. The algorithm is also bad in a
      // different way, which is that it measures position from the center, not
      // a bounding box, so the balls enter the boundary before reversing. Even
      // then, a square bounding box would be hit-detected prematurely if the
      // entity is moving at a diagonal.

      pos.x += vel.vx;
      pos.y += vel.vy;

      bounce_from_boundary(pos.x, vel.vx, world_width);
      bounce_from_boundary(pos.y, vel.vy, world_height);

      scene_.registry()[id] = tick_n_;
      if (out) out->push_back(id);
      return true;
    });

    // Advance path-following enemies. Collect entities that reached the end
    // so they can be despawned after iteration.
    std::vector<entity_id_t> finished;
    scene_.for_each<Position, PathFollower>([&](auto id, auto comps) {
      auto& [pos, pf] = comps;
      pf.progress += pf.speed;

      // TODO: Is this even possible?
      if (pf.path_id >= paths_.size()) return true;

      // TODO: Speed can be negative, so we need to collect entities that
      // exited front and back, separately. Why separately? So that we can
      // count them against the score. Note that we also have to be careful
      // here to deal with the possibility of a non-enemy that follows the
      // track and therefore doesn't count against the score. The easy way is
      // to iterate through the entities and ask which archetype they are. This
      // might be possible with ducktyping is enemies have a component that the
      // player doesn't, such as an enemy type that defines how to draw them.
      const auto& sp = paths_[pf.path_id];
      if (pf.progress >= sp.total_length) {
        finished.push_back(id);
        return true;
      }

      pos = sp.position_from_progress(pf.progress);
      scene_.registry()[id] = tick_n_;

      if (out) out->push_back(id);
      return true;
    });
    // IDs were collected from an active iteration, so erase_entity will not
    // fail; the return value is voided intentionally.
    for (auto id : finished) (void)scene_.erase_entity(id);

    return tick_n_;
  }

  // Return snapshots for every entity whose that has changed since
  // `since_tick`. Pass 0 (the default) to return all entities. Visits all
  // storages that carry `Position` (`p_sid`, `pv_sid`, `enemy_sid`).
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
  std::vector<segmented_path> paths_;

  // Clamp `pos` to `[-limit/2, +limit/2]` and, if it was out of range, negate
  // `vel` so the entity bounces off that boundary wall.
  // TODO: Is there an off-by-one error here? If the world is 1920 wide, the
  // actual range isn't [-960, +960], it's [-960, +960). Do we need to deal
  // with this? It depends on how the client scales it, right?
  static void bounce_from_boundary(float& pos, float& vel, float limit) {
    const float half = limit / 2.F;
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
