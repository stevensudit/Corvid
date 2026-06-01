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
#include <iterator>
#include <span>
#include <sys/uio.h>
#include <type_traits>
#include <utility>
#include <vector>

#include "../enums/bool_enums.h"
#include "../meta/maybe.h"

namespace corvid { inline namespace proto {
using namespace bool_enums;

#pragma region iov_queue

// A queue of owned byte buffers, presented to a gather/scatter operation as an
// `iovec` view. The same machinery serves both directions; a queue uses one
// or the other:
//
//   Send: the producer `append`s buffers that already hold bytes. `unused`
//   offers the not-yet-written bytes for a gather write, `consume` records how
//   many the sink took, and `retire` frees them once they need not be replayed
//   (immediately for a reliable sink, after the ack for an unreliable one).
//
//   Receive: the producer `append`s empty, pre-sized buffers. `unused` offers
//   their free room for a scatter read, `consume` records how many bytes the
//   system filled, and `harvest_bytes` or `harvest_chunk` hands the filled
//   payload back to the caller.
//
// Three byte watermarks advance monotonically, `reclaimed <= used <=
// appended`:
//   appended   total bytes appended (send: data; receive: capacity)
//   used       already accessed by the system (send: written; receive: filled)
//   reclaimed  released from the front (send: acked; receive: harvested)
//
// `consume` drives `used`; `retire` and the `harvest` methods drive
// `reclaimed`. They differ only in what they do with a buffer once its bytes
// are reclaimed: `retire` frees each fully-reclaimed front buffer in place,
// `harvest_bytes` copies the bytes out and leaves the drained buffers as slack
// for `tidy`, and `harvest_chunk` moves a buffer out to the caller.
//
// Two cursors track the watermarks as a buffer index plus an offset within
// that buffer, so `unused` and the `harvest` methods read the position
// directly without rescanning:
//   unused cursor     the first buffer holding an unused byte
//   retained cursor   the first buffer holding an unreclaimed byte
//
// `Chunk` is any movable owner of a contiguous run of 1-byte elements that
// keeps its buffer at a stable address when the `Chunk` itself is moved.
//
// That stability is the load-bearing requirement: the system holds the `iovec`
// addresses until the bytes are reclaimed, across appends that may reallocate
// the chunk vector (which moves the `Chunk` handles).
//
// `std::vector<uint8_t>` and other 1-byte vectors qualify; `std::string`
// qualifies only with its small-string buffer defeated (e.g. a `reserve` past
// the SSO threshold) so the bytes live on the heap. `Chunk` must also support
// `data`, `size`, `empty`, `begin` (used to deduce the element type), and
// move-assignment from a default-constructed instance (used to free a retired
// chunk).
//
// So long as it meets these requirements, it could come from some sort of
// object pool or arena.
//
// `State`, when not `void`, is an arbitrary per-queue value the producer sets
// through `state` and can be used by the system to parameterize the operation,
// alongside `unused`, e.g. the QUIC `write_stream_flags` that ride with a
// stream's final bytes. It could also be used to track state relevant to
// parsing or production. Either way, the queue carries it opaquely: it neither
// interprets nor clears it. When `void` it adds no storage.
template<typename Chunk = std::vector<uint8_t>, typename State = void>
class iov_queue {
public:
  using chunk_t = Chunk;

  // The 1-byte element type, deduced from what `chunk_t::begin` iterates over
  // (e.g. `uint8_t` for `std::vector<uint8_t>`, `char` for `std::string`). The
  // `harvest` methods report their good bytes as a span of this type.
  using byte_t = std::iter_value_t<decltype(std::declval<chunk_t&>().begin())>;

#pragma region Utilities

  // Total bytes spanned by `iov`. A sink that takes only a prefix of an
  // `unused` span (e.g. capped by a fixed vec count) sums the prefix with this
  // to learn the exact byte count to `consume`.
  [[nodiscard]] static uint64_t iov_byte_count(
      std::span<const iovec> iov) noexcept {
    uint64_t n = 0;
    for (const auto& e : iov) n += e.iov_len;
    return n;
  }

#pragma endregion
#pragma region Construction

  iov_queue() { iov_.reserve(16); }

  iov_queue(iov_queue&&) = delete;
  iov_queue& operator=(iov_queue&&) = delete;
  iov_queue(const iov_queue&) = delete;
  iov_queue& operator=(const iov_queue&) = delete;

#pragma endregion
#pragma region Mutators

  // Take ownership of `chunk`, appending it to the back. For a send queue, the
  // chunk holds bytes to write; for a receive queue, it is empty room to fill,
  // pre-sized to the capacity the kernel may scatter into. (It does not need
  // to be prefilled.)
  bool append(chunk_t&& chunk) {
    if (chunk.empty()) return false;
    appended_ += chunk.size();
    [[maybe_unused]] const auto start_data = chunk.data();
    chunks_.emplace_back(std::move(chunk));
    assert(chunks_.back().data() == start_data);
    return true;
  }

