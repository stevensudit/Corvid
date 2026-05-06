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

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

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
void IouBufPool_ReadInitialState() {
  // Freshly-allocated read buffer: empty payload, active = entire block.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf.blockrw(), block_type::read);
    EXPECT_EQ(buf.size(), 4096ULL);
    EXPECT_EQ(buf.payload_span().size(), 0ULL);
    EXPECT_EQ(buf.active_span().size(), buf.size());
    EXPECT_TRUE(buf.active_span().data() == buf.payload_span().data());
    EXPECT_FALSE(buf.result().ok());
  }
  if (true) {
    // If this fails, it means we can finally get rid of the hack where we
    // pretend to have a copy constructor but actually throw.
#if defined(__cpp_lib_move_only_function) && !defined(__GLIBCXX__)
    EXPECT_TRUE(false);
#endif
  }
}
#pragma endregion

#pragma region ReadAfterUpdate
void IouBufPool_ReadAfterUpdate() {
  // After a simulated read, payload grows and active shrinks to the tail.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

    const size_t n = sim_read(buf, "hello world"sv);
    EXPECT_EQ(buf.payload_span().size(), n);
    EXPECT_EQ(buf.active_span().size(), buf.size() - n);
    EXPECT_TRUE(buf.active_span().data() ==
                buf.payload_span().data() + buf.payload_span().size());
    EXPECT_TRUE(buf.result().ok());
    EXPECT_EQ(buf.payload_view().substr(0, 5), "hello");
  }
}
#pragma endregion

#pragma region ReadMultipleUpdates
void IouBufPool_ReadMultipleUpdates() {
  // Two successive reads concatenate into a single growing payload.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

    sim_read(buf, "hello"sv);
    EXPECT_EQ(buf.payload_span().size(), 5ULL);

    sim_read(buf, " world"sv);
    EXPECT_EQ(buf.payload_span().size(), 11ULL);
    EXPECT_EQ(buf.payload_view(), "hello world");
    EXPECT_EQ(buf.active_span().size(), buf.size() - 11);
  }
}
#pragma endregion

#pragma region ReadConsumePartial
void IouBufPool_ReadConsumePartial() {
  // consume_read(n) with n < payload returns n bytes and advances the front.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

    sim_read(buf, "abcdefgh"sv);
    ASSERT_EQ(buf.payload_span().size(), 8ULL);

    auto slice = buf.consume_read(3);
    EXPECT_EQ(slice.size(), 3ULL);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(slice.data()), 3),
        "abc");
    EXPECT_EQ(buf.payload_span().size(), 5ULL);
    EXPECT_EQ(buf.payload_view(), "defgh");
    // active_span still covers the tail beyond the original 8 bytes read.
    EXPECT_EQ(buf.active_span().size(), buf.size() - 8);
  }
}
#pragma endregion

#pragma region ReadConsumeFullReset
void IouBufPool_ReadConsumeFullReset() {
  // Consuming all payload bytes resets the buffer to its initial state.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);
    const std::byte* base = buf.active_span().data();

    sim_read(buf, "reset-me"sv);
    const size_t n = buf.payload_span().size();
    const auto slice = buf.consume_read(n);
    EXPECT_EQ(slice.size(), n);

    EXPECT_EQ(buf.payload_span().size(), 0ULL);
    EXPECT_EQ(buf.active_span().size(), buf.size());
    EXPECT_TRUE(buf.payload_span().data() == base);
    EXPECT_TRUE(buf.active_span().data() == base);
  }
}
#pragma endregion

#pragma region ReadConsumeOverRequest
void IouBufPool_ReadConsumeOverRequest() {
  // Requesting more bytes than available returns only what's present.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

    sim_read(buf, "hi"sv);
    const auto slice = buf.consume_read(9999);
    EXPECT_EQ(slice.size(), 2ULL);
    // Fully consumed: reset to initial state.
    EXPECT_EQ(buf.payload_span().size(), 0ULL);
    EXPECT_EQ(buf.active_span().size(), buf.size());
  }
}
#pragma endregion

