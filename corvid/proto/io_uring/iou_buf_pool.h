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
// When possible, the entire 2 MB block is registered with the kernel as one
// entry in the `io_uring` fixed-buffer table (`buf_index` = 0). The kernel
// pins these pages once at registration time, via `register_with`, avoiding
// per-operation pinning overhead.
//
// Internally, the memory is managed by a three-tier slab allocator with O(1)
// doubly-linked free-lists for 4 KB, 16 KB, and 64 KB slots. Larger slabs are
// split on demand; freed slabs coalesce back up when needed.
//
// Zone heuristic: each free-list keeps its highest-address blocks near the
// head and its lowest-address blocks near the tail. Direct allocations pop
// from the head (LIFO, high-address first). Splits borrow from the tail (the
// lowest-address block), and coalesced blocks are returned to the tail as
// well. Over time, this clusters small allocations at low addresses and
// reserves high addresses for large allocations.
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
#pragma region Free list
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

  // Doubly-linked intrusive free-list. The head is the hot (LIFO) end:
  // direct allocations pop from here and returned blocks push here. The tail
  // is the deferred (cold) end: splits borrow from here, and coalesced or
  // freshly split blocks are pushed here so they accumulate for preferential
  // re-splitting rather than immediate reuse.
  struct free_list {
    free_node* head{};
    free_node* tail{};

    explicit operator bool() const noexcept { return head != nullptr; }

    // Push block to head (hot end). Used when returning a directly borrowed
    // block (LIFO).
    void push_head(ptr p) noexcept {
      auto* node = reinterpret_cast<free_node*>(p);
      *node = {.next = head, .prev = nullptr};
      if (head)
        head->prev = node;
      else
        tail = node;
      head = node;
    }

    // Push block to tail (cold end). Used for coalesced blocks and for
    // sub-blocks produced by a split.
    void push_tail(ptr p) noexcept {
      auto* node = reinterpret_cast<free_node*>(p);
      *node = {.next = nullptr, .prev = tail};
      if (tail)
        tail->next = node;
      else
        head = node;
      tail = node;
    }

    // Pop from head (hot end) for direct allocation (LIFO).
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

    // Pop from tail (cold end) when borrowing a block for splitting.
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
#pragma region Construction
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
    init_free_lists();
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
  // submission. Returns false (and sets `errno`) on failure. The `ring`
  // unregisters on destruction.
  [[nodiscard]] bool register_with(iou_ring& ring) noexcept {
    iovec iov{base_, hugepage_size};
    return ring.register_buffers(&iov, 1);
  }
#pragma endregion
#pragma region Buffers

  // Borrow a buffer for an incoming read. Returns an empty buffer if:
  //   - the pool has fewer than `write_reserve_size` bytes free after this
  //   alloc, or
  //   - in-flight read bytes would exceed `read_throttle_size`.
  // Thread-safe.
  [[nodiscard]] buffer borrow_reader(
      block_size sz = block_size::small) noexcept {
    std::scoped_lock lock{mutex_};
    const auto len = *sz;
    if (available_bytes_ < write_reserve_size + len) return {};
    if (in_flight_read_bytes_.load(std::memory_order::relaxed) + len >
        read_throttle_size)
      return {};
    span_t s{alloc_block(len), len};
    if (!s.data()) return {};
    available_bytes_ -= len;
    in_flight_read_bytes_.fetch_add(len, std::memory_order::relaxed);
    return {*this, s, 0U, true};
  }

  // Borrow a buffer for an outgoing write. Not subject to read backpressure;
  // may draw from the write reserve. Returns an empty buffer if fully
  // exhausted. Thread-safe.
  [[nodiscard]] buffer borrow_writer(
      block_size sz = block_size::small) noexcept {
    std::scoped_lock lock{mutex_};
    span_t s{alloc_block(*sz), *sz};
    if (!s.data()) return {};
    available_bytes_ -= s.size();
    return {*this, s, 0U, false};
  }

  // Total free bytes currently in the pool. Thread-safe.
  [[nodiscard]] size_t available() const noexcept {
    std::scoped_lock lock{mutex_};
    return available_bytes_;
  }

#pragma endregion
#pragma region Overrides
private:
  void return_buffer(span_t s, bool is_read) noexcept override {
    if (std::scoped_lock lock{mutex_}; true) {
      assert(available_bytes_ + s.size() <= hugepage_size);
      return_block(s.data(), s.size());
      available_bytes_ += s.size();
    }
    if (is_read) decrement_read_bytes(s.size());
  }

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
#pragma region Helpers

  // Push all 32 x 64 KB blocks onto the large free-list.
  // Blocks are pushed in ascending address order; with LIFO, block 31 (highest
  // address) ends at the head and block 0 (lowest) ends at the tail.
  void init_free_lists() noexcept {
    available_bytes_ = hugepage_size;
    const size_t n = hugepage_size / *block_size::large;
    for (size_t i = 0; i < n; ++i)
      large_list_.push_head(base_ + (i * *block_size::large));
  }

  // Page-index of the first 4 KB page covered by address `p`.
  [[nodiscard]] size_t find_page_index(cptr p) const noexcept {
    return (p - base_) / *block_size::small;
  }

  // Mark pages as externally allocated (in_use=true) or free (in_use=false) in
  // bitmap.
  void mark_pages(cptr p, size_t sz, bool in_use) noexcept {
    const size_t first = find_page_index(p);
    const size_t count = sz / *block_size::small;
    for (size_t i = 0; i < count; ++i) in_use_pages_[first + i] = in_use;
  }

  // True if no page in `[p, p+sz)` is currently allocated externally.
  [[nodiscard]] bool are_all_free(cptr p, size_t sz) const noexcept {
    const size_t first = find_page_index(p);
    const size_t count = sz / *block_size::small;
    for (size_t i = 0; i < count; ++i)
      if (in_use_pages_[first + i]) return false;
    return true;
  }

  // Split one block from `src` into four child blocks pushed to `dst`.
  // Borrows from the tail (cold end). Children are appended to the tail in
  // descending address order so the highest-address sub-block lands nearest
  // the head, preserving the zone invariant even if `dst` is non-empty.
  [[nodiscard]] static bool
  split(free_list& src, free_list& dst, size_t child_sz) noexcept {
    ptr base = src.pop_tail();
    if (!base) return false;
    for (size_t i = 4; i-- > 0;) dst.push_tail(base + (child_sz * i));
    return true;
  }

  // Scan all parent-sized windows; coalesce any where all four child siblings
  // are free. Removes coalesced child blocks from `src` and pushes the
  // reconstituted parent to `dst` tail (cold end) for preferential
  // re-splitting. Returns true if at least one block was coalesced.
  [[nodiscard]] bool coalesce(free_list& src, size_t child_sz, free_list& dst,
      size_t parent_sz) noexcept {
    const size_t n = hugepage_size / parent_sz;
    bool any{};
    for (size_t i = 0; i < n; ++i) {
      ptr parent = base_ + (i * parent_sz);
      if (!are_all_free(parent, parent_sz)) continue;
      for (size_t j = 0; j < 4; ++j) src.remove(parent + (j * child_sz));
      dst.push_tail(parent);
      any = true;
    }
    return any;
  }

  // Ensure `large_list_` is non-empty, coalescing medium blocks if needed.
  [[nodiscard]] bool ensure_large() noexcept {
    return large_list_ ||
           coalesce(medium_list_, *block_size::medium, large_list_,
               *block_size::large);
  }

  // Ensure `medium_list_` is non-empty, splitting or coalescing as needed.
  [[nodiscard]] bool ensure_medium() noexcept {
    if (medium_list_) return true;
    if (split(large_list_, medium_list_, *block_size::medium)) return true;
    return coalesce(medium_list_, *block_size::medium, large_list_,
               *block_size::large) &&
           split(large_list_, medium_list_, *block_size::medium);
  }

  // Ensure `small_list_` is non-empty, splitting or coalescing as needed.
  [[nodiscard]] bool ensure_small() noexcept {
    if (small_list_) return true;
    if (!ensure_medium()) return false;
    if (split(medium_list_, small_list_, *block_size::small)) return true;
    return coalesce(small_list_, *block_size::small, medium_list_,
               *block_size::medium) &&
           split(medium_list_, small_list_, *block_size::small);
  }

  // Allocate one block of `sz` bytes. Caller holds `mutex_`. Returns
  // `nullptr` if the pool is exhausted for that size class.
  // Direct allocations pop from the head (hot/LIFO end).
  [[nodiscard]] ptr alloc_block(size_t sz) noexcept {
    if (sz <= *block_size::small) {
      if (!ensure_small()) return nullptr;
      ptr p = small_list_.pop_head();
      if (p) mark_pages(p, *block_size::small, true);
      return p;
    }
    if (sz <= *block_size::medium) {
      if (!ensure_medium()) return nullptr;
      ptr p = medium_list_.pop_head();
      if (p) mark_pages(p, *block_size::medium, true);
      return p;
    }
    if (sz <= *block_size::large) {
      if (!ensure_large()) return nullptr;
      ptr p = large_list_.pop_head();
      if (p) mark_pages(p, *block_size::large, true);
      return p;
    }
    return nullptr; // Sizes above 64 KB are unsupported.
  }

  // Return a slab to its tier free-list. Coalescing is deferred to alloc time.
  // All tiers return to the head (LIFO) for hot reuse. Coalescing eligibility
  // is tracked by `in_use_pages_`, not by list position.
  void return_block(ptr p, size_t sz) noexcept {
    mark_pages(p, sz, false);
    if (sz == *block_size::small)
      small_list_.push_head(p);
    else if (sz == *block_size::medium)
      medium_list_.push_head(p);
    else
      large_list_.push_head(p);
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
