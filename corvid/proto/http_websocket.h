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
#include <memory>
#include <optional>
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
enum class ws_opcode : uint8_t {
  continuation = 0x00,
  text = 0x01,
  binary = 0x02,
  // 0x03-0x07: reserved non-control frames
  close = 0x08,
  ping = 0x09,
  pong = 0x0A,
  // 0x0B-0x0F: reserved control frames
  rsv3 = 0x10,
  rsv2 = 0x20,
  rsv1 = 0x40,
  fin = 0x80,
};

}} // namespace corvid::proto

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::ws_opcode> =
    corvid::enums::bitmask::make_bitmask_enum_spec<corvid::proto::ws_opcode,
        "fin, rsv1, rsv2, rsv3, close, rsv4, binary, text">();

namespace corvid { inline namespace proto {

// Wire-format WebSocket frame header. This is similar to `sockaddr_storage`,
// in that it is a fixed-size structure capable of holding the largest possible
// header.
//
// Memory layout at the front matches the on-wire representation:
//   `opcode`           -- byte 0: FIN|RSV1-3|opcode (cast directly from wire)
//   `variable_section` -- bytes 1..N (at most 13): MASK(1)|len7(7),
//                         optional 2- or 8-byte extended length, optional
//                         4-byte mask
struct ws_frame_header {
  ws_opcode opcode{};             // byte 0 on the wire
  uint8_t variable_section[13]{}; // bytes 1-13; only `length-1` bytes valid
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

  template<access>
  friend class ws_frame_wrapper;

  ws_frame_wrapper() = default;

  // Const-safe copy constructor.
  template<access OTHER_ACCESS>
  ws_frame_wrapper(const ws_frame_wrapper<OTHER_ACCESS>& other) noexcept
  requires(ACCESS == OTHER_ACCESS ||
              (ACCESS == access::as_const &&
                  OTHER_ACCESS == access::as_mutable))
      : header_{other.header_}, header_length_{other.header_length_},
        payload_length_{other.payload_length_}, mask_{other.mask_} {}

  // Construct over `header`. Initialize with length of `header` or the full
  // buffer it's at the front of, then use `is_complete` before `parse`.
  explicit ws_frame_wrapper(header_t& header, size_t header_length = 0)
      : header_{&header}, header_length_{header_length} {}

  // Construct over `frame`. Initialize with length of `frame`, then use
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
  // `payload_length`, including the opcode, initial length byte, and any
  // extended length bytes, as well as the mask key. Suitable for sizing a
  // buffer before `build`ing a header and payload in it.
  static size_t
  header_length_for(size_t payload_len, size_t is_mask = false) noexcept {
    size_t mask_len = is_mask ? 4 : 0;
    if (payload_len < 126) return 2 + mask_len;
    if (payload_len <= 0xFFFF) return 4 + mask_len;
    return 10 + mask_len;
  }

  // Extract the length byte, excluding the `is_final` bit. This is generally
  // not the length; use `payload_length` for that.
  [[nodiscard]] uint8_t length_byte() const noexcept {
    return header_->variable_section[0] & uint8_t{0x7F};
  }

  // Opcode.
  [[nodiscard]] const ws_opcode& opcode() const noexcept {
    return header_->opcode;
  }

  [[nodiscard]] ws_opcode& opcode() noexcept
  requires mutable_v
  {
    return header_->opcode;
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
    return (header_->variable_section[0] & 0x80U) != 0;
  }

  // Mask key, or 0 if `!is_masked()`.
  [[nodiscard]] uint32_t mask_key() const noexcept { return mask_; }

  // Whether this is the final frame.
  [[nodiscard]] bool is_final() const noexcept {
    return bitmask::has(header_->opcode, ws_opcode::fin);
  }