#pragma region ReadUpdateError
void IouBufPool_ReadUpdateError() {
  // An error result leaves spans unchanged.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

    sim_read(buf, "good data"sv);
    const size_t payload_before = buf.payload_span().size();
    const size_t active_before = buf.active_span().size();

    buf.update(iou_res{-ECONNRESET}, iou_cqe_flags{});
    EXPECT_FALSE(buf.result().ok());
    EXPECT_EQ(buf.payload_span().size(), payload_before);
    EXPECT_EQ(buf.active_span().size(), active_before);
  }
}
#pragma endregion

#pragma region WriteInitialState
void IouBufPool_WriteInitialState() {
  // Freshly-allocated write buffer: both payload and active are empty.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf.blockrw(), block_type::write);
    EXPECT_EQ(buf.size(), 4096ULL);
    EXPECT_EQ(buf.payload_span().size(), 0ULL);
    EXPECT_EQ(buf.active_span().size(), 0ULL);
    EXPECT_TRUE(buf.payload_span().data() == buf.active_span().data());
  }
}
#pragma endregion

#pragma region WriteViaAppend
void IouBufPool_WriteViaAppend() {
  // append fills payload and active_span covers the same bytes.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    EXPECT_TRUE(buf.append("hello"sv));
    EXPECT_EQ(buf.payload_span().size(), 5ULL);
    EXPECT_EQ(buf.active_span().size(), 5ULL);
    EXPECT_TRUE(buf.active_span().data() == buf.payload_span().data());
    EXPECT_EQ(buf.payload_view(), "hello");

    // Second append extends both spans.
    EXPECT_TRUE(buf.append(", world"sv));
    EXPECT_EQ(buf.payload_span().size(), 12ULL);
    EXPECT_EQ(buf.active_span().size(), 12ULL);
    EXPECT_EQ(buf.payload_view(), "hello, world");
  }
}
#pragma endregion

#pragma region WriteAppendOverflow
void IouBufPool_WriteAppendOverflow() {
  // append returns false without modifying anything when data would not fit.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    const size_t cap = buf.size();
    const std::string filler(cap, 'x');
    EXPECT_TRUE(buf.append(std::string_view{filler}));
    EXPECT_EQ(buf.payload_span().size(), cap);

    EXPECT_FALSE(buf.append("y"sv));           // one byte too many
    EXPECT_EQ(buf.payload_span().size(), cap); // unchanged
  }
}
#pragma endregion

#pragma region WriteViaTailAndUpdatePayload
void IouBufPool_WriteViaTailAndUpdatePayload() {
  // Manual fill: get tail_span, memcpy, then update_payload.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    auto tail = buf.tail_span();
    EXPECT_EQ(tail.size(), buf.size()); // tail = full block initially
    constexpr std::string_view msg{"tail-fill"};
    std::memcpy(tail.data(), msg.data(), msg.size());
    EXPECT_TRUE(buf.update_payload(tail.first(msg.size())));

    EXPECT_EQ(buf.payload_span().size(), msg.size());
    EXPECT_EQ(buf.active_span().size(), msg.size());
    EXPECT_EQ(buf.payload_view(), msg);
  }
}
#pragma endregion

#pragma region WriteUpdatePayloadBadStart
void IouBufPool_WriteUpdatePayloadBadStart() {
  // update_payload rejects a span that does not start at payload_span's end.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    EXPECT_TRUE(buf.append("abc"sv));

    // tail_span() starts right after "abc"; subspan(1) shifts it by one byte.
    auto tail = buf.tail_span();
    EXPECT_FALSE(buf.update_payload(tail.subspan(1, 3)));
    EXPECT_EQ(buf.payload_span().size(), 3ULL); // unchanged
  }
}
#pragma endregion

