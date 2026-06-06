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
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string_view>

#include <ngtcp2/ngtcp2.h>

#include "../../enums.h"

// C++ wrappers over the slice of ngtcp2's C API used to recover the
// Destination Connection ID (DCID) from a received QUIC datagram, which is
// what `iou_dgram_router`'s plugin needs to demux packets onto sessions.

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_status

// `NGTCP2_ERR_*` wrapper. Outcome of `quic_version_cid::decode` (and any other
// ngtcp2 call we wrap that returns an `int` error code from this set).
// NOLINTNEXTLINE(performance-enum-size)
enum class quic_status : int16_t {
  ok = 0,
  invalid_argument = NGTCP2_ERR_INVALID_ARGUMENT,                       // -201
  nobuf = NGTCP2_ERR_NOBUF,                                             // -202
  proto = NGTCP2_ERR_PROTO,                                             // -203
  invalid_state = NGTCP2_ERR_INVALID_STATE,                             // -204
  ack_frame = NGTCP2_ERR_ACK_FRAME,                                     // -205
  stream_id_blocked = NGTCP2_ERR_STREAM_ID_BLOCKED,                     // -206
  stream_in_use = NGTCP2_ERR_STREAM_IN_USE,                             // -207
  stream_data_blocked = NGTCP2_ERR_STREAM_DATA_BLOCKED,                 // -208
  flow_control = NGTCP2_ERR_FLOW_CONTROL,                               // -209
  connection_id_limit = NGTCP2_ERR_CONNECTION_ID_LIMIT,                 // -210
  stream_limit = NGTCP2_ERR_STREAM_LIMIT,                               // -211
  final_size = NGTCP2_ERR_FINAL_SIZE,                                   // -212
  crypto = NGTCP2_ERR_CRYPTO,                                           // -213
  pkt_num_exhausted = NGTCP2_ERR_PKT_NUM_EXHAUSTED,                     // -214
  required_transport_param = NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM,       // -215
  malformed_transport_param = NGTCP2_ERR_MALFORMED_TRANSPORT_PARAM,     // -216
  frame_encoding = NGTCP2_ERR_FRAME_ENCODING,                           // -217
  decrypt = NGTCP2_ERR_DECRYPT,                                         // -218
  stream_shut_wr = NGTCP2_ERR_STREAM_SHUT_WR,                           // -219
  stream_not_found = NGTCP2_ERR_STREAM_NOT_FOUND,                       // -220
  stream_state = NGTCP2_ERR_STREAM_STATE,                               // -221
  recv_version_negotiation = NGTCP2_ERR_RECV_VERSION_NEGOTIATION,       // -222
  closing = NGTCP2_ERR_CLOSING,                                         // -223
  draining = NGTCP2_ERR_DRAINING,                                       // -224
  transport_param = NGTCP2_ERR_TRANSPORT_PARAM,                         // -225
  discard_pkt = NGTCP2_ERR_DISCARD_PKT,                                 // -226
  conn_id_blocked = NGTCP2_ERR_CONN_ID_BLOCKED,                         // -227
  internal = NGTCP2_ERR_INTERNAL,                                       // -228
  crypto_buffer_exceeded = NGTCP2_ERR_CRYPTO_BUFFER_EXCEEDED,           // -229
  write_more = NGTCP2_ERR_WRITE_MORE,                                   // -230
  retry = NGTCP2_ERR_RETRY,                                             // -231
  drop_conn = NGTCP2_ERR_DROP_CONN,                                     // -232
  aead_limit_reached = NGTCP2_ERR_AEAD_LIMIT_REACHED,                   // -233
  no_viable_path = NGTCP2_ERR_NO_VIABLE_PATH,                           // -234
  version_negotiation = NGTCP2_ERR_VERSION_NEGOTIATION,                 // -235
  handshake_timeout = NGTCP2_ERR_HANDSHAKE_TIMEOUT,                     // -236
  version_negotiation_failure = NGTCP2_ERR_VERSION_NEGOTIATION_FAILURE, // -237
  idle_close = NGTCP2_ERR_IDLE_CLOSE,                                   // -238
  fatal = NGTCP2_ERR_FATAL,                                             // -500
  nomem = NGTCP2_ERR_NOMEM,                                             // -501
  callback_failure = NGTCP2_ERR_CALLBACK_FAILURE,                       // -502
};
consteval auto corvid_enum_spec(quic_status*) {
  return corvid::enums::sequence::make_sequence_enum_spec<quic_status,
      "-502,callback_failure,nomem,fatal|-238,idle_close,version_negotiation_"
      "failure,handshake_timeout,version_negotiation,no_viable_path,aead_"
      "limit_reached,drop_conn,retry,write_more,crypto_buffer_exceeded,"
      "internal,conn_id_blocked,discard_pkt,transport_param,draining,closing,"
      "recv_version_negotiation,stream_state,stream_not_found,stream_shut_wr,"
      "decrypt,frame_encoding,malformed_transport_param,required_transport_"
      "param,pkt_num_exhausted,crypto,final_size,stream_limit,connection_id_"
      "limit,flow_control,stream_data_blocked,stream_in_use,stream_id_blocked,"
      "ack_frame,invalid_state,proto,nobuf,invalid_argument|0,ok">();
}

