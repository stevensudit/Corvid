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
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "../enums/bool_enums.h"
#include "../enums/bitmask_enum.h"
#include "base-64.h"
#include "endian.h"
#include "sha-1.h"
#include "http_head_codec.h"
#include "http_transaction.h"

namespace corvid { inline namespace proto {

using namespace bool_enums;

// WebSocket frame header byte encoding (RFC 6455 section 5.2).
//
// This isn't given a proper name in the spec, but the low half is referred to
// as the opcode, while the only allowed bit in the high half is FIN. The MASK
// flag isn't here: it's the high bit of the length byte that follows.
enum class ws_frame_control : uint8_t {
  continuation = 0x00,
  text = 0x01,
  binary = 0x02,
  rsv4 = 0x04,
  // 0x03-0x07: reserved non-control frames
  close = 0x08,
  ping = 0x09,
  pong = 0x0A,
  control = 0x08, // mask for control frames
  // 0x0B-0x0F: reserved control frames
  opcode = 0x0FU, // mask for opcode nibble
  rsv3 = 0x10,
  rsv2 = 0x20,
  rsv1 = 0x40,
  rsvd = 0x74,
  fin = 0x80,
};

}} // namespace corvid::proto

template<>
constexpr inline auto
    corvid::enums::registry::enum_spec_v<corvid::proto::ws_frame_control> =
        corvid::enums::bitmask::make_bitmask_enum_spec<
            corvid::proto::ws_frame_control,
            "fin, rsv1, rsv2, rsv3, close, rsv4, binary, text">();

namespace corvid { inline namespace proto {

// Wire-format WebSocket frame header. This is similar to `sockaddr_storage`,
// in that it is a fixed-size structure capable of holding the largest
// variable-length value.
//
// Memory layout at the front matches the on-wire representation:
//   `frame_control`    -- byte 0: FIN|RSV1-3|opcode
//   `payload_length`   -- byte 1: MASK(1)|len7(7)
//   `variable_section` -- bytes 2..N (at most 12): optional 2- or 8-byte
//                         extended length, optional 4-byte mask key
struct ws_frame_header {
  ws_frame_control frame_control{}; // byte 0 on the wire
  uint8_t payload_size_flags{};     // byte 1 on the wire: MASK(1)|len7(7)
  uint8_t variable_section[12]{};   // bytes 2-13; only `length-2` bytes valid
};

// Lightweight, non-owning wrapper around `ws_frame_header` for parsing and
// updating fields, along with related utilities. Used as `ws_frame_lens` or
// `ws_frame_view`.
template<access ACCESS = access::as_mutable>
class ws_frame_wrapper {
public:
  static constexpr bool mutable_v = (ACCESS == access::as_mutable);
  using header_t =
      std::conditional_t<mutable_v, ws_frame_header, const ws_frame_header>;
  using char_ptr_t = std::conditional_t<mutable_v, char*, const char*>;

  ws_frame_wrapper() = default;

  // Const-safe copy constructor.
  template<access OTHER_ACCESS>
  ws_frame_wrapper(const ws_frame_wrapper<OTHER_ACCESS>& other) noexcept
  requires(ACCESS == OTHER_ACCESS ||
              (ACCESS == access::as_const &&
                  OTHER_ACCESS == access::as_mutable))
      : header_{other.header_}, header_length_{other.header_length_},
        payload_length_{other.payload_length_}, mask_{other.mask_} {}

  // Construct over `header`, initializing with length of `header` or the full
  // buffer it's at the front of. Use `is_complete` before `parse`.
  explicit ws_frame_wrapper(header_t& header, size_t header_length = 0)
      : header_{&header}, header_length_{header_length} {}

  // Construct over `frame`, initializing with length of `frame`. Use
  // `is_complete` before `parse`.
  explicit ws_frame_wrapper(char_ptr_t frame, size_t header_length = 0)
      : header_{reinterpret_cast<header_t*>(frame)},
        header_length_{header_length} {}

  // Construct over `frame`. Use `is_complete` before `parse`.
  explicit ws_frame_wrapper(std::string& frame)
      : header_{reinterpret_cast<header_t*>(frame.data())},
        header_length_{frame.size()} {}

  // Construct over `frame`. Use `is_complete` before `parse`.
  explicit ws_frame_wrapper(std::string_view frame) noexcept
  requires(ACCESS == access::as_const)
      : header_{reinterpret_cast<header_t*>(frame.data())},
        header_length_{frame.size()} {}

