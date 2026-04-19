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
class iou_buf_pool {
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

  using span_t = iou_sqe::span_t;
  using const_span_t = iou_sqe::const_span_t;
#pragma endregion
#pragma region Buffer handle
public:
  // Moveable RAII handle to a single slab allocation. Returns its memory to
  // the pool on destruction or `reset()`. `buf_index()` is always 0;
  // `offset()` gives the byte distance from the huge page base for
  // file-offset SQE fields.
  //
  // These spans define the buffer state:
  //   `full_span`     -- entire allocation; never changes after construction.
  //   `payload_span`  -- data region (bytes received for reads, bytes to send
  //                      for writes). Starts empty.
  //   `active_span`   -- what gets passed to the kernel for the next I/O op.
  //                      For reads: the writable tail after `payload_span`.
  //                      For writes: the unsent portion of `payload_span`.
  //   `tail_span`     -- the writable tail after `payload_span`; a derived
  //                      value.
  //
  // Read lifecycle:
  //   1. Allocate: `payload_span` empty at start of `full_span`;
  //      `active_span` = entire `full_span`.
  //   2. Submit recv SQE using `active_span`.
  //   3. On completion, call `update(res)`: extends `payload_span` by bytes
  //      read; `active_span` becomes the new tail.
  //   4. Read via `payload_span` / `payload_view`. Can discard now.
  //   5. Optionally consume incrementally with `consume_read`. Full drain
  //   resets to step 1. Can discard now.
  //   6. Optionally submit recv SQE again to read more into the tail, then
  //   repeat until done.
  //   7. Optionally `promote_to_write` to proxy received data.
  //
  // Write lifecycle:
  //   1. Allocate: both `payload_span` and `active_span` empty at start of
  //      `full_span`.
  //   2. Fill via `append` or via `tail_span` + `update_payload`.
  //   3. Submit send SQE using `active_span`.
  //   4. On completion, call `update`: advances `active_span` front by
  //      bytes sent. If fully sent, buffer enters consumed state.
  //   5. In consumed state, the next `append`, `tail_span`, or
  //      `update_payload` implicitly resets to step 1.
  //   6. Can discard now.
  //   7. Optionally `demote_to_read` to receive more data into the tail.
  class buffer {
  public:
    using span_t = iou_buf_pool::span_t;
    using const_span_t = iou_buf_pool::const_span_t;

    buffer() = default;
    ~buffer() { reset(); }

    buffer(buffer&& o) noexcept
        : pool_{std::exchange(o.pool_, nullptr)},
          full_span_{std::exchange(o.full_span_, {})},
          payload_span_{std::exchange(o.payload_span_, {})},
          active_span_{std::exchange(o.active_span_, {})}, res_{o.res_},
          is_read_{o.is_read_} {}

    buffer& operator=(buffer&& o) noexcept {
      if (this != &o) {
        reset();
        pool_ = std::exchange(o.pool_, nullptr);
        full_span_ = std::exchange(o.full_span_, {});
        payload_span_ = std::exchange(o.payload_span_, {});
        active_span_ = std::exchange(o.active_span_, {});
        res_ = o.res_;
        is_read_ = o.is_read_;
      }
      return *this;
    }

    // Copyable enough to satisfy `std::function`, but throws if you actually
    // try to copy it. This will no longer be necessary once
    // `std::move_only_function` becomes available.
    buffer(const buffer&) {
      throw std::logic_error("iou_buf_pool::buffer is not copyable");
    }
    buffer& operator=(const buffer&) = delete;

    // Check for validity when a `buffer` has been allocated from the pool.
    [[nodiscard]] explicit operator bool() const noexcept { return pool_; }

    // Access the result of the I/O operation; initially an error condition.
    [[nodiscard]] iou_res result() const noexcept { return res_; }

    // Span of accumulated payload data. For reads, this is bytes received so
    // far; for writes, bytes being sent.
    [[nodiscard]] span_t payload_span() noexcept { return payload_span_; }

    // String view over `payload_span`.
    [[nodiscard]] std::string_view payload_view() noexcept {
      return {reinterpret_cast<char*>(payload_span_.data()),
          payload_span_.size()};
    }

