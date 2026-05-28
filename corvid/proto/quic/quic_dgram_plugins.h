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
#include "quic_session_io.h"
#include "quic_ssl_ctx.h"

// `iou_dgram_router_plugin`/`iou_dgram_session_plugin` pair that demuxes UDP
// datagrams to `iou_dgram_session`s by QUIC Destination Connection ID, plus
// the upper-layer plugin shape that HTTP/3 (and potentially other protocols,
// such as DNS and SMB) sits on.
//
// The router-side plugin parses each datagram's header (long or short) by
// constructing a `quic_version_cid` over the packet bytes and returns the DCID
// as the routing key. The session-side plugin owns a `quic_conn` (ngtcp2 state
// machine + per-conn SSL) and drives its read/write/expiry cycle. After each
// processed datagram, the session calls the upper-layer plugin's `drain(now)`
// so the plugin can queue outbound stream bytes before the session emits
// packets.

namespace corvid { inline namespace proto { namespace quic {

#pragma region no_op_plugin

// Upper-layer plugin contract (duck-typed, enforced by the session template):
//   * Constructor accepting `quic_session_io&` (so the plugin captures the
//     session ref and reaches the router / `quic_conn` through it).
//     `session_plugin` inherits `quic_session_io` publicly and binds to the
//     base reference at construction.
//
//   * Inherits `quic_conn_handlers` so the session can install it as the
//     `quic_conn`'s upcall target via `set_handlers`.
//
//   * `bool drain(time_point_t now)` - per-turn hook fired after every
//     successful `quic_conn::read_pkt`, on the loop thread. This is the
//     ONLY outbound path: drain MUST loop `writev_stream` over its
//     per-stream queues and/or `stream_id::none` until ngtcp2 reports
//     nothing more to send, shipping each non-empty packet through
//     `quic_session_io::send_packet`. The bytes from the incoming datagram
//     are *not* passed in: by the time this hook fires, `read_pkt` has
//     already decrypted them and dispatched them through
//     `quic_conn_handlers` upcalls. `quic_no_op_plugin::drain` is the
//     base/fallback that emits non-stream frames (ACKs, MAX_DATA, etc.)
//     via the `stream_id::none` form.
class quic_no_op_plugin: public quic_conn_handlers {
public:
  explicit quic_no_op_plugin(quic_session_io& s) noexcept : io_{s} {}

  // Drive ngtcp2's outbound queue until it stops producing. ngtcp2's pacing
  // dictates one packet per call; we ship each packet on its own borrowed
  // buffer. `stream_id::none` means "emit whatever non-stream frames are
  // queued"; concrete plugins override this with a per-stream drive and may
  // end with a `stream_id::none` flush to pick up any remaining ACKs.
  bool drain(time_point_t now) noexcept {
    for (;;) {
      auto out = io_.borrow_send_buffer();
      if (!out) return true;
      uint64_t accepted = 0;
      const auto status = io_.conn().writev_stream(quic_stream_id::none, {},
          out, accepted, write_stream_flags::none, now);
      if (status != quic_decode_status::ok) return false;
      if (out.payload_bytes().empty()) return true;
      (void)io_.send_packet(std::move(out));
    }
  }

protected:
  quic_session_io& io_;
};

#pragma endregion
#pragma region dgram_protocol

// Bundle of router and session plugin types for QUIC CID-keyed routing over
// `iou_dgram_router`, templatized on the upper-layer plugin type. Matches the
// shape of `iouring::iou_dgram_echo_protocol`: `router_plugin` decides
// routing, `session_plugin` owns per-connection state.
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