  // Calculate the total number of bytes required for a header that encodes
  // `payload_length`. Suitable for sizing a buffer before `build`ing a header
  // and payload in it.
  static size_t
  header_length_for(size_t payload_len, bool is_mask = false) noexcept {
    size_t mask_len = is_mask ? 4 : 0;
    if (payload_len < 126) return 2 + mask_len;
    if (payload_len <= 0xFFFF) return 4 + mask_len;
    return 10 + mask_len;
  }

  // Extract the payload size flags byte, excluding the `MASK` bit. While it is
  // used to determine the payload length, it is generally not the actual
  // value: use `payload_length` for that.
  [[nodiscard]] uint8_t payload_size_flags() const noexcept {
    return header_->payload_size_flags & uint8_t{0x7F};
  }

  // Frame control byte, the first byte of the header.
  [[nodiscard]] const ws_frame_control& frame_control() const noexcept {
    return header_->frame_control;
  }

  [[nodiscard]] ws_frame_control& frame_control() noexcept
  requires mutable_v
  {
    return header_->frame_control;
  }

  // Whether this is the final frame.
  [[nodiscard]] bool is_final() const noexcept {
    return bitmask::has(header_->frame_control, ws_frame_control::fin);
  }

  // Opcode, the low nibble of the frame control byte.
  [[nodiscard]] ws_frame_control opcode() const noexcept {
    return header_->frame_control & ws_frame_control::opcode;
  }

  // Variable section.
  [[nodiscard]] const uint8_t* variable_section() const noexcept {
    return header_->variable_section;
  }

  [[nodiscard]] uint8_t* variable_section() noexcept
  requires mutable_v
  {
    return header_->variable_section;
  }

  // Whether the variable section has a mask. Note that there's a distinction
  // between having a mask of 0 and not having a mask, per RFC 6455
  // section 5.2.
  [[nodiscard]] bool is_masked() const noexcept {
    return (header_->payload_size_flags & 0x80U) != 0;
  }

  // Mask key, or 0 if `!is_masked()`.
  [[nodiscard]] uint32_t mask_key() const noexcept { return mask_; }

  // Determine whether the header is complete. Interprets `header_length` as
  // `buffer_length` and probes the start of the buffer up to that point. In
  // the process, sets `header_length` correctly. If the header is complete
  // then it can be parsed. If it's incomplete, then `header_length` is the
  // estimated number of bytes needed to complete it.
  [[nodiscard]] bool is_complete() noexcept {
    const size_t buffer_length = header_length_;
    if (buffer_length >= 14) return true; // max header size is 14 bytes

    // Must have at least the frame control byte and first length byte.
    header_length_ = 2;
    if (buffer_length < header_length_) return false;

    // Mask takes 4 more bytes after the length bytes.
    if (is_masked()) header_length_ += 4;
    if (buffer_length < header_length_) return false;

    // If `lb` == 126, need 2 more bytes for extended length;
    // if 127, need 8 more.
    const auto lb = payload_size_flags();
    if (lb == 126)
      header_length_ += 2;
    else if (lb == 127)
      header_length_ += 8;

    if (buffer_length < header_length_) return false;
    return true;
  }

  // Extract the payload length, header length and mask (if any), making them
  // available as properties. Does not depend upon `header_length` being set in
  // advance, but does require that `is_complete` would have returned true.
  [[nodiscard]] bool parse() noexcept {
    assert(is_complete());
    const auto lb = payload_size_flags();
    const auto* vs = header_->variable_section;
    header_length_ = 2;

    // Note: These are casting `memcpy` operations, to avoid strict-aliasing
    // issues. They don't actually generate a call to a function.

    // Decode length.
    if (lb <= 125)
      payload_length_ = lb;
    else if (lb == 126) {
      uint16_t v{};
      std::memcpy(&v, vs, sizeof(v));
      payload_length_ = ntoh16(v);
      header_length_ += 2;
    } else {
      uint64_t v{};
      std::memcpy(&v, vs, sizeof(v));
      payload_length_ = ntoh64(v);
      header_length_ += 8;
    }

    // Decode mask.
    mask_ = 0;
    if (is_masked()) {
      std::memcpy(&mask_, vs + header_length_ - 2, sizeof(mask_));
      mask_ = ntoh32(mask_);
      header_length_ += 4;
    }

    return true;
  }

  // Header buffer.
  [[nodiscard]] const ws_frame_header& header() const noexcept {
    return *header_;
  }

