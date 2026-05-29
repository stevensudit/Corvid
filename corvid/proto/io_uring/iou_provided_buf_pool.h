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
#include "../../infra/relaxed_atomic.h"

#include "iou_buffer_pool_base.h"
#include "iou_buffer.h"

namespace corvid { inline namespace proto { namespace iouring {

// Pool of kernel-managed buffers for `io_uring` Provided Buffers (PBUF_RING).
//
// A hugepage-aligned slab of `slab_size` bytes is split into fixed-size
// `buf_size` slots. The ring is registered via `register_with`; callers submit
// SQEs with `IOSQE_BUFFER_SELECT` and `bgid`, then reconstruct an `iou_buffer`
// from the resulting CQE.
//
// Unlike `iou_buf_pool`, there is no borrow/return API: the kernel selects and
// fills a slot, and the caller reconstructs an `iou_buffer` view over it.
// Destroying or resetting the buffer replenishes the slot back into the ring.
//
// Passing `slab_size = 0` produces a no-op pool: no memory is allocated,
// `operator bool` returns false, and all operations fail gently.
//
// `iou_provided_buf_pool` is non-copyable and non-movable. The pool must
// outlive all `iou_buffer` objects it produces.
class iou_provided_buf_pool: public buffer_pool_base {
  enum class allow : bool { ctor };

#pragma region Construction
public:
  using posted_fn = fixed_function<default_fixed_function::capacity, bool()>;
  using dispatcher_t = owner_thread_dispatcher<posted_fn>;

