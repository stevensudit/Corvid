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
#include "../corvid/proto/io_uring/iou_buf_pool.h"

#include <cstring>
#include <netinet/in.h>
#include <string_view>
#include <functional>

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::proto::iouring;
using namespace std::string_view_literals;
using block_type = iou_buffer::block_type;

// Simulate kernel filling bytes into `buf`'s active_span, then call update.
// Returns the byte count "received".
static size_t sim_read(iou_buf_pool::buffer& buf, std::string_view data) {
  auto active = buf.active_span();
  const size_t n = std::min(data.size(), active.size());
  std::memcpy(active.data(), data.data(), n);
  buf.update(iou_res{static_cast<int32_t>(n)}, iou_cqe_flags{});
  return n;
}

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region ReadInitialState
TEST_CASE("ReadInitialState", "[IouBufPool]") {
  // Freshly-allocated read buffer: empty payload, active = entire block.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);
    CHECK(buf.blockrw() == block_type::read);
    CHECK(buf.size() == 4096ULL);
    CHECK(buf.payload_span().size() == 0ULL);
    CHECK(buf.active_span().size() == buf.size());
    CHECK(buf.active_span().data() == buf.payload_span().data());
    CHECK_FALSE(buf.result().ok());
  }
  if (true) {
    // If this fails, it means we can finally get rid of the hack where we
    // pretend to have a copy constructor but actually throw.
#if defined(__cpp_lib_move_only_function) && !defined(__GLIBCXX__)
    CHECK(false);
#endif
  }
}
#pragma endregion

#pragma region ReadAfterUpdate
TEST_CASE("ReadAfterUpdate", "[IouBufPool]") {
  // After a simulated read, payload grows and active shrinks to the tail.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    const size_t n = sim_read(buf, "hello world"sv);
    CHECK(buf.payload_span().size() == n);
    CHECK(buf.active_span().size() == (buf.size() - n));
    CHECK(buf.active_span().data() ==
          buf.payload_span().data() + buf.payload_span().size());
    CHECK(buf.result().ok());
    CHECK(buf.payload_view().substr(0, 5) == "hello");
  }
}
#pragma endregion

#pragma region ReadMultipleUpdates
TEST_CASE("ReadMultipleUpdates", "[IouBufPool]") {
  // Two successive reads concatenate into a single growing payload.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    sim_read(buf, "hello"sv);
    CHECK(buf.payload_span().size() == 5ULL);

    sim_read(buf, " world"sv);
    CHECK(buf.payload_span().size() == 11ULL);
    CHECK(buf.payload_view() == "hello world");
    CHECK(buf.active_span().size() == (buf.size() - 11));
  }
}
#pragma endregion

#pragma region ReadConsumePartial
TEST_CASE("ReadConsumePartial", "[IouBufPool]") {
  // consume_read(n) with n < payload returns n bytes and advances the front.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    sim_read(buf, "abcdefgh"sv);
    REQUIRE(buf.payload_span().size() == 8ULL);

    auto slice = buf.consume_read(3);
    CHECK(slice.size() == 3ULL);
    CHECK((std::string_view(reinterpret_cast<const char*>(slice.data()), 3)) ==
          ("abc"));
    CHECK(buf.payload_span().size() == 5ULL);
    CHECK(buf.payload_view() == "defgh");
    // active_span still covers the tail beyond the original 8 bytes read.
    CHECK(buf.active_span().size() == (buf.size() - 8));
  }
}
#pragma endregion

#pragma region ReadConsumeFullReset
TEST_CASE("ReadConsumeFullReset", "[IouBufPool]") {
  // Consuming all payload bytes resets the buffer to its initial state.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);
    const std::byte* base = buf.active_span().data();

    sim_read(buf, "reset-me"sv);
    const size_t n = buf.payload_span().size();
    const auto slice = buf.consume_read(n);
    CHECK(slice.size() == n);

    CHECK(buf.payload_span().size() == 0ULL);
    CHECK(buf.active_span().size() == buf.size());
    CHECK(buf.payload_span().data() == base);
    CHECK(buf.active_span().data() == base);
  }
}
#pragma endregion

