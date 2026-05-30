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
#include <sys/uio.h>
#include <utility>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "../net_endpoint.h"
#include "../../enums/bool_enums.h"
#include "../../concurrency/timeouts.h"
#include "../../infra/exception_firewalls.h"
#include "../../infra/log.h"
#include "../../strings/conversion.h"
#include "../io_uring/iou_buffer.h"

#include "quic_header.h"
#include "quic_ssl_ctx.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region write_stream_flags

// Flags passed to `quic_conn::writev_stream`, mirroring
// `NGTCP2_WRITE_STREAM_FLAG_*`. `more` tells ngtcp2 the caller has more stream
// data coming this turn and to defer finalizing the packet (used to coalesce
// small writes; the caller must follow up with another `writev_stream` or a
// flush). `fin` marks the supplied bytes as the last on the stream. `padding`
// pads the packet to the path MTU (typically only useful for probes).
enum class write_stream_flags : uint8_t {
  none = 0,
  more = NGTCP2_WRITE_STREAM_FLAG_MORE,      // 0x01
  fin = NGTCP2_WRITE_STREAM_FLAG_FIN,        // 0x02
  padding = NGTCP2_WRITE_STREAM_FLAG_PADDING // 0x04
};

#pragma endregion

}}} // namespace corvid::proto::quic

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::quic::write_stream_flags> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::proto::quic::write_stream_flags, "padding, fin, more">();

namespace corvid { inline namespace proto { namespace quic {

using namespace std::chrono_literals;

#pragma region quic_close_request

// Which CONNECTION_CLOSE frame variant ngtcp2 should emit.
//   transport:   CONNECTION_CLOSE of type 0x1c (RFC 9000 sec. 19.19).
//                `error_code` is from the QUIC transport error space
//                (`NGTCP2_*` constants, e.g., `NGTCP2_NO_ERROR`).
//   application: CONNECTION_CLOSE of type 0x1d. `error_code` is defined by
//                the application protocol (e.g., the `H3_*` constants from
//                RFC 9114 sec. 8.1).
enum class quic_close_kind : uint8_t {
  transport = 0x1c,
  application = 0x1d,
};

// A pending request to close the connection, carrying the kind, error code,
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
// trampolines forward into. The upper-layer protocol plugin (echo, DNS,
// HTTP/3, etc.) inherits this; the owning session installs the plugin via
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
// Noexcept policy: the trampolines in `quic_conn` are the firewall, not
// these virtuals. The trampolines are themselves `noexcept` (forced by
// the C ABI) and wrap each upcall in `quic_conn::try_callback`, which
// runs the call through `infra::try_or_log` and converts the result to
// ngtcp2's int status. That means the virtuals here are intentionally
// NOT `noexcept`: overrides are free to throw on allocation failure or
// library errors, and a thrown exception will be caught at the
// trampoline and reported to ngtcp2 as `NGTCP2_ERR_CALLBACK_FAILURE`,
// which drops the connection cleanly without terminating the process.
// Overrides MAY still mark themselves `noexcept` if they can't throw but MUST
// if they're not exception-safe. Outside this upcall surface, Corvid types
// reflect their throw behavior honestly (e.g.,
// `quic_stream_send_queue::append` is not `noexcept` because it
// allocates, while `commit` is). The same shape applies to other
// C-library callback wrappers (nghttp3, etc.): the wrapper static is
// the firewall via `try_callback`, the virtual / C++ method behind it
// can throw.
//
// Defaults are no-op `true`, so concrete plugins override only what they
// need.
class quic_conn_handlers {
public:
  using time_point_t = steady_now_clock::time_point_t;

  // Idle-timeout transport parameter advertised for this plugin's connections,
  // forwarded into `quic_conn::init` by the session's registration path. A `0`
  // disables the local idle timeout.
  const std::chrono::nanoseconds idle_timeout;

  explicit quic_conn_handlers(
      std::chrono::nanoseconds idle_timeout = 30s) noexcept
      : idle_timeout{idle_timeout} {}
  quic_conn_handlers(const quic_conn_handlers&) = delete;
  quic_conn_handlers& operator=(const quic_conn_handlers&) = delete;
  virtual ~quic_conn_handlers() = default;

#pragma region Handshake progression

  // TLS handshake finished (both endpoints have completed). On the server this
  // fires once the server has received and processed the client's Finished; on
  // the client, when the client itself has sent Finished. Fires once per conn.
  [[nodiscard]] virtual bool on_handshake_completed() { return true; }

  // 1-RTT TX key is installed; the endpoint can now send application data.
  // This is the moment HTTP/3 / nghttp3 cares about for submitting requests
  // and responses. Fires before `on_handshake_completed` on the server, after
  // it on the client.
  [[nodiscard]] virtual bool on_app_tx_ready() { return true; }

