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
#include <string>
#include <string_view>
#include <vector>

#include "../containers/transparent.h"
#include "../containers/opt_find.h"

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
enum class WorldTick : uint32_t {
  invalid = std::numeric_limits<uint32_t>::max()
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
// geometric type (square, circle, etc), its size, radius, length, width,
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

// Path-following component. Typically part of an invader archetype, but may
// also apply to defenders. On each tick, `progress` advances by `speed`; the
// entity's `Position` is re-derived from the segmented path geometry.
struct Pathing {
  PathId pathId{};  // Index into `sim_world::paths_`.
  float progress{}; // Distance traveled along the path so far.
  float speed{};    // Distance per tick.
};

// Appearance component. Controls rendering on client side, but has no effect
// on physics or game logic.
// TODO: Add field for sprite selection.
struct Appearance {
  WorldTick modified{WorldTick::invalid}; // Tick when last modified.
  char32_t glyph{};     // A Unicode character to display, if any.
  float radius{5.F};    // World-space radius of the rendered shape.
  uint32_t fgColor{};   // RGBA.
  uint32_t bgColor{};   // RGBA.
  float attackRadius{}; // Display-only (world units).
};

// Health component. Applies to both defenders and invaders. The two health
// fields are streamed to the client in order to render health bars. The
// `regen` is server-side only.
struct Health {
  WorldTick modified{WorldTick::invalid}; // Tick when last modified.
  float currentHealth{};
  float maxHealth{};
  float regen{}; // Regeneration or bleed per tick.
};

// Visual effects component. Controls transient overlays on the client side,
// but has no effect on physics or game logic.
struct VisualEffects {
  WorldTick modified{WorldTick::invalid}; // Tick when last modified.
  uint32_t selectionColor{};              // RGBA.
  float rangeRadius{};
  uint32_t rangeColor{}; // RGBA.
  uint32_t flashColor{}; // RGBA.
  WorldTick flashExpiry{WorldTick::invalid};
};

// Defensive tower component, common across all defenders.
struct Defender {
  int defenderType{};      // Eventually an enum.
  float hitCircleRadius{}; // Hit detection, as opposed to appearance.
  float attackRadius{};
  uint32_t rangeColor{}; // RGBA.
  float attackDamage{};  // Interpretation depends on the tower type.
  WorldTick cooldown{};
  WorldTick nextAttack{}; // Updated when the tower attacks.
};

// Stats for defenders, shown when selecting a defender.
struct DefenderStats {
  float totalDamageDealt{};
  float totalDamageTaken{};
  float totalKills{};
};

// Area-of-effect component for defenders that have an attack that hits an area
// rather than a single target.
struct DefenderAoe {
  int damageType{}; // Eventually an enum.
};

// Hitscan component for defenders that have an attack that hits a single
// target instantly, rather than spawning a projectile that travels to the
// target.
struct DefenderHitscan {
  uint32_t beamColor{}; // RGBA.
  WorldTick beamDuration{};
  int beamType{}; // Eventually an enum.
};

// Projectile component for `DefenderShooter`. Used as part of its own
// archetype, and also as the `bullet_template`. As a template, the `expiry`
// field stores the TTL. Once instantiated, added to the current tick to gets
// its final moment.
struct DefenderBullet {
  float speed{};
  float directDamage{};
  float damageOverTime{};
  float splashRadius{};
  float directDamageType{}; // Eventually an enum.
  float dotDamageType{};    // Eventually an enum.
  int projectileType{};     // Eventually an enum.
  WorldTick expiry{};
};

// Shooter component for defenders that spawn projectiles. The
// `bullet_template` is used to spawn bullets with the same properties as the
// tower's attack, but with their own position and velocity.
struct DefenderShooter {
  DefenderBullet bullet_template{};
  float fireRate{}; // Shots per tick.
  int spread{};     // Eventually an enum.
};

// Invader component, shared across all invaders.
struct Invader {
  int invaderType{};       // Eventually an enum.
  float hitCircleRadius{}; // Hit detection, as opposed to appearance.
  int bounty{10}; // Resources awarded to the player for killing this enemy.
};

// ECS types for the simulation world.
//
// Registry metadata (`uint64_t`) stores each entity's last-change tick count,
// enabling delta snapshots without a separate dirty set.
//
// Storage layout (store_id assignment is positional, 1-based):
//   `sidStaging`         = 0 -> staging storage, used as tombstone
//
//   `sidInvaderAlpha`    = 1 -> `ArchInvaderAlpha`
//                               alpha invaders
//                               (Position + Appearance + VisualEffects +
//                               Pathing + Invader + Health)
//
//   `sidDefenderAoe`     = 2 -> `ArchDefenderAoe`
//                               area-of-effect defenders
//                               (Position + Appearance + VisualEffects +
//                               Defender + DefenderStats + Health +
//                               DefenderAoe)
//
//   `sidBullet`          = 3 -> `ArchBullet`
//                               spawned projectiles
//                               (Position + Velocity + DefenderBullet)
//
//   `sidDefenderShooter` = 4 -> `ArchDefenderShooter`
//                               projectile-firing defenders
//                               (Position + Appearance + VisualEffects +
//                               Defender + DefenderStats + Health +
//                               DefenderShooter)
using WorldReg = entity_registry<WorldTick>;
using WorldSid = WorldReg::store_id_t;
using ArchInvaderAlpha = archetype_storage<WorldReg,
    std::tuple<Position, Appearance, VisualEffects, Pathing, Invader, Health>>;
using ArchDefenderAoe = archetype_storage<WorldReg,
    std::tuple<Position, Appearance, VisualEffects, Defender, DefenderStats,
        Health, DefenderAoe>>;
using ArchBullet = archetype_storage<WorldReg,
    std::tuple<Position, Velocity, DefenderBullet>>;
using ArchDefenderShooter = archetype_storage<WorldReg,
    std::tuple<Position, Appearance, VisualEffects, Defender, DefenderStats,
        Health, DefenderShooter>>;
using WorldScene = archetype_scene<WorldReg, ArchInvaderAlpha, ArchDefenderAoe,
    ArchBullet, ArchDefenderShooter>;

// Simulation world: encapsulates all ECS entity state for the game and
// provides physics.
//
// Each `tick()` advances `Position` components based on velocity and bounces
// off the world boundary. The registry metadata records the tick count at each
// entity's last state change so callers can request delta snapshots starting
// from any past tick.
class SimWorld {
public:
  using EntityId = WorldReg::id_t;
  using Handle = WorldReg::handle_t;

