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
// and optional UTF-8 reason phrase. Built by `quic_cb_result::close_transport`
// / `quic_cb_result::close_application`; consumed by the session's drain on
// the next turn, which feeds it into `ngtcp2_ccerr_set_*_error` and
// `ngtcp2_conn_write_connection_close`.
//
// IMPORTANT: the storage backing `reason` must outlive the read_pkt + drain
// cycle .
struct quic_close_request {
  quic_close_kind kind{};
  uint64_t error_code{};
  std::string_view reason;
};

#pragma endregion
#pragma region quic_cb_result

// The return value from every `quic_conn_handlers` upcall. Carries what the
// plugin wants the trampoline to do next:
//   ok               -- continue normally; trampoline returns 0 to ngtcp2.
//   callback_failure -- internal error; trampoline returns
//                       NGTCP2_ERR_CALLBACK_FAILURE. ngtcp2 will terminate the
//                       conn with INTERNAL_ERROR CONNECTION_CLOSE on the wire
//                       and bail from `read_pkt`; no further callbacks fire in
//                       this turn.
//   close_connection -- graceful app-driven close; trampoline returns 0 and
//                       stashes the carried `quic_close_request` on
//                       `quic_conn`. The session's drain reads the stash and
//                       emits CONNECTION_CLOSE with the requested kind, error
//                       code, and reason.
//
// First request wins: if more than one callback in a single read_pkt turn
// returns `close_connection`, only the first request is honored; later ones
// are dropped. Rationale: once a close has been decided, downstream callbacks
// shouldn't be able to override the chosen error code.
//
// For per-stream abort, do not use this result type: call
// `quic_conn::shutdown_stream` directly from within the callback (it is safe
// to call from inside an ngtcp2 callback) and then return `ok`.
class quic_cb_result {
public:
  enum class kind : uint8_t {
    ok,
    callback_failure,
    close_connection,
  };

  [[nodiscard]] static constexpr quic_cb_result ok() noexcept {
    return {kind::ok, {}};
  }

  [[nodiscard]] static constexpr quic_cb_result callback_failure() noexcept {
    return {kind::callback_failure, {}};
  }

  [[nodiscard]] static constexpr quic_cb_result
  close_transport(uint64_t error_code, std::string_view reason = {}) noexcept {
    return {kind::close_connection,
        quic_close_request{quic_close_kind::transport, error_code, reason}};
  }

  [[nodiscard]] static constexpr quic_cb_result close_application(
      uint64_t error_code, std::string_view reason = {}) noexcept {
    return {kind::close_connection,
        quic_close_request{quic_close_kind::application, error_code, reason}};
  }

  [[nodiscard]] constexpr kind type() const noexcept { return kind_; }

  [[nodiscard]] constexpr const quic_close_request& close() const noexcept {
    return close_;
  }

private:
  constexpr quic_cb_result(kind k, quic_close_request c) noexcept
      : kind_{k}, close_{c} {}

  kind kind_;
  quic_close_request close_{quic_close_kind::application, 0, {}};
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
// Defaults are no-op `ok` so concrete plugins override only what they need.
// Returning `quic_cb_result::callback_failure()` aborts the connection;
// returning `close_transport` / `close_application` queues a graceful close
// (first request in a turn wins).
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
  [[nodiscard]] virtual quic_cb_result on_handshake_completed() noexcept {
    return quic_cb_result::ok();
  }

  // 1-RTT TX key is installed; the endpoint can now send application data.
  // This is the moment HTTP/3 / nghttp3 cares about for submitting requests
  // and responses. Fires before `on_handshake_completed` on the server, after
  // it on the client.
  [[nodiscard]] virtual quic_cb_result on_app_tx_ready() noexcept {
    return quic_cb_result::ok();
  }

  // Handshake confirmed (RFC 9001 sec. 4.1.2). On the client this fires upon
  // receipt of HANDSHAKE_DONE; on the server, when the client's
  // Handshake-level ACK arrives. After this, both sides are guaranteed to use
  // 1-RTT keys exclusively.
  [[nodiscard]] virtual quic_cb_result on_handshake_confirmed() noexcept {
    return quic_cb_result::ok();
  }

#pragma endregion
#pragma region Stream lifecycle

