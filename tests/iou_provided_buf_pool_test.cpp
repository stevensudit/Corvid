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

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::proto::iouring;
using namespace std::string_view_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region NoOpZeroSlab
TEST_CASE("NoOpZeroSlab", "[IouProvidedBufPool]") {
  // slab_size=0 produces a no-op pool with no allocation.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    auto pool =
        iou_provided_buf_pool::create(dispatcher, 0, block_size::kb004);
    CHECK_FALSE(*pool);
    CHECK(pool->buf_size() == 0ULL);
    CHECK(pool->buf_count() == 0ULL);
    CHECK(pool->slab_size() == 0ULL);
    CHECK(pool->buf_data(0) == nullptr);

    // reconstruct returns empty buffer.
    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (0U << 16U))};
    auto buf = pool->borrow(iou_res{4}, flags);
    CHECK_FALSE(buf);
  }
}
#pragma endregion

#pragma region ConstructValid
TEST_CASE("ConstructValid", "[IouProvidedBufPool]") {
  // Valid construction: sizes, buf_count, slab_size, and bgid are correct.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    // 2 MB / 4 KB = 512 buffers.
    constexpr size_t slab = 2ULL * 1024 * 1024;
    auto pool =
        iou_provided_buf_pool::create(dispatcher, slab, block_size::kb004, 3);
    CHECK(*pool);
    CHECK(pool->buf_size() == 4096ULL);
    CHECK(pool->buf_count() == 512ULL);
    CHECK(pool->slab_size() == slab);
    CHECK(pool->bgid() == 3);
    CHECK(pool->buf_data(0) != nullptr);
    CHECK(pool->buf_data(511) != nullptr);
    CHECK(pool->buf_data(512) == nullptr); // out of range
  }
}
#pragma endregion

#pragma region BufCountFromDivision
TEST_CASE("BufCountFromDivision", "[IouProvidedBufPool]") {
  // buf_count is derived as slab_size / buf_size.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    // 4 MB / 4 KB = 1024.
    auto pool = iou_provided_buf_pool::create(dispatcher, 4ULL * 1024 * 1024,
        block_size::kb004);
    CHECK(*pool);
    CHECK(pool->buf_count() == 1024ULL);
    CHECK(pool->slab_size() == (4ULL * 1024 * 1024));
  }
  if (true) {
    // 8 MB slab (4 hugepages) / 1 MB = 8 buffers (power of two).
    constexpr size_t slab = 4ULL * buffer_pool_base::hugepage_size;
    auto pool =
        iou_provided_buf_pool::create(dispatcher, slab, block_size::m01);
    CHECK(*pool);
    CHECK(pool->buf_count() == 8ULL);
    CHECK(pool->slab_size() == slab);

    iou_ring ring;
    CHECK(pool->register_with(ring));
  }
}
#pragma endregion

#pragma region BufDataOffsets
TEST_CASE("BufDataOffsets", "[IouProvidedBufPool]") {
  // buf_data(bid) returns pointers that are exactly buf_size apart.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    constexpr size_t slab = 2ULL * 1024 * 1024;
    auto pool =
        iou_provided_buf_pool::create(dispatcher, slab, block_size::kb004);
    REQUIRE(pool);
    const std::byte* base = pool->buf_data(0);
    REQUIRE(base != nullptr);
    for (size_t i = 1; i < pool->buf_count(); ++i) {
      CHECK(pool->buf_data(i) == (base + i * pool->buf_size()));
    }
  }
}
#pragma endregion

#pragma region RegisterWithRing
TEST_CASE("RegisterWithRing", "[IouProvidedBufPool]") {
  // register_with succeeds, and a second call on the same pool fails.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    auto pool = iou_provided_buf_pool::create(dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    REQUIRE(pool);
    iou_ring ring;
    REQUIRE(pool->register_with(ring));
    // Double registration must fail.
    CHECK_FALSE(pool->register_with(ring));
  }
}
#pragma endregion

#pragma region ReconstructBeforeRegister
TEST_CASE("ReconstructBeforeRegister", "[IouProvidedBufPool]") {
  // reconstruct before register_with returns an empty buffer.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    auto pool = iou_provided_buf_pool::create(dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    REQUIRE(pool);
    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (1U << 16U))};
    auto buf = pool->borrow(iou_res{8}, flags);
    CHECK_FALSE(buf);
  }
}
#pragma endregion

