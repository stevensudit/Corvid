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

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "../proto/json_parser.h"
#include "sim_game.h"

namespace corvid { inline namespace sim {

// Return the absolute world tick of an expiry as a uint32_t.
// Returns 0 if `expiry` is invalid (i.e. no expiry is set).
[[nodiscard]] inline uint32_t tickExpiryTick(WorldTick expiry) {
  if (expiry == WorldTick::invalid) return 0;
  return *expiry;
}

// Returns 0 if the color is unset; otherwise returns the absolute expiry tick.
[[nodiscard]] inline uint32_t
tickExpiryTick(uint32_t color, WorldTick expiry) {
  if (color == 0) return 0;
  return tickExpiryTick(expiry);
}

[[nodiscard]] inline uint32_t flashExpiryTick(const VisualEffects& effects) {
  return tickExpiryTick(effects.flashColor, effects.flashExpiry);
}

[[nodiscard]] inline uint32_t cooldownExpiryTick(
    const VisualEffects& effects) {
  return tickExpiryTick(effects.cooldownColor, effects.cooldownExpiry);
}

// Reusable state, to avoid repeated allocations.
struct SimGameStateJson {
  std::string body;
  std::vector<SimWorld::EntityId> erased_ids;
  std::vector<TransientExplosion> transient_explosions;
  std::vector<TransientBeam> transient_beams;