  // Handshake confirmed (RFC 9001 sec. 4.1.2). Client-only: fires when
  // HANDSHAKE_DONE arrives, signaling that the server has fully
  // installed 1-RTT and the client may discard Handshake-level state.
  // ngtcp2 silently transitions the server into the confirmed state
  // without a callback, so concrete plugins do not see this on the
  // server side; rely on `on_handshake_completed` there instead.
  [[nodiscard]] virtual bool on_handshake_confirmed() { return true; }

#pragma endregion
#pragma region Stream lifecycle

  // Peer opened a new stream.
  [[nodiscard]] virtual bool on_stream_open(quic_stream_id stream_id) {
    (void)stream_id;
    return true;
  }

  // Inbound stream payload. `flags` carries `fin` and/or `zero_rtt`. May fire
  // with empty `data` on a pure FIN. Bytes are valid only for the call
  // duration; copy or hand to the upper layer (e.g.,
  // `nghttp3_conn_read_stream`) before returning.
  [[nodiscard]] virtual bool on_recv_stream_data(quic_stream_id stream_id,
      uint64_t offset, std::span<const uint8_t> data,
      quic_stream_data_flags flags) {
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
      quic_stream_id stream_id, uint64_t offset, uint64_t datalen) {
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
      uint64_t final_size, uint64_t app_error_code) {
    (void)stream_id;
    (void)final_size;
    (void)app_error_code;
    return true;
  }

  // Peer sent STOP_SENDING: it no longer wants our data on `stream_id`. The
  // plugin should stop submitting bytes for this stream; ngtcp2 will emit
  // RESET_STREAM on the next outbound turn.
  [[nodiscard]] virtual bool
  on_stream_stop_sending(quic_stream_id stream_id, uint64_t app_error_code) {
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
      std::optional<uint64_t> app_error_code) {
    (void)stream_id;
    (void)app_error_code;
    return true;
  }

#pragma endregion
#pragma region Flow control feedback

  // Peer raised our send window on `stream_id` to `max_data` bytes total. A
  // plugin previously stalled on this stream may resume; HTTP/3 forwards to
  // `nghttp3_conn_unblock_stream`.
  [[nodiscard]] virtual bool
  on_extend_max_stream_data(quic_stream_id stream_id, uint64_t max_data) {
    (void)stream_id;
    (void)max_data;
    return true;
  }

  // Peer raised our limit on locally-initiated bidirectional / unidirectional
  // streams to `max_streams` total. The plugin may now open additional streams
  // up to that count.
  [[nodiscard]] virtual bool on_extend_max_local_streams_bidi(
      uint64_t max_streams) {
    (void)max_streams;
    return true;
  }
  [[nodiscard]] virtual bool on_extend_max_local_streams_uni(
      uint64_t max_streams) {
    (void)max_streams;
    return true;
  }

#pragma endregion
#pragma region Datagrams (RFC 9221)

  // Inbound DATAGRAM frame.
  [[nodiscard]] virtual bool
  on_recv_datagram(std::span<const uint8_t> data, quic_datagram_flags flags) {
    (void)data;
    (void)flags;
    return true;
  }

  // Peer acknowledged the packet carrying the DATAGRAM whose `dgram_id` (the
  // value we passed to `writev_datagram`) is given. Datagrams are unreliable;
  // this is a telemetry signal, not a contract: it fires at most once per
  // `dgram_id`.
  [[nodiscard]] virtual bool on_ack_datagram(uint64_t dgram_id) {
    (void)dgram_id;
    return true;
  }

  // ngtcp2 declared the packet carrying the DATAGRAM with `dgram_id` lost.
  // Datagrams are not retransmitted; this is a telemetry signal for the
  // application to decide whether to resend.
  [[nodiscard]] virtual bool on_lost_datagram(uint64_t dgram_id) {
    (void)dgram_id;
    return true;
  }

#pragma endregion
};

#pragma endregion
#pragma region quic_conn

// C++ wrapper over ngtcp2's per-connection API. Owns:
//   - an `ngtcp2_conn` (the QUIC state machine),
//   - an `SSL*` (the TLS state machine on top of which QUIC carries its
//     cryptographic handshake), and
//   - an `ngtcp2_crypto_ossl_ctx*` (the shim that lets ngtcp2 drive the
//     OpenSSL state machine).
//
// The shim's standard callback set is installed on the ngtcp2 conn, so a real
// TLS 1.3 handshake actually runs to completion against a properly configured
// peer.
//
// `quic_conn` is intentionally neither copyable nor movable: ngtcp2 stores our
// `this` pointer as `user_data` at construction time and the shim stores
// `&conn_ref_` (also pointing into `this`) on the SSL via `SSL_set_app_data`.
// Neither has a setter, so the wrapper's address must stay fixed for the
// lifetime of the underlying objects. Hold one by value at a stable address (a
// member of a session whose own address is pinned via `shared_ptr`) or via
// `std::unique_ptr<quic_conn>`.
class quic_conn {
public:
  using key_t = quic_cid;
  using time_point_t = steady_now_clock::time_point_t;

#pragma region Construction

