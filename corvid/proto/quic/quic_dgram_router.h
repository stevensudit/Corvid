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
#include <concepts>
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
// the upper-layer `quic_plugin` shape that HTTP/3 (and potentially other
// protocols, such as DNS and SMB) sit on.
//
// The router-side plugin parses each datagram's header (long or short) by
// constructing a `quic_version_cid` over the packet bytes and returns the
// DCID as the routing key. The session-side plugin owns a `quic_conn`
// (ngtcp2 state machine + per-conn SSL) and drives its read/write/expiry
// cycle. After each processed datagram, the session forwards to the
// upper-layer `quic_plugin`'s `on_packet_receive`.

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_plugin

// TODO: This is DOA; already obsolete.
// Concept for the upper-layer plugin owned by `quic_dgram_protocol::
// session_plugin`. The plugin is constructed in-place with `(session&)`
// so it captures the session ref and reaches the router via `session.router()`
// when it needs to send.
//
// Required ctor (verified at session construction):
//   `quic_plugin(quic_dgram_protocol::session_plugin&)` - primary path.
//
// Required methods:
//   `bool on_packet_receive()` - tick notification fired after every
//       successful `quic_conn::read_pkt`, on the loop thread, before
//       the session drains writes. The bytes from the incoming datagram
//       are *not* passed in: by the time this hook fires, `read_pkt`
//       has already decrypted them and dispatched them into ngtcp2.
//       Stream data and other higher-layer events flow back to the
//       upper layer through the ngtcp2 callback table on `quic_conn`
//       (the side channel through which HTTP/3 will eventually receive
//       its stream callbacks once nghttp3 is wired in). This hook is
//       the chance to queue new outbound stream data on `quic_conn` so
//       the session's drain loop picks it up on the way out.
template<typename P>
concept quic_plugin = requires(P p) {
  { p.on_packet_receive() } -> std::same_as<bool>;
};

// Default no-op upper-layer plugin. Inherits `quic_conn_handlers` so the
// session can install it as the `quic_conn`'s upcall target; every
// virtual defaults to `true` so packets flow without reaction.
class quic_no_op_plugin: public quic_conn_handlers {
public:
  template<typename Session>
  explicit quic_no_op_plugin(Session&) noexcept {}

  bool on_packet_receive() noexcept {
    (void)this;
    return true;
  }
};

#pragma endregion
#pragma region quic_dgram_protocol

// Bundle of router and session plugin types for QUIC CID-keyed routing
// over `iou_dgram_router`, templatized on the upper-layer plugin type.
// Matches the shape of `iouring::iou_dgram_echo_protocol`:
// `router_plugin` decides routing, `session_plugin` owns per-connection
// state.
template<typename QuicPlugin = quic_no_op_plugin>
class quic_dgram_protocol {
public:
  using buffer = iouring::iou_loop::buffer;
  using time_point_t = timeouts::time_point_t;
  using quic_plugin_t = QuicPlugin;

  // CID length, in bytes, of the SCIDs we issue locally and therefore the
  // length the short-header decoder must assume on incoming packets.
  static constexpr size_t cid_length = quic_version_cid::default_scid_length;

  class session_plugin;

  class router_plugin {
  public:
    using session_t = iouring::iou_dgram_session<session_plugin>;
    using router_t = iouring::iou_dgram_router<router_plugin>;
    using key_t = quic_cid;

    // The TLS context whose role determines this router's mode and whose
    // cert/key (server role) or verify settings (client role) flow into
    // new sessions. Held by reference; must outlive the router.
    //
    // A server-role context lets `create_session` turn unsolicited
    // Initials into server-side sessions. A client-role context disables
    // `create_session` (unsolicited inbound is dropped); client sessions
    // are instead constructed explicitly via `session_plugin::make_client`
    // and registered under their own SCID.
    explicit router_plugin(quic_ssl_ctx& tls) noexcept : tls_{tls} {}

    [[nodiscard]] quic_ssl_ctx& tls() const noexcept { return tls_; }

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
    // RFC 9000). Client-role routers always drop -- a client never
    // accepts server-driven inbound.
    bool create_session(const buffer& buf, router_t& router) {
      if (tls_.role() != connection_role::server) return false;
      const quic_version_cid vc{buf.payload_bytes(), cid_length};
      if (!vc || !vc.is_long_header()) return false;
      const key_t peer_scid{vc.scid_bytes()};
      const key_t original_dcid{vc.dcid_bytes()};
      (void)session_t::make(router, buf, tls_, peer_scid, original_dcid,
          router.local_endpoint(), buf.peer_addr());
      return true;
    }

  private:
    quic_ssl_ctx& tls_;
  };

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
  // Client-side sessions register under their own SCID only -- inbound
  // packets from the server always address us by the SCID we chose; the
  // initial DCID we put on the wire for our first Initial is never seen
  // on inbound.
  //
  // Additional CIDs issued by ngtcp2 through `get_new_connection_id2`
  // are not yet registered with the router -- that is the next-next
  // milestone (CID rotation / migration).
  class session_plugin {
  public:
    using router_t = iouring::iou_dgram_router<router_plugin>;
    using session_t = iouring::iou_dgram_session<session_plugin>;
    using key_t = quic_cid;

