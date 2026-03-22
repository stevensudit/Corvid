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
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "../concurrency/relaxed_atomic.h"
#include "../strings/no_zero.h"

namespace corvid { inline namespace proto {

using namespace corvid::strings::no_zero_funcs;

// Persistent flat receive buffer owned by a connection. The framework
// (polling thread) appends bytes after `end`; the parser (any thread)
// consumes bytes from `begin`. Both indexes are `std::atomic<size_t>`, so if
// data gets added between the time the view was delivered and the parser
// (perhaps in a worker thread) consumes it, it's visible.
//
// A `std::string buffer` is used, with its size linked to its capacity.
//
// Memory ordering:
//   `end`:   release-store after bytes land in `buffer`; acquire-load in
//            `active` and `recv_buffer_view::active_view`.
//   `begin`: release-store in `recv_buffer_view::consume`; acquire-load
//            in `active` and `active_view`.
//   `reads_enabled`: loop-thread-only, no atomics.
struct recv_buffer {
  // Backing storage. `buffer.size() == buffer.capacity`. `end` tracks the
  // written extent; `begin` tracks the consumed extent.
  std::string buffer;

  // Start of unconsumed data. Updated by the parser via `consume`. Never
  // changed by the framework while a view is extant.
  std::atomic_size_t begin{0};

  // One-past the end of unconsumed data. Updated by the framework after
  // each recv, even while a view is extant.
  std::atomic_size_t end{0};

  // System of record for EPOLLIN desire. Loop-thread-only (not atomic).
  // Consulted by `stream_conn::wants_read_events`. May start `false` on
  // connections that should not begin reading until explicitly armed (e.g.,
  // accepted sockets that inherit a disabled-reads listener).
  bool reads_enabled{true};

  // True while a `recv_buffer_view` is live. Loop-thread-only. When set,
  // `handle_readable` still recvs into the buffer (extending `end`
  // atomically) but suppresses the `on_data` dispatch. The in-flight parser
  // holds the view and will observe new bytes on its next `active_view`
  // call.
  bool view_active{false};

  // Minimum capacity to maintain when compacting. This is used to avoid
  // repeatedly enlarging and shrinking the buffer when parsing frames of
  // varying sizes. The parser can call `set_buffer_size` to request a larger
  // buffer for the next read if it determines that the current capacity is
  // insufficient for the next frame, and `compact` will respect that request.
  // However, it will be shrunk down to `min_capacity` once it's no longer
  // needed.
  relaxed_atomic_size_t min_capacity;

  // Initialize (or reinitialize) backing storage to `capacity` bytes without
  // zero-init. Resets `begin` and `end` to zero. Only safe when EPOLLIN is
  // disabled and no `recv_buffer_view` is live.
  void resize(size_t capacity) {
    assert(!reads_enabled);
    assert(!view_active);
    no_zero::enlarge_to(buffer, capacity);
    begin.store(0, std::memory_order::relaxed);
    end.store(0, std::memory_order::relaxed);
  }

  // Acquire-load `begin` and `end`; return a view of the currently active
  // (unconsumed) region. More data may arrive after this call and extend
  // `end`; subsequent calls may therefore return a larger region.
  [[nodiscard]] std::string_view active() const {
    const size_t b = begin.load(std::memory_order::acquire);
    const size_t e = end.load(std::memory_order::acquire);
    assert(b <= e);
    return {buffer.data() + b, e - b};
  }

  // Bytes available for writing after `end`.
  [[nodiscard]] size_t write_space() const noexcept {
    return buffer.size() - end.load(std::memory_order::relaxed);
  }

