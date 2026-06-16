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
#include "../corvid/enums/enum_conversion.h"

#include "catch2_main.h"

using namespace corvid::proto::quic;
using namespace corvid::proto::quic::http3_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("h3_error_code names round-trip across the gap", "[http3]") {
  using namespace corvid;
  using E = h3_error_code;

  // Forward: both blocks resolve by name; the gap between them, and codes
  // past the last block, print numerically.
  CHECK(enum_as_string(E::no_error) == "no_error");                 // 0x0100
  CHECK(enum_as_string(E::version_fallback) == "version_fallback"); // 0x0110
  CHECK(enum_as_string(E::qpack_decompression_failed) ==
        "qpack_decompression_failed"); // 0x0200
  CHECK(enum_as_string(E::qpack_decoder_stream_error) ==
        "qpack_decoder_stream_error");      // 0x0202
  CHECK(enum_as_string(E{0x111}) == "273"); // in the gap between blocks
  CHECK(enum_as_string(E{0x300}) == "768"); // past the last block

  // Reverse: names map back; an unknown code does not.
  CHECK(parse_enum<E>("internal_error") == E::internal_error);
  CHECK(parse_enum<E>("qpack_encoder_stream_error") ==
        E::qpack_encoder_stream_error);
  CHECK(!parse_enum<E>("nonexistent"));
}

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

// Records the peer's SETTINGS arrival plus any received HEADERS,
// end-of-headers, and end-of-stream. Everything else keeps the no-op base
// behavior.
struct recording_handlers: http3_conn_handlers {
  bool settings_received{false};
  std::vector<std::pair<std::string, std::string>> headers;
  bool headers_ended{false};
  bool stream_ended{false};
  void* last_user_data{};

  [[nodiscard]] bool on_recv_settings(const http3_settings&) override {
    settings_received = true;
    return true;
  }
  [[nodiscard]] bool on_recv_header(quic_stream_id, qpack_token,
      std::string_view name, std::string_view value, nv_flags,
      void* user_data) override {
    headers.emplace_back(std::string{name}, std::string{value});
    last_user_data = user_data;
    return true;
  }
  [[nodiscard]] bool
  on_end_headers(quic_stream_id, stream_chunk, void*) override {
    headers_ended = true;
    return true;
  }
  [[nodiscard]] bool on_end_stream(quic_stream_id, void*) override {
    stream_ended = true;
    return true;
  }
};

