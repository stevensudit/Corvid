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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sys/socket.h>
#include <thread>
#include <vector>

#include "../corvid/proto/io_uring/iou_loop.h"
#include "../corvid/proto/quic/quic_conn.h"
#include "../corvid/proto/quic/quic_dgram_router.h"
#include "../corvid/proto/quic/quic_self_signed_cert.h"
#include "../corvid/proto/quic/quic_ssl_ctx.h"

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::iouring;
using namespace corvid::proto::quic;
using namespace std::chrono_literals;

namespace {

bool WaitFor(const auto& pred, std::chrono::milliseconds timeout = 500ms) {
#ifdef DEBUG
  timeout = 1h;
#endif
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return pred();
}

// Build a minimal QUIC v1 long-header packet header with the given DCID and
// SCID. Returns the byte sequence; everything past the SCID is unused by
// `ngtcp2_pkt_decode_version_cid`, so no payload follows.
std::vector<uint8_t> make_long_header_packet(std::span<const uint8_t> dcid,
    std::span<const uint8_t> scid, uint32_t version = 1) {
  std::vector<uint8_t> bytes;
  bytes.reserve(7 + dcid.size() + scid.size());
  bytes.push_back(0xc0); // long header, fixed bit, Initial type
  bytes.push_back(static_cast<uint8_t>((version >> 24) & 0xff));
  bytes.push_back(static_cast<uint8_t>((version >> 16) & 0xff));
  bytes.push_back(static_cast<uint8_t>((version >> 8) & 0xff));
  bytes.push_back(static_cast<uint8_t>(version & 0xff));
  bytes.push_back(static_cast<uint8_t>(dcid.size()));
  bytes.insert(bytes.end(), dcid.begin(), dcid.end());
  bytes.push_back(static_cast<uint8_t>(scid.size()));
  bytes.insert(bytes.end(), scid.begin(), scid.end());
  return bytes;
}

// Build a short-header packet with the given DCID. The DCID length is not on
// the wire; the decoder must be told via `short_dcidlen` (here, the protocol
// constant `quic_dgram_protocol<>::cid_length`).
std::vector<uint8_t> make_short_header_packet(std::span<const uint8_t> dcid) {
  std::vector<uint8_t> bytes;
  bytes.reserve(1 + dcid.size() + 1);
  bytes.push_back(0x40); // short header (bit 7 clear), fixed bit set
  bytes.insert(bytes.end(), dcid.begin(), dcid.end());
  bytes.push_back(0x00); // a stray packet-number byte to look realistic
  return bytes;
}

// Wrap raw bytes in a synthetic, non-owning `iou_loop::buffer` so the plugin
// can read them through `payload_view`.
iou_loop::buffer wrap(std::span<uint8_t> bytes) {
  return iou_loop::buffer::make_synthetic(
      {reinterpret_cast<std::byte*>(bytes.data()), bytes.size()});
}

constexpr std::array<uint8_t, 16> sample_dcid{0xde, 0xad, 0xbe, 0xef, 0xfe,
    0xed, 0xfa, 0xce, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

constexpr std::array<uint8_t, 16> sample_scid{0x11, 0x22, 0x33, 0x44, 0x55,
    0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("quic_dgram_protocol::router_plugin::extract", "[quic][router]") {
  // `router_plugin` requires a server SSL context for `create_session`, but
  // we only exercise `extract` here, which doesn't consult it. A client-side
  // context is enough to satisfy construction.
  quic_ssl_ctx tls{"corvid-test"};
  REQUIRE(tls);
  quic_dgram_protocol<>::router_plugin plugin{tls};

  SECTION("long-header packet yields the on-wire DCID") {
    auto pkt = make_long_header_packet(sample_dcid, sample_scid);
    auto buf = wrap(pkt);
    const quic_cid key = plugin.extract(buf);
    REQUIRE(key.length() == sample_dcid.size());
    CHECK(std::ranges::equal(key.bytes(), sample_dcid));
  }

  SECTION("long-header DCID may be shorter than the local cid_length") {
    // Long-header packets carry the DCID length on the wire, so an 8-byte
    // DCID is decoded as 8 bytes regardless of our `cid_length` constant.
    std::array<uint8_t, 8> short_dcid{0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
        0xa7};
    auto pkt = make_long_header_packet(short_dcid, sample_scid);
    auto buf = wrap(pkt);
    const quic_cid key = plugin.extract(buf);
    REQUIRE(key.length() == short_dcid.size());
    CHECK(std::ranges::equal(key.bytes(), short_dcid));
  }

  SECTION("short-header packet yields cid_length bytes after the type byte") {
    auto pkt = make_short_header_packet(sample_dcid);
    auto buf = wrap(pkt);
    const quic_cid key = plugin.extract(buf);
    REQUIRE(key.length() == quic_dgram_protocol<>::cid_length);
    CHECK(std::ranges::equal(key.bytes(), sample_dcid));
  }

  SECTION("truncated packet yields an empty CID") {
    std::array<uint8_t, 1> stub{0xc0}; // long header, no version, no len
    auto buf = wrap(stub);
    const quic_cid key = plugin.extract(buf);
    CHECK(key.empty());
  }

  SECTION("short-header packet shorter than cid_length yields empty CID") {
    std::array<uint8_t, 8> stub{0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00};
    auto buf = wrap(stub);
    const quic_cid key = plugin.extract(buf);
    CHECK(key.empty());
  }
}

namespace {

constexpr std::string_view handshake_alpn = "corvid-test";

constexpr std::array<uint8_t, 16> client_dcid_bytes{0xde, 0xad, 0xbe, 0xef,
    0xfe, 0xed, 0xfa, 0xce, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

constexpr std::array<uint8_t, 16> client_scid_bytes{0x11, 0x22, 0x33, 0x44,
    0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};

[[nodiscard]] quic_conn::time_point_t now_tp() noexcept {
  return std::chrono::steady_clock::now();
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE(
    "quic_dgram_protocol drives a server-side TLS 1.3 handshake "
    "through the live iou_dgram_router",
    "[quic][router][handshake]") {
  // The server runs on an `iou_loop_runner`, accepting datagrams via the
  // QUIC plugin pair. The client side is driven by hand, ferrying packets
  // through a raw UDP socket so the test can step the handshake at its
  // own pace.

  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx server_tls{ck, handshake_alpn};
  REQUIRE(server_tls);
  quic_ssl_ctx client_tls{handshake_alpn};
  REQUIRE(client_tls);

  iou_loop_runner runner;

  // Bind the server-side router. Each incoming Initial creates a new
  // `quic_dgram_protocol::session_plugin` carrying its own `quic_conn`.
  auto server_router =
      iou_dgram_router_handle<quic_dgram_protocol<>::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          server_tls);
  CHECK(server_router);
  const auto server_addr = server_router->local_endpoint();
  REQUIRE_FALSE(server_addr.empty());

  // Raw blocking UDP socket for the client. SO_RCVTIMEO bounds each
  // recvfrom so the loop drains naturally when the server has nothing
  // more to send for now.
  auto client_sock = net_socket::create_for(net_endpoint::loopback_v4(),
      execution::blocking, message_style::datagram);
  REQUIRE(client_sock);
  REQUIRE(client_sock.bind(net_endpoint::loopback_v4()));
  const timeval rcv_timeout{.tv_sec = 0, .tv_usec = 50000};
  REQUIRE(client_sock.set_option(socket_option::rcvtimeo, rcv_timeout));
  const net_endpoint client_addr{client_sock};

  const quic_cid client_dcid{client_dcid_bytes};
  const quic_cid client_scid{client_scid_bytes};

  quic_conn client{client_tls, client_dcid, client_scid, client_addr,
      server_addr, now_tp()};
  REQUIRE(client);

  // Trampolines deref `handlers_` unconditionally; the test only drives
  // the conn by hand, so attach the base no-op handlers.
  quic_conn_handlers noop_handlers;
  client.set_handlers(&noop_handlers);

  auto client_path = quic_conn::make_ngtcp2_path(client_addr, server_addr);

  std::array<uint8_t, 1500> pkt_buf{};
  const auto server_sockaddr = server_addr.as_sockaddr();

  // Each iteration: drain the client's outbound, ship every emitted
  // packet to the server, then drain whatever the server has bounced
  // back. Bail as soon as the client reports `is_handshake_completed`,
  // which can only happen after the server's TLS finished message has
  // been processed -- so a single client-side check is a sufficient
  // bidirectional witness for the test.
  bool finished = false;
  for (int iter = 0; iter < 32 && !finished; ++iter) {
    for (int safety = 0; safety < 32; ++safety) {
      const auto res = client.write_pkt(client_path, pkt_buf, now_tp());
      if (!res.ok() || res.bytes_written == 0) break;
      const auto sent = ::sendto(client_sock.handle(), pkt_buf.data(),
          res.bytes_written, 0, server_sockaddr.first, server_sockaddr.second);
      REQUIRE(sent == static_cast<ssize_t>(res.bytes_written));
    }

    for (int safety = 0; safety < 32; ++safety) {
      const auto got = ::recvfrom(client_sock.handle(), pkt_buf.data(),
          pkt_buf.size(), 0, nullptr, nullptr);
      if (got <= 0) break;
      const auto rv = client.read_pkt(client_path,
          std::span<const uint8_t>{pkt_buf.data(), static_cast<size_t>(got)},
          now_tp());
      REQUIRE(rv == quic_decode_status::ok);
    }

    finished = client.is_handshake_completed();
  }

  CHECK(finished);
}

TEST_CASE(
    "quic_dgram_protocol drives a TLS 1.3 handshake through the live "
    "iou_dgram_router on both sides",
    "[quic][router][handshake][client]") {
  // Both client and server run on the same `iou_loop_runner`. The client
  // session is constructed via `session_plugin::make_client`, which posts
  // `register_self` to the loop thread; that pushes the Initial out and
  // arms the handshake-expiry timer. The router then ferries every
  // subsequent packet automatically.

  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx server_tls{ck, handshake_alpn};
  REQUIRE(server_tls);
  quic_ssl_ctx client_tls{handshake_alpn};
  REQUIRE(client_tls);

  iou_loop_runner runner;

  auto server_router =
      iou_dgram_router_handle<quic_dgram_protocol<>::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          server_tls);
  CHECK(server_router);
  const auto server_addr = server_router->local_endpoint();
  REQUIRE_FALSE(server_addr.empty());

  auto client_router =
      iou_dgram_router_handle<quic_dgram_protocol<>::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          client_tls);
  CHECK(client_router);
  REQUIRE_FALSE(client_router->local_endpoint().empty());

  auto client_sess = quic_dgram_protocol<>::session_plugin::make_client(
      *client_router.pointer(), server_addr);
  REQUIRE(client_sess);

  // Client-side handshake completion is a sufficient bidirectional
  // witness: it only flips true after the client consumes the server's
  // TLS Finished, which only ships once the server has processed the
  // client's handshake bytes routed through `server_router`.
  CHECK(WaitFor([&] {
    return client_sess->plugin().conn().is_handshake_completed();
  }));
}
// NOLINTEND(readability-function-cognitive-complexity)