    // Consume up to `n` bytes from the front of the payload. Returns the
    // taken slice. Fully draining the payload resets the buffer to its
    // initial read state (empty payload, active = full block).
    [[nodiscard]] span_t consume_read(size_t n) noexcept {
      assert(is_read_);
      const size_t take = std::min(n, payload_span_.size());
      span_t result{payload_span_.data(), take};
      payload_span_ = payload_span_.subspan(take);
      if (payload_span_.empty()) {
        payload_span_ = {full_span_.data(), 0};
        active_span_ = full_span_;
      }
      return result;
    }

    // Returns the region of `full_span` after `payload_span` ends, for use
    // with the manual `tail_span` + `update_payload` fill pattern. Implicitly
    // resets to initial write state if the buffer is consumed.
    [[nodiscard]] span_t tail_span() noexcept {
      assert(!is_read_);
      if (do_is_fully_consumed()) do_reset_write_spans();
      std::byte* end = payload_span_.data() + payload_span_.size();
      return {end,
          static_cast<size_t>(full_span_.data() + full_span_.size() - end)};
    }

    // Extend `payload_span` and `active_span` to cover `payload`. The
    // start of `payload` must equal the current end of `payload_span`
    // (i.e. it must be the span returned by `tail_span()`, trimmed to the
    // bytes actually written). The end of `payload` must not exceed
    // `full_span`. Returns false on violation; spans are left unchanged.
    // Implicitly resets if the buffer is in consumed state before extending.
    [[nodiscard]] bool update_payload(span_t payload) noexcept {
      assert(!is_read_);
      auto tail = tail_span();
      if (payload.data() != tail.data()) return false;
      if (payload.size() > tail.size()) return false;
      payload_span_ = {payload_span_.data(),
          payload_span_.size() + payload.size()};
      active_span_ = {active_span_.data(),
          active_span_.size() + payload.size()};
      return true;
    }

    // Copy `more` to the tail of `payload_span_` and extend both
    // `payload_span_` and `active_span_` by that amount. Returns false
    // without modifying anything if `more` would not fit in the remaining
    // tail. Implicitly resets if the buffer is in consumed state first.
    [[nodiscard]] bool append(const_span_t more) noexcept {
      assert(!is_read_);
      auto tail = tail_span();
      if (more.size() > tail.size()) return false;
      std::memcpy(tail.data(), more.data(), more.size());
      payload_span_ = {payload_span_.data(),
          payload_span_.size() + more.size()};
      active_span_ = {active_span_.data(), active_span_.size() + more.size()};
      return true;
    }

    [[nodiscard]] bool append(std::string_view sv) noexcept {
      return append(const_span_t{reinterpret_cast<const std::byte*>(sv.data()),
          sv.size()});
    }

    // Update with the result of an I/O completion.
    //   Read mode: extends `payload_span` by bytes read; `active_span`
    //     becomes the new tail (space remaining for further reads).
    //   Write mode: advances `active_span` front by bytes sent; when fully
    //     sent, `active_span` becomes zero-length (consumed state).
    // On error `res`, spans are left unchanged. Returns self for chaining.
    buffer& update(iou_res res) noexcept {
      res_ = res;
      if (!res.ok()) return *this;
      if (is_read_) {
        const size_t extend = std::min(res.bytes(),
            full_span_.size() -
                static_cast<size_t>(payload_span_.data() - full_span_.data()) -
                payload_span_.size());
        payload_span_ = {payload_span_.data(), payload_span_.size() + extend};
        auto* end = payload_span_.data() + payload_span_.size();
        active_span_ = {end,
            static_cast<size_t>(full_span_.data() + full_span_.size() - end)};
      } else {
        const size_t sent = std::min(res.bytes(), active_span_.size());
        active_span_ = active_span_.subspan(sent);
      }
      return *this;
    }

    // Promote this read buffer to write mode. `payload_span` is kept as-is
    // (the received bytes become the write payload); `active_span` is set to
    // `payload_span` (so the next send transmits exactly what was read).
    // Decrements the pool's in-flight read byte count for the full block.
    buffer& promote_to_write() noexcept {
      assert(is_read_);
      pool_->do_decrement_read_bytes(full_span_.size());
      active_span_ = payload_span_;
      is_read_ = false;
      return *this;
    }