  static constexpr WorldSid sidStaging{0};
  static constexpr WorldSid sidInvaderAlpha{1};
  static constexpr WorldSid sidDefenderAoe{2};
  static constexpr WorldSid sidBullet{3};
  static constexpr WorldSid sidDefenderShooter{4};
  static constexpr float widthOfWorld = 1920.F;
  static constexpr float heightOfWorld = 1080.F;

  void clear() {
    scene_.clear();
    paths_.clear();
    updatedEntities_.clear();
    pathEscapees_.clear();
    tick_ = {};
  }

  // Register a named entity type from a `megatuple_t` template. The set of
  // optionals that have values must exactly match one archetype's components.
  // Templates survive `clear()` and are only replaced by calling this method
  // again with the same label.
  void registerEntity(std::string label, WorldScene::megatuple_t tpl) {
    entity_templates_.insert_or_assign(std::move(label), std::move(tpl));
  }

  // Spawn an entity by label. Looks up the pre-registered template, stamps
  // `tick_` into any `modified` fields, marks the entity dirty, and returns
  // its handle. Returns an invalid handle if the label is unknown or the
  // template bitmap does not match any archetype.
  [[nodiscard]] Handle spawnEntity(std::string_view label) {
    auto megatuple = find_opt(entity_templates_, label);
    if (!megatuple) return {};
    auto h =
        scene_.store_new_entity_from_mega({WorldTick::invalid}, *megatuple);
    if (!h) return {};
    // Stamp the current tick into modification-tracking fields.
    auto [app, fx, hp] =
        scene_.try_get_some_components<Appearance, VisualEffects, Health>(
            h.id());
    if (app) app->modified = tick_;
    if (fx) fx->modified = tick_;
    if (hp) hp->modified = tick_;
    (void)markDirty(h.id());
    return h;
  }

  // Return a pointer to component `C` for entity `id`, or `nullptr` if the
  // entity does not carry that component.
  template<typename C>
  [[nodiscard]] C* try_get_component(EntityId id) noexcept {
    return scene_.try_get_component<C>(id);
  }

  // Return a tuple of pointers to components `Cs...` for entity `id`, or a
  // tuple of `nullptr`s if the entity does not carry all of those components.
  template<typename... Cs>
  [[nodiscard]] auto try_get_components(EntityId id) noexcept {
    return scene_.try_get_components<Cs...>(id);
  }

  // Bake and store a path. Returns the index used as `path_follower::pathId`.
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

  // Obtain a pointer to the `Appearance` for the entity. It is already marked
  // dirty and `modified`, so the caller only needs to change the other fields.
  [[nodiscard]] Appearance* changeAppearance(EntityId id) {
    auto* app = scene_.try_get_component<Appearance>(id);
    if (!app) return nullptr;
    app->modified = tick_;
    (void)markDirty(id);
    return app;
  }