  // Length of header, including variable section.
  [[nodiscard]] size_t header_length() const noexcept {
    return header_length_;
  }

  // Length of payload, according to header.
  [[nodiscard]] size_t payload_length() const noexcept {
    return payload_length_;
  }

  // Length of entire frame.
  [[nodiscard]] size_t total_length() const noexcept {
    return header_length_ + payload_length_;
  }

  // Header buffer as a string view.
  [[nodiscard]] std::string_view view() const noexcept {
    return {reinterpret_cast<const char*>(header_), header_length_};
  }

  // Copy header to the start of the provided buffer, which must be of at least
  // `header_length` bytes.
  [[nodiscard]] bool copy_to(char* header) const {
    if (header_length_ == 0) return false;
    std::memcpy(header, header_, header_length_);
    return true;
  }

  // Copy `src` to `dst`, applying the mask key from the header. Uses `memcpy`
  // when it's effectively zero. Works correctly even if `dst` is `src.data()`
  // for in-place masking. `dst` must point to a buffer of at least
  // `src.size()` bytes. This operation is useful on both the client and server
  // side.
  [[nodiscard]] bool
  mask_payload_copy(char* dst, std::string_view src) noexcept {
    auto* p = reinterpret_cast<uint8_t*>(dst);
    const auto* s = reinterpret_cast<const uint8_t*>(src.data());
    size_t n = src.size();

    // Whether there wasn't a mask key or it was zero, there's no XORing to
    // do, so just copy if necessary.
    const auto key = mask_key();
    if (!key) {
      if (p != s) std::memcpy(p, s, n);
      return true;
    }

    // Duplicate key into 64 bits, for efficiency.
    const uint32_t key32 = ntoh32(key);
    const uint64_t key64 =
        static_cast<uint64_t>(key32) | (static_cast<uint64_t>(key32) << 32);

    // Godbolt confirms that Clang does an amazing job with this. For the main
    // loop, it XORs 256 bits at a time.
    while (n >= sizeof(uint64_t)) {
      uint64_t chunk{};
      std::memcpy(&chunk, s, sizeof(chunk));
      chunk ^= key64;
      std::memcpy(p, &chunk, sizeof(chunk));
      p += sizeof(uint64_t);
      s += sizeof(uint64_t);
      n -= sizeof(uint64_t);
    }

    // Handle the stragglers with a bytewise loop.
    if (n != 0) {
      uint8_t mask[sizeof(uint64_t)];
      std::memcpy(mask, &key64, sizeof(mask));
      for (size_t i = 0; i < n; ++i) p[i] = s[i] ^ mask[i];
    }
    return true;
  }

  // Copy `payload` into `frame`, starting right after the header and applying
  // the mask in the same pass. This operation is useful on the client side.
  [[nodiscard]] bool
  mask_payload_copy(std::string& frame, std::string_view payload) noexcept {
    assert(reinterpret_cast<const char*>(&header()) == frame.data());
    if (frame.size() < total_length()) return false;
    if (payload.size() != payload_length()) return false;
    return mask_payload_copy(frame.data() + header_length(), payload);
  }

  // Mask/unmask the payload bytes already in `frame`. This instance should
  // point into the front of that frame.
  [[nodiscard]] bool mask_payload(std::string& frame) noexcept {
    if (!mask_key()) return true;
    if (frame.size() < total_length()) return false;
    const std::string_view payload{frame.data() + header_length(),
        payload_length()};
    return mask_payload_copy(frame, payload);
  }

  // Build header into `header`and return wrapper for it.
  static ws_frame_wrapper<access::as_mutable> build(ws_frame_header& header,
      ws_frame_control frame_control, size_t payload_len,
      std::optional<uint32_t> mask) noexcept {
    ws_frame_wrapper<access::as_mutable> lens{header};
    lens.frame_control() = frame_control;
    uint8_t mask_bit = mask ? uint8_t{0x80} : uint8_t{0};
    auto vs = lens.variable_section();
    lens.payload_length_ = payload_len;

    // Encode length.
    if (payload_len < 126) {
      header.payload_size_flags = mask_bit | static_cast<uint8_t>(payload_len);
      lens.header_length_ = 2;
    } else if (payload_len <= 0xFFFF) {
      header.payload_size_flags = mask_bit | 126;
      auto v = static_cast<uint16_t>(payload_len);
      v = hton16(v);
      std::memcpy(vs, &v, sizeof(v));
      lens.header_length_ = 4;
    } else {
      header.payload_size_flags = mask_bit | 127;
      auto v = static_cast<uint64_t>(payload_len);
      v = hton64(v);
      std::memcpy(vs, &v, sizeof(v));
      lens.header_length_ = 10;
    }

    // Encode mask.
    if (mask) {
      lens.mask_ = *mask;
      uint32_t be_mask = hton32(lens.mask_);
      std::memcpy(vs + lens.header_length_ - 2, &be_mask, sizeof(be_mask));
      lens.header_length_ += 4;
    }
    return lens;
  }