  // Mark `n` unused bytes as used, advancing the `used` watermark: for a send,
  // these are bytes the sink wrote, for a receive, bytes the system filled.
  // The next `unused` does not include them.
  bool consume(uint64_t n) noexcept {
    assert(used_ + n <= appended_);
    used_ += n;
    advance(unused_index_, unused_offset_, n, [](chunk_t&) noexcept {});
    return true;
  }

  // Retire `n` bytes from the front, freeing each fully-reclaimed buffer in
  // place. A reliable send caller may skip `consume` and retire directly;
  // `used` is pulled along so the unused view and `unacknowledged` stay
  // honest.
  bool retire(uint64_t n) noexcept {
    assert(reclaimed_ + n <= appended_);
    reclaimed_ += n;
    if (used_ < reclaimed_) {
      const uint64_t pull = reclaimed_ - used_;
      used_ = reclaimed_;
      advance(unused_index_, unused_offset_, pull, [](chunk_t&) noexcept {});
    }
    advance(retained_index_, retained_offset_, n, [](chunk_t& c) noexcept {
      clear_chunk(c);
    });
    return true;
  }

  // Copy used-but-unreclaimed bytes into `out`, starting at offset `at` and
  // filling the room that remains (`out.size() - at`). Reclaims the copied
  // bytes, leaving the drained source buffers as slack for `tidy`. Returns the
  // region written, [at, at + written); `out` itself is not resized. A nonzero
  // `at` lets the caller accumulate after a header or a prior harvest.
  //
  // This technique is convenient, but requires an extra copy and leaves the
  // drained buffers around as slack until `tidy` drops them. If the caller can
  // wait to take ownership of full chunks, `harvest_chunk` is more efficient.
  [[nodiscard]] std::span<const byte_t>
  harvest_bytes(chunk_t& out, size_t at = 0) noexcept {
    assert(at <= out.size());
    const uint64_t want =
        std::min<uint64_t>(used_ - reclaimed_, out.size() - at);
    uint64_t written = 0;
    while (written < want) {
      auto& chunk = chunks_[retained_index_];
      const uint64_t take =
          std::min<uint64_t>(chunk.size() - retained_offset_, want - written);
      std::memcpy(out.data() + at + written, chunk.data() + retained_offset_,
          take);
      written += take;
      retained_offset_ += take;
      if (retained_offset_ == chunk.size()) {
        ++retained_index_;
        retained_offset_ = 0;
      }
    }
    reclaimed_ += written;
    return std::span<const byte_t>{out.data() + at,
        static_cast<size_t>(written)};
  }

  // Move the front fully-filled buffer into `out`, reclaiming its bytes.
  // Returns the buffer's good bytes: the whole buffer unless its front was
  // already taken by `harvest_bytes`, in which case the span starts past that
  // prefix. The partially-filled tail buffer is never moved out (the system
  // may still be filling it). If no chunk is available; then `out` is emptied
  // and the span is empty.
  [[nodiscard]] std::span<const byte_t> harvest_chunk(chunk_t& out) noexcept {
    if (retained_index_ >= unused_index_) {
      out = chunk_t{};
      return {};
    }
    const uint64_t good_start = retained_offset_;
    out = std::move(chunks_[retained_index_]);
    const uint64_t good = out.size() - good_start;
    reclaimed_ += good;
    ++retained_index_;
    retained_offset_ = 0;
    return std::span<const byte_t>{out.data() + good_start,
        static_cast<size_t>(good)};
  }

  // For long-running queues, check `slack` and decide when to call `tidy` to
  // drop the empty leading slots. With `release`, also returns the vector's
  // freed capacity to the allocator and rebases the byte watermarks toward
  // zero so they can't overflow.
  bool tidy(deallocation_policy policy = deallocation_policy::preserve) {
    chunks_.erase(chunks_.begin(),
        chunks_.begin() + static_cast<std::ptrdiff_t>(retained_index_));
    unused_index_ -= retained_index_;
    retained_index_ = 0;
    chunks_.shrink_to_fit();
    if (policy == deallocation_policy::release) {
      // The dropped slots held `reclaimed - retained_offset` bytes; the rest
      // of `reclaimed` stays as the front buffer's already-reclaimed prefix.
      const uint64_t dropped = reclaimed_ - retained_offset_;
      appended_ -= dropped;
      used_ -= dropped;
      reclaimed_ -= dropped;
    }
    return true;
  }

