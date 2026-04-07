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

#include <atomic>
#include <format>
#include <iostream>
#include <memory>
#include <vector>

#include "../proto/http_websocket_transaction.h"
#include "../strings/any_strings.h"
#include "sim_game.h"
#include "sim_json_parse.h"

namespace corvid { inline namespace proto {

using namespace std::chrono_literals;

// WebSocket transaction for the CorvidSim `/ws` route.
//
// Inherits from `http_websocket_transaction` and wires up all callbacks in the
// constructor so behavior lives in named private methods rather than lambdas.
//
// Protocol:
//   Client sends: {"type":"hello","client":"browser"}
//   Server replies: {"type":"hello_ack","message":"connected"}
//   Client sends: {"type":"ui_canvas",...} or {"type":"ui_action",...}
//   Server then sends at 20 Hz: {"type":"world_delta",...}
//
// Keepalive (20s ping / 5s pong timeout) is enabled so that browser pong
// replies reset the HTTP server's 30s read timeout; the constraint
// ping_interval (20s) + pong_timeout (5s) = 25s < read_timeout (30s) is
// satisfied.
//
// Tick scheduling follows the same `timer_fuse` double-check pattern used by
// the base class: the wheel-thread callback only calls `loop->post()`, and the
// loop-thread callback performs the definitive liveness check before sending
// and re-arming. `tick_seq_` is a plain member (like `ping_interval_seq_` in
// the base) because `timer_fuse::get_if_armed` locks the `weak_ptr` before
// dereferencing the sequencer, so the sequencer is never accessed after the
// transaction is destroyed.
class SimWsHandler final: public http_websocket_transaction {
public:
  // Construct and initialize a new transaction, with its own `SimGame` and
  // tick timer.
  SimWsHandler(request_head&& req, const std::shared_ptr<epoll_loop>& loop,
      const std::shared_ptr<timing_wheel>& wheel)
      : http_websocket_transaction{std::move(req)} {
    // Register methods as callbacks.
    websocket().on_message =
        [this](http_websocket& ws, std::string&& msg, ws_frame_control) {
          return do_message(ws, std::move(msg));
        };
    websocket().on_close = [](http_websocket&, uint16_t, std::string_view) {
      do_close();
    };
    (void)enable_keepalive(loop, wheel, 20s, 5s);
    game_.loadMap();
    std::cout << "WebSocket client connected\n";
  }

  // Returns a `transaction_factory` for use with `http_server::add_route`.
  [[nodiscard]] static transaction_factory make_factory(
      std::shared_ptr<epoll_loop> loop, std::shared_ptr<timing_wheel> wheel) {
    // Factory factory.
    return [loop = std::move(loop), wheel = std::move(wheel)](
               request_head&& req) -> transaction_ptr {
      return std::make_shared<SimWsHandler>(std::move(req), loop, wheel);
    };
  }

private:
  using Fuse = timer_fuse<http_transaction>;

  std::atomic<uint64_t> tick_seq_; // Uses for sequencing tick timers
  WorldTick current_tick_{};       // updated each frame; loop-thread only
  SimGame game_;                   // all simulation entity state
  update_strategy send_strategy_{update_strategy::full};
  size_t buffer_high_watermark_ = 16ULL * 1024; // try to avoid resizes
  size_t erased_ids_high_watermark = 64;        // ditto for erased ID vector

  // Handle an incoming text frame by classifying and forwarding the message.
  [[nodiscard]] bool do_message(http_websocket& ws, std::string&& msg) {
    switch (classify_sim_client_message(msg)) {
      case SimClientMessageKind::hello:
        if (!ws.send_text(R"({"type":"hello_ack","message":"connected"})"))
          return false;
        return do_arm_tick();
      case SimClientMessageKind::ui_canvas:
        return do_ui_canvas(msg);
      case SimClientMessageKind::ui_action:
        return do_ui_action(msg);
      case SimClientMessageKind::unknown:
        break;
    }
    return true;
  }

  [[nodiscard]] bool do_ui_canvas(std::string_view msg) {
    const auto input = parse_ui_canvas_message(msg);
    if (!input) return true; // malformed, ignore
    game_.handle_ui_canvas(*input);
    return true;
  }

  [[nodiscard]] bool do_ui_action(std::string_view msg) {
    const auto input = parse_ui_action_message(msg);
    if (!input) return true; // malformed, ignore
    game_.handle_ui_action(*input);
    return true;
  }

  static void do_close() { std::cout << "WebSocket client disconnected\n"; }

  // Schedule the next tick. Uses the `timer_fuse` double-check pattern:
  // the wheel-thread callback pre-checks liveness and posts to the loop
  // thread, which performs the definitive check and calls `do_tick_fire`.
  [[nodiscard]] bool do_arm_tick() {
    auto wheel = keepalive_wheel_.lock();
    if (!wheel) return true;
    return Fuse::set_timeout(*wheel, tick_seq_,
        std::weak_ptr<http_transaction>{shared_from_this()}, 50ms,
        [loop_w = keepalive_loop_](const Fuse& fuse) -> bool {
          auto tx = fuse.get_if_armed();
          auto loop = loop_w.lock();
          if (!tx || !loop) return true;
          return loop->post([fuse]() -> bool {
            auto tx = fuse.get_if_armed();
            if (!tx) return true;
            return std::static_pointer_cast<SimWsHandler>(tx)->do_tick_fire();
          });
        });
  }