  // Determine whether the header is complete. Interprets `header_length` as
  // `buffer_length` and probes the start of the header buffer up to that
  // point. In the process, sets `header_length`. If the header is complete
  // then it can be parsed.
  [[nodiscard]] bool is_complete() noexcept {
    const size_t buffer_length = header_length_;
    if (buffer_length >= 14) return true; // max header size is 14 bytes

    // Must have at least opcode and first length byte.
    header_length_ = 2;
    if (buffer_length < header_length_) return false;

    // Mask takes 4 more bytes after the length bytes.
    if (is_masked()) header_length_ += 4;
    if (buffer_length < header_length_) return false;

    // If `lb` == 126, need 2 more bytes for extended length;
    // if 127, need 8 more.
    const auto lb = length_byte();
    if (lb == 126)
      header_length_ += 2;
    else if (lb == 127)
      header_length_ += 8;

    if (buffer_length < header_length_) return false;
    return true;
  }

  // Extract the payload length, header length and mask (if any) from the
  // variable section. `is_complete` must be true before calling this (but
  // it doesn't need to be called).
  [[nodiscard]] bool parse() noexcept {
    const auto lb = length_byte();
    const auto* vs = header_->variable_section;
    header_length_ = 2;

    if (lb <= 125) {
      payload_length_ = lb;
    } else if (lb == 126) {
      uint16_t v{};
      std::memcpy(&v, vs + 1, sizeof(v));
      payload_length_ = ntoh16(v);
      header_length_ += 2;
    } else {
      uint64_t v{};
      std::memcpy(&v, vs + 1, sizeof(v));
      payload_length_ = ntoh64(v);
      header_length_ += 8;
    }

    mask_ = 0;
    if (is_masked()) {
      std::memcpy(&mask_, vs + header_length_ - 1, sizeof(mask_));
      mask_ = ntoh32(mask_);
      header_length_ += 4;
    }

    return true;
  }

  // Pointer to start of header.
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

  [[nodiscard]] size_t total_length() const noexcept {
    return header_length_ + payload_length_;
  }

  // Encoded header bytes.
  [[nodiscard]] std::string_view view() const noexcept {
    return {reinterpret_cast<const char*>(header_), header_length_};
  }

  // Copy header to the provided `ws_frame_header` object.
  [[nodiscard]] bool copy_to(char* header) const {
    if (header_length_ == 0) return false;
    std::memcpy(header, header_, header_length_);
    return true;
  }

  // Copy `src` to `dst`, applying the mask. Uses `memcpy` when unmasked.
  // Works correctly even if `dst` is `src.data()` for in-place masking. `dst`
  // must point to a buffer of at least `src.size()` bytes.
  [[nodiscard]] bool
  mask_payload_copy(char* dst, std::string_view src) noexcept {
    auto* p = reinterpret_cast<uint8_t*>(dst);
    const auto* s = reinterpret_cast<const uint8_t*>(src.data());
    size_t n = src.size();

    const auto key = mask_key();
    if (!key) return std::memcpy(p, s, n) || true;

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

    if (n != 0) {
      uint8_t mask[sizeof(uint64_t)];
      std::memcpy(mask, &key64, sizeof(mask));
      for (size_t i = 0; i < n; ++i) p[i] = s[i] ^ mask[i];
    }
    return true;
  }

  // Copy `payload` into `frame` (at the payload area after the header),
  // applying the mask in the same pass. Returns false if the sizes are wrong.
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
      ws_opcode opcode, size_t payload_len,
      std::optional<uint32_t> mask) noexcept {
    ws_frame_wrapper<access::as_mutable> lens{header};
    lens.opcode() = opcode;
    uint8_t mask_bit = mask ? uint8_t{0x80} : uint8_t{0};
    auto vs = lens.variable_section();
    lens.payload_length_ = payload_len;

    if (payload_len < 126) {
      vs[0] = mask_bit | static_cast<uint8_t>(payload_len);
      lens.header_length_ = 2;
    } else if (payload_len <= 0xFFFF) {
      vs[0] = static_cast<char>(mask_bit | 126);
      auto v = static_cast<uint16_t>(payload_len);
      v = hton16(v);
      std::memcpy(vs + 1, &v, sizeof(v));
      lens.header_length_ = 4;
    } else {
      vs[0] = static_cast<char>(mask_bit | 127);
      auto v = static_cast<uint64_t>(payload_len);
      v = hton64(v);
      std::memcpy(vs + 1, &v, sizeof(v));
      lens.header_length_ = 10;
    }

    if (mask) {
      uint32_t be_mask = hton32(*mask);
      std::memcpy(vs + lens.header_length_, &be_mask, sizeof(be_mask));
      lens.header_length_ += 4;
    }
    return lens;
  }

