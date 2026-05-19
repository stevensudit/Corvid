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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <ngtcp2/ngtcp2.h>

#include "../../enums/sequence_enum.h"

// C++ wrappers over the slice of ngtcp2's C API used to recover the
// Destination Connection ID (DCID) from a received QUIC datagram, which is
// what `iou_dgram_router`'s plugin needs to demux packets onto sessions.
//
// Follows the same shape as `iou_wrap.h`: enums (and their registry
// specializations) come first at namespace scope so the sequence-enum
// operators are in effect before any class declares a method that returns or
// consumes them. The wrapper classes follow in a second namespace block.

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_decode_status

// `NGTCP2_ERR_*` wrapper. Outcome of `quic_version_cid::decode` (and any
// other ngtcp2 call we wrap that returns an `int` error code from this set).
enum class quic_decode_status : int16_t {
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
  NGTCP2_ERR_MUST_BE_INT16 = 0x7FFF
};

#pragma endregion

}}} // namespace corvid::proto::quic

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::quic::quic_decode_status> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::quic::quic_decode_status,
        corvid::proto::quic::quic_decode_status::ok,
        corvid::proto::quic::quic_decode_status::callback_failure>();

namespace corvid { inline namespace proto { namespace quic {

#pragma region quic_connection_id

// Routing-table key for a locally-issued QUIC Connection ID. Strongly-typed
// alias for `uint64_t`: we issue CIDs of exactly that width (see
// `quic_version_cid::cid_length`), so the type identity keeps them from
// being confused with arbitrary integers in routing tables or session APIs.
// `std::hash` and the comparison operators are provided by the language for
// scoped enums with an explicit underlying type, so no further machinery is
// needed.
//
// NOT used for peer-issued Source CIDs in long-header Initial packets, which
// can be 1..20 bytes per spec. Those live with the connection handshake
// path; the router only ever keys on CIDs it issued itself.
enum class quic_connection_id : uint64_t {};

#pragma endregion
#pragma region quic_version_cid

// Wrapper over `ngtcp2_version_cid` (the output of
// `ngtcp2_pkt_decode_version_cid`), which carries the QUIC version and the
// two (source and destination) CID byte spans recovered from a packet's
// header without decrypting it.
//
// Use `decode` to populate the wrapped struct from raw packet bytes. The
// returned `dcid_bytes()` / `scid_bytes()` spans view directly into the
// source buffer (matching ngtcp2's contract: the struct stores `const
// uint8_t*` pointers into the original input). They are invalidated if the
// source buffer is freed, moved, or modified. For a stable copy of a
// locally-issued DCID, hand the span through `from_ngtcp2_cid` after
// populating an `ngtcp2_cid`, or `memcpy` into a `quic_connection_id`
// directly.
class quic_version_cid {
public:
  // Length, in bytes, of the Connection IDs we issue locally. The QUIC spec
  // permits 1..20 (`NGTCP2_MAX_CIDLEN`).
  static constexpr size_t cid_length = sizeof(uint64_t);

  constexpr quic_version_cid() noexcept = default;

  // Decode the QUIC header at the start of `packet`, populating version,
  // DCID, and (for long-header packets) SCID. `short_dcidlen` is the length
  // of the DCID for short-header packets, which carry no on-wire length
  // field; defaults to the local CID length we issue. Long-header packets
  // carry the lengths on the wire and ignore this parameter.
  //
  // Returns `ok` for any well-formed packet (long or short header).
  // Returns `version_negotiation` when a long-header packet's version is
  // unsupported by ngtcp2; the CID fields are still populated so the
  // caller can construct a Version Negotiation response. Returns
  // `invalid_argument` for malformed framing.
  [[nodiscard]] quic_decode_status decode(std::span<const uint8_t> packet,
      size_t short_dcidlen = cid_length) noexcept {
    const int rv = ngtcp2_pkt_decode_version_cid(&vc_, packet.data(),
        packet.size(), short_dcidlen);
    return static_cast<quic_decode_status>(rv);
  }

  // QUIC version field. Zero for short-header packets, since they do not
  // carry a version on the wire.
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

  // View of the Source Connection ID bytes inside the source packet. Empty
  // for short-header packets.
  [[nodiscard]] std::span<const uint8_t> scid_bytes() const noexcept {
    return {vc_.scid, vc_.scidlen};
  }

  // Underlying ngtcp2 struct, for callers that need to hand it back to a
  // ngtcp2 C function unchanged.
  [[nodiscard]] const ngtcp2_version_cid& value() const noexcept {
    return vc_;
  }

  // Fill an `ngtcp2_cid` from a locally-issued `quic_connection_id`. The
  // result is exactly `cid_length` bytes wide.
  static void to_ngtcp2_cid(quic_connection_id cid, ngtcp2_cid& out) noexcept {
    out.datalen = cid_length;
    const auto v = static_cast<uint64_t>(cid);
    std::memcpy(out.data, &v, cid_length);
  }

  // Reconstitute a `quic_connection_id` from an `ngtcp2_cid` holding one of
  // our locally-issued CIDs. The first `cid_length` bytes are interpreted
  // as the underlying integer; the source must hold at least that many.
  static quic_connection_id from_ngtcp2_cid(const ngtcp2_cid& in) noexcept {
    assert(in.datalen >= cid_length);
    uint64_t v{};
    std::memcpy(&v, in.data, cid_length);
    return static_cast<quic_connection_id>(v);
  }

private:
  ngtcp2_version_cid vc_{};
};

#pragma endregion

}}} // namespace corvid::proto::quic
