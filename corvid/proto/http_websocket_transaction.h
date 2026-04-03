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
#include <functional>
#include <memory>
#include <string>

#include "epoll_loop.h"
#include "http_transaction.h"
#include "http_websocket.h"
#include "../concurrency/timer_fuse.h"

namespace corvid { inline namespace proto {

using namespace std::chrono_literals;

// HTTP transaction that performs the WebSocket upgrade handshake and then
// delegates all subsequent data flow to an `http_websocket` instance.
//
// `handle_data` (first call):
//   Validates the upgrade request, sends a `101 Switching Protocols`
//   response, and returns `stream_claim::claim` to hold the input stream
//   permanently. On validation failure, sends `400 Bad Request` and
//   returns `stream_claim::release`.
//
// `handle_data` (subsequent calls):
//   Forwards the receive buffer to `websocket_.feed()`. Returns `claim`
//   normally; returns `release` if `feed` returns false (protocol error
//   or close frame received).
//
// `handle_drain`:
//   Returns `stream_claim::claim` unconditionally. The WebSocket output
//   stream stays alive until the close handshake completes (handled via
//   `send_close` inside `on_close`), not merely until the send queue drains.
//
// After upgrade, `http_phase` never returns to `request_line`; the pipeline
// is permanently fixed on this transaction until the connection closes.
class http_websocket_transaction final: public http_transaction {
public:
  using duration_t = timing_wheel::duration_t;
  using configure_fn = std::function<bool(http_websocket_transaction&)>;

  explicit http_websocket_transaction(request_head&& req)
      : http_transaction{std::move(req)} {}

  // Access the WebSocket pump to install `on_message` / `on_close`
  // callbacks before the connection is upgraded.
  [[nodiscard]] http_websocket& websocket() noexcept { return websocket_; }

  [[nodiscard]] stream_claim handle_data(recv_buffer_view& view) override {
    if (!upgraded_) return do_upgrade(view);
    if (!websocket_.feed(view)) return stream_claim::release;
    return stream_claim::claim;
  }

  // The WebSocket output never completes via send-queue drain; return
  // `claim` unconditionally. Calls `on_drain`, if set, in order to offer flow
  // control. Returns `release` once the close handshake is complete.
  [[nodiscard]] stream_claim handle_drain(const send_fn& send) override {
    if (!websocket_send_) websocket_send_ = send;
    if (!pending_response_.empty()) {
      if (!send(std::move(pending_response_))) return stream_claim::release;
      pending_response_.clear();
      // Start keepalive cycle after the upgrade response is sent.
      if (!do_arm_ping_interval()) return stream_claim::release;
    }
    // If the close handshake is complete, shut down gracefully after flush.
    if (websocket_.close_pending()) {
      close_after = after_response::close;
      return stream_claim::release;
    }
    if (on_drain) return on_drain(*this, send);
    return upgraded_ ? stream_claim::claim : stream_claim::release;
  }

  // Enable WebSocket-level ping/pong keepalive.
  //
  // After the upgrade handshake completes, sends a ping every `ping_interval`.
  // The ping payload is a 4-byte big-endian counter. A matching pong resets
  // the interval timer. If no matching pong arrives within `pong_timeout`, the
  // server sends a close frame (code 1001) to begin graceful shutdown.
  //
  // Requirement: `ping_interval + pong_timeout` must be less than the HTTP
  // server's read timeout to prevent the server from closing the connection
  // before the keepalive does.
  //
  // Call from the `configure` callback passed to `make_factory`.
  [[nodiscard]] bool enable_keepalive(const std::shared_ptr<epoll_loop>& loop,
      const std::shared_ptr<timing_wheel>& wheel,
      duration_t ping_interval = 30s, duration_t pong_timeout = 10s) {
    keepalive_loop_ = std::weak_ptr{loop};
    keepalive_wheel_ = std::weak_ptr{wheel};
    ping_interval_ = ping_interval;
    pong_timeout_ = pong_timeout;
    websocket_.on_pong = [this](http_websocket&) {
      ws_fuse_t::disarm(pong_wait_seq_);
      return do_arm_ping_interval();
    };
    return true;
  }

  // Build a `transaction_factory` that constructs an
  // `http_websocket_transaction` for each matching request and then calls
  // `configure` on it so the caller can install `on_message` / `on_close`, and
  // perhaps `on_drain`.
  [[nodiscard]] static transaction_factory make_factory(
      configure_fn configure = {}) {
    return [configure = std::move(configure)](
               request_head&& req) -> std::shared_ptr<http_transaction> {
      auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
      if (configure && !configure(*tx)) return nullptr;
      return tx;
    };
  }

private:
  using ws_fuse_t = timer_fuse<http_transaction>;