  // Move the fully-reclaimed leading buffers to the back, where they become
  // free room to fill again as though freshly appended (their sizes are added
  // to `appended`). The counterpart to `harvest_bytes` for a receive queue
  // reusing its buffers: rather than letting `tidy` free the drained buffers,
  // recycle them for the next read. Clears the slack, so `slack` returns to
  // zero. (`harvest_chunk` and `retire` empty their buffers, so recycling
  // those contributes no room.)
  bool recycle() noexcept {
    uint64_t recycled = 0;
    for (size_t ndx = 0; ndx < retained_index_; ++ndx)
      recycled += chunks_[ndx].size();
    std::rotate(chunks_.begin(),
        chunks_.begin() + static_cast<std::ptrdiff_t>(retained_index_),
        chunks_.end());
    appended_ += recycled;
    unused_index_ -= retained_index_;
    retained_index_ = 0;
    return true;
  }

#pragma endregion
#pragma region Accessors

  // Size of unused bytes (send: bytes still to write; receive: free capacity).
  [[nodiscard]] uint64_t size() const noexcept {
    if (used_ >= appended_) return 0;
    return appended_ - used_;
  }

  // Size of used but unreclaimed bytes (send: sent and unacknowledged;
  // receive: filled and not yet harvested).
  [[nodiscard]] uint64_t unacknowledged() const noexcept {
    if (reclaimed_ >= used_) return 0;
    return used_ - reclaimed_;
  }

  // The carried per-queue `State` (present only when `State` is not `void`).
  // The producer sets it; the syscall reads it alongside `unused`. The queue
  // never inspects it.
  [[nodiscard]] auto& state(this auto& self) noexcept
  requires(!std::is_void_v<State>)
  {
    return self.state_;
  }

  // The unused bytes [used, appended) as an `iovec` view: bytes to write, for
  // a send; free room to fill, for a receive.
  //
  // Entries in the iov buffer remain valid even after mutating calls (other
  // than `retire` and `harvest_chunk`, which free or move the underlying
  // buffers), but calling this method overwrites that buffer.
  [[nodiscard]] std::span<const iovec> unused() {
    iov_.clear();
    if (unused_index_ >= chunks_.size()) return iov_;
    auto& first = chunks_[unused_index_];
    iov_.emplace_back(at_offset(first, unused_offset_),
        first.size() - unused_offset_);
    for (size_t ndx = unused_index_ + 1; ndx < chunks_.size(); ++ndx) {
      auto& chunk = chunks_[ndx];
      iov_.emplace_back(at_offset(chunk, 0), chunk.size());
    }
    return iov_;
  }

  // Number of unused leading slots; a hint that `tidy` might be worth it.
  [[nodiscard]] uint64_t slack() const noexcept { return retained_index_; }

#pragma endregion
#pragma region Diagnostics

  [[nodiscard]] uint64_t appended() const noexcept { return appended_; }
  [[nodiscard]] uint64_t used() const noexcept { return used_; }
  [[nodiscard]] uint64_t reclaimed() const noexcept { return reclaimed_; }
  [[nodiscard]] size_t retained_chunks() const noexcept {
    return chunks_.size() - retained_index_;
  }

#pragma endregion
#pragma region Helpers
private:
  // Walk a cursor forward by `n` bytes, calling `on_pass` for each buffer
  // fully passed. Lands the cursor on the first byte past the advance,
  // pointing one slot past the back (offset 0) when it ends on a buffer
  // boundary.
  template<typename OnPass>
  void advance(size_t& index, uint64_t& offset, uint64_t n, OnPass on_pass) {
    while (n > 0) {
      assert(index < chunks_.size());
      auto& chunk = chunks_[index];
      const uint64_t avail = chunk.size() - offset;
      if (n < avail) {
        offset += n;
        return;
      }
      n -= avail;
      on_pass(chunk);
      ++index;
      offset = 0;
    }
  }

  // Free a reclaimed chunk's buffer. A helper (rather than a trait) so an
  // exotic `Chunk` can be accommodated with an `if constexpr` branch here;
  // move-assigning an empty instance frees `std::vector` / `std::string`.
  static void clear_chunk(chunk_t& chunk) noexcept { chunk = chunk_t{}; }

  // `void*` to `chunk`'s byte at `offset`, normalizing the element type and
  // dropping constness (the syscall may read or write, per direction).
  [[nodiscard]] static void* at_offset(chunk_t& chunk, uint64_t offset) {
    return const_cast<void*>(static_cast<const void*>(chunk.data() + offset));
  }

#pragma endregion
#pragma region Data members

  std::vector<chunk_t> chunks_;
  std::vector<iovec> iov_;
  [[no_unique_address]] maybe_void_t<State> state_{};
  size_t unused_index_{};      // first slot with an unused byte
  uint64_t unused_offset_{};   // offset within chunks_[unused_index_]
  size_t retained_index_{};    // first slot with an unreclaimed byte
  uint64_t retained_offset_{}; // offset of that byte within retained slot
  uint64_t reclaimed_{};       // Released from the front
  uint64_t used_{};            // Handed across the syscall, not yet reclaimed
  uint64_t appended_{};        // Total ever appended

#pragma endregion
};

#pragma endregion

}} // namespace corvid::proto