  // Build header in-place at the provided `header` pointer, which should point
  // to the start of a buffer of at least `header_length_for(payload_len,
  // mask.has_value())` bytes. Returns a wrapper for the built header.
  [[nodiscard]] static ws_frame_wrapper<access::as_mutable>
  // NOLINTNEXTLINE(readability-non-const-parameter)
  build(char* header, ws_opcode opcode, size_t payload_len,
      std::optional<uint32_t> mask) noexcept {
    assert(header);
    auto& header_ref = *reinterpret_cast<ws_frame_header*>(header);
    return build(header_ref, opcode, payload_len, mask);
  }

  // Builds header into the frame, leaving its `size` the header length and its
  // `capacity` the total frame length. To complete this frame, use
  // `mask_payload_copy`.
  [[nodiscard]]
  static ws_frame_wrapper<access::as_mutable> build(std::string& frame,
      ws_opcode opcode, size_t payload_len, std::optional<uint32_t> mask) {
    assert(!frame.empty());
    const size_t header_len =
        ws_frame_wrapper::header_length_for(payload_len, mask.has_value());
    frame.reserve(header_len + payload_len);
    no_zero::resize_to(frame, header_len);
    return build(frame.data(), opcode, payload_len, mask);
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
  // more bytes are needed. On success, the returned `ws_frame_view` has
  // `header_length`, `payload_length`, and `total_length` filled. The caller
  // must read at least `payload_length` more bytes to get the whole frame.
  [[nodiscard]] static std::optional<ws_frame_view> parse_header(
      std::string_view data) noexcept {
    ws_frame_view hdr{data.data(), data.size()};
    if (!hdr.is_complete() || !hdr.parse()) return std::nullopt;
    return hdr;
  }

  // Serialize a complete WebSocket frame into a new string. `opcode`
  // carries both the FIN flag and the opcode nibble (e.g.,
  // `ws_opcode::fin | ws_opcode::text`). If `mask` is non-null, the MASK
  // bit is set and the payload is masked (required for client -> server).
  //
  [[nodiscard]] static std::string serialize_frame(ws_opcode opcode,
      std::string_view payload, std::optional<uint32_t> mask = std::nullopt) {
    std::string frame;
    auto hdr = ws_frame_lens::build(frame, opcode, payload.size(), mask);

    // Expand to full frame size, then copy and optionally mask the payload.
    no_zero::resize_to(frame, hdr.total_length());
    if (!hdr.mask_payload_copy(frame, payload)) frame.clear();
    return frame;
  }

  // Compute the `Sec-WebSocket-Accept` value per RFC 6455 section 4.2.2:
  //   Base64(SHA-1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
  // Returns an empty string if `client_key` is empty or malformed.
  [[nodiscard]] static std::string compute_accept_key(
      std::string_view client_key) {
    if (client_key.empty()) return {};
    const std::string input = std::string{client_key} + std::string{ws_guid_};
    return encode_digest(sha_1::digest(input));
  }

private:
  static constexpr std::string_view ws_guid_ =
      "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

  // Convert the 20-byte SHA-1 digest to bytes before Base64-encoding it.
  [[nodiscard]] static std::string encode_digest(const sha_1::digest_t& h) {
    const auto raw = sha_1::bytes(h);
    return base_64::encode(std::span<const uint8_t>{raw});
  }
};
#if 1
// Callback-driven WebSocket message pump.
//
// Attach to a connection after the upgrade handshake completes. Feed raw
// receive bytes via `feed(recv_buffer_view)`, which reassembles frames,
// defragments multi-frame messages, and fires `on_message` / `on_close`.
//
// Send outbound messages via `send_text`, `send_binary`, `send_close`, and
// `send_pong`. All send methods forward frames through the injected
// `send_fn`.
//
// Designed to be orthogonal to `http_transaction` so it can be reused on
// the client side (set `is_server = false`).
class http_websocket {
public:
  using message_fn =
      std::function<void(http_websocket&, std::string_view, ws_opcode)>;
  using close_fn =
      std::function<void(http_websocket&, uint16_t, std::string_view)>;

