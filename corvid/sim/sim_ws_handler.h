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
#include <iostream>
#include <memory>

#include "../proto/http_websocket_transaction.h"
#include "../strings/any_strings.h"
#include "sim_game.h"
#include "sim_json_parse.h"
#include "sim_json_wire.h"

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
  SimGameStateJson json_buffer_; // persistent buffer and high-watermark

  // Handle an incoming text frame by classifying and forwarding the message.
  [[nodiscard]] bool do_message(http_websocket& ws, std::string&& msg) {
    const auto root = parseSimClientMessageRoot(msg);
    if (!root) return true; // malformed, ignore

    switch (classifySimClientMessage(*root)) {
    case SimClientMessageKind::hello:
      if (!ws.send_text(buildSimHelloAckJson())) return false;
      return do_arm_tick();
    case SimClientMessageKind::ui_canvas: return do_ui_canvas(*root);
    case SimClientMessageKind::ui_action: return do_ui_action(*root);
    case SimClientMessageKind::unknown: break;
    }
    return true;
  }

  [[nodiscard]] bool do_ui_canvas(json_object_view msg) {
    const auto input = parseUiCanvasMessage(msg);
    if (!input) return true; // malformed, ignore
    if (const auto response = game_.handleUiCanvas(*input))
      return websocket().send_text(buildSimUiResponseJson(*response));
    return true;
  }

  [[nodiscard]] bool do_ui_action(json_object_view msg) {
    const auto input = parseUiActionMessage(msg);
    if (!input) return true; // malformed, ignore
    if (const auto response = game_.handleUiAction(*input))
      return websocket().send_text(buildSimUiResponseJson(*response));
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

    (void)game_.next();
    if (!send_game_state()) return false;
    send_strategy_ = update_strategy::incremental;
    current_tick_ = game_.tick();
    return do_arm_tick();
  }

  // Stream snapshot of game state to the client as JSON. Uses deltas when
  // possible.
  [[nodiscard]] bool send_game_state() {
    (void)buildSimGameStateJson(json_buffer_, game_, send_strategy_);

    std::string header_buf;
    (void)ws_frame_lens::build(header_buf,
        ws_frame_control::text | ws_frame_control::fin,
        json_buffer_.body.size(), std::nullopt);

    if (!websocket().send_frame(strings::as_vector(std::move(header_buf),
            std::move(json_buffer_.body))))
      return false;

    return true;
  }
};
}} // namespace corvid::proto
