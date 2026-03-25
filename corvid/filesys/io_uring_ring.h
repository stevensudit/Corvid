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

// NOTICE: This is purely vibe-coded. It is not ready for production.

// Include glibc system headers before <linux/io_uring.h> to avoid conflicts
// between <linux/fs.h> (pulled in transitively) and glibc's <sys/stat.h>.
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstring>
#include <signal.h>

#include <linux/io_uring.h>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <optional>
#include <stdexcept>
#include <system_error>

#include "os_file.h"

namespace corvid { inline namespace filesys {

// Raw io_uring syscall wrappers. Avoids depending on liburing.
//
// `iou_setup` calls `io_uring_setup(2)`.
// `iou_enter` calls `io_uring_enter(2)`.
inline int iou_setup(unsigned entries, io_uring_params* p) noexcept {
  return static_cast<int>(::syscall(__NR_io_uring_setup, entries, p));
}

// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
inline int iou_enter(int fd, unsigned to_submit, unsigned min_complete,
    unsigned flags, const void* arg, size_t argsz) noexcept {
  return static_cast<int>(::syscall(__NR_io_uring_enter, fd, to_submit,
      min_complete, flags, arg, argsz));
}

// `iou_register` calls `io_uring_register(2)`.
inline int
iou_register(int fd, unsigned opcode, void* arg, unsigned nr_args) noexcept {
  return static_cast<int>(
      ::syscall(__NR_io_uring_register, fd, opcode, arg, nr_args));
}

// RAII wrapper around a Linux `io_uring` instance.
//
// Manages the SQ/CQ ring mmaps and SQE array. Provides low-level helpers for
// preparing and submitting SQEs and consuming CQEs.
//
// Not thread-safe: all calls must come from the same thread (the loop thread).
//
// Usage pattern:
//   1. Fill an `io_uring_sqe` via `get_sqe()` (returns `nullptr` if SQ full).
//   2. Call `submit()` to flush prepared SQEs to the kernel.
//   3. Call `wait(timeout_ms)` to block until CQEs are ready.
//   4. Iterate `peek_cqe()` / `advance_cq()` to consume CQEs.
//
// Memory layout: On kernels with `IORING_FEAT_SINGLE_MMAP` (>= 5.4), the SQ
// and CQ rings share one `mmap` region. The SQE array is always a separate
// `mmap`. Without `IORING_FEAT_SINGLE_MMAP`, two ring `mmap` regions are
// used.
class [[nodiscard]] io_uring_ring {
public:
  static constexpr unsigned default_entries = 256;

  io_uring_ring() noexcept = default;

  io_uring_ring(io_uring_ring&& o) noexcept
      : ring_fd_{std::move(o.ring_fd_)}, ring_map_{o.ring_map_},
        ring_map_size_{o.ring_map_size_}, cq_map_{o.cq_map_},
        cq_map_size_{o.cq_map_size_}, sqes_map_{o.sqes_map_},
        sqes_map_size_{o.sqes_map_size_}, sq_head_{o.sq_head_},
        sq_tail_{o.sq_tail_}, sq_ring_mask_val_{o.sq_ring_mask_val_},
        sq_entries_{o.sq_entries_}, sq_array_{o.sq_array_},
        cq_head_{o.cq_head_}, cq_tail_{o.cq_tail_},
        cq_ring_mask_val_{o.cq_ring_mask_val_}, cq_entries_{o.cq_entries_},
        cqes_{o.cqes_}, sqes_{o.sqes_}, sq_local_tail_{o.sq_local_tail_} {
    o.ring_map_ = MAP_FAILED;
    o.cq_map_ = MAP_FAILED;
    o.sqes_map_ = MAP_FAILED;
    o.ring_map_size_ = 0;
    o.cq_map_size_ = 0;
    o.sqes_map_size_ = 0;
    o.sq_head_ = nullptr;
    o.sq_tail_ = nullptr;
    o.sq_array_ = nullptr;
    o.cq_head_ = nullptr;
    o.cq_tail_ = nullptr;
    o.cqes_ = nullptr;
    o.sqes_ = nullptr;
  }