#pragma region ReadConsumeOverRequest
TEST_CASE("ReadConsumeOverRequest", "[IouBufPool]") {
  // Requesting more bytes than available returns only what's present.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    sim_read(buf, "hi"sv);
    const auto slice = buf.consume_read(9999);
    CHECK(slice.size() == 2ULL);
    // Fully consumed: reset to initial state.
    CHECK(buf.payload_span().size() == 0ULL);
    CHECK(buf.active_span().size() == buf.size());
  }
}
#pragma endregion

#pragma region ReadUpdateError
TEST_CASE("ReadUpdateError", "[IouBufPool]") {
  // An error result leaves spans unchanged.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    sim_read(buf, "good data"sv);
    const size_t payload_before = buf.payload_span().size();
    const size_t active_before = buf.active_span().size();

    buf.update(iou_res{-ECONNRESET}, iou_cqe_flags{});
    CHECK_FALSE(buf.result().ok());
    CHECK(buf.payload_span().size() == payload_before);
    CHECK(buf.active_span().size() == active_before);
  }
}
#pragma endregion

#pragma region WriteInitialState
TEST_CASE("WriteInitialState", "[IouBufPool]") {
  // Freshly-allocated write buffer: both payload and active are empty.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);
    CHECK(buf.blockrw() == block_type::write);
    CHECK(buf.size() == 4096ULL);
    CHECK(buf.payload_span().size() == 0ULL);
    CHECK(buf.active_span().size() == 0ULL);
    CHECK(buf.payload_span().data() == buf.active_span().data());
  }
}
#pragma endregion

#pragma region WriteViaAppend
TEST_CASE("WriteViaAppend", "[IouBufPool]") {
  // append fills payload and active_span covers the same bytes.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    CHECK(buf.append("hello"sv));
    CHECK(buf.payload_span().size() == 5ULL);
    CHECK(buf.active_span().size() == 5ULL);
    CHECK(buf.active_span().data() == buf.payload_span().data());
    CHECK(buf.payload_view() == "hello");

    // Second append extends both spans.
    CHECK(buf.append(", world"sv));
    CHECK(buf.payload_span().size() == 12ULL);
    CHECK(buf.active_span().size() == 12ULL);
    CHECK(buf.payload_view() == "hello, world");
  }
}
#pragma endregion

#pragma region WriteAppendOverflow
TEST_CASE("WriteAppendOverflow", "[IouBufPool]") {
  // append returns false without modifying anything when data would not fit.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    const size_t cap = buf.size();
    const std::string filler(cap, 'x');
    CHECK(buf.append(std::string_view{filler}));
    CHECK(buf.payload_span().size() == cap);

    CHECK_FALSE(buf.append("y"sv));          // one byte too many
    CHECK(buf.payload_span().size() == cap); // unchanged
  }
}
#pragma endregion

#pragma region WriteViaTailAndUpdatePayload
TEST_CASE("WriteViaTailAndUpdatePayload", "[IouBufPool]") {
  // Manual fill: get tail_span, memcpy, then update_payload.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    auto tail = buf.tail_span();
    CHECK(tail.size() == buf.size()); // tail = full block initially
    constexpr std::string_view msg{"tail-fill"};
    std::memcpy(tail.data(), msg.data(), msg.size());
    CHECK(buf.update_payload(tail.first(msg.size())));

    CHECK(buf.payload_span().size() == msg.size());
    CHECK(buf.active_span().size() == msg.size());
    CHECK(buf.payload_view() == msg);
  }
}
#pragma endregion

