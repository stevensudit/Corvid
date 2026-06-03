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
#include <utility>

#include "../io_uring/iou_dgram_session.h"
#include "quic_conn.h"
#include "quic_ssl_ctx.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_session_io

// Non-templated pairing of a `quic_conn` with the io_uring datagram session
// that carries its packets. Owns the `quic_conn`; holds a reference to the
// session's non-templated base for `borrow_send_buffer` / `send_packet` /
// loop-thread checks.
//
// Upper-layer plugins (echo, HTTP/3, ...) hold a `quic_session_io&` rather
// than the templated session type, so the plugin contract has no dependence on
// `quic_dgram_protocol<QuicPlugin>::session_plugin`.
//
// `quic_dgram_protocol<QuicPlugin>::session_plugin` inherits this publicly: it
// acts as the `iou_dgram_session` plugin AND exposes the `quic_session_io`
// surface to the upper plugin it owns.
class quic_session_io {
public:
  using buffer = iouring::iou_loop::buffer;

  quic_session_io(const quic_session_io&) = delete;
  quic_session_io(quic_session_io&&) = delete;
  quic_session_io& operator=(const quic_session_io&) = delete;
  quic_session_io& operator=(quic_session_io&&) = delete;

#pragma region Accessors

  [[nodiscard]] auto& conn(this auto& self) noexcept { return self.conn_; }
  [[nodiscard]] bool is_loop_thread() const noexcept {
    return ssnbase_.loop().is_loop_thread();
  }

  // The configured server name, with a role-dependent meaning. For a client
  // session it is the TLS SNI sent in the handshake and the default request
  // `:authority`. For a server session it is the authority the server answers
  // for, which the HTTP/3 layer matches against each request's `:authority`
  // (`http3_server_stream`'s misdirected-request gate). Empty when none was
  // configured.
  [[nodiscard]] const std::string& server_name() const noexcept {
    return server_name_;
  }

#pragma endregion
#pragma region I/O

  // Borrow a buffer for sending. Forwards to the session base; the buffer is
  // owned by the loop's pool and returns there on completion. Safe from any
  // thread.
  [[nodiscard]] buffer borrow_send_buffer() const {
    return ssnbase_.borrow_send_buffer();
  }

  // Stamp the packet's peer address from the conn's bound peer and ship it
  // through the session's send path. The buffer returns to the owning plugin
  // via `handle_sent` on completion. Safe from any thread (the session base's
  // `send` is). On error (closed session, etc.) the returned token is invalid;
  // the buffer is consumed regardless.
  iouring::iou_loop::completion_token send_packet(buffer&& buf) noexcept {
    buf.peer_addr() = conn_.peer();
    return ssnbase_.send(std::move(buf));
  }

  // Ask the session to run an outbound turn soon, for use after the upper
  // plugin queues work that did not originate from an inbound packet (the
  // first request on an idle connection, or a follow-up request fired from
  // inside a response upcall). The drain is posted to the loop rather than run
  // inline because ngtcp2/nghttp3 forbid emitting I/O from within a callback;
  // a posted task always runs after the current callback returns. Safe from
  // any thread; the session is kept alive across the hop. Returns false only
  // if the post could not be enqueued.
  [[nodiscard]] bool request_drain() {
    auto keepalive = ssnbase_.shared_from_this();
    return ssnbase_.loop().post(
        [this, keepalive = std::move(keepalive)]() -> bool {
          return do_drain_cycle(steady_now_clock::now());
        });
  }

#pragma endregion
protected:
  quic_session_io(iouring::iou_dgram_session_base& ssnbase, quic_ssl_ctx& tls,
      std::string server_name = {}) noexcept
      : ssnbase_{ssnbase}, conn_{tls}, server_name_{std::move(server_name)} {}

  // One outbound turn: flush queued packets and re-arm expiry. Implemented by
  // `session_plugin`, which owns the drain / close / expiry machinery;
  // `request_drain` posts it to the loop.
  [[nodiscard]] virtual bool do_drain_cycle(
      steady_now_clock::time_point_t now) = 0;

#pragma region Data members
private:
  iouring::iou_dgram_session_base& ssnbase_;
  quic_conn conn_;
  std::string server_name_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