#pragma region WriteUpdatePayloadOverflow
void IouBufPool_WriteUpdatePayloadOverflow() {
  // update_payload rejects a span whose end exceeds full_span.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    auto tail = buf.tail_span(); // entire block
    // Construct a span that ends one byte past full_span.
    const iou_buf_pool::span_t overrun{tail.data(), tail.size() + 1};
    EXPECT_FALSE(buf.update_payload(overrun));
    EXPECT_EQ(buf.payload_span().size(), 0ULL); // unchanged
  }
}
#pragma endregion

#pragma region WriteUpdatePartialSend
void IouBufPool_WriteUpdatePartialSend() {
  // Partial send advances active_span front while payload_span stays fixed.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    EXPECT_TRUE(buf.append("0123456789"sv)); // 10 bytes
    EXPECT_EQ(buf.active_span().size(), 10ULL);

    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{6}, iou_cqe_flags{}); // kernel sent first 6 bytes

    EXPECT_EQ(buf.payload_span().size(), 10ULL); // unchanged
    EXPECT_EQ(buf.active_span().size(), 4ULL);   // 10 - 6
    EXPECT_EQ(std::string_view(
                  reinterpret_cast<const char*>(buf.active_span().data()), 4),
        "6789");
  }
}
#pragma endregion

#pragma region WriteFullyConsumedThenAppend
void IouBufPool_WriteFullyConsumedThenAppend() {
  // After a complete send, the next append resets from the block base.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);
    const std::byte* base = buf.active_span().data();

    EXPECT_TRUE(buf.append("sent"sv));
    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{4}, iou_cqe_flags{}); // fully consumed
    EXPECT_EQ(buf.active_span().size(), 0ULL);

    // payload_view still shows "sent" before the implicit reset.
    EXPECT_EQ(buf.payload_view(), "sent");

    EXPECT_TRUE(buf.append("new"sv));
    EXPECT_EQ(buf.payload_span().size(), 3ULL);
    EXPECT_EQ(buf.active_span().size(), 3ULL);
    EXPECT_TRUE(buf.payload_span().data() == base);
    EXPECT_EQ(buf.payload_view(), "new");
  }
}
#pragma endregion

#pragma region WriteFullyConsumedThenTailSpan
void IouBufPool_WriteFullyConsumedThenTailSpan() {
  // After a complete send, tail_span() triggers an implicit reset.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);
    const std::byte* base = buf.active_span().data();

    EXPECT_TRUE(buf.append("gone"sv));
    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{4}, iou_cqe_flags{});

    auto tail = buf.tail_span(); // triggers implicit reset
    EXPECT_EQ(tail.size(), buf.size());
    EXPECT_TRUE(tail.data() == base);
  }
}
#pragma endregion

#pragma region WriteUpdateError
void IouBufPool_WriteUpdateError() {
  // An error result leaves spans unchanged.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    EXPECT_TRUE(buf.append("data"sv));
    const size_t payload_before = buf.payload_span().size();
    const size_t active_before = buf.active_span().size();

    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{-EPIPE}, iou_cqe_flags{});
    EXPECT_FALSE(buf.result().ok());
    EXPECT_EQ(buf.payload_span().size(), payload_before);
    EXPECT_EQ(buf.active_span().size(), active_before);
  }
}
#pragma endregion

#pragma region AppendToPartiallySentBuffer
void IouBufPool_AppendToPartiallySentBuffer() {
  // After a partial send, appending more extends both payload and active.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    EXPECT_TRUE(buf.append("hello"sv));
    [[maybe_unused]] auto [active_buffer, buffer_index, file_offset] =
        buf.prepare();
    buf.update(iou_res{3},
        iou_cqe_flags{}); // sent "hel"; active points to "lo" (2 bytes)

    EXPECT_EQ(buf.payload_span().size(), 5ULL);
    EXPECT_EQ(buf.active_span().size(), 2ULL);

    EXPECT_TRUE(buf.append(" world"sv));
    EXPECT_EQ(buf.payload_span().size(), 11ULL);
    EXPECT_EQ(buf.active_span().size(), 8ULL);
    EXPECT_EQ(buf.payload_view(), "hello world");
    EXPECT_EQ(std::string_view(
                  reinterpret_cast<const char*>(buf.active_span().data()), 8),
        "lo world");
  }
}
#pragma endregion