  // Build header in-place at the provided `header` pointer, which should point
  // to the start of a buffer of at least `header_length_for(payload_len,
  // mask.has_value())` bytes. Returns a wrapper for the built header.
  [[nodiscard]] static ws_frame_wrapper<access::as_mutable>
  // NOLINTNEXTLINE(readability-non-const-parameter)
  build(char* header, ws_frame_control frame_control, size_t payload_len,
      std::optional<uint32_t> mask) noexcept {
    assert(header);
    auto& header_ref = *reinterpret_cast<ws_frame_header*>(header);
    return build(header_ref, frame_control, payload_len, mask);
  }

  // Build header into the frame, leaving its `size` the header length and its
  // `capacity` the total frame length. To complete this frame, use
  // `mask_payload_copy`.
  [[nodiscard]]
  static ws_frame_wrapper<access::as_mutable>
  build(std::string& frame, ws_frame_control frame_control, size_t payload_len,
      std::optional<uint32_t> mask) {
    const size_t header_len =
        ws_frame_wrapper::header_length_for(payload_len, mask.has_value());
    frame.reserve(header_len + payload_len);
    no_zero::resize_to(frame, header_len);
    return build(frame.data(), frame_control, payload_len, mask);
  }

private:
  header_t* header_{};
  size_t header_length_{};
  size_t payload_length_{};
  uint32_t mask_{};
};

// Read-only view.
using ws_frame_view = ws_frame_wrapper<access::as_const>;

// Mutable lens.
using ws_frame_lens = ws_frame_wrapper<access::as_mutable>;

// Stateless WebSocket frame codec. All members are static.
struct ws_frame_codec {
  // Parse the frame header at the start of `data`. Returns `nullopt` if
  // more bytes are needed for the header. On success, the returned
  // `ws_frame_view` has `header_length`, `payload_length`, and `total_length`
  // filled. The caller must then read `payload_length` more bytes to get the
  // whole frame.
  [[nodiscard]] static std::optional<ws_frame_view> parse_header(
      std::string_view data) noexcept {
    ws_frame_view hdr{data.data(), data.size()};
    if (!hdr.is_complete() || !hdr.parse()) return std::nullopt;
    return hdr;
  }

  // Serialize a complete WebSocket frame into a new string. `frame_control`
  // carries both the FIN flag and the opcode nibble. If `mask` is present,
  // the MASK bit is set in the length byte and the payload is masked (required
  // for client -> server).
  [[nodiscard]] static std::string
  serialize_frame(ws_frame_control frame_control, std::string_view payload,
      std::optional<uint32_t> mask = std::nullopt) {
    std::string frame;
    auto hdr =
        ws_frame_lens::build(frame, frame_control, payload.size(), mask);

    // Expand to full frame size, then copy and optionally mask the payload.
    no_zero::resize_to(frame, hdr.total_length());
    if (!hdr.mask_payload_copy(frame, payload)) frame.clear();
    return frame;
  }

  // Compute the `Sec-WebSocket-Accept` value. Returns an empty string if
  // `client_key` is empty or malformed.
  [[nodiscard]] static std::string compute_accept_key(
      std::string_view client_key) {
    if (client_key.empty()) return {};
    if (base_64::decode(client_key).size() != 16) return {};
    const std::string input = std::string{client_key} + std::string{ws_guid};
    return encode_digest(sha_1::digest(input));
  }

