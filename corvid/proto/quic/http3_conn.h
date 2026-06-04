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
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <sys/uio.h>
#include <utility>

#include <nghttp3/nghttp3.h>
#include <openssl/rand.h>

#include "../../infra/exception_firewalls.h"
#include "../../infra/log.h"
#include "../../strings/core/conversion.h"
#include "http3_header.h"
#include "quic_header.h"
#include "quic_ssl_ctx.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region h3_error_code

// Wrapper over the HTTP/3 application error codes nghttp3 surfaces on stream
// teardown (`on_h3_stream_close`, `on_stop_sending`, `on_reset_stream`). The
// named values mirror nghttp3's `NGHTTP3_H3_*` (RFC 9114 sec. 8.1) and
// `NGHTTP3_QPACK_*` (RFC 9204) constants; `no_error` (0x0100) is the
// clean-close code.
//
// This is an open value space, not a closed set: the code rides a QUIC
// application error field, a 62-bit varint the peer may populate with any
// value, including codes from another application protocol layered over QUIC
// or RFC 9114 GREASE. An unrecognized code is still a valid `h3_error_code`,
// just unnamed, so the underlying type stays 64-bit and callers must not
// assume the value matches one of the named constants.
// NOLINTNEXTLINE(performance-enum-size)
enum class h3_error_code : uint64_t {
  no_error = NGHTTP3_H3_NO_ERROR, // 0x0100
  general_protocol_error = NGHTTP3_H3_GENERAL_PROTOCOL_ERROR,
  internal_error = NGHTTP3_H3_INTERNAL_ERROR,
  stream_creation_error = NGHTTP3_H3_STREAM_CREATION_ERROR,
  closed_critical_stream = NGHTTP3_H3_CLOSED_CRITICAL_STREAM,
  frame_unexpected = NGHTTP3_H3_FRAME_UNEXPECTED,
  frame_error = NGHTTP3_H3_FRAME_ERROR,
  excessive_load = NGHTTP3_H3_EXCESSIVE_LOAD,
  id_error = NGHTTP3_H3_ID_ERROR,
  settings_error = NGHTTP3_H3_SETTINGS_ERROR,
  missing_settings = NGHTTP3_H3_MISSING_SETTINGS,
  request_rejected = NGHTTP3_H3_REQUEST_REJECTED,
  request_cancelled = NGHTTP3_H3_REQUEST_CANCELLED,
  request_incomplete = NGHTTP3_H3_REQUEST_INCOMPLETE,
  message_error = NGHTTP3_H3_MESSAGE_ERROR,
  connect_error = NGHTTP3_H3_CONNECT_ERROR,
  version_fallback = NGHTTP3_H3_VERSION_FALLBACK,
  qpack_decompression_failed = NGHTTP3_QPACK_DECOMPRESSION_FAILED, // 0x0200
  qpack_encoder_stream_error = NGHTTP3_QPACK_ENCODER_STREAM_ERROR,
  qpack_decoder_stream_error = NGHTTP3_QPACK_DECODER_STREAM_ERROR,
};
consteval auto corvid_enum_spec(h3_error_code*) {
  return corvid::enums::sequence::make_sequence_enum_spec<h3_error_code,
      "256,no_error,general_protocol_error,internal_error,"
      "stream_creation_error,closed_critical_stream,frame_unexpected,"
      "frame_error,excessive_load,id_error,settings_error,missing_settings,"
      "request_rejected,request_cancelled,request_incomplete,message_error,"
      "connect_error,version_fallback|512,qpack_decompression_failed,"
      "qpack_encoder_stream_error,qpack_decoder_stream_error">();
}

#pragma endregion

#pragma region http3_settings

// The peer's HTTP/3 SETTINGS, copied out of nghttp3's `nghttp3_proto_settings`
// (the values nghttp3 recognizes) for delivery to `on_recv_settings`.
//
// A plain value type, copied during the callback because the source pointer
// does not outlive it. The two flag fields are exposed as `bool` (nghttp3
// stores them as nonzero-means-enabled `uint8_t`); the rest are widened to
// `uint64_t` to match the wire varint range and stay platform-independent.
struct http3_settings {
  // Maximum header section size the peer will accept (SETTINGS_MAX_FIELD_-
  // SECTION_SIZE).
  uint64_t max_field_section_size{};

  // Maximum QPACK dynamic-table capacity the peer allows.
  uint64_t qpack_max_dtable_capacity{};

  // Maximum number of streams the peer can have blocked on QPACK decoding.
  uint64_t qpack_blocked_streams{};

  // Peer enabled the Extended CONNECT method (RFC 9220). Meaningful on the
  // server; nghttp3 ignores it on the client.
  bool enable_connect_protocol{};

  // Peer enabled HTTP/3 Datagrams (RFC 9297).
  bool h3_datagram{};
};

#pragma endregion
#pragma region body_vecs

