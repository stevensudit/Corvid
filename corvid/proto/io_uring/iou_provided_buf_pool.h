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
#pragma once
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <sys/mman.h>

#include "../../concurrency/owner_thread_dispatcher.h"

#include "iou_buffer_pool_base.h"
#include "iou_buffer.h"

namespace corvid { inline namespace proto { namespace iouring {

// Pool of kernel-managed buffers for `io_uring` Provided Buffers (PBUF_RING).
//
// A hugepage-aligned slab of `slab_size` bytes is split into fixed-size
// `buf_size` slots. The ring is registered via `register_with`; callers submit
// SQEs with `IOSQE_BUFFER_SELECT` and `bgid()`, then reconstruct an
// `iou_buffer` from the resulting CQE.
//
// Unlike `iou_buf_pool`, there is no borrow/return API: the kernel selects
// and fills a slot, and the caller reconstructs an `iou_buffer` view over it.
// Destroying or resetting the buffer replenishes the slot back into the ring.
//
// Passing `slab_size = 0` produces a no-op pool: no memory is allocated,
// `operator bool()` returns false, and all operations fail gently.
//
// `iou_provided_buf_pool` is non-copyable and non-movable. The pool must
// outlive all `iou_buffer` objects it produces.
class iou_provided_buf_pool: public buffer_pool_base {
public:
#pragma region Construction

  // Construct a pool backed by a slab of `slab_size` bytes (must be a
  // multiple of `hugepage_size`), split into slots of `buf_size` bytes each.
  // `buf_count` is derived as `slab_size / buf_size` and must be a power of
  // two. Pass `slab_size = 0` for a no-op pool. Throws `std::system_error`
  // on allocation failure.
  iou_provided_buf_pool(owner_thread_dispatcher<>* dispatcher,
      size_t slab_size, block_size buf_size, uint16_t bgid = 0)
      : dispatcher_{dispatcher}, bgid_{bgid} {
    if (slab_size == 0) return;

    assert(slab_size % hugepage_size == 0);
    slab_size_ = slab_size;
    buf_size_ = *buf_size;
    buf_count_ = slab_size_ / buf_size_;
    if (buf_count_ == 0) return;
    assert(std::has_single_bit(buf_count_));

    // Try a hugepage-backed mapping first; fall back to anonymous with
    // hugepage advice. Over-allocate by one `hugepage_size` to guarantee
    // alignment.
    base_ = reinterpret_cast<std::byte*>(::mmap(nullptr, slab_size_,
        *mmap_prot::read | *mmap_prot::write,
        *(mmap_mask::map_private | mmap_mask::anonymous | mmap_mask::hugetlb |
            mmap_mask::populate),
        -1, 0));
    if (base_ == MAP_FAILED) {
      const size_t reserve = slab_size_ + hugepage_size;
      auto* raw_ptr = reinterpret_cast<std::byte*>(::mmap(nullptr, reserve,
          *mmap_prot::read | *mmap_prot::write,
          *(mmap_mask::map_private | mmap_mask::anonymous), -1, 0));
      if (raw_ptr == MAP_FAILED)
        throw std::system_error(errno, std::system_category(), "mmap");
      const size_t prefix =
          (hugepage_size -
              (reinterpret_cast<uintptr_t>(raw_ptr) % hugepage_size)) %
          hugepage_size;
      base_ = raw_ptr + prefix;
      if (prefix > 0) ::munmap(raw_ptr, prefix);
      const size_t suffix = reserve - prefix - slab_size_;
      if (suffix > 0) ::munmap(base_ + slab_size_, suffix);
      ::madvise(base_, slab_size_, MADV_HUGEPAGE);
    }
    std::memset(base_, 0, slab_size_);
  }

  ~iou_provided_buf_pool() override {
    if (base_) ::munmap(base_, slab_size_);
  }

  iou_provided_buf_pool(const iou_provided_buf_pool&) = delete;
  iou_provided_buf_pool& operator=(const iou_provided_buf_pool&) = delete;
  iou_provided_buf_pool(iou_provided_buf_pool&&) = delete;
  iou_provided_buf_pool& operator=(iou_provided_buf_pool&&) = delete;

#pragma endregion
#pragma region Accessors

  // Whether the pool is active (non-zero sizes, memory allocated).
  [[nodiscard]] explicit operator bool() const noexcept { return base_; }
  [[nodiscard]] bool operator!() const noexcept { return !base_; }

  // Buffer group ID; use as the `bgid` for `IOSQE_BUFFER_SELECT` SQEs.
  [[nodiscard]] uint16_t bgid() const noexcept { return bgid_; }