  // Magic GUID from RFC.
  static constexpr std::string_view ws_guid =
      "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

private:
  // Convert the 20-byte SHA-1 digest to bytes before Base64-encoding it.
  [[nodiscard]] static std::string encode_digest(const sha_1::digest_t& h) {
    const auto raw = sha_1::bytes(h);
    return base_64::encode(std::span<const uint8_t>{raw});
  }
};

// Callback-driven WebSocket message pump.
//
// This is the state machine capable of running either the client or server
// side of a WebSocket connection.
//
// Attach to a connection after the upgrade handshake completes, injecting a
// `send_fn` to handle outbound frames.
//
// Incoming frames are fed via `feed(recv_buffer_view&)`, which reassembles
// messages from fragments, and fires `on_message` / `on_close`.
//
// Send outbound messages via `send_text`, `send_binary`, `send_close`, and
// `send_pong`.
class http_websocket {
public:
  // Notification with the payload of the message, and its opcode.
  //
  // When `deliver_fragments` is false (default), this fires once per complete
  // message. The opcode argument is `text` or `binary`; the `fin` bit is
  // never set.
  //
  // When `deliver_fragments` is true, this fires once per arriving frame.
  // Non-final fragments receive the data opcode (`text` or `binary` -- never
  // `continuation`; the initial opcode is propagated for continuations). The
  // final fragment receives `opcode | ws_frame_control::fin` so the handler
  // can detect it by testing `bitmask::has(op, ws_frame_control::fin)`.
  using message_fn =
      std::function<bool(http_websocket&, std::string&&, ws_frame_control)>;

  // Notification of the receipt of a close frame, with the status code and
  // optional reason. After this returns, a close frame is returned
  // automatically, so this is the last chance to send out data or do other
  // cleanup. Callback will not fire if the connection is closed uncleanly.
  using close_fn =
      std::function<void(http_websocket&, uint16_t, std::string_view)>;

  // Sanity check limit on frame size, whether a fragment or complete.
  static constexpr size_t max_frame_size{size_t{16} * 1024 * 1024};

  // Error value for feed(std::string_view&).
  static constexpr size_t insatiable = std::numeric_limits<size_t>::max();

  // Called when a text or binary message arrives.
  message_fn on_message;

  // Called when a close frame is received.
  close_fn on_close;

  // Called when a pong is received whose payload matches the most recently
  // sent ping counter. Not called for unmatched or unsolicited pongs.
  std::function<bool(http_websocket&)> on_pong;

  // When true, `on_message` is fired once per arriving frame fragment rather
  // than once per fully assembled message. Non-final fragments receive the
  // data opcode without the `fin` bit; the final fragment receives
  // `opcode | ws_frame_control::fin`. Defaults to false.
  bool deliver_fragments{false};

  // Construct with a `send_fn`, in either client or server mode.
  explicit http_websocket(http_transaction::send_fn send,
      connection_role role = connection_role::server)
      : send_{std::move(send)}, is_server_{role == connection_role::server} {}

  // Feed raw received bytes from `handle_data`. Accumulates fragmented
  // messages across frames in `message_` and fires callbacks, while handling
  // control frames. On success, consumes frames from the front of `view`,
  // updating it. Returns false on a protocol error (bad frame_control,
  // oversized frame, etc.); the caller should treat false as a cue to close
  // the connection.
  [[nodiscard]] bool feed(recv_buffer_view& view) {
    // Loop until we run out of frame fragments.
    while (true) {
      std::string_view data = view.active_view();
      size_t needed = feed(data);
      view.update_active_view(data);

      if (needed == insatiable) return false;
      // `needed` is the total bytes required for the active header or frame
      // to fit after compaction. If it exceeds the current capacity, request
      // growth before the view destructs and `resume_receive` compacts.
      if (needed > view.buffer_capacity()) view.expand_to(needed);
      return true;
    }
  }

  // Accumulates fragmented messages across frames in `message_` and fires
  // callbacks, while handling control frames. On success, consumes frames from
  // the front of `data`, updating it. Once it's done, `data` will have had all
  // complete frames removed from the front. Returns the total bytes required
  // for the next incomplete header or frame to fit in the receive buffer after
  // compaction, or `insatiable` on a protocol error.
  [[nodiscard]] size_t feed(std::string_view& data) {
    // Loop over all frames in `data`.
    while (true) {
      if (data.empty()) break;

      // Try to parse the header. If we can't, then return a lowball estimate
      // and wait for more.
      ws_frame_view hdr{data.data(), data.size()};
      if (!hdr.is_complete()) return hdr.total_length();
      if (!hdr.parse()) return sizeof(ws_frame_header);

      // Now that we know the entire frame size, demand that exact amount.
      const size_t total = hdr.total_length();
      if (data.size() < total) {
        if (total > max_frame_size) return insatiable;
        return total;
      }

      // Extract and process payload of frame.
      auto payload = data.substr(hdr.header_length(), hdr.payload_length());
      bool processed = handle_payload(hdr, payload);
      if (!processed) return insatiable;

      data.remove_prefix(total);
    }
    return 0;
  }

