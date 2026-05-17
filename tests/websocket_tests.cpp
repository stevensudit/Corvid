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

#include "../corvid/proto.h"
#include "../corvid/concurrency/jthread_stoppable_sleep.h"

#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace std::string_literals;
using namespace std::chrono_literals;

bool is_codex() {
  const char* value = std::getenv("CODEX_SANDBOX_NETWORK_DISABLED");
  return value && std::string_view{value} == "1";
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

// `http_head_codec` unit tests.

// Verify that a well-formed HTTP/1.1 GET request is parsed correctly.
// Load `data` into `buf` and return a live view with a no-op resume callback.
// `buf` must outlive the returned view.
[[nodiscard]] recv_buffer_view
wstx_make_view(recv_buffer& buf, std::string_view data = {}) {
  buf.reads_enabled = false;
  buf.resize(std::max(data.size() + 1, size_t{256}));
  if (!data.empty()) std::memcpy(buf.buffer.data(), data.data(), data.size());
  buf.end.store(data.size(), std::memory_order::relaxed);
  buf.begin.store(0, std::memory_order::relaxed);
  return recv_buffer_view{buf, [](size_t, size_t) {}};
}

// Build a well-formed WebSocket upgrade `request_head` without parsing.
[[nodiscard]] request_head wstx_make_upgrade_req(
    std::string* accept_key_ptr = nullptr) {
  http_websocket hws{[](any_strings&&) { return true; }};
  std::string accept_key;
  request_head req =
      http_websocket::generate_upgrade_request("/ws", accept_key);
  if (accept_key_ptr) *accept_key_ptr = std::move(accept_key);
  return req;
}

void wstx_reextract_options(request_head& req) {
  req.options = {};
  req.options.extract(req.headers);
}

#pragma region AcceptKey

// `ws_frame_wrapper` and `http_websocket` unit tests.

// RFC 6455 section 1.3: known input -> known accept key.
TEST_CASE("WebSocket_AcceptKey", "[WebSocket]") {
  const auto key =
      ws_frame_view::compute_accept_key("dGhlIHNhbXBsZSBub25jZQ==");
  CHECK((key) == ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

#pragma endregion
#pragma region FrameCodec_RoundTrip

// Serialize an unmasked text frame and verify the parsed header fields.
TEST_CASE("WebSocket_FrameCodec_RoundTrip", "[WebSocket]") {
  const std::string payload{"hello"};
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, payload);

  REQUIRE((frame.size()) >= (7ULL));
  ws_frame_view hdr{frame};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.is_final()));
  CHECK_FALSE((hdr.is_masked()));
  CHECK((hdr.header_length()) == (2ULL));
  CHECK((hdr.payload_length()) == (5ULL));
  CHECK((hdr.total_length()) == (7ULL));

  const std::string_view extracted{frame.data() + hdr.header_length(),
      hdr.payload_length()};
  CHECK((extracted) == (payload));
}

#pragma endregion
#pragma region Feed_SingleText

// Client pump receives a single unmasked text frame from the server.
TEST_CASE("WebSocket_Feed_SingleText", "[WebSocket]") {
  std::string got_msg;
  ws_frame_control got_op{};
  http_websocket ws{[](any_strings&&) { return true; },
      connection_role::client};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    got_msg = std::move(p);
    got_op = op;
    return true;
  };
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "hello");
  std::string_view wire{frame};
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK((got_msg) == ("hello"));
  CHECK((got_op) == (ws_frame_control::text));
}

#pragma endregion
#pragma region Feed_SingleTextInvalidUtf8

// Invalid UTF-8 in a text frame fails the connection with close code 1007.
TEST_CASE("WebSocket_Feed_SingleTextInvalidUtf8", "[WebSocket]") {
  std::string sent_frame;
  http_websocket ws{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  bool msg_fired{};
  ws.on_message = [&](http_websocket&, std::string&&, ws_frame_control) {
    msg_fired = true;
    return true;
  };

  std::string wire_frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "\x80", 0x12345678U);
  std::string_view wire{wire_frame};
  CHECK((ws.feed(wire)) == (http_websocket::insatiable));
  CHECK_FALSE((msg_fired));

  ws_frame_view hdr{sent_frame};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.opcode()) == (ws_frame_control::close));
  const std::string_view close_payload = std::string_view{sent_frame}.substr(
      hdr.header_length(), hdr.payload_length());
  REQUIRE((close_payload.size()) >= (2U));
  const uint16_t code =
      (static_cast<uint8_t>(close_payload[0]) << 8) |
      static_cast<uint8_t>(close_payload[1]);
  CHECK((code) == (uint16_t{1007}));
}

#pragma endregion
#pragma region Feed_SingleTextInvalidUtf8Disabled

// Disabling UTF-8 validation allows invalid text payloads through.
TEST_CASE("WebSocket_Feed_SingleTextInvalidUtf8Disabled", "[WebSocket]") {
  std::string got_msg;
  ws_frame_control got_op{};
  bool msg_fired{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.validate_utf8 = false;
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    got_msg = std::move(p);
    got_op = op;
    msg_fired = true;
    return true;
  };

  std::string wire_frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "\x80", 0x12345678U);
  std::string_view wire{wire_frame};
  CHECK((ws.feed(wire)) == (0U));
  CHECK((msg_fired));
  CHECK((got_msg) == (std::string("\x80", 1)));
  CHECK((got_op) == (ws_frame_control::text));
}

#pragma endregion
#pragma region Feed_MaskedBinary

// Server pump receives a masked binary frame and correctly unmasks it.
TEST_CASE("WebSocket_Feed_MaskedBinary", "[WebSocket]") {
  std::string got_msg;
  ws_frame_control got_op{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    got_msg = std::move(p);
    got_op = op;
    return true;
  };
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::binary, "world",
      uint32_t{0xDEADBEEF});
  std::string_view wire{frame};
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK((got_msg) == ("world"));
  CHECK((got_op) == (ws_frame_control::binary));
}

#pragma endregion
#pragma region Feed_Ping