    // Called when the router has no session registered under the extracted
    // DCID. Server-role routers turn long-header (Initial) packets into new
    // sessions; short-header packets to unknown CIDs are dropped (a future
    // revision may emit a stateless reset instead, per RFC 9000). Client-role
    // routers always drop: a client never accepts server-driven inbound.
    //
    // The buffer is handed unparsed to `session_t::make`, which calls the
    // session plugin's `register_self(buf)`. That hook does the wire-header
    // parse, generates the server's SCID, allocates the `quic_conn`, and
    // registers the session under both the original DCID and the new SCID. The
    // long-header sniff up front only exists to reject obviously unsuitable
    // packets before spinning up a session that would just discard itself.
    bool create_session(const buffer& buf, router_t& router) {
      if (tls_.role() != connection_role::server) return false;
      const quic_version_cid vc{buf.payload_bytes(), cid_length};
      if (!vc || !vc.is_long_header()) return false;
      auto ssn = session_t::make(router, buf, tls_);
      return static_cast<bool>(ssn->plugin().conn());
    }

#pragma endregion
#pragma region Data members
  private:
    quic_ssl_ctx& tls_;

#pragma endregion
  };

#pragma endregion
#pragma region session_plugin

  // Per-connection state: owns the `quic_conn` (ngtcp2 + per-conn SSL), routes
  // incoming datagrams through `quic_conn::read_pkt`, drives
  // `quic_conn::write_pkt` through the router's UDP socket, and arms one
  // expiry-sweeper entry against `iou_loop::timeouts` to drive loss-detection
  // / PTO / handshake timers.
  //
  // Server-side sessions register under two CIDs:
  //   * the client's original DCID (the routing key for the Initial packet
  //     that created the session, and for any Initial retransmissions that
  //     arrive before the client switches over);
  //
  //   * the server's freshly-generated primary SCID (the routing key for every
  //     subsequent client packet, which uses the server's SCID as its DCID).
  //
  // Client-side sessions register under their own SCID only: inbound packets
  // from the server always address us by the SCID we chose; the initial DCID
  // we put on the wire for our first Initial is never seen on inbound.
  //
  // Additional CIDs issued by ngtcp2 through `get_new_connection_id2` are not
  // yet registered with the router: that is the next-next milestone (CID
  // rotation / migration).
  class session_plugin: public quic_session_io {
  public:
    using router_t = iouring::iou_dgram_router<router_plugin>;
    using session_t = iouring::iou_dgram_session<session_plugin>;
    using key_t = quic_cid;

#pragma region Construction
  public:
    // Server-side construction. Constructs a thin `quic_conn` (owned by the
    // `quic_session_io` base) in the server role bound to `server_tls`; the
    // underlying `ngtcp2_conn` is allocated by `register_self(buf)` once the
    // wire header has been parsed for the CIDs and peer endpoint. Reached
    // via `iou_dgram_session::make(router, buf, tls)` from
    // `router_plugin::create_session`.
    session_plugin(router_t& router, session_t& session,
        quic_ssl_ctx& server_tls) noexcept
        : quic_session_io{session, server_tls}, router_{router},
          session_{session},
          scid_{make_random_cid(quic_dgram_protocol::cid_length)},
          plugin_{*this} {
      conn().set_handlers(&plugin_);
    }

    // Client-side construction. Constructs a thin `quic_conn` in the client
    // role bound to `client_tls`; the underlying `ngtcp2_conn` is allocated by
    // `do_register_client`, which `make_client` calls inline on the loop
    // thread. The peer endpoint is captured here because the buffer passed
    // to `register_self` is empty for client-role sessions. Reached via the
    // `make_client` factory below.
    session_plugin(router_t& router, session_t& session,
        quic_ssl_ctx& client_tls, const net_endpoint& peer) noexcept
        : quic_session_io{session, client_tls}, router_{router},
          session_{session}, peer_{peer},
          scid_{make_random_cid(quic_dgram_protocol::cid_length)},
          plugin_{*this} {
      conn().set_handlers(&plugin_);
    }