  // Send a text message frame (FIN set, text opcode).
  [[nodiscard]] bool send_text(std::string_view payload) {
    return send_frame(ws_frame_control::fin | ws_frame_control::text, payload);
  }

  // Send a binary message frame (FIN set, binary opcode).
  [[nodiscard]] bool send_binary(std::string_view payload) {
    return send_frame(ws_frame_control::fin | ws_frame_control::binary,
        payload);
  }

  // Send a close frame, updating state.
  [[nodiscard]] bool
  send_close(uint16_t code = 1000, std::string_view reason = {}) {
    // Payload is optional.
    std::string payload;
    if (code != 0 || !reason.empty()) {
      payload.reserve(2 + reason.size());
      payload.push_back(static_cast<char>(code >> 8));
      payload.push_back(static_cast<char>(code & 0xFF));
      payload.append(reason);
    }

    if (!send_frame(ws_frame_control::fin | ws_frame_control::close, payload))
      return false;

    // We only send close once, and then we don't send anything but pongs.
    // Moreover, we stop listening for non-control frames.
    sent_close_ = true;

    // If this is our response to the other side's close frame, the handshake
    // is complete. `dispatch_close` will set `received_close_` on return,
    // making `close_pending` true, which signals the transaction to close.
    return true;
  }

  // True while either side has initiated the close handshake.
  [[nodiscard]] bool is_closing() const noexcept {
    return sent_close_ || received_close_;
  }

  // True once both sides have exchanged close frames. The transaction should
  // shut down the connection gracefully when this becomes true. Note that this
  // state is simulated by `set_close_pending`, to gracefully handle errors.
  [[nodiscard]] bool is_close_pending() const noexcept {
    return sent_close_ && received_close_;
  }

  // Pretend that we've sent and received close frames, thus triggering
  // `close_pending`.
  [[nodiscard]] bool set_close_pending() noexcept {
    sent_close_ = true;
    received_close_ = true;
    return false;
  }

  // True if `send_ping` has been called and the matching pong has not yet
  // arrived.
  [[nodiscard]] bool ping_pending() const noexcept {
    return pending_ping_.has_value();
  }

  // Send a ping frame (FIN set, ping opcode). The payload is a 4-byte
  // big-endian representation of an auto-incrementing counter. The counter
  // value is stored in `pending_ping_` for comparison with incoming pongs.
  // Calling `send_ping` while a ping is already outstanding replaces the
  // stored counter; only the most recent ping is tracked (RFC 6455 allows
  // the peer to reply to only the most recent ping).
  [[nodiscard]] bool send_ping() {
    ++ping_seq_;
    pending_ping_ = ping_seq_;
    const uint32_t be_ping_seq = hton32(ping_seq_);
    char payload[sizeof(be_ping_seq)];
    std::memcpy(payload, &be_ping_seq, sizeof(be_ping_seq));
    return send_frame(ws_frame_control::fin | ws_frame_control::ping,
        {payload, sizeof(payload)});
  }

  // Send a pong frame (FIN set, pong opcode). Normally sent automatically in
  // response to a ping; also usable for unsolicited keepalive pongs.
  [[nodiscard]] bool send_pong(std::string_view payload = {}) {
    // Always allow sending a pong, even after we've sent a close frame.
    scoped_value guard{sent_close_, false};
    return send_frame(ws_frame_control::fin | ws_frame_control::pong, payload);
  }

  // Send a frame. `frame_control` encodes both the FIN flag and the opcode
  // nibble. Masking is applied automatically for client-side connections. To
  // use this for fragmented messages: omit `ws_frame_control::fin` on all but
  // the last fragment, use `ws_frame_control::continuation` on all but the
  // first.
  [[nodiscard]] bool
  send_frame(ws_frame_control frame_control, std::string_view payload) {
    if (sent_close_) return false;
    std::optional<uint32_t> mask;
    if (!is_server_) mask.emplace(rd_());
    std::string frame =
        ws_frame_codec::serialize_frame(frame_control, payload, mask);
    return send_frame(std::move(frame));
  }

  // Send a serialized frame.
  [[nodiscard]] bool send_frame(std::string&& frame) {
    if (sent_close_ || !send_) return false;
    return send_(std::move(frame));
  }