#pragma region WriteUpdatePayloadBadStart
TEST_CASE("WriteUpdatePayloadBadStart", "[IouBufPool]") {
  // update_payload rejects a span that does not start at payload_span's end.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    CHECK(buf.append("abc"sv));

    // tail_span() starts right after "abc"; subspan(1) shifts it by one byte.
    auto tail = buf.tail_span();
    CHECK_FALSE(buf.update_payload(tail.subspan(1, 3)));
    CHECK(buf.payload_span().size() == 3ULL); // unchanged
  }
}
#pragma endregion

#pragma region WriteUpdatePayloadOverflow
TEST_CASE("WriteUpdatePayloadOverflow", "[IouBufPool]") {
  // update_payload rejects a span whose end exceeds full_span.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    auto tail = buf.tail_span(); // entire block
    // Construct a span that ends one byte past full_span.
    const iou_buf_pool::span_t overrun{tail.data(), tail.size() + 1};
    CHECK_FALSE(buf.update_payload(overrun));
    CHECK(buf.payload_span().size() == 0ULL); // unchanged
  }
}
#pragma endregion

#pragma region WriteUpdatePartialSend
TEST_CASE("WriteUpdatePartialSend", "[IouBufPool]") {
  // Partial send advances active_span front while payload_span stays fixed.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    CHECK(buf.append("0123456789"sv)); // 10 bytes
    CHECK(buf.active_span().size() == 10ULL);

    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{6}, iou_cqe_flags{}); // kernel sent first 6 bytes

    CHECK(buf.payload_span().size() == 10ULL); // unchanged
    CHECK(buf.active_span().size() == 4ULL);   // 10 - 6
    CHECK((std::string_view(
              reinterpret_cast<const char*>(buf.active_span().data()), 4)) ==
          ("6789"));
  }
}
#pragma endregion

#pragma region WriteFullyConsumedThenAppend
TEST_CASE("WriteFullyConsumedThenAppend", "[IouBufPool]") {
  // After a complete send, the next append resets from the block base.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);
    const std::byte* base = buf.active_span().data();

    CHECK(buf.append("sent"sv));
    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{4}, iou_cqe_flags{}); // fully consumed
    CHECK(buf.active_span().size() == 0ULL);

    // payload_view still shows "sent" before the implicit reset.
    CHECK(buf.payload_view() == "sent");

    CHECK(buf.append("new"sv));
    CHECK(buf.payload_span().size() == 3ULL);
    CHECK(buf.active_span().size() == 3ULL);
    CHECK(buf.payload_span().data() == base);
    CHECK(buf.payload_view() == "new");
  }
}
#pragma endregion

#pragma region WriteFullyConsumedThenTailSpan
TEST_CASE("WriteFullyConsumedThenTailSpan", "[IouBufPool]") {
  // After a complete send, tail_span() triggers an implicit reset.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);
    const std::byte* base = buf.active_span().data();

    CHECK(buf.append("gone"sv));
    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{4}, iou_cqe_flags{});

    auto tail = buf.tail_span(); // triggers implicit reset
    CHECK(tail.size() == buf.size());
    CHECK(tail.data() == base);
  }
}
#pragma endregion

#pragma region WriteUpdateError
TEST_CASE("WriteUpdateError", "[IouBufPool]") {
  // An error result leaves spans unchanged.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    CHECK(buf.append("data"sv));
    const size_t payload_before = buf.payload_span().size();
    const size_t active_before = buf.active_span().size();

    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{-EPIPE}, iou_cqe_flags{});
    CHECK_FALSE(buf.result().ok());
    CHECK(buf.payload_span().size() == payload_before);
    CHECK(buf.active_span().size() == active_before);
  }
}
#pragma endregion

