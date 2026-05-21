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
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include <openssl/rand.h>

#include "../../concurrency/timeouts.h"
#include "../io_uring/iou_dgram_router.h"
#include "../io_uring/iou_dgram_session.h"
#include "quic_conn.h"
#include "quic_header.h"
#include "quic_ssl_ctx.h"

// `iou_dgram_router_plugin`/`iou_dgram_session_plugin` pair that demuxes UDP
// datagrams to `iou_dgram_session`s by QUIC Destination Connection ID, plus
// the upper-layer plugin shape that HTTP/3 (and potentially other protocols,
// such as DNS and SMB) sits on.
//
// The router-side plugin parses each datagram's header (long or short) by
// constructing a `quic_version_cid` over the packet bytes and returns the
// DCID as the routing key. The session-side plugin owns a `quic_conn`
// (ngtcp2 state machine + per-conn SSL) and drives its read/write/expiry
// cycle. After each processed datagram, the session calls the upper-layer
// plugin's `drain(now)` so the plugin can queue outbound stream bytes
// before the session emits packets.

namespace corvid { inline namespace proto { namespace quic {

#pragma region no_op_plugin

// Upper-layer plugin contract (duck-typed, enforced by the session
// template):
//   * Constructor accepting `session_plugin&` (so the plugin captures the
//     session ref and reaches the router / `quic_conn` through it).
//   * Inherits `quic_conn_handlers` so the session can install it as the
//     `quic_conn`'s upcall target via `set_handlers`.
//   * `bool drain(time_point_t now) noexcept` - per-turn hook fired after
//     every successful `quic_conn::read_pkt`, on the loop thread, before
//     the session emits outbound packets. The bytes from the incoming
//     datagram are *not* passed in: by the time this hook fires,
//     `read_pkt` has already decrypted them and dispatched them through
//     `quic_conn_handlers` upcalls. Stream data is the plugin's chance
//     to call `quic_conn::writev_stream` to push queued bytes into
//     ngtcp2, which the session's `drain_writes` then packs into UDP
//     packets via `write_pkt`.
class quic_no_op_plugin: public quic_conn_handlers {
public:
  template<typename Session>
  explicit quic_no_op_plugin(Session&) noexcept {}

  bool drain(timeouts::time_point_t /*now*/) noexcept {
    (void)this;
    return true;
  }
};

#pragma endregion
#pragma region dgram_protocol

// Bundle of router and session plugin types for QUIC CID-keyed routing
// over `iou_dgram_router`, templatized on the upper-layer plugin type.
// Matches the shape of `iouring::iou_dgram_echo_protocol`:
// `router_plugin` decides routing, `session_plugin` owns per-connection
// state.
template<typename QuicPlugin = quic_no_op_plugin>
class quic_dgram_protocol {
public:
#pragma region Types

  using buffer = iouring::iou_loop::buffer;
  using time_point_t = timeouts::time_point_t;
  using quic_plugin_t = QuicPlugin;

  static constexpr size_t cid_length = quic_version_cid::default_scid_length;

  class session_plugin;

#pragma endregion
#pragma region router_plugin

  class router_plugin {
  public:
    using session_t = iouring::iou_dgram_session<session_plugin>;
    using router_t = iouring::iou_dgram_router<router_plugin>;
    using key_t = quic_cid;

#pragma region Construction

    // The TLS context whose role determines this router's mode and whose
    // cert/key (server role) or verify settings (client role) flow into new
    // sessions. Held by reference; must outlive the router.
    //
    // A server-role context lets `create_session` turn unsolicited Initials
    // into server-side sessions. A client-role context disables
    // `create_session` (unsolicited inbound is dropped); client sessions
    // are instead constructed explicitly via `session_plugin::make_client`
    // and registered under their own SCID.
    explicit router_plugin(quic_ssl_ctx& tls) noexcept : tls_{tls} {}

#pragma endregion
#pragma region Accessors

