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
// flag is the high bit of the length byte that follows.
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
// in that it is a fixed-size structure capable of holding the largest possible
// header.
//
// Memory layout at the front matches the on-wire representation:
//   `frame_control`    -- byte 0: FIN|RSV1-3|opcode (cast directly from wire)
//   `variable_section` -- bytes 1..N (at most 13): MASK(1)|len7(7),
//                         optional 2- or 8-byte extended length, optional
//                         4-byte mask
struct ws_frame_header {
  ws_frame_control frame_control{}; // byte 0 on the wire
  uint8_t variable_section[13]{};   // bytes 1-13; only `length-1` bytes valid
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

  // Construct over `header`, initializing with length of `header` or the full
  // buffer it's at the front of, then use `is_complete` before `parse`.
  explicit ws_frame_wrapper(header_t& header, size_t header_length = 0)
      : header_{&header}, header_length_{header_length} {}

  // Construct over `frame`, initializing with length of `frame`, then use
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
  // `payload_length`, including the frame control byte, initial length byte,
  // and any extended length bytes, as well as the mask key. Suitable for
  // sizing a buffer before `build`ing a header and payload in it.
  static size_t
  header_length_for(size_t payload_len, size_t is_mask = false) noexcept {
    size_t mask_len = is_mask ? 4 : 0;
    if (payload_len < 126) return 2 + mask_len;
    if (payload_len <= 0xFFFF) return 4 + mask_len;
    return 10 + mask_len;
  }

  // Extract the length byte, excluding the `MASK` bit. This is not
  // generally the length; use `payload_length` for that.
  [[nodiscard]] uint8_t length_byte() const noexcept {
    return header_->variable_section[0] & uint8_t{0x7F};
  }

  // Opcode.
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