// Server auto-pongs a ping frame and does not fire on_message.
TEST_CASE("WebSocket_Feed_Ping", "[WebSocket]") {
  std::string sent_frame;
  bool msg_fired{};
  http_websocket ws_server{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws_server.on_message =
      [&](http_websocket&, std::string&&, ws_frame_control) {
        msg_fired = true;
        return true;
      };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  CHECK((ws_client.send_frame(ws_frame_control::fin | ws_frame_control::ping,
      "ping-payload")));
  std::string_view wire{received_frame};

  CHECK((ws_server.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK_FALSE((msg_fired));
  REQUIRE_FALSE((sent_frame.empty()));
  ws_frame_view hdr{sent_frame};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  const auto pong_op = static_cast<ws_frame_control>(
      static_cast<uint8_t>(hdr.opcode()) & 0x0FU);
  CHECK((pong_op) == (ws_frame_control::pong));
  // Pong body must echo the ping payload.
  CHECK((hdr.payload_length()) == (12ULL));
}

#pragma endregion
#pragma region Feed_Close

// Server fires on_close with the correct status code and reason string.
TEST_CASE("WebSocket_Feed_Close", "[WebSocket]") {
  uint16_t got_code{};
  std::string got_reason;
  http_websocket ws_server{[](any_strings&&) { return true; }};
  ws_server.on_close =
      [&](http_websocket&, uint16_t code, std::string_view reason) {
        got_code = code;
        got_reason = reason;
      };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  CHECK((ws_client.send_close(1001, "going away")));
  std::string_view wire{received_frame};

  CHECK((ws_server.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK((got_code) == (uint16_t{1001}));
  CHECK((got_reason) == ("going away"));
}

#pragma endregion
#pragma region Feed_CloseInvalidUtf8Reason

// Invalid UTF-8 in a close reason fails the connection with 1007.
TEST_CASE("WebSocket_Feed_CloseInvalidUtf8Reason", "[WebSocket]") {
  std::string sent_frame;
  bool close_fired{};
  http_websocket ws_server{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws_server.on_close = [&](http_websocket&, uint16_t, std::string_view) {
    close_fired = true;
  };
  std::string payload;
  payload.push_back(char{0x03});
  payload.push_back(static_cast<char>(0xE8)); // 1000
  payload.push_back(static_cast<char>(0x80)); // invalid UTF-8 reason byte
  const std::string received_frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::close, payload, 0x12345678U);
  std::string_view wire{received_frame};

  CHECK((ws_server.feed(wire)) != (0U));
  CHECK((wire.size()) != (0U));
  CHECK_FALSE((close_fired));

  ws_frame_view hdr{sent_frame};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.opcode()) == (ws_frame_control::close));
  const std::string_view close_payload = std::string_view{sent_frame}.substr(
      hdr.header_length(), hdr.payload_length());
  REQUIRE((close_payload.size()) >= (2U));
  const uint16_t code =
      (static_cast<uint8_t>(close_payload[0]) << 8) |
      static_cast<uint8_t>(close_payload[1]);
  CHECK((code) == (uint16_t{1007}));
}

#pragma endregion
#pragma region Feed_Fragmented

// Three-frame fragmented message is assembled and delivered exactly once.
TEST_CASE("WebSocket_Feed_Fragmented", "[WebSocket]") {
  std::string got_msg;
  ws_frame_control got_op{};
  int msg_count{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    got_msg = std::move(p);
    got_op = op;
    ++msg_count;
    return true;
  };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  // Fragment 1: FIN=0, text opcode, "hel".
  CHECK((ws_client.send_frame(ws_frame_control::text, "hel")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK((msg_count) == (0));

  // Fragment 2: FIN=0, continuation, "lo ".
  CHECK((ws_client.send_frame(ws_frame_control::continuation, "lo ")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK((msg_count) == (0));

  // Fragment 3: FIN=1, continuation, "world" -> dispatch.
  CHECK((ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "world")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK((msg_count) == (1));
  CHECK((got_msg) == ("hello world"));
  CHECK((got_op) == (ws_frame_control::text));
}

#pragma endregion
#pragma region Feed_FragmentedDelivery

// Three-frame fragmented message with deliver_fragments=true fires on_message
// once per frame. Non-final frames carry the data opcode without the fin bit;
// the final frame carries opcode|fin.
TEST_CASE("WebSocket_Feed_FragmentedDelivery", "[WebSocket]") {
  struct Call {
    std::string payload;
    ws_frame_control op{};
  };
  std::vector<Call> calls;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.deliver_fragments = true;
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    calls.push_back({std::move(p), op});
    return true;
  };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  // Fragment 1: FIN=0, text opcode, "hel".
  CHECK((ws_client.send_frame(ws_frame_control::text, "hel")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  REQUIRE((calls.size()) == (1U));
  CHECK((calls[0].payload) == ("hel"));
  CHECK((calls[0].op) == (ws_frame_control::text));

  // Fragment 2: FIN=0, continuation, "lo ".
  CHECK((ws_client.send_frame(ws_frame_control::continuation, "lo ")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  REQUIRE((calls.size()) == (2U));
  CHECK((calls[1].payload) == ("lo "));
  CHECK((calls[1].op) == (ws_frame_control::text));

  // Fragment 3: FIN=1, continuation, "world" -> dispatched with fin bit.
  CHECK((ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "world")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  REQUIRE((calls.size()) == (3U));
  CHECK((calls[2].payload) == ("world"));
  CHECK((calls[2].op) == (ws_frame_control::fin | ws_frame_control::text));
}

#pragma endregion
#pragma region Feed_FragmentedDeliverySplitUtf8

// In fragment-delivery mode, a code point split across frames is accepted.
TEST_CASE("WebSocket_Feed_FragmentedDeliverySplitUtf8", "[WebSocket]") {
  struct Call {
    std::string payload;
    ws_frame_control op{};
  };
  std::vector<Call> calls;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.deliver_fragments = true;
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    calls.push_back({std::move(p), op});
    return true;
  };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  CHECK((ws_client.send_frame(ws_frame_control::text, "\xF0\x9F")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));

  CHECK((ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "\x98\x80")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));

  REQUIRE((calls.size()) == (2U));
  CHECK((calls[0].payload) == ("\xF0\x9F"));
  CHECK((calls[0].op) == (ws_frame_control::text));
  CHECK((calls[1].payload) == ("\x98\x80"));
  CHECK((calls[1].op) == (ws_frame_control::fin | ws_frame_control::text));
}

#pragma endregion
#pragma region Feed_FragmentedDeliveryInvalidUtf8

// In fragment-delivery mode, invalid UTF-8 across frames fails with 1007.
TEST_CASE("WebSocket_Feed_FragmentedDeliveryInvalidUtf8", "[WebSocket]") {
  std::string sent_frame;
  http_websocket ws{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws.deliver_fragments = true;
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  CHECK((ws_client.send_frame(ws_frame_control::text, "\xF0")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));

  CHECK((ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "A")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (http_websocket::insatiable));

  ws_frame_view hdr{sent_frame};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.opcode()) == (ws_frame_control::close));
  const std::string_view payload = std::string_view{sent_frame}.substr(
      hdr.header_length(), hdr.payload_length());
  REQUIRE((payload.size()) >= (2U));
  const uint16_t code =
      (static_cast<uint8_t>(payload[0]) << 8) |
      static_cast<uint8_t>(payload[1]);
  CHECK((code) == (uint16_t{1007}));
}

#pragma endregion
#pragma region Feed_FragmentedDeliveryInvalidUtf8EmptyFinal

// An empty final fragment must still fail if the prior text ended
// mid-code-point.
TEST_CASE("WebSocket_Feed_FragmentedDeliveryInvalidUtf8EmptyFinal",
    "[WebSocket]") {
  std::string sent_frame;
  http_websocket ws{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws.deliver_fragments = true;
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  CHECK((ws_client.send_frame(ws_frame_control::text, "\xF0")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));

  CHECK((ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (http_websocket::insatiable));

  ws_frame_view hdr{sent_frame};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.opcode()) == (ws_frame_control::close));
  const std::string_view payload = std::string_view{sent_frame}.substr(
      hdr.header_length(), hdr.payload_length());
  REQUIRE((payload.size()) >= (2U));
  const uint16_t code =
      (static_cast<uint8_t>(payload[0]) << 8) |
      static_cast<uint8_t>(payload[1]);
  CHECK((code) == (uint16_t{1007}));
}

#pragma endregion
#pragma region Feed_FragmentedDeliveryInvalidUtf8Disabled

// Disabling UTF-8 validation also suppresses fragment-mode UTF-8 failures.
TEST_CASE("WebSocket_Feed_FragmentedDeliveryInvalidUtf8Disabled",
    "[WebSocket]") {
  struct Call {
    std::string payload;
    ws_frame_control op{};
  };
  std::vector<Call> calls;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.deliver_fragments = true;
  ws.validate_utf8 = false;
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    calls.push_back({std::move(p), op});
    return true;
  };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  CHECK((ws_client.send_frame(ws_frame_control::text, "\xF0")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));

  CHECK((ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "A")));
  wire = received_frame;
  CHECK((ws.feed(wire)) == (0U));

  REQUIRE((calls.size()) == (2U));
  CHECK((calls[0].payload) == (std::string("\xF0", 1)));
  CHECK((calls[0].op) == (ws_frame_control::text));
  CHECK((calls[1].payload) == ("A"));
  CHECK((calls[1].op) == (ws_frame_control::fin | ws_frame_control::text));
}

#pragma endregion
#pragma region Feed_PartialFrame

// Feeding only the header bytes of a frame returns true (awaiting payload).
TEST_CASE("WebSocket_Feed_PartialFrame", "[WebSocket]") {
  bool msg_fired{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&&, ws_frame_control) {
    msg_fired = true;
    return true;
  };
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "Hello", 0);
  const size_t frame_size = frame.size();
  std::string buf;
  std::string_view wire;

  // With 1 byte, it has no idea.
  buf = frame;
  buf.resize(1);
  wire = buf;
  CHECK((ws.feed(wire)) == (2ULL));
  CHECK_FALSE((msg_fired));

  // With 2, it knows what the frame is and knows it needs a mask, but won't
  // look any further.
  buf = frame;
  buf.resize(2);
  wire = buf;
  CHECK((ws.feed(wire)) == (6ULL));
  CHECK_FALSE((msg_fired));

  // With 6, it has the full frame, and knows it needs the payload.
  buf = frame;
  buf.resize(6);
  wire = buf;
  CHECK((ws.feed(wire)) == (frame_size));
  CHECK_FALSE((msg_fired));

  // With 8, it knows it only has a partial payload.
  buf = frame;
  buf.resize(8);
  wire = buf;
  CHECK((ws.feed(wire)) == (frame_size));
  CHECK_FALSE((msg_fired));

  // With the whole thing it works.
  buf = frame;
  wire = buf;
  CHECK((ws.feed(wire)) == (0ULL));
  CHECK((msg_fired));
}

#pragma endregion
#pragma region Feed_RecvBufferViewRequestsFrameSizedGrowth

// `feed(recv_buffer_view&)` requests growth to the full frame size when a
// frame prefix fills the buffer but the completed frame would not fit after
// compaction.
TEST_CASE("WebSocket_Feed_RecvBufferViewRequestsFrameSizedGrowth",
    "[WebSocket]") {
  recv_buffer rb;
  rb.buffer.resize(256);
  const size_t capacity = rb.buffer.capacity();
  rb.min_capacity = capacity;
  rb.begin.store(0, std::memory_order::relaxed);
  rb.end.store(capacity, std::memory_order::relaxed);

  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text,
      std::string(capacity, 'x'));
  REQUIRE((frame.size()) > (capacity));
  std::memcpy(rb.buffer.data(), frame.data(), capacity);

  size_t resume_new_size{};
  size_t resume_last_seen_end{};
  {
    recv_buffer_view view{rb,
        [&](size_t n, size_t lse) {
          resume_new_size = n;
          resume_last_seen_end = lse;
        }};
    http_websocket ws{[](any_strings&&) { return true; }};
    CHECK((ws.feed(view)));
  }

  CHECK((resume_new_size) == (frame.size()));
  CHECK((resume_last_seen_end) == (capacity));
}

#pragma endregion
#pragma region Feed_MultipleFrames

// Two complete frames in one buffer each fire on_message.
TEST_CASE("WebSocket_Feed_MultipleFrames", "[WebSocket]") {
  std::vector<std::string> msgs;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control) {
    msgs.emplace_back(std::move(p));
    return true;
  };
  const std::string both =
      ws_frame_lens::serialize_frame(
          ws_frame_control::fin | ws_frame_control::text, "foo", 0) +
      ws_frame_lens::serialize_frame(
          ws_frame_control::fin | ws_frame_control::text, "bar", 0);
  std::string_view wire{both};
  CHECK((ws.feed(wire)) == (0ULL));
  REQUIRE((msgs.size()) == (2ULL));
  CHECK((msgs[0]) == ("foo"));
  CHECK((msgs[1]) == ("bar"));
}

#pragma endregion
#pragma region Feed_MultipleFramesViaView

// Two complete frames in one `recv_buffer_view` call: both must be delivered.
// This exercises `feed(recv_buffer_view&)` specifically, not the
// `feed(std::string_view&)` overload.
TEST_CASE("WebSocket_Feed_MultipleFramesViaView", "[WebSocket]") {
  std::vector<std::string> msgs;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control) {
    msgs.emplace_back(std::move(p));
    return true;
  };
  const std::string both =
      ws_frame_lens::serialize_frame(
          ws_frame_control::fin | ws_frame_control::text, "foo", 0) +
      ws_frame_lens::serialize_frame(
          ws_frame_control::fin | ws_frame_control::text, "bar", 0);
  recv_buffer buf;
  buf.reads_enabled = false;
  buf.resize(both.size() + 1);
  std::memcpy(buf.buffer.data(), both.data(), both.size());
  buf.end.store(both.size(), std::memory_order::relaxed);
  recv_buffer_view view{buf, [](size_t, size_t) {}};
  CHECK((ws.feed(view)));
  REQUIRE((msgs.size()) == (2ULL));
  CHECK((msgs[0]) == ("foo"));
  CHECK((msgs[1]) == ("bar"));
}

#pragma endregion
#pragma region Feed_BadContinuation

// A continuation frame without a prior start fragment is a protocol error.
TEST_CASE("WebSocket_Feed_BadContinuation", "[WebSocket]") {
  http_websocket ws{[](any_strings&&) { return true; }};
  std::string buf = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "data");
  std::string_view wire{buf};
  CHECK((ws.feed(wire)) == (-1ULL));
}

#pragma endregion
#pragma region Feed_InterleavedData

// A non-continuation data frame arriving mid-fragment is a protocol error.
TEST_CASE("WebSocket_Feed_InterleavedData", "[WebSocket]") {
  http_websocket ws{[](any_strings&&) { return true; }};
  std::string buf =
      ws_frame_lens::serialize_frame(ws_frame_control::text, "start", 0);
  std::string_view wire{buf};
  CHECK((ws.feed(wire)) == (0ULL));

  // This shouldn't be marked as text.
  buf = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "bad", 0);
  wire = buf;
  CHECK((ws.feed(wire)) == (-1ULL));
}

#pragma endregion
#pragma region Feed_DataAfterSentClose

// Data frame received after we sent a close is silently discarded.
// `on_message` must not fire and `feed` must succeed (not return
// `insatiable`).
TEST_CASE("WebSocket_Feed_DataAfterSentClose", "[WebSocket]") {
  bool msg_fired{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&&, ws_frame_control) {
    msg_fired = true;
    return true;
  };
  REQUIRE((ws.send_close(1000)));
  REQUIRE((ws.is_close_started()));

  // Peer sends a text frame after we've already sent our close.
  const std::string frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "late", 0);
  std::string_view wire{frame};
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK_FALSE((msg_fired));
}