  // Called on the loop thread: advance the simulation one frame and send a
  // world update message at 20 Hz. Re-arms for the next 50 ms interval.
  [[nodiscard]] bool do_tick_fire() {
    if (websocket().is_close_started()) return true;
    if (websocket().is_send_in_fragment()) return do_arm_tick();

    current_tick_ = game_.step();
    if (!send_game_state()) return false;
    send_strategy_ = update_strategy::incremental;
    return do_arm_tick();
  }

  // Stream snapshot of game state to the client as JSON. Uses deltas when
  // possible.
  [[nodiscard]] bool send_game_state() {
    std::string buf;
    buf.reserve(buffer_high_watermark_);
    auto it = std::back_inserter(buf);
    // State to allow comma delimiter management in callbacks.
    bool wrote_any_path = false;
    bool wrote_any_joint = false;
    bool wrote_any_upsert = false;
    bool wrote_any_erased = false;

    // If full send, wrap in `world_snapshot` envelope and include paths.
    if (send_strategy_ == update_strategy::full) {
      (void)game_.markAllDirty(update_strategy::full);
      it = std::format_to(it, R"({{"type":"world_snapshot","paths":[)");
      (void)game_.extractPaths(
          [&it, &wrote_any_path, &wrote_any_joint](auto, const Position& pos) {
            if (wrote_any_joint) it = std::format_to(it, ",");
            wrote_any_path = true;
            wrote_any_joint = true;
            it = std::format_to(it, R"({{"x":{:.1f},"y":{:.1f}}})", pos.x,
                pos.y);
            return true;
          });
      buf += R"(],"delta":)";
      it = std::back_inserter(buf);
    }

    std::vector<SimWorld::EntityId> erasedIds;
    erasedIds.reserve(erased_ids_high_watermark);

    // Display state.
    size_t current_wave{};
    WaveTick wave_tick{};
    int lives_count{};
    int resources_count{};
    std::string_view phase{};

    it = std::format_to(it, R"({{"type":"world_delta","tick":{}, "upserts":[)",
        *current_tick_);

    (void)game_.extractDelta(
        // Upserts.
        [&it, &wrote_any_upsert,
            current_tick = current_tick_](SimWorld::EntityId entityId,
            const Position& pos, const Appearance& app) {
          if (wrote_any_upsert) it = std::format_to(it, ",");
          wrote_any_upsert = true;
          if (app.modified + 1 != current_tick) {
            it = std::format_to(it,
                R"({{"pos":{{"id":{},"x":{:.1f},"y":{:.1f}}}}})", *entityId,
                pos.x, pos.y);
            return true;
          }

          const auto glow_color =
              app.effect_expiry < current_tick ? 0U : app.glow_color;
          it = std::format_to(it,
              R"({{"pos":{{"id":{},"x":{:.1f},"y":{:.1f}}},"app":{{"glyph":{},"scale":{:.3f},"fg":{},"bg":{},"glow":{}}}}})",
              *entityId, pos.x, pos.y, static_cast<uint32_t>(app.glyph),
              app.scale, app.fg_color, app.bg_color, glow_color);
          return true;
        },
        // Erasures.
        [&erasedIds](SimWorld::EntityId entityId) {
          erasedIds.push_back(entityId);
          return true;
        },
        [&current_wave, &wave_tick, &lives_count, &resources_count,
            &phase](auto currentWave, auto waveTick, auto lives,
            auto resources, auto currentPhase) {
          current_wave = currentWave;
          wave_tick = waveTick;
          lives_count = lives;
          resources_count = resources;
          phase = currentPhase;
          return true;
        });

    // Mostly, the future is predicted by the past.
    erased_ids_high_watermark = erasedIds.size();
    it = std::format_to(it, R"(],"erased":[)");
    for (auto entityId : erasedIds) {
      if (wrote_any_erased) it = std::format_to(it, ",");
      wrote_any_erased = true;
      it = std::format_to(it, R"({})", *entityId);
    }
    it = std::format_to(it,
        R"(],"currentWave":{},"waveTick":{},"lives":{},"resources":{},"phase":"{}"}})",
        current_wave, *wave_tick, lives_count, resources_count, phase);

    if (send_strategy_ == update_strategy::full) buf += "}";

    std::string header_buf;
    (void)ws_frame_lens::build(header_buf,
        ws_frame_control::text | ws_frame_control::fin, buf.size(),
        std::nullopt);

    // The past predicts the future, mostly.
    buffer_high_watermark_ = buf.size();

    if (!websocket().send_frame(
            strings::as_vector(std::move(header_buf), std::move(buf))))
      return false;

    return true;
  }
};
}} // namespace corvid::proto
