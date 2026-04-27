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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "iou_wrap.h"
#include "../../meta/forwarding_address.h"

namespace corvid { inline namespace proto { namespace iouring {
#pragma region buffer_pool_base

// Fwd.
class iou_buffer;

// Abstract backing pool used by `iou_buffer`.
class buffer_pool_base {
public:
  using span_t = iou_sqe::span_t;
  using const_span_t = iou_sqe::const_span_t;

  virtual ~buffer_pool_base() = default;

private:
  friend class iou_buffer;

  // Return base address of the pool's memory region.
  [[nodiscard]] virtual std::byte* base() const noexcept = 0;

  // Return a buffer to the pool.
  virtual void return_buffer(span_t s, bool is_read) noexcept = 0;

  // Track reads bytes separately, to selectively throttle.
  virtual void decrement_read_bytes(size_t n) noexcept = 0;
  virtual void increment_read_bytes(size_t n) noexcept = 0;

protected:
  [[nodiscard]] static iou_buffer make_buffer(buffer_pool_base& pool,
      span_t span, size_t buf_index, bool is_read) noexcept;

  // TODO: We'll need a way to make Provided Buffers programmatically
  // detectable. What we really want is for the regular flow, where the user
  // tries to append to the buffer, to fail as though the buffer were full.
  // Perhaps it should just return and empty active_buffer in that case.
};

#pragma endregion
#pragma region iou_buffer

// Moveable RAII handle to a single slab allocation. Returns its memory to the
// pool on destruction or `reset()`. `offset()` gives the byte distance from
// the pool base for file-offset SQE fields.
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
//   4. On completion, call `update`: advances `active_span` front by bytes
//      sent. If fully sent, buffer enters consumed state.
//   5. In consumed state, the next `append`, `tail_span`, or
//      `update_payload` implicitly resets to step 1.
//   6. Can discard now.
//   7. Optionally `demote_to_read` to receive more data into the tail.
//
// There are some features to this class that you must understand in order to
// safely modify it. The first is that it is technically copyable, but any
// attempt throws. This is necessary to satisfy `std::function`.
//
// The second is also related to being bound into `std::function`. Inheriting
// `address_forwarder<iou_buffer>` allows a caller to register an external
// `iou_buffer*` via `forwarding_address()`; it is updated on every move and
// nulled on destruction. Clear it once the buffer has settled.
class iou_buffer: public address_forwarder<iou_buffer> {
#pragma region Construction
public:
  using span_t = buffer_pool_base::span_t;
  using const_span_t = buffer_pool_base::const_span_t;
  static constexpr uint64_t seek_current = static_cast<uint64_t>(-1);

  iou_buffer() = default;
  ~iou_buffer() { do_reset(); }

  iou_buffer(iou_buffer&& o) noexcept
      : address_forwarder<iou_buffer>(std::move(o.as_base_move())),
        pool_{std::exchange(o.pool_, nullptr)},
        full_span_{std::exchange(o.full_span_, {})},
        payload_span_{std::exchange(o.payload_span_, {})},
        active_span_{std::exchange(o.active_span_, {})},
        buf_index_{std::exchange(o.buf_index_, {})},
        file_offset_{std::exchange(o.file_offset_, {})},
        pending_releases_{std::exchange(o.pending_releases_, {})},
        is_read_{o.is_read_}, res_{o.res_}, cqe_flags_{o.cqe_flags_} {}

  iou_buffer& operator=(iou_buffer&& o) noexcept {
    if (this != &o) {
      reset();
      address_forwarder<iou_buffer>::operator=(std::move(o.as_base_move()));
      pool_ = std::exchange(o.pool_, nullptr);
      full_span_ = std::exchange(o.full_span_, {});
      payload_span_ = std::exchange(o.payload_span_, {});
      active_span_ = std::exchange(o.active_span_, {});
      buf_index_ = std::exchange(o.buf_index_, {});
      file_offset_ = std::exchange(o.file_offset_, {});
      pending_releases_ = std::exchange(o.pending_releases_, {});
      is_read_ = o.is_read_;
      res_ = o.res_;
      cqe_flags_ = o.cqe_flags_;
    }
    return *this;
  }

  // Copyable enough to satisfy `std::function`, but throws if you actually
  // try to copy it. This will no longer be necessary once
  // `std::move_only_function` becomes available.
  iou_buffer(const iou_buffer& o) : address_forwarder<iou_buffer>(o) {
    throw std::logic_error{"iou_buffer is not copyable"};
  }
  iou_buffer& operator=(const iou_buffer&) = delete;

#pragma endregion
#pragma region Accessors

  // Check for validity when a `buffer` has been allocated from the pool.
  [[nodiscard]] explicit operator bool() const noexcept { return pool_; }

  // Access the result of the I/O operation; initially an error condition.
  [[nodiscard]] iou_res result() const noexcept { return res_; }

  // Access the CQE flags of the I/O operation; initially zero.
  [[nodiscard]] iou_cqe_flags cqe_flags() const noexcept { return cqe_flags_; }

