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
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "../net_endpoint.h"
#include "../../enums/bool_enums.h"
#include "../../concurrency/timeouts.h"

#include "quic_header.h"
#include "quic_ssl_ctx.h"

// C++ wrapper over ngtcp2's per-connection API. Owns:
//   - an `ngtcp2_conn` (the QUIC state machine),
//   - an `SSL*` (the TLS state machine on top of which QUIC carries its
//     cryptographic handshake), and
//   - an `ngtcp2_crypto_ossl_ctx*` (the shim that lets ngtcp2 drive the
//     OpenSSL state machine).
// The shim's standard callback set is installed on the ngtcp2 conn, so a
// real TLS 1.3 handshake actually runs to completion against a properly
// configured peer.
//
// `quic_conn` is intentionally neither copyable nor movable: ngtcp2 stores
// our `this` pointer as `user_data` at construction time and the shim
// stores `&conn_ref_` (also pointing into `this`) on the SSL via
// `SSL_set_app_data`. Neither has a setter, so the wrapper's address must
// stay fixed for the lifetime of the underlying objects. Hold one by
// value at a stable address (a member of a session whose own address is
// pinned via `shared_ptr`) or via `std::unique_ptr<quic_conn>`.

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_write_result

// Result of `quic_conn::write_pkt`: either the number of bytes written
// (which may be 0 when the connection has nothing to send right now), or
// a `quic_decode_status` indicating why ngtcp2 refused to emit a packet.
struct quic_write_result {
  size_t bytes_written{};
  quic_decode_status status{quic_decode_status::ok};

