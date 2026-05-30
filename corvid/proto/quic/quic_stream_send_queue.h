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
#include <cstdint>
#include <deque>
#include <span>
#include <sys/uio.h>
#include <utility>
#include <vector>

#include "quic_conn.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_stream_send_queue

// Per-stream owning queue of outbound bytes for QUIC. Built around the
// `quic_conn::writev_stream` contract: the queue rebuilds a contiguous `iovec`
// view over its unoffered bytes on demand, hands ownership of the underlying
// storage to the queue (move-in), and tracks three stream-offset watermarks
// plus a sticky fin flag.
//
//   `appended`      high water of bytes ever appended (== back-of-queue
//                   offset)
//
//   `offered`       bytes ngtcp2 has accepted into its send queue
//
//   `acked`         bytes the peer has ACKed
//
//   `front_offset`  stream offset of `chunks_.front()[0]`
//
//   `front_offset` <= `acked` <= `offered` <= `appended`
//
// On `retire_acked`, chunks whose entire extent falls at or below `acked` are
// popped from the front; partial-ack does not split a chunk (the wasted memory
// is bounded by the chunk size and goes away on the next pop).
//
// Lifetime contract: bytes accepted by ngtcp2 (`offered - acked` worth) must
// remain valid in this queue until the peer acks them, because ngtcp2 may
// retransmit. The queue enforces this by never popping past `acked`.
//
// The reused `iov_` scratch is rebuilt on every `writable_iov` call; the
// returned span is stable until the next mutating call on this queue
// (`append`, `commit`, `retire_acked`, `writable_iov`).
class quic_stream_send_queue {
public:
#pragma region Mutators

  // Append a chunk of bytes to the back of the queue. `flags` carries sticky
  // modifiers (typically `fin`) that ride on every `writev` call until ngtcp2
  // has buffered all appended bytes.
  void append(std::vector<uint8_t>&& chunk,
      write_stream_flags flags = write_stream_flags::none) {
    appended_ += chunk.size();
    if (!chunk.empty()) chunks_.push_back(std::move(chunk));
    pending_flags_ = pending_flags_ | flags;
  }

  // Advance the `offered` watermark by `bytes_accepted` (the value returned
  // through `quic_conn::writev_stream`'s out parameter). Once all appended
  // bytes have been offered, ngtcp2 has buffered the sticky flags too, so
  // clear them (no need to keep passing FIN, etc).
  void commit(uint64_t bytes_accepted) noexcept {
    offered_ += bytes_accepted;
    if (offered_ == appended_) pending_flags_ = write_stream_flags::none;
  }

  // Advance the `acked` watermark by `datalen` (the value from
  // `on_acked_stream_data_offset`). Pop front chunks whose entire extent is
  // now at or below the `acked` watermark.
  void retire_acked(uint64_t datalen) noexcept {
    for (acked_ += datalen; !chunks_.empty(); chunks_.pop_front()) {
      const auto next_front = front_offset_ + chunks_.front().size();
      if (next_front > acked_) break;
      front_offset_ = next_front;
    }
  }

#pragma endregion
#pragma region Accessors

  // True if there are bytes ngtcp2 hasn't seen yet, or sticky flags not yet
  // handed off. Drives the plugin's per-turn drain loop.
  [[nodiscard]] bool has_work() const noexcept {
    return offered_ < appended_ || pending_flags_ != write_stream_flags::none;
  }

  // All offered bytes have been acked AND any sticky flags have been handed
  // off. The queue can be destroyed (or recycled) safely at this point.
  [[nodiscard]] bool fully_drained() const noexcept {
    return acked_ >= appended_ && pending_flags_ == write_stream_flags::none;
  }

  // Flags to pass to `quic_conn::writev_stream` this turn. Cleared by `commit`
  // once all appended bytes have been buffered by ngtcp2.
  [[nodiscard]] write_stream_flags writable_flags() const noexcept {
    return pending_flags_;
  }

  // Rebuild and return the iovec view over the unoffered byte range
  // [`offered`, `appended`). Returned span aliases internal storage and
  // remains valid until the next mutating call on this queue.
  [[nodiscard]] std::span<const iovec> writable_iov() {
    iov_.clear();
    if (offered_ >= appended_) return iov_;
    uint64_t skip = offered_ - front_offset_;
    for (auto& c : chunks_) {
      if (skip >= c.size()) {
        skip -= c.size();
        continue;
      }
      iov_.push_back(iovec{c.data() + skip, c.size() - skip});
      skip = 0;
    }
    return iov_;
  }

#pragma endregion
#pragma region Diagnostics

  // Watermark accessors, exposed for testing and diagnostics.
  [[nodiscard]] uint64_t appended() const noexcept { return appended_; }
  [[nodiscard]] uint64_t offered() const noexcept { return offered_; }
  [[nodiscard]] uint64_t acked() const noexcept { return acked_; }
  [[nodiscard]] size_t retained_chunks() const noexcept {
    return chunks_.size();
  }

#pragma endregion
#pragma region Data members
private:
  std::deque<std::vector<uint8_t>> chunks_;
  std::vector<iovec> iov_;
  uint64_t front_offset_{};
  uint64_t offered_{};
  uint64_t acked_{};
  uint64_t appended_{};
  write_stream_flags pending_flags_{};

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