#pragma endregion

#pragma region is_soft_error

// True if `s` is a per-packet drop that should NOT take the connection
// down.
//
// Currently soft:
//   `decrypt`     - AEAD packet protection check failed (RFC 9001 sec. 5.2:
//                   discard the packet, keep the connection).
//   `discard_pkt` - ngtcp2 signals "drop this packet" (e.g., stateless
//                   reset, packet at the wrong encryption level).
//   `draining`    - peer sent CONNECTION_CLOSE; we are in the draining
//                   period and let ngtcp2's timer manage the wind-down.
//   `closing`     - we sent CONNECTION_CLOSE; ngtcp2 keeps responding to
//                   stragglers with rate-limited CONNECTION_CLOSEs until
//                   its own timer expires.
//
// Anything else non-ok is connection-fatal. New ngtcp2 statuses default
// to fatal (close-the-session) until a reviewer audits them and adds them
// here if appropriate.
[[nodiscard]] constexpr bool is_soft_error(quic_status s) noexcept {
  return s == quic_status::decrypt || s == quic_status::discard_pkt ||
         s == quic_status::draining || s == quic_status::closing;
}

#pragma endregion

#pragma region quic_cid

// Variable-length QUIC Connection ID, wrapping `ngtcp2_cid`. Up to
// `NGTCP2_MAX_CIDLEN` (20) bytes of payload plus a length field. Used as the
// routing-table key for the dgram router's session map: a single session can
// be registered under multiple CIDs (the client's original initial DCID, every
// SCID ngtcp2 issues for it, every CID added via `NEW_CONNECTION_ID` later in
// the connection), and ngtcp2's helpers (`ngtcp2_conn_get_scid`,
// `ngtcp2_conn_get_client_initial_dcid`) enumerate them for us at registration
// / teardown time.
//
// Comparison is byte-wise (length first, then `memcmp`: consistent total
// ordering). The hash specialization at file scope hashes the same byte view,
// so two CIDs are equal iff they hash equally.
class quic_cid {
public:
  static constexpr size_t max_length = NGTCP2_MAX_CIDLEN;

  constexpr quic_cid() noexcept = default;

  explicit quic_cid(const ngtcp2_cid& cid) noexcept : cid_{cid} {}