  // Construct a `quic_conn` bound to `tls`. The role is taken from `tls.role`
  // and pinned for the lifetime of the conn. Only sets up the per-conn SSL
  // object and shim context; the `ngtcp2_conn` itself is allocated by `init`,
  // once the CIDs and endpoints are known.
  //
  // Loop-thread only beyond construction:
  //   * Call `init` exactly once before any I/O method or handler upcall
  //     fires. Check its return; on `false`, discard the conn.
  //   * Call `set_handlers` between construction and the first I/O.
  //   * All I/O methods (`read_pkt`, `write_pkt`, `writev_stream`,
  //     `open_bidi_stream`, `expiry`, `handle_expiry`) deref `conn_`
  //     unconditionally; they require a successful `init` and must
  //     run on the loop thread.
  //
  // The session layer wraps all of this; outside callers never see a
  // not-yet-initialized or failed conn (a failed `init` is terminal, and the
  // session is discarded before the caller observes it). For cross-thread
  // progress signals, override the relevant `quic_conn_handlers` upcall
  // instead of reaching into the conn.
  explicit quic_conn(quic_ssl_ctx& tls) noexcept : role_{tls.role()} {
    ensure_crypto_init();
    if (!tls) return;

    // 1. Per-conn SSL object, with `conn_ref_` set as app_data so the crypto
    // shim can recover our `ngtcp2_conn*` during handshake.
    ssl_ptr ssl{SSL_new(tls.native())};
    if (!ssl) return;
    conn_ref_.get_conn = &get_conn_static;
    conn_ref_.user_data = this;
    SSL_set_app_data(ssl.get(), &conn_ref_);

    // 2. Install the QUIC TLS callbacks on the SSL per role, then put the SSL
    // into accept/connect state. The shim's `configure_*_session` only wires
    // the callback table; OpenSSL still needs to know which direction this SSL
    // drives, or `SSL_do_handshake` fails with "connection type not set".
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

    // Store what we have but stop short of instantiating the ngtcp2 conn.
    ssl_ = std::move(ssl);
    ossl_ctx_ = std::move(ossl_ctx);
  }

  quic_conn(const quic_conn&) = delete;
  quic_conn& operator=(const quic_conn&) = delete;
  quic_conn(quic_conn&&) = delete;
  quic_conn& operator=(quic_conn&&) = delete;

