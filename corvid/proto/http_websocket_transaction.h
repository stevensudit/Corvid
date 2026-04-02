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
#include <functional>
#include <memory>
#include <string>

#include "http_transaction.h"
#include "http_websocket.h"

namespace corvid { inline namespace proto {

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
  // control.
  [[nodiscard]] stream_claim handle_drain(send_fn& send) override {
    if (!websocket_send_) websocket_send_ = send;
    if (!pending_response_.empty()) {
      if (!send(std::move(pending_response_))) return stream_claim::release;
      pending_response_.clear();
    }
    if (on_drain) return on_drain(*this, send);
    return upgraded_ ? stream_claim::claim : stream_claim::release;
  }

  // Build a `transaction_factory` that constructs an
  // `http_websocket_transaction` for each matching request and then calls
  // `configure` on it so the caller can install `on_message` / `on_close`, and
  // perhaps `on_drain`.
  [[nodiscard]] static transaction_factory make_factory(
      std::function<void(http_websocket_transaction&)> configure = {}) {
    return [configure = std::move(configure)](
               request_head&& req) -> std::shared_ptr<http_transaction> {
      auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
      if (configure) configure(*tx);
      return tx;
    };
  }

private:
  // Perform the RFC 6455 upgrade handshake. Called on the first
  // `handle_data` invocation.
  [[nodiscard]] stream_claim do_upgrade(recv_buffer_view& view) {
    // Validate upgrade request.
    if (request_headers.method != http_method::GET)
      return send_bad_request(view);
    if (request_headers.options.upgrade != upgrade_value::websocket)
      return send_bad_request(view);

    const auto conn_hdr = request_headers.headers.get("Connection");
    if (!conn_hdr || !conn_hdr->contains("Upgrade"))
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
    // TODO: Question this.
    // Consume any leftover data already in the buffer (upgrade response
    // has no HTTP body; any subsequent bytes are WebSocket frames, handled
    // on the next `handle_data` call).
    view.consume(view.active_view().size());

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

  http_transaction::send_fn websocket_send_;
  http_websocket websocket_{[this](std::string&& frame) {
    if (!websocket_send_) return false;
    return websocket_send_(std::move(frame));
  }};
  std::string pending_response_;
  bool upgraded_{};
};

}} // namespace corvid::proto