  // Called when a complete (possibly reassembled) text or binary message
  // arrives. `opcode` is `text` or `binary` (the fragment opcode).
  message_fn on_message;

  // Called when a close frame is received. `code` is the 2-byte status
  // code (1000 = normal); `reason` is the optional UTF-8 reason string.
  close_fn on_close;

  explicit http_websocket(http_transaction::send_fn send,
      connection_role role = connection_role::server)
      : send_{std::move(send)}, is_server_{role == connection_role::server} {}

  // Feed raw received bytes. Accumulates partial frames in `recv_frame_`,
  // reassembles complete frames, defragments messages, and fires callbacks.
  // Returns false on a protocol error (bad opcode, oversized frame, etc.);
  // the caller should treat false as a cue to close the connection.
  [[nodiscard]] bool feed(recv_buffer_view& view) {
    while (true) {
      const std::string_view data = view.active_view();

      // Try to parse the frame header from the front of the view.
      auto hdr_opt = ws_frame_codec::parse_header(data);
      if (!hdr_opt) return true; // need more data

      auto& hdr = *hdr_opt;
      const size_t total = hdr.total_length();
      if (data.size() < total) {
        // Payload not yet fully arrived; expand if it won't fit.
        if (total > view.buffer_capacity()) view.expand_to(total);
        return true;
      }

      if (!do_feed_frame(view, hdr, data)) return false;
    }
  }

  // Send a text message frame (FIN set, text opcode).
  [[nodiscard]] bool send_text(std::string_view payload) {
    return do_send(ws_opcode::fin | ws_opcode::text, payload);
  }

  // Send a binary message frame (FIN set, binary opcode).
  [[nodiscard]] bool send_binary(std::string_view payload) {
    return do_send(ws_opcode::fin | ws_opcode::binary, payload);
  }

  // Send a close frame.
  [[nodiscard]] bool
  send_close(uint16_t code = 1000, std::string_view reason = {}) {
    std::string body;
    body.reserve(2 + reason.size());
    body.push_back(static_cast<char>(code >> 8));
    body.push_back(static_cast<char>(code & 0xFF));
    body.append(reason);
    return do_send(ws_opcode::fin | ws_opcode::close, body);
  }

  // Send a pong frame.
  [[nodiscard]] bool send_pong(std::string_view payload = {}) {
    return do_send(ws_opcode::fin | ws_opcode::pong, payload);
  }

private:
  // Process one complete frame (header already parsed, full payload present in
  // `data`). Validates fragmentation, accumulates payload, consumes bytes, and
  // dispatches when the message is complete.
  [[nodiscard]] bool do_feed_frame(recv_buffer_view& view, ws_frame_view& hdr,
      std::string_view data) {
    const auto opcode_bits =
        static_cast<ws_opcode>(static_cast<uint8_t>(hdr.opcode()) & 0x0FU);
    const bool is_fin = hdr.is_final();

    // A non-default `fragment_opcode_` means we are mid-message.
    const bool in_fragment = (fragment_opcode_ != ws_opcode{});

    // Validate fragmentation state per RFC 6455 section 5.4.
    if (in_fragment) {
      // Only continuation frames are valid while assembling a message.
      if (opcode_bits != ws_opcode::continuation) return false;
    } else {
      // A continuation frame is invalid when no message is in progress.
      if (opcode_bits == ws_opcode::continuation) return false;
    }

    // View of the (possibly masked) payload in the receive buffer.
    const std::string_view payload_sv{
        data.data() + hdr.header_length(), hdr.payload_length()};

    // Accumulate the (unmasked) payload into `recv_frame_`.
    if (in_fragment) {
      // Enforce a 16 MiB limit to prevent memory exhaustion.
      constexpr size_t max_assembled{size_t{16} * 1024 * 1024};
      if (recv_frame_.size() + hdr.payload_length() > max_assembled)
        return false;
      const size_t old_size = recv_frame_.size();
      no_zero::resize_to(recv_frame_, old_size + hdr.payload_length());
      if (!payload_sv.empty())
        if (!hdr.mask_payload_copy(recv_frame_.data() + old_size, payload_sv))
          return false;
    } else {
      // First (or only) frame of a message.
      no_zero::resize_to(recv_frame_, hdr.payload_length());
      if (!payload_sv.empty())
        if (!hdr.mask_payload_copy(recv_frame_.data(), payload_sv)) return false;
      // Save opcode so continuations can be validated and the assembled
      // message dispatched with the correct type.
      if (!is_fin) fragment_opcode_ = opcode_bits;
    }

    view.consume(hdr.total_length());

    if (is_fin) {
      // Dispatch the complete (possibly reassembled) message. Moving
      // `recv_frame_` clears it automatically for the next message.
      const ws_opcode dispatch_opcode =
          in_fragment ? fragment_opcode_ : opcode_bits;
      fragment_opcode_ = {};
      return dispatch_frame(hdr, dispatch_opcode, std::move(recv_frame_));
    }
    return true;
  }