    [[nodiscard]] quic_ssl_ctx& tls() const noexcept { return tls_; }

#pragma endregion
#pragma region Routing

    // Recover the DCID from the packet header.
    [[nodiscard]] key_t extract(const buffer& buf) const noexcept {
      (void)this;
      const quic_version_cid vc{buf.payload_bytes(), cid_length};
      if (!vc) return {};
      return key_t{vc.dcid_bytes()};
    }

    // Called when the router has no session registered under the
    // extracted DCID. Server-role routers turn long-header (Initial)
    // packets into new sessions; short-header packets to unknown CIDs are
    // dropped (a future revision may emit a stateless reset instead, per
    // RFC 9000). Client-role routers always drop: a client never accepts
    // server-driven inbound.
    bool create_session(const buffer& buf, router_t& router) {
      if (tls_.role() != connection_role::server) return false;
      const quic_version_cid vc{buf.payload_bytes(), cid_length};
      if (!vc || !vc.is_long_header()) return false;
      return session_plugin::make_server(router, tls_, key_t{vc.scid_bytes()},
                 key_t{vc.dcid_bytes()}, buf.peer_addr()) != nullptr;
    }

#pragma endregion
#pragma region Data members
  private:
    quic_ssl_ctx& tls_;

#pragma endregion
  };

#pragma endregion
#pragma region session_plugin

  // Per-connection state: owns the `quic_conn` (ngtcp2 + per-conn SSL),
  // routes incoming datagrams through `quic_conn::read_pkt`, drives
  // `quic_conn::write_pkt` through the router's UDP socket, and arms
  // one expiry-sweeper entry against `iou_loop::timeouts()` to drive
  // loss-detection / PTO / handshake timers.
  //
  // Server-side sessions register under two CIDs:
  //   * the client's original DCID (the routing key for the Initial
  //     packet that created the session, and for any Initial
  //     retransmissions that arrive before the client switches over);
  //   * the server's freshly-generated primary SCID (the routing key
  //     for every subsequent client packet, which uses the server's
  //     SCID as its DCID).
  //
  // Client-side sessions register under their own SCID only: inbound packets
  // from the server always address us by the SCID we chose; the initial DCID
  // we put on the wire for our first Initial is never seen on inbound.
  //
  // Additional CIDs issued by ngtcp2 through `get_new_connection_id2` are not
  // yet registered with the router: that is the next-next milestone (CID
  // rotation / migration).
  class session_plugin {
  public:
    using router_t = iouring::iou_dgram_router<router_plugin>;
    using session_t = iouring::iou_dgram_session<session_plugin>;
    using key_t = quic_cid;

#pragma region Construction
  public:
    // Server-side construction. Builds a `quic_conn` in the server role bound
    // to `server_tls`. Reached via the `make_server` factory below, which
    // `router_plugin::create_session` calls when an Initial arrives for an
    // unknown DCID.
    session_plugin(router_t& router, session_t& session,
        quic_ssl_ctx& server_tls, const key_t& peer_scid,
        const key_t& original_dcid, const net_endpoint& local,
        const net_endpoint& peer) noexcept
        : router_{router}, session_{session}, original_dcid_{original_dcid},
          scid_{make_random_cid(quic_dgram_protocol::cid_length)},
          conn_{server_tls, peer_scid, scid_, original_dcid, local, peer,
              timeouts::now()},
          plugin_{*this} {
      conn_.set_handlers(&plugin_);
    }

    // Client-side construction. Builds a `quic_conn` in the client role
    // bound to `client_tls`, generating a fresh initial DCID (which we
    // put on the wire for our first Initial, never received) and SCID
    // (which the router will register us under). Reached via the
    // `make_client` factory below; not used by the router.
    session_plugin(router_t& router, session_t& session,
        quic_ssl_ctx& client_tls, const net_endpoint& local,
        const net_endpoint& peer) noexcept
        : router_{router}, session_{session},
          scid_{make_random_cid(quic_dgram_protocol::cid_length)},
          conn_{client_tls, make_random_cid(quic_dgram_protocol::cid_length),
              scid_, local, peer, timeouts::now()},
          plugin_{*this} {
      conn_.set_handlers(&plugin_);
    }