  // Finish initalization, using the provided CIDs and endpoints to allocate
  // the underlying `ngtcp2_conn`. Must be called exactly once after
  // construction, before any I/O or handler upcall fires.
  //
  // Returns `false` if the ctor's SSL setup failed, or if
  // `ngtcp2_conn_{server,client}_new` fails; the caller is expected to discard
  // the entire `quic_conn` in that case.
  //
  // `dcid` is the destination CID this side puts on outbound packets. For the
  // server role, that is the SCID the client first sent (the client's own
  // CID). For the client role, that is a random CID the client picks for the
  // Initial DCID, which the server echoes back via the
  // `original_destination_connection_id` transport parameter so the client can
  // verify the response came from the same server it asked.
  //
  // `scid` is this side's own CID (the source CID we put on outbound packets
  // and the routing key by which the peer addresses us).
  //
  // `original_dcid` applies only to the server role: it is the DCID the client
  // put on the wire in its first Initial, which travels back via the
  // `original_destination_connection_id` transport parameter. For the client
  // role, pass `key_t{}`.
  [[nodiscard]] bool init(const key_t& dcid, const key_t& scid,
      const net_endpoint& local, const net_endpoint& peer,
      const key_t& original_dcid, time_point_t now,
      std::chrono::nanoseconds idle_timeout) noexcept {
    if (!ssl_ || !ossl_ctx_) return false;
    if (conn_) return false;
    // Cascade failure in the unlikely event that `RAND_bytes` fails.
    if (!dcid || !scid) return false;
    if (role_ == connection_role::server && !original_dcid) return false;

    ngtcp2_path_storage_init(&path_storage_, local.as_sockaddr_ptr(),
        local.sockaddr_size(), peer.as_sockaddr_ptr(), peer.sockaddr_size(),
        nullptr);

    // 4. Build ngtcp2 settings + transport params + path.
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = steady_now_clock::as_nanoseconds(now);

    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    // ngtcp2's defaults are all-zero, which would forbid the peer from opening
    // any streams or sending any data. Set v1 working defaults so the echo
    // plugin (and any other stream-using upper plugin) can do meaningful work
    // out of the box: 64 bidi streams, 3 uni streams (the HTTP/3 control +
    // qpack enc/dec count), and a 1 MB connection-level flow-control window
    // with 256 KB per stream. These can be raised mid- connection by
    // MAX_STREAMS / MAX_DATA frames. Configurability is deferred; if a caller
    // needs tighter or looser limits, init will grow a transport-params
    // override later.
    params.initial_max_streams_bidi = 64;
    params.initial_max_streams_uni = 3;
    params.initial_max_data = 1U << 20;                    // 1 MB
    params.initial_max_stream_data_bidi_local = 1U << 18;  // 256 KB
    params.initial_max_stream_data_bidi_remote = 1U << 18; // 256 KB
    params.initial_max_stream_data_uni = 1U << 18;         // 256 KB
    params.max_idle_timeout =
        static_cast<ngtcp2_duration>(idle_timeout.count());
    if (role_ == connection_role::server) {
      params.original_dcid = original_dcid.value();
      params.original_dcid_present = 1;
    }

    ngtcp2_conn* raw_conn = nullptr;
    const auto& cbs =
        (role_ == connection_role::server)
            ? server_callbacks
            : client_callbacks;
    const int rv =
        (role_ == connection_role::server)
            ? ngtcp2_conn_server_new(&raw_conn, dcid.pointer(), scid.pointer(),
                  &path_storage_.path, NGTCP2_PROTO_VER_V1, &cbs, &settings,
                  &params, nullptr, this)
            : ngtcp2_conn_client_new(&raw_conn, dcid.pointer(), scid.pointer(),
                  &path_storage_.path, NGTCP2_PROTO_VER_V1, &cbs, &settings,
                  &params, nullptr, this);
    if (rv != 0) return false;
    conn_ptr conn{raw_conn};

    // 5. Bind shim ctx to the ngtcp2 conn. ngtcp2 does not take ownership; the
    // ctx and SSL must outlive the conn (handled by member order).
    ngtcp2_conn_set_tls_native_handle(conn.get(), ossl_ctx_.get());

    conn_ = std::move(conn);
    return true;
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool ok() const noexcept { return !!conn_; }
  [[nodiscard]] explicit operator bool() const noexcept { return !!conn_; }
  [[nodiscard]] bool operator!() const noexcept { return !conn_; }
  [[nodiscard]] connection_role role() const noexcept { return role_; }
  [[nodiscard]] auto native(this auto& self) { return self.conn_.get(); }

  // Return the bound local/peer address by value..
  [[nodiscard]] net_endpoint local() const noexcept {
    return net_endpoint{*path_storage_.path.local.addr,
        path_storage_.path.local.addrlen};
  }
  [[nodiscard]] net_endpoint peer() const noexcept {
    return net_endpoint{*path_storage_.path.remote.addr,
        path_storage_.path.remote.addrlen};
  }

#pragma endregion
#pragma region Plugin wiring

  // Install the upcall handlers that `quic_conn`'s ngtcp2 trampolines dispatch
  // into. The session must call this once, after the upper plugin is
  // constructed and before any I/O entry point (`read_pkt`, `write_pkt`,
  // `writev_stream`) fires.
  //
  // The trampolines deref `handlers_` unconditionally, so a null pointer here
  // at I/O time crashes. The pointee must outlive `quic_conn`; typically it is
  // a base subobject of the upper plugin owned next to `quic_conn` in the
  // session.
  void set_handlers(quic_conn_handlers* handlers) noexcept {
    handlers_ = handlers;
  }

  // Queue a graceful CONNECTION_CLOSE for the session's per-turn drain to emit
  // after `read_pkt` returns. Typically called from inside a
  // `quic_conn_handlers` upcall (which then returns the bool of its choosing
  // to ngtcp2). Requesting close is terminal: this is a one-shot decision, not
  // something the conn keeps polling.
  //
  // The defaults give a no-fault clean close (transport NO_ERROR, no reason
  // phrase). `kind` selects the CONNECTION_CLOSE frame variant the drain emits
  // (RFC 9000 sec. 19.19); `error_code` is interpreted in the matching
  // namespace (transport vs application). The storage backing `reason` must
  // outlive the read_pkt + drain cycle.
  void request_close(quic_close_kind kind = quic_close_kind::transport,
      uint64_t error_code = 0, std::string_view reason = {}) noexcept {
    pending_close_ = {kind, error_code, reason};
  }

  // True iff a `request_close` has been stashed and not yet shipped. The
  // session inspects this after the upper-plugin drain returns and, if set,
  // ships one more packet via `write_connection_close`. `quic_conn` itself
  // does not poll this; the consumer is the session's post-drain step.
  [[nodiscard]] bool has_pending_close() const noexcept {
    return pending_close_.kind != quic_close_kind{};
  }

#pragma endregion
#pragma region I/O

  // Feed a received datagram into the conn for decryption + decoding. This
  // leads to various callbacks being invoked.
  [[nodiscard]] quic_status read_pkt(std::span<const uint8_t> pkt,
      time_point_t now = steady_now_clock::now()) noexcept {
    const int rv = ngtcp2_conn_read_pkt(conn_.get(), &path_storage_.path,
        nullptr, pkt.data(), pkt.size(),
        steady_now_clock::as_nanoseconds(now));
    return static_cast<quic_status>(rv);
  }

  // Drive a single outgoing packet carrying only non-stream frames (ACKs,
  // MAX_DATA, etc.). ngtcp2 writes the produced packet into `buf`'s tail,
  // extending `buf`'s payload by the packet length; the caller calls again on
  // the same `buf` to keep appending packets, or borrows a fresh `buf` per
  // packet for one-packet-per-datagram shipping. ngtcp2 may update the
  // destination path in `path_storage_`.
  //
  // ngtcp2 packets are atomic, so this gives up (returns `ok` without
  // extending `buf`) whenever the remaining tail can't hold the next packet
  // ngtcp2 wants to emit, not just when the tail is fully empty. The same
  // status (`ok`, payload unchanged) covers three cases the caller can't
  // distinguish directly: tail too small, congestion control limit, or
  // ngtcp2 idle.
  //
  // The cheap disambiguator is "retry once with a fresh buffer": if the new
  // buffer also doesn't grow, it was congestion or idle and the drain can
  // stop; if it grows, the prior failure was capacity. Production
  // one-packet-per-buffer drains (the session's `drain_writes`) get this for
  // free since every iteration starts from a fresh path-MTU buffer. On error,
  // `buf` is left unchanged.
  [[nodiscard]] quic_status write_pkt(iouring::iou_buffer& buf,
      time_point_t now = steady_now_clock::now()) noexcept {
    auto tail = buf.tail_span();
    if (tail.empty()) return quic_status::ok;
    const ngtcp2_ssize rv = ngtcp2_conn_write_pkt(conn_.get(),
        &path_storage_.path, nullptr, reinterpret_cast<uint8_t*>(tail.data()),
        tail.size(), steady_now_clock::as_nanoseconds(now));
    if (rv < 0) return static_cast<quic_status>(rv);
    if (rv > 0)
      (void)buf.update_payload({tail.data(), static_cast<size_t>(rv)});
    return quic_status::ok;
  }

  // Drive a single outgoing packet, optionally carrying stream bytes. ngtcp2
  // writes the produced packet into `buf`'s tail (extending `buf`'s payload by
  // the packet length) and reports through `bytes_accepted` how many bytes of
  // `iov` it consumed into its send queue. The caller owns `iov` and is
  // responsible for advancing its own per-stream cursors by `bytes_accepted`
  // between calls; ngtcp2 may accept zero, some prefix, or all of the offered
  // bytes depending on flow control and packet capacity. To keep appending
  // packets, call again on the same `buf`: its tail moves forward across
  // calls. To ship each packet as its own UDP datagram, borrow a fresh `buf`
  // per packet.
  //
  // ngtcp2 packets are atomic, so this gives up (returns `ok` without
  // extending `buf` and with `bytes_accepted == 0`) whenever the remaining
  // tail can't hold the next packet ngtcp2 wants to emit, not just when the
  // tail is fully empty. The same status covers three cases the caller can't
  // distinguish directly: tail too small, congestion control limit, or ngtcp2
  // idle.
  //
  // The cheap disambiguator is "retry once with a fresh buffer": if the new
  // buffer also doesn't grow, it was congestion or idle and the drain can
  // stop; if it grows, the prior failure was capacity (ship the partial
  // buffer, keep the new one going). The drain terminates when the caller has
  // no more bytes to offer and a fresh-buffer call also stops growing.
  //
  // Bytes accepted into the stream queue (reflected in `bytes_accepted`) must
  // remain valid in the caller's storage until the peer ACKs them via
  // `on_acked_stream_data_offset`. `stream_id == quic_stream_id::none` (the
  // ngtcp2 -1 sentinel) emits a packet carrying only non-stream frames (ACKs,
  // MAX_DATA, etc.), the same as `write_pkt`; `bytes_accepted` is `0` in that
  // case. `flags` selects ngtcp2 write modifiers; see `write_stream_flags` for
  // the per-bit semantics (`fin` to terminate the stream, `more` to coalesce
  // subsequent calls into the same packet, `padding` to pad to path MTU).
  //
  // `write_more` is not a real error: ngtcp2 returns it when the caller
  // passed `write_stream_flags::more`, bytes were consumed into an
  // in-progress packet (`bytes_accepted` reflects the count), and ngtcp2
  // wants the caller to either supply more stream data on the same `buf`
  // or finalize via `write_pkt` / a follow-up call without `more`. `buf`
  // is intentionally not extended yet: no packet exists on the wire until
  // finalization. Callers that do not pass `more` will never see this status.
  // On any other error, `buf` is left unchanged and `bytes_accepted` is `0`.
  //
  // ngtcp2 may prefer to emit queued non-stream frames (ACKs, MAX_DATA,
  // HANDSHAKE_DONE, etc.) ahead of the offered stream data even when
  // `stream_id` names a real stream and `more` was set: the call returns `ok`
  // with `bytes_accepted == 0` and `buf` extended by the non-stream packet,
  // while the offered stream bytes stay buffered for the next call. A
  // MORE-using drain must keep calling: the first invocation does not reliably
  // surface `write_more`. Termination is "buf stopped growing", not
  // "bytes_accepted == 0".
  [[nodiscard]] quic_status writev_stream(quic_stream_id stream_id,
      std::span<const iovec> iov, iouring::iou_buffer& buf,
      uint64_t& bytes_accepted,
      write_stream_flags flags = write_stream_flags::none,
      time_point_t now = steady_now_clock::now()) noexcept {
    bytes_accepted = 0;
    auto tail = buf.tail_span();
    if (tail.empty()) return quic_status::ok;
    ngtcp2_ssize pdatalen{-1};
    const ngtcp2_ssize rv = ngtcp2_conn_writev_stream(conn_.get(),
        &path_storage_.path, nullptr, reinterpret_cast<uint8_t*>(tail.data()),
        tail.size(), &pdatalen, *flags, *stream_id,
        reinterpret_cast<const ngtcp2_vec*>(iov.data()), iov.size(),
        steady_now_clock::as_nanoseconds(now));
    auto status = quic_status::ok;
    if (rv > 0) {
      (void)buf.update_payload({tail.data(), static_cast<size_t>(rv)});
    } else {
      status = static_cast<quic_status>(rv);
      if (status != quic_status::write_more) return status;
    }
    if (pdatalen > 0) bytes_accepted = static_cast<uint64_t>(pdatalen);
    return status;
  }

  // Emit a CONNECTION_CLOSE frame from the stash set by `request_close`.
  [[nodiscard]] quic_status write_connection_close(iouring::iou_buffer& buf,
      time_point_t now = steady_now_clock::now()) noexcept {
    if (!has_pending_close()) return quic_status::ok;
    auto tail = buf.tail_span();
    if (tail.empty()) return quic_status::ok;
    ngtcp2_ccerr ccerr;
    const auto reason = strings::as_byte_span(pending_close_.reason);
    if (pending_close_.kind == quic_close_kind::application)
      ngtcp2_ccerr_set_application_error(&ccerr, pending_close_.error_code,
          reason.data(), reason.size());
    else
      ngtcp2_ccerr_set_transport_error(&ccerr, pending_close_.error_code,
          reason.data(), reason.size());
    const ngtcp2_ssize rv = ngtcp2_conn_write_connection_close(conn_.get(),
        &path_storage_.path, nullptr, reinterpret_cast<uint8_t*>(tail.data()),
        tail.size(), &ccerr, steady_now_clock::as_nanoseconds(now));
    if (rv < 0) return static_cast<quic_status>(rv);
    if (rv == 0) return quic_status::internal;
    (void)buf.update_payload({tail.data(), static_cast<size_t>(rv)});
    pending_close_ = {};
    return quic_status::ok;
  }

  // Open a locally-initiated bidirectional stream. On success `stream_id` is
  // set to the ngtcp2-picked id from the next free slot in the local
  // bidirectional space, which the caller uses on subsequent `writev_stream`
  // calls. Fails with `NGTCP2_ERR_STREAM_ID_BLOCKED` when the peer's
  // `initial_max_streams_bidi` has been exhausted; the caller may retry after
  // `on_extend_max_local_streams_bidi` fires. On input, `stream_id` must be
  // `quic_stream_id::none`. On failure `stream_id` is left untouched.
  [[nodiscard]] quic_status open_bidi_stream(
      quic_stream_id& stream_id) noexcept {
    assert(stream_id == quic_stream_id::none);
    int64_t raw{};
    const int rv = ngtcp2_conn_open_bidi_stream(conn_.get(), &raw, nullptr);
    if (rv != 0) return static_cast<quic_status>(rv);
    stream_id = static_cast<quic_stream_id>(raw);
    return quic_status::ok;
  }

#pragma endregion
#pragma region Expiry

  // The next deadline at which `handle_expiry` should be called. If ngtcp2
  // currently has no pending timer, returns `time_point_t::max`. Caller arms
  // an external timer at this point and invokes `handle_expiry` when it fires.
  [[nodiscard]] time_point_t expiry() const noexcept {
    return steady_now_clock::from_nanoseconds(
        ngtcp2_conn_get_expiry(conn_.get()));
  }

  [[nodiscard]] quic_status handle_expiry(
      time_point_t now = steady_now_clock::now()) noexcept {
    return static_cast<quic_status>(ngtcp2_conn_handle_expiry(conn_.get(),
        steady_now_clock::as_nanoseconds(now)));
  }

  // True once ngtcp2 has entered the closing (we sent CONNECTION_CLOSE) or
  // draining (peer sent it) period. In both, no further transmission is
  // allowed; the application owns the RFC 9000 sec. 10.2 wind-down timer,
  // since ngtcp2 does not fold that period into `expiry`.
  [[nodiscard]] bool in_close_period() const noexcept {
    return ngtcp2_conn_in_closing_period(conn_.get()) ||
           ngtcp2_conn_in_draining_period(conn_.get());
  }

  // Current Probe Timeout. The wind-down period after entering closing /
  // draining is 3*PTO (RFC 9000 sec. 10.2). This is expected to be much
  // shorter than the idle timeout.
  [[nodiscard]] std::chrono::nanoseconds pto() const noexcept {
    return std::chrono::nanoseconds{ngtcp2_conn_get_pto(conn_.get())};
  }

#pragma endregion
#pragma region Handlers
private:
  // App-supplied callbacks. The crypto-shim functions handle the AEAD / HP /
  // key / crypto-data callbacks; we only own the ones the shim does not
  // provide. Trampolines that surface a `quic_conn_handlers` upcall recover
  // the typed `quic_conn*` from `user_data` and translate the handler's bool
  // return into `0` / `NGTCP2_ERR_CALLBACK_FAILURE`. The owning session must
  // call `set_handlers` before any I/O fires (`read_pkt`, `write_pkt`,
  // `writev_stream`), since the trampolines deref `handlers_` unconditionally;
  // a null pointer here will crash. A handler that wants a graceful
  // CONNECTION_CLOSE instead of a callback failure calls `request_close` first
  // to request the close, then returns whichever bool fits its needs.

  // Generate random bytes. No way to signal error and can't throw through C,
  // so just terminate in the unlikely event that it fails
  static void
  on_rand(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx*) noexcept {
    try_or_terminate([&] {
      if (destlen > static_cast<size_t>(std::numeric_limits<int>::max()))
        log::fatal("on_rand: destlen {} exceeds int max", destlen);

      if (RAND_bytes(dest, static_cast<int>(destlen)) != 1)
        log::fatal("on_rand: RAND_bytes failed");

      return true;
    });
  }

  // CIDs must be unpredictable per RFC 9000 sec. 5.1, so fill with
  // cryptographic randomness. The stateless-reset token is reused as the
  // "secret known to the issuer" in the reset path; same constraint applies.
  static int on_get_new_connection_id2(ngtcp2_conn*, ngtcp2_cid* cid,
      ngtcp2_stateless_reset_token* token, size_t cidlen, void*) noexcept {
    cid->datalen = cidlen;
    return success(
        RAND_bytes(cid->data, static_cast<int>(cidlen)) == 1 &&
        RAND_bytes(token->data, sizeof(token->data)) == 1);
  }

  // CID retirement is not yet plumbed back to the router; the CID-rotation
  // milestone will replace this no-op with a session-level
  // `router.remove_session` call.
  static int
  on_remove_connection_id(ngtcp2_conn*, const ngtcp2_cid*, void*) noexcept {
    return success(true);
  }

  // `recv_rx_key` is unused: nothing in the handler contract reacts to RX key
  // installation. The TX direction is handled separately by `on_recv_tx_key`
  // (it filters on 1-RTT and surfaces `on_app_tx_ready`).
  static int
  on_recv_rx_key(ngtcp2_conn*, ngtcp2_encryption_level, void*) noexcept {
    return success(true);
  }

  // v2 trampoline around the shim's v1 `get_path_challenge_data` (which writes
  // raw bytes). The ngtcp2 callback table at version V3 uses the v2 signature,
  // which takes a typed struct that wraps the same byte array.
  static int on_get_path_challenge_data2(ngtcp2_conn* conn,
      ngtcp2_path_challenge_data* data, void* user_data) noexcept {
    return ngtcp2_crypto_get_path_challenge_data_cb(conn, data->data,
        user_data);
  }

  // Trampolines that forward `quic_conn_handlers` upcalls.

  static int on_handshake_completed(ngtcp2_conn*, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_handshake_completed();
    });
  }