  io_uring_ring(const io_uring_ring&) = delete;
  io_uring_ring& operator=(const io_uring_ring&) = delete;
  io_uring_ring& operator=(io_uring_ring&&) = delete;

  ~io_uring_ring() { do_unmap(); }

  // Create an `io_uring` ring with at least `entries` SQ slots (rounded up
  // to the next power of two by the kernel). Throws `std::system_error` on
  // failure.
  [[nodiscard]] static io_uring_ring create(
      unsigned entries = default_entries) {
    io_uring_ring r;
    r.do_create(entries);
    return r;
  }

  [[nodiscard]] bool is_open() const noexcept { return ring_fd_.is_open(); }
  [[nodiscard]] int handle() const noexcept { return ring_fd_.handle(); }

  // Get the next SQE slot. The caller fills the returned `io_uring_sqe`
  // and calls `submit()` to make it visible to the kernel. Returns `nullptr`
  // if the SQ is full; the caller should call `submit()` to free space.
  // The returned slot is zero-initialized.
  [[nodiscard]] io_uring_sqe* get_sqe() noexcept {
    assert(is_open());
    const unsigned head =
        sq_head_->load(std::memory_order::acquire); // kernel-visible head
    if (sq_local_tail_ - head >= sq_entries_) return nullptr;
    const unsigned idx = sq_local_tail_ & sq_ring_mask_val_;
    if (sq_array_) sq_array_[idx] = idx; // identity mapping for sq_array
    ++sq_local_tail_;
    io_uring_sqe* sqe = &sqes_[idx];
    std::memset(sqe, 0, sizeof(*sqe));
    return sqe;
  }

  // Submit all SQEs prepared since the last `submit()`. Advances the
  // kernel-visible SQ tail and calls `io_uring_enter`. Returns the number
  // of SQEs submitted, or -1 on error.
  [[nodiscard]] int submit() noexcept {
    assert(is_open());
    const unsigned old_tail = sq_tail_->load(std::memory_order::relaxed);
    const unsigned to_submit = sq_local_tail_ - old_tail;
    if (to_submit == 0) return 0;
    // Ensure SQE writes are visible before advancing the tail.
    std::atomic_thread_fence(std::memory_order::release);
    sq_tail_->store(sq_local_tail_, std::memory_order::release);
    const int r = iou_enter(ring_fd_.handle(), to_submit, 0, 0, nullptr, 0);
    return r < 0 ? -1 : r;
  }

  // Peek at the next ready CQE without consuming it. Returns `nullptr` if
  // the CQ is empty.
  [[nodiscard]] const io_uring_cqe* peek_cqe() noexcept {
    assert(is_open());
    const unsigned head = cq_head_->load(std::memory_order::relaxed);
    const unsigned tail = cq_tail_->load(std::memory_order::acquire);
    if (head == tail) return nullptr;
    return &cqes_[head & cq_ring_mask_val_];
  }

  // Consume `n` CQEs from the head of the CQ. Must not exceed the number
  // of CQEs returned by `peek_cqe` iterations.
  void advance_cq(unsigned n = 1) noexcept {
    assert(is_open());
    cq_head_->fetch_add(n, std::memory_order::release);
  }

  // Return the number of CQEs currently available without blocking.
  [[nodiscard]] int available_cqes() noexcept {
    assert(is_open());
    const unsigned head = cq_head_->load(std::memory_order::relaxed);
    const unsigned tail = cq_tail_->load(std::memory_order::acquire);
    return static_cast<int>(tail - head);
  }

