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

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../corvid/proto/quic/quic_conn.h"
#include "../corvid/proto/quic/quic_self_signed_cert.h"
#include "../corvid/proto/quic/quic_ssl_ctx.h"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::proto::quic;

namespace {

constexpr std::array<uint8_t, 16> dcid_bytes{0xde, 0xad, 0xbe, 0xef, 0xfe,
    0xed, 0xfa, 0xce, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

constexpr std::array<uint8_t, 16> scid_bytes{0x11, 0x22, 0x33, 0x44, 0x55,
    0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};

constexpr std::string_view alpn = "corvid-test";

[[nodiscard]] quic_conn::time_point_t now_tp() noexcept {
  return std::chrono::steady_clock::now();
}

// A UDP socket bound to loopback on an OS-assigned port. The socket is
// held open by this object so the assigned port stays reserved to us;
// no actual I/O goes through it. The point is to get a unique endpoint
// per test run so parallel test execution can't collide on a port.
struct bound_loopback {
  net_socket sock;
  net_endpoint addr;

  [[nodiscard]] static bound_loopback make_v4() noexcept {
    bound_loopback bl;
    bl.sock = net_socket::create_for(net_endpoint::loopback_v4(),
        execution::nonblocking, message_style::datagram);
    if (!bl.sock.is_open()) return {};
    if (!bl.sock.bind(net_endpoint::loopback_v4())) return {};
    bl.addr = net_endpoint{bl.sock};
    return bl;
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return sock.is_open() && !addr.empty();
  }
};

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("quic_conn constructs as server", "[quic][conn]") {
  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx tls{ck, alpn};
  REQUIRE(tls);

  const auto local = bound_loopback::make_v4();
  const auto peer = bound_loopback::make_v4();
  REQUIRE(local);
  REQUIRE(peer);

  const quic_cid peer_scid{dcid_bytes};
  const quic_cid scid{scid_bytes};
  const quic_cid original_dcid{dcid_bytes};

  quic_conn conn{tls, peer_scid, scid, original_dcid, local.addr, peer.addr,
      now_tp()};
  REQUIRE(conn);
  CHECK(conn.role() == connection_role::server);
  CHECK_FALSE(conn.is_handshake_completed());
  CHECK(conn.native());
}

TEST_CASE("quic_conn constructs as client", "[quic][conn]") {
  quic_ssl_ctx tls{alpn};
  REQUIRE(tls);

  const auto local = bound_loopback::make_v4();
  const auto peer = bound_loopback::make_v4();
  REQUIRE(local);
  REQUIRE(peer);

  const quic_cid dcid{dcid_bytes};
  const quic_cid scid{scid_bytes};

  quic_conn conn{tls, dcid, scid, local.addr, peer.addr, now_tp()};
  REQUIRE(conn);
  CHECK(conn.role() == connection_role::client);
  CHECK_FALSE(conn.is_handshake_completed());
  CHECK(conn.native());
}

TEST_CASE("quic_conn expiry is queryable on a fresh conn", "[quic][conn]") {
  // Fresh server conn: ngtcp2 reports a non-default expiry (idle timeout or
  // handshake timer). We don't pin to an exact value -- it's enough that
  // the call returns without crashing and the wrapper round-trips through
  // its chrono conversion.
  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx tls{ck, alpn};
  REQUIRE(tls);

  const auto local = bound_loopback::make_v4();
  const auto peer = bound_loopback::make_v4();
  REQUIRE(local);
  REQUIRE(peer);

  const quic_cid peer_scid{dcid_bytes};
  const quic_cid scid{scid_bytes};
  const quic_cid original_dcid{dcid_bytes};

  quic_conn conn{tls, peer_scid, scid, original_dcid, local.addr, peer.addr,
      now_tp()};
  REQUIRE(conn);

  const auto deadline = conn.expiry();
  // Any non-default value is fine; the API just needs to round-trip.
  (void)deadline;
}

TEST_CASE("quic_conn handshake completes in-process", "[quic][conn]") {
  // Drive a real TLS 1.3 handshake between a client and server conn, in
  // memory, by manually ferrying each emitted datagram from one side's
  // `write_pkt` into the other side's `read_pkt`. Convergence criterion
  // is `is_handshake_completed() == true` on both sides.

  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx server_tls{ck, alpn};
  quic_ssl_ctx client_tls{alpn};
  REQUIRE(server_tls);
  REQUIRE(client_tls);

  const auto server_loop = bound_loopback::make_v4();
  const auto client_loop = bound_loopback::make_v4();
  REQUIRE(server_loop);
  REQUIRE(client_loop);
  const auto& server_addr = server_loop.addr;
  const auto& client_addr = client_loop.addr;

  // Client picks a random DCID (which the server will echo back in
  // transport params as `original_dcid`) and an SCID for itself.
  const quic_cid client_chosen_dcid{dcid_bytes};
  const quic_cid client_scid{scid_bytes};
  quic_conn client{client_tls, client_chosen_dcid, client_scid, client_addr,
      server_addr, now_tp()};
  REQUIRE(client);

  // Server-side construction:
  //   - dcid sent to ngtcp2 = the client's SCID (where the server will
  //     send its packets);
  //   - scid = the server's own freshly-generated CID;
  //   - original_dcid = the client's chosen DCID (the value that travels
  //     back via the `original_destination_connection_id` transport
  //     parameter).
  constexpr std::array<uint8_t, 16> server_scid_bytes{0xaa, 0xbb, 0xcc, 0xdd,
      0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
  const quic_cid server_scid{server_scid_bytes};
  quic_conn server{server_tls, client_scid, server_scid, client_chosen_dcid,
      server_addr, client_addr, now_tp()};
  REQUIRE(server);

  // Paths recorded from each side's viewpoint: `(local, peer)`.
  auto client_path = quic_conn::make_ngtcp2_path(client_addr, server_addr);
  auto server_path = quic_conn::make_ngtcp2_path(server_addr, client_addr);

  std::array<uint8_t, 1500> buf{};

  // Drive each side's `write_pkt` until it has nothing more to emit,
  // delivering each emitted packet straight to the peer's `read_pkt`.
  // Returns false if either call reports a status other than `ok`.
  auto pump = [&buf](quic_conn& from, ngtcp2_path& from_path, quic_conn& to,
                  const ngtcp2_path& to_path) -> bool {
    for (int safety = 0; safety < 32; ++safety) {
      auto res = from.write_pkt(from_path, buf, now_tp());
      if (!res.ok()) return false;
      if (res.bytes_written == 0) return true;
      const auto rv = to.read_pkt(to_path,
          std::span<const uint8_t>{buf.data(), res.bytes_written}, now_tp());
      if (rv != quic_decode_status::ok) return false;
    }
    return false;
  };

  for (int iter = 0; iter < 16; ++iter) {
    REQUIRE(pump(client, client_path, server, server_path));
    REQUIRE(pump(server, server_path, client, client_path));
    if (client.is_handshake_completed() && server.is_handshake_completed())
      break;
  }

  CHECK(client.is_handshake_completed());
  CHECK(server.is_handshake_completed());
}
// NOLINTEND(readability-function-cognitive-complexity)
