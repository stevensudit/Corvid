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
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include "../corvid/proto/quic/quic_dgram_router.h"

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::iouring;
using namespace corvid::proto::quic;
using namespace std::chrono_literals;

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
// can read them through `payload_view`. The buffer is read-only in practice,
// but `make_synthetic` takes a mutable `span<std::byte>` (its general read
// path lets the kernel fill the storage), so callers must pass a non-const
// buffer.
iou_loop::buffer wrap(std::span<uint8_t> bytes) {
  return iou_loop::buffer::make_synthetic(
      {reinterpret_cast<std::byte*>(bytes.data()), bytes.size()});
}

bool wait_for(const auto& pred, std::chrono::milliseconds timeout = 500ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return pred();
}

constexpr std::array<uint8_t, 16> sample_dcid{0xde, 0xad, 0xbe, 0xef, 0xfe,
    0xed, 0xfa, 0xce, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

constexpr std::array<uint8_t, 16> sample_scid{0x11, 0x22, 0x33, 0x44, 0x55,
    0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
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

TEST_CASE("quic_dgram_protocol::router_plugin::create_session",
    "[quic][router]") {
  using handle_t = iou_dgram_router_handle<quic_dgram_protocol::router_plugin>;

  iou_loop_runner runner;

  // A real UDP socket is bound but never used for actual I/O in this test;
  // we drive the plugin methods directly on the loop thread.
  auto handle = handle_t::bind(*runner.loop(), net_endpoint::loopback_v4(),
      shot_type::single);
  REQUIRE(handle);
  auto& router = *handle.pointer();

  SECTION("long-header packet creates a session under the DCID") {
    auto pkt = make_long_header_packet(sample_dcid, sample_scid);
    auto buf = wrap(pkt);

    std::atomic_bool created{false};
    std::atomic_bool removed{false};
    (void)runner.loop()->execute_or_post(
        [&router, &buf, &created, &removed]() -> bool {
          // `extract` and `create_session` are intended to run on the loop
          // thread (the router's recv callback dispatches them there).
          created = router.plugin().create_session(buf, router);
          // The session must now be registered under the parsed DCID.
          removed = router.remove_session(quic_cid{sample_dcid});
          return true;
        });
    REQUIRE(wait_for([&] { return created.load() && removed.load(); }));
  }

  SECTION("short-header packet does not create a session") {
    auto pkt = make_short_header_packet(sample_dcid);
    auto buf = wrap(pkt);

    std::atomic_bool decided{false};
    std::atomic_bool created{false};
    std::atomic_bool removed{false};
    (void)runner.loop()->execute_or_post(
        [&router, &buf, &created, &removed, &decided]() -> bool {
          created = router.plugin().create_session(buf, router);
          // Nothing should be registered under the DCID.
          removed = router.remove_session(quic_cid{sample_dcid});
          decided = true;
          return true;
        });
    REQUIRE(wait_for([&] { return decided.load(); }));
    CHECK_FALSE(created.load());
    CHECK_FALSE(removed.load());
  }

  SECTION("malformed packet does not create a session") {
    std::array<uint8_t, 1> stub{0xc0};
    auto buf = wrap(stub);

    std::atomic_bool decided{false};
    std::atomic_bool created{false};
    (void)runner.loop()->execute_or_post(
        [&router, &buf, &created, &decided]() -> bool {
          created = router.plugin().create_session(buf, router);
          decided = true;
          return true;
        });
    REQUIRE(wait_for([&] { return decided.load(); }));
    CHECK_FALSE(created.load());
  }
}

TEST_CASE("quic_dgram_protocol::session_plugin tracks its DCID",
    "[quic][session]") {
  // End-to-end: drive a long-header packet through create_session, then
  // close the router and verify that unregister_self removes the CID.
  using handle_t = iou_dgram_router_handle<quic_dgram_protocol::router_plugin>;

  iou_loop_runner runner;
  auto handle = handle_t::bind(*runner.loop(), net_endpoint::loopback_v4(),
      shot_type::single);
  REQUIRE(handle);
  auto& router = *handle.pointer();

  auto pkt = make_long_header_packet(sample_dcid, sample_scid);
  auto buf = wrap(pkt);

  std::atomic_bool created{false};
  (void)runner.loop()->execute_or_post([&router, &buf, &created]() -> bool {
    created = router.plugin().create_session(buf, router);
    return true;
  });
  REQUIRE(wait_for([&] { return created.load(); }));

  // Closing the router fires every session's `unregister_self`, which calls
  // `router.remove_session(key_)`. If that path worked, a follow-up direct
  // `remove_session` for the same key finds nothing to erase.
  CHECK(handle.close());

  std::atomic_bool still_registered{true};
  std::atomic_bool decided{false};
  (void)runner.loop()->execute_or_post(
      [&router, &still_registered, &decided]() -> bool {
        still_registered = router.remove_session(quic_cid{sample_dcid});
        decided = true;
        return true;
      });
  REQUIRE(wait_for([&] { return decided.load(); }));
  CHECK_FALSE(still_registered.load());
}
// NOLINTEND(readability-function-cognitive-complexity)