  // Optionally compact the receive buffer and optionally resize it.
  //
  // `target` is the parser's requested minimum capacity (0 = no request).
  // The effective resize size is determined as follows:
  //   - If `target` exceeds the current capacity: grow to `target`.
  //   - If `target == 0` and the buffer has bloated beyond 2x `min_capacity`
  //     (e.g., after a one-off large `expand_to()`): shrink back to
  //     `min_capacity`, but only when all active data fits.
  //   - If `target == 0` and the buffer is below `min_capacity` (e.g.,
  //     `set_recv_buf_size` raised the target): grow to `min_capacity`.
  //   - Otherwise: leave capacity unchanged.
  //
  // When a resize is required, active bytes are always copied to the front of
  // the new buffer. When no resize is needed and there are no active bytes,
  // `begin` and `end` are reset to 0 (cheap reset). When no resize is needed
  // and active bytes remain, the memmove is skipped unless one of the
  // following conditions holds:
  //   - Must: write space is exhausted but `begin > 0` (bytes before the
  //     active region can be reclaimed).
  //   - Worth it: `begin` is past the 1/4 mark and `end` is past the 3/4
  //     mark (a short `recv` would likely exhaust write space and force a
  //     compaction after the next `on_data` call anyway).
  // If neither condition holds, `begin` and `end` are left unchanged.
  //
  // Only safe to call within the polling thread (which can't be asserted on
  // here) and when no `recv_buffer_view` is live.
  void compact(size_t target = 0) {
    assert(!view_active);
    const size_t b = begin.load(std::memory_order::relaxed);
    const size_t e = end.load(std::memory_order::relaxed);
    assert(e >= b); // `end` can never precede `begin`
    const size_t active_len = e - b;

    // Determine the effective resize target.
    const size_t configured = min_capacity;
    const size_t current = buffer.capacity();
    size_t new_size = current;

    // When no target is specified,  shrinking is possible.
    if (target == 0) {
      if (active_len <= configured && current > 2 * configured)
        // shrink: all active data fits in configured
        new_size = configured;
      else if (current < configured)
        // grow: `set_recv_buf_size` increased the target
        new_size = configured;
    } else if (target > current)
      // grow: parser requested more capacity
      new_size = target;

    if (new_size != current) {
      // Resize: always move active bytes to the front of the new buffer.
      std::string new_buf;
      no_zero::resize_to(new_buf, new_size);
      if (active_len > 0)
        std::memcpy(new_buf.data(), buffer.data() + b, active_len);
      buffer = std::move(new_buf);
      // If we grew to meet `min_capacity`, sync it to the actual post-resize
      // capacity, which the allocator may have rounded upward. Only overwrite
      // if `min_capacity` is still `configured` -- a concurrent
      // `set_recv_buf_size` call may have already updated it to a larger
      // value.
      if (target == 0 && new_size > current) {
        auto expected = configured;
        min_capacity->compare_exchange_strong(expected, buffer.capacity(),
            std::memory_order::relaxed, std::memory_order::relaxed);
      }
    } else if (active_len > 0) {
      // Active bytes remain. Move them only when necessary or worth it.
      // Note that, by dividing before multiplying, we avoid potential overflow
      // at the cost of a small amount of imprecision when `current` is not
      // divisible by 4. As this is a heuristic anyway, and sizes are quite
      // likely powers of two, this is fine.
      const bool must = (e == current && b > 0);
      const bool worth_it = (b > current / 4 && e > current / 4 * 3);
      if (!must && !worth_it) return;
      std::memmove(buffer.data(), buffer.data() + b, active_len);
    }

    begin.store(0, std::memory_order::release);
    end.store(active_len, std::memory_order::release);
  }
};

// Limited-interface token handed to the parser via the `on_data` callback.
// At most one `recv_buffer_view` is live at a time for a given connection.
//
// `active_view` acquire-loads `begin` and `end` and returns a snapshot of
// at least the currently unconsumed region. If running outside the polling
// thread, more bytes may arrive while the view is live.
//
// `consume(n)` advances `begin` by `n` bytes (release-store).
//
// The destructor calls `resume_cb_(new_buffer_size_)`, which posts all
// recovery work (compact, re-enable reads, maybe re-dispatch) to the loop.
// The `resume_cb_` captures a `std::shared_ptr` to the owning connection,
// keeping the connection (and therefore this buffer) alive for the duration
// of the view.
//
// Non-copyable (one active parser at a time), movable.
class recv_buffer_view {
public:
  // Construct a view over `buf`. `resume_cb` is called from the destructor
  // with the requested new buffer size (0 = no change). It should capture a
  // `std::shared_ptr` to the owning connection to keep the connection alive
  // while this view is live.
  recv_buffer_view(recv_buffer& buf, std::function<void(size_t)> resume_cb)
      : buf_{&buf}, resume_cb_{std::move(resume_cb)} {}

  recv_buffer_view(const recv_buffer_view&) = delete;
  recv_buffer_view& operator=(const recv_buffer_view&) = delete;

  recv_buffer_view(recv_buffer_view&& o) noexcept
      : buf_{std::exchange(o.buf_, nullptr)},
        resume_cb_{std::exchange(o.resume_cb_, {})},
        new_buffer_size_{o.new_buffer_size_} {}

  recv_buffer_view& operator=(recv_buffer_view&& o) noexcept {
    if (this != &o) {
      buf_ = std::exchange(o.buf_, nullptr);
      resume_cb_ = std::exchange(o.resume_cb_, {});
      new_buffer_size_ = o.new_buffer_size_;
    }
    return *this;
  }

  // Destructor: calls `resume_cb_(new_buffer_size_)` to post compact /
  // re-enable-reads / optional re-dispatch back to the loop.
  // NOLINTBEGIN(bugprone-exception-escape)
  ~recv_buffer_view() {
    if (buf_) resume_cb_(new_buffer_size_);
  }
  // NOLINTEND(bugprone-exception-escape)

  // Acquire-load `begin` and `end`; return a snapshot of at least the
  // currently active bytes. Subsequent calls from an async parser may return
  // a larger view as the framework appends more data.
  [[nodiscard]] std::string_view active_view() const {
    assert(buf_);
    return buf_->active();
  }

  // Implicit conversion to `std::string_view`.
  operator std::string_view() const { return active_view(); }

  // Full capacity of the backing buffer. Use this to detect frames that
  // cannot fit even after compaction. If the frame exceeds `buffer_size`,
  // either copy out to your own accumulation buffer or call `expand_to`
  // before the view destructs to request enlargement.
  [[nodiscard]] size_t buffer_size() const noexcept {
    assert(buf_);
    return buf_->buffer.capacity();
  }

  // Request that the buffer grow to at least `n` bytes on the next compact.
  // Takes effect when the destructor invokes the callback.
  void expand_to(size_t n) noexcept {
    assert(buf_);
    new_buffer_size_ = std::max(new_buffer_size_, n);
  }

  // Advance `begin` by `n` bytes (release-store fetch_add).
  void consume(size_t n) {
    assert(buf_);
    buf_->begin.fetch_add(n, std::memory_order::release);
  }

  // Wrapper for `consume`. Pass the unconsumed tail of an `active_view`
  // snapshot after advancing it past parsed content.
  void update_active_view(std::string_view remaining) {
    assert(buf_);
    const size_t b = buf_->begin.load(std::memory_order::relaxed);
    const char* base = buf_->buffer.data() + b;
    assert(remaining.data() >= base);
    const auto consumed = static_cast<size_t>(remaining.data() - base);
    if (consumed > 0) consume(consumed);
  }

private:
  recv_buffer* buf_;                      // non-owning; nulled on move
  std::function<void(size_t)> resume_cb_; // keeps connection alive
  size_t new_buffer_size_{};              // 0 = no growth; set via `expand_to`
};

}} // namespace corvid::proto
