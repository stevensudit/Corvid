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
#include <stdexcept>
#include <vector>

#include "../ecs.h"

// SimWorld: authoritative server-side world state and logic for the CorvidSim
// game. Owns the entities and the physics, but not the rules or the game flow.

// Architectural Notes:
// - Put all of the shapes and skins into a raw JSON file and serve it up. The
//   C++ code never reads it, but does reference IDs defined in it.
// - The shapes could, in principle, reference sprites, which are likewise
//   served up as one or more PNG files. Again, only the client has to care
//   about how to render, while the server focuses on physics, geometry, and
//   logic.
// - The definitions of enemies, towers, maps, and waves are kept in plain CSV
//   files, which we can parse with nothing more than a comma splitter. These
//   are deserialized into C++ structs at startup. We do not serve the CSV
//   files.
// - To aid this, we might change the default web server to skip over anything
//   with a leading dot, allowing us to store ".game-definitions.csv" even if
//   CSV files were enabled.
namespace corvid { inline namespace sim {

// ID of path.
enum class PathId : uint16_t {
  invalid = std::numeric_limits<uint16_t>::max()
};

// Tick for world state.
enum class WorldTick : uint64_t {
  invalid = std::numeric_limits<uint64_t>::max()
};

}} // namespace corvid::sim

template<>
constexpr auto corvid::enums::registry::enum_spec_v<corvid::sim::PathId> =
    corvid::enums::sequence::make_sequence_enum_spec<corvid::sim::PathId,
        "">();

template<>
constexpr auto corvid::enums::registry::enum_spec_v<corvid::sim::WorldTick> =
    corvid::enums::sequence::make_sequence_enum_spec<corvid::sim::WorldTick,
        "">();

