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
#include <mutex>
#include <span>
#include <system_error>
#include <sys/mman.h>
#include <sys/uio.h>

#include "iou_buffer.h"
#include "iou_wrap.h"

namespace corvid { inline namespace proto {
namespace iouring {

// Pool of pre-registered fixed I/O buffers backed by a single 2 MB huge page.
//
// The entire 2 MB block is registered with the kernel as one entry in the
// `io_uring` fixed-buffer table (`buf_index` = 0). The kernel pins these pages
// once at registration time, via `register_with`, avoiding per-operation
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
// `iou_buf_pool` is non-copyable and non-movable. The pool must outlive
// all uses of the memory it manages, and the `buffer` objects it hands out.
//
// Plans:
// - Allow `iou_buf_pool` size to be configured at construction time.
// - Add coalescing of adjacent free slabs to support more flexible allocation
//   patterns. This may well require additional bookkeeping, including a bitmap
//   of allocated 4k pages and a doubly-linked free list.
class iou_buf_pool: public buffer_pool_base {
#pragma region Internals
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
#pragma endregion
#pragma region Types
public:
  // NOLINTNEXTLINE(performance-enum-size)
  enum class block_size : size_t {
    small = 4UL * 1024,
    medium = 16UL * 1024,
    large = 64UL * 1024
  };

  using span_t = buffer_pool_base::span_t;
  using const_span_t = buffer_pool_base::const_span_t;
  using buffer = iou_buffer;
#pragma endregion
#pragma region Construction and registration
public:
  // Allocate and warm the 2 MB backing store. Falls back to a plain
  // `MAP_ANONYMOUS` mapping if `MAP_HUGETLB` is unavailable (e.g., WSL2
  // without huge pages configured). Throws `std::system_error` on failure.
  iou_buf_pool() {
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
    // explicit memset guarantees zeroing and forces physical page
    // assignment.
    std::memset(base_, 0, HUGE_PAGE_SIZE);
    do_init_free_lists();
  }

  ~iou_buf_pool() override {
    if (base_) ::munmap(base_, HUGE_PAGE_SIZE);
  }

  iou_buf_pool(const iou_buf_pool&) = delete;
  iou_buf_pool& operator=(const iou_buf_pool&) = delete;
  iou_buf_pool(iou_buf_pool&&) = delete;
  iou_buf_pool& operator=(iou_buf_pool&&) = delete;

  // Register the 2 MB block as a single fixed buffer (index 0) with `ring`.
  // Must be called exactly once before any buffer is used in an I/O
  // submission. Returns false (and sets `errno`) on failure.
  [[nodiscard]] bool register_with(iou_ring& ring) noexcept {
    iovec iov{base_, HUGE_PAGE_SIZE};
    return ring.register_buffers(&iov, 1);
  }
#pragma endregion
#pragma region Buffer allocation and management

  // Borrow a buffer for an incoming read. Returns an empty buffer if:
  //   - the pool has fewer than WRITE_RESERVE bytes free after this alloc,
  //   or
  //   - in-flight read bytes would exceed READ_THROTTLE.
  // Thread-safe.
  [[nodiscard]] buffer borrow_reader(
      block_size sz = block_size::small) noexcept {
    std::scoped_lock lock{mutex_};
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
    return {*this, s, 0U, true};
  }

  // Borrow a buffer for an outgoing write. Not subject to read backpressure;
  // may draw from the write reserve. Returns an empty buffer if fully
  // exhausted. Thread-safe.
  [[nodiscard]] buffer borrow_writer(
      block_size sz = block_size::small) noexcept {
    std::scoped_lock lock{mutex_};
    const auto len = static_cast<size_t>(sz);
    void* p = do_alloc_block(len);
    if (!p) return {};
    available_bytes_ -= len;
    return {*this, span_t{static_cast<std::byte*>(p), len}, 0U, false};
  }

  // Total free bytes currently in the pool. Thread-safe.
  [[nodiscard]] size_t available() const noexcept {
    std::scoped_lock lock{mutex_};
    return available_bytes_;
  }

#pragma endregion
#pragma region Helpers

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

  // Allocate one block of `sz` bytes. Caller holds `mutex_`. Returns
  // `nullptr` if the pool is exhausted for that size class.
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

  void return_buffer(span_t s, bool is_read) noexcept override {
    if (std::scoped_lock lock{mutex_}; true) {
      assert(available_bytes_ + s.size() <= HUGE_PAGE_SIZE);
      if (s.size() <= SMALL_SIZE)
        do_push(small_head_, s.data());
      else if (s.size() <= MEDIUM_SIZE)
        do_push(medium_head_, s.data());
      else
        do_push(large_head_, s.data());
      available_bytes_ += s.size();
    }
    if (is_read) decrement_read_bytes(s.size());
  }

  // Overrides.

  [[nodiscard]] std::byte* base() const noexcept override { return base_; }

  void decrement_read_bytes(size_t n) noexcept override {
    [[maybe_unused]] const auto old =
        in_flight_read_bytes_.fetch_sub(n, std::memory_order::relaxed);
    assert(old >= n);
  }

  void increment_read_bytes(size_t n) noexcept override {
    in_flight_read_bytes_.fetch_add(n, std::memory_order::relaxed);
  }
#pragma endregion
#pragma region Data members
private:
  std::byte* base_{};
  mutable std::mutex mutex_;
  free_node* small_head_{};
  free_node* medium_head_{};
  free_node* large_head_{};
  size_t available_bytes_{};
  std::atomic_size_t in_flight_read_bytes_;
#pragma endregion
};

} // namespace iouring
using iouring::iou_buf_pool;
using iouring::iou_cqe_flags;
using iouring::iou_res;
}} // namespace corvid::proto
