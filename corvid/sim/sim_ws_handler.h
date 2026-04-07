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
#include <charconv>
#include <cmath>
#include <format>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "../proto/http_websocket_transaction.h"
#include "../strings/any_strings.h"
#include "sim_game.h"
#include "sim_world.h"

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
//   Client sends: {"type":"spawn","x":N,"y":N}
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
    game_.start_wave();
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

  // Handle an incoming text frame. Dispatches on `"type"` by substring search
  // (no parser needed for these fixed message shapes).
  [[nodiscard]] bool do_message(http_websocket& ws, std::string&& msg) {
    if (msg.contains(R"("type":"hello")") ||
        msg.contains(R"("type": "hello")"))
    {
      if (!ws.send_text(R"({"type":"hello_ack","message":"connected"})"))
        return false;
      return do_arm_tick();
    }
    if (msg.contains(R"("type":"spawn")") ||
        msg.contains(R"("type": "spawn")"))
    {
      return do_spawn(msg);
    }
    return true;
  }

  // Parse a `spawn` message and add a new entity at the click position.
  [[nodiscard]] bool do_spawn(std::string_view msg) {
    const auto pos = parse_position(msg);
    if (!pos) return true; // malformed, ignore

    // Aim new point based on its angle from the center, with a fixed speed.
    const auto [length, direction] = convert::CartesianToPolar(pos->x, pos->y);
    const auto vel = Velocity::fromPolar(40.0, direction);
    (void)vel;
    (void)game_;
    return true;
  }

  // Parse an `{x, y}` position from the fixed message shape used by the sim.
  [[nodiscard]] static std::optional<Position> parse_position(
      std::string_view msg) {
    const auto ox = parse_coord(msg, R"("x":)");
    const auto oy = parse_coord(msg, R"("y":)");
    if (!ox || !oy) return std::nullopt;
    return Position{*ox, *oy};
  }

  // Find `key` in `msg` and parse the number that follows it.
  [[nodiscard]] static std::optional<float>
  parse_coord(std::string_view msg, std::string_view key) {
    auto pos = msg.find(key);
    if (pos == std::string_view::npos) return std::nullopt;
    pos += key.size();
    while (pos < msg.size() && msg[pos] == ' ') ++pos;
    float val{};
    auto [ptr, ec] =
        std::from_chars(msg.data() + pos, msg.data() + msg.size(), val);
    if (ec != std::errc{}) return std::nullopt;
    return val;
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

  [[nodiscard]] bool send_game_state() {
    std::string buf;
    buf.reserve(16ULL * 1024);
    auto it = std::back_inserter(buf);
    // State to allow comma delimiter management in callbacks.
    bool wrote_any_path = false;
    bool wrote_any_joint = false;
    bool wrote_any_upsert = false;
    bool wrote_any_erased = false;

    // If full send, wrap in `world_snapshot` envelope and include paths.
    if (send_strategy_ == update_strategy::full) {
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
    erasedIds.reserve(64);

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

    if (!websocket().send_frame(
            strings::as_vector(std::move(header_buf), std::move(buf))))
      return false;

    return true;
  }
};
}} // namespace corvid::proto