  // Peer opened a new stream.
  [[nodiscard]] virtual quic_cb_result on_stream_open(
      quic_stream_id stream_id) noexcept {
    (void)stream_id;
    return quic_cb_result::ok();
  }

  // Inbound stream payload. `flags` carries `fin` and/or `zero_rtt`. May fire
  // with empty `data` on a pure FIN. Bytes are valid only for the call
  // duration; copy or hand to the upper layer (e.g.,
  // `nghttp3_conn_read_stream`) before returning.
  [[nodiscard]] virtual quic_cb_result
  on_recv_stream_data(quic_stream_id stream_id, uint64_t offset,
      std::span<const uint8_t> data, quic_stream_data_flags flags) noexcept {
    (void)stream_id;
    (void)offset;
    (void)data;
    (void)flags;
    return quic_cb_result::ok();
  }

  // Peer acknowledged that bytes `[offset, offset+datalen)` on `stream_id`
  // were received. The plugin can release send-side buffers it was retaining
  // for retransmit; HTTP/3 forwards to `nghttp3_conn_add_ack_offset`.
  [[nodiscard]] virtual quic_cb_result on_acked_stream_data_offset(
      quic_stream_id stream_id, uint64_t offset, uint64_t datalen) noexcept {
    (void)stream_id;
    (void)offset;
    (void)datalen;
    return quic_cb_result::ok();
  }

  // Peer sent RESET_STREAM on its sending side. `final_size` is the
  // total it claims to have sent; ngtcp2 will not deliver any further
  // `on_recv_stream_data` for this stream. `app_error_code` is
  // peer-supplied.
  [[nodiscard]] virtual quic_cb_result
  on_stream_reset(quic_stream_id stream_id, uint64_t final_size,
      uint64_t app_error_code) noexcept {
    (void)stream_id;
    (void)final_size;
    (void)app_error_code;
    return quic_cb_result::ok();
  }

  // Peer sent STOP_SENDING: it no longer wants our data on `stream_id`. The
  // plugin should stop submitting bytes for this stream; ngtcp2 will emit
  // RESET_STREAM on the next outbound turn.
  [[nodiscard]] virtual quic_cb_result on_stream_stop_sending(
      quic_stream_id stream_id, uint64_t app_error_code) noexcept {
    (void)stream_id;
    (void)app_error_code;
    return quic_cb_result::ok();
  }

  // Stream fully terminated. ngtcp2 will not touch retained send data for
  // this stream again, so the plugin may release any unacknowledged stream
  // buffers. `app_error_code` is populated only when the peer supplied one
  // (ngtcp2's `NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET`); empty for
  // clean closes that carried no error code.
  [[nodiscard]] virtual quic_cb_result on_stream_close(
      quic_stream_id stream_id,
      std::optional<uint64_t> app_error_code) noexcept {
    (void)stream_id;
    (void)app_error_code;
    return quic_cb_result::ok();
  }

#pragma endregion
#pragma region Flow control feedback

  // Peer raised our send window on `stream_id` to `max_data` bytes total. A
  // plugin previously stalled on this stream may resume; HTTP/3 forwards to
  // `nghttp3_conn_unblock_stream`.
  [[nodiscard]] virtual quic_cb_result on_extend_max_stream_data(
      quic_stream_id stream_id, uint64_t max_data) noexcept {
    (void)stream_id;
    (void)max_data;
    return quic_cb_result::ok();
  }

  // Peer raised our limit on locally-initiated bidirectional / unidirectional
  // streams to `max_streams` total. The plugin may now open additional streams
  // up to that count.
  [[nodiscard]] virtual quic_cb_result on_extend_max_local_streams_bidi(
      uint64_t max_streams) noexcept {
    (void)max_streams;
    return quic_cb_result::ok();
  }
  [[nodiscard]] virtual quic_cb_result on_extend_max_local_streams_uni(
      uint64_t max_streams) noexcept {
    (void)max_streams;
    return quic_cb_result::ok();
  }

#pragma endregion
#pragma region Datagrams (RFC 9221)

