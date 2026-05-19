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

#include "../net_endpoint.h"
#include "../../enums/bool_enums.h"
#include "../../concurrency/timeout_sweeper_base.h"

#include "quic_header.h"

// C++ wrapper over ngtcp2's per-connection API. Owns a single `ngtcp2_conn`
// via RAII, exposes `read_pkt` / `write_pkt` / expiry plumbing, and installs a
// fixed callback table of static trampolines that recover the typed
// `quic_conn*` from `user_data`. The trampolines are stubs in this slice: they
// satisfy ngtcp2's "must be specified" assertions over the AEAD / HP / rand /
// key callbacks but do not perform real crypto. Replacing them with
// `ngtcp2_crypto_ossl` shims (real TLS) is the next milestone; a
// `quic_conn` built here can be constructed, queried, and destroyed
// cleanly, but a real handshake will not complete.
//
// `quic_conn` is intentionally neither copyable nor movable: ngtcp2 stores
// our `this` pointer as `user_data` at construction time and never offers
// a setter, so the wrapper's address must stay fixed for the lifetime of
// the underlying `ngtcp2_conn`. Hold one by value at a stable location
// (e.g., as a member of a session whose address itself is stable through
// `shared_ptr`), or via `std::unique_ptr<quic_conn>`.

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

  // Construct a connection. On success, `operator bool()` returns true;
  // on allocation failure or invalid arguments the conn pointer stays
  // null, and every method becomes a no-op that returns
  // `quic_decode_status::invalid_state` (or the equivalent on its return
  // type).
  //
  // For a server, `dcid` is the DCID the client sent in its Initial packet
  // (used as the original DCID in transport parameters); `scid` is the CID
  // we choose for ourselves and will register with the router. For a
  // client, `dcid` is the CID we pick for the server to use as its SCID and
  // `scid` is our own SCID.
  quic_conn(connection_role r, const key_t& dcid, const key_t& scid,
      const net_endpoint& local, const net_endpoint& peer,
      time_point_t now) noexcept
      : role_{r} {
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = timeout_sweeper_base::as_nanoseconds(now);

    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    if (r == connection_role::server) {
      params.original_dcid = dcid.value();
      params.original_dcid_present = 1;
    }

    auto path = make_ngtcp2_path(local, peer);
    ngtcp2_conn* raw = nullptr;
    const auto& cbs =
        (r == connection_role::server)
            ? server_callbacks()
            : client_callbacks();
    const int rv =
        (r == connection_role::server)
            ? ngtcp2_conn_server_new(&raw, dcid.pointer(), scid.pointer(),
                  &path, NGTCP2_PROTO_VER_V1, &cbs, &settings, &params,
                  nullptr, this)
            : ngtcp2_conn_client_new(&raw, dcid.pointer(), scid.pointer(),
                  &path, NGTCP2_PROTO_VER_V1, &cbs, &settings, &params,
                  nullptr, this);
    if (rv != 0) return;
    conn_.reset(raw);
  }

  quic_conn(const quic_conn&) = delete;
  quic_conn& operator=(const quic_conn&) = delete;
  // Non-movable: ngtcp2 captured `this` as `user_data` at construction.
  quic_conn(quic_conn&&) = delete;
  quic_conn& operator=(quic_conn&&) = delete;