#pragma endregion
#pragma region Feed_DataAfterReceivedClose

// Data frame received after we received a close is silently discarded.
// `on_message` must not fire and `feed` must succeed (not return
// `insatiable`).
TEST_CASE("WebSocket_Feed_DataAfterReceivedClose", "[WebSocket]") {
  bool msg_fired{};
  std::string sent_frame;
  http_websocket ws{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws.on_message = [&](http_websocket&, std::string&&, ws_frame_control) {
    msg_fired = true;
    return true;
  };

  // Feed an inbound close from a masked client frame.
  std::string close_frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::close, {}, 0x12345678U);
  std::string_view wire{close_frame};
  REQUIRE((ws.feed(wire)) == (0U));
  REQUIRE((ws.is_close_started()));

  // Peer sends a text frame after their own close frame.
  const std::string frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "late", 0x12345678U);
  wire = frame;
  CHECK((ws.feed(wire)) == (0U));
  CHECK((wire.size()) == (0U));
  CHECK_FALSE((msg_fired));
}

#pragma endregion
#pragma region Send_Server

// Server pump sends unmasked frames.
TEST_CASE("WebSocket_Send_Server", "[WebSocket]") {
  std::string sent;
  http_websocket ws{[&](any_strings&& f) {
    sent = std::get<std::string>(std::move(f));
    return true;
  }};
  CHECK((ws.send_text("hi")));
  REQUIRE_FALSE((sent.empty()));
  ws_frame_view hdr{sent};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK_FALSE((hdr.is_masked()));
  CHECK((hdr.payload_length()) == (2ULL));
  // Payload is plaintext; verify it directly.
  const std::string_view pl{sent.data() + hdr.header_length(), 2};
  CHECK((pl) == ("hi"));
}

#pragma endregion
#pragma region Send_Client

// Client pump sends masked frames; payload round-trips via unmask.
TEST_CASE("WebSocket_Send_Client", "[WebSocket]") {
  std::string sent;
  http_websocket ws{
      [&](any_strings&& f) {
        sent = std::get<std::string>(std::move(f));
        return true;
      },
      connection_role::client};
  CHECK((ws.send_text("hi")));
  REQUIRE_FALSE((sent.empty()));
  ws_frame_view hdr{sent};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.is_masked()));
  CHECK((hdr.payload_length()) == (2ULL));
  char unmasked[2]{};
  CHECK((hdr.mask_payload_copy(unmasked,
      {sent.data() + hdr.header_length(), hdr.payload_length()})));
  CHECK((std::string_view(unmasked, 2)) == ("hi"));
}

#pragma endregion
#pragma region FrameWrapper_HeaderAndViews