// The next slice of an outbound body, returned by
// `http3_conn_handlers::on_send_data_ready` when nghttp3 pulls request
// (client) or response (server) body data.
//
// `iov` is a gather view of one or more segments. The iovec array itself need
// only stay valid for the call (the trampoline copies it into nghttp3's vec
// array at once), but the bytes the segments point at MUST stay valid until
// nghttp3 reports them acked via `on_acked_stream_data` (the buffer-lifetime
// invariant).
//
// The producer must cap `iov` at the `max_vecs` budget passed to
// `on_send_data_ready`, since nghttp3 lends a fixed-size vec array. `eof`
// marks the final slice; after it, the send side closes. `block` (empty `iov`,
// not `eof`) means "no bytes yet": nghttp3 pauses the stream until
// `resume_stream`.
struct body_vecs {
  std::span<const iovec> iov;
  bool eof{false};
  bool block{false};
};

#pragma endregion
#pragma region conn handlers

// Protocol-neutral upcall contract that `http3_conn`'s static nghttp3 callback
// trampolines forward into. The HTTP/3 upper-layer plugin inherits this; it
// owns the `http3_conn` and installs itself via `http3_conn::set_handlers`
// after construction.
//
// This mirrors `quic_conn_handlers` one layer up: where `quic_conn_handlers`
// surfaces ngtcp2's transport callbacks, `http3_conn_handlers` surfaces
// nghttp3's HTTP/3 callbacks (headers, body, settings, stream lifecycle). The
// plugin bridges between the two: ngtcp2 stream bytes arrive via
// `quic_conn_handlers::on_recv_stream_data`, get fed to
// `http3_conn::read_stream`, and re-emerge here as decoded HEADERS / DATA.
//
// All upcalls run on the loop thread inside an nghttp3 callback frame, itself
// nested inside an ngtcp2 `read_pkt` frame. The same no-recursive-writes rule
// applies: these hooks may update state and call nghttp3's non-write APIs, but
// outbound packet emission happens only in the session's per-turn drain.
//
// Each upcall returns `bool`: `true` to continue (trampoline returns 0 to
// nghttp3), `false` to signal callback failure (trampoline returns
// `NGHTTP3_ERR_CALLBACK_FAILURE`; nghttp3 bails and the connection must be
// closed).
//
// Noexcept policy is identical to `quic_conn_handlers`: the trampolines in
// `http3_conn` are the firewall (each wraps the upcall in `try_callback`), so
// these virtuals are intentionally NOT `noexcept` and may throw on allocation
// failure; the throw is caught at the trampoline and reported to nghttp3 as
// `NGHTTP3_ERR_CALLBACK_FAILURE`. Overrides MAY mark themselves `noexcept` if
// they cannot throw.
//
// Every per-stream upcall takes a trailing `void* stream_user_data`: the
// opaque pointer associated with the stream. It is set when a request /
// response is submitted with a body (the pointer passed to `submit_request`,
// so `on_read_body` and the response upcalls can recover per-stream state)
// or explicitly via `set_stream_user_data`; it is null otherwise. The
// connection-level `on_recv_settings` has no such parameter.
//
// Defaults are no-op `true`, so concrete plugins override only what they need.
class http3_conn_handlers {
public:
  http3_conn_handlers() = default;
  http3_conn_handlers(const http3_conn_handlers&) = delete;
  http3_conn_handlers& operator=(const http3_conn_handlers&) = delete;
  virtual ~http3_conn_handlers() = default;

#pragma region Headers

  // An incoming HTTP field section (request or response HEADERS) has started
  // on `stream_id`. Zero or more `on_recv_header` upcalls follow, then
  // `on_end_headers`.
  [[nodiscard]] virtual bool
  on_begin_headers(quic_stream_id stream_id, void* stream_user_data) {
    (void)stream_id;
    (void)stream_user_data;
    return true;
  }

  // One decoded HTTP field. `token` identifies a known header name, or is
  // `qpack_token::unknown` for a name nghttp3 does not recognize. `name` and
  // `value` are valid only for the call; copy if you need to retain them (they
  // are nghttp3 reference-counted buffers behind the view, but the view itself
  // is ephemeral). `flags` is the `nv_flags` bitmask; on the receive path only
  // `never_index` is normally set.
  [[nodiscard]] virtual bool on_recv_header(quic_stream_id stream_id,
      qpack_token token, std::string_view name, std::string_view value,
      nv_flags flags, void* stream_user_data) {
    (void)stream_id;
    (void)token;
    (void)name;
    (void)value;
    (void)flags;
    (void)stream_user_data;
    return true;
  }

  // The incoming HTTP field section on `stream_id` has ended. `chunk_fin` is
  // `stream_chunk::fin` if the stream's receiving side also ended with this
  // section (a request or response with no body).
  [[nodiscard]] virtual bool on_end_headers(quic_stream_id stream_id,
      stream_chunk chunk_fin, void* stream_user_data) {
    (void)stream_id;
    (void)chunk_fin;
    (void)stream_user_data;
    return true;
  }

#pragma endregion
#pragma region Trailers

  // An incoming HTTP trailer field section (the optional HEADERS after the
  // body) has started on `stream_id`.
  [[nodiscard]] virtual bool
  on_begin_trailers(quic_stream_id stream_id, void* stream_user_data) {
    (void)stream_id;
    (void)stream_user_data;
    return true;
  }

  // One decoded HTTP trailer field, with the same `token` / `name` / `value` /
  // `flags` contract as `on_recv_header`.
  [[nodiscard]] virtual bool on_recv_trailer(quic_stream_id stream_id,
      qpack_token token, std::string_view name, std::string_view value,
      nv_flags flags, void* stream_user_data) {
    (void)stream_id;
    (void)token;
    (void)name;
    (void)value;
    (void)flags;
    (void)stream_user_data;
    return true;
  }

