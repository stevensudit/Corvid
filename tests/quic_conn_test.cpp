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
#include <string>
#include <vector>

#include "../corvid/proto/quic/quic_conn.h"
#include "../corvid/proto/quic/quic_self_signed_cert.h"
#include "../corvid/proto/quic/quic_ssl_ctx.h"
#include "../corvid/strings/enum_conversion.h"
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

// `quic_conn_handlers` subclass that appends a string for every upcall it
// receives. Used by the handshake / dispatch tests to verify the
// trampolines wire through to the right virtual methods. Override only the
// upcalls the tests actually inspect; the base-class no-op defaults cover
// the rest.
struct trace_handlers: quic_conn_handlers {
  std::vector<std::string> events;

  bool on_handshake_completed() noexcept override {
    events.emplace_back("handshake_completed");
    return true;
  }
  bool on_app_tx_ready() noexcept override {
    events.emplace_back("app_tx_ready");
    return true;
  }
  bool on_handshake_confirmed() noexcept override {
    events.emplace_back("handshake_confirmed");
    return true;
  }

  [[nodiscard]] bool saw(std::string_view name) const noexcept {
    for (const auto& e : events)
      if (e == name) return true;
    return false;
  }
};

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

  quic_conn conn{tls};
  REQUIRE(conn.init(peer_scid, scid, local.addr, peer.addr, original_dcid,
      now_tp()));
  REQUIRE(conn);
  CHECK(conn.role() == connection_role::server);
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

  quic_conn conn{tls};
  REQUIRE(conn.init(dcid, scid, local.addr, peer.addr, quic_cid{}, now_tp()));
  REQUIRE(conn);
  CHECK(conn.role() == connection_role::client);
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

  quic_conn conn{tls};
  REQUIRE(conn.init(peer_scid, scid, local.addr, peer.addr, original_dcid,
      now_tp()));
  REQUIRE(conn);

  const auto deadline = conn.expiry();
  // Any non-default value is fine; the API just needs to round-trip.
  (void)deadline;
}

