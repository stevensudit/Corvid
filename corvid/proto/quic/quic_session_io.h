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

#pragma endregion
protected:
  quic_session_io(iouring::iou_dgram_session_base& ssnbase,
      quic_ssl_ctx& tls) noexcept
      : ssnbase_{ssnbase}, conn_{tls} {}

#pragma region Data members
private:
  iouring::iou_dgram_session_base& ssnbase_;
  quic_conn conn_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