  // Byte size of this allocation: 4096, 16384, or 65536.
  [[nodiscard]] size_t size() const noexcept { return full_span_.size(); }

  // Index of buffer entry. Can be 0 if the entire page is registered
  // as a single buffer entry.
  [[nodiscard]] size_t buf_index() const noexcept { return buf_index_; }

  // Offset into file.
  //
  // For sockets, this must be `seek_current` or `0`. For files, you can also
  // use `seek_current`, in which case it will be automatically advanced by the
  // kernel. Alternately, you can set it explicitly, in which case it will be
  // advanced automatically by the `update` method. See also: `set_read_size`
  // and `seek_read`.
  [[nodiscard]] uint64_t& file_offset() noexcept { return file_offset_; }

  // Byte offset from the pool base.
  [[nodiscard]] size_t pool_base_offset() const noexcept {
    return full_span_.data() - pool_->base();
  }

  // Span of the entire buffer. Not generally useful outside of
  // `iou_buf_pool`.
  [[nodiscard]] span_t full_span() noexcept { return full_span_; }

  // Span for the next kernel I/O submission. For read buffers this is the
  // writable tail; for write buffers this is the unsent portion. Not
  // generally useful outside of `io_loop`.
  [[nodiscard]] span_t active_span() noexcept { return active_span_; }

  // Span of accumulated payload data. For reads, this is bytes received so
  // far; for writes, bytes being sent.
  [[nodiscard]] span_t payload_span() noexcept { return payload_span_; }

  // For zero-copy sends, we need to track when the buffer has been pinned. We
  // increment this when we use it in a SQE and decrement it when a CQE either
  // arrives without `iou_cqe_flags::more`, or a follow-up CQE arrives with
  // `iou_cqe_flags::notify`. Only when this reaches zero can we release the
  // callback, and hence the buffer.
  size_t& pending_releases() noexcept { return pending_releases_; }

  // Access timeout associated with this buffer.
  iou_timespec& timeout() noexcept { return timeout_; }

  // Return the allocation to the pool immediately; buffer becomes empty.
  void reset() noexcept {
    do_reset();
    pool_ = nullptr;
    full_span_ = {};
    payload_span_ = {};
    active_span_ = {};
    buf_index_ = {};
    file_offset_ = {};
    pending_releases_ = {};
    is_read_ = false;
    res_ = iou_res{-1};
    cqe_flags_ = {};
  }

#pragma endregion
#pragma region Payload

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

  // Extend `payload_span` and `active_span` to cover `payload`. Used in a
  // writer after directly appending to the tail.
  //
  // The start of `payload` must equal the current end of `payload_span`
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
    active_span_ = {active_span_.data(), active_span_.size() + payload.size()};
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
    payload_span_ = {payload_span_.data(), payload_span_.size() + more.size()};
    active_span_ = {active_span_.data(), active_span_.size() + more.size()};
    return true;
  }

  [[nodiscard]] bool append(std::string_view sv) noexcept {
    return append(const_span_t{reinterpret_cast<const std::byte*>(sv.data()),
        sv.size()});
  }

  // Set maximum bytes to read.
  //
  //  Set `active_span` to `bytes_to_read`, starting at the current tail.
  //  Returns false without modifying anything if `bytes_to_read` exceeds the
  //  available tail space.
  [[nodiscard]] bool set_read_size(size_t bytes_to_read) noexcept {
    assert(is_read_);
    auto* tail_start = payload_span_.data() + payload_span_.size();
    const auto tail_size = static_cast<size_t>(
        full_span_.data() + full_span_.size() - tail_start);
    if (bytes_to_read > tail_size) return false;
    active_span_ = {tail_start, bytes_to_read};
    return true;
  }

  // Set up a file read from `new_offset` for `bytes_to_read`. Updates
  // `file_offset` and shrinks `active_span_` to exactly `bytes_to_read`,
  // starting at the current tail. Returns false without modifying anything if
  // `bytes_to_read` exceeds the available tail space. See also: `file_offset`.
  [[nodiscard]] bool
  seek_read(uint64_t new_offset, size_t bytes_to_read) noexcept {
    assert(is_read_);
    if (!set_read_size(bytes_to_read)) return false;
    file_offset_ = new_offset;
    return true;
  }

#pragma endregion
#pragma region State

  // Reset the I/O result manually. Not generally useful outside of
  // `io_loop`.
  void reset_result(iou_res res = iou_res{-1},
      iou_cqe_flags cqe_flags = {}) noexcept {
    res_ = res;
    cqe_flags_ = cqe_flags;
  }

  // Result of `prepare` call. Essentially, a named tuple.
  struct prepare_result {
    span_t active_span;
    size_t buf_index;
    uint64_t file_offset;
  };

  // Prepare the buffer for I/O submission. Resets the I/O result and returns
  // the active span, buffer index, and file offset. This is primarily for
  // internal use.
  prepare_result prepare() noexcept {
    reset_result();
    ++pending_releases_;
    return {.active_span = active_span(),
        .buf_index = buf_index(),
        .file_offset = file_offset_};
  }

