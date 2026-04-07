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

// SimWorld: authoritative server-side world state and logic for the CorvidSim
// game. Owns the entities and the physics, but not the rules or the game flow.

namespace corvid { inline namespace sim {

// ID of path.
enum class PathId : uint16_t {
  invalid = std::numeric_limits<uint16_t>::max()
};

}} // namespace corvid::sim

template<>
constexpr auto corvid::enums::registry::enum_spec_v<corvid::sim::PathId> =
    corvid::enums::sequence::make_sequence_enum_spec<corvid::sim::PathId,
        "">();

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

// `PathJoints` defines a path in terms of the coordinates of its joints. This
// is the authored path, a compact format suitable for defining the path, but
// not ideal for runtime use, which is why it's converted to `SegmentedPath`.
struct PathJoints {
  struct Joint {
    Position p;
  };

  std::vector<Joint> joints;
  float width{40.F};
};

// `SegmentedPath` is built from a `PathJoints` and consists of pre-computed
// segments with cumulative distances, enabling efficient runtime mapping from
// distance traveled to world position. Essentially, it's an indexed vertex
// buffer for the path geometry.
struct SegmentedPath {
  struct Segment {
    Position front;
    Position back;
    float length;           // Euclidean distance from `front` to `back`.
    float cumulative_start; // Sum of lengths of all preceding segments.
  };