#pragma region AppendToPartiallySentBuffer
TEST_CASE("AppendToPartiallySentBuffer", "[IouBufPool]") {
  // After a partial send, appending more extends both payload and active.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    CHECK(buf.append("hello"sv));
    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{3},
        iou_cqe_flags{}); // sent "hel"; active points to "lo" (2 bytes)

    CHECK(buf.payload_span().size() == 5ULL);
    CHECK(buf.active_span().size() == 2ULL);

    CHECK(buf.append(" world"sv));
    CHECK(buf.payload_span().size() == 11ULL);
    CHECK(buf.active_span().size() == 8ULL);
    CHECK(buf.payload_view() == "hello world");
    CHECK((std::string_view(
              reinterpret_cast<const char*>(buf.active_span().data()), 8)) ==
          ("lo world"));
  }
}
#pragma endregion

#pragma region PromoteToWrite
TEST_CASE("PromoteToWrite", "[IouBufPool]") {
  // Promoting a read buffer keeps payload; active_span = payload_span.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    sim_read(buf, "proxy data"sv);
    REQUIRE(buf.payload_span().size() == 10ULL);
    const std::byte* payload_data = buf.payload_span().data();

    buf.promote_to_write();

    CHECK(buf.blockrw() == block_type::write);
    CHECK(buf.payload_span().size() == 10ULL);
    CHECK(buf.active_span().size() == 10ULL);
    CHECK(buf.active_span().data() == payload_data);
    CHECK(buf.payload_view() == "proxy data");
  }
}
#pragma endregion

#pragma region DemoteToRead
TEST_CASE("DemoteToRead", "[IouBufPool]") {
  // Demoting a write buffer keeps payload; active_span = tail after payload.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_writer();
    REQUIRE(buf);

    CHECK(buf.append("header"sv));
    buf.demote_to_read();

    CHECK(buf.blockrw() == block_type::read);
    CHECK(buf.payload_span().size() == 6ULL);
    CHECK(buf.active_span().size() == (buf.size() - 6ULL));
    CHECK(buf.payload_view() == "header");
    CHECK(buf.active_span().data() ==
          buf.payload_span().data() + buf.payload_span().size());
  }
}
#pragma endregion

#pragma region PromoteDemoteRoundtrip
TEST_CASE("PromoteDemoteRoundtrip", "[IouBufPool]") {
  // promote_to_write then demote_to_read preserves payload throughout.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    sim_read(buf, "roundtrip"sv);
    REQUIRE(buf.payload_span().size() == 9ULL);

    buf.promote_to_write();
    CHECK(buf.payload_view() == "roundtrip");
    CHECK(buf.active_span().size() == 9ULL);

    buf.demote_to_read();
    CHECK(buf.payload_view() == "roundtrip");
    CHECK(buf.active_span().size() == (buf.size() - 9ULL));
  }
}
#pragma endregion

#pragma region AvailableTracking
TEST_CASE("AvailableTracking", "[IouBufPool]") {
  // Allocating reduces available bytes; reset restores them.
  if (true) {
    auto pool = iou_buf_pool::create();
    const size_t initial = pool->available();

    auto r = pool->borrow_reader();
    REQUIRE(r);
    CHECK(pool->available() == (initial - r.size()));

    auto w = pool->borrow_writer();
    REQUIRE(w);
    CHECK(pool->available() == (initial - r.size() - w.size()));

    r.reset();
    CHECK(pool->available() == (initial - w.size()));

    w.reset();
    CHECK(pool->available() == initial);
  }
}
#pragma endregion

#pragma region MoveBuffer
TEST_CASE("MoveBuffer", "[IouBufPool]") {
  // Moving a buffer transfers ownership; source becomes empty.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto a = pool->borrow_writer();
    REQUIRE(a);
    CHECK(a.append("move me"sv));

    auto b = std::move(a);
    CHECK_FALSE(a); // NOLINT(bugprone-use-after-move)
    CHECK(b);
    CHECK(b.payload_view() == "move me");
  }
}
#pragma endregion