  // Submit any pending SQEs, then block until at least one CQE is ready,
  // subject to `timeout_ms`. Pass -1 to block indefinitely, 0 to poll.
  // Returns the number of CQEs ready on success, or -1 on hard error. A
  // timeout expiry returns 0.
  [[nodiscard]] int wait(int timeout_ms) noexcept {
    assert(is_open());
    // Flush pending SQEs first so the kernel sees our latest subscriptions.
    if (submit() < 0) return -1;

    // Fast path: CQEs already available.
    {
      const unsigned head = cq_head_->load(std::memory_order::relaxed);
      const unsigned tail = cq_tail_->load(std::memory_order::acquire);
      if (head != tail) return static_cast<int>(tail - head);
    }

    if (timeout_ms == 0) return 0;

    if (timeout_ms < 0) {
      // Block indefinitely until at least one CQE arrives.
      const int r = iou_enter(ring_fd_.handle(), 0, 1, IORING_ENTER_GETEVENTS,
          nullptr, 0);
      if (r < 0) return (errno == EINTR) ? 0 : -1;
    } else {
      // Block with a bounded timeout via `IORING_ENTER_EXT_ARG`.
      // `io_uring_getevents_arg.ts` is a pointer-as-u64 to a
      // `__kernel_timespec`.
      __kernel_timespec ts{.tv_sec = static_cast<long long>(timeout_ms / 1000),
          .tv_nsec = static_cast<long long>(timeout_ms % 1000) * 1000000LL};
      io_uring_getevents_arg arg{.sigmask = 0,
          .sigmask_sz = static_cast<__u32>(_NSIG / 8),
          .pad = 0,
          .ts = reinterpret_cast<__u64>(&ts)};
      const int r = iou_enter(ring_fd_.handle(), 0, 1,
          IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG, &arg, sizeof(arg));
      if (r < 0) {
        if (errno == ETIME || errno == EINTR) return 0;
        return -1;
      }
    }

    const unsigned head = cq_head_->load(std::memory_order::relaxed);
    const unsigned tail = cq_tail_->load(std::memory_order::acquire);
    return static_cast<int>(tail - head);
  }

private:
  // Create the ring, mmapping the SQ/CQ rings and SQE array.
  // Throws `std::system_error` on failure.
  void do_create(unsigned entries) {
    io_uring_params p{};

    const int fd = iou_setup(entries, &p);
    if (fd < 0)
      throw std::system_error(errno, std::generic_category(),
          "io_uring_setup");
    ring_fd_ = os_file{fd};

    sq_entries_ = p.sq_entries;
    cq_entries_ = p.cq_entries;

    // Size required for the SQ array component (sq_array is only present
    // without IORING_SETUP_NO_SQARRAY, which we do not set for compatibility).
    const size_t sq_ring_size =
        p.sq_off.array + (p.sq_entries * sizeof(unsigned));
    const size_t cq_ring_size =
        p.cq_off.cqes + (p.cq_entries * sizeof(io_uring_cqe));

    const bool single_mmap = (p.features & IORING_FEAT_SINGLE_MMAP) != 0;
    if (single_mmap) {
      // Both rings share one mmap at `IORING_OFF_SQ_RING`.
      ring_map_size_ = std::max(sq_ring_size, cq_ring_size);
      ring_map_ = ::mmap(nullptr, ring_map_size_, PROT_READ | PROT_WRITE,
          MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
      if (ring_map_ == MAP_FAILED)
        throw std::system_error(errno, std::generic_category(), "mmap sq/cq");
      cq_map_ = ring_map_; // same mapping, do not unmap separately
      cq_map_size_ = 0;
    } else {
      // Separate mmaps for SQ and CQ rings.
      ring_map_size_ = sq_ring_size;
      ring_map_ = ::mmap(nullptr, ring_map_size_, PROT_READ | PROT_WRITE,
          MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
      if (ring_map_ == MAP_FAILED)
        throw std::system_error(errno, std::generic_category(), "mmap sq");
      cq_map_size_ = cq_ring_size;
      cq_map_ = ::mmap(nullptr, cq_map_size_, PROT_READ | PROT_WRITE,
          MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
      if (cq_map_ == MAP_FAILED)
        throw std::system_error(errno, std::generic_category(), "mmap cq");
    }

    // SQE array always has its own mmap.
    sqes_map_size_ = p.sq_entries * sizeof(io_uring_sqe);
    sqes_map_ = ::mmap(nullptr, sqes_map_size_, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (sqes_map_ == MAP_FAILED)
      throw std::system_error(errno, std::generic_category(), "mmap sqes");

    // Resolve pointers into the ring maps.
    auto* sq_base = static_cast<uint8_t*>(ring_map_);
    sq_head_ =
        reinterpret_cast<std::atomic<unsigned>*>(sq_base + p.sq_off.head);
    sq_tail_ =
        reinterpret_cast<std::atomic<unsigned>*>(sq_base + p.sq_off.tail);
    sq_ring_mask_val_ =
        *reinterpret_cast<unsigned*>(sq_base + p.sq_off.ring_mask);
    // `sq_array` maps SQ tail indices to SQE indices. We use an identity
    // mapping (`sq_array[i] = i`), so the array exists but is trivially
    // maintained.
    sq_array_ = reinterpret_cast<unsigned*>(sq_base + p.sq_off.array);

    auto* cq_base = static_cast<uint8_t*>(cq_map_);
    cq_head_ =
        reinterpret_cast<std::atomic<unsigned>*>(cq_base + p.cq_off.head);
    cq_tail_ =
        reinterpret_cast<std::atomic<unsigned>*>(cq_base + p.cq_off.tail);
    cq_ring_mask_val_ =
        *reinterpret_cast<unsigned*>(cq_base + p.cq_off.ring_mask);
    cqes_ = reinterpret_cast<io_uring_cqe*>(cq_base + p.cq_off.cqes);

    sqes_ = static_cast<io_uring_sqe*>(sqes_map_);

    // Sync local SQ tail with ring state (should be 0 on fresh ring).
    sq_local_tail_ = sq_tail_->load(std::memory_order::relaxed);
  }

  // Unmap all ring regions.
  void do_unmap() noexcept {
    if (sqes_map_ != MAP_FAILED) {
      ::munmap(sqes_map_, sqes_map_size_);
      sqes_map_ = MAP_FAILED;
    }
    if (cq_map_ != MAP_FAILED && cq_map_ != ring_map_) {
      ::munmap(cq_map_, cq_map_size_);
      cq_map_ = MAP_FAILED;
    }
    if (ring_map_ != MAP_FAILED) {
      ::munmap(ring_map_, ring_map_size_);
      ring_map_ = MAP_FAILED;
    }
  }

  os_file ring_fd_;

  // Mmap regions. `cq_map_` equals `ring_map_` when `IORING_FEAT_SINGLE_MMAP`
  // is available (the common case); its size is 0 so `do_unmap` skips it.
  void* ring_map_{MAP_FAILED};
  size_t ring_map_size_{0};
  void* cq_map_{MAP_FAILED};
  size_t cq_map_size_{0};
  void* sqes_map_{MAP_FAILED};
  size_t sqes_map_size_{0};

  // Pointers into the ring maps.
  std::atomic<unsigned>* sq_head_{nullptr};
  std::atomic<unsigned>* sq_tail_{nullptr};
  unsigned sq_ring_mask_val_{0};
  unsigned sq_entries_{0};
  unsigned* sq_array_{nullptr}; // identity-mapped; null only if NO_SQARRAY

  std::atomic<unsigned>* cq_head_{nullptr};
  std::atomic<unsigned>* cq_tail_{nullptr};
  unsigned cq_ring_mask_val_{0};
  unsigned cq_entries_{0};
  io_uring_cqe* cqes_{nullptr};

  io_uring_sqe* sqes_{nullptr};

  // Local SQ tail: number of SQEs prepared since the ring was created (wraps
  // modulo 2^32). The kernel-visible tail in `sq_tail_` lags behind this
  // until `submit()` is called.
  unsigned sq_local_tail_{0};
};

}} // namespace corvid::filesys
