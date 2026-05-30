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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <sys/uio.h>

#include "http3_conn.h"
#include "quic_conn.h"
#include "quic_header.h"
#include "quic_session_io.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region http3_plugin

// Upper-layer plugin for `quic_dgram_protocol` that runs HTTP/3 over a
// `quic_conn`. It is the bridge between the two protocol layers: it inherits
// both abstract upcall bases (`quic_conn_handlers` for ngtcp2's transport
// events, `http3_conn_handlers` for nghttp3's HTTP/3 events) and owns the
// `http3_conn` that sits between them. One object, two hats; the tight,
// bidirectional, single-turn coupling between the layers is the reason they
// share one object rather than splitting across two (see the roadmap's HTTP/3
// layering section).
//
// The forwarding is mechanical:
//
//   transport (ngtcp2) -> HTTP/3 (nghttp3)
//     on_recv_stream_data         -> http3_conn::read_stream (+ flow-control
//                                    credit for the bytes nghttp3 consumed)
//     on_acked_stream_data_offset -> http3_conn::add_ack_offset
//     on_stream_close             -> http3_conn::close_stream
//     on_stream_reset             -> http3_conn::shutdown_stream_read
//     on_stream_stop_sending      -> http3_conn::shutdown_stream_write
//     on_extend_max_stream_data   -> http3_conn::unblock_stream
//
//   HTTP/3 (nghttp3) -> transport (ngtcp2)
//     on_stop_sending             -> quic_conn::shutdown_stream_read
//     on_reset_stream             -> quic_conn::shutdown_stream_write
//     on_deferred_consume         -> flow-control credit
//     on_recv_settings            -> retained for inspection
//
// Lifecycle: nghttp3 is initialized lazily, the first time the read or bind
// path needs it. The three local unidirectional streams (control, QPACK
// encoder, QPACK decoder) are opened and bound on `on_app_tx_ready`, once the
// 1-RTT TX key is installed and ngtcp2 permits opening streams. If the peer
// has not granted unidirectional-stream credit by then, the bind defers to the
// later `on_extend_max_local_streams_uni`. Binding queues the control-stream
// type byte and SETTINGS, which the per-turn `drain` ships.
//
// Request / response submission and HEADERS / DATA handling (the application
// surface above this bridge) land with the HTTP/3 fake-GET milestone; the
// header / body upcalls keep their no-op base behavior here.
class http3_plugin: public quic_conn_handlers, public http3_conn_handlers {
public:
#pragma region Construction

  explicit http3_plugin(quic_session_io& s) noexcept : io_{s} {
    // nghttp3's `set_handlers` must precede `init`; `init` itself is lazy (see
    // `ensure_h3_init`) and runs the first time any path needs nghttp3.
    h3_.set_handlers(this);
  }

#pragma endregion
#pragma region QUIC handlers

  // 1-RTT TX key installed: initialize nghttp3 and, if the peer has granted
  // unidirectional-stream credit, open + bind the HTTP/3 control and QPACK
  // streams. The client commonly reaches this point before ngtcp2 has applied
  // the peer's transport params, leaving no uni credit yet; `try_bind_streams`
  // then defers and the retry lands in `on_extend_max_local_streams_uni`.
  [[nodiscard]] bool on_app_tx_ready() noexcept override {
    if (!ensure_h3_init()) return false;
    return try_bind_streams();
  }

  // Feed inbound stream bytes to nghttp3, then return QUIC flow-control credit
  // for whatever nghttp3 accounted (which excludes DATA-frame body bytes;
  // those are credited via `on_deferred_consume` / the body handler).
  [[nodiscard]] bool on_recv_stream_data(quic_stream_id stream_id,
      uint64_t /*offset*/, std::span<const uint8_t> data,
      quic_stream_data_flags flags) noexcept override {
    if (!ensure_h3_init()) return false;
    const auto chunk =
        bitmask::has(flags, quic_stream_data_flags::fin)
            ? stream_chunk::fin
            : stream_chunk::more;
    size_t consumed = 0;
    if (!h3_.read_stream(stream_id, data, chunk, consumed)) return false;
    return credit_flow_control(stream_id, consumed);
  }