  // Slab size in bytes (a multiple of `hugepage_size`).
  [[nodiscard]] size_t slab_size() const noexcept { return slab_size_; }

  // Per-buffer size in bytes.
  [[nodiscard]] size_t buf_size() const noexcept { return buf_size_; }

  // Number of buffer slots (`slab_size / buf_size`).
  [[nodiscard]] size_t buf_count() const noexcept { return buf_count_; }

  // Pointer to the start of buffer slot `bid`. Returns null if the pool is
  // unconfigured or `bid` is out of range.
  [[nodiscard]] std::byte* buf_data(size_t bid) const noexcept {
    if (!base_ || bid >= buf_count_) return nullptr;
    return base_ + (bid * buf_size_);
  }

#pragma endregion
#pragma region Registration

  // Register the buffer ring with `ring` and enqueue all buffer slots.
  // Must be called once before any SQE with `IOSQE_BUFFER_SELECT` is
  // submitted.
  [[nodiscard]] bool register_with(iou_ring& ring) noexcept {
    if (!base_) return true;

    // Associate buffer ring with I/O ring.
    buf_ring_ = iou_buf_ring{ring, buf_count_, bgid_};
    if (!buf_ring_) return false;

    // Enqueue all buffer slots into the kernel ring. The kernel will select
    // and fill them on `IOSQE_BUFFER_SELECT` SQEs, and replenish them when we
    // return them in `return_buffer`.
    const int mask = static_cast<int>(buf_count_) - 1;
    for (size_t i = 0; i < buf_count_; ++i) {
      buf_ring_.add(base_ + (i * buf_size_), static_cast<unsigned>(buf_size_),
          static_cast<unsigned short>(i), mask, static_cast<int>(i));
    }
    buf_ring_.advance(static_cast<int>(buf_count_));

    return true;
  }

  // Borrow an `iou_buffer` from a multishot recv CQE.
  //
  // For a TCP `recv` (without a `msghdr`), `payload_span` covers
  // the entire written region; `result().bytes()` equals the written byte
  // count.
  //
  // For a UDP `recvmsg` (with a `msghdr`), the buffer is parsed out, with
  // `payload_span` covering the payload, and `result().bytes()` likewise
  // reflecting the payload size. The `peer_addr` is filled in from the peer
  // address portion of the buffer, and `msghdr_flags` is set from the `msghdr`
  // flags field.
  //
  // Destroying or resetting the returned buffer replenishes the slot.
  [[nodiscard]] buffer borrow(iou_res res, iou_cqe_flags cqe_flags,
      msghdr* msgh = nullptr) noexcept {
    if (!base_ || !buf_ring_) return {};
    if (!bitmask::has(cqe_flags, iou_cqe_flags::buffer)) return {};
    const size_t bid = (*cqe_flags >> IORING_CQE_BUFFER_SHIFT) & 0xffffU;
    if (bid >= buf_count_) return {};
    span_t span{base_ + (bid * buf_size_), buf_size_};

    auto buf = make_buffer(*this, span, bid, block_type::read);

    buf.pending_releases() = 1;
    if (msgh)
      buf.update(res, cqe_flags, *msgh);
    else
      buf.update(res, cqe_flags);
    buf.pending_releases() = 0;

    return buf;
  }

#pragma endregion
#pragma region Overrides
private:
  [[nodiscard]] std::byte* base() const noexcept override { return base_; }

  // Replenish the returned slot into the kernel ring.
  [[nodiscard]] bool return_buffer(span_t s, block_type /*blockrw*/) override {
    if (!s.data()) return false;
    assert(buf_ring_ && buf_size_ > 0);
    const size_t bid = (s.data() - base_) / buf_size_;
    assert(bid < buf_count_);
    const int mask = static_cast<int>(buf_count_) - 1;

    (void)dispatcher_->execute_or_post([this, s, bid, mask] {
      buf_ring_.add(s.data(), buf_size_, static_cast<unsigned short>(bid),
          mask, 0);
      buf_ring_.advance(1);
      return true;
    });
    return true;
  }

  [[nodiscard]] bool decrement_read_bytes(size_t) override {
    throw std::logic_error(
        "Provided buffer pool does not track in-flight read bytes");
  }
  [[nodiscard]] bool increment_read_bytes(size_t) override {
    throw std::logic_error(
        "Provided buffer pool does not track in-flight read bytes");
  }

#pragma endregion
#pragma region Data members
private:
  owner_thread_dispatcher<>* dispatcher_;
  std::byte* base_{};
  size_t buf_size_{};
  size_t buf_count_{};
  size_t slab_size_{};
  uint16_t bgid_{};
  iou_buf_ring buf_ring_;

#pragma endregion
};

}}} // namespace corvid::proto::iouring