// `header()`, `header_view()`, and `payload_view()` return correctly bounded
// views after `parse`.
TEST_CASE("WebSocket_FrameWrapper_HeaderAndViews", "[WebSocket]") {
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "abc");
  ws_frame_view hdr{frame.data(), frame.size()};
  REQUIRE((hdr.is_complete() && hdr.parse()));

  // `header_view` covers exactly the header bytes and begins at `header()`.
  CHECK((hdr.header_view().size()) == (hdr.header_length()));
  CHECK((hdr.header_view().data()) ==
        (reinterpret_cast<const char*>(&hdr.header())));

  // `payload_view` covers the payload bytes immediately after the header.
  CHECK((hdr.payload_view().size()) == (hdr.payload_length()));
  CHECK((hdr.payload_view()) == ("abc"));
}

#pragma endregion
#pragma region FrameWrapper_CopyTo

// `copy_to` copies the header bytes into a caller-supplied buffer.
TEST_CASE("WebSocket_FrameWrapper_CopyTo", "[WebSocket]") {
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::binary, "xy");
  ws_frame_view hdr{frame.data(), frame.size()};
  REQUIRE((hdr.is_complete() && hdr.parse()));

  char buf[14]{};
  CHECK((hdr.copy_to(buf)));
  CHECK((std::memcmp(buf, frame.data(), hdr.header_length())) == (0));

  // A default-constructed (null) wrapper has no header to copy.
  ws_frame_view empty{};
  char buf2[14]{};
  CHECK_FALSE((empty.copy_to(buf2)));
}

#pragma endregion
#pragma region FrameWrapper_MaskPayloadInPlace

// `mask_payload` unmasks the payload bytes of a masked frame in-place,
// exercising `mask_key()` and the mutable `variable_section()` accessor.
TEST_CASE("WebSocket_FrameWrapper_MaskPayloadInPlace", "[WebSocket]") {
  const std::string payload{"hello"};
  const uint32_t key = 0xDEADBEEF;
  std::string frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, payload, key);

  ws_frame_lens lens{frame};
  REQUIRE((lens.is_complete() && lens.parse()));
  CHECK((lens.is_masked()));
  CHECK((lens.mask_key()) != (0U));

  // The mutable `variable_section()` pointer falls right after the two fixed
  // header bytes.
  CHECK((reinterpret_cast<const char*>(lens.variable_section())) ==
        (frame.data() + 2));

  // In-place unmask: payload must round-trip back to the original.
  CHECK((lens.mask_payload(frame)));
  const std::string_view unmasked{frame.data() + lens.header_length(),
      lens.payload_length()};
  CHECK((unmasked) == (payload));

  // Applying `mask_payload` again re-masks; the round-trip is symmetric.
  CHECK((lens.mask_payload(frame)));
  CHECK((std::string_view(frame.data() + lens.header_length(),
            lens.payload_length())) != (payload));
}

#pragma endregion
#pragma region FrameWrapper_MaskPayloadCopyByteOrder

// Verify that `mask_payload_copy` applies the mask in RFC 6455 byte order:
// each payload byte is XOR'd with masking-key-octet-(i mod 4), where the
// octets are taken in frame (network) order, not as a 32-bit integer.
//
// Uses the concrete example from RFC 6455 Section 5.7:
//   masking key bytes: 0x37 0xfa 0x21 0x3d
//   payload:           "Hello"
//   masked:            0x7f 0x9f 0x4d 0x51 0x58
//
// The second case ("Hello, World!") exercises both the 8-byte-at-a-time main
// loop and the trailing straggler loop.
TEST_CASE("WebSocket_FrameWrapper_MaskPayloadCopyByteOrder", "[WebSocket]") {
  // `build` stores mask_ in host order and writes hton32(mask_) to the frame.
  // To get frame bytes [0x37, 0xfa, 0x21, 0x3d] on a little-endian host,
  // hton32(mask_val) must equal 0x3d21fa37, so mask_val = 0x37fa213d.
  const uint32_t mask_val = 0x37FA213D;

  // Short payload: exercises only the straggler loop (n < 8).
  {
    const std::string_view payload = "Hello";
    const std::string expected{"\x7f\x9f\x4d\x51\x58", 5};

    std::string frame;
    auto lens = ws_frame_lens::build(frame,
        ws_frame_control::fin | ws_frame_control::text, payload.size(),
        mask_val);
    REQUIRE((lens.mask_key()) == (mask_val));

    std::string dst(payload.size(), '\0');
    REQUIRE((lens.mask_payload_copy(dst.data(), payload)));
    CHECK((dst) == (expected));
  }

  // Longer payload: exercises the 8-byte main loop plus the straggler (13
  // bytes = 8 + 5).
  {
    const std::string_view payload = "Hello, World!";
    const std::string expected{
        "\x7f\x9f\x4d\x51\x58\xd6\x01\x6a\x58\x88\x4d\x59\x16", 13};

    std::string frame;
    auto lens = ws_frame_lens::build(frame,
        ws_frame_control::fin | ws_frame_control::text, payload.size(),
        mask_val);
    REQUIRE((lens.mask_key()) == (mask_val));

    std::string dst(payload.size(), '\0');
    REQUIRE((lens.mask_payload_copy(dst.data(), payload)));
    CHECK((dst) == (expected));
  }
}

#pragma endregion
#pragma region Send_Binary

// `send_binary` sends a FIN+binary frame with the correct opcode.
TEST_CASE("WebSocket_Send_Binary", "[WebSocket]") {
  std::string sent;
  http_websocket ws{[&](any_strings&& f) {
    sent = std::get<std::string>(std::move(f));
    return true;
  }};
  CHECK((ws.send_binary("data")));
  REQUIRE_FALSE((sent.empty()));
  ws_frame_view hdr{sent};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.is_final()));
  CHECK((hdr.opcode()) == (ws_frame_control::binary));
  CHECK((hdr.payload_length()) == (4ULL));
  CHECK((std::string_view(sent.data() + hdr.header_length(), 4)) == ("data"));
}

#pragma endregion
#pragma region Send_Pong_Direct

// `send_pong` sends a FIN+pong frame even after `send_close` would otherwise
// block outbound data.
TEST_CASE("WebSocket_Send_Pong_Direct", "[WebSocket]") {
  std::string sent;
  http_websocket ws{[&](any_strings&& f) {
    sent = std::get<std::string>(std::move(f));
    return true;
  }};
  REQUIRE((ws.send_close(1000)));
  CHECK((ws.send_pong("echo")));
  ws_frame_view hdr{sent};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.opcode()) == (ws_frame_control::pong));
  CHECK((std::string_view(sent.data() + hdr.header_length(),
            hdr.payload_length())) == ("echo"));
}

#pragma endregion
#pragma region Send_Frame_Prebuilt

// `send_frame(std::string&&)` delivers a pre-serialized frame directly to the
// send callback.
TEST_CASE("WebSocket_Send_Frame_Prebuilt", "[WebSocket]") {
  std::string sent;
  http_websocket ws{[&](any_strings&& f) {
    sent = std::get<std::string>(std::move(f));
    return true;
  }};
  std::string frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "pre");
  CHECK((ws.send_frame(std::move(frame))));
  ws_frame_view hdr{sent};
  REQUIRE((hdr.is_complete()));
  REQUIRE((hdr.parse()));
  CHECK((hdr.opcode()) == (ws_frame_control::text));
  CHECK((hdr.payload_length()) == (3ULL));
}

#pragma endregion
#pragma region Hangup

// `hangup` invokes the send callback with `std::monostate` to signal RST.
TEST_CASE("WebSocket_Hangup", "[WebSocket]") {
  bool called{};
  bool got_monostate{};
  http_websocket ws{[&](any_strings&& f) {
    called = true;
    got_monostate = std::holds_alternative<std::monostate>(f);
    return true;
  }};
  CHECK_FALSE((ws.hangup()));
  CHECK((called));
  CHECK((got_monostate));
}

#pragma endregion
#pragma region Fail