  // The incoming trailer section on `stream_id` has ended. `chunk_fin` is
  // `stream_chunk::fin` if the stream's receiving side also ended here (the
  // usual case, since trailers are the last thing on a stream).
  [[nodiscard]] virtual bool on_end_trailers(quic_stream_id stream_id,
      stream_chunk chunk_fin, void* stream_user_data) {
    (void)stream_id;
    (void)chunk_fin;
    (void)stream_user_data;
    return true;
  }

#pragma endregion
#pragma region Body and stream

  // Inbound HTTP body bytes (DATA frame payload) on `stream_id`. Valid only
  // for the call; copy before returning. Flow-control credit for these bytes
  // is the application's responsibility (via the underlying QUIC stack).
  [[nodiscard]] virtual bool on_recv_data(quic_stream_id stream_id,
      std::span<const uint8_t> data, void* stream_user_data) {
    (void)stream_id;
    (void)data;
    (void)stream_user_data;
    return true;
  }

  // The receiving side of `stream_id` is closed: for a server, the request
  // arrived in full; for a client, the response did.
  [[nodiscard]] virtual bool
  on_end_stream(quic_stream_id stream_id, void* stream_user_data) {
    (void)stream_id;
    (void)stream_user_data;
    return true;
  }

  // nghttp3 consumed `consumed` bytes for a stream that had been blocked on
  // inter-stream synchronization (e.g., QPACK). The application owes that much
  // QUIC flow-control credit. Distinct from `read_stream`'s direct return,
  // which covers the unblocked path.
  [[nodiscard]] virtual bool on_deferred_consume(quic_stream_id stream_id,
      size_t consumed, void* stream_user_data) {
    (void)stream_id;
    (void)consumed;
    (void)stream_user_data;
    return true;
  }

  // `stream_id` is fully closed at the HTTP/3 layer. `app_error_code` is the
  // HTTP/3 application error code for the closure (`h3_error_code::no_error`
  // for a clean close). Named distinctly from the transport-level
  // `quic_conn_handlers::on_stream_close` so a bridge plugin inheriting both
  // bases can override each without one hiding the other.
  [[nodiscard]] virtual bool on_h3_stream_close(quic_stream_id stream_id,
      h3_error_code app_error_code, void* stream_user_data) {
    (void)stream_id;
    (void)app_error_code;
    (void)stream_user_data;
    return true;
  }

  // The send side of `stream_id` was acknowledged by `datalen` bytes; the
  // application may release that prefix of its retained send buffers. nghttp3
  // tracks its own offsets separately; this mirrors the QUIC-level ack into
  // the HTTP/3 layer.
  [[nodiscard]] virtual bool on_acked_stream_data(quic_stream_id stream_id,
      uint64_t datalen, void* stream_user_data) {
    (void)stream_id;
    (void)datalen;
    (void)stream_user_data;
    return true;
  }

  // nghttp3 is pulling the next slice of the outbound body for `stream_id` (a
  // request body on the client, a response body on the server), to frame into
  // DATA. Fill at most `max_vecs` segments, nghttp3's vec budget for this
  // pull. Return the next `body_vecs`; the default has no body (immediate
  // `eof`). `stream_user_data` is whatever was associated with the stream at
  // submit time. The returned bytes MUST stay valid until acked (see
  // `body_vecs`).
  [[nodiscard]] virtual body_vecs on_send_data_ready(quic_stream_id stream_id,
      size_t max_vecs, void* stream_user_data) {
    (void)stream_id;
    (void)max_vecs;
    (void)stream_user_data;
    return {.iov = {}, .eof = true};
  }

#pragma endregion
#pragma region Stream reset

  // nghttp3 asks the application to send STOP_SENDING on `stream_id` (it no
  // longer wants the peer's data). The plugin forwards this to ngtcp2 via
  // `quic_conn::shutdown_stream_read`.
  [[nodiscard]] virtual bool on_stop_sending(quic_stream_id stream_id,
      h3_error_code app_error_code, void* stream_user_data) {
    (void)stream_id;
    (void)app_error_code;
    (void)stream_user_data;
    return true;
  }

  // nghttp3 asks the application to reset (RESET_STREAM) the sending side of
  // `stream_id`. The plugin forwards this to ngtcp2 via
  // `quic_conn::shutdown_stream_write`.
  [[nodiscard]] virtual bool on_reset_stream(quic_stream_id stream_id,
      h3_error_code app_error_code, void* stream_user_data) {
    (void)stream_id;
    (void)app_error_code;
    (void)stream_user_data;
    return true;
  }

#pragma endregion
#pragma region Settings

  // The peer's SETTINGS frame arrived on its control stream, carrying
  // `settings` (the values nghttp3 recognizes). Valid only for the call; copy
  // out anything to retain.
  [[nodiscard]] virtual bool on_recv_settings(const http3_settings& settings) {
    (void)settings;
    return true;
  }

#pragma endregion
};

#pragma endregion
#pragma region http3_conn