  explicit quic_cid(std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() > max_length) return;
    cid_.datalen = bytes.size();
    std::memcpy(cid_.data, bytes.data(), bytes.size());
  }

  [[nodiscard]] size_t length() const noexcept { return cid_.datalen; }
  [[nodiscard]] bool empty() const noexcept { return cid_.datalen == 0; }

  [[nodiscard]] explicit operator bool() const noexcept { return !empty(); }
  [[nodiscard]] bool operator!() const noexcept { return empty(); }

  // Bytes view, valid for the lifetime of this object.
  [[nodiscard]] std::span<const uint8_t> bytes() const noexcept {
    return {cid_.data, cid_.datalen};
  }

  // Underlying ngtcp2 struct, for handing to ngtcp2 C calls unchanged.
  [[nodiscard]] const ngtcp2_cid* pointer() const noexcept { return &cid_; }
  [[nodiscard]] ngtcp2_cid* pointer() noexcept { return &cid_; }
  [[nodiscard]] const ngtcp2_cid& value() const noexcept { return cid_; }

  [[nodiscard]] bool operator==(const quic_cid& other) const noexcept {
    return cid_.datalen == other.cid_.datalen &&
           std::memcmp(cid_.data, other.cid_.data, cid_.datalen) == 0;
  }

  [[nodiscard]] std::strong_ordering operator<=>(
      const quic_cid& other) const noexcept {
    if (auto c = cid_.datalen <=> other.cid_.datalen; c != 0) return c;
    const int r = std::memcmp(cid_.data, other.cid_.data, cid_.datalen);
    return r < 0   ? std::strong_ordering::less
           : r > 0 ? std::strong_ordering::greater
                   : std::strong_ordering::equal;
  }

private:
  ngtcp2_cid cid_{};
};

#pragma endregion
#pragma region quic_version_cid

// Wrapper over `ngtcp2_version_cid` (the output of
// `ngtcp2_pkt_decode_version_cid`), which carries the QUIC version and the two
// (source and destination) CID byte spans recovered from a packet's header
// without decrypting it.
//
// Use `decode` to populate the wrapped struct from raw packet bytes. The
// returned `dcid_bytes` / `scid_bytes` spans view directly into the source
// buffer (matching ngtcp2's contract: the struct stores `const uint8_t*`
// pointers into the original input). They are invalidated if the source buffer
// is freed, moved, or modified. For a stable copy of a CID, pass the span to
// `quic_cid`'s span constructor.
class quic_version_cid {
public:
  // Default length, in bytes, of the SCIDs we issue locally.
  static constexpr size_t default_scid_length = 16;

  constexpr quic_version_cid() noexcept = default;

  // Decode the QUIC header at the start of `packet`, populating version, DCID,
  // and (for long-header packets) SCID.
  //
  // `status() == ok` for any well-formed packet (long or short header).
  // `version_negotiation` when a long-header packet's version is unsupported
  // by ngtcp2 ; the CID fields are still populated so the caller can construct
  // a Version Negotiation response. `invalid_argument` for malformed framing.
  explicit quic_version_cid(std::span<const uint8_t> packet,
      size_t short_dcidlen = default_scid_length) noexcept {
    status_ = static_cast<quic_status>(ngtcp2_pkt_decode_version_cid(&vc_,
        packet.data(), packet.size(), short_dcidlen));
  }

  // Result of the decode performed by the constructor.
  // `quic_status::ok` on a default-constructed object that hasn't been
  // decoded.
  [[nodiscard]] quic_status status() const noexcept { return status_; }

  // True iff `status() == ok`. `operator!` is the inverse, for the
  // `if (!vc) { /* decode failed */ }` idiom.
  [[nodiscard]] explicit operator bool() const noexcept {
    return status_ == quic_status::ok;
  }
  [[nodiscard]] bool operator!() const noexcept {
    return status_ != quic_status::ok;
  }

  // QUIC version field. Zero for short-header packets, since they do not carry
  // a version on the wire.
  [[nodiscard]] uint32_t version() const noexcept { return vc_.version; }

  // True for long-header packets (Initial, 0-RTT, Handshake, Retry, Version
  // Negotiation). These carry both a version and an SCID on the wire.
  [[nodiscard]] bool is_long_header() const noexcept {
    return vc_.version != 0;
  }

  // View of the Destination Connection ID bytes inside the source packet.
  // Always populated on success.
  [[nodiscard]] std::span<const uint8_t> dcid_bytes() const noexcept {
    return {vc_.dcid, vc_.dcidlen};
  }