  [[nodiscard]] bool on_acked_stream_data_offset(quic_stream_id stream_id,
      uint64_t /*offset*/, uint64_t datalen) noexcept override {
    if (!h3_) return true;
    return h3_.add_ack_offset(stream_id, datalen);
  }

  [[nodiscard]] bool on_stream_close(quic_stream_id stream_id,
      std::optional<uint64_t> app_error_code) noexcept override {
    if (!h3_) return true;
    auto code = h3_error_code::no_error;
    if (app_error_code) code = static_cast<h3_error_code>(*app_error_code);
    return h3_.close_stream(stream_id, code);
  }

  // Peer reset its send side (RESET_STREAM): tell nghttp3 to discard the read
  // side. The transport error code is HTTP/3's to interpret; nghttp3's
  // shutdown takes none.
  [[nodiscard]] bool on_stream_reset(quic_stream_id stream_id,
      uint64_t /*final_size*/, uint64_t /*app_error_code*/) noexcept override {
    if (!h3_) return true;
    return h3_.shutdown_stream_read(stream_id);
  }

  // Peer sent STOP_SENDING: tell nghttp3 to stop writing this stream.
  [[nodiscard]] bool on_stream_stop_sending(quic_stream_id stream_id,
      uint64_t /*app_error_code*/) noexcept override {
    if (!h3_) return true;
    h3_.shutdown_stream_write(stream_id);
    return true;
  }

  // Peer raised our per-stream send window: nghttp3 may resume the stream.
  [[nodiscard]] bool on_extend_max_stream_data(quic_stream_id stream_id,
      uint64_t /*max_data*/) noexcept override {
    if (!h3_) return true;
    return h3_.unblock_stream(stream_id);
  }

  // Peer raised our local unidirectional-stream limit. If the control / QPACK
  // streams could not be bound earlier (no uni credit when the TX key
  // arrived), bind them now. A no-op once bound.
  [[nodiscard]] bool on_extend_max_local_streams_uni(
      uint64_t /*max_streams*/) noexcept override {
    if (!h3_) return true;
    return try_bind_streams();
  }

#pragma endregion
#pragma region HTTP/3 handlers

  // The peer's SETTINGS arrived on its control stream; retain a copy.
  [[nodiscard]] bool on_recv_settings(
      const http3_settings& settings) noexcept override {
    peer_settings_ = settings;
    return true;
  }

  // nghttp3 wants STOP_SENDING on `stream_id`: forward to ngtcp2 (closes the
  // read side, emitting STOP_SENDING with the HTTP/3 error code).
  [[nodiscard]] bool on_stop_sending(quic_stream_id stream_id,
      h3_error_code app_error_code,
      void* /*stream_user_data*/) noexcept override {
    return io_.conn().shutdown_stream_read(stream_id, *app_error_code) ==
           quic_status::ok;
  }

  // nghttp3 wants RESET_STREAM on `stream_id`: forward to ngtcp2 (closes the
  // write side, emitting RESET_STREAM with the HTTP/3 error code).
  [[nodiscard]] bool on_reset_stream(quic_stream_id stream_id,
      h3_error_code app_error_code,
      void* /*stream_user_data*/) noexcept override {
    return io_.conn().shutdown_stream_write(stream_id, *app_error_code) ==
           quic_status::ok;
  }

  // nghttp3 consumed `consumed` bytes for a stream that had been blocked on
  // inter-stream synchronization; credit that QUIC flow control now (the
  // unblocked path is credited inline in `on_recv_stream_data`).
  [[nodiscard]] bool on_deferred_consume(quic_stream_id stream_id,
      size_t consumed, void* /*stream_user_data*/) noexcept override {
    return credit_flow_control(stream_id, consumed);
  }

#pragma endregion
#pragma region Drain