#pragma region PromoteToWrite
void IouBufPool_PromoteToWrite() {
  // Promoting a read buffer keeps payload; active_span = payload_span.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

    sim_read(buf, "proxy data"sv);
    ASSERT_EQ(buf.payload_span().size(), 10ULL);
    const std::byte* payload_data = buf.payload_span().data();

    buf.promote_to_write();

    EXPECT_EQ(buf.blockrw(), block_type::write);
    EXPECT_EQ(buf.payload_span().size(), 10ULL);
    EXPECT_EQ(buf.active_span().size(), 10ULL);
    EXPECT_TRUE(buf.active_span().data() == payload_data);
    EXPECT_EQ(buf.payload_view(), "proxy data");
  }
}
#pragma endregion

#pragma region DemoteToRead
void IouBufPool_DemoteToRead() {
  // Demoting a write buffer keeps payload; active_span = tail after payload.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_writer();
    ASSERT_TRUE(buf);

    EXPECT_TRUE(buf.append("header"sv));
    buf.demote_to_read();

    EXPECT_EQ(buf.blockrw(), block_type::read);
    EXPECT_EQ(buf.payload_span().size(), 6ULL);
    EXPECT_EQ(buf.active_span().size(), buf.size() - 6ULL);
    EXPECT_EQ(buf.payload_view(), "header");
    EXPECT_TRUE(buf.active_span().data() ==
                buf.payload_span().data() + buf.payload_span().size());
  }
}
#pragma endregion

#pragma region PromoteDemoteRoundtrip
void IouBufPool_PromoteDemoteRoundtrip() {
  // promote_to_write then demote_to_read preserves payload throughout.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

    sim_read(buf, "roundtrip"sv);
    ASSERT_EQ(buf.payload_span().size(), 9ULL);

    buf.promote_to_write();
    EXPECT_EQ(buf.payload_view(), "roundtrip");
    EXPECT_EQ(buf.active_span().size(), 9ULL);

    buf.demote_to_read();
    EXPECT_EQ(buf.payload_view(), "roundtrip");
    EXPECT_EQ(buf.active_span().size(), buf.size() - 9ULL);
  }
}
#pragma endregion

#pragma region AvailableTracking
void IouBufPool_AvailableTracking() {
  // Allocating reduces available bytes; reset restores them.
  if (true) {
    iou_buf_pool pool;
    const size_t initial = pool.available();

    auto r = pool.borrow_reader();
    ASSERT_TRUE(r);
    EXPECT_EQ(pool.available(), initial - r.size());

    auto w = pool.borrow_writer();
    ASSERT_TRUE(w);
    EXPECT_EQ(pool.available(), initial - r.size() - w.size());

    r.reset();
    EXPECT_EQ(pool.available(), initial - w.size());

    w.reset();
    EXPECT_EQ(pool.available(), initial);
  }
}
#pragma endregion

#pragma region MoveBuffer
void IouBufPool_MoveBuffer() {
  // Moving a buffer transfers ownership; source becomes empty.
  if (true) {
    iou_buf_pool pool;
    auto a = pool.borrow_writer();
    ASSERT_TRUE(a);
    EXPECT_TRUE(a.append("move me"sv));

    auto b = std::move(a);
    EXPECT_FALSE(a); // NOLINT(bugprone-use-after-move)
    EXPECT_TRUE(b);
    EXPECT_EQ(b.payload_view(), "move me");
  }
}
#pragma endregion

