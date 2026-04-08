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

#include <string>
#include <string_view>
#include <vector>

#include "../proto/json_parser.h"
#include "sim_game.h"

namespace corvid { inline namespace sim {

struct sim_game_state_json {
  std::string body;
  std::vector<SimWorld::EntityId> erased_ids;
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

[[nodiscard]] inline bool build_sim_game_state_json(
    sim_game_state_json& result, SimGame& game, WorldTick current_tick,
    update_strategy send_strategy = update_strategy::incremental) {
  result.body.clear();
  result.body.reserve(result.body_highwater);

  json_writer writer{result.body};
  if (send_strategy == update_strategy::full)
    (void)game.markAllDirty(update_strategy::full);

  result.erased_ids.clear();
  result.erased_ids.reserve(result.erased_ids_highwater);

  size_t current_wave{};
  WaveTick wave_tick{};
  int lives_count{};
  int resources_count{};
  std::string_view phase{};

  auto write_delta =
      [&writer, &game, &result, current_tick, &current_wave, &wave_tick,
          &lives_count, &resources_count,
          &phase](json_writer<std::string>& target) {
        target.member(json_trusted{"type"}, json_trusted{"world_delta"})
            .member(json_trusted{"tick"}, *current_tick);

        if (auto upserts = target.member_array(json_trusted{"upserts"})) {
          (void)game.extractDelta(
              [&writer, current_tick](SimWorld::EntityId entity_id,
                  const Position& pos, const Appearance& app) {
                auto entity = writer.object();

                if (auto position =
                        entity->member_object(json_trusted{"pos"})) {
                  position->member(json_trusted{"id"}, *entity_id)
                      .member(json_trusted{"x"}, pos.x,
                          std::chars_format::fixed, 1)
                      .member(json_trusted{"y"}, pos.y,
                          std::chars_format::fixed, 1);
                }

                if (app.modified + 1 == current_tick) {
                  const auto glow_color =
                      app.effect_expiry < current_tick ? 0U : app.glow_color;
                  entity->member_object(json_trusted{"app"})
                      ->member(json_trusted{"glyph"},
                          static_cast<uint32_t>(app.glyph))
                      .member(json_trusted{"scale"}, app.scale,
                          std::chars_format::fixed, 3)
                      .member(json_trusted{"fg"}, app.fg_color)
                      .member(json_trusted{"bg"}, app.bg_color)
                      .member(json_trusted{"glow"}, glow_color);
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

        result.erased_ids_highwater =
            std::max(result.erased_ids.size(), result.erased_ids_highwater);

        if (auto erased = target.member_array(json_trusted{"erased"})) {
          for (const auto entity_id : result.erased_ids)
            erased->value(*entity_id);
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

  result.body_highwater = std::max(result.body.size(), result.body_highwater);
  return true;
}

}} // namespace corvid::sim