  [[nodiscard]] bool ok() const noexcept {
    return status == quic_decode_status::ok;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

#pragma endregion
#pragma region quic_close_request

// Which CONNECTION_CLOSE frame variant ngtcp2 should emit.
//   transport   -- CONNECTION_CLOSE of type 0x1c (RFC 9000 sec. 19.19).
//                  `error_code` is from the QUIC transport error space
//                  (`NGTCP2_*` constants, e.g., `NGTCP2_NO_ERROR`).
//   application -- CONNECTION_CLOSE of type 0x1d. `error_code` is defined by
//                  the application protocol (e.g., the `H3_*` constants from
//                  RFC 9114 sec. 8.1).
enum class quic_close_kind : uint8_t {
  transport = 0x1c,
  application = 0x1d,
};

// A pending request to close the connection, carrying the variant, error code,
// and optional UTF-8 reason phrase. Stashed on `quic_conn` by `request_close`
// (typically called from inside a `quic_conn_handlers` upcall) and consumed
// by the session's drain on the same turn, which feeds it into
// `ngtcp2_ccerr_set_*_error` and `ngtcp2_conn_write_connection_close`.
//
// `kind == {}` is the "no close requested" sentinel; non-default `kind` means
// the caller asked to close.
//
// IMPORTANT: the storage backing `reason` must outlive the read_pkt + drain
// cycle.
struct quic_close_request {
  quic_close_kind kind{};
  uint64_t error_code{};
  std::string_view reason;
};

#pragma endregion
#pragma region quic_conn_handlers

// Protocol-neutral upcall contract that `quic_conn`'s static ngtcp2 callback
// trampolines forward into. The upper-layer plugin (echo, DNS, HTTP/3, etc.)
// inherits this; the owning session installs the plugin via
// `quic_conn::set_handlers` after the plugin is constructed.
//
// All upcalls run on the loop thread inside an ngtcp2 callback frame.
// CRITICAL: ngtcp2 forbids `ngtcp2_conn_writev_stream` / `write_pkt` from
// inside any callback. These hooks may only update state and call non-write
// upper-layer APIs (e.g., `nghttp3_conn_read_stream`,
// `nghttp3_conn_add_ack_offset`). Outbound packet emission happens
// in the session's per-turn drain, after `read_pkt` returns.
//
// Per-stream abort *is* allowed from inside a callback via
// `quic_conn::shutdown_stream`: that path calls
// `ngtcp2_conn_shutdown_stream_read` / `_write` immediately (which have
// local-only effects: stopping further read delivery and marking the write
// side closed) and only the eventual RESET_STREAM / STOP_SENDING packet
// emission is deferred to drain. The local effects matter: deferring the
// ngtcp2 call would let already- processed stream data continue to be
// delivered during the current `read_pkt` path.
//
// Each upcall returns `bool`: `true` to continue normally (trampoline
// returns 0 to ngtcp2), `false` to signal callback failure (trampoline
// returns `NGTCP2_ERR_CALLBACK_FAILURE`; ngtcp2 bails from `read_pkt` and
// no further callbacks fire in this turn). To request a graceful
// CONNECTION_CLOSE, call `quic_conn::request_close(kind, error_code,
// reason)` from within the callback to stash the close request, then
// return `false` (or `true`, depending on whether you want ngtcp2 to
// keep dispatching the rest of the packet); the session's drain will
// see the stash and emit the requested CONNECTION_CLOSE after
// `read_pkt` returns.
//
// Defaults are no-op `true` so concrete plugins override only what they
// need.
class quic_conn_handlers {
public:
  quic_conn_handlers() = default;
  quic_conn_handlers(const quic_conn_handlers&) = delete;
  quic_conn_handlers& operator=(const quic_conn_handlers&) = delete;
  virtual ~quic_conn_handlers() = default;

#pragma region Handshake progression

  // TLS handshake finished (both endpoints have completed). On the server this
  // fires once the server has received and processed the client's Finished; on
  // the client, when the client itself has sent Finished. Fires once per conn.
  [[nodiscard]] virtual bool on_handshake_completed() noexcept { return true; }

  // 1-RTT TX key is installed; the endpoint can now send application data.
  // This is the moment HTTP/3 / nghttp3 cares about for submitting requests
  // and responses. Fires before `on_handshake_completed` on the server, after
  // it on the client.
  [[nodiscard]] virtual bool on_app_tx_ready() noexcept { return true; }

  // Handshake confirmed (RFC 9001 sec. 4.1.2). Client-only: fires when
  // HANDSHAKE_DONE arrives, signalling that the server has fully
  // installed 1-RTT and the client may discard Handshake-level state.
  // ngtcp2 silently transitions the server into the confirmed state
  // without a callback, so concrete plugins do not see this on the
  // server side; rely on `on_handshake_completed` there instead.
  [[nodiscard]] virtual bool on_handshake_confirmed() noexcept { return true; }

#pragma endregion
#pragma region Stream lifecycle

  // Peer opened a new stream.
  [[nodiscard]] virtual bool on_stream_open(
      quic_stream_id stream_id) noexcept {
    (void)stream_id;
    return true;
  }

  // Inbound stream payload. `flags` carries `fin` and/or `zero_rtt`. May fire
  // with empty `data` on a pure FIN. Bytes are valid only for the call
  // duration; copy or hand to the upper layer (e.g.,
  // `nghttp3_conn_read_stream`) before returning.
  [[nodiscard]] virtual bool on_recv_stream_data(quic_stream_id stream_id,
      uint64_t offset, std::span<const uint8_t> data,
      quic_stream_data_flags flags) noexcept {
    (void)stream_id;
    (void)offset;
    (void)data;
    (void)flags;
    return true;
  }

  // Peer acknowledged that bytes `[offset, offset+datalen)` on `stream_id`
  // were received. The plugin can release send-side buffers it was retaining
  // for retransmit; HTTP/3 forwards to `nghttp3_conn_add_ack_offset`.
  [[nodiscard]] virtual bool on_acked_stream_data_offset(
      quic_stream_id stream_id, uint64_t offset, uint64_t datalen) noexcept {
    (void)stream_id;
    (void)offset;
    (void)datalen;
    return true;
  }

  // Peer sent RESET_STREAM on its sending side. `final_size` is the
  // total it claims to have sent; ngtcp2 will not deliver any further
  // `on_recv_stream_data` for this stream. `app_error_code` is
  // peer-supplied.
  [[nodiscard]] virtual bool on_stream_reset(quic_stream_id stream_id,
      uint64_t final_size, uint64_t app_error_code) noexcept {
    (void)stream_id;
    (void)final_size;
    (void)app_error_code;
    return true;
  }

  // Peer sent STOP_SENDING: it no longer wants our data on `stream_id`. The
  // plugin should stop submitting bytes for this stream; ngtcp2 will emit
  // RESET_STREAM on the next outbound turn.
  [[nodiscard]] virtual bool on_stream_stop_sending(quic_stream_id stream_id,
      uint64_t app_error_code) noexcept {
    (void)stream_id;
    (void)app_error_code;
    return true;
  }

  // Stream fully terminated. ngtcp2 will not touch retained send data for
  // this stream again, so the plugin may release any unacknowledged stream
  // buffers. `app_error_code` is populated only when the peer supplied one
  // (ngtcp2's `NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET`); empty for
  // clean closes that carried no error code.
  [[nodiscard]] virtual bool on_stream_close(quic_stream_id stream_id,
      std::optional<uint64_t> app_error_code) noexcept {
    (void)stream_id;
    (void)app_error_code;
    return true;
  }

#pragma endregion
#pragma region Flow control feedback

  // Peer raised our send window on `stream_id` to `max_data` bytes total. A
  // plugin previously stalled on this stream may resume; HTTP/3 forwards to
  // `nghttp3_conn_unblock_stream`.
  [[nodiscard]] virtual bool on_extend_max_stream_data(
      quic_stream_id stream_id, uint64_t max_data) noexcept {
    (void)stream_id;
    (void)max_data;
    return true;
  }

  // Peer raised our limit on locally-initiated bidirectional / unidirectional
  // streams to `max_streams` total. The plugin may now open additional streams
  // up to that count.
  [[nodiscard]] virtual bool on_extend_max_local_streams_bidi(
      uint64_t max_streams) noexcept {
    (void)max_streams;
    return true;
  }
  [[nodiscard]] virtual bool on_extend_max_local_streams_uni(
      uint64_t max_streams) noexcept {
    (void)max_streams;
    return true;
  }

#pragma endregion
#pragma region Datagrams (RFC 9221)

  // Inbound DATAGRAM frame.
  [[nodiscard]] virtual bool on_recv_datagram(quic_datagram_flags flags,
      std::span<const uint8_t> data) noexcept {
    (void)flags;
    (void)data;
    return true;
  }

  // Peer acknowledged the packet carrying the DATAGRAM whose `dgram_id` (the
  // value we passed to `writev_datagram`) is given. Datagrams are unreliable;
  // this is a telemetry signal, not a contract: it fires at most once per
  // `dgram_id`.
  [[nodiscard]] virtual bool on_ack_datagram(uint64_t dgram_id) noexcept {
    (void)dgram_id;
    return true;
  }

  // ngtcp2 declared the packet carrying the DATAGRAM with `dgram_id` lost.
  // Datagrams are not retransmitted; this is a telemetry signal for the
  // application to decide whether to resend.
  [[nodiscard]] virtual bool on_lost_datagram(uint64_t dgram_id) noexcept {
    (void)dgram_id;
    return true;
  }

#pragma endregion
};

#pragma endregion
#pragma region quic_conn

class quic_conn {
public:
  using key_t = quic_cid;
  using time_point_t = timeouts::time_point_t;

#pragma region Construction

  // Construct a CLIENT-side connection.
  //
  // On failure, every method becomes a no-op that returns
  // `quic_decode_status::invalid_state`.
  //
  // `initial_dcid` is the random CID the client picks for the Initial
  // DCID (which the server echoes back via the
  // `original_destination_connection_id` transport parameter so the
  // client can verify the response came from the same server it asked).
  // `scid` is the client's own CID, used as the Initial SCID.
  explicit quic_conn(quic_ssl_ctx& client_tls, const key_t& initial_dcid,
      const key_t& scid, const net_endpoint& local, const net_endpoint& peer,
      time_point_t now) noexcept
      : quic_conn{init_tag{}, connection_role::client, client_tls,
            initial_dcid, scid, local, peer, now, key_t{}} {}

  // Construct a SERVER-side connection.
  //
  // On failure, every method becomes a no-op that returns
  // `quic_decode_status::invalid_state`.
  //
  // `peer_scid` is the SCID the client put on the wire (the server uses
  // it as its destination when sending). `scid` is the server's own
  // freshly-generated CID. `original_dcid` is the DCID the client put
  // on the wire; it travels back to the client via the
  // `original_destination_connection_id` transport parameter.
  explicit quic_conn(quic_ssl_ctx& server_tls, const key_t& peer_scid,
      const key_t& scid, const key_t& original_dcid, const net_endpoint& local,
      const net_endpoint& peer, time_point_t now) noexcept
      : quic_conn{init_tag{}, connection_role::server, server_tls, peer_scid,
            scid, local, peer, now, original_dcid} {}

  quic_conn(const quic_conn&) = delete;
  quic_conn& operator=(const quic_conn&) = delete;
  // Non-movable: ngtcp2 captured `this` as `user_data` at construction,
  // and the SSL holds a pointer back into `conn_ref_`.
  quic_conn(quic_conn&&) = delete;
  quic_conn& operator=(quic_conn&&) = delete;

private:
  // Tag for the unified worker constructor below. Disambiguates it from
  // the two public role-specific constructors that delegate to it.
  struct init_tag {};

  // Unified worker. `expected_role` must agree with `tls.role()` or the
  // conn is left in the null state.
  quic_conn(init_tag, connection_role expected_role, quic_ssl_ctx& tls,
      const key_t& dcid, const key_t& scid, const net_endpoint& local,
      const net_endpoint& peer, time_point_t now,
      const key_t& original_dcid) noexcept
      : role_{expected_role} {
    ensure_crypto_init();
    if (tls.role() != expected_role) return;
    if (!tls) return;

    // 1. Per-conn SSL object, with `conn_ref_` set as app_data so the
    //    crypto shim can recover our `ngtcp2_conn*` during handshake.
    ssl_ptr ssl{SSL_new(tls.native())};
    if (!ssl) return;
    conn_ref_.get_conn = &get_conn_static;
    conn_ref_.user_data = this;
    SSL_set_app_data(ssl.get(), &conn_ref_);

    // 2. Install the QUIC TLS callbacks on the SSL per role, then put the
    //    SSL into accept/connect state. The shim's `configure_*_session`
    //    only wires the callback table; OpenSSL still needs to know which
    //    direction this SSL drives, or `SSL_do_handshake` fails with
    //    "connection type not set".
    if (role_ == connection_role::server) {
      if (ngtcp2_crypto_ossl_configure_server_session(ssl.get()) != 0) return;
      SSL_set_accept_state(ssl.get());
    } else {
      if (ngtcp2_crypto_ossl_configure_client_session(ssl.get()) != 0) return;
      SSL_set_connect_state(ssl.get());
    }

    // 3. Wrap the SSL in the shim's per-conn context.
    ngtcp2_crypto_ossl_ctx* raw_oc = nullptr;
    if (ngtcp2_crypto_ossl_ctx_new(&raw_oc, ssl.get()) != 0) return;
    ossl_ctx_ptr ossl_ctx{raw_oc};

    // 4. Build ngtcp2 settings + transport params + path.
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = timeouts::as_nanoseconds(now);

    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    if (role_ == connection_role::server) {
      params.original_dcid = original_dcid.value();
      params.original_dcid_present = 1;
    }

    auto path = make_ngtcp2_path(local, peer);
    ngtcp2_conn* raw_conn = nullptr;
    const auto& cbs =
        (role_ == connection_role::server)
            ? server_callbacks
            : client_callbacks;
    const int rv =
        (role_ == connection_role::server)
            ? ngtcp2_conn_server_new(&raw_conn, dcid.pointer(), scid.pointer(),
                  &path, NGTCP2_PROTO_VER_V1, &cbs, &settings, &params,
                  nullptr, this)
            : ngtcp2_conn_client_new(&raw_conn, dcid.pointer(), scid.pointer(),
                  &path, NGTCP2_PROTO_VER_V1, &cbs, &settings, &params,
                  nullptr, this);
    if (rv != 0) return;
    conn_ptr conn{raw_conn};

    // 5. Bind shim ctx to the ngtcp2 conn. ngtcp2 does not take ownership;
    //    the ctx and SSL must outlive the conn (handled by member order).
    ngtcp2_conn_set_tls_native_handle(conn.get(), ossl_ctx.get());

    // All steps succeeded; commit.
    ssl_ = std::move(ssl);
    ossl_ctx_ = std::move(ossl_ctx);
    conn_ = std::move(conn);
  }

public:
#pragma endregion
#pragma region Accessors

  [[nodiscard]] explicit operator bool() const noexcept { return !!conn_; }
  [[nodiscard]] connection_role role() const noexcept { return role_; }
  [[nodiscard]] auto native(this auto& self) { return self.conn_.get(); }

  [[nodiscard]] bool is_handshake_completed() const noexcept {
    if (!conn_) return false;
    return ngtcp2_conn_get_handshake_completed(conn_.get()) != 0;
  }

  // Build an `ngtcp2_path` whose `addr` fields point inside the given
  // `net_endpoint`s. The endpoints must outlive any ngtcp2 call that uses
  // the returned path. ngtcp2 takes `sockaddr*` (non-const) even though it
  // only reads through these pointers; the `const_cast` is safe.
  [[nodiscard]] static ngtcp2_path make_ngtcp2_path(const net_endpoint& local,
      const net_endpoint& peer) noexcept {
    ngtcp2_path path{};
    path.local.addr = const_cast<sockaddr*>(local.as_sockaddr_ptr());
    path.local.addrlen = local.sockaddr_size();
    path.remote.addr = const_cast<sockaddr*>(peer.as_sockaddr_ptr());
    path.remote.addrlen = peer.sockaddr_size();
    return path;
  }

#pragma endregion
#pragma region Plugin wiring

  // Install the upcall handlers that `quic_conn`'s ngtcp2 trampolines
  // dispatch into. Pass `nullptr` to detach. The session calls this once,
  // after the upper plugin is constructed. The pointee must outlive
  // `quic_conn` (or be detached first); typically it is a base subobject
  // of the upper plugin owned next to `quic_conn` in the session.
  void set_handlers(quic_conn_handlers* handlers) noexcept {
    handlers_ = handlers;
  }

  // Queue a graceful CONNECTION_CLOSE for the session's per-turn drain to
  // emit after `read_pkt` returns. Typically called from inside a
  // `quic_conn_handlers` upcall (which then returns the bool of its
  // choosing to ngtcp2). Requesting close is terminal: this is a
  // one-shot decision, not something the conn keeps polling.
  //
  // The defaults give a no-fault clean close (transport NO_ERROR, no
  // reason phrase). `kind` selects the CONNECTION_CLOSE frame variant
  // the drain emits (RFC 9000 sec. 19.19); `error_code` is interpreted
  // in the matching namespace (transport vs application). The storage
  // backing `reason` must outlive the read_pkt + drain cycle.
  void request_close(quic_close_kind kind = quic_close_kind::transport,
      uint64_t error_code = 0, std::string_view reason = {}) noexcept {
    pending_close_ = {kind, error_code, reason};
  }

#pragma endregion
#pragma region IO

  // Feed a received datagram into the conn for decryption + decoding.
  [[nodiscard]] quic_decode_status read_pkt(const ngtcp2_path& path,
      std::span<const uint8_t> pkt,
      time_point_t now = timeouts::now()) noexcept {
    if (!conn_) return quic_decode_status::invalid_state;
    const int rv = ngtcp2_conn_read_pkt(conn_.get(), &path, nullptr,
        pkt.data(), pkt.size(), timeouts::as_nanoseconds(now));
    return static_cast<quic_decode_status>(rv);
  }

  // Drive a single outgoing packet. `dest` is the buffer ngtcp2 will write
  // the encrypted bytes into; on success `bytes_written` is the number of
  // bytes written (zero means "nothing to send right now"). `path_out`
  // is filled with the path the packet should be sent on.
  [[nodiscard]] quic_write_result write_pkt(ngtcp2_path& path_out,
      std::span<uint8_t> dest, time_point_t now = timeouts::now()) noexcept {
    if (!conn_) return {0, quic_decode_status::invalid_state};
    const ngtcp2_ssize rv = ngtcp2_conn_write_pkt(conn_.get(), &path_out,
        nullptr, dest.data(), dest.size(), timeouts::as_nanoseconds(now));
    if (rv < 0) return {0, static_cast<quic_decode_status>(rv)};
    return {static_cast<size_t>(rv), quic_decode_status::ok};
  }

#pragma endregion
#pragma region Expiry

  // The next deadline at which `handle_expiry` should be called. If ngtcp2
  // currently has no pending timer, returns `time_point_t::max()`. Caller
  // arms an external timer at this point and invokes `handle_expiry` when
  // it fires.
  [[nodiscard]] time_point_t expiry() const noexcept {
    if (!conn_) return time_point_t::max();

    return timeouts::from_nanoseconds(ngtcp2_conn_get_expiry(conn_.get()));
  }

  [[nodiscard]] quic_decode_status handle_expiry(
      time_point_t now = timeouts::now()) noexcept {
    if (!conn_) return quic_decode_status::invalid_state;
    return static_cast<quic_decode_status>(
        ngtcp2_conn_handle_expiry(conn_.get(), timeouts::as_nanoseconds(now)));
  }

#pragma endregion
#pragma region Handlers
private:
  // -- App-supplied callbacks. The crypto-shim functions handle the AEAD /
  // HP / key / crypto-data callbacks; we only own the ones the shim does
  // not provide. Trampolines that surface a `quic_conn_handlers` upcall
  // recover the typed `quic_conn*` from `user_data`, no-op when no
  // handlers are attached, and otherwise translate the handler's bool
  // return into `0` / `NGTCP2_ERR_CALLBACK_FAILURE`. A handler that wants
  // a graceful CONNECTION_CLOSE instead of a callback failure calls
  // `request_close` first to request the close, then returns whichever bool
  // fits its needs.
  static void on_rand(uint8_t* dest, size_t destlen,
      const ngtcp2_rand_ctx* /*ctx*/) noexcept {
    // `RAND_bytes` cannot reasonably fail in normal operation; on error,
    // the worst case is that the connection rejects packets. Returning
    // zeroed buffers would be worse (deterministic, predictable).
    RAND_bytes(dest, static_cast<int>(destlen));
  }

  // CIDs must be unpredictable per RFC 9000 sec. 5.1, so fill with
  // cryptographic randomness. The stateless-reset token is reused as the
  // "secret known to the issuer" in the reset path; same constraint
  // applies.
  static int on_get_new_connection_id2(ngtcp2_conn* /*conn*/, ngtcp2_cid* cid,
      ngtcp2_stateless_reset_token* token, size_t cidlen,
      void* /*user_data*/) noexcept {
    cid->datalen = cidlen;
    return success(
        RAND_bytes(cid->data, static_cast<int>(cidlen)) == 1 &&
        RAND_bytes(token->data, sizeof(token->data)) == 1);
  }

  // CID retirement is not yet plumbed back to the router; the
  // CID-rotation milestone will replace this no-op with a session-level
  // `router.remove_session` call.
  static int
  on_remove_connection_id(ngtcp2_conn*, const ngtcp2_cid*, void*) noexcept {
    return success(true);
  }

  // `recv_rx_key` is unused: nothing in the handler contract reacts to RX
  // key installation. The TX direction is handled separately by
  // `on_recv_tx_key` (it filters on 1-RTT and surfaces `on_app_tx_ready`).
  static int
  on_recv_rx_key(ngtcp2_conn*, ngtcp2_encryption_level, void*) noexcept {
    return success(true);
  }

  // v2 trampoline around the shim's v1 `get_path_challenge_data` (which
  // writes raw bytes). The ngtcp2 callback table at version V3 uses the
  // v2 signature, which takes a typed struct that wraps the same byte
  // array.
  static int on_get_path_challenge_data2(ngtcp2_conn* conn,
      ngtcp2_path_challenge_data* data, void* user_data) noexcept {
    return ngtcp2_crypto_get_path_challenge_data_cb(conn, data->data,
        user_data);
  }

  // -- Trampolines that forward `quic_conn_handlers` upcalls.

  // Translate a handler's `bool` return into the int ngtcp2 callbacks
  // expect: `0` on success, `NGTCP2_ERR_CALLBACK_FAILURE` on failure.
  [[nodiscard]] static constexpr int success(bool ok) noexcept {
    return ok ? 0 : NGTCP2_ERR_CALLBACK_FAILURE;
  }

  static int on_handshake_completed(ngtcp2_conn*, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_handshake_completed());
  }

  static int on_handshake_confirmed(ngtcp2_conn*, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_handshake_confirmed());
  }

