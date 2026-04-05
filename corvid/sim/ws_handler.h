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
#include <numbers>
#include <cmath>
#include <format>
#include <iostream>
#include <memory>
#include <vector>

#include "../proto/http_websocket_transaction.h"
#include "sim_world.h"

namespace corvid { inline namespace proto {

using namespace std::chrono_literals;

// WebSocket transaction for the CorvidSim `/ws` route.
//
// Inherits from `http_websocket_transaction` and wires up all callbacks in the
// constructor so behaviour lives in named private methods rather than lambdas.
//
// Protocol:
//   Client sends: {"type":"hello","client":"browser"}
//   Server replies: {"type":"hello_ack","message":"connected"}
//   Client sends: {"type":"spawn","x":N,"y":N}
//   Server then sends at 20 Hz: {"type":"snapshot","entities":[...]}
//   Server also sends at 1 Hz: {"type":"tick","tick":N}
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
class sim_ws_transaction final: public http_websocket_transaction {
public:
  sim_ws_transaction(request_head&& req,
      const std::shared_ptr<epoll_loop>& loop,
      const std::shared_ptr<timing_wheel>& wheel)
      : http_websocket_transaction{std::move(req)} {
    websocket().on_message =
        [this](http_websocket& ws, std::string&& msg, ws_frame_control) {
          return do_message(ws, std::move(msg));
        };
    websocket().on_close = [](http_websocket&, uint16_t, std::string_view) {
      do_close();
    };
    // `enable_keepalive` stores weak_ptrs and sets `on_pong`; it always
    // returns `true`, so voiding the result is safe here.
    (void)enable_keepalive(loop, wheel, 20s, 5s);

    // Populate the world with five entities at the origin, evenly fanned
    // around the full circle at the same speed.
    constexpr int kCount = 5000;
    constexpr float kSpeed = 40.0;
    constexpr float kStep = 2.0 * std::numbers::pi / kCount;
    for (int i = 0; i < kCount; ++i)
      (void)world_.spawn(sim::Position{0.0, 0.0},
          sim::Velocity::from_polar(kSpeed, i * kStep));

    std::cout << "WebSocket client connected\n";
  }

  // Returns a `transaction_factory` for use with `http_server::add_route`.
  [[nodiscard]] static transaction_factory make_factory(
      std::shared_ptr<epoll_loop> loop, std::shared_ptr<timing_wheel> wheel) {
    return [loop = std::move(loop), wheel = std::move(wheel)](
               request_head&& req) -> transaction_ptr {
      return std::make_shared<sim_ws_transaction>(std::move(req), loop, wheel);
    };
  }

private:
  using fuse_t = timer_fuse<http_transaction>;

  std::atomic<tick_t> tick_seq_; // Uses for sequencing tick timers
  tick_t current_tick_{0};       // updated each frame; loop-thread only
  sim::sim_world world_;         // all simulation entity state

  // Handle an incoming text frame. Dispatches on `"type"` by substring search
  // (no parser needed for these fixed message shapes).
  [[nodiscard]] bool do_message(http_websocket& ws, std::string&& msg) {
    if (msg.contains(R"("type":"hello")") ||
        msg.contains(R"("type": "hello")"))
    {
      if (!ws.send_text(R"({"type":"hello_ack","message":"connected"})"))
        return false;
      return do_arm_tick(1);
    }
    if (msg.contains(R"("type":"spawn")") ||
        msg.contains(R"("type": "spawn")"))
    {
      return do_spawn(msg);
    }
    return true;
  }

  // Parse a `spawn` message and add a new entity at the click position.
  //
  // The entity travels in a straight line; velocity is set perpendicular to
  // the vector from the canvas centre to the click point (tangential), with
  // a fixed speed of 40 pixels per frame. If the click is exactly on-centre,
  // the entity moves rightward.
  [[nodiscard]] bool do_spawn(std::string_view msg) {
    auto x = parse_coord(msg, R"("x":)");
    auto y = parse_coord(msg, R"("y":)");
    if (!x || !y) return true; // malformed, ignore
    float r = std::sqrt((*x * *x) + (*y * *y));
    constexpr float speed = 40.0;
    sim::Velocity vel =
        (r > 0.0) ? sim::Velocity{-speed * *y / r, speed * *x / r}
                  : sim::Velocity{speed, 0.0};
    (void)world_.spawn(sim::Position{*x, *y}, vel);
    return true;
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
  [[nodiscard]] bool do_arm_tick(tick_t tick_n) {
    auto wheel = keepalive_wheel_.lock();
    if (!wheel) return true;
    return fuse_t::set_timeout(*wheel, tick_seq_,
        std::weak_ptr<http_transaction>{shared_from_this()}, 50ms,
        [loop_w = keepalive_loop_, tick_n](const fuse_t& fuse) -> bool {
          auto tx = fuse.get_if_armed();
          auto loop = loop_w.lock();
          if (!tx || !loop) return true;
          return loop->post([fuse, tick_n]() -> bool {
            auto tx = fuse.get_if_armed();
            if (!tx) return true;
            return std::static_pointer_cast<sim_ws_transaction>(tx)
                ->do_tick_fire(tick_n);
          });
        });
  }

  // Called on the loop thread: advance the simulation one frame, send a
  // snapshot message (20 Hz) and, once per 20 frames, a tick message (1 Hz).
  // Re-arms for the next 50 ms interval. If a fragmented send is in progress,
  // defers by re-arming with the same counter so the sequence has no gaps.
  [[nodiscard]] bool do_tick_fire(tick_t tick_n) {
    if (websocket().is_close_started()) return true;
    if (websocket().is_send_in_fragment()) return do_arm_tick(tick_n);

    current_tick_ = world_.tick();

    auto snaps = world_.snapshot();
    std::string entities;
    entities.reserve(snaps.size() * 40);
    for (const auto& e : snaps) {
      if (!entities.empty()) entities += ',';
      entities += std::format(R"({{"id":{},"x":{:.1f},"y":{:.1f}}})",
          static_cast<std::size_t>(e.id), e.pos.x, e.pos.y);
    }

    auto snap_msg =
        std::format(R"({{"type":"snapshot","entities":[{}]}})", entities);
    if (!websocket().send_text(snap_msg)) return false;

    if (tick_n % 20 == 0) {
      auto tick_msg =
          std::format(R"({{"type":"tick","tick":{}}})", tick_n / 20);
      if (!websocket().send_text(tick_msg)) return false;
    }

    return do_arm_tick(tick_n + 1);
  }
};

}} // namespace corvid::proto
