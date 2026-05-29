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
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <system_error>

#include "../../infra/relaxed_atomic.h"
#include "../../containers/fixed_bitset.h"
#include "../../enums/sequence_enum.h"
#include "iou_buffer_pool_base.h"
#include "iou_buffer.h"
#include "iou_wrap.h"

namespace corvid { inline namespace proto { namespace iouring {

#pragma region iou_buf_pool_of

// Pool of pre-registered fixed I/O buffers backed by a single huge page,
// defaulting to 2 MB. If transparent huge pages are unavailable, falls back
// to a `MAP_ANONYMOUS` mapping with `MADV_HUGEPAGE` advice.
//
// The entire block is registered with the kernel as one entry in the
// `io_uring` fixed-buffer table (`buf_index` = 0). The kernel pins these pages
// once at registration time, via `register_with`, avoiding per-operation
// pinning overhead.
//
// Internally, the memory is managed as a binary buddy allocator with
// `tier_count` tiers. Tier 0 holds blocks of `MIN_BLOCK` bytes; each higher
// tier doubles the block size up to `SIZE` bytes at tier `tier_count - 1`. On
// construction, the slab holds a single full-size block at the top tier.
// Allocation splits blocks top-down on demand; freed buddies coalesce
// lazily (bottom-up) when a tier is exhausted.
//
// Zone heuristic: tiers below `tier_count / 2` keep their lowest-address
// blocks near the head (ascending); tiers at or above keep their
// highest-address blocks near the head (descending). Over time this clusters
// small allocations at low addresses and reserves high addresses for large
// allocations. Splits borrow from the tail of the source tier (cold end) and
// push children in the direction appropriate to the destination tier. The goal
// is to cluster same-sized blocks together to allow coalescing to succeed more
// often.
//
// Backpressure: `borrow_reader` enforces two limits:
//   - a hard 1/4 block write reserve that read allocations cannot touch;
//   - a 3/4 block in-flight cap on outstanding read bytes.
// `borrow_writer` is unconstrained (it may use any available memory,
// including the write reserve).
//
// `iou_buf_pool_of` is non-copyable and non-movable. The pool must outlive
// all uses of the memory it manages, and the `buffer` objects it hands out.
template<size_t SIZE = 2UL * 1024 * 1024, size_t MIN_BLOCK = 1UL * 1024>
class iou_buf_pool_of: public buffer_pool_base {
  static_assert(std::has_single_bit(SIZE), "SIZE must be a power of 2");
  static_assert(std::has_single_bit(MIN_BLOCK),
      "MIN_BLOCK must be a power of 2");
  static_assert(SIZE >= MIN_BLOCK * 2, "SIZE must be at least 2x MIN_BLOCK");

#pragma endregion
#pragma region Free list

  static constexpr size_t slab_size = SIZE;
  static constexpr size_t min_block_size = MIN_BLOCK;

  static constexpr size_t read_throttle_size = slab_size * 3 / 4;
  static constexpr size_t write_reserve_size = slab_size / 4;
  // Tier i: block size == `min_block_size << i`
  // (tier 0 == min_block_size, tier_count-1 == slab_size).
  static constexpr size_t tier_count =
      std::countr_zero(slab_size / min_block_size) + 1;

  // Intrusive linked list node. Mapped onto the start of each free block.
  // In debug mode, the canaries detect use-after-free and double-free bugs in
  // the free list management.
  struct free_node {
#ifndef NDEBUG
    static constexpr unsigned __int128 canary_value =
        static_cast<unsigned __int128>(0xC0A51F5F4FE6157ULL) << 64 |
        0xC0DE5C2B1E5AFEFDULL;
    unsigned __int128 canary_front;
#endif
    free_node* next;
    free_node* prev;
#ifndef NDEBUG
    unsigned __int128 canary_back;
#endif

    void init(free_node* n, free_node* p) noexcept {
      assert(!is_valid());
      assert(!n || n->is_valid());
      assert(!p || p->is_valid());
      next = n;
      prev = p;
#ifndef NDEBUG
      canary_front = canary_value;
      canary_back = canary_value;
#endif
    }

    ptr cleared() noexcept {
      assert(is_valid());
      assert(!prev || prev->is_valid());
      assert(!next || next->is_valid());
#ifndef NDEBUG
      next = nullptr;
      prev = nullptr;
      canary_front = 0;
      canary_back = 0;
#endif
      return reinterpret_cast<ptr>(this);
    }

