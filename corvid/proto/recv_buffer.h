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
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

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

  // Move active bytes to front of `buffer` and optionally enlarge.
  //
  // If `begin == end`: cheap compact (no copy), reset both to 0.
  // Otherwise: copy or memmove active bytes to `buffer[0]`, reset `begin=0`,
  // `end=active_len`.
  //
  // `target == 0`: leave capacity unchanged.
  // `target > 0`: ensure capacity meets or exceeds `target`.
  //
  // The reason why we want to potentially increase capacity during compaction
  // is that the parser may determine that, even with every byte of the buffer
  // available, the full frame still won't fit. In that case, rather than
  // forcing the parser to store partial frames and concatenate them, we might
  // as well.
  //
  // Only safe when EPOLLIN is disabled and no `recv_buffer_view` is live
  // (i.e., called from within the `resume_receive` execute_or_post
  // lambda). Release-stores `begin = 0` and `end = active_len` after the
  // move.
  void compact(size_t target = 0) {
    const size_t b = begin.load(std::memory_order::relaxed);
    const size_t e = end.load(std::memory_order::relaxed);
    assert(e >= b); // `end` can never precede `begin`
    const size_t active_len = e - b;

    // TODO: Ideally, the parser will be greedy, extracting as many full frames
    // as it can, and leaving only the last partial frame in the buffer. In
    // that case, the active area will be small and moving it will be cheap.
    // Still, we shouldn't compact after every parse. A better plan is to only
    // compact when:
    // 1. We have to: there's no space past the end but there's space in front.
    // 2. It's worth it: if the start of the active region is past the 1/4
    // point and the end is past the 3/4 point, it makes sense to compact now
    // so as to avoid a short recv that forces us to compact anyhow, and then
    // only after the parser has wasted time checking to see if a full frame is
    // available. (Side note: If the parser checks for the frame using a
    // prefix, it can be very cheap. If it has to search for a sentinel, that
    // could be expensive, but there's no reason why it can't keep a high-water
    // mark to avoid re-scanning the same data after a short recv. It just has
    // to start where it left off, minus the length of the sentinel.)
    // 3. When we're enlarging. No reason to move the active data to anywhere
    // but the beginning.

    // TODO: !!! Move recv_buf_capacity_ out of stream_conn and into
    // recv_buffer

    // TODO: Look at `recv_buf.recv_buf_capacity_` to detect when we've grown
    // well beyond it and then shrink back down when the parser is done with
    // the large frame. Otherwise, a single large frame could cause us to
    // permanently consume much more memory than our configured target.
    // Essentially, when `target == 0`, and we've doubled the capacity, and
    // we're using no more than half of the buffer, go back to the normal size.
    //
    if (target > buffer.size()) {
      std::string new_buf;
      no_zero::resize_to(new_buf, target);
      if (active_len > 0)
        std::memcpy(new_buf.data(), buffer.data() + b, active_len);
      buffer = std::move(new_buf);
    } else if (active_len > 0) {
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
    if (n > new_buffer_size_) new_buffer_size_ = n;
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
    const size_t consumed = static_cast<size_t>(remaining.data() - base);
    if (consumed > 0) consume(consumed);
  }

private:
  recv_buffer* buf_;                      // non-owning; nulled on move
  std::function<void(size_t)> resume_cb_; // keeps connection alive
  size_t new_buffer_size_{};              // 0 = no growth; set via `expand_to`
};

}} // namespace corvid::proto