  // The opcode nibble.
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
    return (header_->variable_section[0] & 0x80U) != 0;
  }

  // Mask key, or 0 if `!is_masked()`.
  [[nodiscard]] uint32_t mask_key() const noexcept { return mask_; }

  // Determine whether the header is complete. Interprets `header_length` as
  // `buffer_length` and probes the start of the header buffer up to that
  // point. In the process, sets `header_length`. If the header is complete
  // then it can be parsed.
  [[nodiscard]] bool is_complete() noexcept {
    const size_t buffer_length = header_length_;
    if (buffer_length >= 14) return true; // max header size is 14 bytes

    // Must have at least frame control byte and first length byte.
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
      ws_frame_control frame_control, size_t payload_len,
      std::optional<uint32_t> mask) noexcept {
    ws_frame_wrapper<access::as_mutable> lens{header};
    lens.frame_control() = frame_control;
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
      lens.mask_ = *mask;
      uint32_t be_mask = hton32(lens.mask_);
      std::memcpy(vs + lens.header_length_ - 1, &be_mask, sizeof(be_mask));
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

  // Builds header into the frame, leaving its `size` the header length and its
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
  // more bytes are needed. On success, the returned `ws_frame_view` has
  // `header_length`, `payload_length`, and `total_length` filled. The caller
  // must read `payload_length` more bytes to get the whole frame.
  [[nodiscard]] static std::optional<ws_frame_view> parse_header(
      std::string_view data) noexcept {
    ws_frame_view hdr{data.data(), data.size()};
    if (!hdr.is_complete() || !hdr.parse()) return std::nullopt;
    return hdr;
  }

  // Serialize a complete WebSocket frame into a new string. `frame_control`
  // carries both the FIN flag and the opcode nibble (e.g.,
  // `ws_frame_control::fin | ws_frame_control::text`). If `mask` is present,
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
// the client side (with `connection_role::client`).
class http_websocket {
public:
  using message_fn =
      std::function<bool(http_websocket&, std::string&&, ws_frame_control)>;
  using close_fn =
      std::function<void(http_websocket&, uint16_t, std::string_view)>;

  // Sanity check limit on frame size, whether a fragment or complete.
  static constexpr size_t max_frame_size{size_t{16} * 1024 * 1024};

  // Error value for feed(std::string_view&).
  static constexpr size_t insatiable = std::numeric_limits<size_t>::max();

  // Called when a text or binary message arrives. Return false to indicate
  // failure.
  message_fn on_message;

  // Called when a close frame is received. `code` is the 2-byte status
  // code (1000 = normal); `reason` is the optional UTF-8 reason string.
  close_fn on_close;

  explicit http_websocket(http_transaction::send_fn send,
      connection_role role = connection_role::server)
      : send_{std::move(send)}, is_server_{role == connection_role::server} {}

  // Feed raw received bytes from `handle_data`. Accumulates fragmented frames
  // in `recv_frame_`, reassembles complete frames, defragments messages, and
  // fires callbacks. Returns false on a protocol error (bad frame_control,
  // oversized frame, etc.); the caller should treat false as a cue to close
  // the connection.
  [[nodiscard]] bool feed(recv_buffer_view& view) {
    // Loop until we run out of frame fragments.
    while (true) {
      std::string_view data = view.active_view();
      size_t needed = feed(data);
      view.update_active_view(data);

      if (needed == insatiable) return false;
      if (needed > view.buffer_capacity()) view.expand_to(needed);
      return true;
    }
  }

  // Accumulates fragmented frames in `recv_frame_`, reassembles complete
  // frames, defragments messages, and fires callbacks. Returns false on a
  // protocol error (bad frame_control, oversized frame, etc.); the caller
  // should treat false as a cue to close the connection.
  [[nodiscard]] size_t feed(std::string_view& data) {
    // Loop over all frames in `data`.
    while (true) {
      if (data.empty()) break;

      auto hdr_opt = ws_frame_codec::parse_header(data);
      if (!hdr_opt) return 2;

      // If more bytes needed, wait for them.
      auto& hdr = *hdr_opt;
      const size_t total = hdr.total_length();
      if (data.size() < total) {
        if (total > max_frame_size) return insatiable;
        return total - data.size();
      }

      auto payload = data.substr(hdr.header_length(), hdr.payload_length());
      bool processed = do_process_payload(hdr, payload);
      if (!processed) return insatiable;

      data.remove_prefix(total);
    }
    return 0;
  }

  // Send a text message frame (FIN set, text opcode).
  [[nodiscard]] bool send_text(std::string_view payload) {
    return do_send(ws_frame_control::fin | ws_frame_control::text, payload);
  }

  // Send a binary message frame (FIN set, binary opcode).
  [[nodiscard]] bool send_binary(std::string_view payload) {
    return do_send(ws_frame_control::fin | ws_frame_control::binary, payload);
  }

  // Send a close frame.
  [[nodiscard]] bool
  send_close(uint16_t code = 1000, std::string_view reason = {}) {
    std::string body;
    body.reserve(2 + reason.size());
    body.push_back(static_cast<char>(code >> 8));
    body.push_back(static_cast<char>(code & 0xFF));
    body.append(reason);
    return do_send(ws_frame_control::fin | ws_frame_control::close, body);
  }

private:
  [[nodiscard]] bool send_pong(std::string_view payload = {}) {
    return do_send(ws_frame_control::fin | ws_frame_control::pong, payload);
  }

  [[nodiscard]] bool dispatch_close(std::string_view payload) {
    uint16_t code{1000};
    std::string_view reason{};
    if (payload.size() >= 2) {
      code = (static_cast<uint16_t>(static_cast<uint8_t>(payload[0])) << 8) |
             static_cast<uint8_t>(payload[1]);
      reason = {payload.data() + 2, payload.size() - 2};
    }
    if (on_close) on_close(*this, code, reason);
    // TODO: This has to actually close.
    return true;
  }

  // Process payload. Validates fragmentation, accumulates payload, consumes
  // bytes, and dispatches when the message is complete.
  [[nodiscard]] bool
  do_process_payload(ws_frame_view& hdr, std::string_view payload) {
    const auto frame_control = hdr.frame_control();
    if (bitmask::has(frame_control, ws_frame_control::rsvd)) return false;
    const auto opcode = hdr.opcode();
    const bool is_fin = hdr.is_final();

    if (bitmask::has(frame_control, ws_frame_control::control))
      return handle_control_frame(hdr, payload);

    // We store the opcode of the initial frame in `fragment_opcode_`, so if
    // it's not 0, we're expecting a continuation. Therefore, we validate
    // fragmentation state per RFC 6455 section 5.4.
    const bool in_fragment = (fragment_opcode_ != ws_frame_control{});
    if (in_fragment && opcode != ws_frame_control::continuation) return false;
    if (!in_fragment && opcode == ws_frame_control::continuation) return false;

    // Accumulate the (unmasked) payload into `recv_frame_`. Note that it's
    // always legal for a payload to be empty.

    // Handle initial (or only) fragment of a message.
    if (!in_fragment) {
      // Save opcode so continuations can be validated and the assembled
      // message dispatched with the correct type.
      if (!is_fin) fragment_opcode_ = opcode;
      recv_frame_.clear();
    }

    // Sanity check.
    if (recv_frame_.size() + payload.size() > max_frame_size) return false;

    // Append the unmasked payload to `recv_frame_`. It is allowed to be empty.
    const size_t old_size = recv_frame_.size();
    no_zero::resize_to(recv_frame_, old_size + payload.size());
    if (!payload.empty() &&
        !hdr.mask_payload_copy(recv_frame_.data() + old_size, payload))
      return false;

    // If it's not the final fragment, we have to wait for more.
    // TODO: We should allow the user to disable this check so that they can
    // see each fragment as it arrives.
    if (!is_fin) return true;

    // Dispatch the complete (possibly reassembled) message. Moving
    // `recv_frame_` clears it automatically for the next message.
    const ws_frame_control dispatch_opcode =
        in_fragment ? fragment_opcode_ : opcode;
    if (is_fin) fragment_opcode_ = {};

    return dispatch_frame(std::move(recv_frame_), dispatch_opcode);
  }

  [[nodiscard]] bool
  handle_control_frame(ws_frame_view& hdr, std::string_view payload) {
    const auto opcode = hdr.opcode();
    if (!hdr.is_final() || payload.size() > 125) return false;
    if (opcode == ws_frame_control::close) {
      if (payload.size() == 1) return false;
      return dispatch_close(payload);
    }
    if (opcode == ws_frame_control::ping) { return send_pong(payload); }
    if (opcode == ws_frame_control::pong) {
      // TODO: Compare payload against outstanding pings, if any and reset
      // timeout. We also need to expose a `send_ping` method that stores the
      // payload for comparison.
      return true;
    }
    return false;
  }

  [[nodiscard]] bool
  dispatch_frame(std::string&& payload, ws_frame_control opcode_bits) {
    bool success = true;
    if (on_message)
      success = on_message(*this, std::move(payload), opcode_bits);
    payload.clear();
    return success;
  }

  [[nodiscard]] bool
  do_send(ws_frame_control opcode, std::string_view payload) {
    std::optional<uint32_t> mask;
    if (!is_server_) mask.emplace(rd_());
    std::string frame = ws_frame_codec::serialize_frame(opcode, payload, mask);
    if (!send_) return false;
    return send_(std::move(frame));
  }

  [[nodiscard]] bool do_send(std::string&& frame) {
    if (!send_) return false;
    return send_(std::move(frame));
  }

  http_transaction::send_fn send_;
  bool is_server_{};
  std::string recv_frame_;
  ws_frame_control fragment_opcode_{};
  std::random_device rd_;
};

}} // namespace corvid::proto