  // Obtain a pointer to the `VisualEffects` for the entity. It is already
  // marked dirty and `modified`, so the caller only needs to change the other
  // fields.
  [[nodiscard]] VisualEffects* changeVisualEffects(EntityId id) {
    auto* effects = scene_.try_get_component<VisualEffects>(id);
    if (!effects) return nullptr;
    effects->modified = tick_;
    (void)markDirty(id);
    return effects;
  }

  // Trigger or replace a flashing overlay for an entity that carries visual
  // effects. The flash remains active until a later update or expiry clears
  // it.
  [[nodiscard]] bool flashEntity(EntityId id, uint32_t color,
      WorldTick duration = WorldTick{20}) {
    auto* effects = changeVisualEffects(id);
    if (!effects) return false;
    effects->flashColor = color;
    effects->flashExpiry = WorldTick{*tick_ + *duration};
    return true;
  }

  // Return a generation-versioned handle for `id`, suitable for storage across
  // ticks, or whenever there is a risk of the underlying entity going away.
  // Use `getId` to get the ID back, just in time.
  [[nodiscard]] Handle getHandle(EntityId id) const {
    return scene_.registry().get_handle(id);
  }

  // Return the entity ID for `handle`, or `EntityId::invalid` if the handle is
  // invalid.
  [[nodiscard]] EntityId getId(Handle h) const {
    return scene_.registry().id_from_handle(h);
  }

  [[nodiscard]] EntityId findEntityAt(const Position& pos) const {
    EntityId found_id = EntityId::invalid;
    scene_.for_each<Position, Appearance>([&](auto id, auto comps) {
      const auto& [epos, app] = comps;
      const auto radius = app.radius;
      // If point outside of bounding box, keep searching.
      if (std::abs(epos.x - pos.x) >= radius ||
          std::abs(epos.y - pos.y) >= radius)
        return true;

      found_id = id;
      return false;
    });

    return found_id;
  }

  [[nodiscard]] auto getTower(EntityId id) {
    return scene_
        .try_get_components<Position, Appearance, VisualEffects, Defender>(id);
  }

  [[nodiscard]] EntityId findTowerAt(const Position& pos) const {
    EntityId found_id = EntityId::invalid;
    scene_.for_each<Position, Appearance, Defender>([&](auto id, auto comps) {
      const auto& [epos, app, _] = comps;
      const auto radius = app.radius;
      // If point outside of bounding box, keep searching.
      if (std::abs(epos.x - pos.x) >= radius ||
          std::abs(epos.y - pos.y) >= radius)
        return true;

      found_id = id;
      return false;
    });

    return found_id;
  }

  // Run all physics for the current tick without advancing the counter. Call
  // `tick()` once all game logic for this frame is complete, including
  // streaming the state to clients.
  [[nodiscard]] bool next() {
    (void)updateMovers();
    (void)updatePathFollowers();
    (void)towersAttack();
    return true;
  }

  // Advance the tick counter and return the new value. Call this at the end
  // of each frame, after `next()`, all game-level logic, and streaming have
  // run.
  [[nodiscard]] WorldTick tick() { return ++tick_; }

  // Return the current tick counter without advancing it.
  [[nodiscard]] WorldTick currentTick() const { return tick_; }

  // Total number of entities in all storages (does not count staged entities).
  [[nodiscard]] std::size_t size() const { return scene_.size(); }