    // Server-side construction. Builds a `quic_conn` in the server role
    // bound to `server_tls`. The router uses this path via
    // `router_plugin::create_session` when an Initial arrives for an
    // unknown DCID. The conn is left in its null state on construction
    // failure; subsequent I/O calls return invalid_state.
    session_plugin(router_t& router, session_t& session,
        quic_ssl_ctx& server_tls, const key_t& peer_scid,
        const key_t& original_dcid, const net_endpoint& local,
        const net_endpoint& peer) noexcept
        : router_{router}, session_{session}, local_{local}, peer_{peer},
          original_dcid_{original_dcid},
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
        : router_{router}, session_{session}, local_{local}, peer_{peer},
          scid_{make_random_cid(quic_dgram_protocol::cid_length)},
          conn_{client_tls, make_random_cid(quic_dgram_protocol::cid_length),
              scid_, local, peer, timeouts::now()},
          plugin_{*this} {
      conn_.set_handlers(&plugin_);
    }

    // Static factory for client-side construction. Builds an
    // unregistered session, then posts `register_self` to the loop
    // thread (which registers under the SCID, drains the Initial, and
    // arms the handshake-expiry timer). Returns the session ptr
    // immediately; the handshake progresses asynchronously through the
    // router. Safe to call from any thread. Returns null if `quic_conn`
    // construction failed (e.g., `client_tls` is server-role).
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

    bool register_self(const buffer&) {
      if (!conn_) return false;
      bool ok1 = true;
      if (conn_.role() == connection_role::server)
        ok1 = router_.add_session(original_dcid_, session_.self());
      const bool ok2 = router_.add_session(scid_, session_.self());
      const auto now = timeouts::now();
      drain_writes(now);
      arm_expiry();
      return ok1 && ok2;
    }

    // Feed the datagram into ngtcp2, let the upper plugin react, then
    // drain anything ngtcp2 wants to send. `now` is snapped once at the
    // top of the turn and threaded through every callee so every
    // operation sees the same wall-clock view. Loop-thread only,
    // asserted.
    bool handle_recv(buffer&& buf) {
      assert(router_.loop().is_loop_thread());
      if (!conn_) return false;
      const auto now = timeouts::now();
      auto path = quic_conn::make_ngtcp2_path(local_, peer_);
      const auto rv = conn_.read_pkt(path, buf.payload_bytes(), now);
      if (rv != quic_decode_status::ok) return false;
      // `on_packet_receive` is purely a tick notification: the bytes from
      // `buf` have already been decrypted and dispatched into ngtcp2 by
      // `read_pkt`. Stream data and other higher-layer events flow back
      // to the plugin through the ngtcp2 callback table on `quic_conn`,
      // not through this hook.
      (void)plugin_.on_packet_receive();
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

    bool unregister_self() {
      const bool ok1 = router_.remove_session(scid_);
      bool ok2 = true;
      if (conn_.role() == connection_role::server)
        ok2 = router_.remove_session(original_dcid_);
      return ok1 && ok2;
    }

    [[nodiscard]] router_t& router() noexcept { return router_; }
    [[nodiscard]] session_t& session() noexcept { return session_; }
    [[nodiscard]] quic_conn& conn() noexcept { return conn_; }
    [[nodiscard]] quic_plugin_t& protocol_plugin() noexcept { return plugin_; }

    // The server's freshly-generated SCID, used as the primary CID for
    // routing packets after the client switches off the Initial DCID.
    // Exposed for tests; the full set of keys also includes
    // `original_dcid_` until the client migrates.
    [[nodiscard]] const key_t& primary_cid() const noexcept { return scid_; }

    // Sweeper-callback entry point. Invoked by `iou_loop::timeouts()`
    // when the registered deadline elapses. Stale entries (left over
    // from a deadline that was superseded by an earlier rearm) detect
    // themselves via `fired_expire != registered_expiry_` and drop.
    [[nodiscard]] time_point_t on_expiry_sweep(
        time_point_t fired_expire) noexcept {
      if (!conn_) return {};
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

  private:
    // Drain ngtcp2's outbound queue. ngtcp2's pacing dictates one packet
    // per `write_pkt` call; we loop until it reports nothing more to
    // send (`bytes_written == 0`) or an error. Each packet rides its
    // own borrowed buffer through the router socket. `now` is supplied
    // by the caller so every `write_pkt` in a single turn sees the same
    // clock reading.
    void drain_writes(time_point_t now) {
      while (conn_) {
        auto out = session_.borrow_send_buffer();
        if (!out) return;
        auto tail = out.tail_span();
        if (tail.empty()) return;
        std::span<uint8_t> dest{reinterpret_cast<uint8_t*>(tail.data()),
            tail.size()};
        auto path_out = quic_conn::make_ngtcp2_path(local_, peer_);
        const auto res = conn_.write_pkt(path_out, dest, now);
        if (!res.ok()) return;
        if (res.bytes_written == 0) return;
        if (!out.update_payload({tail.data(), res.bytes_written})) return;
        out.peer_addr() = peer_;
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
      if (!conn_) return;
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

    router_t& router_;
    session_t& session_;
    net_endpoint local_;
    net_endpoint peer_;
    key_t original_dcid_;
    key_t scid_;
    quic_conn conn_;
    quic_plugin_t plugin_;
    time_point_t registered_expiry_;
  };
};

#pragma endregion

}}} // namespace corvid::proto::quic