  std::vector<Segment> segments;
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
        &SegmentedPath::Segment::cumulative_start);
    if (it != segments.begin()) --it;
    const auto& seg = *it;

    // Calculate the interpolation factor `t` along this segment, then lerp the
    // front and back positions to get the world position.
    float t = 0.F;
    if (seg.length > 0.F) t = (progress - seg.cumulative_start) / seg.length;

    return {lerp(seg.front.x, seg.back.x, t),
        lerp(seg.front.y, seg.back.y, t)};
  }

  // Convert authored `PathJoints` into a `SegmentedPath`. Requires at
  // least two joints; returns an empty `SegmentedPath` if the input is
  // degenerate.
  static SegmentedPath from_joints(const PathJoints& p) {
    if (p.joints.size() < 2) return {};
    SegmentedPath sp;
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

  [[nodiscard]] PathJoints to_joints() const {
    PathJoints pj;
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
  PathId path_id{}; // Index into `sim_world::paths_`.
  float progress{}; // Distance traveled along the path so far.
  float speed{};    // Distance per tick.
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
using Tick = uint64_t;
using WorldReg = entity_registry<Tick>;
using WorldSid = WorldReg::store_id_t;
using ArchP = archetype_storage<WorldReg, std::tuple<Position>>;
using ArchPV = archetype_storage<WorldReg, std::tuple<Position, Velocity>>;
using ArchEnemy =
    archetype_storage<WorldReg, std::tuple<Position, PathFollower>>;
using WorldScene = archetype_scene<WorldReg, ArchP, ArchPV, ArchEnemy>;

// Simulation world: encapsulates all ECS entity state for the game.
//
// Background entities (Position only) live in `p_sid`. Moving entities
// (Position + Velocity) live in `pv_sid`. Each `tick()` advances positions by
// velocity and bounces off the world boundary. The registry metadata records
// the tick count at each entity's last state change so callers can request
// delta snapshots starting from any past tick.
class SimWorld {
public:
  using EntityId = WorldReg::id_t;
  using Handle = WorldReg::handle_t;

  static constexpr WorldSid staging_sid{0};
  static constexpr WorldSid p_sid{1};
  static constexpr WorldSid pv_sid{2};
  static constexpr WorldSid enemy_sid{3};

  static constexpr float world_width = 1920.0;
  static constexpr float world_height = 1080.0;

  // State snapshot for a single entity. This is the serialization format for
  // the entity state sent to clients.
  struct EntitySnapshot {
    EntityId id;
    Position pos;
  };

  void clear() {
    scene_.clear();
    paths_.clear();
    tick_n_ = 0;
  }

  // Spawn a moving entity with the given initial position and velocity.
  // Returns a handle for later `despawn()`. The entity's last-change tick
  // is set to the current tick count.
  [[nodiscard]] Handle spawn(Position pos, Velocity vel) {
    return scene_.store_new_entity<pv_sid>(tick_n_, pos, vel);
  }

  // Spawn an immobile entity. Returns a handle for later `despawn()`.
  [[nodiscard]] Handle spawn_background(Position pos) {
    return scene_.store_new_entity<p_sid>(tick_n_, pos);
  }

  // Bake and store a path. Returns the index used as `path_follower::path_id`.
  // The index is stable for the lifetime of the world.
  [[nodiscard]] PathId add_path(const PathJoints& p) {
    if (!paths_.push_back(SegmentedPath::from_joints(p)))
      return PathId::invalid;
    return paths_.size_as_enum() - 1;
  }

  // Return a pointer to the baked path at `id`, or `nullptr` if out of range.
  [[nodiscard]] const SegmentedPath* get_path(PathId path_id) const {
    if (path_id >= paths_.size_as_enum()) return nullptr;
    return &paths_[path_id];
  }

  // Return snapshots for all authored paths so the client can render the
  // original joints while runtime movement follows the baked geometry.
  [[nodiscard]] std::vector<PathJoints> path_snapshot() const {
    std::vector<PathJoints> result;
    result.reserve(paths_.size() + 1);
    for (const auto& p : paths_) result.push_back(p.to_joints());
    return result;
  }

  // Spawn a path-following enemy. Initial position is derived from `progress`
  // on the named path. Returns a handle for later `despawn()`.
  [[nodiscard]] Handle
  spawn_enemy(PathId path_id, float speed, float progress = 0.F) {
    if (path_id >= paths_.size_as_enum()) return {};
    const auto pos = paths_[path_id].position_from_progress(progress);
    return scene_.store_new_entity<enemy_sid>(tick_n_, pos,
        PathFollower{path_id, progress, speed});
  }

  // Returns true if the handle refers to a live entity.
  [[nodiscard]] bool is_alive(Handle h) const {
    return scene_.registry().is_valid(h);
  }

  // Erase an entity by handle. Invalidates the handle. Returns false if the
  // handle is no longer valid (entity already dead).
  bool despawn(Handle& h) { return scene_.erase_entity(h); }

  // Advance one simulation frame. Sets each changed entity's registry metadata
  // to the new tick count and tracks changed entities for callbacks.
  [[nodiscard]] Tick tick() {
    ++tick_n_;

    (void)tick_movers();
    (void)tick_path_followers();

    return tick_n_;
  }

  // Return snapshots for every entity whose that has changed since
  // `since_tick`. Pass 0 (the default) to return all entities. Visits all
  // storages that carry `Position` (`p_sid`, `pv_sid`, `enemy_sid`).
  [[nodiscard]] std::vector<EntitySnapshot> snapshot(
      Tick since_tick = 0) const {
    std::vector<EntitySnapshot> result;
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
  [[nodiscard]] std::vector<EntitySnapshot> snapshot(
      const std::vector<EntityId>& ids) const {
    std::vector<EntitySnapshot> result;
    result.reserve(ids.size());
    for (auto id : ids) {
      if (const auto* pos = scene_.try_get_component<Position>(id))
        result.push_back({id, *pos});
    }
    return result;
  }

  // Total number of entities in all storages (does not count staged entities).
  [[nodiscard]] std::size_t size() const { return scene_.size(); }

  // List of entities that have escaped the path.
  [[nodiscard]] auto& path_escapees() const { return path_escapees_; }

  // Resolve path escapees by calling `cb` on each one, where `cb` is of the
  // shape:
  // `std::function<bool(EntityId, const Position&, constPathFollower&)`
  //
  // Destructively iterates through the list of path escapees.  If `cb` returns
  // true, the escapee is erased; if false, it's left alive, so the caller
  // ought to have done something with it, such as migrating it to a different
  // path. Note that you must not modify `path_escapees_` in the process.
  [[nodiscard]] bool resolve_escapes(auto&& cb) {
    for (auto id : path_escapees_) {
      if (!scene_.registry().is_valid(id)) continue;
      auto [pos, pf] = scene_.try_get_components<Position, PathFollower>(id);
      if (!pos || !pf) continue;
      if (!cb(id, *pos, *pf)) continue;
      (void)erase_entity(id);
    }
    path_escapees_.clear();
    return true;
  }

private:
  WorldScene scene_;
  Tick tick_n_{0};
  id_container<SegmentedPath, PathId> paths_;

  std::vector<EntityId> updated_entities_;
  std::vector<EntityId> path_escapees_;

  // Marks an entity as dirty so that it will be included in the next delta
  // snapshot.
  bool mark_dirty(EntityId id) {
    // If it's been deleted, it's already dirty.
    if (!scene_.registry().is_valid(id)) return false;
    auto& last_updated = scene_.registry()[id];
    if (last_updated != tick_n_) {
      last_updated = tick_n_;
      updated_entities_.push_back(id);
      return true;
    }
    return false;
  }

  // Logically erases an entity, adding it to the dirty list. Actually, it's
  // tombstoned into staging.
  bool erase_entity(EntityId id) {
    // Can't kill the dead.
    if (!scene_.registry().is_valid(id)) return false;
    (void)mark_dirty(id);
    // We don't actually erase it yet, just migrate it to staging so that we
    // can erase it as part of the snapshot process.
    return scene_.migrate_entity(id, staging_sid);
  }

  // Advance velocity-driven entities.
  [[nodiscard]] bool tick_movers() {
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
      (void)mark_dirty(id);

      pos.x += vel.vx;
      pos.y += vel.vy;

      bounce_from_boundary(pos.x, vel.vx, world_width);
      bounce_from_boundary(pos.y, vel.vy, world_height);

      updated_entities_.push_back(id);
      return true;
    });

    return true;
  }

  // Advance path-following enemies. Collect entities that changed, as well as
  // those that escaped the path.
  [[nodiscard]] bool tick_path_followers() {
    scene_.for_each<Position, PathFollower>([&](auto id, auto comps) {
      auto& [pos, pf] = comps;
      assert(pf.path_id < paths_.size_as_enum());
      if (pf.speed == 0.F) return true;

      (void)mark_dirty(id);

      pf.progress += pf.speed;

      const auto& sp = paths_[pf.path_id];
      if (pf.progress >= sp.total_length || pf.progress < 0.F) {
        path_escapees_.push_back(id);
        return true;
      }

      pos = sp.position_from_progress(pf.progress);
      return true;
    });

    return true;
  }

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
