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
flash_expiry_delay_ms(const VisualEffects& effects, WorldTick current_tick) {
  if (effects.flash_color == 0 || effects.flash_expiry == WorldTick::invalid ||
      effects.flash_expiry < current_tick)
    return 0;

  constexpr uint64_t ms_per_tick = 50;
  const auto ticks_until_expiry =
      static_cast<uint64_t>(*effects.flash_expiry) -
      static_cast<uint64_t>(*current_tick);
  const auto expiry_delay_ms = ticks_until_expiry * ms_per_tick;
  return static_cast<uint32_t>(std::min(expiry_delay_ms,
      static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

// Reusable state, to avoid repeated allocations.
struct sim_game_state_json {
  std::string body;
  std::vector<SimWorld::EntityId> erased_ids;

  // Clear the body and erased IDs, updating highwater marks for future
  // reserve.
  [[nodiscard]] bool clear() {
    body_highwater = std::max(body.size(), body_highwater);
    erased_ids_highwater = std::max(erased_ids.size(), erased_ids_highwater);
    body.clear();
    erased_ids.clear();
    return true;
  }

  [[nodiscard]] bool reset() {
    (void)clear();
    body.reserve(body_highwater);
    erased_ids.reserve(erased_ids_highwater);
    return true;
  }

private:
  size_t body_highwater{16ULL * 1024};
  size_t erased_ids_highwater{64};
};

[[nodiscard]] inline std::string build_sim_hello_ack_json(
    std::string_view message = "connected") {
  std::string body;
  json_writer{body}
      .object()
      ->member(json_trusted{"type"}, json_trusted{"hello_ack"})
      .member(json_trusted{"message"}, message);
  return body;
}

[[nodiscard]] inline bool
build_sim_game_state_json(sim_game_state_json& result, SimGame& game,
    update_strategy send_strategy = update_strategy::incremental) {
  (void)result.reset();
  const auto current_tick = game.currentTick();
  json_writer writer{result.body};
  if (send_strategy == update_strategy::full)
    (void)game.markAllDirty(update_strategy::full);

  size_t current_wave{};
  WaveTick wave_tick{};
  int lives_count{};
  int resources_count{};
  std::string_view phase{};

  auto write_delta = [&writer, &game, &result, current_tick, &current_wave,
                         &wave_tick, &lives_count, &resources_count,
                         &phase](json_writer<std::string>& target) {
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
                  .member(json_trusted{"fg"}, app.fg_color)
                  .member(json_trusted{"bg"}, app.bg_color);
            }

            if (effects.modified == current_tick) {
              entity->member_object(json_trusted{"vfx"})
                  ->member(json_trusted{"selection"}, effects.selection_color)
                  .member(json_trusted{"rangeRadius"}, effects.range_radius,
                      std::chars_format::fixed, 3)
                  .member(json_trusted{"range"}, effects.range_color)
                  .member(json_trusted{"flash"}, effects.flash_color)
                  .member(json_trusted{"flashExpiryMs"},
                      flash_expiry_delay_ms(effects, current_tick));
            }

            return true;
          },
          [&result](SimWorld::EntityId entity_id) {
            result.erased_ids.push_back(entity_id);
            return true;
          },
          [&current_wave, &wave_tick, &lives_count, &resources_count,
              &phase](auto new_current_wave, auto new_wave_tick,
              auto new_lives, auto new_resources, auto new_phase) {
            current_wave = new_current_wave;
            wave_tick = new_wave_tick;
            lives_count = new_lives;
            resources_count = new_resources;
            phase = new_phase;
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
  };

  if (send_strategy == update_strategy::full) {
    auto snapshot = writer.object();
    snapshot->member(json_trusted{"type"}, json_trusted{"world_snapshot"});
    if (auto paths = snapshot->member_array(json_trusted{"paths"})) {
      (void)game.extractPaths([&writer](auto, const Position& pos) {
        auto path = writer.object();
        path->member(json_trusted{"x"}, pos.x, std::chars_format::fixed, 1)
            .member(json_trusted{"y"}, pos.y, std::chars_format::fixed, 1);
        return true;
      });
    }
    write_delta(*snapshot->member_object(json_trusted{"delta"}));
  } else
    write_delta(*writer.object());

  return true;
}

}} // namespace corvid::sim
