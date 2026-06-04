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
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../containers/core/transparent.h"
#include "../containers/core/opt_find.h"

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
// - The definitions of invaders, defenders, maps, and waves are kept in plain
//   CSV files, which we can parse with nothing more than a comma splitter.
//   These are deserialized into C++ structs at startup. We do not serve the
//   CSV files.
// - To aid this, we might change the default web server to skip over anything
//   with a leading dot, allowing us to store ".game-definitions.csv" even if
//   CSV files were enabled.
namespace corvid { inline namespace sim {

// ID of path.
enum class PathId : uint16_t {
  invalid = std::numeric_limits<uint16_t>::max()
};
consteval auto corvid_enum_spec(PathId*) {
  return corvid::enums::sequence::make_sequence_enum_spec<PathId, "">();
}

// Tick for world state.
enum class WorldTick : uint32_t {
  invalid = std::numeric_limits<uint32_t>::max()
};
consteval auto corvid_enum_spec(WorldTick*) {
  return corvid::enums::sequence::make_sequence_enum_spec<WorldTick, "">();
}

// Targeting modes for defenders.
enum class TargetMode : uint8_t { first, last, closest, strongest, weakest };
consteval auto corvid_enum_spec(TargetMode*) {
  return corvid::enums::sequence::make_sequence_enum_spec<TargetMode,
      "first,last,closest,strongest,weakest">();
}

// Conversions between Cartesian coordinates (x, y), and Euclidean vectors
// represented in polar form (length, direction). Direction is in radians, with
// 0 along the positive x-axis and increasing counter-clockwise.
namespace convert {
// Convert Cartesian coordinates to a Euclidean vector.
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

// Linear interpolation, where `t` is the interpolation factor in `[0,1]`.
// Returns a when `t==0` and b when `t==1`. Can also be used for extrapolation
// when `t` is outside this range.
[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept {
  return a + ((b - a) * t);
}

// 2D Cartesian position component, in world-space pixels.
struct Position {
  float x{};
  float y{};

  [[nodiscard]] static Position fromPolar(float radius, float angle) {
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

// Circular hitbox, defined by a center position and a radius.
struct Circle: public Position {
  float radius{};
};

// Boundaries of the simulation world, where `(0,0)` is the center.
struct SimWorldBounds {
  using WorldReg = entity_registry<WorldTick>;
  using WorldSid = WorldReg::store_id_t;
  using EntityId = WorldReg::id_t;
  using Handle = WorldReg::handle_t;
  static constexpr float widthOfWorld = 1920.F;
  static constexpr float heightOfWorld = 1080.F;
  static constexpr float half_w = widthOfWorld * 0.5F;
  static constexpr float half_h = heightOfWorld * 0.5F;

  // Whether the position is within the bounds of the world.
  [[nodiscard]] static constexpr bool isInBounds(const Position& pos) {
    return pos.x >= -half_w && pos.x <= half_w && pos.y >= -half_h &&
           pos.y <= half_h;
  }

  // Whether a radius around the position is within the bounds of the world.
  [[nodiscard]] static constexpr bool
  isInBounds(const Position& pos, float radius) {
    return pos.x - radius >= -half_w && pos.x + radius <= half_w &&
           pos.y - radius >= -half_h && pos.y + radius <= half_h;
  }

  // Clamp `pos` to `[-limit/2, +limit/2]` and, if it was out of range,
  // negate `vel` so the entity bounces off that boundary wall. Return whether
  // it was in bounds.
  // TODO: Is there an off-by-one error here? If the world is 1920 wide, the
  // actual range isn't [-960, +960], it's [-960, +960). Do we need to deal
  // with this? It depends on how the client scales it, right?
  [[nodiscard]] constexpr static bool
  bounceFromBoundary(float& pos, float& vel, float limit) {
    const float half = limit / 2.F;
    if (pos < -half) {
      pos = -half;
      vel = -vel;
      return false;
    } else if (pos > half) {
      pos = half;
      vel = -vel;
      return false;
    }
    return true;
  }
};

// --- Path geometry ---

// `PathJoints` defines a path in terms of the coordinates of its joints. This
// is the authored path, a compact format suitable for defining the path, but
// not ideal for runtime use, which is why it's converted to `SegmentedPath`.
struct PathJoints {
  struct Joint {
    Position pos;
  };

  std::vector<Joint> joints;
  float width{40.F};
};

// `SegmentedPath` is built from a `PathJoints` and consists of pre-computed
// segments with cumulative distances and angles, enabling efficient runtime
// mapping from distance traveled to world position. Essentially, it's an
// indexed vertex buffer for the path geometry.
struct SegmentedPath: private SimWorldBounds {
  struct Segment {
    Position front;
    Position back;
    float length;          // Euclidean distance from `front` to `back`.
    float cumulativeStart; // Sum of lengths of all preceding segments.
    float angle{};         // Direction from `front` to `back`, in radians.
  };

  std::vector<Segment> segments;
  float totalLength{};
  float width{};

  // Return the segment that contains `progress`, clamping `progress` to
  // `[0, totalLength]` in place.
  [[nodiscard]] const Segment& segmentAtProgress(float& progress) const {
    assert(segments.size());
    progress = std::clamp(progress, 0.F, totalLength);
    auto it = std::ranges::upper_bound(segments, progress, {},
        &Segment::cumulativeStart);
    if (it != segments.begin()) --it;
    return *it;
  }

  // Map `progress`, a distance traveled, to a world-space `Position`.
  // `progress` is clamped to `[0, totalLength]`.
  [[nodiscard]] Position
  calculatePositionFromProgress(float progress, float previousProgress) const {
    const auto& seg = segmentAtProgress(progress);

    // If `previousProgress` was on an earlier segment, emit the joint position
    // first, so that fast-moving entities visibly take corners instead of
    // cutting across them between updates.
    if (previousProgress < seg.cumulativeStart) return seg.front;

    // Calculate the interpolation factor `t` along this segment, then lerp the
    // front and back positions to get the world position.
    float t = 0.F;
    if (seg.length > 0.F) t = (progress - seg.cumulativeStart) / seg.length;

    return {lerp(seg.front.x, seg.back.x, t),
        lerp(seg.front.y, seg.back.y, t)};
  }

  // Return the direction angle (radians) of the segment containing
  // `progress`.
  [[nodiscard]] float angleAtProgress(float progress) const {
    assert(segments.size());
    return segmentAtProgress(progress).angle;
  }

  // Convert authored `PathJoints` into a `SegmentedPath`.
  [[nodiscard]] static SegmentedPath fromJoints(const PathJoints& p) {
    assert(p.joints.size() >= 2);

    SegmentedPath sp;
    sp.width = p.width;
    sp.segments.reserve(p.joints.size() - 1);
    float cumulative{};

    for (std::size_t i = 0; i + 1 < p.joints.size(); ++i) {
      const Position& front = p.joints[i].pos;
      const Position& back = p.joints[i + 1].pos;
      assert(isInBounds(front));
      assert(isInBounds(back));

      const float dx = back.x - front.x;
      const float dy = back.y - front.y;
      const float len = std::hypot(dx, dy);
      assert(len != 0.F);
      const float angle = std::atan2(dy, dx);

      sp.segments.push_back({front, back, len, cumulative, angle});
      cumulative += len;
    }
    sp.totalLength = cumulative;
    return sp;
  }
};

// Path-following component. Typically part of an invader archetype, but may
// in principle apply to defenders. On each tick, `progress` advances by
// `speed`; the entity's `Position` is re-derived from the segmented path
// geometry.
struct Pathing {
  PathId pathId{};  // Index into `sim_world::paths_`.
  float progress{}; // Distance traveled along the path so far.
  float speed{};    // Distance per tick.
};

// Appearance component. Controls rendering on client side. The `radius` does
// affect placement, but otherwise these fields have no effect on physics or
// game logic.
// TODO: Add field for sprite selection.
struct Appearance {
  WorldTick modified{WorldTick::invalid}; // Tick when last modified.
  char32_t glyph{};      // A Unicode character to display, if any.
  float radius{5.F};     // World-space radius of the rendered shape.
  uint32_t fgColor{};    // RGBA.
  uint32_t bgColor{};    // RGBA.
  float attackRadius{};  // Display-only (world units).
  uint32_t trailColor{}; // RGBA.
};

// Health component. Applies to both defenders and invaders. The two health
// fields are streamed to the client in order to render health bars. The
// `regen` is server-side only.
struct Health {
  WorldTick modified{WorldTick::invalid}; // Tick when last modified.
  float currentHealth{};
  float maxHealth{};
  float regen{}; // Regeneration or bleed per tick. Not streamed to clients.
};

// Visual effects component. Controls transient overlays on the client side,
// but has no effect on physics or game logic.
struct VisualEffects {
  WorldTick modified{WorldTick::invalid}; // Tick when last modified.
  uint32_t selectionColor{};              // RGBA.
  float rangeRadius{}; // Does not affect physics or game logic; display-only.
  uint32_t rangeColor{}; // RGBA.
  uint32_t flashColor{}; // RGBA.
  WorldTick flashExpiry{WorldTick::invalid};
  uint32_t cooldownColor{}; // RGBA. Active from fire time until `nextAttack`.
  WorldTick cooldownExpiry{WorldTick::invalid};
};

// Fire-and-forget, display-only explosion streamed once to the client.
// `expiry` is absolute for emitted instances; when embedded as a template it
// stores the desired duration to add to the current tick.
struct TransientExplosion {
  WorldTick expiry{WorldTick::invalid};
  Circle circle{};
  uint32_t primaryColor{};
  uint32_t secondaryColor{};
};

// Fire-and-forget, display-only beam streamed once to the client.
// `expiry` is absolute for emitted instances; when embedded as a template it
// stores the desired duration to add to the current tick.
struct TransientBeam {
  Circle circle{};
  WorldTick expiry{WorldTick::invalid};
  uint32_t primaryColor{};
  uint32_t secondaryColor{}; // Not used in wedge mode
  Position targetPos{};
  float lineWidth{};
  float halfAngleDeg{}; // When > 0, render as wedge
  float coneRadius{};
};

// Defensive component, common across all defenders.
struct Defender {
  uint16_t entityTemplateIndex{std::numeric_limits<uint16_t>::max()};
  float hitCircleRadius{}; // Hit detection, as opposed to appearance.
  float attackRadius{};    // Used for physics and game logic.
  uint32_t rangeColor{};   // RGBA.
  float attackDamage{};    // Interpretation depends on the defender type.
  WorldTick cooldown{};    // Time between attacks.
  WorldTick nextAttack{};  // Updated when the defender attacks.
  TargetMode targetMode{TargetMode::first};
};

// Stats for defenders, shown when selecting a defender.
struct DefenderStats {
  float totalDamageDealt{};
  float totalDamageTaken{};
  float totalKills{};
};

// Area-of-effect component for defenders which have an attack that hits an
// area rather than a single target.
struct DefenderAoe {
  int damageType{}; // Eventually an enum.
};

// Hitscan component for defenders that have an attack that hits a single
// target instantly, rather than spawning a projectile that travels to the
// target.
struct DefenderHitscan {
  TransientBeam transientTemplate{};
};

// Projectile component for `DefenderShooter`. Used as part of its own
// archetype, and also as the `bulletTemplate`. As a template, the `expiry`
// field stores the TTL. Once instantiated, added to the current tick to gets
// its final moment.
struct DefenderBullet {
  WorldTick expiry{}; // Expiration tick of the projectile.
  SimWorldBounds::EntityId shooterId{SimWorldBounds::EntityId::invalid};
  float hitCircleRadius{}; // Hit detection, used for physics and game logic.
  float speed{};
  float directDamage{};     // Damage upon impact.
  float damageOverTime{};   // Damage applied over time.
  float splashRadius{};     // Radius for area-of-effect damage.
  float directDamageType{}; // Eventually an enum.
  float dotDamageType{};    // Eventually an enum.
  int projectileType{};     // Eventually an enum.
};

// Shooter component for defenders that spawn projectiles. The
// `bulletTemplate` is used to spawn bullets with the same properties as the
// defender's attack, but with their own position and velocity. When
// `muzzleFlashTemplate.primaryColor` is set, a transient beam is emitted
// toward `aimPos` on each shot, guaranteeing a visible effect even if the
// bullet hits within a single tick.
struct DefenderShooter {
  DefenderBullet bulletTemplate{};
  TransientBeam muzzleFlashTemplate{}; // Template; `expiry` stores TTL.
  float fireRate{};                    // Shots per tick.
  int spread{};                        // Eventually an enum.
};

// Invader component, shared across all invaders.
struct Invader {
  uint16_t entityTemplateIndex{std::numeric_limits<uint16_t>::max()};
  float hitCircleRadius{}; // Hit detection, used for physics and game logic.
  uint32_t bounty{10};     // Resources awarded for killing this invader.
};

// ECS types for the simulation world.
//
// Registry metadata (`uint64_t`) stores each entity's last-change tick
// count, enabling delta snapshots without a separate dirty set.
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
//   `sidDefenderShooter`  = 4 -> `ArchDefenderShooter`
//                                projectile-firing defenders
//                                (Position + Appearance + VisualEffects +
//                                Defender + DefenderStats + Health +
//                                DefenderShooter)
//
//   `sidDefenderHitscan`  = 5 -> `ArchDefenderHitscan`
//                                instant-hit single-target defenders
//                                (Position + Appearance + VisualEffects +
//                                Defender + DefenderStats + Health +
//                                DefenderHitscan)
using WorldReg = SimWorldBounds::WorldReg;
using ArchInvaderAlpha = archetype_storage<WorldReg,
    std::tuple<Position, Appearance, VisualEffects, Pathing, Invader, Health>>;
using ArchDefenderAoe = archetype_storage<WorldReg,
    std::tuple<Position, Appearance, VisualEffects, Defender, DefenderStats,
        Health, DefenderAoe>>;
using ArchBullet = archetype_storage<WorldReg,
    std::tuple<Position, Velocity, Appearance, DefenderBullet>>;
using ArchDefenderShooter = archetype_storage<WorldReg,
    std::tuple<Position, Appearance, VisualEffects, Defender, DefenderStats,
        Health, DefenderShooter>>;
using ArchDefenderHitscan = archetype_storage<WorldReg,
    std::tuple<Position, Appearance, VisualEffects, Defender, DefenderStats,
        Health, DefenderHitscan>>;
using WorldScene = archetype_scene<WorldReg, ArchInvaderAlpha, ArchDefenderAoe,
    ArchBullet, ArchDefenderShooter, ArchDefenderHitscan>;

// Per-map entity template storage. The map is keyed by label, but we also keep
// a list of labels separately so that this index can be stored in the ECS,
// avoiding strings. This is particularly relevant for projectiles, which need
// to know which defender spawned them in order to get full credit for their
// kills.
struct EntityTemplateStore {
  string_unordered_map<WorldScene::megatuple_t> templates;
  std::vector<std::string> labels;

  [[nodiscard]] bool
  registerEntity(std::string label, WorldScene::megatuple_t tpl) {
    auto [it, inserted] =
        templates.insert_or_assign(std::move(label), std::move(tpl));
    if (!inserted) return false;
    auto& [key, value] = *it;
    labels.push_back(key);
    auto idx = static_cast<uint16_t>(labels.size() - 1);
    if (auto& defender = std::get<std::optional<Defender>>(value); defender)
      defender->entityTemplateIndex = idx;
    else if (auto& invader = std::get<std::optional<Invader>>(value); invader)
      invader->entityTemplateIndex = idx;
    return true;
  }
};

// Simulation world: encapsulates all ECS entity state for the game and
// provides physics.
//
// Each `tick` advances `Position` components based on velocity and bounces off
// the world boundary. The registry metadata records the tick count at each
// entity's last state change so callers can request delta snapshots starting
// from any past tick.
class SimWorld: public SimWorldBounds {
public:
  static constexpr WorldSid sidStaging{0};
  static constexpr WorldSid sidInvaderAlpha{1};
  static constexpr WorldSid sidDefenderAoe{2};
  static constexpr WorldSid sidBullet{3};
  static constexpr WorldSid sidDefenderShooter{4};
  static constexpr WorldSid sidDefenderHitscan{5};

  // Clear state.
  void clear() {
    scene_.clear();
    paths_.clear();
    updatedEntities_.clear();
    pathEscapees_.clear();
    pendingKills_.clear();
    pendingTransientExplosions_.clear();
    pendingTransientBeams_.clear();
    entityTemplateStore_ = nullptr;
    tick_ = {};
  }

  // Point this world at a map's entity template store.
  // Used primarily for testing.
  void setEntityTemplateStore(const EntityTemplateStore* store) {
    entityTemplateStore_ = store;
  }

  // Look up a registered entity template by label.
  [[nodiscard]] auto findEntityTemplate(std::string_view label) const {
    return find_opt(entityTemplateStore_->templates, label);
  }

  // Spawn an entity by label. Looks up the pre-registered template.
  [[nodiscard]] Handle spawnEntity(std::string_view label) {
    return spawnEntity(findEntityTemplate(label));
  }

  // Assert helper for validating entity template indices. It returns true if
  // the component isn't present, as a convenience for assertions.
  template<typename Component>
  [[nodiscard]] bool
  hasValidEntityTemplateIndex(const WorldScene::megatuple_t& megatuple) const {
    const auto& component = std::get<std::optional<Component>>(megatuple);
    if (!component) return true;
    return component->entityTemplateIndex <
           entityTemplateStore_->labels.size();
  }

  // Spawn an entity by definition. Stamps `tick_` into any `modified` fields,
  // marks the entity dirty, and returns its handle.
  [[nodiscard]] Handle spawnEntity(const WorldScene::megatuple_t* megatuple) {
    if (!megatuple) return {};
    assert(hasValidEntityTemplateIndex<Defender>(*megatuple));
    assert(hasValidEntityTemplateIndex<Invader>(*megatuple));
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
  // tuple of `nullptr`s if the entity does not carry all of those
  // components.
  template<typename... Cs>
  [[nodiscard]] auto try_get_components(EntityId id) noexcept {
    return scene_.try_get_components<Cs...>(id);
  }

  // Bake and store a path. Returns the index used, as `Pathing::pathId`.
  // The index is stable for the lifetime of the world.
  [[nodiscard]] PathId addPath(const PathJoints& pj) {
    if (!paths_.push_back(SegmentedPath::fromJoints(pj)))
      return PathId::invalid;
    return paths_.size_as_enum() - 1;
  }

  // Return a pointer to the baked path at `pathId`, or `nullptr` if out of
  // range.
  [[nodiscard]] const SegmentedPath* getPath(PathId pathId) const {
    if (pathId >= paths_.size_as_enum()) return nullptr;
    return &paths_[pathId];
  }

  // Get the label for an entity template by its index.
  [[nodiscard]] std::string_view getEntityTemplateLabel(
      uint16_t entityTemplateIndex) const {
    if (entityTemplateIndex >= entityTemplateStore_->labels.size()) return {};
    return entityTemplateStore_->labels[entityTemplateIndex];
  }

  // Obtain a pointer to the `Appearance` for the entity. It is already
  // marked dirty and `modified`, so the caller only needs to change the
  // other fields.
  [[nodiscard]] Appearance* changeAppearance(EntityId id) {
    auto* app = scene_.try_get_component<Appearance>(id);
    if (!app) return nullptr;
    app->modified = tick_;
    (void)markDirty(id);
    return app;
  }

  // Obtain a pointer to the `VisualEffects` for the entity. It is already
  // marked dirty and `modified`, so the caller only needs to change the
  // other fields.
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

  // Set a cooldown overlay on a defender. `absoluteExpiry` is an absolute tick
  // (typically `defender.nextAttack`) at which the overlay clears.
  [[nodiscard]] bool
  setCooldown(EntityId id, uint32_t color, WorldTick absoluteExpiry) {
    auto* effects = changeVisualEffects(id);
    if (!effects) return false;
    effects->cooldownColor = color;
    effects->cooldownExpiry = absoluteExpiry;
    return true;
  }

  // Return a generation-versioned handle for `id`, suitable for storage
  // across ticks, or whenever there is a risk of the underlying entity going
  // away. Use `getId` to get the ID back, just in time, with validation.
  [[nodiscard]] Handle getHandle(EntityId id) const {
    return scene_.registry().get_handle(id);
  }

  // Return the entity ID for the `Handle`, or `EntityId::invalid` if it's
  // invalid.
  [[nodiscard]] EntityId getId(Handle h) const {
    return scene_.registry().id_from_handle(h);
  }

  // Find an entity at a given position, if any. Uses simple bounding box,
  // suitable for UI, not hit detection.
  [[nodiscard]] EntityId findEntityAt(const Position& pos) const {
    EntityId found_id = EntityId::invalid;
    scene_.for_each<Position, Appearance>([&](auto id, auto comps) {
      const auto& [epos, app] = comps;
      if (std::abs(epos.x - pos.x) >= app.radius ||
          std::abs(epos.y - pos.y) >= app.radius)
        return true;

      found_id = id;
      return false;
    });

    return found_id;
  }

  // Find ID of the defender at the given position, if any. Uses simple
  // bounding box, suitable for UI, not hit detection.
  [[nodiscard]] EntityId findDefenderAt(const Position& pos) const {
    auto found_id = EntityId::invalid;
    scene_.for_each<Position, Appearance, Defender>([&](auto id, auto comps) {
      const auto& [epos, app, _] = comps;
      if (std::abs(epos.x - pos.x) >= app.radius ||
          std::abs(epos.y - pos.y) >= app.radius)
        return true;

      found_id = id;
      return false;
    });

    return found_id;
  }

  // Obtain standard components for a defender entity.
  [[nodiscard]] auto getDefender(EntityId id) {
    return scene_
        .try_get_components<Position, Appearance, VisualEffects, Defender>(id);
  }

  // Check if an object at `pos` with a given `radius` overlaps any
  // defenders. Used for placement. Skips the entity with `excludeId` (pass
  // `EntityId::invalid` to skip no entity; used when moving a placed
  // defender so it can partially overlap its own footprint).
  [[nodiscard]] bool doesOveralapDefenders(const Position& pos, float radius,
      EntityId excludeId = EntityId::invalid) const {
    bool overlaps = false;
    scene_.for_each<Position, Appearance, Defender>([&](auto id, auto comps) {
      if (id == excludeId) return true;
      const auto& [defender_pos, defender_app, _] = comps;
      if (!circlesOverlap(pos, radius, defender_pos, defender_app.radius))
        return true;

      overlaps = true;
      return false;
    });
    return overlaps;
  }

  // Check if an object at `pos` with a given `radius` touches any paths.
  [[nodiscard]] bool doesTouchPath(const Position& pos, float radius) const {
    for (const auto& path : paths_) {
      const float expanded_radius = radius + (path.width * 0.5F);
      const float expanded_radius_sq = expanded_radius * expanded_radius;
      for (const auto& seg : path.segments)
        if (distanceSquaredToSegment(pos, seg.front, seg.back) <=
            expanded_radius_sq)
          return true;
    }
    return false;
  }

  // Whether defender placement at `pos` with a given `radius` is blocked.
  // Pass `excludeId` when relocating an already-placed defender so its own
  // footprint does not veto the destination.
  [[nodiscard]] bool isDefenderPlacementBlocked(const Position& pos,
      float radius, EntityId excludeId = EntityId::invalid) const {
    return !isInBounds(pos, radius) ||
           doesOveralapDefenders(pos, radius, excludeId) ||
           doesTouchPath(pos, radius);
  }

  // Run all physics for the current tick without advancing the counter. Call
  // `tick` once all game logic for this frame is complete, including streaming
  // the state to clients.
  [[nodiscard]] bool next() {
    (void)updateMovers();
    (void)updatePathFollowers();
    (void)updateProjectiles();
    // TODO: Add regen.
    (void)defendersAttack();

    return true;
  }

  // Advance the tick counter and return the new value. Call this at the end of
  // each frame, after `next`, all game-level logic, and streaming have run.
  [[nodiscard]] WorldTick tick() { return ++tick_; }

  // Return the current tick counter without advancing it.
  [[nodiscard]] WorldTick currentTick() const { return tick_; }

  // Total number of entities in all storages (does not count staged entities).
  [[nodiscard]] std::size_t size() const { return scene_.size(); }

  // Whether any invader entities are currently active (i.e. not yet
  // tombstoned). Call after `resolveEscapees` and `resolveKills` so that
  // entities removed this tick are not counted.
  [[nodiscard]] bool hasActiveInvaders() const {
    bool found = false;
    scene_.for_each<Invader>([&found](auto, auto) {
      found = true;
      return false; // Stop on first match.
    });
    return found;
  }

  // Destructively extract upserts and erasures.
  //
  // Call back `cbUpserts(EntityId, Position, Appearance, VisualEffects,
  // Health)` for each changed entity that has a `Position` and `Appearance`
  // and has changed since the last tick, and `cbErased(EntityId)` for each
  // entity that has been erased since the last tick. The visual effects
  // pointer is null for archetypes that do not carry that component. These
  // callbacks will be interleaved.
  [[nodiscard]] bool
  extractUpdatedEntities(auto&& cbUpserts, auto&& cbErased) {
    static constexpr VisualEffects nfx;
    static constexpr Health nhp;
    auto& reg = scene_.registry();
    for (auto id : updatedEntities_) {
      const auto [pos, app, fx, hp] = scene_.try_get_some_components<Position,
          Appearance, VisualEffects, Health>(id);
      if (pos && app) {
        (void)cbUpserts(id, *pos, *app, fx ? *fx : nfx, hp ? *hp : nhp);
      } else if (reg.get_location(id).store_id == sidStaging) {
        (void)cbErased(id);
        (void)scene_.erase_entity(id);
      }
    }
    updatedEntities_.clear();
    return true;
  }

  // Destructively extract fire-and-forget transient beams emitted this
  // frame, calling `cbBeam(TransientBeam)` for each.
  [[nodiscard]] bool extractTransientBeams(auto&& cbBeam) {
    for (const auto& beam : pendingTransientBeams_) (void)cbBeam(beam);
    pendingTransientBeams_.clear();
    return true;
  }

  // Destructively extract fire-and-forget transient explosions emitted this
  // frame, calling `cbExplosion(TransientExplosion)` for each.
  [[nodiscard]] bool extractTransientExplosions(auto&& cbExplosion) {
    for (const auto& explosion : pendingTransientExplosions_)
      (void)cbExplosion(explosion);
    pendingTransientExplosions_.clear();
    return true;
  }

  // Call back `cbPath(PathId, Position)` for all joints of the path
  // identified by `pathId`.
  [[nodiscard]] bool obtainPath(auto&& cbPath, PathId pathId) const {
    assert(pathId < paths_.size_as_enum());
    const auto& sp = paths_[pathId];
    for (const auto& seg : sp.segments) cbPath(pathId, seg.front);
    if (!sp.segments.empty()) cbPath(pathId, sp.segments.back().back);
    return true;
  }

  // Call back `cbPath(PathId, Position)` for all joints of all paths.
  [[nodiscard]] bool obtainPaths(auto&& cbPath) const {
    for (PathId pathId{0}; pathId < paths_.size_as_enum(); ++pathId)
      (void)obtainPath(cbPath, pathId);
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
      if (pos && cbEscapee(id, *pos, *pf)) (void)tombstoneEntity(id);
    }
    pathEscapees_.clear();
    return true;
  }

  // Resolve pending kills by calling `cbKill(EntityId, const Position&, const
  // Invader&)` for each. If `cbKill` returns true, the entity is tombstoned;
  // if false, it is left alive so the caller can migrate it (e.g. to a
  // transient death animation).
  //
  // Destructively iterates the pending-kills list.
  [[nodiscard]] bool resolveKills(auto&& cbKill) {
    for (auto id : pendingKills_) {
      auto [pos, inv] = scene_.try_get_components<Position, Invader>(id);
      if (pos && cbKill(id, *pos, *inv)) {
        pendingTransientExplosions_.emplace_back(TransientExplosion{
            .expiry = WorldTick{*tick_ + 6},
            .circle = Circle{Position{pos->x, pos->y}, inv->hitCircleRadius},
            .primaryColor = 0xFFB040E6U,
            .secondaryColor = 0xFFFFD080U});
        (void)tombstoneEntity(id);
      }
    }
    pendingKills_.clear();
    return true;
  }

  // Mark an entity as dirty so that it will be included in the next delta
  // snapshot.
  [[nodiscard]] bool markDirty(EntityId id) {
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
  // entities only. When `update_strategy::full`, also stamps `Appearance` and
  // `VisualEffects` `modified` fields with `tick_` so the next extraction
  // includes them. Must be called before `tick` so `markDirty`'s deduplication
  // check (`last_updated == tick_`) is valid.
  [[nodiscard]] bool markAllDirty(
      update_strategy strategy = update_strategy::incremental) {
    scene_.registry().for_each([&](auto id, auto&) {
      const auto loc = scene_.registry().get_location(id);
      if (loc.store_id == sidStaging) return true;

      (void)markDirty(id);
      if (strategy == update_strategy::incremental) return true;

      auto [app, fx, hp] =
          scene_.try_get_some_components<Appearance, VisualEffects, Health>(
              id);
      if (app) app->modified = tick_;
      if (fx) fx->modified = tick_;
      if (hp) hp->modified = tick_;
      return true;
    });
    return true;
  }

  // Calculate squared distance between two positions.
  static constexpr float
  distanceSquared(const Position& a, const Position& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
  }

  // Determine if two circles overlap.
  static constexpr bool circlesOverlap(const Position& posA, float radiusA,
      const Position& posB, float radiusB) {
    const float r = radiusA + radiusB;
    return distanceSquared(posA, posB) <= r * r;
  }

  // Calculate the squared distance from a point to the closest point on a line
  // segment.
  static constexpr float distanceSquaredToSegment(const Position& point,
      const Position& segA, const Position& segB) {
    const float dx = segB.x - segA.x;
    const float dy = segB.y - segA.y;
    const float len_sq = (dx * dx) + (dy * dy);
    if (len_sq <= 0.F) return distanceSquared(point, segA);

    const float t = std::clamp(
        (((point.x - segA.x) * dx) + ((point.y - segA.y) * dy)) / len_sq, 0.F,
        1.F);
    const Position projection{
        segA.x + (dx * t),
        segA.y + (dy * t),
    };
    return distanceSquared(point, projection);
  }

private:
  WorldScene scene_;
  WorldTick tick_{};
  id_container<SegmentedPath, PathId> paths_;

  std::vector<EntityId> updatedEntities_;
  std::vector<EntityId> pathEscapees_;
  std::vector<EntityId> pendingKills_;
  std::vector<TransientExplosion> pendingTransientExplosions_;
  std::vector<TransientBeam> pendingTransientBeams_;

  const EntityTemplateStore* entityTemplateStore_{};

  [[nodiscard]] static const Appearance& appearanceForProjectile(
      const DefenderBullet& bullet) {
    static constexpr Appearance kType1{.glyph = U'*',
        .radius = 8.F,
        .fgColor = 0xFFFFFFFFU,
        .bgColor = 0xFFA020FFU,
        .attackRadius = 0.F,
        .trailColor = 0xFFA020FFU};
    static constexpr Appearance kDefault{.glyph = U'.',
        .radius = 6.F,
        .fgColor = 0xFFFFFFFFU,
        .bgColor = 0xC0C0C0FFU,
        .attackRadius = 0.F,
        .trailColor = 0xC0C0C0FFU};
    switch (bullet.projectileType) {
    case 1: return kType1;
    default: return kDefault;
    }
  }

  // Move an entity to staging to tombstone it, adding it to the dirty list so
  // that it will show up as a deletion.
  [[nodiscard]] bool tombstoneEntity(EntityId id) {
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
      // should be reflected back. So, for example, if dx is 20 and the
      // entity is 5 pixels from the right edge, it should spend 5 of those
      // 20 moving to the right, and then 15 moving back. Its velocity should
      // be reversed starting at the edge. The only reason the current
      // algorithm works at all is that velocities are small. The algorithm
      // is also bad in a different way, which is that it measures position
      // from the center, not a bounding box, so the balls enter the boundary
      // before reversing. Even then, a square bounding box would be
      // hit-detected prematurely if the entity is moving at a diagonal.
      (void)markDirty(id);

      pos.x += vel.vx;
      pos.y += vel.vy;

      (void)bounceFromBoundary(pos.x, vel.vx, widthOfWorld);
      (void)bounceFromBoundary(pos.y, vel.vy, heightOfWorld);
      return true;
    });

    return true;
  }

  // Apply projectile damage to one invader and credit the originating
  // defender's live stats. Shared by both direct-hit and splash-hit
  // resolution.
  // TODO: This will need to be expanded to handle different damage types, as
  // well as applying a lingering damage-over-time effect.
  [[nodiscard]] bool applyProjectileDamage(EntityId targetId, float damage,
      DefenderStats& shooterStats, uint32_t flashColor) {
    auto* hp = scene_.try_get_component<Health>(targetId);
    if (!hp || hp->currentHealth <= 0.F || damage <= 0.F) return false;

    // Apply damage.
    const float actualDamage = std::min(damage, hp->currentHealth);
    hp->modified = tick_;
    hp->currentHealth -= actualDamage;
    shooterStats.totalDamageDealt += actualDamage;
    (void)markDirty(targetId);

    // Check for death.
    if (hp->currentHealth <= 0.F) {
      pendingKills_.push_back(targetId);
      shooterStats.totalKills += 1.F;
    } else
      (void)flashEntity(targetId, flashColor, WorldTick{5});

    return true;
  }

  // Project `point` onto the swept segment from `start` to `end` and return
  // its squared travel distance from `start`. Used to choose the first
  // invader a projectile encounters during this frame.
  [[nodiscard]] static float calculateSweepDistance(const Position& start,
      const Position& end, const Position& point) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float lenSq = (dx * dx) + (dy * dy);
    if (lenSq <= 0.F) return 0.F;

    // Clamp the projected point to the segment so the returned value stays
    // within this frame's swept path.
    const float t = std::clamp(
        (((point.x - start.x) * dx) + ((point.y - start.y) * dy)) / lenSq, 0.F,
        1.F);
    return t * lenSq;
  }

  // Find the first invader, if any, struck by this projectile along its swept
  // path. Returns {targetId, impactPos}; an invalid `targetId` means no hit.
  [[nodiscard]] std::pair<EntityId, Position>
  findFirstProjectileHit(const Position& bulletPos, const Velocity& vel,
      const DefenderBullet& bullet) {
    const Position previousPos{bulletPos.x - vel.vx, bulletPos.y - vel.vy};

    EntityId targetId = EntityId::invalid;
    Position impactPos = bulletPos;
    float bestTravel = std::numeric_limits<float>::max();
    scene_.for_each<Position, Invader>([&](auto enemyId, auto comps) {
      const auto& [enemyPos, invader] = comps;
      const float r = bullet.hitCircleRadius + invader.hitCircleRadius;
      // Skip if it misses.
      if (distanceSquaredToSegment(enemyPos, previousPos, bulletPos) > r * r)
        return true;

      // Keep closest hit.
      const float travel =
          calculateSweepDistance(previousPos, bulletPos, enemyPos);
      if (travel < bestTravel) {
        bestTravel = travel;
        targetId = enemyId;
        impactPos = enemyPos;
      }
      return true;
    });

    return {targetId, impactPos};
  }

  // Apply splash damage around a detonation point.
  [[nodiscard]] bool applySplashProjectileDamage(const Position& center,
      const DefenderBullet& bullet, DefenderStats& shooterStats) {
    scene_.for_each<Position, Invader>([&](auto enemyId, auto enemyComps) {
      const auto& [enemyPos, invader] = enemyComps;
      if (!circlesOverlap(center, bullet.splashRadius, enemyPos,
              invader.hitCircleRadius))
        return true;
      (void)applyProjectileDamage(enemyId, bullet.directDamage, shooterStats,
          0xFFFFA040U);
      return true;
    });
    return true;
  }

  // Emit the explosion visual for a detonating projectile.
  [[nodiscard]] bool
  emitProjectileExplosion(const Position& pos, const DefenderBullet& bullet) {
    pendingTransientExplosions_.emplace_back(TransientExplosion{
        .expiry = WorldTick{*tick_ + 3},
        .circle = Circle{Position{pos.x, pos.y},
            std::max(bullet.splashRadius, bullet.hitCircleRadius * 3.F)},
        .primaryColor = 0xFFFF80FFU,
        .secondaryColor = 0xFF8020C0U});
    return true;
  }

  // Resolve a confirmed projectile hit by applying its damage policy and
  // emitting the impact visual. Structural erasure is handled by the caller
  // after projectile iteration completes.
  [[nodiscard]] bool resolveProjectileHit(EntityId targetId,
      const Position& impactPos, const DefenderBullet& bullet) {
    auto shooterStats =
        scene_.try_get_component<DefenderStats>(bullet.shooterId);
    assert(shooterStats);
    if (bullet.directDamage > 0.F)
      (void)applyProjectileDamage(targetId, bullet.directDamage, *shooterStats,
          0xFFFFA040U);
    if (bullet.splashRadius > 0.F)
      (void)applySplashProjectileDamage(impactPos, bullet, *shooterStats);
    (void)emitProjectileExplosion(impactPos, bullet);
    return true;
  }

  // Resolve an explosive projectile that expired in flight: apply splash
  // damage and emit the detonation visual. No-op if the bullet has no splash
  // radius.
  [[nodiscard]] bool
  resolveExpiredProjectile(const Position& pos, const DefenderBullet& bullet) {
    if (bullet.splashRadius <= 0.F) return true;
    auto shooterStats =
        scene_.try_get_component<DefenderStats>(bullet.shooterId);
    assert(shooterStats);
    (void)applySplashProjectileDamage(pos, bullet, *shooterStats);
    (void)emitProjectileExplosion(pos, bullet);
    return true;
  }

  // Resolve projectile lifecycle after movement: expire old bullets, detect
  // hits along their swept paths, and queue bullets for erasure when done.
  [[nodiscard]] bool updateProjectiles() {
    std::vector<EntityId> expiredBullets;
    scene_.for_each<Position, Velocity, DefenderBullet>(
        [&](auto bulletId, auto comps) {
          auto& [bulletPos, vel, bullet] = comps;
          if (bullet.expiry <= tick_) {
            (void)resolveExpiredProjectile(bulletPos, bullet);
            expiredBullets.push_back(bulletId);
            return true;
          }
          // Check whether the projectile hits an invader along its path.
          if (auto [targetId, impactPos] =
                  findFirstProjectileHit(bulletPos, vel, bullet);
              targetId != EntityId::invalid)
          {
            (void)resolveProjectileHit(targetId, impactPos, bullet);
            expiredBullets.push_back(bulletId);
          }
          return true;
        });

    for (auto bulletId : expiredBullets) (void)tombstoneEntity(bulletId);
    return true;
  }

  // Advance path-following enemies. Collect entities that changed, as well
  // as those that escaped the path.
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

  // Candidate invader collected during target search.
  struct InvaderCandidate {
    EntityId id;
    float progress{};      // Distance along path; used by `first` and `last`.
    float currentHealth{}; // Used by `strongest` and `weakest`.
    float distSq{};        // Squared distance to defender; used by `closest`.
  };

  // Collect all invaders whose hit circle overlaps the defender's attack
  // circle, recording the fields needed for target selection. Clears
  // `candidates` first so the caller can reuse its allocation. Returns the
  // number of candidates found.
  [[nodiscard]] size_t collectCandidates(const Position& defenderPos,
      float attackRadius, std::vector<InvaderCandidate>& candidates) {
    candidates.clear();
    scene_.for_each<Position, Invader, Pathing, Health>(
        [&](auto enemyId, auto comps) {
          const auto& [enemyPos, invader, pf, hp] = comps;
          const float dSq = distanceSquared(defenderPos, enemyPos);
          const float r = attackRadius + invader.hitCircleRadius;
          if (dSq > r * r) return true;

          candidates.push_back({enemyId, pf.progress, hp.currentHealth, dSq});
          return true;
        });
    return candidates.size();
  }

  // Return the id of the best candidate according to `mode`.
  [[nodiscard]] static EntityId selectTarget(
      const std::vector<InvaderCandidate>& candidates, TargetMode mode) {
    if (candidates.empty()) return EntityId::invalid;
    auto it = candidates.cbegin();
    switch (mode) {
    case TargetMode::first:
      it = std::ranges::max_element(candidates, {},
          &InvaderCandidate::progress);
      break;
    case TargetMode::last:
      it = std::ranges::min_element(candidates, {},
          &InvaderCandidate::progress);
      break;
    case TargetMode::closest:
      it = std::ranges::min_element(candidates, {}, &InvaderCandidate::distSq);
      break;
    case TargetMode::strongest:
      it = std::ranges::max_element(candidates, {},
          &InvaderCandidate::currentHealth);
      break;
    case TargetMode::weakest:
      it = std::ranges::min_element(candidates, {},
          &InvaderCandidate::currentHealth);
      break;
    }
    return it->id;
  }

  // Damage every candidate. Those whose health drops to zero are added to
  // `pendingKills_` for `SimGame` to resolve (bounty, death animation, etc.).
  // Then put the defender on cooldown.
  [[nodiscard]] bool attackWithAoe(EntityId defenderId, Defender& defender,
      DefenderStats& stats, const std::vector<InvaderCandidate>& candidates,
      const DefenderAoe&) {
    const auto* defenderPos = scene_.try_get_component<Position>(defenderId);
    assert(defenderPos);
    pendingTransientExplosions_.emplace_back(TransientExplosion{
        .expiry = WorldTick{*tick_ + 1},
        .circle = Circle{Position{defenderPos->x, defenderPos->y},
            defender.attackRadius},
        .primaryColor = withAlpha(defender.rangeColor, 0x30U),
        .secondaryColor = withAlpha(defender.rangeColor, 0x10U)});
    for (const auto& cand : candidates) {
      auto* hp = scene_.try_get_component<Health>(cand.id);
      if (!hp) continue;
      const float actualDamage =
          std::min(defender.attackDamage, cand.currentHealth);
      hp->modified = tick_;
      hp->currentHealth -= defender.attackDamage;
      (void)markDirty(cand.id);
      if (hp->currentHealth <= 0.F) {
        pendingKills_.push_back(cand.id);
        stats.totalKills += 1.F;
      } else {
        (void)flashEntity(cand.id, 0xFF7F7FFF, WorldTick{5});
      }
      stats.totalDamageDealt += actualDamage;
    }
    defender.nextAttack = WorldTick{*tick_ + *defender.cooldown};
    (void)flashEntity(defenderId, 0xFFFFFFFF, WorldTick{5});
    (void)setCooldown(defenderId, 0x0000007FU, defender.nextAttack);
    return true;
  }

  // Solve for the earliest positive intercept time `t`, given the defender
  // position `D`, the target position `T` moving at (`tvx`, `tvy`), and a
  // bullet at `bulletSpeed`. Returns `NaN` if no positive solution exists (the
  // bullet can never catch the target on its current heading).
  //
  // Derivation: `|T + V_t*t - D|^2 = (bulletSpeed*t)^2`, expanded as a
  // quadratic in `t` with:
  // a` = `|V_t|^2 - bulletSpeed^2`, `b` = `2*(T-D).V_t`, c = `|T-D|^2`.
  //
  // The smallest positive root is the intercept time.
  [[nodiscard]] static float computeInterceptTime(const Position& defenderPos,
      const Position& targetPos, float tvx, float tvy, float bulletSpeed) {
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    const float dx = targetPos.x - defenderPos.x;
    const float dy = targetPos.y - defenderPos.y;
    const float a = (tvx * tvx) + (tvy * tvy) - (bulletSpeed * bulletSpeed);
    const float b = 2.F * ((dx * tvx) + (dy * tvy));
    const float c = (dx * dx) + (dy * dy);

    // Degenerate: target and bullet at the same speed. Linear in `t`.
    if (std::abs(a) < 1e-4F) return (b < -1e-4F) ? (-c / b) : nan;

    const float disc = (b * b) - (4.F * a * c);
    if (disc < 0.F) return nan; // No real solution.

    const float sq = std::sqrt(disc);
    const float t1 = (-b + sq) / (2.F * a);
    const float t2 = (-b - sq) / (2.F * a);

    // Return the smallest positive root, or `NaN` if neither qualifies.
    if (t1 > 0.F && (t2 <= 0.F || t1 < t2)) return t1;
    if (t2 > 0.F) return t2;
    return nan;
  }

  // Spawn a bullet aimed at the predicted intercept position of the best
  // single target, then put the defender on cooldown. If the intercept
  // calculation determines the bullet can never reach the target, does not
  // fire and does not start the cooldown.
  [[nodiscard]] bool attackWithShooter(EntityId defenderId, Defender& defender,
      const Position& defenderPos,
      const std::vector<InvaderCandidate>& candidates,
      const DefenderShooter& shooter) {
    const auto targetId = selectTarget(candidates, defender.targetMode);
    if (targetId == EntityId::invalid) return false;

    const auto [targetPos, targetPathing] =
        scene_.try_get_components<Position, Pathing>(targetId);
    if (!targetPos) return false;

    // Compute the target's current velocity from its path segment and speed.
    float tvx{};
    float tvy{};
    if (const auto* path = getPath(targetPathing->pathId)) {
      const float angle = path->angleAtProgress(targetPathing->progress);
      tvx = targetPathing->speed * std::cos(angle);
      tvy = targetPathing->speed * std::sin(angle);
    }

    const float t = computeInterceptTime(defenderPos, *targetPos, tvx, tvy,
        shooter.bulletTemplate.speed);
    // TODO: Right now, if the "best" target is outrunning the bullet, we
    // simply do not fire. A smarter algorithm would remove the bad target and
    // try again.
    if (std::isnan(t)) return false; // Target is outrunning the bullet.

    const Position aimPos{targetPos->x + (tvx * t), targetPos->y + (tvy * t)};

    const float adx = aimPos.x - defenderPos.x;
    const float ady = aimPos.y - defenderPos.y;
    const float dist = std::sqrt((adx * adx) + (ady * ady));

    Velocity vel{};
    if (dist > 0.F) {
      vel.vx = (adx / dist) * shooter.bulletTemplate.speed;
      vel.vy = (ady / dist) * shooter.bulletTemplate.speed;
    }

    // `bulletTemplate.expiry` stores TTL; convert to an absolute tick.
    auto bullet = shooter.bulletTemplate;
    bullet.shooterId = defenderId;
    bullet.expiry = WorldTick{*tick_ + *shooter.bulletTemplate.expiry};

    (void)spawnBullet(defenderPos, vel, bullet);
    if (shooter.muzzleFlashTemplate.primaryColor != 0) {
      pendingTransientBeams_.emplace_back(TransientBeam{
          .circle = Circle{Position{defenderPos.x, defenderPos.y},
              shooter.muzzleFlashTemplate.circle.radius},
          .expiry = WorldTick{*tick_ + *shooter.muzzleFlashTemplate.expiry},
          .primaryColor = shooter.muzzleFlashTemplate.primaryColor,
          .secondaryColor = shooter.muzzleFlashTemplate.secondaryColor,
          .targetPos = aimPos,
          .lineWidth = shooter.muzzleFlashTemplate.lineWidth,
          .halfAngleDeg = shooter.muzzleFlashTemplate.halfAngleDeg,
          .coneRadius = shooter.muzzleFlashTemplate.coneRadius});
    }
    defender.nextAttack = WorldTick{*tick_ + *defender.cooldown};
    (void)flashEntity(defenderId, 0xFFFFFFFF, WorldTick{5});
    (void)setCooldown(defenderId, 0x0000007FU, defender.nextAttack);
    return true;
  }

  // Spawn a bullet entity directly, bypassing the label-based template
  // system by using the template in the defender itself
  [[nodiscard]] Handle spawnBullet(const Position& pos, const Velocity& vel,
      const DefenderBullet& bullet) {
    auto h = scene_.store_new_entity({WorldTick::invalid},
        std::tuple{pos, vel, appearanceForProjectile(bullet), bullet});
    if (h) {
      if (auto* app = scene_.try_get_component<Appearance>(h.id()))
        app->modified = tick_;
      (void)markDirty(h.id());
    }
    return h;
  }

  // Apply hitscan damage to the best single target instantly and emit a
  // transient beam copy from the stored template. Puts the defender on
  // cooldown.
  [[nodiscard]] bool attackWithHitscan(EntityId defenderId, Defender& defender,
      DefenderStats& stats, const std::vector<InvaderCandidate>& candidates,
      const DefenderHitscan& hitscan) {
    const auto targetId = selectTarget(candidates, defender.targetMode);
    if (targetId == EntityId::invalid) return false;

    auto* hp = scene_.try_get_component<Health>(targetId);
    if (!hp) return false;

    const float actualDamage =
        std::min(defender.attackDamage, hp->currentHealth);
    hp->modified = tick_;
    hp->currentHealth -= defender.attackDamage;
    (void)markDirty(targetId);
    if (hp->currentHealth <= 0.F) {
      pendingKills_.push_back(targetId);
      stats.totalKills += 1.F;
    }
    stats.totalDamageDealt += actualDamage;

    const auto* defenderPos = scene_.try_get_component<Position>(defenderId);
    const auto* targetPos = scene_.try_get_component<Position>(targetId);
    if (!defenderPos || !targetPos) return false;

    pendingTransientBeams_.emplace_back(TransientBeam{
        .circle = Circle{Position{defenderPos->x, defenderPos->y},
            hitscan.transientTemplate.circle.radius},
        .expiry = WorldTick{*tick_ + *hitscan.transientTemplate.expiry},
        .primaryColor = hitscan.transientTemplate.primaryColor,
        .secondaryColor = hitscan.transientTemplate.secondaryColor,
        .targetPos = *targetPos,
        .lineWidth = hitscan.transientTemplate.lineWidth});

    defender.nextAttack = WorldTick{*tick_ + *defender.cooldown};
    (void)flashEntity(defenderId, 0xFFFFFFFF, WorldTick{5});
    (void)setCooldown(defenderId, 0x0000007FU, defender.nextAttack);
    return true;
  }

  // For each defender that is off cooldown and has targets in range, dispatch
  // to the appropriate attack handler based on the defender's attack type.
  // Cooldown is only reset when the defender actually fires.
  [[nodiscard]] bool defendersAttack() {
    std::vector<InvaderCandidate> candidates;
    scene_.for_each<Position, Defender, DefenderStats>(
        [&](auto defenderId, auto defenderComps) {
          auto& [defenderPos, defender, stats] = defenderComps;
          if (tick_ < defender.nextAttack) return true;

          if (!collectCandidates(defenderPos, defender.attackRadius,
                  candidates))
            return true;

          auto [aoe, shooter,
              hitscan] = scene_.try_get_some_components<DefenderAoe,
              DefenderShooter, DefenderHitscan>(defenderId);
          if (aoe)
            (void)attackWithAoe(defenderId, defender, stats, candidates, *aoe);
          else if (shooter)
            (void)attackWithShooter(defenderId, defender, defenderPos,
                candidates, *shooter);
          else if (hitscan)
            (void)attackWithHitscan(defenderId, defender, stats, candidates,
                *hitscan);

          return true;
        });

    return true;
  }

  [[nodiscard]] static constexpr bool isVisibleColor(uint32_t color) noexcept {
    return (color & 0xFFU) != 0U;
  }

  [[nodiscard]] static constexpr uint32_t
  withAlpha(uint32_t color, uint8_t alpha) noexcept {
    return (color & 0xFFFFFF00U) | alpha;
  }
};
}} // namespace corvid::sim