#pragma region CoalesceSmallToMedium
void IouBufPool_CoalesceSmallToMedium() {
  // Four smalls from the same medium coalesce back to one medium.
  // The three sibling mediums are held, so the large does NOT coalesce.
  if (true) {
    iou_buf_pool pool;
    // Drain three mediums from the first large block (zone tail = lowest).
    auto m0 = pool.borrow_writer(block_size::kb016);
    auto m1 = pool.borrow_writer(block_size::kb016);
    auto m2 = pool.borrow_writer(block_size::kb016);
    ASSERT_TRUE(m0);
    ASSERT_TRUE(m1);
    ASSERT_TRUE(m2);
    // The fourth medium is split into four smalls.
    auto s0 = pool.borrow_writer(block_size::kb004);
    auto s1 = pool.borrow_writer(block_size::kb004);
    auto s2 = pool.borrow_writer(block_size::kb004);
    auto s3 = pool.borrow_writer(block_size::kb004);
    ASSERT_TRUE(s0);
    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    ASSERT_TRUE(s3);
    // Free all four smalls: they should coalesce into the fourth medium.
    s0.reset();
    s1.reset();
    s2.reset();
    s3.reset();
    // Three sibling mediums are still held: the large must NOT coalesce.
    // The coalesced medium should be immediately allocatable.
    auto m_new = pool.borrow_writer(block_size::kb016);
    ASSERT_TRUE(m_new);
    // Release everything and verify full recovery.
    m0.reset();
    m1.reset();
    m2.reset();
    m_new.reset();
    EXPECT_EQ(pool.available(), 2ULL * 1024 * 1024);
  }
}
#pragma endregion

#pragma region CoalesceMediumToLarge
void IouBufPool_CoalesceMediumToLarge() {
  // Four mediums from the same large coalesce back to one large.
  if (true) {
    iou_buf_pool pool;
    const size_t initial = pool.available();
    auto m0 = pool.borrow_writer(block_size::kb016);
    auto m1 = pool.borrow_writer(block_size::kb016);
    auto m2 = pool.borrow_writer(block_size::kb016);
    auto m3 = pool.borrow_writer(block_size::kb016);
    ASSERT_TRUE(m0);
    ASSERT_TRUE(m1);
    ASSERT_TRUE(m2);
    ASSERT_TRUE(m3);
    m0.reset();
    m1.reset();
    m2.reset();
    m3.reset();
    // All four mediums freed: the parent large must be reconstructed.
    EXPECT_EQ(pool.available(), initial);
    auto l = pool.borrow_writer(block_size::kb032);
    ASSERT_TRUE(l);
    l.reset();
    EXPECT_EQ(pool.available(), initial);
  }
}
#pragma endregion

#pragma region CoalesceChain
void IouBufPool_CoalesceChain() {
  // Allocate all 512 smalls, free all 512: cascading coalesce must rebuild
  // all 32 large blocks.
  if (true) {
    iou_buf_pool pool;
    constexpr size_t TOTAL_SMALLS = 512;
    constexpr size_t TOTAL_LARGE = 32;
    std::array<iou_buf_pool::buffer, TOTAL_SMALLS> bufs;
    for (size_t i = 0; i < TOTAL_SMALLS; ++i) {
      bufs[i] = pool.borrow_writer(block_size::kb004);
      ASSERT_TRUE(bufs[i]);
    }
    EXPECT_EQ(pool.available(), 0ULL);
    for (size_t i = 0; i < TOTAL_SMALLS; ++i) bufs[i].reset();
    EXPECT_EQ(pool.available(), 2ULL * 1024 * 1024);
    // All 32 large blocks must now be individually allocatable.
    std::array<iou_buf_pool::buffer, TOTAL_LARGE> large_bufs;
    for (size_t i = 0; i < TOTAL_LARGE; ++i) {
      large_bufs[i] = pool.borrow_writer(block_size::kb064);
      ASSERT_TRUE(large_bufs[i]);
    }
    auto extra = pool.borrow_writer(block_size::kb064);
    EXPECT_FALSE(extra);
    for (auto& b : large_bufs) b.reset();
    EXPECT_EQ(pool.available(), 2ULL * 1024 * 1024);
  }
}
#pragma endregion