  // Update with the result of an I/O completion.
  //   Read mode: extends `payload_span` by bytes read; `active_span`
  //     becomes the new tail (space remaining for further reads).
  //   Write mode: advances `active_span` front by bytes sent; when fully
  //     sent, `active_span` becomes zero-length (consumed state).
  // On error `res`, spans are left unchanged. Returns self for chaining.
  iou_buffer& update(iou_res res, iou_cqe_flags cqe_flags) noexcept {
    res_ = res;
    cqe_flags_ = cqe_flags;
    if (!res.ok()) return *this;
    if (file_offset_ != seek_current) file_offset_ += res.bytes();
    if (is_read_) {
      --pending_releases_;
      const size_t extend = std::min(res.bytes(),
          full_span_.size() -
              static_cast<size_t>(payload_span_.data() - full_span_.data()) -
              payload_span_.size());
      payload_span_ = {payload_span_.data(), payload_span_.size() + extend};
      auto* end = payload_span_.data() + payload_span_.size();
      active_span_ = {end,
          static_cast<size_t>(full_span_.data() + full_span_.size() - end)};
    } else {
      assert(pending_releases_ > 0);
      // When no more CQEs, the ZC pin is released.
      if (!bitmask::has(cqe_flags_, iou_cqe_flags::more)) --pending_releases_;
      // If it's not a notification, then we can update the amount written.
      if (!bitmask::has(cqe_flags_, iou_cqe_flags::notif))
        active_span_ =
            active_span_.subspan(std::min(res.bytes(), active_span_.size()));
    }
    return *this;
  }

#pragma endregion
#pragma region Read/Write

  // Promote this read buffer to write mode. `payload_span` is kept as-is
  // (the received bytes become the write payload); `active_span` is set to
  // `payload_span` (so the next send transmits exactly what was read).
  // Decrements the pool's in-flight read byte count for the full block.
  iou_buffer& promote_to_write() noexcept {
    assert(is_read_);
    // TODO: Add a return value that can be used to cancel the attempt. In
    // particular, Provided Buffers cannot be reused this way.
    pool_->decrement_read_bytes(full_span_.size());
    active_span_ = payload_span_;
    is_read_ = false;
    return *this;
  }

  // Demote this write buffer to read mode. `payload_span` is kept as-is;
  // `active_span` becomes the tail (space after `payload_span` for
  // additional incoming data).
  iou_buffer& demote_to_read() noexcept {
    assert(!is_read_);
    // TODO: Add a return value that can be used to cancel the attempt.
    pool_->increment_read_bytes(full_span_.size());
    auto* end = payload_span_.data() + payload_span_.size();
    active_span_ = {end,
        static_cast<size_t>(full_span_.data() + full_span_.size() - end)};

    is_read_ = true;
    return *this;
  }

  // Whether this buffer is in read mode. Not generally useful externally.
  [[nodiscard]] bool is_read() const noexcept { return is_read_; }

#pragma endregion
#pragma region Helpers
private:
  friend class buffer_pool_base;

  iou_buffer(buffer_pool_base& pool, span_t span, size_t buf_index,
      bool is_read) noexcept
      : pool_{&pool}, full_span_{span}, payload_span_{span.data(), 0},
        active_span_{span.data(), 0}, buf_index_{buf_index}, is_read_{is_read},
        res_{-1} {
    if (is_read_) active_span_ = full_span_;
  }

  // True when the write buffer has been fully sent (or is initially empty).
  // Both states are treated identically: the next write operation resets.
  [[nodiscard]] bool do_is_fully_consumed() const noexcept {
    assert(!is_read_);
    return active_span_.size() == 0 &&
           active_span_.data() >= payload_span_.data() + payload_span_.size();
  }

  void do_reset_write_spans() noexcept {
    assert(!is_read_);
    payload_span_ = {full_span_.data(), 0};
    active_span_ = {full_span_.data(), 0};
  }

  void do_reset() noexcept {
    if (pool_) pool_->return_buffer(full_span_, is_read_);
  }

#pragma endregion
#pragma region Data members
private:
  buffer_pool_base* pool_{};
  span_t full_span_;
  span_t payload_span_;
  span_t active_span_;
  size_t buf_index_{};
  uint64_t file_offset_{seek_current};
  size_t pending_releases_{};
  bool is_read_{};

  iou_timespec timeout_;
  net_endpoint addr_;
#if 0
  msghdr msg_;   // TODO: Reconstitute after move.
  iovec iov_[2]; // TODO: Ditto.

  static constexpr size_t control_capacity =
      CMSG_SPACE(sizeof(in_pktinfo)); // IPv4 IP_PKTINFO

  std::array<std::byte, control_capacity> control;
#endif

  iou_res res_;
  iou_cqe_flags cqe_flags_{};
};

[[nodiscard]] inline iou_buffer
buffer_pool_base::make_buffer(buffer_pool_base& pool, span_t span,
    size_t buf_index, bool is_read) noexcept {
  return iou_buffer{pool, span, buf_index, is_read};
}

#pragma endregion

}}} // namespace corvid::proto::iouring
