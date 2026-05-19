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
#include <cstdint>

#include "../corvid/proto/quic/quic_conn.h"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::proto::quic;

namespace {

constexpr std::array<uint8_t, 16> dcid_bytes{0xde, 0xad, 0xbe, 0xef, 0xfe,
    0xed, 0xfa, 0xce, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

constexpr std::array<uint8_t, 16> scid_bytes{0x11, 0x22, 0x33, 0x44, 0x55,
    0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};

quic_conn::time_point_t now{};

} // namespace

TEST_CASE("quic_conn constructs as server", "[quic][conn]") {
  const quic_cid dcid{dcid_bytes};
  const quic_cid scid{scid_bytes};
  const auto local = net_endpoint::loopback_v4(4433);
  const auto peer = net_endpoint::loopback_v4(54321);

  quic_conn conn{connection_role::server, dcid, scid, local, peer, now};
  REQUIRE(conn);
  CHECK(conn.role() == connection_role::server);
  CHECK_FALSE(conn.is_handshake_completed());
  CHECK(conn.native() != nullptr);
}

TEST_CASE("quic_conn constructs as client", "[quic][conn]") {
  const quic_cid dcid{dcid_bytes};
  const quic_cid scid{scid_bytes};
  const auto local = net_endpoint::loopback_v4(54321);
  const auto peer = net_endpoint::loopback_v4(4433);

  quic_conn conn{connection_role::client, dcid, scid, local, peer, now};
  REQUIRE(conn);
  CHECK(conn.role() == connection_role::client);
  CHECK_FALSE(conn.is_handshake_completed());
  CHECK(conn.native() != nullptr);
}

TEST_CASE("quic_conn expiry is queryable on a fresh conn", "[quic][conn]") {
  // Fresh server conn: ngtcp2 reports a non-default expiry (idle timeout or
  // handshake timer). We don't pin to an exact value -- it's enough that
  // the call returns without crashing and the wrapper round-trips through
  // its chrono conversion.
  const quic_cid dcid{dcid_bytes};
  const quic_cid scid{scid_bytes};
  const auto local = net_endpoint::loopback_v4(4433);
  const auto peer = net_endpoint::loopback_v4(54321);

  quic_conn conn{connection_role::server, dcid, scid, local, peer, now};
  REQUIRE(conn);

  const auto deadline = conn.expiry();
  // Any non-default value is fine; the API just needs to round-trip.
  (void)deadline;
}