namespace corvid { inline namespace sim {

// Conversions between Cartesian coordinates (x, y) and Euclidean vectors in
// polar form (length, direction). Direction is in radians, with 0 along the
// positive x-axis and increasing counter-clockwise.
namespace convert {
// Convert Cartesian coordinates Euclidean vector,
[[nodiscard]] constexpr std::pair<float, float>
CartesianToPolar(float x, float y) noexcept {
  float length = std::sqrt((x * x) + (y * y));
  float direction = std::atan2(y, x);
  return {length, direction};
}

// Convert a Euclidean vector to Cartesian coordinates.
[[nodiscard]] constexpr std::pair<float, float>
PolarToCartesian(float length, float direction) noexcept {
  float x = length * std::cos(direction);
  float y = length * std::sin(direction);
  return {x, y};
}
} // namespace convert

// Linear interpolation, where t is the interpolation factor in [0,1]. Returns
// a when t=0 and b when t=1. Can also be used for extrapolation when t is
// outside [0,1].
[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept {
  return a + ((b - a) * t);
}

// 2D Cartesian position component, in world-space pixels.
struct Position {
  float x{};
  float y{};

  static Position fromPolar(float radius, float angle) {
    auto [x, y] = convert::PolarToCartesian(radius, angle);
    return {x, y};
  }
};

// 2D Cartesian velocity component, in world-space pixels per frame.
struct Velocity {
  float vx{};
  float vy{};

  [[nodiscard]] static Velocity fromPolar(float speed, float angle) {
    auto [vx, vy] = convert::PolarToCartesian(speed, angle);
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
    float length;          // Euclidean distance from `front` to `back`.
    float cumulativeStart; // Sum of lengths of all preceding segments.
  };

  std::vector<Segment> segments;
  float totalLength{};
  float width{};

  // Map a distance traveled (`progress`) to a world-space `Position`.
  // If `previousProgress` was on an earlier segment, emit the joint position
  // first so fast-moving entities visibly take corners instead of cutting
  // across them between updates. `progress` is clamped to `[0, totalLength]`.
  // Returns the origin if `segments` is empty.
  [[nodiscard]] Position
  calculatePositionFromProgress(float progress, float previousProgress) const {
    if (segments.empty()) return {};
    progress = std::clamp(progress, 0.F, totalLength);

    // Find the last segment whose cumulativeStart <= progress.
    auto it = std::ranges::upper_bound(segments, progress, {},
        &SegmentedPath::Segment::cumulativeStart);
    if (it != segments.begin()) --it;
    const auto& seg = *it;

    if (previousProgress < seg.cumulativeStart) return seg.front;

    // Calculate the interpolation factor `t` along this segment, then lerp the
    // front and back positions to get the world position.
    float t = 0.F;
    if (seg.length > 0.F) t = (progress - seg.cumulativeStart) / seg.length;

    return {lerp(seg.front.x, seg.back.x, t),
        lerp(seg.front.y, seg.back.y, t)};
  }

  // Convert authored `PathJoints` into a `SegmentedPath`. Requires at
  // least two joints; returns an empty `SegmentedPath` if the input is
  // degenerate.
  static SegmentedPath fromJoints(const PathJoints& p) {
    if (p.joints.size() < 2) return {};
    SegmentedPath sp;
    sp.width = p.width;
    sp.segments.reserve(p.joints.size() - 1);
    float cumulative{};
    constexpr float half_w = 1920.F / 2.F;
    constexpr float half_h = 1080.F / 2.F;

    for (const auto& joint : p.joints) {
      const auto& pos = joint.p;
      // Check for configuration errors.
      if (pos.x < -half_w || pos.x > half_w || pos.y < -half_h ||
          pos.y > half_h)
        throw std::runtime_error("Path joint lies outside the world bounds");
    }

    for (std::size_t i = 0; i + 1 < p.joints.size(); ++i) {
      const Position& front = p.joints[i].p;
      const Position& back = p.joints[i + 1].p;
      const float dx = back.x - front.x;
      const float dy = back.y - front.y;
      const float len = std::hypot(dx, dy);
      sp.segments.push_back({front, back, len, cumulative});
      cumulative += len;
    }
    sp.totalLength = cumulative;
    return sp;
  }
};

using Tick = uint64_t;
constexpr Tick invalidTick = std::numeric_limits<Tick>::max();

// TODO: We likely want to break out the physics-relevant properties into
// components. We already have Position and Velocity (as well as PathFollower,
// which is a type of velocity). We may need a Shape component which has the
// geometric type (square, circle, etc), its scale, radius, length, width,
// etc., and local basis (orientation). This can be used to compute the
// bounding box (which could be AABB (axis-aligned bounding box) or OBB
// (oriented bounding box) or even a circle collider (super-fast for towers))
// for collision detection and hit box purposes. We may also need an Aura for
// field effects. Rendering cares about the origin/anchor point of the shape,
// which may not be the center. Towers may have an Aura (radius, damage,
// dmgtype) or ProjectileRange (), or both. We could use Components for Hitbox,
// Hurtbox, Attack (dmg, knockback, statusEffect), Expiry (ttl in ticks).
// For circle hitboxes, collision is trivial: when the distance between centers
// is less than the sum of the radii, they collide.

// Path-following component. Stored alongside `Position` in the enemy
// archetype. Each tick, `progress` advances by `speed`; the entity's
// `Position` is re-derived from the segmented path geometry.
struct PathFollower {
  PathId pathId{};  // Index into `sim_world::paths_`.
  float progress{}; // Distance traveled along the path so far.
  float speed{};    // Distance per tick.
};

// Appearance component. Controls rendering on client side, but has no effect
// on physics or game logic.
struct Appearance {
  char32_t glyph{};      // a Unicode character to display, if any.
  float scale{1.F};      // multiplier on the base size of the shape, if any.
  uint32_t fg_color{};   // RGB color, if any.
  uint32_t bg_color{};   // RGB color, if any.
  uint32_t glow_color{}; // RGB color for glow effect, if any.
  WorldTick effect_expiry{WorldTick::invalid}; // When glow effect expires.
};

// Defensive tower component. Stored alongside `Position` in the tower
// archetype.
//
// Note that towers sitting in the catalog or being dragged and dropped into
// place are not entities, just client-side UI ephemera. Only once a tower is
// placed does it become part of the world.
struct Tower {
  int tower_type{};
  float attack_radius{};
  float attack_damage{};   // Per-attack damage.
  WorldTick cooldown{};    // Cooldown for this sort of tower, in ticks.
  WorldTick next_attack{}; // Tick when the next attack can occur.
};

// Enemy invader component. Stored alongside `Position` and `PathFollower` in
// the enemy archetype.
struct Invader {
  float health{};
  float radius{}; // Distinct from appearance: this is used for hit detection.
  int bounty{1};  // Resources awarded to the player for killing this enemy.
};

// Bullet component. Stored alongside `Position` and `Velocity` in the bullet
// archetype.
struct Bullet {
  int bullet_type{};
  float damage{};
  WorldTick expiration{};
};

// ECS types for the simulation world.
//
// Registry metadata (`uint64_t`) stores each entity's last-change tick count,
// enabling delta snapshots without a separate dirty set.
//
// Storage layout (store_id assignment is positional, 1-based):
//   `sidStaging` = 0                      : staging storage, used as tombstone
//
//   `sidPos`     = 1  -> `arch_p_t`       : background / destructible entities
//                                           (Position)
//
//   `sidPosVel`  = 2  -> `arch_pv_t`      : moving entities
//                                           (Position + Velocity)
//
//   `sidEnemy`   = 3  -> `arch_enemy_t`   : path-following enemies
//                                           (Position + Appearance +
//                                           PathFollower + Invader)
//
//   `sidTower`   = 4  -> `arch_tower_t`   : placed towers
//                                          (Position + Appearance + Tower)
//
//   `sidBullet`  = 5  -> `arch_bullet_t`  : bullets
//                                           (Position + Velocity + Bullet)
//
// Background storage comes first so that retiring a mobile entity is a
// natural `archetype_scene::migrate_entity` call rather than erase + re-add.
using WorldReg = entity_registry<WorldTick>;
using WorldSid = WorldReg::store_id_t;
using ArchP = archetype_storage<WorldReg, std::tuple<Position>>;
using ArchPV = archetype_storage<WorldReg, std::tuple<Position, Velocity>>;
using ArchEnemy = archetype_storage<WorldReg,
    std::tuple<Position, Appearance, PathFollower, Invader>>;
using ArchTower =
    archetype_storage<WorldReg, std::tuple<Position, Appearance, Tower>>;
using ArchBullet =
    archetype_storage<WorldReg, std::tuple<Position, Velocity, Bullet>>;
using WorldScene =
    archetype_scene<WorldReg, ArchP, ArchPV, ArchEnemy, ArchTower, ArchBullet>;

// Simulation world: encapsulates all ECS entity state for the game and
// provides physics.
//
// Each `tick()` advances `Position` components based on
// by velocity and bounces off the world boundary. The registry metadata
// records the tick count at each entity's last state change so callers can
// request delta snapshots starting from any past tick.
class SimWorld {
public:
  using EntityId = WorldReg::id_t;
  using Handle = WorldReg::handle_t;

  static constexpr WorldSid sidStaging{0};
  static constexpr WorldSid sidPos{1};
  static constexpr WorldSid sidPosVel{2};
  static constexpr WorldSid sidEnemy{3};
  static constexpr WorldSid sidTower{4};
  static constexpr float widthOfWorld = 1920.F;
  static constexpr float heightOfWorld = 1080.F;

  void clear() {
    scene_.clear();
    paths_.clear();
    tick_ = {};
  }

  // Spawn a moving entity with the given initial position and velocity.
  // Returns a handle for later `despawn()`. The entity's last-change tick
  // is set to the current tick count.
  [[nodiscard]] Handle spawnMover(Position pos, Velocity vel) {
    auto h =
        scene_.store_new_entity<sidPosVel>({WorldTick::invalid}, pos, vel);
    if (h) (void)markDirty(h.id());
    return h;
  }

  // Spawn an immobile entity. Returns a handle for later `despawn()`.
  [[nodiscard]] Handle spawnBackground(Position pos) {
    auto h = scene_.store_new_entity<sidPos>({WorldTick::invalid}, pos);
    if (h) (void)markDirty(h.id());
    return h;
  }

  // Bake and store a path. Returns the index used as `path_follower::path_id`.
  // The index is stable for the lifetime of the world.
  [[nodiscard]] PathId addPath(const PathJoints& p) {
    if (!paths_.push_back(SegmentedPath::fromJoints(p)))
      return PathId::invalid;
    return paths_.size_as_enum() - 1;
  }

  // Return a pointer to the baked path at `id`, or `nullptr` if out of range.
  [[nodiscard]] const SegmentedPath* getPath(PathId pathId) const {
    if (pathId >= paths_.size_as_enum()) return nullptr;
    return &paths_[pathId];
  }

  // Spawn a path-following enemy. Initial position is derived from `progress`
  // on the named path. Returns a handle for later `despawn()`.
  [[nodiscard]] Handle
  spawnEnemy(PathId pathId, float speed, float progress = 0.F) {
    if (pathId >= paths_.size_as_enum()) return {};
    Appearance app{U'U', 2.F, 0xFFFFFFFF, 0xFF, 0, WorldTick::invalid};
    const auto pos =
        paths_[pathId].calculatePositionFromProgress(progress, progress);
    return scene_.store_new_entity<sidEnemy>(tick_, pos, app,
        PathFollower{pathId, progress, speed}, Invader{});
  }

  // Advance one simulation frame. Sets each changed entity's registry metadata
  // to the new tick count and tracks changed entities for callbacks.
  [[nodiscard]] WorldTick tick() {
    ++tick_;

    (void)updateMovers();
    (void)updatePathFollowers();

    return tick_;
  }

  // Total number of entities in all storages (does not count staged entities).
  [[nodiscard]] std::size_t size() const { return scene_.size(); }

  // Destructively extract upserts and erasures.
  //
  // Call back `cbUpserts(EntityId, Position, Appearance)` for each changed
  // entity that has a `Position` and `Appearance` and has changed since the
  // last tick, and `cbErased(EntityId)` for each entity that has been erased
  // since the last tick. These calls will be interleaved.
  [[nodiscard]] bool
  extractUpdatedEntities(auto&& cbUpserts, auto&& cbErased) {
    auto& reg = scene_.registry();
    for (auto id : updatedEntities_) {
      const auto [pos, app] =
          scene_.try_get_components<Position, Appearance>(id);
      if (pos && app) {
        (void)cbUpserts(id, *pos, *app);
      } else {
        if (reg.get_location(id).store_id != sidStaging) continue;
        (void)cbErased(id);
        (void)scene_.erase_entity(id);
      }
    }
    updatedEntities_.clear();
    return true;
  }

  // Call back `extractPath(pathId, Position)` for all joints of the path
  // identified by `pathId`.
  [[nodiscard]] bool obtainPath(auto&& cbPath, PathId pathId) const {
    if (pathId >= paths_.size_as_enum()) return false;
    const auto& sp = paths_[pathId];
    for (const auto& seg : sp.segments) cbPath(pathId, Position{seg.front});
    if (!sp.segments.empty())
      cbPath(pathId, Position{sp.segments.back().back});
    return true;
  }

  // Call back `cbPath(pathId, Position)` for all joints of all paths.
  [[nodiscard]] bool obtainPaths(auto&& cbPath) const {
    for (PathId pathId{0}; pathId < paths_.size_as_enum(); ++pathId)
      if (!obtainPath(cbPath, pathId)) return false;
    return true;
  }

  // Resolve path escapees by calling `cbEscapee(EntityId, const Position&,
  // const PathFollower&)` for each.
  //
  // Destructively iterates through the list of path escapees. If `cbEscapee`
  // returns true, the escapee is erased; if false, it's left alive, so the
  // caller ought to have done something with it, such as migrating it to a
  // different path.
  [[nodiscard]] bool resolveEscapees(auto&& cbEscapee) {
    for (auto id : pathEscapees_) {
      if (!scene_.registry().is_valid(id)) continue;
      auto [pos, pf] = scene_.try_get_components<Position, PathFollower>(id);
      if (!pos || !pf) continue;
      if (!cbEscapee(id, *pos, *pf)) continue;
      (void)tombstoneEntity(id);
    }
    pathEscapees_.clear();
    return true;
  }

  // Mark all entities dirty, to force a full snapshot.
  [[nodiscard]] bool markAllDirty() {
    scene_.registry().for_each([&](auto id, auto&) {
      const auto loc = scene_.registry().get_location(id);
      if (loc.store_id == sidStaging) return true;

      (void)markDirty(id);
      return true;
    });
    return true;
  }

private:
  WorldScene scene_;
  WorldTick tick_{};
  id_container<SegmentedPath, PathId> paths_;

  std::vector<EntityId> updatedEntities_;
  std::vector<EntityId> pathEscapees_;

  // Marks an entity as dirty so that it will be included in the next delta
  // snapshot.
  [[nodiscard]] bool markDirty(EntityId id) {
    // If it's been deleted, it's already dirty.
    if (!scene_.registry().is_valid(id)) return false;
    auto& last_updated = scene_.registry()[id];
    if (last_updated != tick_) {
      last_updated = tick_;
      updatedEntities_.push_back(id);
      return true;
    }
    return false;
  }

  // Logically erases an entity, adding it to the dirty list.
  [[nodiscard]] bool tombstoneEntity(EntityId id) {
    // Can't kill the dead.
    if (!scene_.registry().is_valid(id)) return false;
    (void)markDirty(id);
    return scene_.remove_entity(id);
  }

  // Advance velocity-driven entities.
  [[nodiscard]] bool updateMovers() {
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
      (void)markDirty(id);

      pos.x += vel.vx;
      pos.y += vel.vy;

      bounce_from_boundary(pos.x, vel.vx, widthOfWorld);
      bounce_from_boundary(pos.y, vel.vy, heightOfWorld);
      return true;
    });

    return true;
  }

  // Advance path-following enemies. Collect entities that changed, as well as
  // those that escaped the path.
  [[nodiscard]] bool updatePathFollowers() {
    scene_.for_each<Position, PathFollower>([&](auto id, auto comps) {
      auto& [pos, pf] = comps;
      assert(pf.pathId < paths_.size_as_enum());
      if (pf.speed == 0.F) return true;

      (void)markDirty(id);

      const float previousProgress = pf.progress;
      pf.progress += pf.speed;
      const auto& sp = paths_[pf.pathId];

      // Collect escapees.
      if (pf.progress >= sp.totalLength || pf.progress < 0.F) {
        pathEscapees_.push_back(id);
        return true;
      }

      pos = sp.calculatePositionFromProgress(pf.progress, previousProgress);
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