  [[nodiscard]] bool dispatch_frame(const ws_frame_view& hdr,
      ws_opcode opcode_bits, std::string payload) {
    if (opcode_bits == ws_opcode::text || opcode_bits == ws_opcode::binary) {
      const bool is_fin = hdr.is_final();
      if (!in_fragment_) {
        fragment_opcode_ = opcode_bits;
        fragment_buf_ = std::move(payload);
      } else {
        fragment_buf_ += payload;
      }
      if (is_fin) {
        if (on_message) on_message(*this, fragment_buf_, fragment_opcode_);
        fragment_buf_.clear();
        in_fragment_ = false;
      } else {
        in_fragment_ = true;
      }
    } else if (opcode_bits == ws_opcode::continuation) {
      if (!in_fragment_) return false; // unexpected continuation
      fragment_buf_ += payload;
      const bool is_fin = hdr.is_final();
      if (is_fin) {
        if (on_message) on_message(*this, fragment_buf_, fragment_opcode_);
        fragment_buf_.clear();
        in_fragment_ = false;
      }
    } else if (opcode_bits == ws_opcode::ping) {
      // Auto-pong with the ping payload (RFC 6455 section 5.5.2).
      (void)send_pong(payload);
    } else if (opcode_bits == ws_opcode::close) {
      uint16_t code{1000};
      std::string_view reason{};
      if (payload.size() >= 2) {
        code = (static_cast<uint16_t>(static_cast<uint8_t>(payload[0])) << 8) |
               static_cast<uint8_t>(payload[1]);
        reason = {payload.data() + 2, payload.size() - 2};
      }
      if (on_close) on_close(*this, code, reason);
    } else {
      // Unknown/reserved opcode.
      return false;
    }
    return true;
  }

  [[nodiscard]] bool do_send(ws_opcode opcode, std::string_view payload) {
    // TODO: Do something smarter with the mask. We shouldn't hardcode it on
    // the client side.
    const std::optional<uint32_t> mask =
        is_server_ ? std::nullopt : std::optional<uint32_t>(0x12345678);
    std::string frame = ws_frame_codec::serialize_frame(opcode, payload, mask);
    if (!send_) return false;
    return send_(std::move(frame));
  }

  [[nodiscard]] bool do_send(std::string&& frame) {
    if (!send_) return false;
    return send_(std::move(frame));
  }

  http_transaction::send_fn send_;
  bool is_server_;
  std::string recv_frame_;
  std::string fragment_buf_;
  ws_opcode fragment_opcode_{};
  bool in_fragment_{};
};

// HTTP transaction that performs the WebSocket upgrade handshake and then
// delegates all subsequent data flow to an `http_websocket` instance.
//
// `handle_data` (first call):
//   Validates the upgrade request, sends a `101 Switching Protocols`
//   response, and returns `stream_claim::claim` to hold the input stream
//   permanently. On validation failure, sends `400 Bad Request` and
//   returns `stream_claim::release`.
//
// `handle_data` (subsequent calls):
//   Forwards the receive buffer to `websocket_.feed()`. Returns `claim`
//   normally; returns `release` if `feed` returns false (protocol error
//   or close frame received).
//
// `handle_drain`:
//   Returns `stream_claim::claim` unconditionally. The WebSocket output
//   stream stays alive until the close handshake completes (handled via
//   `send_close` inside `on_close`), not merely until the send queue drains.
//
// After upgrade, `http_phase` never returns to `request_line`; the pipeline
// is permanently fixed on this transaction until the connection closes.
class http_websocket_transaction final: public http_transaction {
public:
  explicit http_websocket_transaction(request_head&& req)
      : http_transaction{std::move(req)} {}

