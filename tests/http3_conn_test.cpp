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

#include "../corvid/proto/quic/http3_conn.h"
#include "../corvid/strings/enum_conversion.h"

#include "catch2_main.h"

using namespace corvid::proto::quic;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// HTTP/3 unidirectional stream IDs, assigned by QUIC convention (RFC 9000 sec.
// 2.1): low two bits 0b10 = client-initiated uni, 0b11 = server-initiated uni,
// incrementing by 4. The control + QPACK encoder + QPACK decoder streams are
// the first three on each side.
constexpr auto client_control = static_cast<quic_stream_id>(2U);
constexpr auto client_qpack_enc = static_cast<quic_stream_id>(6U);
constexpr auto client_qpack_dec = static_cast<quic_stream_id>(10U);
constexpr auto server_control = static_cast<quic_stream_id>(3U);
constexpr auto server_qpack_enc = static_cast<quic_stream_id>(7U);
constexpr auto server_qpack_dec = static_cast<quic_stream_id>(11U);

// Records whether the peer's SETTINGS frame arrived. Everything else keeps the
// no-op base behavior.
struct recording_handlers: http3_conn_handlers {
  bool settings_received{false};
  [[nodiscard]] bool on_recv_settings(const http3_settings&) override {
    settings_received = true;
    return true;
  }
};

// Drive `from`'s outbound queue until it stops producing, feeding every byte
// into `to` on the same stream ID. Mirrors the eventual session drain, minus
// QUIC: nghttp3 bytes go straight across instead of through ngtcp2. Returns
// the number of (non-empty) writes relayed.
int pump(http3_conn& from, http3_conn& to) {
  int writes{0};
  for (int guard = 0; guard < 100; ++guard) {
    quic_stream_id stream_id = quic_stream_id::none;
    std::span<const iovec> vecs;
    stream_chunk chunk_fin{stream_chunk::more};
    REQUIRE(from.writev_stream(stream_id, vecs, chunk_fin));
    if (stream_id == quic_stream_id::none) break;

    size_t total{0};
    for (const auto& v : vecs) {
      size_t consumed{0};
      REQUIRE(to.read_stream(stream_id,
          {static_cast<const uint8_t*>(v.iov_base), v.iov_len},
          stream_chunk::more, consumed));
      total += v.iov_len;
    }
    REQUIRE(from.add_write_offset(stream_id, total));
    if (total != 0) ++writes;
    // A real stream end would set `fin`; the control / QPACK streams stay
    // open, so a fin-only zero-length offer should not occur here. Stop if it
    // does to avoid spinning.
    if (total == 0 && chunk_fin != stream_chunk::fin) break;
  }
  return writes;
}

} // namespace

TEST_CASE("http3_conn links nghttp3 control streams end to end", "[http3]") {
  recording_handlers client_handlers;
  recording_handlers server_handlers;

  http3_conn client;
  http3_conn server;
  client.set_handlers(&client_handlers);
  server.set_handlers(&server_handlers);

  SECTION("initializes both roles") {
    CHECK(client.init(connection_role::client));
    CHECK(server.init(connection_role::server));
    CHECK(client.ok());
    CHECK(server.ok());
    // Double init is rejected.
    CHECK_FALSE(client.init(connection_role::client));
  }

  SECTION("control + QPACK stream binding and SETTINGS exchange") {
    REQUIRE(client.init(connection_role::client));
    REQUIRE(server.init(connection_role::server));

    REQUIRE(client.bind_control_stream(client_control));
    REQUIRE(client.bind_qpack_streams(client_qpack_enc, client_qpack_dec));
    REQUIRE(server.bind_control_stream(server_control));
    REQUIRE(server.bind_qpack_streams(server_qpack_enc, server_qpack_dec));

    // Client -> server: the server must see the client's SETTINGS frame.
    CHECK(pump(client, server) > 0);
    CHECK(server_handlers.settings_received);
    CHECK_FALSE(client_handlers.settings_received);

    // Server -> client: and symmetrically, exercising the client read path and
    // the server's outbound path.
    CHECK(pump(server, client) > 0);
    CHECK(client_handlers.settings_received);
  }
}

TEST_CASE("NvFlagsString", "[http3]") {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  using namespace corvid;
  using namespace corvid::strings;
  using F = nv_flags;
  if (true) {
    CHECK(enum_as_string(F::never_index) == "never_index");
    CHECK(enum_as_string(F::no_copy_name) == "no_copy_name");
    CHECK(enum_as_string(F::no_copy_value) == "no_copy_value");
    CHECK(enum_as_string(F::try_index) == "try_index");
  }
  if (true) {
    // Higher bits print first.
    CHECK(enum_as_string(F::try_index | F::never_index) ==
          "try_index + never_index");
  }
  if (true) {
    constexpr F bad{0xff};
    CHECK(parse_enum("never_index", bad) == F::never_index);
    CHECK(parse_enum("try_index", bad) == F::try_index);
    CHECK(parse_enum("try_index + never_index", bad) ==
          (F::try_index | F::never_index));
  }
}

TEST_CASE("StreamChunkString", "[http3]") {
  // Both sequence values round-trip through `enum_as_string` / `parse_enum`.
  using namespace corvid;
  using namespace corvid::strings;
  using C = stream_chunk;
  if (true) {
    CHECK(enum_as_string(C::more) == "more");
    CHECK(enum_as_string(C::fin) == "fin");
  }
  if (true) {
    constexpr C bad{0xff};
    CHECK(parse_enum("more", bad) == C::more);
    CHECK(parse_enum("fin", bad) == C::fin);
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
