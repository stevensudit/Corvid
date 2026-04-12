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

[[nodiscard]] inline uint32_t
flashExpiryDelayMs(const VisualEffects& effects, WorldTick current_tick) {
  // If not flashing, nothing to do.
  if (effects.flashColor == 0 || effects.flashExpiry == WorldTick::invalid ||
      effects.flashExpiry < current_tick)
    return 0;

  // Calculate milliseconds until expiry, capping at max uint32_t.
  constexpr uint32_t ms_per_tick = 50;
  const auto ticks_until_expiry = *effects.flashExpiry - *current_tick;
  const auto expiry_delay_ms = ticks_until_expiry * ms_per_tick;
  return std::min(expiry_delay_ms, std::numeric_limits<uint32_t>::max());
}

// Reusable state, to avoid repeated allocations.
struct SimGameStateJson {
  std::string body;
  std::vector<SimWorld::EntityId> erased_ids;

  // Clear the body and erased IDs. These fields retain their allocations.
  [[nodiscard]] bool clear() {
    body.clear();
    erased_ids.clear();
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

[[nodiscard]] inline bool buildSimGameStateJson(SimGameStateJson& result,
    SimGame& game,
    update_strategy send_strategy = update_strategy::incremental) {
  (void)result.clear();
  const auto current_tick = game.currentTick();
  json_writer writer{result.body};
  if (send_strategy == update_strategy::full)
    (void)game.markAllDirty(update_strategy::full);

  size_t current_wave{};
  WaveTick wave_tick{};
  int lives_count{};
  int resources_count{};
  std::string_view phase{};
  UiState ui_state;

  auto write_delta = [&writer, &game, &result, current_tick, &current_wave,
                         &wave_tick, &lives_count, &resources_count, &phase,
                         &ui_state, send_strategy](
                        json_writer<std::string>& target) {
    target.member(json_trusted{"type"}, json_trusted{"world_delta"})
        .member(json_trusted{"tick"}, *current_tick);

    if (auto upserts = target.member_array(json_trusted{"upserts"})) {
      (void)game.extractDelta(
          [&writer, current_tick](SimWorld::EntityId entity_id,
              const Position& pos, const Appearance& app,
              const VisualEffects& effects) {
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
                      std::chars_format::fixed, 1);
            }

            if (effects.modified == current_tick) {
              entity->member_object(json_trusted{"vfx"})
                  ->member(json_trusted{"selection"}, effects.selectionColor)
                  .member(json_trusted{"rangeRadius"}, effects.rangeRadius,
                      std::chars_format::fixed, 3)
                  .member(json_trusted{"range"}, effects.rangeColor)
                  .member(json_trusted{"flash"}, effects.flashColor)
                  .member(json_trusted{"flashExpiryMs"},
                      flashExpiryDelayMs(effects, current_tick));
            }

            return true;
          },
          [&result](SimWorld::EntityId entity_id) {
            result.erased_ids.push_back(entity_id);
            return true;
          },
          [&current_wave, &wave_tick, &lives_count, &resources_count,
              &phase, &ui_state](auto new_current_wave, auto new_wave_tick,
              auto new_lives, auto new_resources, auto new_phase,
              const UiState& new_ui_state) {
            current_wave = new_current_wave;
            wave_tick = new_wave_tick;
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

    target.member(json_trusted{"currentWave"}, current_wave)
        .member(json_trusted{"waveTick"}, *wave_tick)
        .member(json_trusted{"lives"}, lives_count)
        .member(json_trusted{"resources"}, resources_count)
        .member(json_trusted{"phase"}, json_trusted{phase});
    if (auto ui = target.member_object(json_trusted{"uiState"})) {
      ui->member(json_trusted{"defenderSelected"}, ui_state.defenderSelected);
      if (ui_state.placementAllowed.has_value())
        ui->member(json_trusted{"placementAllowed"},
            *ui_state.placementAllowed);
      if (ui_state.spawnAllowed.has_value())
        ui->member(json_trusted{"spawnAllowed"}, *ui_state.spawnAllowed);
      const bool include_summary = ui_state.defenderSelected &&
          ui_state.defenderSummary.has_value() &&
          (send_strategy == update_strategy::full ||
              ui_state.defenderSummary->modified == current_tick);
      if (include_summary) {
        const auto& summary = *ui_state.defenderSummary;
        auto defender = ui->member_object(json_trusted{"defenderSummary"});
        defender->member(json_trusted{"entityName"}, summary.entityName)
            .member(json_trusted{"displayName"}, summary.displayName)
            .member(json_trusted{"flavorText"}, summary.flavorText)
            .member(json_trusted{"resourceCost"}, summary.resourceCost,
                std::chars_format::fixed, 1);
        if (auto app = defender->member_object(json_trusted{"appearance"})) {
          app->member(json_trusted{"glyph"},
                 static_cast<uint32_t>(summary.appearance.glyph))
              .member(json_trusted{"radius"}, summary.appearance.radius,
                  std::chars_format::fixed, 3)
              .member(json_trusted{"fg"}, summary.appearance.fgColor)
              .member(json_trusted{"bg"}, summary.appearance.bgColor)
              .member(json_trusted{"attackRadius"},
                  summary.appearance.attackRadius, std::chars_format::fixed, 1);
        }
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
            .member(json_trusted{"flavorText"}, entry.flavorText)
            .member(json_trusted{"resourceCost"}, entry.resourceCost,
                std::chars_format::fixed, 1);
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

    write_delta(*snapshot->member_object(json_trusted{"delta"}));
  } else
    write_delta(*writer.object());

  return true;
}

}} // namespace corvid::sim