  // `recv_tx_key` fires once per TX-key install during the handshake;
  // ngtcp2 omits the INITIAL level, so the levels we observe are
  // HANDSHAKE and 1-RTT. Application data only becomes sendable at
  // 1-RTT, so we filter and only surface `on_app_tx_ready` then. The
  // intermediate HANDSHAKE call is silently ignored.
  static int on_recv_tx_key(ngtcp2_conn*, ngtcp2_encryption_level level,
      void* user_data) noexcept {
    if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) return 0;
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_app_tx_ready());
  }

  static int
  on_stream_open(ngtcp2_conn*, int64_t stream_id, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_stream_open(
        static_cast<quic_stream_id>(stream_id)));
  }

  static int on_recv_stream_data(ngtcp2_conn*, uint32_t flags,
      int64_t stream_id, uint64_t offset, const uint8_t* data, size_t datalen,
      void* user_data, void* /*stream_user_data*/) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_recv_stream_data(
        static_cast<quic_stream_id>(stream_id), offset,
        std::span<const uint8_t>{data, datalen},
        static_cast<quic_stream_data_flags>(flags)));
  }

  static int on_acked_stream_data_offset(ngtcp2_conn*, int64_t stream_id,
      uint64_t offset, uint64_t datalen, void* user_data,
      void* /*stream_user_data*/) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_acked_stream_data_offset(
        static_cast<quic_stream_id>(stream_id), offset, datalen));
  }

  static int on_stream_reset(ngtcp2_conn*, int64_t stream_id,
      uint64_t final_size, uint64_t app_error_code, void* user_data,
      void* /*stream_user_data*/) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_stream_reset(
        static_cast<quic_stream_id>(stream_id), final_size, app_error_code));
  }

  static int on_stream_stop_sending(ngtcp2_conn*, int64_t stream_id,
      uint64_t app_error_code, void* user_data,
      void* /*stream_user_data*/) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_stream_stop_sending(
        static_cast<quic_stream_id>(stream_id), app_error_code));
  }

  static int on_stream_close(ngtcp2_conn*, uint32_t flags, int64_t stream_id,
      uint64_t app_error_code, void* user_data,
      void* /*stream_user_data*/) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    std::optional<uint64_t> ec;
    if (flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)
      ec = app_error_code;
    return success(self->handlers_->on_stream_close(
        static_cast<quic_stream_id>(stream_id), ec));
  }

  static int on_extend_max_stream_data(ngtcp2_conn*, int64_t stream_id,
      uint64_t max_data, void* user_data,
      void* /*stream_user_data*/) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_extend_max_stream_data(
        static_cast<quic_stream_id>(stream_id), max_data));
  }

  //!!! If we had an enum for uni/bidi then we could combine these
  // into a single callback. Having said that, I don't think we have any
  // such enum and I'm not sure if it's worth the trouble.
  static int on_extend_max_local_streams_bidi(ngtcp2_conn*,
      uint64_t max_streams, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(
        self->handlers_->on_extend_max_local_streams_bidi(max_streams));
  }

  static int on_extend_max_local_streams_uni(ngtcp2_conn*,
      uint64_t max_streams, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(
        self->handlers_->on_extend_max_local_streams_uni(max_streams));
  }

  static int on_recv_datagram(ngtcp2_conn*, uint32_t flags,
      const uint8_t* data, size_t datalen, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    //!!! on_recv_datagram should move flags to the last parameter.
    return success(self->handlers_->on_recv_datagram(
        static_cast<quic_datagram_flags>(flags),
        std::span<const uint8_t>{data, datalen}));
  }

  static int
  on_ack_datagram(ngtcp2_conn*, uint64_t dgram_id, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_ack_datagram(dgram_id));
  }

  static int
  on_lost_datagram(ngtcp2_conn*, uint64_t dgram_id, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return success(self->handlers_->on_lost_datagram(dgram_id));
  }

