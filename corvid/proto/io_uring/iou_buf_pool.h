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
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <sys/mman.h>
#include <sys/uio.h>

#include "iou_wrap.h"

namespace corvid { inline namespace proto { inline namespace iouring {

// Pool of pre-registered fixed I/O buffers backed by a single 2 MB huge page.
//
// The entire 2 MB block is registered with the kernel as one entry in the
// io_uring fixed-buffer table (`buf_index` = 0). The kernel pins these pages
// once at registration time via `register_with`, avoiding per-operation
// pinning overhead.
//
// Internally the memory is managed by a three-tier slab allocator with O(1)
// intrusive free-lists for 4 KB, 16 KB, and 64 KB slots. Larger slabs are
// split on demand; splitting is one-way (no buddy merging in this revision).
//
// Backpressure: `alloc_read` enforces two limits:
//   - a hard 512 KB write reserve that read allocations cannot touch;
//   - a 1536 KB in-flight cap on outstanding read bytes.
// `alloc_write` is unconstrained (it may use any available memory, including
// the write reserve).
//
// `iou_dma_buf_pool` is non-copyable and non-movable: the token's `pool_`
// pointer must remain valid for its lifetime. Always own the pool as a member
// of a heap-allocated object (e.g., `iou_basic_loop`).
class iou_dma_buf_pool {
private:
  static constexpr size_t HUGE_PAGE_SIZE = 2UL * 1024 * 1024; // 2 MB
  static constexpr size_t SMALL_SIZE = 4UL * 1024;            // 4 KB
  static constexpr size_t MEDIUM_SIZE = 16UL * 1024;          // 16 KB
  static constexpr size_t LARGE_SIZE = 64UL * 1024;           // 64 KB
  static constexpr size_t READ_THROTTLE = 1536UL * 1024;      // 1.5 MB
  static constexpr size_t WRITE_RESERVE = 512UL * 1024;       // 512 KB

  struct free_node {
    free_node* next;
  };

public:
  // NOLINTNEXTLINE(performance-enum-size)
  enum class block_size : size_t {
    small = 4UL * 1024,
    medium = 16UL * 1024,
    large = 64UL * 1024
  };

public:
  // Moveable RAII handle to a single slab allocation. Returns its memory to
  // the pool on destruction or `reset()`. `buf_index()` is always 0;
  // `offset()` gives the byte distance from the huge page base for
  // file-offset SQE fields.
  class token {
  public:
    token() = default;
    ~token() { reset(); }

    token(token&& o) noexcept
        : pool_{std::exchange(o.pool_, nullptr)},
          span_{std::exchange(o.span_, {})}, is_read_{o.is_read_} {}

    token& operator=(token&& o) noexcept {
      if (this != &o) {
        reset();
        pool_ = std::exchange(o.pool_, nullptr);
        span_ = std::exchange(o.span_, {});
        is_read_ = o.is_read_;
      }
      return *this;
    }

    token(const token&) = delete;
    token& operator=(const token&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return pool_; }

    // Mutable span over the full allocated slab.
    [[nodiscard]] std::span<std::byte> span() noexcept { return span_; }

    // Read-only span over the first `n` bytes (clamped to `size()`).
    [[nodiscard]] std::span<const std::byte> span(size_t n) const noexcept {
      return span_.first(std::min(n, span_.size()));
    }

    [[nodiscard]] std::byte* data() noexcept { return span_.data(); }
    [[nodiscard]] const std::byte* data() const noexcept {
      return span_.data();
    }

    // Byte size of this allocation: 4096, 16384, or 65536.
    [[nodiscard]] size_t size() const noexcept { return span_.size(); }

    // Always 0: the entire 2 MB page is registered as a single buffer entry.
    [[nodiscard]] static size_t buf_index() noexcept { return 0; }

    // Byte offset from the huge page base. Use as `sqe->off` for file I/O.
    [[nodiscard]] uint64_t offset() const noexcept {
      return static_cast<uint64_t>(span_.data() - pool_->base_);
    }

    // Return the allocation to the pool immediately; token becomes empty.
    void reset() noexcept {
      if (!pool_) return;
      pool_->do_return(span_, is_read_);
      pool_ = nullptr;
      span_ = {};
    }

  private:
    friend class iou_dma_buf_pool;

    token(iou_dma_buf_pool* pool, std::span<std::byte> span,
        bool is_read) noexcept
        : pool_{pool}, span_{span}, is_read_{is_read} {}

    iou_dma_buf_pool* pool_{};
    std::span<std::byte> span_;
    bool is_read_{};
  };