// C++ wrapper over nghttp3's per-connection API. Owns a single `nghttp3_conn`
// (the HTTP/3 framing + QPACK state machine), shaped like `quic_conn` one
// layer down. nghttp3 is crypto-agnostic and transport-agnostic: it produces
// and consumes HTTP/3 stream bytes, which the owning plugin shuttles to and
// from `quic_conn` (ngtcp2). QPACK encoder / decoder state lives inside the
// `nghttp3_conn`; there are no separate context objects to own.
//
// Like `quic_conn`, this is neither copyable nor movable: nghttp3 stores our
// `this` as `conn_user_data` at construction, so the wrapper's address must
// stay fixed. Hold one by value at a pinned address (a member of the HTTP/3
// plugin, itself a member of a `shared_ptr`-pinned session).
//
// Usage:
//   * Construct, then `set_handlers`, then `init(role)`.
//   * Bind the HTTP/3 control and QPACK streams (`bind_control_stream`,
//     `bind_qpack_streams`) once the stream IDs are known.
//   * Per turn: feed inbound stream bytes via `read_stream`; pull outbound
//     stream bytes via `writev_stream` + `add_write_offset`; report QUIC acks
//     via `add_ack_offset`; report stream closure via `close_stream`.
class http3_conn {
public:
  // Upper bound on fields per `submit_request` / `submit_response`. These are
  // our own outgoing headers, so this is a sanity cap (lets the submit scratch
  // live on the stack instead of allocating), not a data-driven limit.
  static constexpr size_t max_submit_fields = 64;

  http3_conn() = default;
  http3_conn(const http3_conn&) = delete;
  http3_conn(http3_conn&&) = delete;
  http3_conn& operator=(const http3_conn&) = delete;
  http3_conn& operator=(http3_conn&&) = delete;

#pragma region Construction

  // Install the upcall target. Must be called before `init`, since the
  // trampolines deref `handlers_` unconditionally and the first callback can
  // fire during the initial `read_stream`.
  void set_handlers(http3_conn_handlers* handlers) noexcept {
    handlers_ = handlers;
  }

