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
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../containers/opt_find.h"
#include "quic_conn.h"
#include "quic_dgram_plugins.h"
#include "quic_header.h"
#include "quic_session_io.h"
#include "quic_stream_send_queue.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_echo_plugin

// Upper-layer plugin for `quic_dgram_protocol` that echoes every byte and FIN
// it receives back on the same stream. Holds one `quic_stream_send_queue` per
// active stream, keyed by `quic_stream_id`.
//
// Lifecycle:
//
//   `on_stream_open`             create an empty send queue
//
//   `on_recv_stream_data`        append the inbound bytes (and any sticky
//                                `fin`) to the stream's queue
//
//   `on_acked_stream_data_offset` retire the freshly-ACKed prefix of that
//                                 queue
//
//   `on_stream_close`            drop the queue
//
//   `drain`                      single loop that picks any stream with
//                                pending work (or falls back to
//                                `stream_id::none` for ACKs / MAX_DATA / etc.)
//                                and hands one packet at a time to ngtcp2 via
//                                `writev_stream`, shipping each non-empty
//                                packet through `quic_session_io::send_packet`
//
// Half-close semantics: when the peer sends `fin`, we mirror it back once our
// outbound queue has drained, so the server's stream-close follows the peer's.
// We do not act on STOP_SENDING or RESET_STREAM here; ngtcp2's defaults (and
// the no-op base) are sufficient for the echo scenario.
class quic_echo_plugin: public quic_no_op_plugin {
public:
#pragma region Construction

  explicit quic_echo_plugin(quic_session_io& s) noexcept
      : quic_no_op_plugin{s} {}

#pragma endregion
#pragma region Handlers

  [[nodiscard]] bool on_stream_open(
      quic_stream_id stream_id) noexcept override {
    (void)queues_[stream_id];
    return true;
  }

  // Append the received bytes (and the sticky `fin` flag if set) to the
  // stream's outgoing queue. ngtcp2 hands us a non-owning span valid only
  // for the call, so the queue must take a copy.
  [[nodiscard]] bool on_recv_stream_data(quic_stream_id stream_id,
      uint64_t /*offset*/, std::span<const uint8_t> data,
      quic_stream_data_flags flags) noexcept override {
    const auto write_flags =
        bitmask::has(flags, quic_stream_data_flags::fin)
            ? write_stream_flags::fin
            : write_stream_flags::none;
    auto& q = queues_[stream_id];
    q.append(std::vector<uint8_t>(data.begin(), data.end()), write_flags);
    return true;
  }

  [[nodiscard]] bool on_acked_stream_data_offset(quic_stream_id stream_id,
      uint64_t /*offset*/, uint64_t datalen) noexcept override {
    if (auto q = find_opt(queues_, stream_id)) q->retire_acked(datalen);
    return true;
  }

  [[nodiscard]] bool on_stream_close(quic_stream_id stream_id,
      std::optional<uint64_t> /*app_error_code*/) noexcept override {
    queues_.erase(stream_id);
    return true;
  }

#pragma endregion
#pragma region Drain

  // Drive ngtcp2's outbound queue until it stops producing. Each iteration
  // picks a stream with pending work (or `stream_id::none` if none), borrows
  // a fresh send buffer, and hands one packet to ngtcp2 via `writev_stream`.
  // ngtcp2 may pack stream bytes alongside non-stream frames in the same
  // packet, so the unified loop subsumes the no-op base's ACK-only flush.
  //
  // Round-robin across streams is not strictly fair: the first
  // unordered_map iterator hit with work wins each iteration, so a stream
  // that keeps having bytes accepted into ngtcp2's queue can starve
  // others. Acceptable for the echo scenario; revisit if a real protocol
  // needs strict fairness.
  [[nodiscard]] bool drain(timeouts::time_point_t now) noexcept {
    for (;;) {
      quic_stream_id sid = quic_stream_id::none;
      std::span<const iovec> iov;
      write_stream_flags flags = write_stream_flags::none;
      quic_stream_send_queue* qp = nullptr;
      for (auto& [id, q] : queues_) {
        if (!q.has_work()) continue;
        sid = id;
        iov = q.writable_iov();
        flags = q.writable_flags();
        qp = &q;
        break;
      }

      auto out = io_.borrow_send_buffer();
      if (!out) return true;
      uint64_t accepted = 0;
      const auto status =
          io_.conn().writev_stream(sid, iov, out, accepted, flags, now);
      if (status != quic_decode_status::ok) return false;
      if (out.payload_bytes().empty()) return true;
      if (qp) qp->commit(accepted);
      (void)io_.send_packet(std::move(out));
    }
  }

#pragma endregion
#pragma region Accessors

  // Number of streams with at least one queue currently allocated. Exposed
  // for tests / diagnostics.
  [[nodiscard]] size_t stream_count() const noexcept { return queues_.size(); }

#pragma endregion
#pragma region Data members
private:
  // NOTE: This could easily be a `std::vector<std::pair<quic_stream_id,
  // quic_stream_send_queue>>`.
  std::unordered_map<quic_stream_id, quic_stream_send_queue> queues_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