    // Static factory for client-side construction.
    //
    // LOOP-THREAD ONLY: returns null if invoked off-loop. Callers on other
    // threads should wrap the call in `router.loop().post(...)` and store the
    // returned `session_ptr` themselves.
    //
    // Constructs the session via `session_t::make(router, buffer{}, ...)`,
    // which honors the plugin contract's "empty buffer => caller defers
    // registration" convention (so `register_self({})` is a no-op here),
    // then runs `do_register_client` inline. Returns null if either
    // ngtcp2 init or router registration fails.
    [[nodiscard]] static std::shared_ptr<session_t>
    make_client(router_t& router, const net_endpoint& peer) {
      if (!router.loop().is_loop_thread()) return {};
      auto ssn =
          session_t::make(router, buffer{}, router.plugin().tls(), peer);
      if (!ssn->plugin().do_register_client()) return {};
      return ssn;
    }

#pragma endregion
#pragma region Registration

    // Recover the server-side CIDs from the inbound Initial, allocate the
    // `ngtcp2_conn` via `quic_conn::init`, and register this session under
    // both the original DCID and the server's SCID.
    //
    // The empty-buffer sentinel (`!buf`) signals the
    // `iou_dgram_session_plugin` "construct without auto-register" convention;
    // the client path uses it (see `make_client`), and we bail without
    // touching the router. Server-side calls always carry a real Initial
    // buffer.
    //
    // Server-side calls come from `router_plugin::create_session`, which runs
    // in the router's recv callback on the loop thread; the work runs inline.
    bool register_self(const buffer& buf) {
      if (!buf) return true;
      assert(router_.loop().is_loop_thread());
      assert(conn().role() == connection_role::server);
      const quic_version_cid vc{buf.payload_bytes(),
          quic_dgram_protocol::cid_length};
      if (!vc || !vc.is_long_header()) return false;
      original_dcid_ = key_t{vc.dcid_bytes()};
      peer_ = buf.peer_addr();
      return do_register_server(key_t{vc.scid_bytes()});
    }

    bool unregister_self() {
      const bool ok1 = router_.remove_session(scid_);
      bool ok2 = true;
      if (conn().role() == connection_role::server)
        ok2 = router_.remove_session(original_dcid_);
      return ok1 && ok2;
    }

#pragma endregion
#pragma region I/O

    // Feed the datagram into ngtcp2, let the upper plugin drive outbound, then
    // re-arm expiry. `now` is snapped once at the top of the turn and threaded
    // through every callee so every operation sees the same wall-clock view.
    // `plugin_.drain` is the only outbound path: stream data and other
    // higher-layer events already reached the plugin during `read_pkt` through
    // the `quic_conn_handlers` upcalls; the plugin then loops `writev_stream`
    // until ngtcp2 reports nothing more to send.
    //
    // Returning `false` closes the session.
    bool handle_recv(buffer&& buf) {
      assert(router_.loop().is_loop_thread());
      const auto now = timeouts::now();
      const auto rv = conn().read_pkt(buf.payload_bytes(), now);
      if (rv != quic_decode_status::ok) return is_soft_error(rv);
      (void)plugin_.drain(now);
      arm_expiry();
      return true;
    }

    // The send buffer comes back here once the kernel has accepted the
    // datagram. There is nothing to do at the QUIC layer: ngtcp2's own ACK /
    // loss-detection machinery (driven via the sweeper-armed expiry timer)
    // handles retransmits, and the buffer returns to the pool when this frame
    // unwinds.
    //
    // HTTP/3 cares about stream-level send acceptance, not UDP-level send
    // acceptance, and ngtcp2 tracks per-stream offsets and ACKs internally.
    // When nghttp3 sits on top, its data callbacks will be driven by ngtcp2's
    // own ACK processing, not by this hook.
    //
    // Returning `false` closes the session.
    bool handle_sent(buffer&& buf) {
      buf.reset();
      return true;
    }

#pragma endregion
#pragma region Accessors

    [[nodiscard]] router_t& router() noexcept { return router_; }
    [[nodiscard]] session_t& session() noexcept { return session_; }
    [[nodiscard]] quic_plugin_t& protocol_plugin() noexcept { return plugin_; }

