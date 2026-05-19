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
#include <span>
#include <utility>

#include "../io_uring/iou_dgram_router.h"
#include "../io_uring/iou_dgram_session.h"
#include "quic_header.h"

// Plugin pair that demuxes UDP datagrams to `iou_dgram_session`s by QUIC
// Destination Connection ID. The router-side plugin parses each datagram's
// header (long or short) by constructing a `quic_version_cid` over the
// packet bytes and returns the DCID as the routing key. The session-side
// plugin is a placeholder pending the `quic_conn` (ngtcp2) wrapper -- it
// registers under the initial DCID it was constructed with and ignores
// recv/sent traffic.

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_dgram_protocol

// Bundle of router and session plugin types for QUIC CID-keyed routing over
// `iou_dgram_router`. Matches the shape of `iouring::iou_dgram_echo_protocol`:
// `router_plugin` decides routing, `session_plugin` owns per-connection
// state.
class quic_dgram_protocol {
public:
  using buffer = iouring::iou_loop::buffer;

  // CID length, in bytes, of the SCIDs we issue locally and therefore the
  // length the short-header decoder must assume on incoming packets.
  static constexpr size_t cid_length = quic_version_cid::default_scid_length;

  class session_plugin;

  class router_plugin {
  public:
    using session_t = iouring::iou_dgram_session<session_plugin>;
    using router_t = iouring::iou_dgram_router<router_plugin>;
    using key_t = quic_cid;

    // Recover the DCID from the packet header.
    [[nodiscard]] key_t extract(const buffer& buf) const noexcept {
      (void)this;
      const quic_version_cid vc{buf.payload_bytes(), cid_length};
      if (!vc) return {};
      return key_t{vc.dcid_bytes()};
    }

    // Called when the router has no session registered under the extracted
    // DCID. Only long-header packets create new sessions; short-header
    // packets to unknown CIDs are dropped (a future revision may emit a
    // stateless reset instead, per RFC 9000).
    bool create_session(const buffer& buf, router_t& router) {
      (void)this;
      const quic_version_cid vc{buf.payload_bytes(), cid_length};
      if (!vc || !vc.is_long_header()) return false;
      (void)session_t::make(router, buf, key_t{vc.dcid_bytes()});
      return true;
    }
  };

  // Stub session plugin. Holds the single DCID it was registered under and
  // tears that registration down on close. The `recv` / `sent` hooks are
  // no-ops pending the `quic_conn` wrapper that owns the ngtcp2 connection.
  class session_plugin {
  public:
    using router_t = iouring::iou_dgram_router<router_plugin>;
    using session_t = iouring::iou_dgram_session<session_plugin>;
    using key_t = quic_cid;

    // `initial_dcid` is the DCID parsed from the Initial packet that
    // triggered session creation, plucked off the wire in
    // `router_plugin::create_session` and forwarded through
    // `iou_dgram_session::make`. Storing it here avoids a second parse from
    // `register_self`.
    //
    // Once ngtcp2 is wired in, this constructor will also accept TLS context
    // and transport parameters, and the plugin will own the resulting
    // `ngtcp2_conn` plus the full set of issued SCIDs.
    session_plugin(router_t& router, session_t& session,
        key_t initial_dcid) noexcept
        : router_{router}, session_{session}, key_{initial_dcid} {}

    bool register_self(const buffer&) {
      return router_.add_session(key_, session_.self());
    }

    // No-op until the ngtcp2 wrapper lands. The buffer is dropped (returned
    // to the loop's pool by `~buffer`).
    bool handle_recv(buffer&&) noexcept {
      (void)this;
      return true;
    }

    bool handle_sent(buffer&&) noexcept {
      (void)this;
      return true;
    }

    bool unregister_self() { return router_.remove_session(key_); }

    // The DCID this session is registered under. Exposed for tests; ngtcp2
    // integration will later return the full set of SCIDs.
    [[nodiscard]] const key_t& key() const noexcept { return key_; }

  private:
    router_t& router_;
    session_t& session_;
    key_t key_;
  };
};

#pragma endregion

}}} // namespace corvid::proto::quic
