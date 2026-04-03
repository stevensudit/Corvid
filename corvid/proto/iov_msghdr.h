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
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sys/socket.h>
#include <sys/uio.h>

#include "../filesys/net_socket.h"

namespace corvid { inline namespace proto {

// Fwd.
struct iov_msghdr_test;

// Scatter/gather socket I/O support using `msghdr` and `sendmsg`. Wraps the
// `msghdr` and the `iovec` array. Use `iov_msghdr_sender` for sending and
// `iov_msghdr_receiver` for receiving.
template<bool SENDER>
class iov_msghdr {
public:
  static constexpr bool is_sender = SENDER;
  static constexpr bool is_receiver = !SENDER;
  static constexpr size_t npos = std::numeric_limits<size_t>::max();

  // Result of an I/O operation, with both the linear count of bytes and the
  // position within the segment.
  struct op_results {
    // Count of bytes transferred in most recent operation.
    size_t transferred{};

    // Byte count translated in segment index and offset.
    size_t index{};
    size_t offset{};
  };

  // Default constructor, allowing segments to be added with `append`. POSIX
  // requires `IOV_MAX` to be at least 16, while Linux typically allows 1024.
  // This class does not impose a limit: the OS will handle it.
  iov_msghdr() {
    segments_.reserve(16);
    update_iov();
  }

  explicit iov_msghdr(std::initializer_list<iovec> iovecs)
      : segments_(iovecs) {
    update();
  }

  explicit iov_msghdr(std::span<const iovec> iovecs)
      : segments_(iovecs.begin(), iovecs.end()) {
    update();
  }

  // Full access to `msghdr`.
  [[nodiscard]] decltype(auto) header(this auto& self) noexcept {
    return (self.header_);
  }

  // View into active segments.
  [[nodiscard]] auto segments() const noexcept {
    return std::span{segments_}.subspan(first_index_);
  }

  // Full clear.
  void clear() noexcept { (void)do_clear(); }

  // Reserve capacity for segments.
  void reserve(size_t count) {
    segments_.reserve(count);
    update_iov();
  }

  // Append a segment. Returns whether the segment was added: empty ones are
  // not.
  [[nodiscard]] bool append(iovec iov) {
    if (iov.iov_len == 0) return false;
    segments_.push_back(iov);
    size_ += iov.iov_len;
    update_iov();
    return true;
  }

  [[nodiscard]] bool append(const void* data, size_t size)
  requires is_sender
  {
    return append(iovec{const_cast<void*>(data), size});
  }

  [[nodiscard]] bool append(void* data, size_t size) {
    return append(iovec{data, size});
  }

  [[nodiscard]] bool append(std::string_view data)
  requires is_sender
  {
    return append(data.data(), data.size());
  }

  template<class T, size_t Extent>
  requires(sizeof(T) == 1)
  [[nodiscard]] bool append(std::span<T, Extent> data) {
    return append(data.data(), data.size_bytes());
  }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] size_t size() const noexcept { return size_; }

  // Compact the segments if there is excessive slack. Completely optional, and
  // only useful if you have a long-running instance and never quite finish
  // consuming all segments.
  bool compact(bool force = false) noexcept {
    // Consume before compacting.
    if (last_op_.transferred == npos) return false;
    if (last_op_.transferred != 0)
      if (!do_consume()) return false;

    if (!force && (first_index_ <= 16)) return false;

    segments_.erase(segments_.begin(), segments_.begin() + first_index_);
    first_index_ = 0;
    return update_iov();
  }

  // Send to `socket` using `sendmsg` to gather segments. On success, updates
  // `last_op`, setting `transferred` to the count of bytes written and
  // pointing `index`/`offset` past the sent data, then returns true. A soft
  // error counts as a success that just so happens to send 0 bytes. On
  // failure, including EOF, sets `last_op` to `npos` and returns false.
  //
  // Applies the previous operation's results before starting a new one,
  // pointing the effective start of the segments past the sent bytes. This
  // gives the caller a chance to first decomission buffers that have been
  // drained.
  //
  // Status       |  Return  | `index`/`offset`         | `transferred`
  // Success         true      past bytes written         count
  // Soft failure    true      start, `{0, 0}`            0
  // Hard failure    false     invalid, `{npos, npos}`    npos
  [[nodiscard]] bool
  send(const net_socket& socket, int flags = MSG_NOSIGNAL) noexcept
  requires is_sender
  {
    // Apply last op before starting new one.
    if (last_op_.transferred == npos) return false;
    if (last_op_.transferred != 0)
      if (!do_consume()) return false;

    const auto result = socket.send(header_, flags);
    if (result < 0) {
      if (!socket.is_hard_error()) return do_set_last(0, 0, 0);
      return do_set_fail();
    }

    last_op_.transferred = static_cast<size_t>(result);
    return do_update_results();
  }