#pragma endregion
#pragma region Helpers

  // Callback tables, one per role. Unmentioned slots are
  // value-initialized to null, which is what ngtcp2 expects for optional
  // callbacks.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
  static constexpr ngtcp2_callbacks server_callbacks{
      .recv_client_initial = &ngtcp2_crypto_recv_client_initial_cb,
      .recv_crypto_data = &ngtcp2_crypto_recv_crypto_data_cb,
      .handshake_completed = &on_handshake_completed,
      .encrypt = &ngtcp2_crypto_encrypt_cb,
      .decrypt = &ngtcp2_crypto_decrypt_cb,
      .hp_mask = &ngtcp2_crypto_hp_mask_cb,
      .recv_stream_data = &on_recv_stream_data,
      .acked_stream_data_offset = &on_acked_stream_data_offset,
      .stream_open = &on_stream_open,
      .stream_close = &on_stream_close,
      .extend_max_local_streams_bidi = &on_extend_max_local_streams_bidi,
      .extend_max_local_streams_uni = &on_extend_max_local_streams_uni,
      .rand = &on_rand,
      .remove_connection_id = &on_remove_connection_id,
      .update_key = &ngtcp2_crypto_update_key_cb,
      .stream_reset = &on_stream_reset,
      .extend_max_stream_data = &on_extend_max_stream_data,
      .handshake_confirmed = &on_handshake_confirmed,
      .delete_crypto_aead_ctx = &ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      .delete_crypto_cipher_ctx = &ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      .recv_datagram = &on_recv_datagram,
      .ack_datagram = &on_ack_datagram,
      .lost_datagram = &on_lost_datagram,
      .stream_stop_sending = &on_stream_stop_sending,
      .version_negotiation = &ngtcp2_crypto_version_negotiation_cb,
      .recv_rx_key = &on_recv_rx_key,
      .recv_tx_key = &on_recv_tx_key,
      .get_new_connection_id2 = &on_get_new_connection_id2,
      .get_path_challenge_data2 = &on_get_path_challenge_data2,
  };

  static constexpr ngtcp2_callbacks client_callbacks{
      .client_initial = &ngtcp2_crypto_client_initial_cb,
      .recv_crypto_data = &ngtcp2_crypto_recv_crypto_data_cb,
      .handshake_completed = &on_handshake_completed,
      .encrypt = &ngtcp2_crypto_encrypt_cb,
      .decrypt = &ngtcp2_crypto_decrypt_cb,
      .hp_mask = &ngtcp2_crypto_hp_mask_cb,
      .recv_stream_data = &on_recv_stream_data,
      .acked_stream_data_offset = &on_acked_stream_data_offset,
      .stream_open = &on_stream_open,
      .stream_close = &on_stream_close,
      .recv_retry = &ngtcp2_crypto_recv_retry_cb,
      .extend_max_local_streams_bidi = &on_extend_max_local_streams_bidi,
      .extend_max_local_streams_uni = &on_extend_max_local_streams_uni,
      .rand = &on_rand,
      .remove_connection_id = &on_remove_connection_id,
      .update_key = &ngtcp2_crypto_update_key_cb,
      .stream_reset = &on_stream_reset,
      .extend_max_stream_data = &on_extend_max_stream_data,
      .handshake_confirmed = &on_handshake_confirmed,
      .delete_crypto_aead_ctx = &ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      .delete_crypto_cipher_ctx = &ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      .recv_datagram = &on_recv_datagram,
      .ack_datagram = &on_ack_datagram,
      .lost_datagram = &on_lost_datagram,
      .stream_stop_sending = &on_stream_stop_sending,
      .version_negotiation = &ngtcp2_crypto_version_negotiation_cb,
      .recv_rx_key = &on_recv_rx_key,
      .recv_tx_key = &on_recv_tx_key,
      .get_new_connection_id2 = &on_get_new_connection_id2,
      .get_path_challenge_data2 = &on_get_path_challenge_data2,
  };
