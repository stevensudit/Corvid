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
#include <cstring>
#include <functional>

#include "../corvid/proto/quic/quic_header.h"
#include "catch2_main.h"

namespace quic = corvid::proto::quic;

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("quic_connection_id basics", "[quic]") {
  SECTION("value-initialized is empty") {
    const quic::quic_cid cid{};
    CHECK(cid.length() == 0);
    CHECK(cid.empty());
  }

  SECTION("byte-span round-trip") {
    const std::array<uint8_t, 8> bytes{0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
        0xcd, 0xef};
    const quic::quic_cid cid{bytes};
    REQUIRE(cid.length() == bytes.size());
    CHECK(std::ranges::equal(cid.bytes(), bytes));
  }

  SECTION("equal CIDs hash equally") {
    const std::array<uint8_t, 16> bytes{0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed,
        0xfa, 0xce, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const quic::quic_cid a{bytes};
    const quic::quic_cid b{bytes};
    CHECK(a == b);
    CHECK(std::hash<quic::quic_cid>{}(a) == std::hash<quic::quic_cid>{}(b));
  }

  SECTION("different lengths compare unequal") {
    const std::array<uint8_t, 8> a_bytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08};
    // Same prefix, longer. The CIDs are not equal.
    const std::array<uint8_t, 16> b_bytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    const quic::quic_cid a{a_bytes};
    const quic::quic_cid b{b_bytes};
    CHECK(a != b);
    CHECK(a < b);
  }

  SECTION("ngtcp2_cid round-trip") {
    const std::array<uint8_t, 8> bytes{0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
        0x70, 0x80};
    ngtcp2_cid in{};
    in.datalen = bytes.size();
    std::memcpy(in.data, bytes.data(), bytes.size());

    const quic::quic_cid cid{in};
    REQUIRE(cid.length() == bytes.size());
    CHECK(std::ranges::equal(cid.bytes(), bytes));

    const ngtcp2_cid& out = cid.value();
    REQUIRE(out.datalen == bytes.size());
    CHECK(std::ranges::equal(std::span<const uint8_t>{out.data, out.datalen},
        bytes));
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