  // Inbound DATAGRAM frame.
  [[nodiscard]] virtual quic_cb_result on_recv_datagram(
      quic_datagram_flags flags, std::span<const uint8_t> data) noexcept {
    (void)flags;
    (void)data;
    return quic_cb_result::ok();
  }

  // Peer acknowledged the packet carrying the DATAGRAM whose `dgram_id` (the
  // value we passed to `writev_datagram`) is given. Datagrams are unreliable;
  // this is a telemetry signal, not a contract: it fires at most once per
  // `dgram_id`.
  [[nodiscard]] virtual quic_cb_result on_ack_datagram(
      uint64_t dgram_id) noexcept {
    (void)dgram_id;
    return quic_cb_result::ok();
  }

  // ngtcp2 declared the packet carrying the DATAGRAM with `dgram_id` lost.
  // Datagrams are not retransmitted; this is a telemetry signal for the
  // application to decide whether to resend.
  [[nodiscard]] virtual quic_cb_result on_lost_datagram(
      uint64_t dgram_id) noexcept {
    (void)dgram_id;
    return quic_cb_result::ok();
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
#pragma region IO

  // Feed a received datagram into the conn for decryption + decoding.
  [[nodiscard]] quic_decode_status read_pkt(const ngtcp2_path& path,
      std::span<const uint8_t> pkt, time_point_t now) noexcept {
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
      std::span<uint8_t> dest, time_point_t now) noexcept {
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

  [[nodiscard]] quic_decode_status handle_expiry(time_point_t now) noexcept {
    if (!conn_) return quic_decode_status::invalid_state;
    return static_cast<quic_decode_status>(
        ngtcp2_conn_handle_expiry(conn_.get(), timeouts::as_nanoseconds(now)));
  }

#pragma endregion
#pragma region Handlers
private:
  // -- App-supplied callbacks. The crypto-shim functions handle the AEAD /
  // HP / key / crypto-data callbacks; we only own the ones the shim does
  // not provide.

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
    if (RAND_bytes(cid->data, static_cast<int>(cidlen)) != 1)
      return NGTCP2_ERR_CALLBACK_FAILURE;
    if (RAND_bytes(token->data, sizeof(token->data)) != 1)
      return NGTCP2_ERR_CALLBACK_FAILURE;
    return 0;
  }

  // No-op handlers for optional callbacks; sessions will override these
  // when they need to react (e.g., for router CID registration on
  // `remove_connection_id`, or for stream-open handling on
  // `handshake_completed`).
  static int on_handshake_completed(ngtcp2_conn*, void*) noexcept { return 0; }
  static int
  on_remove_connection_id(ngtcp2_conn*, const ngtcp2_cid*, void*) noexcept {
    return 0;
  }
  static int
  on_recv_key(ngtcp2_conn*, ngtcp2_encryption_level, void*) noexcept {
    return 0;
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
      .rand = &on_rand,
      .remove_connection_id = &on_remove_connection_id,
      .update_key = &ngtcp2_crypto_update_key_cb,
      .delete_crypto_aead_ctx = &ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      .delete_crypto_cipher_ctx = &ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      .version_negotiation = &ngtcp2_crypto_version_negotiation_cb,
      .recv_rx_key = &on_recv_key,
      .recv_tx_key = &on_recv_key,
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
      .recv_retry = &ngtcp2_crypto_recv_retry_cb,
      .rand = &on_rand,
      .remove_connection_id = &on_remove_connection_id,
      .update_key = &ngtcp2_crypto_update_key_cb,
      .delete_crypto_aead_ctx = &ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      .delete_crypto_cipher_ctx = &ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      .version_negotiation = &ngtcp2_crypto_version_negotiation_cb,
      .recv_rx_key = &on_recv_key,
      .recv_tx_key = &on_recv_key,
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

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::quic::quic_cb_result::kind> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::quic::quic_cb_result::kind,
        "ok, callback_failure, close_connection">();