  // Create the underlying `nghttp3_conn` in the given role with
  // library-default settings. Returns false if already initialized or nghttp3
  // reports an error (only `NGHTTP3_ERR_NOMEM` in practice).
  [[nodiscard]] bool init(connection_role role) {
    if (conn_) return false;
    nghttp3_settings settings;
    nghttp3_settings_default(&settings);
    // TODO: Consider enabling `h3_datagram` here.
    nghttp3_conn* raw{};
    role_ = role;
    const int rv =
        (role == connection_role::server)
            ? nghttp3_conn_server_new(&raw, &callbacks, &settings, nullptr,
                  this)
            : nghttp3_conn_client_new(&raw, &callbacks, &settings, nullptr,
                  this);
    if (rv != 0) return log_error("nghttp3_conn_new", rv);
    conn_.reset(raw);
    return true;
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool ok() const noexcept { return !!conn_; }
  [[nodiscard]] explicit operator bool() const noexcept { return !!conn_; }
  [[nodiscard]] bool operator!() const noexcept { return !conn_; }
  [[nodiscard]] connection_role role() const noexcept { return role_; }
  [[nodiscard]] auto native(this auto& self) { return self.conn_.get(); }

#pragma endregion
#pragma region Stream binding

  // Bind `stream_id` as the outgoing HTTP/3 control stream (a
  // locally initiated unidirectional stream). nghttp3 queues the stream type
  // byte + SETTINGS for the next `writev_stream`.
  [[nodiscard]] bool bind_control_stream(quic_stream_id stream_id) {
    return ok("nghttp3_conn_bind_control_stream",
        nghttp3_conn_bind_control_stream(conn_.get(), from(stream_id)));
  }

  // Bind the outgoing QPACK encoder and decoder streams (two locally initiated
  // unidirectional streams).
  [[nodiscard]] bool bind_qpack_streams(quic_stream_id enc_stream_id,
      quic_stream_id dec_stream_id) {
    return ok("nghttp3_conn_bind_qpack_streams",
        nghttp3_conn_bind_qpack_streams(conn_.get(), from(enc_stream_id),
            from(dec_stream_id)));
  }

#pragma endregion
#pragma region Submit

  // Submit a request on `stream_id`, a client-initiated bidirectional stream
  // the caller has opened via `quic_conn::open_bidi_stream`. `fields` are the
  // request HEADERS, with the pseudo-headers (":method", ":scheme",
  // ":authority", ":path") first per RFC 9114. nghttp3 queues the HEADERS for
  // the next `writev_stream` and copies `fields` (names and values), so the
  // views need not outlive this call.
  //
  // With `with_body == false` (the default) this is the header-only path: the
  // stream ends after the HEADERS (no request body). With `with_body == true`
  // a body data reader is installed, so nghttp3 will pull the body via
  // `on_read_body` upcalls (the stream ends when `on_read_body` reports eof);
  // `stream_user_data` is associated with the stream and handed back to those
  // upcalls (and the response upcalls). Returns false if `fields` exceeds
  // `max_submit_fields`.
  [[nodiscard]] bool submit_request(quic_stream_id stream_id,
      std::span<const http3_field> fields, bool with_body = false,
      void* stream_user_data = nullptr) {
    assert(role_ == connection_role::client);
    std::array<nghttp3_nv, max_submit_fields> nva{};
    if (!fill_nv(fields, nva)) return false;
    const nghttp3_data_reader dr{.read_data = &read_data};
    return ok("nghttp3_conn_submit_request",
        nghttp3_conn_submit_request(conn_.get(), from(stream_id), nva.data(),
            fields.size(), with_body ? &dr : nullptr, stream_user_data));
  }

  // Submit a response on the request's `stream_id`. `fields` are the response
  // HEADERS (":status" first). nghttp3 queues the HEADERS for the next
  // `writev_stream` and copies `fields`, so the views need not outlive this
  // call.
  //
  // With `with_body == false` (the default) this is the header-only path: the
  // stream ends after the HEADERS (no response body). With `with_body == true`
  // a body data reader is installed, so nghttp3 pulls the body via
  // `on_send_data_ready` upcalls (the stream ends when one reports eof).
  // Unlike `submit_request`, this takes no `stream_user_data`: a server
  // response runs on a peer-initiated stream whose user data is already set,
  // so those upcalls route back without it. Same copy / cap notes as
  // `submit_request`.
  [[nodiscard]] bool submit_response(quic_stream_id stream_id,
      std::span<const http3_field> fields, bool with_body = false) {
    assert(role_ == connection_role::server);
    std::array<nghttp3_nv, max_submit_fields> nva{};
    if (!fill_nv(fields, nva)) return false;
    const nghttp3_data_reader dr{.read_data = &read_data};
    return ok("nghttp3_conn_submit_response",
        nghttp3_conn_submit_response(conn_.get(), from(stream_id), nva.data(),
            fields.size(), with_body ? &dr : nullptr));
  }

#pragma endregion
#pragma region Inbound

  // Feed `data` (received on `stream_id`, with `chunk_fin ==
  // stream_chunk::fin` marking the peer's final bytes) into nghttp3. Decoded
  // HEADERS / DATA / lifecycle events surface as `http3_conn_handlers` upcalls
  // during this call. On success `consumed` is the number of bytes nghttp3
  // accounted for QUIC flow-control purposes (which excludes DATA-frame body
  // bytes; those are credited via the handler / `on_deferred_consume`).
  // Returns false on a connection error, after which only destruction is
  // legal.
  [[nodiscard]] bool read_stream(quic_stream_id stream_id,
      std::span<const uint8_t> data, stream_chunk chunk_fin,
      size_t& consumed) {
    consumed = 0;
    const nghttp3_ssize rv = nghttp3_conn_read_stream(conn_.get(),
        from(stream_id), data.data(), data.size(), *chunk_fin);
    if (rv < 0)
      return log_error("nghttp3_conn_read_stream", static_cast<int>(rv));
    consumed = static_cast<size_t>(rv);
    return true;
  }

  // Report that QUIC has acknowledged `datalen` more bytes on `stream_id`'s
  // send side, so nghttp3 can release the corresponding retained body buffers.
  [[nodiscard]] bool
  add_ack_offset(quic_stream_id stream_id, uint64_t datalen) {
    return ok("nghttp3_conn_add_ack_offset",
        nghttp3_conn_add_ack_offset(conn_.get(), from(stream_id), datalen));
  }

  // Tell nghttp3 that `stream_id` has closed at the QUIC layer with
  // `app_error_code`. nghttp3 releases the stream's state and may fire
  // `on_h3_stream_close`.
  [[nodiscard]] bool
  close_stream(quic_stream_id stream_id, h3_error_code app_error_code) {
    return ok("nghttp3_conn_close_stream",
        nghttp3_conn_close_stream(conn_.get(), from(stream_id),
            *app_error_code));
  }

#pragma endregion
#pragma region Outbound

  // Pull the next chunk of outbound HTTP/3 stream data. On success `stream_id`
  // names the stream to write (or `quic_stream_id::none` if nothing is
  // pending, in which case `vecs` is empty), `vecs` is a view over an internal
  // scratch array of `iovec` valid until the next `writev_stream` call, and
  // `chunk_fin` is `stream_chunk::fin` if these bytes end the stream. The
  // caller hands `vecs` straight to `quic_conn::writev_stream` (which also
  // takes `iovec`), then calls `add_write_offset` with the accepted byte
  // count. Returns false on a connection error.
  //
  // Note the nghttp3 contract: a zero-length result with a real `stream_id`
  // and `chunk_fin == stream_chunk::fin` means "no bytes, but the write side
  // closes here", which the caller must still relay (an `add_write_offset` of
  // 0).
  [[nodiscard]] bool writev_stream(quic_stream_id& stream_id,
      std::span<const iovec>& vecs, stream_chunk& chunk_fin) {
    int64_t raw_id{-1};
    int raw_fin{};
    // `iovec` and `nghttp3_vec` are layout-compatible (pointer + length); the
    // cast confines the nghttp3 spelling to this C call, matching how
    // `quic_conn::writev_stream` reinterprets `iovec` as `ngtcp2_vec`.
    const nghttp3_ssize rv = nghttp3_conn_writev_stream(conn_.get(), &raw_id,
        &raw_fin, reinterpret_cast<nghttp3_vec*>(vecs_.data()), vecs_.size());
    if (rv < 0)
      return log_error("nghttp3_conn_writev_stream", static_cast<int>(rv));
    stream_id = make_stream_id(raw_id);
    vecs = {vecs_.data(), static_cast<size_t>(rv)};
    chunk_fin = make_stream_chunk(raw_fin);
    return true;
  }

  // Report how many bytes the QUIC stack accepted for `stream_id` from the
  // most recent `writev_stream`. Must be called even when `n` is 0 (e.g., a
  // pure fin), so nghttp3 advances its own offset.
  [[nodiscard]] bool add_write_offset(quic_stream_id stream_id, size_t n) {
    return ok("nghttp3_conn_add_write_offset",
        nghttp3_conn_add_write_offset(conn_.get(), from(stream_id), n));
  }

#pragma endregion
#pragma region Flow control

  // Tell nghttp3 that `stream_id` is blocked by QUIC flow control, so it stops
  // offering that stream's bytes in `writev_stream` until the matching
  // `unblock_stream`. The caller blocks a stream when
  // `quic_conn::writev_stream` accepts fewer bytes than offered because the
  // stream's send window is full.
  void block_stream(quic_stream_id stream_id) noexcept {
    nghttp3_conn_block_stream(conn_.get(), from(stream_id));
  }

  // Tell nghttp3 that `stream_id`, previously blocked via `block_stream`, is
  // writable again (the peer extended its flow-control window). nghttp3
  // reports success even when it has no such stream, so false here means only
  // NOMEM.
  [[nodiscard]] bool unblock_stream(quic_stream_id stream_id) {
    return ok("nghttp3_conn_unblock_stream",
        nghttp3_conn_unblock_stream(conn_.get(), from(stream_id)));
  }

#pragma endregion
#pragma region Stream user data

  // Associate `stream_user_data` with `stream_id` after the fact; nghttp3 then
  // passes it to every per-stream upcall (the trailing `stream_user_data`
  // parameter on `http3_conn_handlers`), letting the plugin hang per-request
  // state off a stream. Returns false if nghttp3 has no such stream.
  [[nodiscard]] bool
  set_stream_user_data(quic_stream_id stream_id, void* stream_user_data) {
    return ok("nghttp3_conn_set_stream_user_data",
        nghttp3_conn_set_stream_user_data(conn_.get(), from(stream_id),
            stream_user_data));
  }

#pragma endregion
#pragma region Stream shutdown

  // Tell nghttp3 the read side of `stream_id` is abruptly closed (the peer
  // sent RESET_STREAM at the QUIC layer); nghttp3 discards any further inbound
  // data and pending stream state. The plugin forwards `quic_conn_handlers::
  // on_stream_reset` here. A no-op (returns true) for streams nghttp3 does not
  // track as client-bidirectional; false means only NOMEM / QPACK overflow.
  [[nodiscard]] bool shutdown_stream_read(quic_stream_id stream_id) {
    return ok("nghttp3_conn_shutdown_stream_read",
        nghttp3_conn_shutdown_stream_read(conn_.get(), from(stream_id)));
  }

  // Tell nghttp3 that further writes to `stream_id` are prohibited (the peer
  // sent STOP_SENDING at the QUIC layer); like `block_stream`, but permanent:
  // `unblock_stream` will not reopen it. The plugin forwards `quic_conn_-
  // handlers::on_stream_stop_sending` here.
  void shutdown_stream_write(quic_stream_id stream_id) noexcept {
    nghttp3_conn_shutdown_stream_write(conn_.get(), from(stream_id));
  }

#pragma endregion
private:
#pragma region Trampolines

  // nghttp3 trampolines: recover the typed `http3_conn*` from `conn_user_data`
  // and forward into the installed `http3_conn_handlers`, with each upcall run
  // through `try_callback` so a thrown exception becomes
  // `NGHTTP3_ERR_CALLBACK_FAILURE` rather than crossing the C ABI. Each
  // per-stream trampoline also forwards nghttp3's `stream_user_data` to the
  // matching upcall; it is null today since `http3_conn` never sets it.
  static int on_begin_headers(nghttp3_conn*, int64_t stream_id,
      void* conn_user_data, void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_begin_headers(make_stream_id(stream_id),
          stream_user_data);
    });
  }

  static int on_recv_header(nghttp3_conn*, int64_t stream_id, int32_t token,
      nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags,
      void* conn_user_data, void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      const nghttp3_vec n = nghttp3_rcbuf_get_buf(name);
      const nghttp3_vec v = nghttp3_rcbuf_get_buf(value);
      return self->handlers_->on_recv_header(make_stream_id(stream_id),
          static_cast<qpack_token>(token),
          strings::as_string_view(std::span{n.base, n.len}),
          strings::as_string_view(std::span{v.base, v.len}),
          static_cast<nv_flags>(flags), stream_user_data);
    });
  }

  static int on_end_headers(nghttp3_conn*, int64_t stream_id, int fin,
      void* conn_user_data, void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_end_headers(make_stream_id(stream_id),
          make_stream_chunk(fin), stream_user_data);
    });
  }

  static int on_begin_trailers(nghttp3_conn*, int64_t stream_id,
      void* conn_user_data, void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_begin_trailers(make_stream_id(stream_id),
          stream_user_data);
    });
  }

  static int on_recv_trailer(nghttp3_conn*, int64_t stream_id, int32_t token,
      nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags,
      void* conn_user_data, void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      const nghttp3_vec n = nghttp3_rcbuf_get_buf(name);
      const nghttp3_vec v = nghttp3_rcbuf_get_buf(value);
      return self->handlers_->on_recv_trailer(make_stream_id(stream_id),
          static_cast<qpack_token>(token),
          strings::as_string_view(std::span{n.base, n.len}),
          strings::as_string_view(std::span{v.base, v.len}),
          static_cast<nv_flags>(flags), stream_user_data);
    });
  }

  static int on_end_trailers(nghttp3_conn*, int64_t stream_id, int fin,
      void* conn_user_data, void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_end_trailers(make_stream_id(stream_id),
          make_stream_chunk(fin), stream_user_data);
    });
  }

  static int on_recv_data(nghttp3_conn*, int64_t stream_id,
      const uint8_t* data, size_t datalen, void* conn_user_data,
      void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_recv_data(make_stream_id(stream_id),
          {data, datalen}, stream_user_data);
    });
  }

  static int on_end_stream(nghttp3_conn*, int64_t stream_id,
      void* conn_user_data, void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_end_stream(make_stream_id(stream_id),
          stream_user_data);
    });
  }

  static int on_deferred_consume(nghttp3_conn*, int64_t stream_id,
      size_t consumed, void* conn_user_data, void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_deferred_consume(make_stream_id(stream_id),
          consumed, stream_user_data);
    });
  }

  static int on_stream_close(nghttp3_conn*, int64_t stream_id,
      uint64_t app_error_code, void* conn_user_data,
      void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_h3_stream_close(make_stream_id(stream_id),
          static_cast<h3_error_code>(app_error_code), stream_user_data);
    });
  }

  static int on_acked_stream_data(nghttp3_conn*, int64_t stream_id,
      uint64_t datalen, void* conn_user_data,
      void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_acked_stream_data(make_stream_id(stream_id),
          datalen, stream_user_data);
    });
  }

  // Body data source (installed per request/response via
  // `nghttp3_data_reader`, not part of the callback table). Called to provide
  // body data for an outbound stream when nghttp3 is ready to frame it into a
  // DATA frame.
  //
  // Unlike the other trampolines, which return the int 0 / CALLBACK_FAILURE
  // convention, this returns `nghttp3_ssize`: the number of `vec` entries
  // filled, with eof signaled via `*pflags` and back-pressure via
  // `NGHTTP3_ERR_WOULDBLOCK`.
  //
  // We lend `on_send_data_ready` the `veccnt` budget; it caps its gather at
  // that, so the returned segments fit `vec` as-is and we copy them wholesale
  // (`iovec` and `nghttp3_vec` are layout-compatible, pointer + length).
  static nghttp3_ssize read_data(nghttp3_conn*, int64_t stream_id,
      nghttp3_vec* vec, size_t veccnt, uint32_t* pflags, void* conn_user_data,
      void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_or_log(
        [&]() -> nghttp3_ssize {
          const auto chunk = self->handlers_->on_send_data_ready(
              make_stream_id(stream_id), veccnt, stream_user_data);
          if (chunk.block) return NGHTTP3_ERR_WOULDBLOCK;
          assert(chunk.iov.size() <= veccnt);
          if (!chunk.iov.empty())
            std::memcpy(vec, chunk.iov.data(), chunk.iov.size_bytes());
          if (chunk.eof) *pflags |= NGHTTP3_DATA_FLAG_EOF;
          return static_cast<nghttp3_ssize>(chunk.iov.size());
        },
        static_cast<nghttp3_ssize>(NGHTTP3_ERR_CALLBACK_FAILURE));
  }

  static int on_stop_sending(nghttp3_conn*, int64_t stream_id,
      uint64_t app_error_code, void* conn_user_data,
      void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_stop_sending(make_stream_id(stream_id),
          static_cast<h3_error_code>(app_error_code), stream_user_data);
    });
  }

  static int on_reset_stream(nghttp3_conn*, int64_t stream_id,
      uint64_t app_error_code, void* conn_user_data,
      void* stream_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_reset_stream(make_stream_id(stream_id),
          static_cast<h3_error_code>(app_error_code), stream_user_data);
    });
  }

  static int on_recv_settings(nghttp3_conn*,
      const nghttp3_proto_settings* settings, void* conn_user_data) noexcept {
    auto* self = static_cast<http3_conn*>(conn_user_data);
    return try_callback([&] {
      return self->handlers_->on_recv_settings(http3_settings{
          .max_field_section_size = settings->max_field_section_size,
          .qpack_max_dtable_capacity = settings->qpack_max_dtable_capacity,
          .qpack_blocked_streams = settings->qpack_blocked_streams,
          .enable_connect_protocol = (settings->enable_connect_protocol != 0),
          .h3_datagram = (settings->h3_datagram != 0)});
    });
  }

