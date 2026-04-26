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
#include "../../enums/sequence_enum.h"
#include "iou_buffer.h"
#include "iou_wrap.h"

namespace corvid { inline namespace proto { namespace iouring {

// NOLINTNEXTLINE(performance-enum-size)
enum class block_size : size_t {
  small = 4UL * 1024,
  medium = 16UL * 1024,
  large = 64UL * 1024
};

}}} // namespace corvid::proto::iouring

template<>
constexpr inline auto
    corvid::enums::registry::enum_spec_v<corvid::proto::iouring::block_size> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::proto::iouring::block_size, "small, medium, large">();

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
public:
#pragma region Types
public:
  using block_size = ::corvid::proto::iouring::block_size;
  using span_t = buffer_pool_base::span_t;
  using const_span_t = buffer_pool_base::const_span_t;
  using buffer = iou_buffer;
  using ptr = std::byte*;
  using cptr = const std::byte*;
#pragma endregion
#pragma region Internals
  // The 2M block has a quarter reserved for writes, so that reads can't
  // exhaust the pool.
  static constexpr size_t hugepage_size = 2UL * 1024 * 1024;          // 2 MB
  static constexpr size_t read_throttle_size = hugepage_size * 3 / 4; // 1.5 MB
  static constexpr size_t write_reserve_size = hugepage_size / 4;     // 512 KB

  // Intrusive free list node.
  // TODO: Add debug-only canaries to detect memory corruption.
  struct free_node {
    free_node* next;
    free_node* prev;
  };

  // Doubly-linked intrusive free-list with head and tail pointers.
  struct free_list {
    free_node* head{};
    free_node* tail{};

    explicit operator bool() const noexcept { return head != nullptr; }

    // Add block to head.
    void push(ptr p) noexcept {
      auto* node = reinterpret_cast<free_node*>(p);
      node->next = head;
      node->prev = nullptr;
      if (head)
        head->prev = node;
      else
        tail = node;
      head = node;
    }

    // Direct allocations pop from head.
    ptr pop_head() noexcept {
      if (!head) return nullptr;
      free_node* p = head;
      head = p->next;
      if (head)
        head->prev = nullptr;
      else
        tail = nullptr;
      return reinterpret_cast<ptr>(p);
    }

    // Splits pop from tail.
    ptr pop_tail() noexcept {
      if (!tail) return nullptr;
      free_node* p = tail;
      tail = p->prev;
      if (tail)
        tail->next = nullptr;
      else
        head = nullptr;
      return reinterpret_cast<ptr>(p);
    }

    // Removal  arbitrary node (used during coalescing).
    void remove(ptr p) noexcept {
      auto* node = reinterpret_cast<free_node*>(p);
      if (node->prev)
        node->prev->next = node->next;
      else
        head = node->next;
      if (node->next) node->next->prev = node->prev;
      if (node == tail) tail = node->prev;
    }
  };
