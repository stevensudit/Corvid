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
#include "../corvid/proto/io_uring/iou_provided_buf_pool.h"

#include <cstring>
#include <string_view>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;
using namespace corvid::proto::iouring;
using namespace std::string_view_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region NoOpZeroSlab
void IouProvidedBufPool_NoOpZeroSlab() {
  // slab_size=0 produces a no-op pool with no allocation.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    iou_provided_buf_pool pool(&dispatcher, 0, block_size::kb004);
    EXPECT_FALSE(pool);
    EXPECT_EQ(pool.buf_size(), 0ULL);
    EXPECT_EQ(pool.buf_count(), 0ULL);
    EXPECT_EQ(pool.slab_size(), 0ULL);
    EXPECT_EQ(pool.buf_data(0), nullptr);

    // reconstruct returns empty buffer.
    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (0U << 16U))};
    auto buf = pool.reconstruct(iou_res{4}, flags);
    EXPECT_FALSE(buf);
  }
}
#pragma endregion

#pragma region ConstructValid
void IouProvidedBufPool_ConstructValid() {
  // Valid construction: sizes, buf_count, slab_size, and bgid are correct.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    // 2 MB / 4 KB = 512 buffers.
    constexpr size_t slab = 2ULL * 1024 * 1024;
    iou_provided_buf_pool pool(&dispatcher, slab, block_size::kb004, 3);
    EXPECT_TRUE(pool);
    EXPECT_EQ(pool.buf_size(), 4096ULL);
    EXPECT_EQ(pool.buf_count(), 512ULL);
    EXPECT_EQ(pool.slab_size(), slab);
    EXPECT_EQ(pool.bgid(), 3);
    EXPECT_NE(pool.buf_data(0), nullptr);
    EXPECT_NE(pool.buf_data(511), nullptr);
    EXPECT_EQ(pool.buf_data(512), nullptr); // out of range
  }
}
#pragma endregion

#pragma region BufCountFromDivision
void IouProvidedBufPool_BufCountFromDivision() {
  // buf_count is derived as slab_size / buf_size.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    // 4 MB / 4 KB = 1024.
    iou_provided_buf_pool pool(&dispatcher, 4ULL * 1024 * 1024,
        block_size::kb004);
    EXPECT_TRUE(pool);
    EXPECT_EQ(pool.buf_count(), 1024ULL);
    EXPECT_EQ(pool.slab_size(), 4ULL * 1024 * 1024);
  }
  if (true) {
    // 8 MB slab (4 hugepages) / 1 MB = 8 buffers (power of two).
    constexpr size_t slab = 4ULL * buffer_pool_base::hugepage_size;
    iou_provided_buf_pool pool(&dispatcher, slab, block_size::m01);
    EXPECT_TRUE(pool);
    EXPECT_EQ(pool.buf_count(), 8ULL);
    EXPECT_EQ(pool.slab_size(), slab);

    iou_ring ring;
    EXPECT_TRUE(pool.register_with(ring));
  }
}
#pragma endregion

#pragma region BufDataOffsets
void IouProvidedBufPool_BufDataOffsets() {
  // buf_data(bid) returns pointers that are exactly buf_size apart.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    constexpr size_t slab = 2ULL * 1024 * 1024;
    iou_provided_buf_pool pool(&dispatcher, slab, block_size::kb004);
    ASSERT_TRUE(pool);
    const std::byte* base = pool.buf_data(0);
    ASSERT_NE(base, nullptr);
    for (size_t i = 1; i < pool.buf_count(); ++i) {
      EXPECT_EQ(pool.buf_data(i), base + i * pool.buf_size());
    }
  }
}
#pragma endregion

#pragma region RegisterWithRing
void IouProvidedBufPool_RegisterWithRing() {
  // register_with succeeds, and a second call on the same pool fails.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    iou_provided_buf_pool pool(&dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    ASSERT_TRUE(pool);
    iou_ring ring;
    ASSERT_TRUE(pool.register_with(ring));
    // Double registration must fail.
    EXPECT_FALSE(pool.register_with(ring));
  }
}
#pragma endregion

#pragma region ReconstructBeforeRegister
void IouProvidedBufPool_ReconstructBeforeRegister() {
  // reconstruct before register_with returns an empty buffer.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    iou_provided_buf_pool pool(&dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    ASSERT_TRUE(pool);
    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (1U << 16U))};
    auto buf = pool.reconstruct(iou_res{8}, flags);
    EXPECT_FALSE(buf);
  }
}
#pragma endregion