    // The server's freshly-generated SCID, used as the primary CID for routing
    // packets after the client switches off the Initial DCID. Exposed for
    // tests; the full set of keys also includes `original_dcid_` until the
    // client migrates.
    [[nodiscard]] const key_t& primary_cid() const noexcept { return scid_; }

#pragma endregion
#pragma region Expiry

    // Sweeper-callback entry point. Invoked by `iou_loop::timeouts` when the
    // registered deadline elapses. Stale entries (left over from a deadline
    // that was superseded by an earlier rearm) detect themselves via
    // `fired_expire != registered_expiry_` and drop.
    [[nodiscard]] time_point_t on_expiry_sweep(
        time_point_t fired_expire) noexcept {
      if (fired_expire != registered_expiry_) return {};
      const auto now = timeouts::now();
      const auto target = conn().expiry();
      if (target > now) {
        registered_expiry_ = target;
        return target;
      }
      (void)conn().handle_expiry(now);
      (void)plugin_.drain(now);
      const auto next = conn().expiry();
      registered_expiry_ = next;
      return next;
    }

#pragma endregion
#pragma region Helpers
  private:
    // Loop-thread half of server-side registration: allocate the `ngtcp2_conn`
    // against the parsed peer SCID, register under both the original DCID and
    // the server's own SCID, drive any initial ngtcp2 output through the
    // plugin, and arm the handshake-expiry timer.
    bool do_register_server(const key_t& peer_scid) {
      assert(router_.loop().is_loop_thread());
      const auto now = timeouts::now();
      if (!conn().init(peer_scid, scid_, router_.local_endpoint(), peer_,
              original_dcid_, now))
        return false;
      const bool ok1 = router_.add_session(original_dcid_, session_.self());
      const bool ok2 = router_.add_session(scid_, session_.self());
      (void)plugin_.drain(now);
      arm_expiry();
      return ok1 && ok2;
    }

    // Loop-thread half of client-side registration: pick a random Initial DCID
    // (which we put on the wire but never receive back), allocate the
    // `ngtcp2_conn`, register under our SCID, push the Initial through the
    // plugin's drain, and arm the handshake-expiry timer.
    bool do_register_client() {
      assert(router_.loop().is_loop_thread());
      const key_t initial_dcid =
          make_random_cid(quic_dgram_protocol::cid_length);
      const auto now = timeouts::now();
      if (!conn().init(initial_dcid, scid_, router_.local_endpoint(), peer_,
              key_t{}, now))
        return false;
      const bool ok = router_.add_session(scid_, session_.self());
      (void)plugin_.drain(now);
      arm_expiry();
      return ok;
    }

    // Schedule (or reschedule) the expiry-sweeper entry. The sweeper has no
    // cancel API, so an existing entry that was scheduled at a later deadline
    // becomes stale and self-cancels on its next fire (via the `fired_expire
    // != registered_expiry_` check above). Skipping the schedule when the new
    // target is the same as the already-registered one (the common case across
    // consecutive packets in a flight) avoids pointless heap churn.
    void arm_expiry() {
      const auto target = conn().expiry();
      if (target == registered_expiry_) return;
      registered_expiry_ = target;
      if (target == time_point_t::max()) return;
      (void)router_.loop().timeouts().schedule(target,
          [weak = std::weak_ptr<session_t>{session_.self()}](
              time_point_t fired_expire) -> time_point_t {
            auto self = weak.lock();
            if (!self) return {};
            return try_or_log(
                [&] { return self->plugin().on_expiry_sweep(fired_expire); },
                time_point_t{});
          });
    }

    // RFC 9000 sec. 5.1: CIDs must be unpredictable. Random bytes from OpenSSL
    // satisfy this; on failure (which should not happen in normal operation)
    // we return a zero-length CID, which makes the enclosing conn unusable --
    // a fail-closed safer than fail-open.
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
    net_endpoint peer_;
    key_t original_dcid_;
    key_t scid_;
    quic_plugin_t plugin_;
    time_point_t registered_expiry_;

#pragma endregion
  };

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