// True if `headers` contains a field with the given name and value.
[[nodiscard]] bool
has_field(const std::vector<std::pair<std::string, std::string>>& headers,
    std::string_view name, std::string_view value) {
  for (const auto& [n, v] : headers)
    if (n == name && v == value) return true;
  return false;
}

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
    for (size_t i = 0; i < vecs.size(); ++i) {
      const auto& v = vecs[i];
      // The FIN rides with the final byte chunk.
      const auto fin = (i + 1 == vecs.size()) ? chunk_fin : stream_chunk::more;
      size_t consumed{0};
      REQUIRE(to.read_stream(stream_id,
          {static_cast<const uint8_t*>(v.iov_base), v.iov_len}, fin,
          consumed));
      total += v.iov_len;
    }
    // A pure FIN (no bytes) still has to be delivered.
    if (vecs.empty() && chunk_fin == stream_chunk::fin) {
      size_t consumed{0};
      REQUIRE(to.read_stream(stream_id, {}, stream_chunk::fin, consumed));
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

TEST_CASE("http3_conn round-trips a request and response", "[http3]") {
  recording_handlers client_handlers;
  recording_handlers server_handlers;

  http3_conn client;
  http3_conn server;
  client.set_handlers(&client_handlers);
  server.set_handlers(&server_handlers);
  REQUIRE(client.init(connection_role::client));
  REQUIRE(server.init(connection_role::server));

  REQUIRE(client.bind_control_stream(client_control));
  REQUIRE(client.bind_qpack_streams(client_qpack_enc, client_qpack_dec));
  REQUIRE(server.bind_control_stream(server_control));
  REQUIRE(server.bind_qpack_streams(server_qpack_enc, server_qpack_dec));

  // First client-initiated bidirectional stream (low two bits 0b00).
  const auto request_stream = static_cast<quic_stream_id>(0U);

  // Client -> server: a header-only GET request (ends the stream).
  const std::array request{
      http3_field::make(":method", "GET"_method),
      http3_field::make(":scheme", "https"),
      http3_field::make(":authority", "example.com"),
      http3_field::make(":path", "/"),
  };
  REQUIRE(client.submit_request(request_stream, request));
  pump(client, server);

  CHECK(server_handlers.headers_ended);
  CHECK(server_handlers.stream_ended);
  CHECK(has_field(server_handlers.headers, ":method", "GET"_method));
  CHECK(has_field(server_handlers.headers, ":scheme", "https"));
  CHECK(has_field(server_handlers.headers, ":authority", "example.com"));
  CHECK(has_field(server_handlers.headers, ":path", "/"));

  // Server -> client: a header-only 200 response (ends the stream).
  const std::array response{
      http3_field::make(":status", "200"),
      http3_field::make("content-length", "0"),
  };
  REQUIRE(server.submit_response(request_stream, response));
  pump(server, client);

  CHECK(client_handlers.headers_ended);
  CHECK(client_handlers.stream_ended);
  CHECK(has_field(client_handlers.headers, ":status", "200"));
  CHECK(has_field(client_handlers.headers, "content-length", "0"));
}

TEST_CASE("http3_conn blocks and unblocks stream output", "[http3]") {
  recording_handlers client_handlers;
  recording_handlers server_handlers;

  http3_conn client;
  http3_conn server;
  client.set_handlers(&client_handlers);
  server.set_handlers(&server_handlers);
  REQUIRE(client.init(connection_role::client));
  REQUIRE(server.init(connection_role::server));

  REQUIRE(client.bind_control_stream(client_control));
  REQUIRE(client.bind_qpack_streams(client_qpack_enc, client_qpack_dec));
  REQUIRE(server.bind_control_stream(server_control));
  REQUIRE(server.bind_qpack_streams(server_qpack_enc, server_qpack_dec));

  // Block every outgoing uni stream the client has pending (control carries
  // SETTINGS, the QPACK streams their type byte). `writev_stream` then offers
  // nothing, so the server never sees the SETTINGS.
  client.block_stream(client_control);
  client.block_stream(client_qpack_enc);
  client.block_stream(client_qpack_dec);
  CHECK(pump(client, server) == 0);
  CHECK_FALSE(server_handlers.settings_received);

  // Unblocking releases the same bytes; the SETTINGS now flush through.
  CHECK(client.unblock_stream(client_control));
  CHECK(client.unblock_stream(client_qpack_enc));
  CHECK(client.unblock_stream(client_qpack_dec));
  CHECK(pump(client, server) > 0);
  CHECK(server_handlers.settings_received);
}

TEST_CASE("http3_conn set_stream_user_data round-trips to upcalls",
    "[http3]") {
  recording_handlers client_handlers;
  recording_handlers server_handlers;

  http3_conn client;
  http3_conn server;
  client.set_handlers(&client_handlers);
  server.set_handlers(&server_handlers);
  REQUIRE(client.init(connection_role::client));
  REQUIRE(server.init(connection_role::server));

  REQUIRE(client.bind_control_stream(client_control));
  REQUIRE(client.bind_qpack_streams(client_qpack_enc, client_qpack_dec));
  REQUIRE(server.bind_control_stream(server_control));
  REQUIRE(server.bind_qpack_streams(server_qpack_enc, server_qpack_dec));

  const auto request_stream = static_cast<quic_stream_id>(0U);

  // No such stream yet: nghttp3 has nothing to attach the pointer to.
  int marker{};
  CHECK_FALSE(client.set_stream_user_data(request_stream, &marker));

  const std::array request{
      http3_field::make(":method", "GET"_method),
      http3_field::make(":scheme", "https"),
      http3_field::make(":authority", "example.com"),
      http3_field::make(":path", "/"),
  };
  REQUIRE(client.submit_request(request_stream, request));
  pump(client, server);

  // The request stream now exists on the client; associate user data with it.
  CHECK(client.set_stream_user_data(request_stream, &marker));

  // The server's response drives the client's recv-header upcalls, which must
  // carry the pointer we just set.
  const std::array response{
      http3_field::make(":status", "200"),
      http3_field::make("content-length", "0"),
  };
  REQUIRE(server.submit_response(request_stream, response));
  pump(server, client);

  REQUIRE_FALSE(client_handlers.headers.empty());
  CHECK(client_handlers.last_user_data == &marker);
}
// NOLINTEND(readability-function-cognitive-complexity)