#pragma endregion
#pragma region Helpers

  // nghttp3 uses `int64_t` stream IDs with `-1` as the "none" sentinel;
  // `quic_stream_id` is a `uint64_t` enum with the same `-1` bit pattern as
  // `none`. The casts round-trip cleanly in both directions.
  [[nodiscard]] static constexpr int64_t from(quic_stream_id id) noexcept {
    return static_cast<int64_t>(*id);
  }
  [[nodiscard]] static constexpr quic_stream_id make_stream_id(
      int64_t id) noexcept {
    return static_cast<quic_stream_id>(static_cast<uint64_t>(id));
  }

  // nghttp3 reports the stream FIN as an `int` (nonzero means this is the last
  // data on the stream); `stream_chunk` is the typed view (`fin` == 1).
  [[nodiscard]] static constexpr stream_chunk make_stream_chunk(
      int fin) noexcept {
    return static_cast<stream_chunk>(fin != 0);
  }

  // Fill `dest` with `destlen` unpredictable bytes (nghttp3's `rand` callback,
  // recommended to harden against a malicious peer). There is no way to signal
  // an error and we can't throw through C, so terminate in the unlikely event
  // the RNG fails. Mirrors `quic_conn`'s ngtcp2 rand callback one layer down.
  static void on_rand(uint8_t* dest, size_t destlen) noexcept {
    try_or_terminate([&] {
      if (destlen > static_cast<size_t>(std::numeric_limits<int>::max()))
        log::fatal("on_rand: destlen {} exceeds int max", destlen);
      if (RAND_bytes(dest, static_cast<int>(destlen)) != 1)
        log::fatal("on_rand: RAND_bytes failed");
      return true;
    });
  }

  // Convert one `http3_field` to the `nghttp3_nv` the submit calls take.
  // The name / value pointers alias the field's; nghttp3 copies them during
  // the synchronous submit (default flags), so they need only outlive the
  // call.
  [[nodiscard]] static nghttp3_nv to_nv(const http3_field& f) noexcept {
    return {.name = strings::as_byte_span(f.name).data(),
        .value = strings::as_byte_span(f.value).data(),
        .namelen = f.name.size(),
        .valuelen = f.value.size(),
        .flags = *f.flags};
  }

  // Fill `nva` with the `nghttp3_nv` form of `fields` of `http3_field` for a
  // submit call, or log and return false if `fields` exceeds the scratch
  // capacity.
  [[nodiscard]] static bool
  fill_nv(std::span<const http3_field> fields, std::span<nghttp3_nv> nva) {
    if (fields.size() > nva.size())
      return log::error("too many fields") && false;
    for (size_t i = 0; i < fields.size(); ++i) nva[i] = to_nv(fields[i]);
    return true;
  }

  // Translate a handler's `bool` into the int nghttp3 callbacks expect: `0` on
  // success, `NGHTTP3_ERR_CALLBACK_FAILURE` on failure.
  [[nodiscard]] static constexpr int success(bool good) noexcept {
    return good ? 0 : NGHTTP3_ERR_CALLBACK_FAILURE;
  }

  // Canonical trampoline body: run `fn` inside `try_or_log` (so a throw
  // becomes `false`), then convert to nghttp3's callback status.
  template<std::invocable F>
  requires std::same_as<std::invoke_result_t<F>, bool>
  [[nodiscard]] static int try_callback(F&& fn) noexcept {
    return success(try_or_log(std::forward<F>(fn)));
  }

  // Log an nghttp3 library error with its textual reason and return false, for
  // the `[[nodiscard]] bool` method translation.
  [[nodiscard]] static bool log_error(const char* what, int rv) {
    log::error("{} failed: {} ({})", what, nghttp3_strerror(rv), rv);
    return false;
  }

  // Translate a 0-or-negative nghttp3 status into `bool`, logging on error.
  [[nodiscard]] static bool ok(const char* what, int rv) {
    return rv == 0 ? true : log_error(what, rv);
  }

  // The nghttp3 callback table. Identical for both roles (only the `_new`
  // entry point differs); unmentioned slots are value-initialized to null,
  // which nghttp3 treats as "callback not installed".
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
  static constexpr nghttp3_callbacks callbacks{
      .acked_stream_data = &on_acked_stream_data,
      .stream_close = &on_stream_close,
      .recv_data = &on_recv_data,
      .deferred_consume = &on_deferred_consume,
      .begin_headers = &on_begin_headers,
      .recv_header = &on_recv_header,
      .end_headers = &on_end_headers,
      .begin_trailers = &on_begin_trailers,
      .recv_trailer = &on_recv_trailer,
      .end_trailers = &on_end_trailers,
      .stop_sending = &on_stop_sending,
      .end_stream = &on_end_stream,
      .reset_stream = &on_reset_stream,
      .rand = &on_rand,
      .recv_settings2 = &on_recv_settings,
  };
#pragma clang diagnostic pop

  using conn_ptr =
      std::unique_ptr<nghttp3_conn, decltype([](nghttp3_conn* p) noexcept {
        if (p) nghttp3_conn_del(p);
      })>;

#pragma endregion
#pragma region Data members

  // Scratch for `writev_stream`: nghttp3 fills these (reinterpreted as
  // `nghttp3_vec` at the call), we view them as `iovec`. 16 matches the count
  // ngtcp2's examples offer per packet; more rarely helps since a QUIC packet
  // is MTU-bounded.
  std::array<iovec, 16> vecs_{};

  // Upper-plugin upcalls, installed via `set_handlers` before `init`. Null
  // until then; trampolines deref unconditionally.
  http3_conn_handlers* handlers_{};

  connection_role role_{};
  conn_ptr conn_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
