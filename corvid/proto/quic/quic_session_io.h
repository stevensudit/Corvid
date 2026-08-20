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
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/uio.h>
#include <utility>

#include "../io_uring/iou_dgram_session.h"
#include "quic_conn.h"
#include "quic_ssl_ctx.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region drain_pick

// The stream offer for one `quic_session_io::pump_drain` iteration.
//
// `sid == none` (with an empty `iov`) offers no stream, so the
// `writev_stream` call emits only non-stream frames (ACKs, MAX_DATA, ...).
// Plugins needing to carry extra state from pick to commit supply their own
// type with these three members plus whatever else they need.
struct drain_pick {
  quic_stream_id sid = quic_stream_id::none;
  std::span<const iovec> iov;
  write_stream_flags flags = write_stream_flags::none;
};

#pragma endregion
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
  // first request on an idle connection, a follow-up request fired from
  // inside a response upcall, or a drain retry after `borrow_send_buffer`
  // came up empty).
  //
  // The drain is posted to the loop rather than run inline because
  // ngtcp2/nghttp3 forbid emitting I/O from within a callback; a posted task
  // always runs after the current callback returns. Safe from any thread; the
  // session is kept alive across the hop, and the loop is reached by plain
  // reference rather than a weak handle because the lifetime model has the
  // loop outlive its sessions. A session that closed before the post runs
  // skips the drain, which is also what ends a borrow-failure retry chain on
  // a closed session. Returns false only if the post could not be enqueued.
  [[nodiscard]] bool request_drain() {
    auto keepalive = ssnbase_.shared_from_this();
    return ssnbase_.loop().post(
        [this, keepalive = std::move(keepalive)]() -> bool {
          if (!ssnbase_.is_open()) return true;
          return do_drain_cycle(steady_now_clock::now());
        });
  }

  // Drive ngtcp2's outbound queue until it stops producing, shipping one
  // packet per iteration on its own borrowed buffer (ngtcp2's pacing dictates
  // one packet per `writev_stream` call).
  //
  // This is the loop skeleton every upper-plugin `drain` shares; the plugin
  // supplies the policy through three callables. `pick(P&)` fills in what to
  // offer ngtcp2 (`drain_pick` is the default shape) and returns false on a
  // hard failure. `on_stream_status(quic_status, const P&)` reacts to the
  // per-stream statuses (`stream_data_blocked`, `stream_shut_wr`,
  // `stream_not_found`), after which the loop continues, because those are
  // per-stream conditions, not connection errors, and other streams may still
  // write (`ngtcp2_conn_writev_stream` doc). `commit(const P&, accepted)`
  // reports how many offered bytes ngtcp2 took, once per shipped packet, and
  // returns false on a hard failure. `accepted` is engaged exactly when the
  // packet carried the offered stream's frame (`0` = a zero-length frame,
  // e.g. a pure FIN) and empty when ngtcp2 omitted it; a commit managing
  // sticky flags must not treat an omitted frame as sent (see
  // `quic_conn::writev_stream`).
  //
  // Returns true when ngtcp2 has nothing more to emit this turn (including
  // the draining/closing connection states, and a failed buffer borrow after
  // posting a retry drain); false on a hard failure from ngtcp2, `pick`, or
  // `commit`, which the session turns into a connection close.
  template<typename P = drain_pick, typename PickFn, typename StreamStatusFn,
      typename CommitFn>
  [[nodiscard]] bool pump_drain(steady_now_clock::time_point_t now,
      PickFn&& pick, StreamStatusFn&& on_stream_status, CommitFn&& commit) {
    for (;;) {
      P p{};
      if (!pick(p)) return false;
      auto out = borrow_send_buffer();
      // Pool exhausted: post a retry drain so the queued output does not
      // strand until the next inbound packet or expiry. The retry ends via
      // `request_drain`'s closed guard or, once a borrow succeeds, this
      // loop's own exits.
      if (!out) return request_drain();
      std::optional<uint64_t> accepted;
      const auto status =
          conn().writev_stream(p.sid, p.iov, out, accepted, p.flags, now);
      // Draining/closing is a connection-level state, so ngtcp2 will emit
      // nothing more; a clean stop, not a failure.
      if (status == quic_status::draining || status == quic_status::closing)
        return true;
      if (status == quic_status::stream_data_blocked ||
          status == quic_status::stream_shut_wr ||
          status == quic_status::stream_not_found)
      {
        on_stream_status(status, p);
        continue;
      }
      if (status != quic_status::ok) return false;
      if (out.payload_bytes().empty()) return true;
      if (!commit(p, accepted)) return false;
      (void)send_packet(std::move(out));
    }
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