  // Clear the body and erased IDs. These fields retain their allocations.
  [[nodiscard]] bool clear() {
    body.clear();
    erased_ids.clear();
    transient_explosions.clear();
    transient_beams.clear();
    return true;
  }
};

[[nodiscard]] inline std::string buildSimHelloAckJson(
    std::string_view message = "connected") {
  std::string body;
  json_writer{body}
      .object()
      ->member(json_trusted{"type"}, json_trusted{"hello_ack"})
      .member(json_trusted{"message"}, message);
  return body;
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
[[nodiscard]] inline bool buildSimGameStateJson(SimGameStateJson& result,
    SimGame& game,
    update_strategy send_strategy = update_strategy::incremental) {
  (void)result.clear();
  const auto current_tick = game.currentTick();
  json_writer writer{result.body};
  if (send_strategy == update_strategy::full)
    (void)game.markAllDirty(update_strategy::full);

  size_t current_wave{};
  int lives_count{};
  int resources_count{};
  std::string_view phase{};
  UiState ui_state;

  auto write_delta = [&writer, &game, &result, current_tick, &current_wave,
                         &lives_count, &resources_count, &phase, &ui_state,
                         send_strategy](json_writer<std::string>& target) {
    target.member(json_trusted{"type"}, json_trusted{"world_delta"})
        .member(json_trusted{"tick"}, *current_tick);

    if (auto upserts = target.member_array(json_trusted{"upserts"})) {
      (void)game.extractDelta(
          [&writer, current_tick](SimWorld::EntityId entity_id,
              const Position& pos, const Appearance& app,
              const VisualEffects& effects, const Health& hp) {
            auto entity = writer.object();

            if (auto position = entity->member_object(json_trusted{"pos"})) {
              position->member(json_trusted{"id"}, *entity_id)
                  .member(json_trusted{"x"}, pos.x, std::chars_format::fixed,
                      1)
                  .member(json_trusted{"y"}, pos.y, std::chars_format::fixed,
                      1);
            }

            if (app.modified == current_tick) {
              entity->member_object(json_trusted{"app"})
                  ->member(json_trusted{"glyph"},
                      static_cast<uint32_t>(app.glyph))
                  .member(json_trusted{"radius"}, app.radius,
                      std::chars_format::fixed, 3)
                  .member(json_trusted{"fg"}, app.fgColor)
                  .member(json_trusted{"bg"}, app.bgColor)
                  .member(json_trusted{"attackRadius"}, app.attackRadius,
                      std::chars_format::fixed, 1)
                  .member(json_trusted{"trailColor"}, app.trailColor);
            }

            if (effects.modified == current_tick) {
              entity->member_object(json_trusted{"vfx"})
                  ->member(json_trusted{"selection"}, effects.selectionColor)
                  .member(json_trusted{"rangeRadius"}, effects.rangeRadius,
                      std::chars_format::fixed, 3)
                  .member(json_trusted{"range"}, effects.rangeColor)
                  .member(json_trusted{"flash"}, effects.flashColor)
                  .member(json_trusted{"flashExpiryTick"},
                      flashExpiryTick(effects))
                  .member(json_trusted{"cooldown"}, effects.cooldownColor)
                  .member(json_trusted{"cooldownExpiryTick"},
                      cooldownExpiryTick(effects))
                  .member(json_trusted{"cooldownDurationTick"},
                      cooldownExpiryTick(effects));
            }

            if (hp.modified == current_tick) {
              entity->member_object(json_trusted{"health"})
                  ->member(json_trusted{"current"}, hp.currentHealth,
                      std::chars_format::fixed, 1)
                  .member(json_trusted{"max"}, hp.maxHealth,
                      std::chars_format::fixed, 1);
            }

            return true;
          },
          [&result](SimWorld::EntityId entity_id) {
            result.erased_ids.push_back(entity_id);
            return true;
          },
          [&result](const TransientExplosion& explosion) {
            result.transient_explosions.push_back(explosion);
            return true;
          },
          [&result](const TransientBeam& beam) {
            result.transient_beams.push_back(beam);
            return true;
          },
          [&current_wave, &lives_count, &resources_count, &phase,
              &ui_state](auto new_current_wave, auto /*new_wave_tick*/,
              auto new_lives, auto new_resources, auto new_phase,
              const UiState& new_ui_state) {
            current_wave = new_current_wave;
            lives_count = new_lives;
            resources_count = new_resources;
            phase = new_phase;
            ui_state = new_ui_state;
            return true;
          });
    }

    if (auto erased = target.member_array(json_trusted{"erased"})) {
      for (const auto entity_id : result.erased_ids) erased->value(*entity_id);
    }
    if (auto explosions =
            target.member_array(json_trusted{"transientExplosions"}))
    {
      for (const auto& explosion : result.transient_explosions) {
        auto exp_json = explosions->object();
        if (auto circle = exp_json->member_object(json_trusted{"circle"})) {
          circle
              ->member(json_trusted{"x"}, explosion.circle.x,
                  std::chars_format::fixed, 1)
              .member(json_trusted{"y"}, explosion.circle.y,
                  std::chars_format::fixed, 1)
              .member(json_trusted{"radius"}, explosion.circle.radius,
                  std::chars_format::fixed, 1);
        }
        exp_json
            ->member(json_trusted{"expiryTick"},
                tickExpiryTick(explosion.expiry))
            .member(json_trusted{"primaryColor"}, explosion.primaryColor)
            .member(json_trusted{"secondaryColor"}, explosion.secondaryColor);
      }
    }
    if (auto beams = target.member_array(json_trusted{"transientBeams"})) {
      for (const auto& beam : result.transient_beams) {
        auto beam_json = beams->object();
        if (auto circle = beam_json->member_object(json_trusted{"circle"})) {
          circle
              ->member(json_trusted{"x"}, beam.circle.x,
                  std::chars_format::fixed, 1)
              .member(json_trusted{"y"}, beam.circle.y,
                  std::chars_format::fixed, 1)
              .member(json_trusted{"radius"}, beam.circle.radius,
                  std::chars_format::fixed, 1);
        }
        beam_json
            ->member(json_trusted{"expiryTick"}, tickExpiryTick(beam.expiry))
            .member(json_trusted{"primaryColor"}, beam.primaryColor)
            .member(json_trusted{"secondaryColor"}, beam.secondaryColor)
            .member(json_trusted{"lineWidth"}, beam.lineWidth,
                std::chars_format::fixed, 1)
            .member(json_trusted{"halfAngleDeg"}, beam.halfAngleDeg,
                std::chars_format::fixed, 1)
            .member(json_trusted{"coneRadius"}, beam.coneRadius,
                std::chars_format::fixed, 1);
        if (auto target_pos =
                beam_json->member_object(json_trusted{"targetPos"}))
        {
          target_pos
              ->member(json_trusted{"x"}, beam.targetPos.x,
                  std::chars_format::fixed, 1)
              .member(json_trusted{"y"}, beam.targetPos.y,
                  std::chars_format::fixed, 1);
        }
      }
    }

    target.member(json_trusted{"currentWave"}, current_wave)
        .member(json_trusted{"lives"}, lives_count)
        .member(json_trusted{"resources"}, resources_count)
        .member(json_trusted{"phase"}, json_trusted{phase});
    if (auto ui = target.member_object(json_trusted{"uiState"})) {
      if (ui_state.selectedDefender.has_value()) {
        const auto& selected = *ui_state.selectedDefender;
        if (auto pos = ui->member_object(json_trusted{"selectedDefender"})) {
          pos->member(json_trusted{"x"}, selected.x, std::chars_format::fixed,
                 1)
              .member(json_trusted{"y"}, selected.y, std::chars_format::fixed,
                  1);
        }
      }
      if (ui_state.placementAllowed.has_value())
        ui->member(json_trusted{"placementAllowed"},
            *ui_state.placementAllowed);
      if (ui_state.spawnAllowed.has_value())
        ui->member(json_trusted{"spawnAllowed"}, *ui_state.spawnAllowed);
      const bool include_summary =
          ui_state.selectedDefender.has_value() &&
          ui_state.defenderSummary.has_value() &&
          (send_strategy == update_strategy::full ||
              ui_state.defenderSummary->modified == current_tick);
      if (include_summary) {
        const auto& summary = *ui_state.defenderSummary;
        auto defender = ui->member_object(json_trusted{"defenderSummary"});
        defender->member(json_trusted{"entityName"}, summary.entityName)
            .member(json_trusted{"displayName"}, summary.displayName)
            .member(json_trusted{"category"}, summary.category)
            .member(json_trusted{"flavorText"}, summary.flavorText)
            .member(json_trusted{"resourceCost"}, summary.resourceCost);
        if (auto app = defender->member_object(json_trusted{"appearance"})) {
          app->member(json_trusted{"glyph"},
                 static_cast<uint32_t>(summary.appearance.glyph))
              .member(json_trusted{"radius"}, summary.appearance.radius,
                  std::chars_format::fixed, 3)
              .member(json_trusted{"fg"}, summary.appearance.fgColor)
              .member(json_trusted{"bg"}, summary.appearance.bgColor)
              .member(json_trusted{"attackRadius"},
                  summary.appearance.attackRadius, std::chars_format::fixed,
                  1);
        }
        defender
            ->member(json_trusted{"totalDamageDealt"},
                summary.totalDamageDealt, std::chars_format::fixed, 1)
            .member(json_trusted{"totalKills"}, summary.totalKills,
                std::chars_format::fixed, 0);
      }
    }
  };

  if (send_strategy == update_strategy::full) {
    auto snapshot = writer.object();
    snapshot->member(json_trusted{"type"}, json_trusted{"world_snapshot"});

    // Map design: sprite filenames + path joints.
    if (auto map_design = snapshot->member_object(json_trusted{"mapDesign"})) {
      const auto& md = game.mapDesign();
      map_design
          ->member(json_trusted{"backgroundSprite"}, md.backgroundSpriteFile)
          .member(json_trusted{"foregroundSprite"}, md.foregroundSpriteFile)
          .member(json_trusted{"pathWidth"},
              md.paths.empty() ? 40.F : md.paths.front().width,
              std::chars_format::fixed, 1);
      if (auto paths = map_design->member_array(json_trusted{"paths"})) {
        (void)game.extractPaths([&writer](auto, const Position& pos) {
          auto path = writer.object();
          path->member(json_trusted{"x"}, pos.x, std::chars_format::fixed, 1)
              .member(json_trusted{"y"}, pos.y, std::chars_format::fixed, 1);
          return true;
        });
      }
    }

    // Defender build menu (purchasable defenders, in menu order).
    if (auto menu = snapshot->member_array(json_trusted{"defenderMenu"})) {
      for (const auto& entry : game.mapDesign().defenderMenu) {
        auto item = writer.object();
        item->member(json_trusted{"entityName"}, entry.entityName)
            .member(json_trusted{"displayName"}, entry.displayName)
            .member(json_trusted{"category"}, entry.category)
            .member(json_trusted{"flavorText"}, entry.flavorText)
            .member(json_trusted{"resourceCost"}, entry.resourceCost);
        if (auto app = item->member_object(json_trusted{"appearance"})) {
          app->member(json_trusted{"glyph"},
                 static_cast<uint32_t>(entry.appearance.glyph))
              .member(json_trusted{"radius"}, entry.appearance.radius,
                  std::chars_format::fixed, 3)
              .member(json_trusted{"fg"}, entry.appearance.fgColor)
              .member(json_trusted{"bg"}, entry.appearance.bgColor)
              .member(json_trusted{"attackRadius"},
                  entry.appearance.attackRadius, std::chars_format::fixed, 1);
        }
      }
    }

    // Category definitions for the top-level build menu.
    if (auto cats = snapshot->member_array(json_trusted{"categories"})) {
      for (const auto& cat : game.mapDesign().categories) {
        auto item = writer.object();
        item->member(json_trusted{"name"}, cat.name)
            .member(json_trusted{"displayName"}, cat.displayName)
            .member(json_trusted{"flavorText"}, cat.flavorText);
        if (auto app = item->member_object(json_trusted{"appearance"})) {
          app->member(json_trusted{"glyph"},
                 static_cast<uint32_t>(cat.appearance.glyph))
              .member(json_trusted{"radius"}, cat.appearance.radius,
                  std::chars_format::fixed, 3)
              .member(json_trusted{"fg"}, cat.appearance.fgColor)
              .member(json_trusted{"bg"}, cat.appearance.bgColor)
              .member(json_trusted{"attackRadius"},
                  cat.appearance.attackRadius, std::chars_format::fixed, 1);
        }
      }
    }

    write_delta(*snapshot->member_object(json_trusted{"delta"}));
  } else
    write_delta(*writer.object());

  return true;
}
// NOLINTEND(readability-function-cognitive-complexity)

}} // namespace corvid::sim