  // View of the Source Connection ID bytes inside the source packet. Empty for
  // short-header packets.
  [[nodiscard]] std::span<const uint8_t> scid_bytes() const noexcept {
    return {vc_.scid, vc_.scidlen};
  }

  // Underlying ngtcp2 struct, for callers that need to hand it back to a
  // ngtcp2 C function unchanged.
  [[nodiscard]] const ngtcp2_version_cid& value() const noexcept {
    return vc_;
  }

private:
  ngtcp2_version_cid vc_{};
  quic_status status_{};
};

#pragma endregion

}}} // namespace corvid::proto::quic

template<>
struct std::hash<corvid::proto::quic::quic_cid> {
  size_t operator()(const corvid::proto::quic::quic_cid& cid) const noexcept {
    const auto bytes = cid.bytes();
    return std::hash<std::string_view>{}(
        {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  }
};

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_stream_id

// QUIC stream identifier. Bit 0 encodes initiator (0 = client, 1 = server) and
// bit 1 encodes direction (0 = bidirectional, 1 = unidirectional) per RFC 9000
// sec. 2.1; bits above 1 are the stream sequence number, which the bitmask
// machinery carries as a residual (printed in hex if the value is
// stringified). `none` (`-1`) is the ngtcp2 sentinel used by `writev_stream`
// when only ACKs or other non-stream frames should be emitted.
enum class quic_stream_id : uint64_t {
  none = static_cast<uint64_t>(-1),
  server_initiated = 0x1,
  unidirectional = 0x2,
  sequence_mask = ~static_cast<uint64_t>(0x3)
};
consteval auto corvid_enum_spec(quic_stream_id*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<quic_stream_id,
      "unidirectional,server_initiated">();
}

#pragma endregion
#pragma region quic_stream_side

// Half of a QUIC stream to act on. The two halves are read and write (the
// local-vs-remote send directions of the same stream), distinct from the
// bidirectional / unidirectional distinction encoded in a `quic_stream_id`'s
// low bits. `both` is the union, used when an abort should tear down both
// halves at once.
enum class quic_stream_side : uint8_t {
  none = 0x0,
  read = 0x1,
  write = 0x2,
  both = read | write,
};
consteval auto corvid_enum_spec(quic_stream_side*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<quic_stream_side,
      "write,read">();
}

#pragma endregion
#pragma region quic_stream_data_flags

// Flags accompanying a `recv_stream_data` upcall, mirroring
// `NGTCP2_STREAM_DATA_FLAG_*`. `fin` marks the last bytes the peer will ever
// send on this stream; `zero_rtt` indicates the data arrived in a 0-RTT packet
// (and is therefore replayable; treat with the usual 0-RTT caution).
// NOLINTNEXTLINE(performance-enum-size)
enum class quic_stream_data_flags : uint32_t {
  none = 0x0,
  fin = NGTCP2_STREAM_DATA_FLAG_FIN,      // 0x1
  zero_rtt = NGTCP2_STREAM_DATA_FLAG_0RTT // 0x2
};
consteval auto corvid_enum_spec(quic_stream_data_flags*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<quic_stream_data_flags,
      "zero_rtt,fin">();
}

#pragma endregion
#pragma region quic_datagram_flags

// Flags accompanying a `recv_datagram` upcall, mirroring
// `NGTCP2_DATAGRAM_FLAG_*`. `zero_rtt` indicates the DATAGRAM rode in a 0-RTT
// packet. NOLINTNEXTLINE(performance-enum-size)
enum class quic_datagram_flags : uint32_t {
  none = 0x0,
  zero_rtt = NGTCP2_DATAGRAM_FLAG_0RTT // 0x1
};
consteval auto corvid_enum_spec(quic_datagram_flags*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<quic_datagram_flags,
      "zero_rtt">();
}

#pragma endregion

}}} // namespace corvid::proto::quic