#pragma region CoalesceSmallToMedium
TEST_CASE("CoalesceSmallToMedium", "[IouBufPool]") {
  // Four smalls from the same medium coalesce back to one medium.
  // The three sibling mediums are held, so the large does NOT coalesce.
  if (true) {
    auto pool = iou_buf_pool::create();
    // Drain three mediums from the first large block (zone tail = lowest).
    auto m0 = pool->borrow_writer(block_size::kb016);
    auto m1 = pool->borrow_writer(block_size::kb016);
    auto m2 = pool->borrow_writer(block_size::kb016);
    REQUIRE(m0);
    REQUIRE(m1);
    REQUIRE(m2);
    // The fourth medium is split into four smalls.
    auto s0 = pool->borrow_writer(block_size::kb004);
    auto s1 = pool->borrow_writer(block_size::kb004);
    auto s2 = pool->borrow_writer(block_size::kb004);
    auto s3 = pool->borrow_writer(block_size::kb004);
    REQUIRE(s0);
    REQUIRE(s1);
    REQUIRE(s2);
    REQUIRE(s3);
    // Free all four smalls: they should coalesce into the fourth medium.
    s0.reset();
    s1.reset();
    s2.reset();
    s3.reset();
    // Three sibling mediums are still held: the large must NOT coalesce.
    // The coalesced medium should be immediately allocatable.
    auto m_new = pool->borrow_writer(block_size::kb016);
    REQUIRE(m_new);
    // Release everything and verify full recovery.
    m0.reset();
    m1.reset();
    m2.reset();
    m_new.reset();
    CHECK(pool->available() == (2ULL * 1024 * 1024));
  }
}
#pragma endregion

#pragma region CoalesceMediumToLarge
TEST_CASE("CoalesceMediumToLarge", "[IouBufPool]") {
  // Four mediums from the same large coalesce back to one large.
  if (true) {
    auto pool = iou_buf_pool::create();
    const size_t initial = pool->available();
    auto m0 = pool->borrow_writer(block_size::kb016);
    auto m1 = pool->borrow_writer(block_size::kb016);
    auto m2 = pool->borrow_writer(block_size::kb016);
    auto m3 = pool->borrow_writer(block_size::kb016);
    REQUIRE(m0);
    REQUIRE(m1);
    REQUIRE(m2);
    REQUIRE(m3);
    m0.reset();
    m1.reset();
    m2.reset();
    m3.reset();
    // All four mediums freed: the parent large must be reconstructed.
    CHECK(pool->available() == initial);
    auto l = pool->borrow_writer(block_size::kb032);
    REQUIRE(l);
    l.reset();
    CHECK(pool->available() == initial);
  }
}
#pragma endregion

#pragma region CoalesceChain
TEST_CASE("CoalesceChain", "[IouBufPool]") {
  // Allocate all 512 smalls, free all 512: cascading coalesce must rebuild
  // all 32 large blocks.
  if (true) {
    auto pool = iou_buf_pool::create();
    constexpr size_t TOTAL_SMALLS = 512;
    constexpr size_t TOTAL_LARGE = 32;
    std::array<iou_buf_pool::buffer, TOTAL_SMALLS> bufs;
    for (size_t i = 0; i < TOTAL_SMALLS; ++i) {
      bufs[i] = pool->borrow_writer(block_size::kb004);
      REQUIRE(bufs[i]);
    }
    CHECK(pool->available() == 0ULL);
    for (size_t i = 0; i < TOTAL_SMALLS; ++i) bufs[i].reset();
    CHECK(pool->available() == (2ULL * 1024 * 1024));
    // All 32 large blocks must now be individually allocatable.
    std::array<iou_buf_pool::buffer, TOTAL_LARGE> large_bufs;
    for (size_t i = 0; i < TOTAL_LARGE; ++i) {
      large_bufs[i] = pool->borrow_writer(block_size::kb064);
      REQUIRE(large_bufs[i]);
    }
    auto extra = pool->borrow_writer(block_size::kb064);
    CHECK_FALSE(extra);
    for (auto& b : large_bufs) b.reset();
    CHECK(pool->available() == (2ULL * 1024 * 1024));
  }
}
#pragma endregion