#pragma clang diagnostic pop

  // The shim retrieves our `ngtcp2_conn*` via the conn_ref's `get_conn`
  // callback, which lets it find us starting from just an SSL pointer.
  static ngtcp2_conn* get_conn_static(ngtcp2_crypto_conn_ref* ref) noexcept {
    return static_cast<quic_conn*>(ref->user_data)->conn_.get();
  }

  // One-time process-wide initialization for the ngtcp2 OpenSSL backend.
  // Documented as optional but strongly recommended for performance.
  static void ensure_crypto_init() noexcept {
    [[maybe_unused]] static const int once = [] {
      return ngtcp2_crypto_ossl_init();
    }();
  }

  // Sever the SSL's back-pointer to `conn_ref_` before `SSL_free`, per
  // the shim's contract (we cannot guarantee that the ngtcp2_conn
  // outlives `SSL_free`, because `conn_` is destroyed first by member
  // order).
  using ssl_ptr = std::unique_ptr<SSL, decltype([](SSL* p) noexcept {
    if (!p) return;
    SSL_set_app_data(p, nullptr);
    SSL_free(p);
  })>;
  using ossl_ctx_ptr = std::unique_ptr<ngtcp2_crypto_ossl_ctx,
      decltype([](ngtcp2_crypto_ossl_ctx* p) noexcept {
        if (p) ngtcp2_crypto_ossl_ctx_del(p);
      })>;
  using conn_ptr =
      std::unique_ptr<ngtcp2_conn, decltype([](ngtcp2_conn* p) noexcept {
        if (p) ngtcp2_conn_del(p);
      })>;

#pragma endregion
#pragma region Data members

  // Declaration order matters: members are destroyed in reverse order, so
  // declaring `conn_` last makes it the first to go. `conn_` references
  // `ossl_ctx_` (via `ngtcp2_conn_set_tls_native_handle`), which in turn
  // wraps `ssl_`. Tearing down inside-out keeps every dependency live
  // while it is being used. `conn_ref_` is declared first so it outlives
  // the SSL that points at it.
  ngtcp2_crypto_conn_ref conn_ref_{};
  connection_role role_;
  ssl_ptr ssl_;
  ossl_ctx_ptr ossl_ctx_;
  conn_ptr conn_;

  // Upper-plugin upcalls, installed via `set_handlers` after construction.
  // Null until then; trampolines no-op on null.
  quic_conn_handlers* handlers_{};

  // Close request stashed by `request_close` (typically called from
  // inside a handler upcall); not externally observable. `kind ==
  // quic_close_kind{}` means "not set". Last write in a turn wins.
  quic_close_request pending_close_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::quic::quic_close_kind> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::quic::quic_close_kind, "transport, application",
        corvid::enums::wrapclip{},
        corvid::proto::quic::quic_close_kind::transport>();