  // Destructively extract upserts and erasures.
  //
  // Call back `cbUpserts(EntityId, Position, Appearance, VisualEffects)` for
  // each changed entity that has a `Position` and `Appearance` and has changed
  // since the last tick, and `cbErased(EntityId)` for each entity that has
  // been erased since the last tick. The visual effects pointer is null for
  // archetypes that do not carry that component. These calls will be
  // interleaved.
  [[nodiscard]] bool
  extractUpdatedEntities(auto&& cbUpserts, auto&& cbErased) {
    static constexpr VisualEffects nfx;
    auto& reg = scene_.registry();
    for (auto id : updatedEntities_) {
      if (!reg.is_valid(id)) continue;
      const auto [pos, app] =
          scene_.try_get_components<Position, Appearance>(id);
      if (pos && app) {
        const auto* fx = &nfx;
        auto maybeFx = scene_.try_get_component<VisualEffects>(id);
        if (maybeFx) fx = maybeFx;
        (void)cbUpserts(id, *pos, *app, *fx);
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
  // const Pathing&)` for each.
  //
  // Destructively iterates through the list of path escapees. If `cbEscapee`
  // returns true, the escapee is erased; if false, it's left alive, so the
  // caller ought to have done something with it, such as migrating it to a
  // different path.
  [[nodiscard]] bool resolveEscapees(auto&& cbEscapee) {
    for (auto id : pathEscapees_) {
      if (!scene_.registry().is_valid(id)) continue;
      auto [pos, pf] = scene_.try_get_components<Position, Pathing>(id);
      if (!pos || !pf) continue;
      if (!cbEscapee(id, *pos, *pf)) continue;
      (void)tombstoneEntity(id);
    }
    pathEscapees_.clear();
    return true;
  }

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

  // Mark all entities dirty. When `update_strategy::incremental`, marks
  // entities for extraction only. When `update_strategy::full`, also stamps
  // `Appearance` and `VisualEffects` `modified` fields with `tick_` so the
  // next extraction includes them. Must be called before `tick()` so
  // `markDirty`'s deduplication check (`last_updated == tick_`) is valid.
  [[nodiscard]] bool markAllDirty(
      update_strategy strategy = update_strategy::incremental) {
    scene_.registry().for_each([&](auto id, auto&) {
      const auto loc = scene_.registry().get_location(id);
      if (loc.store_id == sidStaging) return true;

      (void)markDirty(id);
      if (strategy == update_strategy::incremental) return true;

      if (auto app = scene_.try_get_component<Appearance>(id))
        app->modified = tick_;
      if (auto effects = scene_.try_get_component<VisualEffects>(id))
        effects->modified = tick_;
      return true;
    });
    return true;
  }

  static constexpr float
  distanceSquared(const Position& a, const Position& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
  }

  static constexpr bool circlesOverlap(const Position& posA, float radiusA,
      const Position& posB, float radiusB) {
    const float r = radiusA + radiusB;
    return distanceSquared(posA, posB) <= r * r;
  }

private:
  WorldScene scene_;
  WorldTick tick_{};
  id_container<SegmentedPath, PathId> paths_;

  std::vector<EntityId> updatedEntities_;
  std::vector<EntityId> pathEscapees_;

  // Entity type definitions: label -> component template. Survives `clear()`.
  string_unordered_map<WorldScene::megatuple_t> entity_templates_;

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

      bounceFromBoundary(pos.x, vel.vx, widthOfWorld);
      bounceFromBoundary(pos.y, vel.vy, heightOfWorld);
      return true;
    });

    return true;
  }

  // Advance path-following enemies. Collect entities that changed, as well as
  // those that escaped the path.
  [[nodiscard]] bool updatePathFollowers() {
    scene_.for_each<Position, Pathing>([&](auto id, auto comps) {
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

  [[nodiscard]] bool towersAttack() {
    // Range over all towers. For each tower, range over all enemies and check
    // for hits. If an enemy is in range and the tower is off cooldown, apply
    // damage and trigger a flash on both.
    scene_.for_each<Position, Defender>([&](auto towerId, auto towerComps) {
      auto& [towerPos, tower] = towerComps;
      if (tick_ < tower.nextAttack) return true;

      scene_.for_each<Position, Invader>([&](auto enemyId, auto enemyComps) {
        auto& [enemyPos, invader] = enemyComps;
        if (!circlesOverlap(towerPos, tower.attackRadius, enemyPos,
                invader.hitCircleRadius))
          return true;

        (void)flashEntity(towerId, 0xFFFFFFFF, WorldTick{5});
        (void)flashEntity(enemyId, 0xFF7F7FFF, WorldTick{5});
        return true;
#if 0
        invader.health -= tower.attack_damage;
        tower.next_attack = WorldTick{*tick_ + *tower.cooldown};
        (void)flashEntity(towerId, 0xFFFFFFFF);
        if (invader.health <= 0.F) {
          (void)tombstoneEntity(enemyId);
        } else {
          (void)flashEntity(enemyId, 0xFF0000FF);
        }
        return false; // Stop after first hit per tower per tick.
#endif
      });

      return true;
    });

    return true;
  }

  [[nodiscard]] static constexpr bool isVisibleColor(uint32_t color) noexcept {
    return (color & 0xFFU) != 0U;
  }

  // Clamp `pos` to `[-limit/2, +limit/2]` and, if it was out of range, negate
  // `vel` so the entity bounces off that boundary wall.
  // TODO: Is there an off-by-one error here? If the world is 1920 wide, the
  // actual range isn't [-960, +960], it's [-960, +960). Do we need to deal
  // with this? It depends on how the client scales it, right?
  static void bounceFromBoundary(float& pos, float& vel, float limit) {
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