#pragma endregion
#pragma region Accessors

  [[nodiscard]] explicit operator bool() const noexcept { return conn_.get(); }
  [[nodiscard]] connection_role role() const noexcept { return role_; }
  [[nodiscard]] auto native(this auto& self) { return self.conn_.get(); }

  [[nodiscard]] bool is_handshake_completed() const noexcept {
    if (!conn_) return false;
    return ngtcp2_conn_get_handshake_completed(conn_.get()) != 0;
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
  // -- Stub trampolines. These satisfy ngtcp2's non-null requirements over
  // the AEAD / HP / rand / key callbacks but do nothing real. They will be
  // replaced by `ngtcp2_crypto_ossl` shims in the next milestone. Each
  // returns 0 (success) where it can, fills caller-provided buffers with
  // zeroes for "random" data, and otherwise leaves output unchanged.

  static int on_recv_client_initial(ngtcp2_conn*, const ngtcp2_cid*, void*) {
    return 0;
  }

  static int on_client_initial(ngtcp2_conn*, void*) { return 0; }

  static int on_recv_crypto_data(ngtcp2_conn*, ngtcp2_encryption_level,
      uint64_t, const uint8_t*, size_t, void*) {
    return 0;
  }

  static int on_recv_retry(ngtcp2_conn*, const ngtcp2_pkt_hd*, void*) {
    return 0;
  }

  static int on_encrypt(uint8_t* dest, const ngtcp2_crypto_aead*,
      const ngtcp2_crypto_aead_ctx*, const uint8_t* plaintext,
      size_t plaintextlen, const uint8_t*, size_t, const uint8_t*, size_t) {
    if (dest != plaintext) std::memcpy(dest, plaintext, plaintextlen);
    return 0;
  }

  static int on_decrypt(uint8_t* dest, const ngtcp2_crypto_aead*,
      const ngtcp2_crypto_aead_ctx*, const uint8_t* ciphertext,
      size_t ciphertextlen, const uint8_t*, size_t, const uint8_t*, size_t) {
    if (dest != ciphertext) std::memcpy(dest, ciphertext, ciphertextlen);
    return 0;
  }

  static int on_hp_mask(uint8_t* dest, const ngtcp2_crypto_cipher*,
      const ngtcp2_crypto_cipher_ctx*, const uint8_t*) {
    std::memset(dest, 0, NGTCP2_HP_MASKLEN);
    return 0;
  }

  static void on_rand(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx*) {
    std::memset(dest, 0, destlen);
  }

  static int on_get_new_connection_id2(ngtcp2_conn*, ngtcp2_cid* cid,
      ngtcp2_stateless_reset_token* token, size_t cidlen, void*) {
    cid->datalen = cidlen;
    std::memset(cid->data, 0, cidlen);
    std::memset(token->data, 0, sizeof(token->data));
    return 0;
  }

  static int on_remove_connection_id(ngtcp2_conn*, const ngtcp2_cid*, void*) {
    return 0;
  }

  static int on_update_key(ngtcp2_conn*, uint8_t*, uint8_t*,
      ngtcp2_crypto_aead_ctx*, uint8_t*, ngtcp2_crypto_aead_ctx*, uint8_t*,
      const uint8_t*, const uint8_t*, size_t, void*) {
    return 0;
  }

  static void
  on_delete_crypto_aead_ctx(ngtcp2_conn*, ngtcp2_crypto_aead_ctx*, void*) {}

  static void on_delete_crypto_cipher_ctx(ngtcp2_conn*,
      ngtcp2_crypto_cipher_ctx*, void*) {}

  static int on_get_path_challenge_data2(ngtcp2_conn*,
      ngtcp2_path_challenge_data* data, void*) {
    std::memset(data->data, 0, sizeof(data->data));
    return 0;
  }

  static int
  on_version_negotiation(ngtcp2_conn*, uint32_t, const ngtcp2_cid*, void*) {
    return 0;
  }

  static int on_recv_key(ngtcp2_conn*, ngtcp2_encryption_level, void*) {
    return 0;
  }

  static int on_handshake_completed(ngtcp2_conn*, void*) { return 0; }

#pragma endregion
#pragma region Helpers

  // Populate a callbacks table once per role. The struct is value-zeroed
  // before assignment so any future optional callback we leave out stays
  // null (which is what ngtcp2 expects for optional slots).
  [[nodiscard]] static const ngtcp2_callbacks& server_callbacks() noexcept {
    static const ngtcp2_callbacks cbs = [] {
      ngtcp2_callbacks c{};
      c.recv_client_initial = &on_recv_client_initial;
      c.recv_crypto_data = &on_recv_crypto_data;
      c.handshake_completed = &on_handshake_completed;
      c.encrypt = &on_encrypt;
      c.decrypt = &on_decrypt;
      c.hp_mask = &on_hp_mask;
      c.rand = &on_rand;
      c.get_new_connection_id2 = &on_get_new_connection_id2;
      c.remove_connection_id = &on_remove_connection_id;
      c.update_key = &on_update_key;
      c.delete_crypto_aead_ctx = &on_delete_crypto_aead_ctx;
      c.delete_crypto_cipher_ctx = &on_delete_crypto_cipher_ctx;
      c.get_path_challenge_data2 = &on_get_path_challenge_data2;
      c.version_negotiation = &on_version_negotiation;
      c.recv_rx_key = &on_recv_key;
      c.recv_tx_key = &on_recv_key;
      return c;
    }();
    return cbs;
  }

  [[nodiscard]] static const ngtcp2_callbacks& client_callbacks() noexcept {
    static const ngtcp2_callbacks cbs = [] {
      ngtcp2_callbacks c{};
      c.client_initial = &on_client_initial;
      c.recv_crypto_data = &on_recv_crypto_data;
      c.handshake_completed = &on_handshake_completed;
      c.recv_retry = &on_recv_retry;
      c.encrypt = &on_encrypt;
      c.decrypt = &on_decrypt;
      c.hp_mask = &on_hp_mask;
      c.rand = &on_rand;
      c.get_new_connection_id2 = &on_get_new_connection_id2;
      c.remove_connection_id = &on_remove_connection_id;
      c.update_key = &on_update_key;
      c.delete_crypto_aead_ctx = &on_delete_crypto_aead_ctx;
      c.delete_crypto_cipher_ctx = &on_delete_crypto_cipher_ctx;
      c.get_path_challenge_data2 = &on_get_path_challenge_data2;
      c.version_negotiation = &on_version_negotiation;
      c.recv_rx_key = &on_recv_key;
      c.recv_tx_key = &on_recv_key;
      return c;
    }();
    return cbs;
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

  struct conn_deleter {
    void operator()(ngtcp2_conn* p) const noexcept {
      if (p) ngtcp2_conn_del(p);
    }
  };
  using conn_ptr = std::unique_ptr<ngtcp2_conn, conn_deleter>;

#pragma endregion
#pragma region Data members

  conn_ptr conn_;
  connection_role role_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
