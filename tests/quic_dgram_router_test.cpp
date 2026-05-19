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
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../corvid/proto/quic/quic_dgram_router.h"

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::iouring;
using namespace corvid::proto::quic;

namespace {

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
// constant `quic_dgram_protocol::cid_length`).
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

TEST_CASE("quic_dgram_protocol::router_plugin::extract", "[quic][router]") {
  quic_dgram_protocol::router_plugin plugin;

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
    REQUIRE(key.length() == quic_dgram_protocol::cid_length);
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
