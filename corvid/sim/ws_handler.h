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
#include <cmath>
#include <format>
#include <iostream>
#include <memory>

#include "../proto/http_websocket_transaction.h"

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

  std::atomic_uint64_t tick_seq_;

  // Handle an incoming text frame. Detects `hello` by searching the raw JSON
  // string (no parser needed for these fixed message shapes), replies with
  // `hello_ack`, then arms the tick timer.
  [[nodiscard]] bool do_message(http_websocket& ws, std::string&& msg) {
    if (!msg.contains(R"("type":"hello")") &&
        !msg.contains(R"("type": "hello")"))
      return true;
    if (!ws.send_text(R"({"type":"hello_ack","message":"connected"})"))
      return false;
    return do_arm_tick(1);
  }

  static void do_close() { std::cout << "WebSocket client disconnected\n"; }

  // Schedule the next tick. Uses the `timer_fuse` double-check pattern:
  // the wheel-thread callback pre-checks liveness and posts to the loop
  // thread, which performs the definitive check and calls `do_tick_fire`.
  [[nodiscard]] bool do_arm_tick(uint64_t tick_n) {
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

  // Called on the loop thread: send a snapshot message (20 Hz) and, once per
  // 20 frames, a tick message (1 Hz). Re-arms for the next 50 ms interval.
  // If a fragmented send is in progress, defer by re-arming with the same
  // counter so the sequence has no gaps.
  //
  // Five entities orbit the canvas center (320, 240) at varying radii,
  // angular speeds (radians per 50 ms frame), and initial phases, so no two
  // are ever in the same position or in sync.
  [[nodiscard]] bool do_tick_fire(uint64_t tick_n) {
    if (websocket().is_close_started()) return true;
    if (websocket().is_send_in_fragment()) return do_arm_tick(tick_n);

    struct orbit {
      int id;
      double radius;
      double speed; // rad/frame (frame = 50 ms)
      double phase; // initial angle (rad)
    };
    static constexpr orbit orbits[] = {
        {1, 80.0, 0.05, 0.00},
        {2, 140.0, 0.035, 1.05},
        {3, 200.0, 0.025, 2.09},
        {4, 80.0, -0.065, 0.52},
        {5, 170.0, -0.04, 3.67},
    };

    const double cx = 320.0;
    const double cy = 240.0;
    auto t = static_cast<double>(tick_n);

    std::string entities;
    for (const auto& o : orbits) {
      double angle = o.phase + (o.speed * t);
      double x = cx + (o.radius * std::cos(angle));
      double y = cy + (o.radius * std::sin(angle));
      if (!entities.empty()) entities += ',';
      entities +=
          std::format(R"({{"id":{},"x":{:.1f},"y":{:.1f}}})", o.id, x, y);
    }
    auto snapshot =
        std::format(R"({{"type":"snapshot","entities":[{}]}})", entities);
    if (!websocket().send_text(snapshot)) return false;

    if (tick_n % 20 == 0) {
      auto tick_msg =
          std::format(R"({{"type":"tick","tick":{}}})", tick_n / 20);
      if (!websocket().send_text(tick_msg)) return false;
    }

    return do_arm_tick(tick_n + 1);
  }
};

}} // namespace corvid::proto