  // Access the WebSocket pump to install `on_message` / `on_close`
  // callbacks before the connection is upgraded.
  [[nodiscard]] http_websocket& websocket() noexcept { return websocket_; }

  [[nodiscard]] stream_claim handle_data(recv_buffer_view& view) override {
    if (!upgraded_) return do_upgrade(view);
    if (!websocket_.feed(view)) return stream_claim::release;
    return stream_claim::claim;
  }

  // The WebSocket output never completes via send-queue drain; return
  // `claim` unconditionally.
  [[nodiscard]] stream_claim handle_drain(send_fn& send) override {
    if (!websocket_send_) websocket_send_ = send;
    if (!pending_response_.empty()) {
      if (!send(std::move(pending_response_))) return stream_claim::release;
      pending_response_.clear();
    }
    return upgraded_ ? stream_claim::claim : stream_claim::release;
  }

  // Build a `transaction_factory` that constructs an
  // `http_websocket_transaction` for each matching request and then calls
  // `configure` on it so the caller can install `on_message` / `on_close`.
  [[nodiscard]] static transaction_factory make_factory(
      std::function<void(http_websocket_transaction&)> configure = {}) {
    return [configure = std::move(configure)](
               request_head&& req) -> std::shared_ptr<http_transaction> {
      auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
      if (configure) configure(*tx);
      return tx;
    };
  }

private:
  // Perform the RFC 6455 upgrade handshake. Called on the first
  // `handle_data` invocation.
  [[nodiscard]] stream_claim do_upgrade(recv_buffer_view& view) {
    // Validate upgrade request.
    if (request_headers.method != http_method::GET)
      return send_bad_request(view);
    if (request_headers.options.upgrade != upgrade_value::websocket)
      return send_bad_request(view);

    const auto conn_hdr = request_headers.headers.get("Connection");
    if (!conn_hdr || !conn_hdr->contains("Upgrade"))
      return send_bad_request(view);

    const auto version_hdr =
        request_headers.headers.get("Sec-Websocket-Version");
    if (!version_hdr || *version_hdr != "13") return send_bad_request(view);

    const auto key_hdr = request_headers.headers.get("Sec-Websocket-Key");
    if (!key_hdr || key_hdr->empty()) return send_bad_request(view);

    const std::string accept = ws_frame_codec::compute_accept_key(*key_hdr);
    if (accept.empty()) return send_bad_request(view);

    // Build 101 Switching Protocols response.
    response_head resp;
    resp.version = request_headers.version;
    resp.status_code = http_status_code::SWITCHING_PROTOCOLS;
    resp.reason = "Switching Protocols";
    if (!resp.headers.add_raw("Upgrade", "websocket"))
      return stream_claim::release;
    if (!resp.headers.add_raw("Connection", "Upgrade"))
      return stream_claim::release;
    if (!resp.headers.add_raw("Sec-Websocket-Accept", accept))
      return stream_claim::release;
    // Consume any leftover data already in the buffer (upgrade response
    // has no HTTP body; any subsequent bytes are WebSocket frames, handled
    // on the next `handle_data` call).
    {
      const std::string_view remaining = view.active_view();
      view.consume(remaining.size());
    }
    pending_response_ = resp.serialize();
    upgraded_ = true;
    return stream_claim::claim;
  }

  [[nodiscard]] stream_claim send_bad_request(recv_buffer_view& view) {
    view.consume(view.active_view().size());
    pending_response_ = response_head::make_error_response(
        after_response::close, request_headers.version,
        http_status_code::BAD_REQUEST, "Bad Request");
    return stream_claim::release;
  }

  http_transaction::send_fn websocket_send_;
  http_websocket websocket_{[this](std::string&& frame) {
    if (!websocket_send_) return false;
    return websocket_send_(std::move(frame));
  }};
  std::string pending_response_;
  bool upgraded_{};
};

#endif
}} // namespace corvid::proto