    [[nodiscard]] bool is_valid() const noexcept {
#ifndef NDEBUG
      return canary_front == canary_value && canary_back == canary_value;
#else
      throw std::logic_error("Checks should only occur in debug mode");
#endif
    }
  };

private:
  // Doubly-linked intrusive free-list. The head is the hot (LIFO) end:
  // direct allocations pop from here and returned blocks push here. The tail
  // is the deferred (cold) end: splits borrow from here, and coalesced or
  // freshly split blocks are pushed here so they accumulate for preferential
  // re-splitting rather than immediate reuse.
  struct free_list {
    free_node* head{};
    free_node* tail{};
    size_t sz{};

    explicit operator bool() const noexcept { return head != nullptr; }

    // Push block to head (hot end). Used when returning a directly borrowed
    // block (LIFO).
    void push_head(ptr p) noexcept {
      auto* node = reinterpret_cast<free_node*>(p);
      node->init(head, nullptr);
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
      node->init(nullptr, tail);
      if (tail)
        tail->next = node;
      else
        head = node;
      tail = node;
    }

    // Pop from head (hot end) for direct allocation (LIFO).
    ptr pop_head() noexcept {
      assert(!head || head->is_valid());
      if (!head) return nullptr;
      free_node* p = head;
      head = p->next;
      assert(!head || head->is_valid());
      if (head)
        head->prev = nullptr;
      else
        tail = nullptr;
      return p->cleared();
    }

    // Pop from tail (cold end) when borrowing a block for splitting.
    ptr pop_tail() noexcept {
      assert(!tail || tail->is_valid());
      if (!tail) return nullptr;
      free_node* p = tail;
      tail = p->prev;
      assert(!tail || tail->is_valid());
      if (tail)
        tail->next = nullptr;
      else
        head = nullptr;
      return p->cleared();
    }

    // Remove arbitrary node (used during coalescing).
    void remove(ptr p) noexcept {
      auto* node = reinterpret_cast<free_node*>(p);
      if (node->prev)
        node->prev->next = node->next;
      else
        head = node->next;
      if (node->next) node->next->prev = node->prev;
      if (node == tail) tail = node->prev;
      node->cleared();
    }
  };

#pragma endregion
#pragma region Construction

  enum class allow : bool { ctor };

public:
  // Public for `std::make_shared`; external callers must go through `create`.
  // Allocates and warms the backing store. Falls back to a plain
  // `MAP_ANONYMOUS` mapping if `MAP_HUGETLB` is unavailable (e.g., WSL2
  // without huge pages configured). Throws `std::system_error` on failure.
  explicit iou_buf_pool_of(allow) {
    static_assert(slab_size % hugepage_size == 0);
    static_assert(std::has_single_bit(hugepage_size));
    base_ = reinterpret_cast<ptr>(::mmap(nullptr, slab_size,
        *(mmap_prot::read | mmap_prot::write),
        *(mmap_mask::map_private | mmap_mask::anonymous | mmap_mask::hugetlb |
            mmap_mask::populate),
        -1, 0));
    // Retry without explicit hugetlb. Over-allocate by one `hugepage_size` to
    // guarantee a hugepage-aligned region exists within the mapping, then trim
    // the prefix and suffix so that `base_` is aligned and `munmap` in the
    // destructor covers exactly `slab_size` bytes.
    if (base_ == MAP_FAILED) {
      constexpr size_t reserve_size = slab_size + hugepage_size;
      auto* raw = reinterpret_cast<ptr>(::mmap(nullptr, reserve_size,
          *(mmap_prot::read | mmap_prot::write),
          *(mmap_mask::map_private | mmap_mask::anonymous), -1, 0));
      if (raw == MAP_FAILED)
        throw std::system_error(errno, std::system_category(), "mmap");
      const auto rawaddr = reinterpret_cast<uintptr_t>(raw);
      const size_t prefix =
          (hugepage_size - (rawaddr % hugepage_size)) % hugepage_size;
      base_ = raw + prefix;
      if (prefix > 0) ::munmap(raw, prefix);
      const size_t suffix = reserve_size - prefix - slab_size;
      if (suffix > 0) ::munmap(base_ + slab_size, suffix);
      // Attempt to enable huge page backing on the aligned region. This is a
      // best-effort optimization; if it fails, we still have a correctly sized
      // and aligned block of memory to use.
      (void)::madvise(base_, slab_size, *mmap_advice::hugepage);
    }
    // Warm pages: an explicit memset guarantees zeroing and forces physical
    // page assignment.
    std::memset(base_, 0, slab_size);
    init_free_lists();
  }

  ~iou_buf_pool_of() override {
    if (base_) ::munmap(base_, slab_size);
  }