  // Per-turn outbound path. Each iteration pulls the next chunk nghttp3 wants
  // to write, hands it to ngtcp2 (which packs stream bytes alongside any
  // non-stream frames), reports back to nghttp3 how much ngtcp2 accepted, and
  // ships the packet. Loops until both layers report nothing more to send.
  //
  // Before nghttp3 is bound (during the handshake), `h3_` is empty and every
  // turn degenerates to the `stream_id::none` flush that emits ACKs and
  // handshake frames, the same work `quic_no_op_plugin::drain` does.
  [[nodiscard]] bool drain(time_point_t now) {
    for (;;) {
      quic_stream_id stream_id = quic_stream_id::none;
      std::span<const iovec> vecs;
      stream_chunk chunk_fin = stream_chunk::more;
      if (h3_ && !h3_.writev_stream(stream_id, vecs, chunk_fin)) return false;
      const auto flags =
          (chunk_fin == stream_chunk::fin)
              ? write_stream_flags::fin
              : write_stream_flags::none;

      auto out = io_.borrow_send_buffer();
      if (!out) return true;
      uint64_t accepted = 0;
      const auto status =
          io_.conn().writev_stream(stream_id, vecs, out, accepted, flags, now);
      // Draining/closing is a connection-level state: ngtcp2 will emit nothing
      // more, so this is a clean stop, not a failure.
      if (status == quic_status::draining || status == quic_status::closing)
        return true;
      if (status != quic_status::ok) return false;

      // Tell nghttp3 how much of the offered stream data ngtcp2 took (must be
      // reported even when 0, e.g. a pure FIN, so nghttp3 advances its
      // offset).
      if (h3_ && stream_id != quic_stream_id::none &&
          !h3_.add_write_offset(stream_id, accepted))
        return false;
      if (out.payload_bytes().empty()) return true;
      (void)io_.send_packet(std::move(out));
    }
  }

#pragma endregion
#pragma region Accessors

  // The peer's HTTP/3 SETTINGS, populated once its control stream's SETTINGS
  // frame has been decoded. Empty until then. Exposed for tests / diagnostics.
  [[nodiscard]] bool has_peer_settings() const noexcept {
    return peer_settings_.has_value();
  }
  [[nodiscard]] const std::optional<http3_settings>&
  peer_settings() const noexcept {
    return peer_settings_;
  }

#pragma endregion
#pragma region Helpers
private:
  // Create the nghttp3 connection once. It needs no QUIC stream credit or
  // keys, so it can run as soon as any path needs nghttp3 (the read path or
  // the bind path); nghttp3's `set_handlers` was wired in the ctor. Returns
  // false only on nghttp3 NOMEM.
  [[nodiscard]] bool ensure_h3_init() noexcept {
    if (h3_) return true;
    return h3_.init(io_.conn().role());
  }

  // Open and bind the three local unidirectional HTTP/3 streams (control,
  // QPACK encoder, QPACK decoder). Requires nghttp3 initialized and the 1-RTT
  // TX key installed. If the peer has not yet granted unidirectional-stream
  // credit, the first open returns `stream_id_blocked`; that is not an error,
  // so we defer and retry from `on_extend_max_local_streams_uni`. The three
  // streams draw from one credit pool (the peer advertises three), so once the
  // first opens the other two do too; an unexpected failure there is fatal.
  [[nodiscard]] bool try_bind_streams() noexcept {
    if (streams_bound_) return true;
    quic_stream_id control = quic_stream_id::none;
    const auto status = io_.conn().open_uni_stream(control);
    if (status == quic_status::stream_id_blocked) return true;
    if (status != quic_status::ok) return false;
    if (!h3_.bind_control_stream(control)) return false;

    quic_stream_id enc = quic_stream_id::none;
    quic_stream_id dec = quic_stream_id::none;
    if (io_.conn().open_uni_stream(enc) != quic_status::ok) return false;
    if (io_.conn().open_uni_stream(dec) != quic_status::ok) return false;
    if (!h3_.bind_qpack_streams(enc, dec)) return false;

    streams_bound_ = true;
    return true;
  }

  // Return `n` consumed receive bytes to both QUIC flow-control windows so the
  // peer's send window reopens. `on_recv_stream_data` only ever fires for
  // streams we receive on (never a local unidirectional stream), so the
  // stream-level extend is always valid; it returns `ok` for an unknown stream
  // and fails only on NOMEM.
  [[nodiscard]] bool
  credit_flow_control(quic_stream_id stream_id, size_t n) noexcept {
    if (n == 0) return true;
    if (io_.conn().extend_max_stream_offset(stream_id, n) != quic_status::ok)
      return false;
    io_.conn().extend_max_offset(n);
    return true;
  }

#pragma endregion
#pragma region Data members

  quic_session_io& io_;
  http3_conn h3_;
  std::optional<http3_settings> peer_settings_;
  bool streams_bound_{false};

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