  // Public for `std::make_shared`; external callers must go through `create`.
  // Constructs a pool backed by a slab of `slab_size` bytes (must be a
  // multiple of `hugepage_size`), split into slots of `buf_size` bytes each.
  // `buf_count` is derived as `slab_size / buf_size` and must be a power of
  // two. Pass `slab_size = 0` for a no-op pool. Throws `std::system_error`
  // on allocation failure.
  iou_provided_buf_pool(allow, dispatcher_t& dispatcher, size_t slab_size,
      block_size buf_size, uint16_t bgid = 0)
      : dispatcher_{&dispatcher}, bgid_{bgid} {
    if (slab_size == 0) return;

    slab_size_ = slab_size;
    buf_size_ = *buf_size;
    buf_count_ = slab_size_ / buf_size_;

    if (buf_count_ == 0) return;
    if (!std::has_single_bit(buf_count_))
      throw std::invalid_argument("buf_count must be a power of two");
    if (slab_size % hugepage_size != 0)
      throw std::invalid_argument(
          "slab_size must be a multiple of hugepage_size");
    if (buf_count_ >
        static_cast<size_t>(std::numeric_limits<unsigned short>::max()) + 1ULL)
      throw std::invalid_argument(
          "buf_count exceeds io_uring 16-bit bid range");

    // Try a hugepage-backed mapping first; fall back to anonymous with
    // hugepage advice. Over-allocate by one `hugepage_size` to guarantee
    // alignment.
    base_ = reinterpret_cast<std::byte*>(::mmap(nullptr, slab_size_,
        *(mmap_prot::read | mmap_prot::write),
        *(mmap_mask::map_private | mmap_mask::anonymous | mmap_mask::hugetlb |
            mmap_mask::populate),
        -1, 0));
    if (base_ == MAP_FAILED) {
      const size_t reserve = slab_size_ + hugepage_size;
      auto* raw_ptr = reinterpret_cast<std::byte*>(::mmap(nullptr, reserve,
          *(mmap_prot::read | mmap_prot::write),
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
      (void)::madvise(base_, slab_size_, *mmap_advice::hugepage);
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

  // Construct a heap-allocated pool owned by `std::shared_ptr`.
  [[nodiscard]] static std::shared_ptr<iou_provided_buf_pool>
  create(dispatcher_t& dispatcher, size_t slab_size, block_size buf_size,
      uint16_t bgid = 0) {
    return std::make_shared<iou_provided_buf_pool>(allow::ctor, dispatcher,
        slab_size, buf_size, bgid);
  }

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

  // Number of slots currently free (enqueued in the kernel ring).
  [[nodiscard]] size_t free_block_count() const noexcept {
    return *free_count_;
  }

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
  //
  // Note that, when `slab_size` is 0, we silently pass.
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
    free_count_ = buf_count_;

    return true;
  }

  // Tell the embedded `iou_buf_ring` that its associated `iou_ring` is about
  // to be destroyed, so its destructor will skip the explicit unregister.
  void skip_unregister() noexcept { buf_ring_.skip_unregister(); }

  // Begin pool shutdown: subsequent `return_buffer` calls become no-ops
  // instead of trying to re-add the returned slot to the (now-disabled) ring.
  //
  // Called from `~iou_basic_loop`. The supported lifetime model is that the
  // user destroys the loop on the loop thread after stopping active workers,
  // so buffer destruction either happens before teardown (normal path) or
  // strictly after it (e.g., a `shared_ptr<iou_stream_conn>` held by user
  // code past `~iou_basic_loop`, dropped later on any thread). The
  // store-release here pairs with the load-acquire in `return_buffer` to
  // give that late call a clean `nullptr` to observe.
  //
  // Not a guard against concurrent buffer use during teardown: returning
  // buffers from another thread while `~iou_basic_loop` is running is
  // outside the supported model.
  void disarm() noexcept {
    dispatcher_.store(nullptr, std::memory_order::release);
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
  // Destroying or resetting the returned buffer replenishes the slot. (In the
  // case of an error, where no Provided Buffer is available, we return a
  // synthetic buffer with an error result; this has no slot to replenish but
  // allows the caller to handle the error without a separate code path.)
  //
  // The throw clang-tidy is worried about is impossible.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  [[nodiscard]] buffer borrow(iou_res res, iou_cqe_flags cqe_flags,
      msghdr* msgh = nullptr) noexcept {
    if (!base_ || !buf_ring_) return {};
    if (!bitmask::has(cqe_flags, iou_cqe_flags::buffer))
      return buffer::make_synthetic({}, {}, res);
    const size_t bid = get_buffer_id(cqe_flags);
    if (bid >= buf_count_) return {};
    span_t span{base_ + (bid * buf_size_), buf_size_};

    --free_count_;
    auto buf = make_buffer(shared_from_this(), span, bid, block_type::read);

    buf.pending_releases() = 1;
    if (msgh)
      buf.update(res, cqe_flags, *msgh);
    else
      buf.update(res, cqe_flags);
    buf.pending_releases() = 0;

    // A Provided Buffer has no `active_span`.
    (void)buf.set_read_size(0);

    return buf;
  }

#pragma endregion
#pragma region Overrides
private:
  [[nodiscard]] std::byte* base() const noexcept override { return base_; }

  // Replenish the returned slot into the kernel ring. After `disarm` (called
  // from `~iou_basic_loop`), the pool is shutting down and the ring must not
  // be touched; this becomes a no-op so a buffer outliving the loop can
  // destruct cleanly. See `disarm` for the lifetime model.
  [[nodiscard]] bool return_buffer(span_t s, block_type /*blockrw*/) override {
    if (!s.data()) return false;
    auto* d = dispatcher_.load(std::memory_order::acquire);
    if (!d) return false;
    assert(buf_ring_ && buf_size_ > 0);
    const size_t bid = (s.data() - base_) / buf_size_;
    assert(bid < buf_count_);
    const int mask = static_cast<int>(buf_count_) - 1;

    (void)d->execute_or_post_with_retry([this, s, bid, mask] {
      buf_ring_.add(s.data(), buf_size_, static_cast<unsigned short>(bid),
          mask, 0);
      buf_ring_.advance(1);
      ++free_count_;
      return true;
    });
    return true;
  }

#pragma endregion
#pragma region Data members
private:
  std::atomic<dispatcher_t*> dispatcher_;
  std::byte* base_{};
  size_t buf_size_{};
  size_t buf_count_{};
  size_t slab_size_{};
  uint16_t bgid_{};
  iou_buf_ring buf_ring_;
  relaxed_atomic_size_t free_count_;

#pragma endregion
};

}}} // namespace corvid::proto::iouring