// `fail` sends a close frame and returns false. `fail_proto` wraps `fail` with
// code 1002 and a "Protocol failure: " prefix. `fail_insatiable` sends close
// and hangup then returns `insatiable`.
TEST_CASE("WebSocket_Fail", "[WebSocket]") {
  // `fail` with no prior close sends a close frame.
  {
    std::string sent;
    http_websocket ws{[&](any_strings&& f) {
      sent = std::get<std::string>(std::move(f));
      return true;
    }};
    CHECK_FALSE((ws.fail(1001, "bye")));
    ws_frame_view hdr{sent};
    REQUIRE((hdr.is_complete()));
    REQUIRE((hdr.parse()));
    CHECK((hdr.opcode()) == (ws_frame_control::close));
  }

  // `fail_proto` sends close code 1002 with a prefixed reason string.
  {
    std::string sent;
    http_websocket ws{[&](any_strings&& f) {
      sent = std::get<std::string>(std::move(f));
      return true;
    }};
    CHECK_FALSE((ws.fail_proto("test reason")));
    ws_frame_view hdr{sent};
    REQUIRE((hdr.is_complete()));
    REQUIRE((hdr.parse()));
    CHECK((hdr.opcode()) == (ws_frame_control::close));
    const std::string_view close_pl{sent.data() + hdr.header_length(),
        hdr.payload_length()};
    REQUIRE((close_pl.size()) >= (2U));
    const uint16_t code =
        (static_cast<uint8_t>(close_pl[0]) << 8) |
        static_cast<uint8_t>(close_pl[1]);
    CHECK((code) == (uint16_t{1002}));
  }

  // `fail_insatiable` returns `insatiable`.
  {
    http_websocket ws{[](any_strings&&) { return true; }};
    CHECK((ws.fail_insatiable(1000, "error")) == (http_websocket::insatiable));
  }
}

#pragma endregion
#pragma region OnDrain

// `on_drain` callback on `http_websocket_transaction` is invoked from
// `handle_drain` after the upgrade response is flushed.
TEST_CASE("WebSocketTransaction_OnDrain", "[WebSocketTransaction]") {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());

  int drain_count{};
  tx->on_drain = [&](http_transaction&, const http_transaction::send_fn&) {
    ++drain_count;
    return stream_claim::claim;
  };

  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    REQUIRE((tx->handle_data(view)) == (stream_claim::claim));
  }

  http_transaction::send_fn send_fn{[](any_strings&&) { return true; }};
  // First drain flushes the 101 response, then falls through to `on_drain`.
  CHECK((tx->handle_drain(send_fn)) == (stream_claim::claim));
  CHECK((drain_count) == (1));

  // Subsequent drains with no pending response also route through `on_drain`.
  CHECK((tx->handle_drain(send_fn)) == (stream_claim::claim));
  CHECK((drain_count) == (2));
}

#pragma endregion
#pragma region UpgradeSuccess

// Valid upgrade handshake: `handle_data` returns `claim`.
TEST_CASE("WebSocketTransaction_UpgradeSuccess", "[WebSocketTransaction]") {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  CHECK((tx->handle_data(view)) == (stream_claim::claim));
}

#pragma endregion
#pragma region DrainSendsResponse

// After upgrade, `handle_drain` sends the 101 response and returns `claim`.
TEST_CASE("WebSocketTransaction_DrainSendsResponse",
    "[WebSocketTransaction]") {
  std::string expected_accept_key;
  auto tx = std::make_shared<http_websocket_transaction>(
      wstx_make_upgrade_req(&expected_accept_key));
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    REQUIRE((tx->handle_data(view)) == (stream_claim::claim));
  }

  std::string sent;
  http_transaction::send_fn send_fn{[&](any_strings&& data) {
    sent = std::get<std::string>(std::move(data));
    return true;
  }};
  CHECK((tx->handle_drain(send_fn)) == (stream_claim::claim));
  REQUIRE_FALSE((sent.empty()));

  // `parse()` expects the wire text without the trailing blank-line CRLF.
  response_head resp;
  REQUIRE((resp.parse(sent.substr(0, sent.size() - 2))));
  CHECK((resp.status_code) == (http_status_code::SWITCHING_PROTOCOLS));

  const auto accept = resp.headers.get("Sec-Websocket-Accept");
  REQUIRE((accept));
  CHECK((*accept) == (expected_accept_key));
}

#pragma endregion
#pragma region DrainBeforeUpgrade

// `handle_drain` before `handle_data` has been called returns `release`.
TEST_CASE("WebSocketTransaction_DrainBeforeUpgrade",
    "[WebSocketTransaction]") {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());
  http_transaction::send_fn send_fn{[](any_strings&&) { return true; }};
  CHECK((tx->handle_drain(send_fn)) == (stream_claim::release));
}

#pragma endregion
#pragma region BadMethod

// Non-GET method: `handle_data` returns `release`.
TEST_CASE("WebSocketTransaction_BadMethod", "[WebSocketTransaction]") {
  auto req = wstx_make_upgrade_req();
  req.method = http_method::POST;
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  CHECK((tx->handle_data(view)) == (stream_claim::release));
}

#pragma endregion
#pragma region MissingUpgrade

// No `Upgrade` option: `handle_data` returns `release`.
TEST_CASE("WebSocketTransaction_MissingUpgrade", "[WebSocketTransaction]") {
  auto req = wstx_make_upgrade_req();
  req.options.upgrade = std::nullopt;
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  CHECK((tx->handle_data(view)) == (stream_claim::release));
}

#pragma endregion
#pragma region MissingConnection

// Missing `Connection` header: `handle_data` returns `release`.
TEST_CASE("WebSocketTransaction_MissingConnection", "[WebSocketTransaction]") {
  auto req = wstx_make_upgrade_req();
  req.headers.remove_key("Connection");
  wstx_reextract_options(req);
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  CHECK((tx->handle_data(view)) == (stream_claim::release));
}

#pragma endregion
#pragma region WrongVersion

// Wrong `Sec-Websocket-Version`: `handle_data` returns `release`.
TEST_CASE("WebSocketTransaction_WrongVersion", "[WebSocketTransaction]") {
  auto req = wstx_make_upgrade_req();
  req.headers.remove_key("Sec-Websocket-Version");
  (void)req.headers.add_raw("Sec-Websocket-Version", "8");
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  CHECK((tx->handle_data(view)) == (stream_claim::release));
}

#pragma endregion
#pragma region MissingKey

// Missing `Sec-Websocket-Key`: `handle_data` returns `release`.
TEST_CASE("WebSocketTransaction_MissingKey", "[WebSocketTransaction]") {
  auto req = wstx_make_upgrade_req();
  req.headers.remove_key("Sec-Websocket-Key");
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  CHECK((tx->handle_data(view)) == (stream_claim::release));
}

#pragma endregion
#pragma region UpgradeRequiredDrain

// Wrong `Sec-Websocket-Version`: drain sends 426 with the required headers and
// `close_after` stays `keep_alive` so the connection remains open for retry.
TEST_CASE("WebSocketTransaction_UpgradeRequiredDrain",
    "[WebSocketTransaction]") {
  auto req = wstx_make_upgrade_req();
  req.headers.remove_key("Sec-Websocket-Version");
  (void)req.headers.add_raw("Sec-Websocket-Version", "8");
  // Do not call wstx_reextract_options: `Sec-Websocket-Version` is read
  // directly from headers (not stored in options), so no re-extraction is
  // needed, and re-extracting would clobber `options.upgrade` since
  // `generate_upgrade_request` sets it directly without a header.
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    REQUIRE((tx->handle_data(view)) == (stream_claim::release));
  }

  std::string sent;
  http_transaction::send_fn send_fn{[&](any_strings&& data) {
    sent = std::get<std::string>(std::move(data));
    return true;
  }};
  CHECK((tx->handle_drain(send_fn)) == (stream_claim::release));
  REQUIRE_FALSE((sent.empty()));

  response_head resp;
  REQUIRE((resp.parse(sent.substr(0, sent.size() - 2))));
  CHECK((resp.status_code) == (http_status_code::UPGRADE_REQUIRED));

  const auto upgrade = resp.headers.get("Upgrade");
  REQUIRE((upgrade));
  CHECK((*upgrade) == ("websocket"));

  const auto connection = resp.headers.get("Connection");
  REQUIRE((connection));
  CHECK((*connection) == ("Upgrade"));

  const auto version = resp.headers.get("Sec-Websocket-Version");
  REQUIRE((version));
  CHECK((*version) == ("13"));

  CHECK((tx->close_after) == (after_response::keep_alive));
}