#pragma endregion
#pragma region Construction and registration
public:
  // Allocate and warm the 2 MB backing store. Falls back to a plain
  // `MAP_ANONYMOUS` mapping if `MAP_HUGETLB` is unavailable (e.g., WSL2
  // without huge pages configured). Throws `std::system_error` on failure.
  iou_buf_pool() {
    base_ = reinterpret_cast<ptr>(::mmap(nullptr, hugepage_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0));
    // Retry without huge pages.
    if (base_ == MAP_FAILED)
      base_ = reinterpret_cast<ptr>(::mmap(nullptr, hugepage_size,
          PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
          -1, 0));
    if (base_ == MAP_FAILED)
      throw std::system_error(errno, std::system_category(), "mmap");
    // Warm pages; MAP_POPULATE may already have faulted them in, but an
    // explicit memset guarantees zeroing and forces physical page
    // assignment.
    std::memset(base_, 0, hugepage_size);
    do_init_free_lists();
  }

  ~iou_buf_pool() override {
    if (base_) ::munmap(base_, hugepage_size);
  }

  iou_buf_pool(const iou_buf_pool&) = delete;
  iou_buf_pool& operator=(const iou_buf_pool&) = delete;
  iou_buf_pool(iou_buf_pool&&) = delete;
  iou_buf_pool& operator=(iou_buf_pool&&) = delete;

  // Register the 2 MB block as a single fixed buffer (index 0) with `ring`.
  // Must be called exactly once before any buffer is used in an I/O
  // submission. Returns false (and sets `errno`) on failure.
  [[nodiscard]] bool register_with(iou_ring& ring) noexcept {
    iovec iov{base_, hugepage_size};
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
    if (available_bytes_ < write_reserve_size + len) return {};
    if (in_flight_read_bytes_.load(std::memory_order::relaxed) + len >
        read_throttle_size)
      return {};
    ptr p = do_alloc_block(len);
    if (!p) return {};
    available_bytes_ -= len;
    span_t s{p, len};
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
    ptr p = do_alloc_block(len);
    if (!p) return {};
    available_bytes_ -= len;
    return {*this, span_t{p, len}, 0U, false};
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
    available_bytes_ = hugepage_size;
    // in_use_pages_ is zero-initialized by default (no pages in use).
    const size_t n = hugepage_size / *block_size::large;
    for (size_t i = 0; i < n; ++i)
      large_list_.push(base_ + (i * *block_size::large));
  }

  // Page-index of the first 4 KB page covered by address `p`.
  [[nodiscard]] size_t do_page_index(cptr p) const noexcept {
    return (p - base_) / *block_size::small;
  }

  // Mark pages as in use after external allocation.
  void do_mark_alloc(cptr p, size_t sz) noexcept {
    const size_t first = do_page_index(p);
    const size_t count = sz / *block_size::small; // 1, 4, or 16
    for (size_t i = 0; i < count; ++i) in_use_pages_[first + i] = true;
  }

  // Mark pages as free after a buffer is returned.
  void do_mark_free(cptr p, size_t sz) noexcept {
    const size_t first = do_page_index(p);
    const size_t count = sz / *block_size::small;
    for (size_t i = 0; i < count; ++i) in_use_pages_[first + i] = false;
  }

  // True if no page in `[p, p+sz)` is currently allocated externally.
  [[nodiscard]] bool do_all_free(cptr p, size_t sz) const noexcept {
    const size_t first = do_page_index(p);
    const size_t count = sz / *block_size::small; // 4 or 16 max
    for (size_t i = 0; i < count; ++i)
      if (in_use_pages_[first + i]) return false;
    return true;
  }

  // Round `p` down to the nearest *block_size::medium-aligned address (parent
  // block).
  [[nodiscard]] static ptr do_medium_parent(ptr p) noexcept {
    return p - (reinterpret_cast<uintptr_t>(p) % *block_size::medium);
  }

  // Round `p` down to the nearest *block_size::large-aligned address (parent
  // block).
  [[nodiscard]] static ptr do_large_parent(ptr p) noexcept {
    return p - (reinterpret_cast<uintptr_t>(p) % *block_size::large);
  }

  [[nodiscard]] static ptr ptr_at(ptr base, size_t offset) noexcept {
    return base + offset;
  }

  // Split one large block into four medium blocks.
  // Uses pop_tail to take the lowest-address large block (zone heuristic).
  [[nodiscard]] bool do_split_large_to_medium() noexcept {
    ptr base = large_list_.pop_tail();
    if (!base) return false;
    for (size_t i = 0; i < 4; ++i)
      medium_list_.push(base + (*block_size::medium * i));
    return true;
  }

  // Split one medium block into four small blocks.
  // Uses pop_tail to take the lowest-address medium block (zone heuristic).
  [[nodiscard]] bool do_split_medium_to_small() noexcept {
    ptr base = medium_list_.pop_tail();
    if (!base) return false;
    for (size_t i = 0; i < 4; ++i)
      small_list_.push(base + (*block_size::small * i));
    return true;
  }

  // Allocate one block of `sz` bytes. Caller holds `mutex_`. Returns
  // `nullptr` if the pool is exhausted for that size class.
  // Direct allocations use pop_head (highest address -- zone heuristic).
  [[nodiscard]] ptr do_alloc_block(size_t sz) noexcept {
    if (sz <= *block_size::small) {
      if (!small_list_) {
        if (!medium_list_ && !do_split_large_to_medium()) return nullptr;
        if (!do_split_medium_to_small()) return nullptr;
      }
      ptr p = small_list_.pop_head();
      if (p) do_mark_alloc(p, *block_size::small);
      return p;
    }
    if (sz <= *block_size::medium) {
      if (!medium_list_ && !do_split_large_to_medium()) return nullptr;
      ptr p = medium_list_.pop_head();
      if (p) do_mark_alloc(p, *block_size::medium);
      return p;
    }
    if (sz <= *block_size::large) {
      ptr p = large_list_.pop_head();
      if (p) do_mark_alloc(p, *block_size::large);
      return p;
    }
    return nullptr; // Sizes above 64 KB are unsupported.
  }

  // Return a slab and coalesce upward if all siblings of the parent are free.
  // The block is pushed onto its tier list first, then the sibling check runs
  // so that all four siblings are on the list when `remove` iterates them.
  // Maximum recursion depth is 2 (small -> medium -> large).
  void do_return_and_coalesce(ptr p, size_t sz) noexcept {
    do_mark_free(p, sz);
    if (sz == *block_size::small) {
      small_list_.push(p);
      ptr parent = do_medium_parent(p);
      if (do_all_free(parent, *block_size::medium)) {
        for (size_t i = 0; i < 4; ++i)
          small_list_.remove(ptr_at(parent, i * *block_size::small));
        do_return_and_coalesce(parent, *block_size::medium);
      }
    } else if (sz == *block_size::medium) {
      medium_list_.push(p);
      ptr parent = do_large_parent(p);
      if (do_all_free(parent, *block_size::large)) {
        for (size_t i = 0; i < 4; ++i)
          medium_list_.remove(ptr_at(parent, i * *block_size::medium));
        large_list_.push(parent);
      }
    } else {
      large_list_.push(p);
    }
  }

  void return_buffer(span_t s, bool is_read) noexcept override {
    if (std::scoped_lock lock{mutex_}; true) {
      assert(available_bytes_ + s.size() <= hugepage_size);
      do_return_and_coalesce(s.data(), s.size());
      available_bytes_ += s.size();
    }
    if (is_read) decrement_read_bytes(s.size());
  }

  // Overrides.

  [[nodiscard]] ptr base() const noexcept override { return base_; }

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
  ptr base_{};
  mutable std::mutex mutex_;
  free_list small_list_{};
  free_list medium_list_{};
  free_list large_list_{};
  size_t available_bytes_{};
  std::atomic_size_t in_flight_read_bytes_;
  // One bit per 4 KB page; 1 = page is allocated externally, 0 = free.
  fixed_bitset<hugepage_size / *block_size::small> in_use_pages_;
#pragma endregion
};

} // namespace iouring
using iouring::iou_buf_pool;
using iouring::iou_cqe_flags;
using iouring::iou_res;
}} // namespace corvid::proto
