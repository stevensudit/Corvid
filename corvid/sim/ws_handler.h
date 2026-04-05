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

#include "../proto/http_websocket_transaction.h"

// WebSocket handler for the CorvidSim /ws route.
//
// Provides `make_ws_factory(loop, wheel)`, which returns a
// `transaction_factory` suitable for use with `http_server::add_route`.
//
// Protocol:
//   Client sends: {"type":"hello","client":"browser"}
//   Server replies: {"type":"hello_ack","message":"connected"}
//   Server then sends once per second: {"type":"tick","tick":N}
//
// Tick scheduling follows the same `timer_fuse` double-check pattern used by
// `http_websocket_transaction::do_arm_ping_interval`: the wheel-thread
// callback only calls `loop->post()`, and the loop-thread callback performs
// the definitive liveness check before sending and re-arming.
namespace corvid { inline namespace proto {

using namespace std::chrono_literals;

// Forward declaration: `arm_tick`'s fire lambda calls `arm_tick` recursively,
// so the definition below must be preceded by a declaration.
[[nodiscard]] inline bool arm_tick(std::weak_ptr<epoll_loop>,
    std::weak_ptr<timing_wheel>, std::weak_ptr<http_transaction>,
    std::shared_ptr<std::atomic_uint64_t>, uint64_t);

// Schedule a tick to be sent after 1 second on the connection identified by
// `tx_w`. On fire, posts to the event loop thread, sends the tick JSON, then
// re-arms for the next tick. Stops automatically when the connection is
// destroyed (fuse fizzles) or when `is_close_started()` is true.
//
// `tick_seq` is the per-connection sequencer held as a `shared_ptr` so that
// every lambda copy keeps the underlying `atomic_uint64_t` alive (required
// because `timer_fuse` stores a raw pointer to it).
[[nodiscard]] inline bool arm_tick(std::weak_ptr<epoll_loop> loop_w,
    std::weak_ptr<timing_wheel> wheel_w, std::weak_ptr<http_transaction> tx_w,
    std::shared_ptr<std::atomic_uint64_t> tick_seq, uint64_t tick_n) {
  using fuse_t = timer_fuse<http_transaction>;
  auto wheel = wheel_w.lock();
  if (!wheel) return true;
  return fuse_t::set_timeout(*wheel, *tick_seq, tx_w, 1000ms,
      [loop_w, wheel_w, tick_seq, tick_n](const fuse_t& fuse) -> bool {
        // Wheel thread: pre-check only.
        auto tx = fuse.get_if_armed();
        auto loop = loop_w.lock();
        if (!tx || !loop) return true;
        return loop->post([fuse, loop_w, wheel_w, tick_seq, tick_n]() -> bool {
          // Loop thread: definitive check, send, re-arm.
          auto tx = fuse.get_if_armed();
          if (!tx) return true;
          auto wstx = std::static_pointer_cast<http_websocket_transaction>(tx);
          if (wstx->websocket().is_close_started()) return true;
          auto payload = std::format(R"({{"type":"tick","tick":{}}})", tick_n);
          if (!wstx->websocket().send_text(payload)) return false;
          return arm_tick(loop_w, wheel_w, tx, tick_seq, tick_n + 1);
        });
      });
}

// Returns a `transaction_factory` for the `/ws` route.
//
// Each accepted WebSocket connection receives `on_message` and `on_close`
// callbacks. On a `hello` message, the server replies with `hello_ack` and
// arms the 1-second recurring tick timer for that connection.
[[nodiscard]] inline transaction_factory make_ws_factory(
    std::shared_ptr<epoll_loop> loop, std::shared_ptr<timing_wheel> wheel) {
  return http_websocket_transaction::make_factory(
      [loop = std::move(loop), wheel = std::move(wheel)](
          http_websocket_transaction& tx) -> bool {
        std::cout << "WebSocket client connected\n";

        tx.websocket().on_close =
            [](http_websocket&, uint16_t, std::string_view) {
              std::cout << "WebSocket client disconnected\n";
            };

        // Per-connection tick sequencer. Held as `shared_ptr` so every lambda
        // copy keeps the `atomic_uint64_t` alive (see `arm_tick` comment).
        auto tick_seq = std::make_shared<std::atomic_uint64_t>(0);

        tx.websocket().on_message =
            [loop_w = std::weak_ptr{loop}, wheel_w = std::weak_ptr{wheel},
                tx_w = std::weak_ptr<http_transaction>{tx.shared_from_this()},
                tick_seq](http_websocket& ws, std::string&& msg,
                ws_frame_control) -> bool {
          // No JSON parser: detect `hello` by searching the raw string.
          // Accept both compact and spaced key/value.
          const bool is_hello =
              msg.find(R"("type":"hello")") != std::string::npos ||
              msg.find(R"("type": "hello")") != std::string::npos;
          if (!is_hello) return true;

          if (!ws.send_text(R"({"type":"hello_ack","message":"connected"})"))
            return false;

          return arm_tick(loop_w, wheel_w, tx_w, tick_seq, 1);
        };

        return true;
      });
}

}} // namespace corvid::proto