#pragma endregion
#pragma region BadRequestDrain

// After a rejected upgrade, `handle_drain` sends the 400 error and returns
// `release`.
TEST_CASE("WebSocketTransaction_BadRequestDrain", "[WebSocketTransaction]") {
  auto req = wstx_make_upgrade_req();
  req.method = http_method::POST;
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    REQUIRE((tx->handle_data(view)) == (stream_claim::release));
  }

  std::string sent;
  http_transaction::send_fn send_fn{[&](any_strings&& data) {
    sent = std::get<std::string>(std::move(data));
    return true;
  }};
  CHECK((tx->handle_drain(send_fn)) == (stream_claim::release));
  REQUIRE_FALSE((sent.empty()));

  response_head resp;
  REQUIRE((resp.parse(sent.substr(0, sent.size() - 2))));
  CHECK((resp.status_code) == (http_status_code::BAD_REQUEST));
}

#pragma endregion
#pragma region FeedAfterUpgrade

// After upgrade, a text frame fires `on_message` and `handle_data` returns
// `claim`.
TEST_CASE("WebSocketTransaction_FeedAfterUpgrade", "[WebSocketTransaction]") {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());

  std::string got_msg;
  ws_frame_control got_op{};
  tx->websocket().on_message =
      [&](http_websocket&, std::string&& p, ws_frame_control op) {
        got_msg = std::move(p);
        got_op = op;
        return true;
      };
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    REQUIRE((tx->handle_data(view)) == (stream_claim::claim));
  }
  http_transaction::send_fn send_fn{[](any_strings&&) { return true; }};
  REQUIRE((tx->handle_drain(send_fn)) == (stream_claim::claim));

  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "hello", 0);
  auto view2 = wstx_make_view(buf, frame);
  CHECK((tx->handle_data(view2)) == (stream_claim::claim));
  CHECK((got_msg) == ("hello"));
  CHECK((got_op) == (ws_frame_control::text));
}

#pragma endregion
#pragma region FeedProtocolError

// After upgrade, a protocol-error frame causes `handle_data` to keep the
// stream claimed, latch close-pending, and let `handle_drain` begin graceful
// shutdown.
TEST_CASE("WebSocketTransaction_FeedProtocolError", "[WebSocketTransaction]") {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    REQUIRE((tx->handle_data(view)) == (stream_claim::claim));
  }
  http_transaction::send_fn send_fn{[](any_strings&&) { return true; }};
  REQUIRE((tx->handle_drain(send_fn)) == (stream_claim::claim));

  // A continuation frame with no prior start fragment is a protocol error.
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "data", 0);
  auto view2 = wstx_make_view(buf, frame);
  CHECK((tx->handle_data(view2)) == (stream_claim::claim));
  CHECK((tx->websocket().is_close_pending()));
  CHECK((tx->handle_drain(send_fn)) == (stream_claim::release));
}

#pragma endregion
#pragma region SubprotocolNegotiation

// When `on_protocol` is set and the client offers subprotocols, the chosen
// protocol appears in the 101 response. When the client offers no protocols,
// the callback is not invoked and the response has no protocol header.
TEST_CASE("WebSocketTransaction_SubprotocolNegotiation",
    "[WebSocketTransaction]") {
  // Case 1: client offers "chat, superchat"; callback picks "chat".
  {
    auto req = wstx_make_upgrade_req();
    (void)req.headers.add_raw("Sec-Websocket-Protocol", "chat, superchat");
    auto tx = std::make_shared<http_websocket_transaction>(std::move(req));

    bool called{};
    tx->on_protocol = [&](std::string_view offered) -> std::string {
      called = true;
      CHECK((offered) == ("chat, superchat"));
      return "chat";
    };

    recv_buffer buf;
    {
      auto view = wstx_make_view(buf);
      REQUIRE((tx->handle_data(view)) == (stream_claim::claim));
    }

    std::string sent;
    http_transaction::send_fn send_fn{[&](any_strings&& data) {
      sent = std::get<std::string>(std::move(data));
      return true;
    }};
    REQUIRE((tx->handle_drain(send_fn)) == (stream_claim::claim));
    CHECK((called));

    response_head resp;
    REQUIRE((resp.parse(sent.substr(0, sent.size() - 2))));
    CHECK((resp.status_code) == (http_status_code::SWITCHING_PROTOCOLS));
    const auto proto = resp.headers.get("Sec-Websocket-Protocol");
    REQUIRE((proto));
    CHECK((*proto) == ("chat"));
  }

  // Case 2: client sends no protocol header; callback is not invoked and the
  // response has no `Sec-Websocket-Protocol` header.
  {
    auto tx =
        std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());

    bool called{};
    tx->on_protocol = [&](std::string_view) -> std::string {
      called = true;
      return "chat";
    };

    recv_buffer buf;
    {
      auto view = wstx_make_view(buf);
      REQUIRE((tx->handle_data(view)) == (stream_claim::claim));
    }

    std::string sent;
    http_transaction::send_fn send_fn{[&](any_strings&& data) {
      sent = std::get<std::string>(std::move(data));
      return true;
    }};
    REQUIRE((tx->handle_drain(send_fn)) == (stream_claim::claim));
    CHECK_FALSE((called));

    response_head resp;
    REQUIRE((resp.parse(sent.substr(0, sent.size() - 2))));
    CHECK((resp.status_code) == (http_status_code::SWITCHING_PROTOCOLS));
    CHECK_FALSE((resp.headers.get("Sec-Websocket-Protocol")));
  }
}

#pragma endregion
#pragma region MakeFactory

// `make_factory` creates an `http_websocket_transaction` and invokes the
// configure callback.
TEST_CASE("WebSocketTransaction_MakeFactory", "[WebSocketTransaction]") {
  bool configured{};
  auto factory = http_websocket_transaction::make_factory(
      [&](http_websocket_transaction& wstx) {
        wstx.websocket().on_message =
            [](http_websocket&, std::string&&, ws_frame_control) {
              return true;
            };
        configured = true;
        return true;
      });

  auto tx = factory(wstx_make_upgrade_req());
  REQUIRE((tx));
  CHECK((configured));

  recv_buffer buf;
  auto view = wstx_make_view(buf);
  CHECK((tx->handle_data(view)) == (stream_claim::claim));
}

#pragma endregion
#pragma region PingCounter

// Verify `send_ping` uses an auto-incrementing 4-byte counter payload and that
// a matching pong fires `on_pong` while a mismatched pong does not.
TEST_CASE("WebSocket_PingCounter", "[WebSocket]") {
  using namespace std::chrono_literals;

  std::string sent_frame;
  http_websocket ws{
      [&](any_strings&& f) {
        sent_frame = std::get<std::string>(std::move(f));
        return true;
      },
      connection_role::server};

  bool pong_fired{};
  ws.on_pong = [&](http_websocket&) {
    pong_fired = true;
    return true;
  };

  // First ping: counter becomes 1, payload is {0,0,0,1}.
  CHECK_FALSE((ws.pong_pending()));
  REQUIRE((ws.send_ping()));
  CHECK((ws.pong_pending()));
  REQUIRE_FALSE((sent_frame.empty()));

  ws_frame_view hdr1{sent_frame};
  REQUIRE((hdr1.is_complete()));
  REQUIRE((hdr1.parse()));
  CHECK((hdr1.opcode()) == (ws_frame_control::ping));
  CHECK((hdr1.payload_length()) == (4ULL));
  // Payload encodes counter value 1 big-endian.
  std::string_view p1 =
      std::string_view{sent_frame}.substr(hdr1.header_length(), 4);
  CHECK((static_cast<uint8_t>(p1[3])) == (1U));

  // Feed a masked pong with a wrong payload: `on_pong` must not fire.
  // Server-mode ws requires masked client frames (RFC 6455 section 5.3).
  std::string bad_pong = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::pong, "xxxx", 0x12345678U);
  std::string_view bad_sv{bad_pong};
  CHECK((ws.feed(bad_sv)) != (http_websocket::insatiable));
  CHECK_FALSE((pong_fired));
  CHECK((ws.pong_pending())); // still pending

  // Feed a masked pong with the correct 4-byte counter payload.
  std::string good_pong = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::pong, p1.substr(0, 4),
      0x12345678U);
  std::string_view good_sv{good_pong};
  CHECK((ws.feed(good_sv)) != (http_websocket::insatiable));
  CHECK((pong_fired));
  CHECK_FALSE((ws.pong_pending()));

  // Second ping: counter becomes 2.
  pong_fired = false;
  REQUIRE((ws.send_ping()));
  ws_frame_view hdr2{sent_frame};
  REQUIRE((hdr2.is_complete()));
  REQUIRE((hdr2.parse()));
  std::string_view p2 =
      std::string_view{sent_frame}.substr(hdr2.header_length(), 4);
  CHECK((static_cast<uint8_t>(p2[3])) == (2U));
}