  // Receive from `socket`, using `recvmsg` to scatter segments. On success,
  // updates `last_op`, setting `transferred` to the count of bytes read and
  // pointing `index`/`offset` past the read data, then returns true. A
  // soft error counts as a success with no new data. On EOF, sets `last_op`
  // to zero and returns false. On hard failure, sets `last_op` to `npos` and
  // returns false.
  //
  // Applies the previous operation's results before starting a new one,
  // pointing the effective start of the segments past the read bytes. This
  // gives the caller a chance to first process buffers that have been
  // filled.
  //
  // Status       |  Return  | `index`/`offset`         | `transferred`
  // Success         true      past bytes read into       count
  // Soft failure    true      start, `{0, 0}`            0
  // EOF             false     start, `{0, 0}`            0
  // Hard failure    false     invalid, `{npos, npos}`    npos
  [[nodiscard]] bool recv(const net_socket& socket, int flags = 0) noexcept
  requires is_receiver
  {
    // Apply last op before starting new one.
    if (last_op_.transferred == npos) return false;
    if (last_op_.transferred != 0)
      if (!do_consume()) return false;

    header_.msg_flags = 0;
    const auto result = socket.recv(header_, flags);
    if (result < 0) {
      if (!socket.is_hard_error()) return do_set_last(0, 0, 0);
      return do_set_fail();
    }
    // EOF.
    if (result == 0) return false;

    last_op_.transferred = static_cast<size_t>(result);
    return do_update_results();
  }

  [[nodiscard]] const auto& last_op() const noexcept { return last_op_; }

private:
  friend struct iov_msghdr_test;

  // Point header at the active segments.
  bool update_iov() {
    if (first_index_ >= segments_.size()) {
      header_.msg_iov = nullptr;
      header_.msg_iovlen = 0;
      return true;
    }
    header_.msg_iov = &segments_[first_index_];
    header_.msg_iovlen = segments_.size() - first_index_;
    return true;
  }

  /// Set `size_` to the total bytes in the active segments.
  bool update_count() {
    size_ = 0;
    for (size_t ndx = first_index_; ndx < segments_.size(); ++ndx)
      size_ += segments_[ndx].iov_len;
    return true;
  }

  // Update both header and count.
  bool update() { return update_iov() && update_count(); }

  bool do_clear() noexcept {
    header_ = {};
    segments_.clear();
    first_index_ = 0;
    size_ = 0;
    last_op_ = {};
    return true;
  }

  // Consume up to the position, making it the new start of the active
  // region. Use with return value from `offset_to_coordinates` to skip past
  // a linear offset. When position is past the end, clears and returns true
  // (a complete consume is success). Returns false only on hard error.
  [[nodiscard]] bool do_consume() noexcept {
    if (segments_.empty() || last_op_.transferred == npos) return false;
    if (!last_op_.transferred) return true;

    // Map to actual index.
    size_t actual_index = first_index_ + last_op_.index;
    size_t offset = last_op_.offset;

    // Disarm.
    last_op_ = {};

    // If past the last segment, trim.
    if (actual_index >= segments_.size()) return do_clear();

    // Subtract bytes from all segments being skipped over. Note that we only
    // look at the lengths; we do not dereference the buffers because they may
    // well have been freed.
    for (size_t i = first_index_; i < actual_index; ++i)
      size_ -= segments_[i].iov_len;
    first_index_ = actual_index;

    // If past the end of the last segment, trim.
    auto& last_iov = segments_[first_index_];
    if (offset > last_iov.iov_len) return do_clear() && false;

    // Consume the start of the last segment.
    last_iov.iov_base = static_cast<uint8_t*>(last_iov.iov_base) + offset;
    last_iov.iov_len -= offset;
    size_ -= offset;
    return update_iov();
  }

  // Converts `last_op_.transferred` from a linear byte count into a segment
  // index and intra-segment offset (both relative to `first_index_`), then
  // stores the result back into `last_op_`. The index may point one past the
  // last segment when the transfer ends exactly on a segment boundary.
  [[nodiscard]] bool do_update_results() noexcept {
    const size_t transferred = last_op_.transferred;
    if (transferred > size()) return do_set_fail();
    if (transferred == 0) return do_set_last(0, 0, 0);

    size_t remaining = transferred;
    for (size_t index = first_index_; index < segments_.size(); ++index) {
      const size_t available = segments_[index].iov_len;

      // If the remaining offset falls within the current segment, store the
      // index and offset within it.
      if (remaining < available)
        return do_set_last(transferred, index - first_index_, remaining);

      // If we exactly consume the last byte of a segment, the next segment
      // is the new start of the active region, so store offset 0 for the
      // next index.
      remaining -= available;
      if (!remaining)
        return do_set_last(transferred, index - first_index_ + 1, 0);
    }

    // Unreachable.
    return true;
  }

  [[nodiscard]] bool
  do_set_last(size_t transferred, size_t index, size_t offset) noexcept {
    last_op_.transferred = transferred;
    last_op_.index = index;
    last_op_.offset = offset;
    return true;
  }

  [[nodiscard]] bool do_set_fail() noexcept {
    return do_set_last(npos, npos, npos) && false;
  }

  msghdr header_{};
  std::vector<iovec> segments_;
  size_t first_index_{};
  size_t size_{};
  op_results last_op_{};
};

// I/O vector for sending or receiving.
using iov_msghdr_sender = iov_msghdr<true>;
using iov_msghdr_receiver = iov_msghdr<false>;
}} // namespace corvid::proto