#pragma region ReconstructPayload
TEST_CASE("ReconstructPayload", "[IouProvidedBufPool]") {
  // After register_with, reconstruct creates a read buffer with the correct
  // payload span.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    auto pool = iou_provided_buf_pool::create(dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    REQUIRE(pool);
    iou_ring ring;
    REQUIRE(pool->register_with(ring));

    // Simulate the kernel writing "hello world" into slot 2.
    const size_t bid = 2;
    auto* slot = reinterpret_cast<char*>(pool->buf_data(bid));
    REQUIRE(slot != nullptr);
    const auto expected("hello world"sv);
    std::memcpy(slot, expected.data(), expected.size());

    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (bid << 16U))};
    auto buf = pool->borrow(iou_res{11}, flags);
    REQUIRE(buf);
    CHECK(buf.blockrw() == iou_buffer::block_type::read);
    CHECK(buf.size() == pool->buf_size());
    CHECK(buf.payload_span().size() == 11ULL);
    CHECK(buf.payload_view() == expected);
    CHECK(buf.buf_index() == bid);
    CHECK(buf.result().ok());
    // Provided buffers are one-shot, so `active_span` is empty.
    CHECK(buf.was_provided());
    CHECK(buf.active_span().size() == 0ULL);
    // Provided buffers do not use ZC send tracking.
    CHECK(buf.pending_releases() == 0ULL);
  }
}
#pragma endregion

#pragma region ReconstructErrorResult
TEST_CASE("ReconstructErrorResult", "[IouProvidedBufPool]") {
  // A CQE with buffer flag set but an error result yields an empty payload.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    auto pool = iou_provided_buf_pool::create(dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    REQUIRE(pool);
    iou_ring ring;
    REQUIRE(pool->register_with(ring));

    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (0U << 16U))};
    auto buf = pool->borrow(iou_res{-ECONNRESET}, flags);
    REQUIRE(buf); // buffer is valid (slot was consumed)
    CHECK_FALSE(buf.result().ok());
    CHECK(buf.payload_span().size() == 0ULL); // no data
  }
}
#pragma endregion

#pragma region ReconstructNoBufferFlag
TEST_CASE("ReconstructNoBufferFlag", "[IouProvidedBufPool]") {
  // A CQE without the buffer flag returns a synthetic stub: no payload, but
  // the CQE `res` is preserved so callers can distinguish EOF (`res=0`)
  // from cancel, hard errors, etc.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    auto pool = iou_provided_buf_pool::create(dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    REQUIRE(pool);
    iou_ring ring;
    REQUIRE(pool->register_with(ring));

    // No IORING_CQE_F_BUFFER flag set.
    auto buf = pool->borrow(iou_res{8}, iou_cqe_flags{});
    REQUIRE(buf);
    CHECK(buf.result().value() == 8);
    CHECK(buf.payload_view().empty());
  }
}
#pragma endregion

#pragma region ReconstructOutOfRangeBid
TEST_CASE("ReconstructOutOfRangeBid", "[IouProvidedBufPool]") {
  // A buffer ID >= buf_count returns an empty buffer.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    auto pool = iou_provided_buf_pool::create(dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    REQUIRE(pool);
    iou_ring ring;
    REQUIRE(pool->register_with(ring));

    // Encode bid=999 (well past buf_count=512).
    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (999U << 16U))};
    auto buf = pool->borrow(iou_res{8}, flags);
    CHECK_FALSE(buf);
  }
}
#pragma endregion

#pragma region ReturnReplenishes
TEST_CASE("ReturnReplenishes", "[IouProvidedBufPool]") {
  // Destroying the reconstructed buffer returns the slot to the ring.
  // We verify indirectly: reconstruct the same slot twice (once after the
  // first buffer is destroyed) to confirm the slot was replenished.
  iou_provided_buf_pool::dispatcher_t dispatcher;
  if (true) {
    auto pool = iou_provided_buf_pool::create(dispatcher, 2ULL * 1024 * 1024,
        block_size::kb004, 0);
    REQUIRE(pool);
    iou_ring ring;
    REQUIRE(pool->register_with(ring));

    const size_t bid = 1;
    const auto flags = iou_cqe_flags{
        static_cast<uint32_t>(IORING_CQE_F_BUFFER | (bid << 16U))};

    // First reconstruction; the slot is live until `buf` is destroyed.
    {
      auto buf = pool->borrow(iou_res{5}, flags);
      REQUIRE(buf);
    }
    // Slot was returned to the ring; reconstruct again (no crash or assert).
    auto buf2 = pool->borrow(iou_res{3}, flags);
    REQUIRE(buf2);
    CHECK(buf2.payload_span().size() == 3ULL);
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