  // Generate a WebSocket upgrade request for the given `path`, returning the
  // request head and the `Sec-WebSocket-Accept` value that the server should
  // respond with. You may need to add "Host" or other headers.
  [[nodiscard]] request_head
  generate_upgrade_request(std::string_view path, std::string& accept_key) {
    const auto client_key = generate_client_key();
    accept_key = ws_frame_codec::compute_accept_key(client_key);

    request_head req;
    req.method = http_method::GET;
    req.version = http_version::http_1_1;
    req.target = path;
    req.options.upgrade = upgrade_value::websocket;
    (void)req.headers.add_raw("Connection", "Upgrade");
    (void)req.headers.add_raw("Sec-Websocket-Version", "13");
    (void)req.headers.add_raw("Sec-Websocket-Key", client_key);

    return req;
  }

private:
  // Signal an immediate RST by invoking the send function with an empty
  // string, then return false so callers can propagate the error.
  //
  // TODO: Consider whether some of the places where we could this should
  // instead call `send_close(1002, "Protocol violation")`, only calling here
  // if that fails.
  bool do_hangup() {
    if (send_) (void)send_(std::string{});
    return false;
  }

  std::string generate_client_key() {
    std::array<uint8_t, 16> raw_bytes;
    for (size_t i = 0; i < 4; ++i) {
      uint32_t val = rd_();
      std::memcpy(&raw_bytes[i * 4], &val, 4);
    }
    return base_64::encode(raw_bytes);
  }

  [[nodiscard]] bool dispatch_close(std::string_view payload) {
    // Ignore duplicate close frames.
    if (received_close_) return true;

    // Decode close code and reason first, regardless of other state.
    uint16_t code{};
    std::string_view reason{};
    if (payload.size() >= 2) {
      code = (static_cast<uint16_t>(static_cast<uint8_t>(payload[0])) << 8) |
             static_cast<uint8_t>(payload[1]);
      reason = {payload.data() + 2, payload.size() - 2};
    }

    // Notify user of the close. The guard above ensures this fires at most
    // once.
    if (on_close) on_close(*this, code, reason);

    // Echo the close frame back unless we already sent one.
    if (!sent_close_) {
      if (!send_close(code, reason)) return do_hangup();
    }

    // Mark close as received. If `sent_close_` is already true,
    // `close_pending` becomes true, signaling that the connection should shut
    // down.
    received_close_ = true;
    return false;
  }

  // Process the payload of a received frame. Accumulates payloads from the
  // frames of a fragmented message and dispatches it to the user as a complete
  // message once the final fragment arrives. Returns false on a protocol
  // error, true on success. Handles control frames and state transitions.
  [[nodiscard]] bool
  handle_payload(ws_frame_view& hdr, std::string_view payload) {
    assert(hdr.payload_length() == payload.size());

    // Sniff frame and reject obvious defects.
    const auto frame_control = hdr.frame_control();
    if (bitmask::has(frame_control, ws_frame_control::rsvd))
      return do_hangup();

    // Clients must send masked frames and servers must not.
    if ((is_server_ && !hdr.is_masked()) || (!is_server_ && hdr.is_masked())) {
      // We want to fail with a close frame, instead of hanging up, so pretend
      // that we received their close frame already.
      received_close_ = true;
      if (!send_close(1002, "Frame violates masking requirements"))
        return do_hangup();
      return false;
    }

    // Control frames are handled internally.
    if (bitmask::has(frame_control, ws_frame_control::control))
      return handle_control_frame(hdr, payload);

    // Once we've received a close frame, discard all further non-control
    // frames. We might be waiting for their close frame at this point.
    if (received_close_) return false;

    return handle_data_frame(hdr, payload);
  }

