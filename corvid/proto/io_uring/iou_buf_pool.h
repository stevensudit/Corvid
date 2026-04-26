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

#include "../../containers/fixed_bitset.h"
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
// doubly-linked free-lists for 4 KB, 16 KB, and 64 KB slots. Larger slabs are
// split on demand; freed slabs coalesce back up when all four siblings in a
// parent block become free simultaneously.
//
// Zone heuristic: splits take the lowest-address block (from the list tail) to
// cluster small allocations at low addresses and reserve high addresses for
// large allocations. Direct allocations take the highest-address block (from
// the list head).
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
    free_node* prev;
  };

  // Doubly-linked intrusive free-list with head and tail pointers.
  struct free_list {
    free_node* head{};
    free_node* tail{};

    explicit operator bool() const noexcept { return head != nullptr; }

    void push(void* p) noexcept {
      auto* node = static_cast<free_node*>(p);
      node->next = head;
      node->prev = nullptr;
      if (head)
        head->prev = node;
      else
        tail = node;
      head = node;
    }

    // Direct allocations pop from head (highest address -- zone heuristic).
    void* pop_head() noexcept {
      if (!head) return nullptr;
      free_node* p = head;
      head = p->next;
      if (head)
        head->prev = nullptr;
      else
        tail = nullptr;
      return p;
    }

    // Splits pop from tail (lowest address -- zone heuristic).
    void* pop_tail() noexcept {
      if (!tail) return nullptr;
      free_node* p = tail;
      tail = p->prev;
      if (tail)
        tail->next = nullptr;
      else
        head = nullptr;
      return p;
    }

    // O(1) removal of an arbitrary node (used during coalescing).
    void remove(void* p) noexcept {
      auto* node = static_cast<free_node*>(p);
      if (node->prev)
        node->prev->next = node->next;
      else
        head = node->next;
      if (node->next) node->next->prev = node->prev;
      if (node == tail) tail = node->prev;
    }
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
  // Blocks pushed in ascending address order; with LIFO, block 31 (highest
  // address) ends at the head and block 0 (lowest) ends at the tail.
  void do_init_free_lists() noexcept {
    available_bytes_ = HUGE_PAGE_SIZE;
    // in_use_pages_ is zero-initialized by default (no pages in use).
    const size_t n = HUGE_PAGE_SIZE / LARGE_SIZE;
    for (size_t i = 0; i < n; ++i) large_list_.push(base_ + (i * LARGE_SIZE));
  }

  // Page-index of the first 4 KB page covered by address `p`.
  [[nodiscard]] size_t do_page_index(const void* p) const noexcept {
    return (static_cast<const std::byte*>(p) - base_) / SMALL_SIZE;
  }

  // Mark pages as in use after external allocation.
  void do_mark_alloc(const void* p, size_t sz) noexcept {
    const size_t first = do_page_index(p);
    const size_t count = sz / SMALL_SIZE; // 1, 4, or 16
    for (size_t i = 0; i < count; ++i) in_use_pages_[first + i] = true;
  }

  // Mark pages as free after a buffer is returned.
  void do_mark_free(const void* p, size_t sz) noexcept {
    const size_t first = do_page_index(p);
    const size_t count = sz / SMALL_SIZE;
    for (size_t i = 0; i < count; ++i) in_use_pages_[first + i] = false;
  }

  // True if no page in `[p, p+sz)` is currently allocated externally.
  [[nodiscard]] bool do_all_free(const void* p, size_t sz) const noexcept {
    const size_t first = do_page_index(p);
    const size_t count = sz / SMALL_SIZE; // 4 or 16 max
    for (size_t i = 0; i < count; ++i)
      if (in_use_pages_[first + i]) return false;
    return true;
  }

  // Round `p` down to the nearest MEDIUM_SIZE-aligned address (parent block).
  [[nodiscard]] static void* do_medium_parent(void* p) noexcept {
    return static_cast<std::byte*>(p) -
           (reinterpret_cast<uintptr_t>(p) % MEDIUM_SIZE);
  }

  // Round `p` down to the nearest LARGE_SIZE-aligned address (parent block).
  [[nodiscard]] static void* do_large_parent(void* p) noexcept {
    return static_cast<std::byte*>(p) -
           (reinterpret_cast<uintptr_t>(p) % LARGE_SIZE);
  }

  [[nodiscard]] static void* ptr_at(void* base, size_t offset) noexcept {
    return static_cast<std::byte*>(base) + offset;
  }

  // Split one large block into four medium blocks.
  // Uses pop_tail to take the lowest-address large block (zone heuristic).
  [[nodiscard]] bool do_split_large_to_medium() noexcept {
    void* p = large_list_.pop_tail();
    if (!p) return false;
    auto* base = static_cast<std::byte*>(p);
    for (size_t i = 0; i < 4; ++i) medium_list_.push(base + (i * MEDIUM_SIZE));
    return true;
  }

  // Split one medium block into four small blocks.
  // Uses pop_tail to take the lowest-address medium block (zone heuristic).
  [[nodiscard]] bool do_split_medium_to_small() noexcept {
    void* p = medium_list_.pop_tail();
    if (!p) return false;
    auto* base = static_cast<std::byte*>(p);
    for (size_t i = 0; i < 4; ++i) small_list_.push(base + (i * SMALL_SIZE));
    return true;
  }

  // Allocate one block of `sz` bytes. Caller holds `mutex_`. Returns
  // `nullptr` if the pool is exhausted for that size class.
  // Direct allocations use pop_head (highest address -- zone heuristic).
  [[nodiscard]] void* do_alloc_block(size_t sz) noexcept {
    if (sz <= SMALL_SIZE) {
      if (!small_list_) {
        if (!medium_list_ && !do_split_large_to_medium()) return nullptr;
        if (!do_split_medium_to_small()) return nullptr;
      }
      void* p = small_list_.pop_head();
      if (p) do_mark_alloc(p, SMALL_SIZE);
      return p;
    }
    if (sz <= MEDIUM_SIZE) {
      if (!medium_list_ && !do_split_large_to_medium()) return nullptr;
      void* p = medium_list_.pop_head();
      if (p) do_mark_alloc(p, MEDIUM_SIZE);
      return p;
    }
    if (sz <= LARGE_SIZE) {
      void* p = large_list_.pop_head();
      if (p) do_mark_alloc(p, LARGE_SIZE);
      return p;
    }
    return nullptr; // Sizes above 64 KB are unsupported.
  }

  // Return a slab and coalesce upward if all siblings of the parent are free.
  // The block is pushed onto its tier list first, then the sibling check runs
  // so that all four siblings are on the list when `remove` iterates them.
  // Maximum recursion depth is 2 (small -> medium -> large).
  void do_return_and_coalesce(void* p, size_t sz) noexcept {
    do_mark_free(p, sz);
    if (sz == SMALL_SIZE) {
      small_list_.push(p);
      void* parent = do_medium_parent(p);
      if (do_all_free(parent, MEDIUM_SIZE)) {
        for (size_t i = 0; i < 4; ++i)
          small_list_.remove(ptr_at(parent, i * SMALL_SIZE));
        do_return_and_coalesce(parent, MEDIUM_SIZE);
      }
    } else if (sz == MEDIUM_SIZE) {
      medium_list_.push(p);
      void* parent = do_large_parent(p);
      if (do_all_free(parent, LARGE_SIZE)) {
        for (size_t i = 0; i < 4; ++i)
          medium_list_.remove(ptr_at(parent, i * MEDIUM_SIZE));
        large_list_.push(parent);
      }
    } else {
      large_list_.push(p);
    }
  }

  void return_buffer(span_t s, bool is_read) noexcept override {
    if (std::scoped_lock lock{mutex_}; true) {
      assert(available_bytes_ + s.size() <= HUGE_PAGE_SIZE);
      do_return_and_coalesce(s.data(), s.size());
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
  free_list small_list_{};
  free_list medium_list_{};
  free_list large_list_{};
  size_t available_bytes_{};
  std::atomic_size_t in_flight_read_bytes_;
  // One bit per 4 KB page; 1 = page is allocated externally, 0 = free.
  fixed_bitset<HUGE_PAGE_SIZE / SMALL_SIZE> in_use_pages_;
#pragma endregion
};

} // namespace iouring
using iouring::iou_buf_pool;
using iouring::iou_cqe_flags;
using iouring::iou_res;
}} // namespace corvid::proto