  // Perform the RFC 6455 upgrade handshake. Called on the first
  // `handle_data` invocation.
  [[nodiscard]] stream_claim do_upgrade(recv_buffer_view& view) {
    // Validate upgrade request.
    if (request_headers.method != http_method::GET)
      return send_bad_request(view);
    if (request_headers.options.upgrade != upgrade_value::websocket)
      return send_bad_request(view);

    const auto version_hdr =
        request_headers.headers.get("Sec-Websocket-Version");
    if (!version_hdr || *version_hdr != "13") return send_bad_request(view);

    const auto key_hdr = request_headers.headers.get("Sec-Websocket-Key");
    if (!key_hdr || key_hdr->empty()) return send_bad_request(view);

    const auto accept = ws_frame_codec::compute_accept_key(*key_hdr);
    if (accept.empty()) return send_bad_request(view);

    // Build 101 Switching Protocols response.
    response_head resp;
    resp.version = request_headers.version;
    resp.status_code = http_status_code::SWITCHING_PROTOCOLS;
    resp.reason = "Switching Protocols";
    if (!resp.headers.add_raw("Upgrade", "websocket"))
      return stream_claim::release;
    if (!resp.headers.add_raw("Connection", "Upgrade"))
      return stream_claim::release;
    if (!resp.headers.add_raw("Sec-Websocket-Accept", accept))
      return stream_claim::release;

    pending_response_ = resp.serialize();
    upgraded_ = true;
    return stream_claim::claim;
  }

  [[nodiscard]] stream_claim send_bad_request(recv_buffer_view& view) {
    view.consume(view.active_view().size());
    pending_response_ = response_head::make_error_response(
        after_response::close, request_headers.version,
        http_status_code::BAD_REQUEST, "Bad Request");
    return stream_claim::release;
  }

  // Schedule the next ping after `ping_interval_`. No-op if keepalive is not
  // enabled. Uses the double-check `timer_fuse` pattern: pre-check on the
  // wheel thread, definitive check after posting to the loop thread.
  [[nodiscard]] bool do_arm_ping_interval() {
    auto wheel = keepalive_wheel_.lock();
    if (!wheel) return true;
    return ws_fuse_t::set_timeout(*wheel, ping_interval_seq_,
        std::weak_ptr<http_transaction>{shared_from_this()}, ping_interval_,
        [loop = keepalive_loop_](const ws_fuse_t& fuse) -> bool {
          auto tx = fuse.get_if_armed();
          auto l = loop.lock();
          if (!tx || !l) return true;
          return l->post([fuse]() -> bool {
            auto tx = fuse.get_if_armed();
            if (!tx) return true;
            return std::static_pointer_cast<http_websocket_transaction>(tx)
                ->do_ping_interval_fire();
          });
        });
  }

  // Arm the pong-wait timer. If no matching pong arrives within
  // `pong_timeout_`, `do_pong_timeout_fire` is called.
  [[nodiscard]] bool do_arm_pong_timeout() {
    auto wheel = keepalive_wheel_.lock();
    if (!wheel) return true;
    return ws_fuse_t::set_timeout(*wheel, pong_wait_seq_,
        std::weak_ptr<http_transaction>{shared_from_this()}, pong_timeout_,
        [loop = keepalive_loop_](const ws_fuse_t& fuse) -> bool {
          auto tx = fuse.get_if_armed();
          auto l = loop.lock();
          if (!tx || !l) return true;
          return l->post([fuse]() -> bool {
            auto tx = fuse.get_if_armed();
            if (!tx) return true;
            return std::static_pointer_cast<http_websocket_transaction>(tx)
                ->do_pong_timeout_fire();
          });
        });
  }

  // Called on the loop thread when `ping_interval_` has elapsed: send a ping
  // and arm the pong-wait timer.
  [[nodiscard]] bool do_ping_interval_fire() {
    if (websocket_.is_closing()) return false;
    if (!websocket_.send_ping()) return false;
    return do_arm_pong_timeout();
  }

  // Called on the loop thread when `pong_timeout_` expires without a
  // matching pong: begin a graceful close.
  [[nodiscard]] bool do_pong_timeout_fire() {
    if (websocket_.is_closing()) return false;
    return websocket_.send_close(1001, "Keepalive pong not received.");
  }

  http_transaction::send_fn websocket_send_;
  http_websocket websocket_{[this](std::string&& frame) {
    if (!websocket_send_) return false;
    return websocket_send_(std::move(frame));
  }};
  std::string pending_response_;
  bool upgraded_{};

  std::weak_ptr<epoll_loop> keepalive_loop_;
  std::weak_ptr<timing_wheel> keepalive_wheel_;
  duration_t ping_interval_{30s};
  duration_t pong_timeout_{10s};
  std::atomic_uint64_t ping_interval_seq_;
  std::atomic_uint64_t pong_wait_seq_;
};
}} // namespace corvid::proto