    // Static factory for server-side construction. Used by
    // `router_plugin::create_session` once it has parsed the Initial's peer
    // SCID and original DCID out of the wire header.Returns null if
    // `quic_conn` construction failed (e.g., `server_tls` is client-role).
    [[nodiscard]] static std::shared_ptr<session_t> make_server(
        router_t& router, quic_ssl_ctx& server_tls, const key_t& peer_scid,
        const key_t& original_dcid, const net_endpoint& peer) {
      auto ssn = session_t::make_unregistered(router, server_tls, peer_scid,
          original_dcid, router.local_endpoint(), peer);
      if (!ssn->plugin().conn_) return {};
      (void)ssn->plugin().register_self({});
      return ssn;
    }

    // Static factory for client-side construction. Returns the session ptr
    // immediately; the handshake progresses asynchronously through the router.
    // Safe to call from any thread. Returns null if `quic_conn` construction
    // failed (e.g., `client_tls` is server-role).
    [[nodiscard]] static std::shared_ptr<session_t>
    make_client(router_t& router, const net_endpoint& peer) {
      auto ssn = session_t::make_unregistered(router, router.plugin().tls(),
          router.local_endpoint(), peer);
      if (!ssn->plugin().conn_) return {};
      (void)router.loop().execute_or_post([ssn]() mutable {
        return ssn->plugin().register_self({});
      });
      return ssn;
    }

#pragma endregion
#pragma region Registration

    bool register_self(const buffer&) {
      bool ok1 = true;
      if (conn_.role() == connection_role::server)
        ok1 = router_.add_session(original_dcid_, session_.self());
      const bool ok2 = router_.add_session(scid_, session_.self());
      drain_writes(timeouts::now());
      arm_expiry();
      return ok1 && ok2;
    }

    bool unregister_self() {
      const bool ok1 = router_.remove_session(scid_);
      bool ok2 = true;
      if (conn_.role() == connection_role::server)
        ok2 = router_.remove_session(original_dcid_);
      return ok1 && ok2;
    }

#pragma endregion
#pragma region I/O

    // Feed the datagram into ngtcp2, let the upper plugin react, then
    // drain anything ngtcp2 wants to send. `now` is snapped once at the
    // top of the turn and threaded through every callee so every
    // operation sees the same wall-clock view. Loop-thread only,
    // asserted.
    bool handle_recv(buffer&& buf) {
      assert(router_.loop().is_loop_thread());
      const auto now = timeouts::now();
      const auto rv = conn_.read_pkt(buf.payload_bytes(), now);
      if (rv != quic_decode_status::ok) return false;
      // `drain` lets the plugin queue outbound stream bytes (via
      // `quic_conn::writev_stream`) before `drain_writes` packs them
      // into UDP packets. Stream data and other higher-layer events
      // already reached the plugin during `read_pkt` through the
      // `quic_conn_handlers` upcalls.
      (void)plugin_.drain(now);
      drain_writes(now);
      arm_expiry();
      return true;
    }

    // The send buffer comes back here once the kernel has accepted the
    // datagram. There is nothing to do at the QUIC layer: ngtcp2's own
    // ACK / loss-detection machinery (driven via the sweeper-armed
    // expiry timer) handles retransmits, and the buffer returns to the
    // pool when this frame unwinds.
    //
    // HTTP/3 cares about stream-level send acceptance, not UDP-level
    // send acceptance, and ngtcp2 tracks per-stream offsets and ACKs
    // internally. When nghttp3 sits on top, its data callbacks will
    // be driven by ngtcp2's own ACK processing, not by this hook.
    bool handle_sent(buffer&& buf) noexcept {
      buf.reset();
      return true;
    }

#pragma endregion
#pragma region Accessors