    // Demote this write buffer to read mode. `payload_span` is kept as-is;
    // `active_span` becomes the tail (space after `payload_span` for
    // additional incoming data).
    buffer& demote_to_read() noexcept {
      assert(!is_read_);
      auto* end = payload_span_.data() + payload_span_.size();
      active_span_ = {end,
          static_cast<size_t>(full_span_.data() + full_span_.size() - end)};
      is_read_ = true;
      return *this;
    }

    // Byte size of this allocation: 4096, 16384, or 65536.
    [[nodiscard]] size_t size() const noexcept { return full_span_.size(); }

    // Always 0: the entire 2 MB page is registered as a single buffer entry.
    // NOTE: When we factor out this class for use with Provide Buffers.
    [[nodiscard]] static size_t buf_index() noexcept { return 0; }

    // Byte offset from the huge page base.
    [[nodiscard]] uint64_t offset() const noexcept {
      return static_cast<uint64_t>(full_span_.data() - pool_->base_);
    }

    // Span of the entire buffer. Not generally useful outside of
    // `iou_buf_pool`.
    [[nodiscard]] span_t full_span() noexcept { return full_span_; }

    // Span for the next kernel I/O submission. For read buffers this is the
    // writable tail; for write buffers this is the unsent portion. Not
    // generally useful outside of `io_loop`.
    [[nodiscard]] span_t active_span() noexcept { return active_span_; }

    // Reset the I/O result manually. Not generally useful outside of
    // `io_loop`.
    void reset_result(iou_res res = iou_res{-1}) noexcept { res_ = res; }

    // Whether this buffer is in read mode. Not generally useful externally.
    [[nodiscard]] bool is_read() const noexcept { return is_read_; }

    // Return the allocation to the pool immediately; buffer becomes empty.
    void reset() noexcept {
      if (!pool_) return;
      pool_->do_return(full_span_, is_read_);
      pool_ = nullptr;
      full_span_ = {};
      payload_span_ = {};
      active_span_ = {};
      res_ = iou_res{-1};
    }

  private:
    friend class iou_buf_pool;

    buffer(iou_buf_pool& pool, span_t span, bool is_read) noexcept
        : pool_{&pool}, full_span_{span}, payload_span_{span.data(), 0},
          active_span_{span.data(), 0}, res_{-1}, is_read_{is_read} {
      if (is_read_) active_span_ = full_span_;
    }

    // True when the write buffer has been fully sent (or is initial-empty).
    // Both states are treated identically: the next write operation resets.
    [[nodiscard]] bool do_is_fully_consumed() const noexcept {
      assert(!is_read_);
      return active_span_.size() == 0 &&
             active_span_.data() >=
                 payload_span_.data() + payload_span_.size();
    }

    void do_reset_write_spans() noexcept {
      assert(!is_read_);
      payload_span_ = {full_span_.data(), 0};
      active_span_ = {full_span_.data(), 0};
    }

    iou_buf_pool* pool_{};
    span_t full_span_;
    span_t payload_span_;
    span_t active_span_;
    iou_res res_;
    bool is_read_{};
  };
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

  ~iou_buf_pool() {
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
    return {*this, s, true};
  }

  // Borrow a buffer for an outgoing write. Not subject to read backpressure;
  // may draw from the write reserve. Returns an empty buffer if fully
  // exhausted. Thread-safe.
  [[nodiscard]] buffer borrow_writer(
      block_size sz = block_size::small) noexcept {
    std::lock_guard lock{mutex_};
    const auto len = static_cast<size_t>(sz);
    void* p = do_alloc_block(len);
    if (!p) return {};
    available_bytes_ -= len;
    return {*this, span_t{static_cast<std::byte*>(p), len}, false};
  }

  // Total free bytes currently in the pool. Thread-safe.
  [[nodiscard]] size_t available() const noexcept {
    std::lock_guard lock{mutex_};
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

  void do_return(span_t s, bool is_read) noexcept {
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
    if (is_read) do_decrement_read_bytes(s.size());
  }

  void do_decrement_read_bytes(size_t n) noexcept {
    in_flight_read_bytes_.fetch_sub(n, std::memory_order::relaxed);
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
}}} // namespace corvid::proto::iouring