  iou_buf_pool_of(const iou_buf_pool_of&) = delete;
  iou_buf_pool_of& operator=(const iou_buf_pool_of&) = delete;
  iou_buf_pool_of(iou_buf_pool_of&&) = delete;
  iou_buf_pool_of& operator=(iou_buf_pool_of&&) = delete;

  // Construct a heap-allocated pool owned by `std::shared_ptr`.
  [[nodiscard]] static std::shared_ptr<iou_buf_pool_of> create() {
    return std::make_shared<iou_buf_pool_of>(allow::ctor);
  }

  // Register the backing block as a single fixed buffer (index 0) with
  // `ring`. Must be called exactly once before any buffer is used in an I/O
  // submission. Returns false (and sets `errno`) on failure. The `ring`
  // unregisters on destruction.
  [[nodiscard]] bool register_with(iou_ring& ring) noexcept {
    iovec iov{base_, slab_size};
    return ring.register_buffers(&iov, 1);
  }

#pragma endregion
#pragma region Buffers

  // Borrow a buffer for an incoming read. Returns an empty buffer if:
  //   - the pool has fewer than `write_reserve_size` bytes free after this
  //   alloc, or
  //   - in-flight read bytes would exceed `read_throttle_size`.
  // Thread-safe.
  [[nodiscard]] buffer borrow_reader(block_size sz = block_size::kb004) {
    std::scoped_lock lock{mutex_};
    const auto len = *sz;
    if (available_bytes_ < write_reserve_size + len) return {};
    if (in_flight_read_bytes_ + len > read_throttle_size) return {};
    span_t s{alloc_block(len), len};
    if (!s.data()) return {};
    available_bytes_ -= len;
    in_flight_read_bytes_ += len;
    return make_buffer(this->shared_from_this(), s, 0U, block_type::read);
  }

  // Borrow a buffer for an outgoing write. Not subject to read backpressure;
  // may draw from the write reserve. Returns an empty buffer if fully
  // exhausted. Thread-safe.
  [[nodiscard]] buffer borrow_writer(block_size sz = block_size::kb004) {
    std::scoped_lock lock{mutex_};
    span_t s{alloc_block(*sz), *sz};
    if (!s.data()) return {};
    available_bytes_ -= s.size();
    return make_buffer(this->shared_from_this(), s, 0U, block_type::write);
  }

  // Total free bytes currently in the pool. Thread-safe.
  [[nodiscard]] size_t available() const noexcept {
    std::scoped_lock lock{mutex_};
    return available_bytes_;
  }

#pragma endregion
#pragma region Overrides
private:
  [[nodiscard]] bool
  return_buffer(span_t s, block_type blockrw) noexcept override {
    if (!s.data()) return false;
    if (std::scoped_lock lock{mutex_}; true) {
      assert(available_bytes_ + s.size() <= slab_size);
      (void)return_block(s.data(), s.size());
      available_bytes_ += s.size();
    }
    if (blockrw == block_type::read) (void)decrement_read_bytes(s.size());
    return true;
  }

  [[nodiscard]] ptr base() const noexcept override { return base_; }

  [[nodiscard]] bool decrement_read_bytes(size_t n) noexcept override {
    [[maybe_unused]] const auto old = in_flight_read_bytes_ -= n;
    assert(old < read_throttle_size);
    return true;
  }

  [[nodiscard]] bool increment_read_bytes(size_t n) noexcept override {
    in_flight_read_bytes_ += n;
    return true;
  }

#pragma endregion
#pragma region Helpers

  // Assign tier sizes and push a single full-size block to the top tier.
  void init_free_lists() noexcept {
    for (size_t i = 0; i < tier_count; ++i) lists_[i].sz = min_block_size << i;
    available_bytes_ = slab_size;
    lists_.back().push_head(base_);
  }

  // Page index of the `min_block_size`-sized page at address `p`.
  [[nodiscard]] size_t find_page_index(cptr p) const noexcept {
    return (p - base_) / min_block_size;
  }

  // Mark pages as externally allocated (`in_use==true`) or free.
  void mark_pages(cptr p, size_t sz, bool in_use) noexcept {
    const size_t first = find_page_index(p);
    const size_t count = sz / min_block_size;
    for (size_t i = 0; i < count; ++i) in_use_pages_[first + i] = in_use;
  }

  // True if no page in `[p, p+sz)` is currently allocated externally.
  [[nodiscard]] bool are_all_free(cptr p, size_t sz) const noexcept {
    const size_t first = find_page_index(p);
    const size_t count = sz / min_block_size;
    for (size_t i = 0; i < count; ++i)
      if (in_use_pages_[first + i]) return false;
    return true;
  }