#pragma endregion
#pragma region WebSocket_Keepalive

// Verify that a live WebSocket server sends periodic pings when keepalive is
// enabled and that a client responding with matching pongs keeps the
// connection open. Uses 100 ms intervals so the test runs quickly.
TEST_CASE("HttpServer_WebSocket_Keepalive", "[HttpServer]") {
  if (is_codex()) return;

  using namespace std::chrono_literals;

  epoll_loop_runner loop_runner;
  timing_wheel_runner wheel_runner;

  auto server = http_server::create(
      net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [loop = s.loop(), wheel = s.wheel()](
                    http_websocket_transaction& tx) {
                  return tx.enable_keepalive(loop, wheel, 100ms, 100ms);
                }));
      },
      loop_runner.loop()->self(), wheel_runner.wheel(),
      /*request_timeout=*/0s, /*write_timeout=*/0s);
  REQUIRE((server));

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE((client));

  // Perform WebSocket upgrade.
  std::string accept_key;
  http_transaction::send_fn send_fn{[&](any_strings&& f) {
    return client.send(std::get<std::string>(f));
  }};
  http_websocket ws_client{std::move(send_fn), connection_role::client};
  auto req = http_websocket::generate_upgrade_request("/ws", accept_key);
  (void)req.headers.add_raw("Host", "localhost");
  REQUIRE((client.send(req.serialize())));

  const auto resp_wire = client.recv_until("\r\n\r\n");
  REQUIRE_FALSE((resp_wire.empty()));
  auto resp_sv = std::string_view{resp_wire};
  resp_sv.remove_suffix(2);
  response_head resp;
  REQUIRE((resp.parse(resp_sv)));
  CHECK((resp.status_code) == (http_status_code::SWITCHING_PROTOCOLS));

  // Respond to several pings from the server; verify connection stays open.
  int pings_answered{};
  bool got_close{};
  ws_client.on_close = [&](http_websocket&, uint16_t, std::string_view) {
    got_close = true;
  };

  // Loop: receive frames from server, answer pings, stop after 3 answers.
  // Each recv() blocks for up to the connect timeout (1 s); pings arrive
  // every 100 ms so each iteration completes quickly.
  while (pings_answered < 3 && !got_close) {
    auto chunk = client.recv();
    if (chunk.empty()) break;
    std::string_view sv{chunk};
    while (!sv.empty()) {
      ws_frame_view hdr{sv.data(), sv.size()};
      if (!hdr.is_complete() || !hdr.parse()) break;
      if (hdr.opcode() == ws_frame_control::ping) {
        // Echo the payload back as a pong.
        std::string_view payload =
            sv.substr(hdr.header_length(), hdr.payload_length());
        CHECK((ws_client.send_pong(payload)));
        ++pings_answered;
      }
      sv.remove_prefix(hdr.total_length());
    }
  }

  CHECK((pings_answered) >= (3));
  CHECK_FALSE((got_close));

  // Clean close: client initiates, waits for server echo so the connection
  // is fully torn down before the test's `server` shared_ptr goes out of
  // scope. Without this, the 4th ping timer fires while `server` (captured
  // raw in the send_fn lambda) has already been destroyed, causing UB.
  CHECK((ws_client.send_close(1000, "")));
  while (!ws_client.is_close_pending()) {
    auto chunk = client.recv();
    if (chunk.empty()) break;
    std::string_view sv{chunk};
    (void)ws_client.feed(sv);
  }
}

#pragma endregion
#pragma region WebSocket_KeepaliveTimeout

// Verify that the server closes the connection with code 1001 when the client
// ignores pings and the pong timeout expires.
TEST_CASE("HttpServer_WebSocket_KeepaliveTimeout", "[HttpServer]") {
  if (is_codex()) return;

  using namespace std::chrono_literals;

  epoll_loop_runner loop_runner;
  timing_wheel_runner wheel_runner;

  auto server = http_server::create(
      net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [loop = s.loop(), wheel = s.wheel()](
                    http_websocket_transaction& tx) {
                  return tx.enable_keepalive(loop, wheel, 100ms, 100ms);
                }));
      },
      loop_runner.loop()->self(), wheel_runner.wheel(),
      /*request_timeout=*/0s, /*write_timeout=*/0s);
  REQUIRE((server));

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE((client));

  // Perform WebSocket upgrade.
  std::string accept_key;
  http_transaction::send_fn send_fn{[&](any_strings&& f) {
    return client.send(std::get<std::string>(f));
  }};
  http_websocket ws_client{std::move(send_fn), connection_role::client};
  auto req = http_websocket::generate_upgrade_request("/ws", accept_key);
  (void)req.headers.add_raw("Host", "localhost");
  REQUIRE((client.send(req.serialize())));

  const auto resp_wire = client.recv_until("\r\n\r\n");
  REQUIRE_FALSE((resp_wire.empty()));
  auto resp_sv = std::string_view{resp_wire};
  resp_sv.remove_suffix(2);
  response_head resp;
  REQUIRE((resp.parse(resp_sv)));
  CHECK((resp.status_code) == (http_status_code::SWITCHING_PROTOCOLS));

  // Do not respond to pings; wait for the server to close with code 1001.
  // The close arrives ~200 ms after upgrade (100 ms ping + 100 ms pong
  // timeout). The connect timeout (1 s) gives enough headroom.
  //
  // Note: do NOT call ws_client.feed() here -- that would auto-pong every
  // ping, defeating the purpose of this test. Parse frames manually and
  // discard pings without replying.
  uint16_t got_close_code{};
  while (got_close_code == 0) {
    auto chunk = client.recv();
    if (chunk.empty()) break; // recv() timed out or EOF
    std::string_view sv{chunk};
    while (!sv.empty()) {
      ws_frame_view hdr{sv.data(), sv.size()};
      if (!hdr.is_complete() || !hdr.parse()) break;
      if (hdr.opcode() == ws_frame_control::close) {
        std::string_view payload =
            sv.substr(hdr.header_length(), hdr.payload_length());
        got_close_code =
            (payload.size() >= 2)
                ? static_cast<uint16_t>(
                      (static_cast<uint8_t>(payload[0]) << 8) |
                      static_cast<uint8_t>(payload[1]))
                : 1000U;
      }
      // Pings are intentionally ignored (no pong sent).
      sv.remove_prefix(hdr.total_length());
    }
  }

  CHECK((got_close_code) == (1001U));
}

#pragma endregion
#pragma region WebSocket

