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

#include <cstdint>
#include <vector>

#include "../corvid/proto/quic/quic_stream_send_queue.h"
#include "catch2_main.h"

using corvid::proto::quic::quic_stream_send_queue;
using corvid::proto::quic::write_stream_flags;

namespace {

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> il) {
  return std::vector<uint8_t>{il};
}

uint64_t iov_total(std::span<const iovec> iov) {
  uint64_t n = 0;
  for (const auto& e : iov) n += e.iov_len;
  return n;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("empty queue reports no work", "[quic][send_queue]") {
  quic_stream_send_queue q;
  CHECK_FALSE(q.has_work());
  CHECK(q.fully_drained());
  CHECK(q.writable_iov().empty());
  CHECK(q.writable_flags() == write_stream_flags::none);
  CHECK(q.appended() == 0);
  CHECK(q.offered() == 0);
  CHECK(q.acked() == 0);
}

TEST_CASE("single-chunk full-accept lifecycle", "[quic][send_queue]") {
  quic_stream_send_queue q;
  q.append(bytes({1, 2, 3, 4, 5}));
  REQUIRE(q.has_work());
  REQUIRE(q.appended() == 5);
  REQUIRE(q.retained_chunks() == 1);

  auto iov = q.writable_iov();
  REQUIRE(iov.size() == 1);
  CHECK(iov[0].iov_len == 5);
  CHECK(iov_total(iov) == 5);

  q.commit(5);
  CHECK_FALSE(q.has_work());
  CHECK(q.offered() == 5);
  CHECK(q.writable_iov().empty());

  // Bytes still retained while peer hasn't ACKed.
  CHECK(q.retained_chunks() == 1);
  CHECK_FALSE(q.fully_drained());

  q.retire_acked(5);
  CHECK(q.acked() == 5);
  CHECK(q.retained_chunks() == 0);
  CHECK(q.fully_drained());
}

TEST_CASE("partial accept advances offered into mid-chunk",
    "[quic][send_queue]") {
  quic_stream_send_queue q;
  q.append(bytes({10, 20, 30, 40, 50, 60, 70, 80}));

  // ngtcp2 accepts only 3 bytes this turn.
  q.commit(3);
  CHECK(q.offered() == 3);
  REQUIRE(q.has_work());

  auto iov = q.writable_iov();
  REQUIRE(iov.size() == 1);
  CHECK(iov[0].iov_len == 5);
  const auto* base = static_cast<const uint8_t*>(iov[0].iov_base);
  CHECK(base[0] == 40);
  CHECK(base[4] == 80);

  // Finish accept.
  q.commit(5);
  CHECK(q.offered() == 8);
  CHECK_FALSE(q.has_work());
}

TEST_CASE("retire pops only fully acked chunks", "[quic][send_queue]") {
  quic_stream_send_queue q;
  q.append(bytes({1, 2, 3}));
  q.append(bytes({4, 5, 6, 7}));
  q.append(bytes({8, 9}));
  REQUIRE(q.retained_chunks() == 3);

  q.commit(9);
  REQUIRE(q.offered() == 9);

  // ACK covers chunk 0 entirely and half of chunk 1.
  q.retire_acked(5);
  CHECK(q.acked() == 5);
  CHECK(q.retained_chunks() == 2);

  // ACK covers the rest of chunk 1.
  q.retire_acked(2);
  CHECK(q.acked() == 7);
  CHECK(q.retained_chunks() == 1);

  q.retire_acked(2);
  CHECK(q.acked() == 9);
  CHECK(q.retained_chunks() == 0);
  CHECK(q.fully_drained());
}

TEST_CASE("iov spans multiple chunks past offered", "[quic][send_queue]") {
  quic_stream_send_queue q;
  q.append(bytes({1, 2, 3}));
  q.append(bytes({4, 5}));
  q.append(bytes({6, 7, 8, 9}));

  // Offer covers chunk 0 entirely and one byte of chunk 1.
  q.commit(4);
  auto iov = q.writable_iov();
  REQUIRE(iov.size() == 2);
  CHECK(iov[0].iov_len == 1);
  CHECK(iov[1].iov_len == 4);
  CHECK(iov_total(iov) == 5);
  CHECK(static_cast<const uint8_t*>(iov[0].iov_base)[0] == 5);
  CHECK(static_cast<const uint8_t*>(iov[1].iov_base)[0] == 6);
}

TEST_CASE("fin rides on writable_flags until all bytes offered",
    "[quic][send_queue]") {
  quic_stream_send_queue q;
  q.append(bytes({1, 2, 3, 4}), write_stream_flags::fin);

  CHECK(q.writable_flags() == write_stream_flags::fin);
  REQUIRE(q.has_work());

  q.commit(2);
  // Still have bytes left to offer, fin still pending.
  CHECK(q.writable_flags() == write_stream_flags::fin);
  REQUIRE(q.has_work());

  q.commit(2);
  // All bytes offered + fin marked handed-off; stop passing the flag.
  CHECK(q.writable_flags() == write_stream_flags::none);
  CHECK_FALSE(q.has_work());

  // Still not fully drained: bytes outstanding to ACK.
  CHECK_FALSE(q.fully_drained());
  q.retire_acked(4);
  CHECK(q.fully_drained());
}

TEST_CASE("fin-only append (no bytes) flushes cleanly", "[quic][send_queue]") {
  quic_stream_send_queue q;
  q.append({}, write_stream_flags::fin);

  CHECK(q.appended() == 0);
  CHECK(q.retained_chunks() == 0);
  REQUIRE(q.has_work());
  CHECK(q.writable_iov().empty());
  CHECK(q.writable_flags() == write_stream_flags::fin);

  // A drain pass with empty iov + fin: ngtcp2 accepts 0 stream bytes but
  // buffers the fin.
  q.commit(0);
  CHECK_FALSE(q.has_work());
  CHECK(q.writable_flags() == write_stream_flags::none);
  CHECK(q.fully_drained()); // no bytes to ACK; fin handed off.
}

TEST_CASE("fin appended on later chunk", "[quic][send_queue]") {
  quic_stream_send_queue q;
  q.append(bytes({1, 2, 3}));
  CHECK(q.writable_flags() == write_stream_flags::none);
  q.append(bytes({4, 5}), write_stream_flags::fin);
  CHECK(q.writable_flags() == write_stream_flags::fin);

  q.commit(3);
  CHECK(q.writable_flags() == write_stream_flags::fin);
  q.commit(2);
  CHECK(q.writable_flags() == write_stream_flags::none);
}

// NOLINTEND(readability-function-cognitive-complexity)