#pragma region ReconstructPayload
void IouProvidedBufPool_ReconstructPayload() {
  // After register_with, reconstruct creates a read buffer with the correct
  // payload span.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    iou_provided_buf_pool pool(&dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    ASSERT_TRUE(pool);
    iou_ring ring;
    ASSERT_TRUE(pool.register_with(ring));

    // Simulate the kernel writing "hello world" into slot 2.
    const size_t bid = 2;
    auto* slot = reinterpret_cast<char*>(pool.buf_data(bid));
    ASSERT_NE(slot, nullptr);
    const auto expected("hello world"sv);
    std::memcpy(slot, expected.data(), expected.size());

    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (bid << 16U))};
    auto buf = pool.reconstruct(iou_res{11}, flags);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf.blockrw(), iou_buffer::block_type::read);
    EXPECT_EQ(buf.size(), pool.buf_size());
    EXPECT_EQ(buf.payload_span().size(), 11ULL);
    EXPECT_EQ(buf.payload_view(), expected);
    EXPECT_EQ(buf.buf_index(), bid);
    EXPECT_TRUE(buf.result().ok());
    // active_span covers the remaining tail after the payload.
    EXPECT_EQ(buf.active_span().size(), pool.buf_size() - 11ULL);
    EXPECT_EQ(buf.active_span().data(),
        buf.payload_span().data() + buf.payload_span().size());
    // Provided buffers do not use ZC send tracking.
    EXPECT_EQ(buf.pending_releases(), 0ULL);
  }
}
#pragma endregion

#pragma region ReconstructErrorResult
void IouProvidedBufPool_ReconstructErrorResult() {
  // A CQE with buffer flag set but an error result yields an empty payload.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    iou_provided_buf_pool pool(&dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    ASSERT_TRUE(pool);
    iou_ring ring;
    ASSERT_TRUE(pool.register_with(ring));

    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (0U << 16U))};
    auto buf = pool.reconstruct(iou_res{-ECONNRESET}, flags);
    ASSERT_TRUE(buf); // buffer is valid (slot was consumed)
    EXPECT_FALSE(buf.result().ok());
    EXPECT_EQ(buf.payload_span().size(), 0ULL); // no data
  }
}
#pragma endregion

#pragma region ReconstructNoBufferFlag
void IouProvidedBufPool_ReconstructNoBufferFlag() {
  // A CQE without the buffer flag returns an empty buffer.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    iou_provided_buf_pool pool(&dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    ASSERT_TRUE(pool);
    iou_ring ring;
    ASSERT_TRUE(pool.register_with(ring));

    // No IORING_CQE_F_BUFFER flag set.
    auto buf = pool.reconstruct(iou_res{8}, iou_cqe_flags{});
    EXPECT_FALSE(buf);
  }
}
#pragma endregion

#pragma region ReconstructOutOfRangeBid
void IouProvidedBufPool_ReconstructOutOfRangeBid() {
  // A buffer ID >= buf_count returns an empty buffer.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    iou_provided_buf_pool pool(&dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    ASSERT_TRUE(pool);
    iou_ring ring;
    ASSERT_TRUE(pool.register_with(ring));

    // Encode bid=999 (well past buf_count=512).
    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (999U << 16U))};
    auto buf = pool.reconstruct(iou_res{8}, flags);
    EXPECT_FALSE(buf);
  }
}
#pragma endregion

#pragma region ReturnReplenishes
void IouProvidedBufPool_ReturnReplenishes() {
  // Destroying the reconstructed buffer returns the slot to the ring.
  // We verify indirectly: reconstruct the same slot twice (once after the
  // first buffer is destroyed) to confirm the slot was replenished.
  owner_thread_dispatcher<> dispatcher;
  if (true) {
    iou_provided_buf_pool pool(&dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    ASSERT_TRUE(pool);
    iou_ring ring;
    ASSERT_TRUE(pool.register_with(ring));

    const size_t bid = 1;
    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (bid << 16U))};

    // First reconstruction; the slot is live until `buf` is destroyed.
    {
      auto buf = pool.reconstruct(iou_res{5}, flags);
      ASSERT_TRUE(buf);
    }
    // Slot was returned to the ring; reconstruct again (no crash or assert).
    auto buf2 = pool.reconstruct(iou_res{3}, flags);
    ASSERT_TRUE(buf2);
    EXPECT_EQ(buf2.payload_span().size(), 3ULL);
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(IouProvidedBufPool_NoOpZeroSlab,
    IouProvidedBufPool_ConstructValid, IouProvidedBufPool_BufCountFromDivision,
    IouProvidedBufPool_BufDataOffsets, IouProvidedBufPool_RegisterWithRing,
    IouProvidedBufPool_ReconstructBeforeRegister,
    IouProvidedBufPool_ReconstructPayload,
    IouProvidedBufPool_ReconstructErrorResult,
    IouProvidedBufPool_ReconstructNoBufferFlag,
    IouProvidedBufPool_ReconstructOutOfRangeBid,
    IouProvidedBufPool_ReturnReplenishes)