  // Accumulate and dispatch a data frame. Called by `handle_payload` after
  // control frames and protocol errors have been filtered out.
  [[nodiscard]] bool
  handle_data_frame(ws_frame_view& hdr, std::string_view payload) {
    const auto opcode = hdr.opcode();
    const auto is_fin = hdr.is_final();

    // We store the initial opcode of a fragmented message in
    // `fragment_opcode_`. Subsequent fragments must have the continuation
    // opcode.
    const bool in_fragment = (fragment_opcode_ != ws_frame_control{});
    if ((in_fragment && opcode != ws_frame_control::continuation) ||
        (!in_fragment && opcode == ws_frame_control::continuation))
      return do_hangup();

    // Accumulate the (unmasked) payloads into `message_`. Note that it's
    // always legal for a payload to be empty.

    // Handle initial (or only) fragment of a message.
    if (!in_fragment) {
      // The initial message but be text or binary. If it's both or neither,
      // that's a fatal protocol error.
      if (bitmask::has(opcode, ws_frame_control::text) ==
          bitmask::has(opcode, ws_frame_control::binary))
        return do_hangup();

      // Save opcode so continuations can be validated and the assembled
      // message dispatched with the correct type.
      if (!is_fin) fragment_opcode_ = opcode;
      message_.clear();
    }

    // Sanity check on size, applied to each fragment and the combined message.
    if (message_.size() + payload.size() > max_frame_size) return do_hangup();

    // Append the unmasked payload to `message_`.
    if (!payload.empty()) {
      const size_t old_size = message_.size();
      no_zero::resize_to(message_, old_size + payload.size());
      if (!hdr.mask_payload_copy(message_.data() + old_size, payload))
        return do_hangup();
    }

    // Determine the data opcode for this frame. Continuation frames carry
    // `ws_frame_control::continuation`, so use the saved initial opcode
    // instead.
    ws_frame_control data_opcode = in_fragment ? fragment_opcode_ : opcode;

    // When delivering fragments, mark FIN.
    if (is_fin && deliver_fragments) data_opcode |= ws_frame_control::fin;

    // When not, delay delivery until the final fragment.
    if (!is_fin && !deliver_fragments) return true;

    // If this is the final fragment, reset `fragment_opcode_` to indicate that
    // we're not in a fragmented message anymore.
    if (is_fin) fragment_opcode_ = {};

    // Dispatch the payload. This moves `message_` and clears it out.
    return dispatch_message(std::move(message_), data_opcode);
  }

  [[nodiscard]] bool
  handle_control_frame(ws_frame_view& hdr, std::string_view payload) {
    const auto opcode = hdr.opcode();
    if (!hdr.is_final() || payload.size() > 125) return do_hangup();

    // Unmask the payload before inspection.
    if (hdr.is_masked() && !payload.empty()) {
      no_zero::resize_to(control_frame_payload_, payload.size());
      (void)hdr.mask_payload_copy(control_frame_payload_.data(), payload);
      payload = control_frame_payload_;
    }

    // Handle the control frame.
    if (opcode == ws_frame_control::close) {
      if (payload.size() == 1) return do_hangup();
      return dispatch_close(payload);
    }
    if (opcode == ws_frame_control::ping) { return send_pong(payload); }
    if (opcode == ws_frame_control::pong) {
      // If not the pong we're looking for, just ignore it.
      if (!pending_ping_ || payload.size() != 4) return true;
      uint32_t received;
      std::memcpy(&received, payload.data(), sizeof(received));
      received = ntoh32(received);
      if (received == *pending_ping_) {
        pending_ping_.reset();
        if (on_pong) return on_pong(*this);
      }
      return true;
    }
    // Unknown control opcode.
    return do_hangup();
  }

  [[nodiscard]] bool
  dispatch_message(std::string&& payload, ws_frame_control opcode_bits) {
    bool success = true;
    if (on_message)
      success = on_message(*this, std::move(payload), opcode_bits);

    // Clear in case it wasn't moved out.
    payload.clear();
    return success;
  }

  // Callback for sending frames through provided mechanism.
  http_transaction::send_fn send_;

  // Whether acting as server or client.
  const bool is_server_{};

  // Opcode of the initial frame in the current fragmented message, or 0 if not
  // in a fragmented message.
  ws_frame_control fragment_opcode_{};

  // Accumulates payload of fragmented messages until the final fragment
  // arrives and the message can be dispatched.
  std::string message_;

  // Buffer for unmasking control frame payloads.
  std::string control_frame_payload_;

  // Whether we've received a close frame, which means we should stop
  // listening.
  bool received_close_{false};

  // Whether we've sent a close frame, which means we should stop sending.
  bool sent_close_{false};

  // Auto-incrementing counter for outgoing pings. Each call to `send_ping`
  // increments this and stores the new value in `pending_ping_`.
  uint32_t ping_seq_{};

  // The counter value of the most recently sent ping, if a pong is still
  // expected. Reset to `nullopt` when a matching pong arrives.
  std::optional<uint32_t> pending_ping_;

  // Random generator for client keys and masks.
  // The RFC does not allow predictable client keys or masks, so we MUST use a
  // non-deterministic generator.
  std::random_device rd_;
};
}} // namespace corvid::proto