// Semi-integration test: `http_server` with `http_websocket_transaction` as
// the route handler; client uses `stream_sync` for I/O and `http_websocket`
// for WebSocket framing.
//
// Flow:
//   1. Server registers an echo handler under `"/ws"`.
//   2. Client sends an HTTP/1.1 upgrade request and receives 101.
//   3. Client sends a masked text frame; server echoes it back unmasked.
//   4. Client decodes the echo and verifies the payload.
TEST_CASE("HttpServer_WebSocket", "[HttpServer]") {
  if (is_codex()) return;

  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [](http_websocket_transaction& tx) {
                  tx.websocket().on_message =
                      [](http_websocket& ws, std::string&& p,
                          ws_frame_control) { return ws.send_text(p); };
                  return true;
                }));
      });
  REQUIRE((server));

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE((client));

  // Send a valid HTTP/1.1 WebSocket upgrade request. The RFC 6455 test-vector
  // key produces accept value `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`.
  REQUIRE((client.send(
      "GET /ws/ HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-Websocket-Version: 13\r\n"
      "Sec-Websocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "\r\n")));

  // Receive and verify the 101 Switching Protocols response.
  const auto resp_wire = client.recv_until("\r\n\r\n");
  REQUIRE_FALSE((resp_wire.empty()));
  auto resp_head_wire = std::string_view{resp_wire};
  REQUIRE((resp_head_wire.size()) >= (2U));
  resp_head_wire.remove_suffix(2);
  response_head resp;
  REQUIRE((resp.parse(resp_head_wire)));
  CHECK((resp.status_code) == (http_status_code::SWITCHING_PROTOCOLS));
  const auto accept = resp.headers.get("Sec-Websocket-Accept");
  REQUIRE((accept));
  CHECK((*accept) == ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

  // Build a client-side WebSocket pump that writes through `client`.
  std::string got_msg;
  ws_frame_control got_op{};
  http_transaction::send_fn client_send{[&](any_strings&& frame) {
    return client.send(std::get<std::string>(frame));
  }};
  http_websocket ws_client{std::move(client_send), connection_role::client};
  ws_client.on_message =
      [&](http_websocket&, std::string&& p, ws_frame_control op) {
        got_msg = std::move(p);
        got_op = op;
        return true;
      };

  // Send a masked text frame; server echoes it back unmasked.
  REQUIRE((ws_client.send_text("hello")));

  // On loopback, the echo arrives in a single recv.
  const auto echo = client.recv();
  REQUIRE_FALSE((echo.empty()));
  std::string_view echo_sv{echo};
  CHECK((ws_client.feed(echo_sv)) == (0ULL));
  CHECK((got_msg) == ("hello"));
  CHECK((got_op) == (ws_frame_control::text));
}

#pragma endregion
#pragma region WebSocket_QueryAndFragmentRoute

// Query strings and fragments must not affect route matching; only the target
// path determines the registered `base_path`.
TEST_CASE("HttpServer_WebSocket_QueryAndFragmentRoute", "[HttpServer]") {
  if (is_codex()) return;

  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [](http_websocket_transaction& tx) {
                  tx.websocket().on_message =
                      [](http_websocket& ws, std::string&& p,
                          ws_frame_control) { return ws.send_text(p); };
                  return true;
                }));
      });
  REQUIRE((server));

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE((client));

  REQUIRE((client.send(
      "GET /ws?token=abc#frag HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-Websocket-Version: 13\r\n"
      "Sec-Websocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "\r\n")));

  const auto resp_wire = client.recv_until("\r\n\r\n");
  REQUIRE_FALSE((resp_wire.empty()));
  auto resp_head_wire = std::string_view{resp_wire};
  REQUIRE((resp_head_wire.size()) >= (2U));
  resp_head_wire.remove_suffix(2);
  response_head resp;
  REQUIRE((resp.parse(resp_head_wire)));
  CHECK((resp.status_code) == (http_status_code::SWITCHING_PROTOCOLS));
  const auto accept = resp.headers.get("Sec-Websocket-Accept");
  REQUIRE((accept));
  CHECK((*accept) == ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

#pragma endregion
#pragma region WebSocket_Frames

// Semi-integration test exercising four frame-sequence scenarios against a
// live `http_server` with an `http_websocket_transaction` echo route.
//
// The upgrade request is built with `generate_upgrade_request` so the
// random client key and its derived accept value are both verified.
//
// Scenarios:
//   1. Single text frame echoed back.
//   2. Two-fragment message ("hel" + "lo") reassembled to "hello".
//   3. Three-fragment message interleaved with a ping (triggers auto-pong)
//      and a client-originated pong (silently absorbed); reassembled echo
//      returned after the pong.
//   4. Close frame (code 1001): server mirrors the code; `on_close` fires.
TEST_CASE("HttpServer_WebSocket_Frames", "[HttpServer]") {
  if (is_codex()) return;

  // Server echoes text messages and mirrors close frames.
  auto web_server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [](http_websocket_transaction& tx) {
                  tx.websocket().on_message =
                      [](http_websocket& ws, std::string&& p,
                          ws_frame_control) { return ws.send_text(p); };
                  return true;
                }));
      });
  REQUIRE((web_server));

  auto client = stream_sync::connect(web_server->local_endpoint(), 1s);
  REQUIRE((client));

  http_transaction::send_fn client_send{[&](any_strings&& frame) {
    return client.send(std::get<std::string>(frame));
  }};
  http_websocket ws_client{std::move(client_send), connection_role::client};

  std::string got_msg;
  ws_frame_control got_op{};
  uint16_t got_close_code{};
  ws_client.on_message =
      [&](http_websocket&, std::string&& p, ws_frame_control op) {
        got_msg = std::move(p);
        got_op = op;
        return true;
      };
  ws_client.on_close = [&](http_websocket&, uint16_t code, std::string_view) {
    got_close_code = code;
  };

  // Build the upgrade request via `generate_upgrade_request`; the method
  // returns the `request_head` and stores the expected accept key.
  std::string accept_key;
  auto req = http_websocket::generate_upgrade_request("/ws", accept_key);
  (void)req.headers.add_raw("Host", "localhost");
  REQUIRE((client.send(req.serialize())));

  // Verify the response.
  const auto resp_wire = client.recv_until("\r\n\r\n");
  REQUIRE_FALSE((resp_wire.empty()));
  auto resp_head_wire = std::string_view{resp_wire};
  REQUIRE((resp_head_wire.size()) >= (2U));
  resp_head_wire.remove_suffix(2);
  response_head resp;
  REQUIRE((resp.parse(resp_head_wire)));
  CHECK((resp.status_code) == (http_status_code::SWITCHING_PROTOCOLS));
  const auto accept = resp.headers.get("Sec-Websocket-Accept");
  REQUIRE((accept));
  CHECK((*accept) == (accept_key));

  // Receive chunks from the socket and feed them through `ws_client` until
  // `on_message` fires.  Handles both single-recv and split-recv delivery.
  const auto recv_msg = [&]() {
    got_msg.clear();
    while (got_msg.empty()) {
      auto chunk = client.recv();
      if (chunk.empty()) break;
      std::string_view sv{chunk};
      (void)ws_client.feed(sv);
    }
  };

  // Case 1: single text frame.
  REQUIRE((ws_client.send_text("hello")));
  recv_msg();
  CHECK((got_msg) == ("hello"));
  CHECK((got_op) == (ws_frame_control::text));

  // Case 2: two-fragment text message.
  //   Fragment 1: FIN=0, text opcode, "hel"
  //   Fragment 2: FIN=1, continuation, "lo"
  //   -> server reassembles and echoes "hello"
  REQUIRE((ws_client.send_frame(ws_frame_control::text, "hel")));
  REQUIRE((ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "lo")));
  recv_msg();
  CHECK((got_msg) == ("hello"));

  // Case 3: three fragments interleaved with a ping and a pong.
  //   FIN=0 text       "foo"     -> accumulate
  //   FIN=1 ping       "payload" -> server auto-pongs to client
  //   FIN=0 cont       "bar"     -> accumulate
  //   FIN=1 pong       "payload" -> silently absorbed by server
  //   FIN=1 cont       "baz"     -> assemble "foobarbaz", echo
  //
  // `recv_msg` feeds chunks until `on_message` fires, transparently
  // consuming any pong frames that arrive before the echo.
  REQUIRE((ws_client.send_frame(ws_frame_control::text, "foo")));
  REQUIRE((ws_client.send_frame(ws_frame_control::fin | ws_frame_control::ping,
      "payload")));
  REQUIRE((ws_client.send_frame(ws_frame_control::continuation, "bar")));
  REQUIRE((ws_client.send_pong("payload")));
  REQUIRE((ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "baz")));
  recv_msg();
  CHECK((got_msg) == ("foobarbaz"));

  // Case 4: close frame (code 1001).
  //   Client sends close; server `on_close` mirrors the code back.
  //   Client feeds received data until its `on_close` fires.
  REQUIRE((ws_client.send_close(1001, "done")));
  while (got_close_code == 0) {
    auto chunk = client.recv();
    if (chunk.empty()) break;
    std::string_view sv{chunk};
    (void)ws_client.feed(sv);
  }
  CHECK((got_close_code) == (uint16_t{1001}));
}

#pragma endregion

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