  static int on_handshake_confirmed(ngtcp2_conn*, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_handshake_confirmed();
    });
  }

  // `recv_tx_key` fires once per TX-key install during the handshake; ngtcp2
  // omits the INITIAL level, so the levels we observe are HANDSHAKE and 1-RTT.
  // Application data only becomes sendable at 1-RTT, so we filter and only
  // surface `on_app_tx_ready` then. The intermediate HANDSHAKE call is
  // silently ignored.
  static int on_recv_tx_key(ngtcp2_conn*, ngtcp2_encryption_level level,
      void* user_data) noexcept {
    if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) return success(true);
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] { return self->handlers_->on_app_tx_ready(); });
  }

  static int
  on_stream_open(ngtcp2_conn*, int64_t stream_id, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_stream_open(
          static_cast<quic_stream_id>(stream_id));
    });
  }

  static int on_recv_stream_data(ngtcp2_conn*, uint32_t flags,
      int64_t stream_id, uint64_t offset, const uint8_t* data, size_t datalen,
      void* user_data, void*) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_recv_stream_data(
          static_cast<quic_stream_id>(stream_id), offset,
          std::span<const uint8_t>{data, datalen},
          static_cast<quic_stream_data_flags>(flags));
    });
  }

  static int on_acked_stream_data_offset(ngtcp2_conn*, int64_t stream_id,
      uint64_t offset, uint64_t datalen, void* user_data, void*) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_acked_stream_data_offset(
          static_cast<quic_stream_id>(stream_id), offset, datalen);
    });
  }

  static int on_stream_reset(ngtcp2_conn*, int64_t stream_id,
      uint64_t final_size, uint64_t app_error_code, void* user_data,
      void*) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_stream_reset(
          static_cast<quic_stream_id>(stream_id), final_size, app_error_code);
    });
  }

  static int on_stream_stop_sending(ngtcp2_conn*, int64_t stream_id,
      uint64_t app_error_code, void* user_data, void*) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_stream_stop_sending(
          static_cast<quic_stream_id>(stream_id), app_error_code);
    });
  }

  static int on_stream_close(ngtcp2_conn*, uint32_t flags, int64_t stream_id,
      uint64_t app_error_code, void* user_data, void*) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    std::optional<uint64_t> ec;
    if (flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)
      ec = app_error_code;
    return try_callback([&] {
      return self->handlers_->on_stream_close(
          static_cast<quic_stream_id>(stream_id), ec);
    });
  }

  static int on_extend_max_stream_data(ngtcp2_conn*, int64_t stream_id,
      uint64_t max_data, void* user_data, void*) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_extend_max_stream_data(
          static_cast<quic_stream_id>(stream_id), max_data);
    });
  }

  static int on_extend_max_local_streams_bidi(ngtcp2_conn*,
      uint64_t max_streams, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_extend_max_local_streams_bidi(max_streams);
    });
  }

  static int on_extend_max_local_streams_uni(ngtcp2_conn*,
      uint64_t max_streams, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_extend_max_local_streams_uni(max_streams);
    });
  }

  static int on_recv_datagram(ngtcp2_conn*, uint32_t flags,
      const uint8_t* data, size_t datalen, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_recv_datagram(
          std::span<const uint8_t>{data, datalen},
          static_cast<quic_datagram_flags>(flags));
    });
  }

  static int
  on_ack_datagram(ngtcp2_conn*, uint64_t dgram_id, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_ack_datagram(dgram_id);
    });
  }

  static int
  on_lost_datagram(ngtcp2_conn*, uint64_t dgram_id, void* user_data) noexcept {
    auto* self = static_cast<quic_conn*>(user_data);
    return try_callback([&] {
      return self->handlers_->on_lost_datagram(dgram_id);
    });
  }