#pragma region UdpTierAlloc
TEST_CASE("UdpTierAlloc", "[IouBufPool]") {
  // Allocate all 1024 x 2 KB slots (2 MB / 2 KB), verify each succeeds and
  // has the right size, then confirm full pool recovery after freeing all.
  if (true) {
    auto pool = iou_buf_pool::create();
    constexpr size_t TOTAL = 2ULL * 1024 * 1024 / (2ULL * 1024); // 1024
    std::array<iou_buf_pool::buffer, TOTAL> bufs;
    for (size_t i = 0; i < TOTAL; ++i) {
      bufs[i] = pool->borrow_writer(block_size::kb002);
      REQUIRE(bufs[i]);
      CHECK(bufs[i].size() == (2ULL * 1024));
    }
    CHECK(pool->available() == 0ULL);
    auto extra = pool->borrow_writer(block_size::kb002);
    CHECK_FALSE(extra);
    for (auto& b : bufs) b.reset();
    CHECK(pool->available() == (2ULL * 1024 * 1024));
  }
}
#pragma endregion

#pragma region UpdateRecvmsgMsgFlagsDefault
TEST_CASE("UpdateRecvmsgMsgFlagsDefault", "[IouBufPool]") {
  // A freshly borrowed buffer has msg_flags() == 0.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);
    CHECK(buf.msghdr_flags() == msg_flags{});
  }
}
#pragma endregion

#pragma region UpdateRecvmsgValid
TEST_CASE("UpdateRecvmsgValid", "[IouBufPool]") {
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    constexpr std::string_view data{"hello-recvmsg"};
    constexpr uint32_t peer_ip = 0x7f000001; // 127.0.0.1
    constexpr uint16_t peer_port = 54321;

    // Write io_uring_recvmsg_out header + peer sockaddr_in + payload at the
    // start of the buffer's active region.
    auto* mem = buf.active_span().data();
    auto* hdr = reinterpret_cast<io_uring_recvmsg_out*>(mem);
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(peer_port);
    peer.sin_addr.s_addr = htonl(peer_ip);
    const size_t namelen = sizeof(sockaddr_in);
    hdr->namelen = static_cast<uint32_t>(namelen);
    hdr->controllen = 0;
    hdr->payloadlen = static_cast<uint32_t>(data.size());
    hdr->flags = 0;
    std::memcpy(mem + sizeof(io_uring_recvmsg_out), &peer, namelen);
    std::memcpy(mem + sizeof(io_uring_recvmsg_out) + namelen, data.data(),
        data.size());

    const size_t total = sizeof(io_uring_recvmsg_out) + namelen + data.size();
    msghdr msg_template{};
    msg_template.msg_namelen = static_cast<socklen_t>(namelen);

    buf.pending_releases() = 1;
    buf.update(iou_res{static_cast<int>(total)}, iou_cqe_flags{},
        msg_template);

    CHECK(buf.payload_span().size() == data.size());
    CHECK(buf.payload_view() == data);
    CHECK(buf.msghdr_flags() == msg_flags{});
    CHECK(buf.result().bytes() == data.size());
    CHECK(buf.peer_addr().is_v4());
    CHECK(buf.peer_addr().port() == peer_port);
  }
}
#pragma endregion