TEST_CASE("quic_conn handshake completes in-process", "[quic][conn]") {
  // Drive a real TLS 1.3 handshake between a client and server conn, in
  // memory, by manually ferrying each emitted datagram from one side's
  // `write_pkt` into the other side's `read_pkt`. Convergence criterion
  // is that the `on_handshake_completed` handler upcall has fired on
  // both sides; we observe it via `trace_handlers`.

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
  quic_conn client{client_tls};
  REQUIRE(client.init(client_chosen_dcid, client_scid, client_addr,
      server_addr, quic_cid{}, now_tp()));
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
  quic_conn server{server_tls};
  REQUIRE(server.init(client_scid, server_scid, server_addr, client_addr,
      client_chosen_dcid, now_tp()));
  REQUIRE(server);

  // Trampolines deref `handlers_` unconditionally; tests that drive I/O
  // must attach handlers before the first read/write. `trace_handlers`
  // records the upcalls we use as the convergence witness.
  trace_handlers client_trace;
  trace_handlers server_trace;
  client.set_handlers(&client_trace);
  server.set_handlers(&server_trace);

  std::array<std::byte, 1500> backing{};

  // Drive each side's `write_pkt` until it has nothing more to emit,
  // delivering each emitted packet straight to the peer's `read_pkt`. Returns
  // false if either call reports a status other than `ok`. The synthetic
  // buffer is re-created each iteration so its payload starts empty and the
  // post-call `payload_bytes` is exactly the produced packet.
  auto pump = [&backing](quic_conn& from, quic_conn& to) -> bool {
    for (int safety = 0; safety < 32; ++safety) {
      auto buf = iouring::iou_buffer::make_synthetic_write(
          {backing.data(), backing.size()});
      const auto status = from.write_pkt(buf, now_tp());
      if (status != quic_decode_status::ok) return false;
      const auto payload = buf.payload_bytes();
      if (payload.empty()) return true;
      const auto rv = to.read_pkt(payload, now_tp());
      if (rv != quic_decode_status::ok) return false;
    }
    return false;
  };

  for (int iter = 0; iter < 16; ++iter) {
    REQUIRE(pump(client, server));
    REQUIRE(pump(server, client));
    if (client_trace.saw("handshake_completed") &&
        server_trace.saw("handshake_completed"))
      break;
  }

  CHECK(client_trace.saw("handshake_completed"));
  CHECK(server_trace.saw("handshake_completed"));
}
TEST_CASE("quic_conn handler upcalls fire during handshake", "[quic][conn]") {
  // Same setup as the handshake test, but with a `trace_handlers` attached to
  // each side. After the handshake completes we expect at least the
  // handshake-progression upcalls (`on_handshake_completed`,
  // `on_app_tx_ready`, `on_handshake_confirmed`) to have fired on both
  // ends. We do not pin to an exact order: ordering between the events
  // differs between client and server per RFC 9001 sec. 4.1, and the
  // `is_handshake_completed` loop already covers the full handshake; this
  // test's job is purely to prove the trampolines reach the handlers.
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

  const quic_cid client_chosen_dcid{dcid_bytes};
  const quic_cid client_scid{scid_bytes};
  quic_conn client{client_tls};
  REQUIRE(client.init(client_chosen_dcid, client_scid, client_addr,
      server_addr, quic_cid{}, now_tp()));
  REQUIRE(client);

  constexpr std::array<uint8_t, 16> server_scid_bytes{0xaa, 0xbb, 0xcc, 0xdd,
      0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
  const quic_cid server_scid{server_scid_bytes};
  quic_conn server{server_tls};
  REQUIRE(server.init(client_scid, server_scid, server_addr, client_addr,
      client_chosen_dcid, now_tp()));
  REQUIRE(server);

  trace_handlers client_trace;
  trace_handlers server_trace;
  client.set_handlers(&client_trace);
  server.set_handlers(&server_trace);

  std::array<std::byte, 1500> backing{};
  auto pump = [&backing](quic_conn& from, quic_conn& to) -> bool {
    for (int safety = 0; safety < 32; ++safety) {
      auto buf = iouring::iou_buffer::make_synthetic_write(
          {backing.data(), backing.size()});
      const auto status = from.write_pkt(buf, now_tp());
      if (status != quic_decode_status::ok) return false;
      const auto payload = buf.payload_bytes();
      if (payload.empty()) return true;
      const auto rv = to.read_pkt(payload, now_tp());
      if (rv != quic_decode_status::ok) return false;
    }
    return false;
  };

  // Pump until both sides have reported `on_handshake_completed`, then
  // drive one extra exchange round so HANDSHAKE_DONE has time to land
  // on the client (which is what triggers the client-side
  // `on_handshake_confirmed` per RFC 9001 sec. 4.1.2). ngtcp2 does not
  // emit the confirmed callback on the server -- see the
  // `on_handshake_confirmed` doc on `quic_conn_handlers` -- so we only
  // assert that one on the client.
  int extra_rounds = 0;
  for (int iter = 0; iter < 16; ++iter) {
    REQUIRE(pump(client, server));
    REQUIRE(pump(server, client));
    if (client_trace.saw("handshake_completed") &&
        server_trace.saw("handshake_completed"))
    {
      if (extra_rounds >= 1) break;
      ++extra_rounds;
    }
  }
  REQUIRE(client_trace.saw("handshake_completed"));
  REQUIRE(server_trace.saw("handshake_completed"));

  CHECK(client_trace.saw("handshake_completed"));
  CHECK(client_trace.saw("app_tx_ready"));
  CHECK(client_trace.saw("handshake_confirmed"));
  CHECK(server_trace.saw("handshake_completed"));
  CHECK(server_trace.saw("app_tx_ready"));
  CHECK_FALSE(server_trace.saw("handshake_confirmed"));
}

TEST_CASE("quic_conn handler returning false aborts read_pkt",
    "[quic][conn]") {
  // A handler that returns `false` from an upcall must surface as
  // `NGTCP2_ERR_CALLBACK_FAILURE` from `read_pkt`. We trip the first
  // server-side upcall (`on_app_tx_ready`, which fires once the 1-RTT TX
  // key installs) by overriding it to return false; pumping should then
  // fail before the handshake completes.
  struct abort_on_tx_ready: trace_handlers {
    bool on_app_tx_ready() noexcept override {
      (void)trace_handlers::on_app_tx_ready();
      return false;
    }
  };

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

  const quic_cid client_chosen_dcid{dcid_bytes};
  const quic_cid client_scid{scid_bytes};
  quic_conn client{client_tls};
  REQUIRE(client.init(client_chosen_dcid, client_scid, client_addr,
      server_addr, quic_cid{}, now_tp()));
  REQUIRE(client);
  constexpr std::array<uint8_t, 16> server_scid_bytes{0xaa, 0xbb, 0xcc, 0xdd,
      0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
  const quic_cid server_scid{server_scid_bytes};
  quic_conn server{server_tls};
  REQUIRE(server.init(client_scid, server_scid, server_addr, client_addr,
      client_chosen_dcid, now_tp()));
  REQUIRE(server);

  abort_on_tx_ready server_trace;
  server.set_handlers(&server_trace);
  // Client doesn't care which upcalls fire; attach a no-op so its
  // trampolines have something to deref.
  quic_conn_handlers noop_handlers;
  client.set_handlers(&noop_handlers);

  std::array<std::byte, 1500> backing{};
  auto make_buf = [&backing] {
    return iouring::iou_buffer::make_synthetic_write(
        {backing.data(), backing.size()});
  };

  // Pump until either the handshake completes or some side errors. The
  // server is expected to error once `on_app_tx_ready` returns false.
  bool saw_error = false;
  for (int iter = 0; iter < 16 && !saw_error; ++iter) {
    for (int safety = 0; safety < 32; ++safety) {
      auto buf = make_buf();
      const auto status = client.write_pkt(buf, now_tp());
      if (status != quic_decode_status::ok) {
        saw_error = true;
        break;
      }
      const auto payload = buf.payload_bytes();
      if (payload.empty()) break;
      const auto rv = server.read_pkt(payload, now_tp());
      if (rv != quic_decode_status::ok) {
        saw_error = true;
        break;
      }
    }
    if (saw_error) break;
    for (int safety = 0; safety < 32; ++safety) {
      auto buf = make_buf();
      const auto status = server.write_pkt(buf, now_tp());
      if (status != quic_decode_status::ok) {
        saw_error = true;
        break;
      }
      const auto payload = buf.payload_bytes();
      if (payload.empty()) break;
      const auto rv = client.read_pkt(payload, now_tp());
      if (rv != quic_decode_status::ok) {
        saw_error = true;
        break;
      }
    }
    if (server_trace.saw("handshake_completed")) break;
  }

  CHECK(server_trace.saw("app_tx_ready"));
  CHECK(saw_error);
  CHECK_FALSE(server_trace.saw("handshake_completed"));
}

#pragma region CloseKindString
TEST_CASE("CloseKindString", "[quic]") {
  // Each named value round-trips through `enum_as_string` / `parse_enum`.
  // Values are the on-wire CONNECTION_CLOSE frame type codes (0x1c, 0x1d).
  using namespace corvid::strings;
  using F = quic_close_kind;
  if (true) {
    CHECK(enum_as_string(F::transport) == "transport");
    CHECK(enum_as_string(F::application) == "application");
  }
  if (true) {
    constexpr F bad{0xff};
    CHECK(parse_enum("transport", bad) == F::transport);
    CHECK(parse_enum("application", bad) == F::application);
  }
}
#pragma endregion

#pragma region WriteStreamFlagsString
TEST_CASE("WriteStreamFlagsString", "[quic]") {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  using namespace corvid::strings;
  using F = write_stream_flags;
  if (true) {
    CHECK(enum_as_string(F::more) == "more");
    CHECK(enum_as_string(F::fin) == "fin");
    CHECK(enum_as_string(F::padding) == "padding");
  }
  if (true) {
    // Higher bits print first.
    CHECK(enum_as_string(F::padding | F::fin | F::more) ==
          "padding + fin + more");
  }
  if (true) {
    constexpr F bad{0xff};
    CHECK(parse_enum("more", bad) == F::more);
    CHECK(parse_enum("fin", bad) == F::fin);
    CHECK(parse_enum("padding", bad) == F::padding);
    CHECK(parse_enum("padding + fin + more", bad) ==
          (F::padding | F::fin | F::more));
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