#pragma endregion
#pragma region Helpers

  // Translate a handler's `bool` return into the int ngtcp2 callbacks expect:
  // `0` on success, `NGTCP2_ERR_CALLBACK_FAILURE` on failure.
  [[nodiscard]] static constexpr int success(bool ok) noexcept {
    return ok ? 0 : NGTCP2_ERR_CALLBACK_FAILURE;
  }

  // Run `fn` (the body of a ngtcp2 trampoline) inside `try_or_log` so a thrown
  // exception becomes a `false` result, then convert that result to ngtcp2's
  // callback status via `success`. This is the canonical body for every
  // trampoline that forwards into a `quic_conn_handlers` upcall: the
  // trampoline is the noexcept firewall, and the virtual it calls is free to
  // throw under low memory or other failures without crossing the C ABI.
  template<std::invocable F>
  requires std::same_as<std::invoke_result_t<F>, bool>
  [[nodiscard]] static int try_callback(F&& fn) noexcept {
    return success(try_or_log(std::forward<F>(fn)));
  }

// Callback tables, one per role. Unmentioned slots are value-initialized to
// null, which is what ngtcp2 expects for optional callbacks.
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
  // Documented as optional but strongly recommended for performance. A
  // failure here means the crypto backend can't function (FIPS provider
  // load failure, OpenSSL provider mismatch, sandbox without urandom,
  // etc.) and every subsequent ngtcp2_crypto_* call would either crash
  // inside libssl or report `NGTCP2_ERR_INTERNAL` with no upstream
  // context. No recovery is possible: log and terminate.
  static void ensure_crypto_init() noexcept {
    [[maybe_unused]] static const int once = [] {
      try_or_terminate([] {
        const int rv = ngtcp2_crypto_ossl_init();
        if (rv != 0) log::fatal("ngtcp2_crypto_ossl_init failed (rv={})", rv);
        return true;
      });
      return true;
    }();
  }

  // For `ssl_ptr`, sever the SSL's back-pointer to `conn_ref_` before
  // `SSL_free`, per the shim's contract (we cannot guarantee that the
  // ngtcp2_conn outlives `SSL_free`, because `conn_` is destroyed first by
  // member order).
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
  // `ossl_ctx_` (via `ngtcp2_conn_set_tls_native_handle`), which in turn wraps
  // `ssl_`. Tearing down inside-out keeps every dependency live while it is
  // being used. `conn_ref_` is declared first so it outlives the SSL that
  // points at it.
  ngtcp2_crypto_conn_ref conn_ref_{};
  connection_role role_{};
  ngtcp2_path_storage path_storage_{};
  ssl_ptr ssl_;
  ossl_ctx_ptr ossl_ctx_;
  conn_ptr conn_;

  // Upper-plugin upcalls, installed via `set_handlers` after construction.
  // Null until then; trampolines deref unconditionally, so the session must
  // install handlers before any I/O fires.
  quic_conn_handlers* handlers_{};

  // Close request stashed by `request_close` (typically called from inside a
  // handler upcall); not externally observable. `kind == quic_close_kind{}`
  // means "not set". Last write in a turn wins.
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
