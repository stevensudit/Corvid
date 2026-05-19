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
  SECTION("ngtcp2_cid round-trip") {
    std::array<uint8_t, quic::quic_version_cid::cid_length> bytes{0x01, 0x23,
        0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    ngtcp2_cid in{};
    in.datalen = bytes.size();
    std::memcpy(in.data, bytes.data(), bytes.size());

    const auto cid = quic::quic_version_cid::from_ngtcp2_cid(in);

    ngtcp2_cid out{};
    quic::quic_version_cid::to_ngtcp2_cid(cid, out);
    REQUIRE(out.datalen == bytes.size());
    CHECK(std::ranges::equal(std::span<const uint8_t>{out.data, out.datalen},
        bytes));
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