  // Allocate and warm the 2 MB backing store. Falls back to a plain
  // `MAP_ANONYMOUS` mapping if `MAP_HUGETLB` is unavailable (e.g., WSL2
  // without huge pages configured). Throws `std::system_error` on failure.
  iou_dma_buf_pool() {
    void* p = ::mmap(nullptr, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0);
    // Retry without huge pages.
    if (p == MAP_FAILED)
      p = ::mmap(nullptr, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
          MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (p == MAP_FAILED)
      throw std::system_error(errno, std::system_category(), "mmap");
    base_ = static_cast<std::byte*>(p);
    // Warm pages; MAP_POPULATE may already have faulted them in, but an
    // explicit memset guarantees zeroing and forces physical page assignment.
    std::memset(base_, 0, HUGE_PAGE_SIZE);
    do_init_free_lists();
  }

  ~iou_dma_buf_pool() {
    if (base_) ::munmap(base_, HUGE_PAGE_SIZE);
  }

  iou_dma_buf_pool(const iou_dma_buf_pool&) = delete;
  iou_dma_buf_pool& operator=(const iou_dma_buf_pool&) = delete;
  iou_dma_buf_pool(iou_dma_buf_pool&&) = delete;
  iou_dma_buf_pool& operator=(iou_dma_buf_pool&&) = delete;

  // Register the 2 MB block as a single fixed buffer (index 0) with `ring`.
  // Must be called exactly once before any token is used in an I/O submission.
  // Returns false (and sets `errno`) on failure.
  [[nodiscard]] bool register_with(iou_ring& ring) noexcept {
    iovec iov{base_, HUGE_PAGE_SIZE};
    return ring.register_buffers(&iov, 1);
  }

  // Borrow a slab for an incoming read. Returns an empty token if:
  //   - the pool has fewer than WRITE_RESERVE bytes free after this alloc, or
  //   - in-flight read bytes would exceed READ_THROTTLE.
  // Thread-safe.
  [[nodiscard]] token alloc_read(block_size sz = block_size::small) noexcept {
    std::lock_guard lock{mutex_};
    const auto len = static_cast<size_t>(sz);
    if (available_bytes_ < WRITE_RESERVE + len) return {};
    if (in_flight_read_bytes_.load(std::memory_order::relaxed) + len >
        READ_THROTTLE)
      return {};
    void* p = do_alloc_block(len);
    if (!p) return {};
    available_bytes_ -= len;
    std::span<std::byte> s{static_cast<std::byte*>(p), len};
    in_flight_read_bytes_.fetch_add(len, std::memory_order::relaxed);
    return {this, s, true};
  }

  // Borrow a slab for an outgoing write. Not subject to read backpressure;
  // may draw from the write reserve. Returns an empty token if fully
  // exhausted. Thread-safe.
  [[nodiscard]] token alloc_write(block_size sz = block_size::small) noexcept {
    std::lock_guard lock{mutex_};
    const auto len = static_cast<size_t>(sz);
    void* p = do_alloc_block(len);
    if (!p) return {};
    available_bytes_ -= len;
    return {this, std::span<std::byte>{static_cast<std::byte*>(p), len},
        false};
  }

  // Total free bytes currently in the pool. Thread-safe.
  [[nodiscard]] size_t available() const noexcept {
    std::lock_guard lock{mutex_};
    return available_bytes_;
  }

private:
  // Push all 32 x 64 KB blocks onto the large free-list.
  void do_init_free_lists() noexcept {
    const size_t n = HUGE_PAGE_SIZE / LARGE_SIZE;
    for (size_t i = 0; i < n; ++i) {
      auto* node = reinterpret_cast<free_node*>(base_ + (i * LARGE_SIZE));
      node->next = large_head_;
      large_head_ = node;
    }
    available_bytes_ = HUGE_PAGE_SIZE;
  }

  // Pop one node from `head`. Returns nullptr if the list is empty.
  [[nodiscard]] static void* do_pop(free_node*& head) noexcept {
    if (!head) return nullptr;
    free_node* p = head;
    head = p->next;
    return p;
  }

  // Push `p` onto `head`.
  static void do_push(free_node*& head, void* p) noexcept {
    auto* node = static_cast<free_node*>(p);
    node->next = head;
    head = node;
  }

  // Split one large block into four medium blocks.
  [[nodiscard]] bool do_split_large_to_medium() noexcept {
    void* p = do_pop(large_head_);
    if (!p) return false;
    auto* base = static_cast<std::byte*>(p);
    for (size_t i = 0; i < 4; ++i)
      do_push(medium_head_, base + (i * MEDIUM_SIZE));
    return true;
  }

  // Split one medium block into four small blocks.
  [[nodiscard]] bool do_split_medium_to_small() noexcept {
    void* p = do_pop(medium_head_);
    if (!p) return false;
    auto* base = static_cast<std::byte*>(p);
    for (size_t i = 0; i < 4; ++i)
      do_push(small_head_, base + (i * SMALL_SIZE));
    return true;
  }

  // Allocate one block of `sz` bytes. Caller holds `mutex_`. Returns `nullptr`
  // if the pool is exhausted for that size class.
  [[nodiscard]] void* do_alloc_block(size_t sz) noexcept {
    if (sz <= SMALL_SIZE) {
      if (!small_head_) {
        if (!medium_head_ && !do_split_large_to_medium()) return nullptr;
        if (!do_split_medium_to_small()) return nullptr;
      }
      return do_pop(small_head_);
    }
    if (sz <= MEDIUM_SIZE) {
      if (!medium_head_ && !do_split_large_to_medium()) return nullptr;
      return do_pop(medium_head_);
    }
    if (sz <= LARGE_SIZE) return do_pop(large_head_);
    return nullptr; // Sizes above 64 KB are unsupported.
  }

  void do_return(std::span<std::byte> s, bool is_read) noexcept {
    if (std::lock_guard lock{mutex_}; true) {
      assert(available_bytes_ + s.size() <= HUGE_PAGE_SIZE);
      if (s.size() <= SMALL_SIZE)
        do_push(small_head_, s.data());
      else if (s.size() <= MEDIUM_SIZE)
        do_push(medium_head_, s.data());
      else
        do_push(large_head_, s.data());
      available_bytes_ += s.size();
    }
    if (is_read)
      in_flight_read_bytes_.fetch_sub(s.size(), std::memory_order::relaxed);
  }

  std::byte* base_{};
  mutable std::mutex mutex_;
  free_node* small_head_{};
  free_node* medium_head_{};
  free_node* large_head_{};
  size_t available_bytes_{};
  std::atomic_size_t in_flight_read_bytes_;
};

}}} // namespace corvid::proto::iouring