#pragma region UpdateRecvmsgTruncated
TEST_CASE("UpdateRecvmsgTruncated", "[IouBufPool]") {
  // When the kernel sets MSG_TRUNC in out->flags, msg_flags() reflects it.
  if (true) {
    auto pool = iou_buf_pool::create();
    auto buf = pool->borrow_reader();
    REQUIRE(buf);

    const size_t namelen = sizeof(sockaddr_in);
    auto* mem = buf.active_span().data();
    auto* hdr = reinterpret_cast<io_uring_recvmsg_out*>(mem);
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    hdr->namelen = static_cast<uint32_t>(namelen);
    hdr->controllen = 0;
    hdr->payloadlen = 5;
    hdr->flags = MSG_TRUNC;
    std::memcpy(mem + sizeof(io_uring_recvmsg_out), &peer, namelen);

    constexpr std::string_view trunc{"trunc"};
    std::memcpy(mem + sizeof(io_uring_recvmsg_out) + namelen, trunc.data(),
        trunc.size());

    const size_t total = sizeof(io_uring_recvmsg_out) + namelen + trunc.size();
    msghdr msg_template{};
    msg_template.msg_namelen = static_cast<socklen_t>(namelen);

    buf.pending_releases() = 1;
    buf.update(iou_res{static_cast<int>(total)}, iou_cqe_flags{},
        msg_template);

    CHECK(bitmask::has(buf.msghdr_flags(), msg_flags::trunc));
  }
}
#pragma endregion

#pragma region SyntheticPrefilled
TEST_CASE("SyntheticPrefilled", "[IouBufPool]") {
  // `make_synthetic` produces a non-owning read buffer whose payload covers
  // the input span and whose active tail is empty.
  std::string data{"hello world"};
  iou_buffer::span_t span{reinterpret_cast<std::byte*>(data.data()),
      data.size()};
  auto buf = iou_buffer::make_synthetic(span);

  REQUIRE(buf);
  CHECK(buf.blockrw() == block_type::read);
  CHECK(buf.size() == data.size());
  CHECK(buf.payload_span().size() == data.size());
  CHECK(buf.payload_view() == data);
  CHECK(buf.active_span().size() == 0ULL);
  CHECK(buf.active_span().data() ==
        buf.payload_span().data() + buf.payload_span().size());
  CHECK(buf.result().bytes() == data.size());
}
#pragma endregion

#pragma region SyntheticDestructionHarmless
TEST_CASE("SyntheticDestructionHarmless", "[IouBufPool]") {
  // The buffer holds no real allocation, so going out of scope must not
  // touch the data the caller owns.
  std::string data{"keepalive"};
  iou_buffer::span_t span{reinterpret_cast<std::byte*>(data.data()),
      data.size()};
  if (true) {
    auto buf = iou_buffer::make_synthetic(span);
    CHECK(buf.payload_view() == data);
  }
  CHECK(data == "keepalive");
}
#pragma endregion

#pragma region SyntheticConsumeRead
TEST_CASE("SyntheticConsumeRead", "[IouBufPool]") {
  // A synthetic buffer behaves like a freshly-completed read: the consumer
  // can drain it via `consume_read`.
  std::string data{"abcdef"};
  iou_buffer::span_t span{reinterpret_cast<std::byte*>(data.data()),
      data.size()};
  auto buf = iou_buffer::make_synthetic(span);

  auto first = buf.consume_read(3);
  CHECK(first.size() == 3ULL);
  CHECK(std::string_view(reinterpret_cast<const char*>(first.data()),
            first.size()) == "abc");
  CHECK(buf.payload_view() == "def");

  auto rest = buf.consume_read(buf.payload_span().size());
  CHECK(rest.size() == 3ULL);
  CHECK(buf.payload_span().size() == 0ULL);
}
#pragma endregion

#pragma region SyntheticMove
TEST_CASE("SyntheticMove", "[IouBufPool]") {
  // Moving a synthetic buffer transfers the view; the source ends up empty.
  std::string data{"transferable"};
  iou_buffer::span_t span{reinterpret_cast<std::byte*>(data.data()),
      data.size()};
  auto src = iou_buffer::make_synthetic(span);
  auto dst = std::move(src);
  REQUIRE(dst);
  CHECK_FALSE(src);
  CHECK(dst.payload_view() == data);
  CHECK(src.payload_span().size() == 0ULL);
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