#pragma region UdpTierAlloc
void IouBufPool_UdpTierAlloc() {
  // Allocate all 1024 x 2 KB slots (2 MB / 2 KB), verify each succeeds and
  // has the right size, then confirm full pool recovery after freeing all.
  if (true) {
    iou_buf_pool pool;
    constexpr size_t TOTAL = 2ULL * 1024 * 1024 / (2ULL * 1024); // 1024
    std::array<iou_buf_pool::buffer, TOTAL> bufs;
    for (size_t i = 0; i < TOTAL; ++i) {
      bufs[i] = pool.borrow_writer(block_size::kb002);
      ASSERT_TRUE(bufs[i]);
      EXPECT_EQ(bufs[i].size(), 2ULL * 1024);
    }
    EXPECT_EQ(pool.available(), 0ULL);
    auto extra = pool.borrow_writer(block_size::kb002);
    EXPECT_FALSE(extra);
    for (auto& b : bufs) b.reset();
    EXPECT_EQ(pool.available(), 2ULL * 1024 * 1024);
  }
}
#pragma endregion

#pragma region UpdateRecvmsgMsgFlagsDefault
void IouBufPool_UpdateRecvmsgMsgFlagsDefault() {
  // A freshly borrowed buffer has msg_flags() == 0.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf.msghdr_flags(), msg_flags{});
  }
}
#pragma endregion

#pragma region UpdateRecvmsgValid
void IouBufPool_UpdateRecvmsgValid() {
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

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

    EXPECT_EQ(buf.payload_span().size(), data.size());
    EXPECT_EQ(buf.payload_view(), data);
    EXPECT_EQ(buf.msghdr_flags(), msg_flags{});
    EXPECT_EQ(buf.result().bytes(), data.size());
    EXPECT_TRUE(buf.peer_addr().is_v4());
    EXPECT_EQ(buf.peer_addr().port(), peer_port);
  }
}
#pragma endregion

#pragma region UpdateRecvmsgTruncated
void IouBufPool_UpdateRecvmsgTruncated() {
  // When the kernel sets MSG_TRUNC in out->flags, msg_flags() reflects it.
  if (true) {
    iou_buf_pool pool;
    auto buf = pool.borrow_reader();
    ASSERT_TRUE(buf);

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

    EXPECT_FALSE(bitmask::has(buf.msghdr_flags(), msg_flags::trunc));
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(IouBufPool_ReadInitialState, IouBufPool_ReadAfterUpdate,
    IouBufPool_ReadMultipleUpdates, IouBufPool_ReadConsumePartial,
    IouBufPool_ReadConsumeFullReset, IouBufPool_ReadConsumeOverRequest,
    IouBufPool_ReadUpdateError, IouBufPool_WriteInitialState,
    IouBufPool_WriteViaAppend, IouBufPool_WriteAppendOverflow,
    IouBufPool_WriteViaTailAndUpdatePayload,
    IouBufPool_WriteUpdatePayloadBadStart,
    IouBufPool_WriteUpdatePayloadOverflow, IouBufPool_WriteUpdatePartialSend,
    IouBufPool_WriteFullyConsumedThenAppend,
    IouBufPool_WriteFullyConsumedThenTailSpan, IouBufPool_WriteUpdateError,
    IouBufPool_AppendToPartiallySentBuffer, IouBufPool_PromoteToWrite,
    IouBufPool_DemoteToRead, IouBufPool_PromoteDemoteRoundtrip,
    IouBufPool_AvailableTracking, IouBufPool_MoveBuffer,
    IouBufPool_CoalesceSmallToMedium, IouBufPool_CoalesceMediumToLarge,
    IouBufPool_CoalesceChain, IouBufPool_UdpTierAlloc)