  // Map `sz` to the smallest tier whose block size covers `sz` bytes.
  [[nodiscard]] static constexpr size_t find_tier(size_t sz) noexcept {
    if (sz == 0 || sz > slab_size) return tier_count;
    return std::bit_width((sz - 1) / min_block_size);
  }

  // Pop one block from `src` and push two half-size children to `dst`.
  // Ascending: low-address child becomes head when `dst` is empty (for
  // small-tier allocations that cluster at low addresses).
  // Descending (default): high-address child becomes head (for large-tier
  // allocations that cluster at high addresses).
  static void
  split_down(free_list& src, free_list& dst, bool ascending = false) noexcept {
    ptr p = src.pop_tail();
    assert(p);
    // The two children are at `p` and `p + dst.sz`. The parent block is
    // removed from `src`, and both children are pushed to `dst` in the
    // direction appropriate to the destination tier's zone heuristic.
    if (ascending) {
      dst.push_tail(p);
      dst.push_tail(p + dst.sz);
    } else {
      dst.push_tail(p + dst.sz);
      dst.push_tail(p);
    }
  }

  // Scan `src` for two buddy blocks whose combined parent range is entirely
  // free in the bitmap. Removes both from `src`, pushes the parent to `dst`.
  // Alignment of `base_` to `hugepage_size` guarantees that masking any child
  // address to `~(dst.sz - 1)` yields a valid in-pool parent address.
  [[nodiscard]] bool coalesce(free_list& src, free_list& dst) noexcept {
    for (auto* node = src.head; node; node = node->next) {
      assert(node->is_valid());
      // The clang-tidy warning is spurious.
      // NOLINTBEGIN(performance-no-int-to-ptr)
      ptr parent = reinterpret_cast<ptr>(
          reinterpret_cast<uintptr_t>(node) & ~(dst.sz - 1));
      // NOLINTEND(performance-no-int-to-ptr)
      if (!are_all_free(parent, dst.sz)) continue;
      src.remove(parent);
      src.remove(parent + src.sz);
      dst.push_tail(parent);
      return true;
    }
    return false;
  }

  // Ensure `lists_[tier]` is non-empty. First attempts a top-down split from
  // any higher tier that already has a block; falls back to a bottom-up
  // coalesce cascade when the pool is fragmented. Returns `false` if
  // impossible.
  [[nodiscard]] bool ensure_tier(size_t tier) noexcept {
    if (lists_[tier]) return true;

    // Top-down: find the lowest non-empty higher tier and cascade splits down.
    for (size_t h = tier + 1; h < tier_count; ++h) {
      if (!lists_[h]) continue;
      for (size_t t = h; t > tier; --t)
        split_down(lists_[t], lists_[t - 1], (t - 1 < tier_count / 2));
      return true;
    }

    // Bottom-up: cascade coalesce from tier 0 upward to fill the target.
    for (size_t t = 1; t <= tier; ++t) {
      while (coalesce(lists_[t - 1], lists_[t]))
        if (lists_[tier]) return true;
    }
    return false;
  }

  // Allocate one block covering `sz` bytes. Returns `nullptr` if the pool is
  // exhausted for that size class, even after splitting or coalescing. Caller
  // holds `mutex_`.
  [[nodiscard]] ptr alloc_block(size_t sz) noexcept {
    const size_t tier = find_tier(sz);
    if (tier >= tier_count || !ensure_tier(tier)) return nullptr;
    ptr p = lists_[tier].pop_head();
    if (p) mark_pages(p, lists_[tier].sz, true);
    return p;
  }

  // Return a block to its tier free-list. Coalescing is deferred to alloc
  // time. Must be executed under lock.
  [[nodiscard]] bool return_block(ptr p, size_t sz) noexcept {
    mark_pages(p, sz, false);
    lists_[find_tier(sz)].push_head(p);
    return true;
  }

#pragma endregion
#pragma region Data members
private:
  ptr base_{};
  mutable std::mutex mutex_;
  std::array<free_list, tier_count> lists_;
  size_t available_bytes_{};
  relaxed_atomic_size_t in_flight_read_bytes_;
  // One bit per `min_block_size` page.
  fixed_bitset<slab_size / min_block_size> in_use_pages_;
#pragma endregion
};

using iou_buf_pool = iou_buf_pool_of<>;

#pragma endregion

}}} // namespace corvid::proto::iouring
