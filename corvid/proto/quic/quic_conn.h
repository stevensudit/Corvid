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
#include <span>
#include <utility>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "../net_endpoint.h"
#include "../../enums/bool_enums.h"
#include "../../concurrency/timeout_sweeper_base.h"

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
#pragma region quic_conn

class quic_conn {
public:
  using key_t = quic_cid;
  using time_point_t = timeout_sweeper_base::time_point_t;

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
    settings.initial_ts = timeout_sweeper_base::as_nanoseconds(now);

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
        pkt.data(), pkt.size(), timeout_sweeper_base::as_nanoseconds(now));
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
        nullptr, dest.data(), dest.size(),
        timeout_sweeper_base::as_nanoseconds(now));
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
    return timeout_sweeper_base::from_nanoseconds(
        ngtcp2_conn_get_expiry(conn_.get()));
  }

  [[nodiscard]] quic_decode_status handle_expiry(time_point_t now) noexcept {
    if (!conn_) return quic_decode_status::invalid_state;
    return static_cast<quic_decode_status>(ngtcp2_conn_handle_expiry(
        conn_.get(), timeout_sweeper_base::as_nanoseconds(now)));
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