    [[nodiscard]] router_t& router() noexcept { return router_; }
    [[nodiscard]] session_t& session() noexcept { return session_; }
    [[nodiscard]] quic_conn& conn() noexcept { return conn_; }
    [[nodiscard]] quic_plugin_t& protocol_plugin() noexcept { return plugin_; }

    // The server's freshly-generated SCID, used as the primary CID for
    // routing packets after the client switches off the Initial DCID.
    // Exposed for tests; the full set of keys also includes
    // `original_dcid_` until the client migrates.
    [[nodiscard]] const key_t& primary_cid() const noexcept { return scid_; }

#pragma endregion
#pragma region Expiry

    // Sweeper-callback entry point. Invoked by `iou_loop::timeouts()`
    // when the registered deadline elapses. Stale entries (left over
    // from a deadline that was superseded by an earlier rearm) detect
    // themselves via `fired_expire != registered_expiry_` and drop.
    [[nodiscard]] time_point_t on_expiry_sweep(
        time_point_t fired_expire) noexcept {
      if (fired_expire != registered_expiry_) return {};
      const auto now = timeouts::now();
      const auto target = conn_.expiry();
      if (target > now) {
        registered_expiry_ = target;
        return target;
      }
      (void)conn_.handle_expiry(now);
      drain_writes(now);
      const auto next = conn_.expiry();
      registered_expiry_ = next;
      return next;
    }

#pragma endregion
#pragma region Helpers
  private:
    // Drain ngtcp2's outbound queue. ngtcp2's pacing dictates one packet
    // per `write_pkt` call; we loop until it reports nothing more to
    // send (an empty post-call payload) or an error. Each packet rides
    // its own borrowed buffer through the router socket. `now` is
    // supplied by the caller so every `write_pkt` in a single turn sees
    // the same clock reading.
    void drain_writes(time_point_t now) {
      while (true) {
        auto out = session_.borrow_send_buffer();
        if (!out) return;
        const auto status = conn_.write_pkt(out, now);
        if (status != quic_decode_status::ok) return;
        if (out.payload_bytes().empty()) return;
        out.peer_addr() = conn_.peer();
        (void)session_.send(std::move(out));
      }
    }

    // Schedule (or reschedule) the expiry-sweeper entry. The sweeper
    // has no cancel API, so an existing entry that was scheduled at a
    // later deadline becomes stale and self-cancels on its next fire
    // (via the `fired_expire != registered_expiry_` check above).
    // Skipping the schedule when the new target is the same as the
    // already-registered one (the common case across consecutive
    // packets in a flight) avoids pointless heap churn.
    void arm_expiry() {
      const auto target = conn_.expiry();
      if (target == registered_expiry_) return;
      registered_expiry_ = target;
      if (target == time_point_t::max()) return;
      (void)router_.loop().timeouts().schedule(target,
          [weak = std::weak_ptr<session_t>{session_.self()}](
              time_point_t fired_expire) -> time_point_t {
            auto self = weak.lock();
            if (!self) return {};
            return self->plugin().on_expiry_sweep(fired_expire);
          });
    }

    // RFC 9000 sec. 5.1: CIDs must be unpredictable. Random bytes from
    // OpenSSL satisfy this; on failure (which should not happen in
    // normal operation) we return a zero-length CID, which makes the
    // enclosing conn unusable -- a fail-closed safer than fail-open.
    [[nodiscard]] static key_t make_random_cid(size_t cidlen) noexcept {
      ngtcp2_cid raw{};
      raw.datalen = cidlen;
      if (RAND_bytes(raw.data, static_cast<int>(cidlen)) != 1) raw.datalen = 0;
      return key_t{raw};
    }

#pragma endregion
#pragma region Data members

    router_t& router_;
    session_t& session_;
    key_t original_dcid_;
    key_t scid_;
    quic_conn conn_;
    quic_plugin_t plugin_;
    time_point_t registered_expiry_;

#pragma endregion
  };

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
