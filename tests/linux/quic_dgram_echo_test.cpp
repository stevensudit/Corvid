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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

#include "corvid/containers/core/opt_find.h"
#include "corvid/proto/io_uring/iou_loop.h"
#include "corvid/proto/iov_queue.h"
#include "corvid/proto/quic/quic_conn.h"
#include "corvid/proto/quic/quic_dgram_plugins.h"
#include "corvid/proto/quic/quic_echo_plugin.h"
#include "corvid/proto/quic/quic_self_signed_cert.h"
#include "corvid/proto/quic/quic_ssl_ctx.h"

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::iouring;
using namespace corvid::proto::quic;
using namespace std::chrono_literals;

namespace {

bool WaitFor(const auto& pred, std::chrono::milliseconds timeout = 1000ms) {
#ifdef DEBUG
  timeout = 1h;
#endif
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return pred();
}

constexpr std::string_view echo_alpn = "corvid-echo";

using send_queue_t = iov_queue<std::vector<uint8_t>, write_stream_flags>;

// Client-side upper plugin for the bilateral echo test. Drives an outbound
// `iov_queue` per stream the test asks to send on, accumulates inbound bytes
// (without echoing back, unlike the server), and uses the same unified drain
// loop the echo plugin uses.
//
// `inject` is the test API: queue bytes (and optionally `fin`) on a stream.
// Loop-thread only; the caller wraps in `post_and_wait` from the test
// thread.
struct echo_client_plugin: quic_conn_handlers {
  explicit echo_client_plugin(quic_session_io& s) noexcept : io_{s} {}

  [[nodiscard]] bool on_app_tx_ready() noexcept override {
    app_tx_ready.store(true, std::memory_order::release);
    return true;
  }

  [[nodiscard]] bool on_handshake_completed() noexcept override {
    handshake_completed.store(true, std::memory_order::release);
    return true;
  }

  // Mirror of the echo plugin's per-stream queue, minus the auto-echo:
  // received bytes are recorded for the test to inspect, not appended for
  // outbound. Allocates on the map insert and the byte-vector grow.
  [[nodiscard]] bool on_recv_stream_data(quic_stream_id stream_id,
      uint64_t /*offset*/, std::span<const uint8_t> data,
      quic_stream_data_flags flags) override {
    // Fail the upcall on demand, so a test can drive the session's
    // hard-error path (read_pkt reports callback_failure).
    if (fail_recv.load(std::memory_order::acquire)) return false;
    {
      std::scoped_lock lock{mu};
      auto& v = received[stream_id];
      v.insert(v.end(), data.begin(), data.end());
    }
    if (bitmask::has(flags, quic_stream_data_flags::fin))
      fins_seen.fetch_add(1, std::memory_order::acq_rel);
    return true;
  }

  [[nodiscard]] bool on_acked_stream_data_offset(quic_stream_id stream_id,
      uint64_t /*offset*/, uint64_t datalen) noexcept override {
    if (auto q = find_opt(queues, stream_id)) q->retire(datalen);
    return true;
  }

  [[nodiscard]] bool on_stream_close(quic_stream_id stream_id,
      std::optional<uint64_t> /*app_error_code*/) noexcept override {
    queues.erase(stream_id);
    return true;
  }

  // Mirror of the echo plugin's drain, including its stream-level status
  // handling.
  //
  // A flow-control-blocked stream is skipped for the turn, a write-shut or
  // unknown one is dropped.
  [[nodiscard]] bool drain(time_point_t now) {
    std::vector<quic_stream_id> blocked;
    for (;;) {
      const auto pick = do_pick_stream(blocked);
      auto out = io_.borrow_send_buffer();
      if (!out) return true;
      uint64_t accepted{};
      const auto status = io_.conn().writev_stream(pick.sid, pick.iov, out,
          accepted, pick.flags, now);
      if (status == quic_status::draining || status == quic_status::closing)
        return true;
      if (status == quic_status::stream_data_blocked) {
        blocked.push_back(pick.sid);
        continue;
      }
      if (status == quic_status::stream_shut_wr ||
          status == quic_status::stream_not_found)
      {
        queues.erase(pick.sid);
        continue;
      }
      if (status != quic_status::ok) return false;
      if (out.payload_bytes().empty()) return true;
      if (pick.qp) {
        pick.qp->consume(accepted);
        if (pick.qp->size() == 0) pick.qp->state() = write_stream_flags::none;
      }
      (void)io_.send_packet(std::move(out));
    }
  }

  struct stream_pick {
    quic_stream_id sid = quic_stream_id::none;
    std::span<const iovec> iov;
    write_stream_flags flags = write_stream_flags::none;
    send_queue_t* qp{};
  };

  [[nodiscard]] stream_pick do_pick_stream(
      std::span<const quic_stream_id> blocked) {
    for (auto& [id, q] : queues) {
      if (q.size() == 0 && q.state() == write_stream_flags::none) continue;
      if (std::ranges::contains(blocked, id)) continue;
      return {id, q.unused(), q.state(), &q};
    }
    return {};
  }

  // Loop-thread API: return `n` bytes of receive credit on `sid`.
  //
  // The plugin deliberately returns no credit on its own, so an echo larger
  // than the initial window blocks the peer deterministically until the test
  // calls this.
  [[nodiscard]] bool credit_stream(quic_stream_id sid, uint64_t n) {
    if (io_.conn().extend_max_stream_offset(sid, n) != quic_status::ok)
      return false;
    io_.conn().extend_max_offset(n);
    return true;
  }

  // Loop-thread API: open a bidi stream and queue `payload` (with FIN) on
  // it. Returns the chosen stream id, or `none` on failure.
  [[nodiscard]] quic_stream_id send_with_fin(std::vector<uint8_t>&& payload) {
    quic_stream_id sid = quic_stream_id::none;
    if (io_.conn().open_bidi_stream(sid) != quic_status::ok)
      return quic_stream_id::none;
    auto& q = queues[sid];
    q.append(std::move(payload));
    q.state() = bitmask::set(q.state(), write_stream_flags::fin);
    return sid;
  }

  std::vector<uint8_t> received_for(quic_stream_id sid) {
    std::scoped_lock lock{mu};
    auto it = received.find(sid);
    if (it == received.end()) return {};
    return it->second;
  }

  quic_session_io& io_;
  std::unordered_map<quic_stream_id, send_queue_t> queues;
  std::mutex mu;
  std::unordered_map<quic_stream_id, std::vector<uint8_t>> received;
  std::atomic_bool handshake_completed{false};
  std::atomic_bool app_tx_ready{false};
  std::atomic_bool fail_recv{false};
  std::atomic_int fins_seen{0};
};

// Two-protocol pair: server uses the echo plugin, client uses the
// test-local recording plugin. They share the router_plugin via the
// `quic_dgram_protocol` template's structure (one protocol per side).
using server_protocol_t = quic_dgram_protocol<quic_echo_plugin>;
using client_protocol_t = quic_dgram_protocol<echo_client_plugin>;

} // namespace

#pragma region Echo

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE(
    "quic_echo_plugin echoes stream bytes (+ FIN) back to a client running "
    "on the same iou_loop_runner",
    "[quic][router][echo]") {
  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx server_tls{ck, echo_alpn};
  REQUIRE(server_tls);
  quic_ssl_ctx client_tls{echo_alpn};
  REQUIRE(client_tls);

  iou_loop_runner runner;

  auto server_router =
      iou_dgram_router_handle<server_protocol_t::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          server_tls);
  CHECK(server_router);
  const auto server_addr = server_router->local_endpoint();
  REQUIRE_FALSE(server_addr.empty());

  auto client_router =
      iou_dgram_router_handle<client_protocol_t::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          client_tls);
  CHECK(client_router);
  REQUIRE_FALSE(client_router->local_endpoint().empty());

  std::shared_ptr<client_protocol_t::session_plugin::session_t> client_sess;
  REQUIRE(runner.loop()->post_and_wait([&]() -> bool {
    client_sess = client_protocol_t::session_plugin::make_client(
        *client_router.pointer(), server_addr, ""); // no SNI
    return client_sess != nullptr;
  }));
  REQUIRE(client_sess);

  auto& client_plugin = client_sess->plugin().protocol_plugin();

  // Wait for both the client's handshake-completed latch and TX-ready
  // latch. App data can only be submitted once the 1-RTT TX key is
  // installed, which `on_app_tx_ready` signals.
  CHECK(WaitFor([&] {
    return client_plugin.handshake_completed.load(
               std::memory_order::acquire) &&
           client_plugin.app_tx_ready.load(std::memory_order::acquire);
  }));

  // Open a bidi stream from the loop thread, queue a short payload with
  // FIN, and let the per-turn drain ship it. Post-and-wait gives us the
  // chosen stream id back synchronously.
  const std::vector<uint8_t> payload{0xde, 0xad, 0xbe, 0xef, 0x00, 0x01, 0x02,
      0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b};
  quic_stream_id sid = quic_stream_id::none;
  REQUIRE(runner.loop()->post_and_wait([&]() -> bool {
    auto payload_copy = payload;
    sid = client_plugin.send_with_fin(std::move(payload_copy));
    if (sid == quic_stream_id::none) return false;
    // Nudge the outbound drain so the first packet ships without waiting
    // for an unrelated inbound to trigger handle_recv.
    return client_plugin.drain(steady_now_clock::now());
  }));
  REQUIRE(sid != quic_stream_id::none);

  // The echo plugin mirrors the FIN once its outbound queue drains, so we
  // wait for both: the full byte-stream match AND at least one FIN. The
  // FIN check guards against partial-echo flukes.
  CHECK(WaitFor([&] {
    if (client_plugin.fins_seen.load(std::memory_order::acquire) == 0)
      return false;
    return client_plugin.received_for(sid) == payload;
  }));
}

#pragma endregion
#pragma region Flow control

TEST_CASE(
    "quic_echo_plugin survives a stream-level flow-control block and resumes "
    "when the peer extends the window",
    "[quic][router][echo]") {
  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx server_tls{ck, echo_alpn};
  REQUIRE(server_tls);
  quic_ssl_ctx client_tls{echo_alpn};
  REQUIRE(client_tls);

  iou_loop_runner runner;

  auto server_router =
      iou_dgram_router_handle<server_protocol_t::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          server_tls);
  CHECK(server_router);
  const auto server_addr = server_router->local_endpoint();
  REQUIRE_FALSE(server_addr.empty());

  auto client_router =
      iou_dgram_router_handle<client_protocol_t::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          client_tls);
  CHECK(client_router);

  std::shared_ptr<client_protocol_t::session_plugin::session_t> client_sess;
  REQUIRE(runner.loop()->post_and_wait([&]() -> bool {
    client_sess = client_protocol_t::session_plugin::make_client(
        *client_router.pointer(), server_addr, ""); // no SNI
    return client_sess != nullptr;
  }));
  REQUIRE(client_sess);

  auto& client_plugin = client_sess->plugin().protocol_plugin();
  CHECK(WaitFor([&] {
    return client_plugin.handshake_completed.load(
               std::memory_order::acquire) &&
           client_plugin.app_tx_ready.load(std::memory_order::acquire);
  }));

  // The payload overruns the 256KB initial stream window (`quic_conn::init`'s
  // transport-param default), and the client plugin returns no receive credit
  // on its own, so the server's echo deterministically blocks at exactly the
  // window.
  constexpr auto stream_window = 262144UZ; // 1U << 18
  constexpr auto payload_size = 400UZ * 1024;
  std::vector<uint8_t> payload(payload_size);
  for (auto ndx = 0UZ; ndx < payload.size(); ++ndx)
    payload[ndx] = static_cast<uint8_t>(ndx);

  quic_stream_id sid = quic_stream_id::none;
  REQUIRE(runner.loop()->post_and_wait([&]() -> bool {
    auto payload_copy = payload;
    sid = client_plugin.send_with_fin(std::move(payload_copy));
    if (sid == quic_stream_id::none) return false;
    return client_plugin.drain(steady_now_clock::now());
  }));
  REQUIRE(sid != quic_stream_id::none);

  // Phase 1: the echo halts at the initial window. Before the drain learned
  // to set a blocked stream aside, hitting the block closed the server
  // session, so nothing more would ever arrive.
  CHECK(WaitFor([&] {
    return client_plugin.received_for(sid).size() == stream_window;
  }));
  CHECK(client_plugin.fins_seen.load(std::memory_order::acquire) == 0);

  // Phase 2: extend the window. The blocked remainder and the mirrored FIN
  // must flow unprompted, driven by the MAX_STREAM_DATA the credit emits.
  REQUIRE(runner.loop()->post_and_wait([&]() -> bool {
    return client_plugin.credit_stream(sid, payload_size);
  }));
  CHECK(WaitFor([&] {
    if (client_plugin.fins_seen.load(std::memory_order::acquire) == 0)
      return false;
    return client_plugin.received_for(sid) == payload;
  }));
}

#pragma endregion
#pragma region Terminal close

TEST_CASE(
    "a handler failure ships a terminal CONNECTION_CLOSE and winds the "
    "session down instead of vanishing",
    "[quic][router][echo]") {
  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx server_tls{ck, echo_alpn};
  REQUIRE(server_tls);
  quic_ssl_ctx client_tls{echo_alpn};
  REQUIRE(client_tls);

  iou_loop_runner runner;

  auto server_router =
      iou_dgram_router_handle<server_protocol_t::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          server_tls);
  CHECK(server_router);
  const auto server_addr = server_router->local_endpoint();
  REQUIRE_FALSE(server_addr.empty());

  auto client_router =
      iou_dgram_router_handle<client_protocol_t::router_plugin>::bind(
          *runner.loop(), net_endpoint::loopback_v4(), shot_type::multi,
          client_tls);
  CHECK(client_router);

  std::shared_ptr<client_protocol_t::session_plugin::session_t> client_sess;
  REQUIRE(runner.loop()->post_and_wait([&]() -> bool {
    client_sess = client_protocol_t::session_plugin::make_client(
        *client_router.pointer(), server_addr, ""); // no SNI
    return client_sess != nullptr;
  }));
  REQUIRE(client_sess);

  auto& client_plugin = client_sess->plugin().protocol_plugin();
  CHECK(WaitFor([&] {
    return client_plugin.handshake_completed.load(
               std::memory_order::acquire) &&
           client_plugin.app_tx_ready.load(std::memory_order::acquire);
  }));

  // Arm the failure, then provoke an echo so the failing upcall fires: the
  // client's read_pkt reports callback_failure, a hard error.
  client_plugin.fail_recv.store(true, std::memory_order::release);
  const std::vector<uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  quic_stream_id sid = quic_stream_id::none;
  REQUIRE(runner.loop()->post_and_wait([&]() -> bool {
    auto payload_copy = payload;
    sid = client_plugin.send_with_fin(std::move(payload_copy));
    if (sid == quic_stream_id::none) return false;
    return client_plugin.drain(steady_now_clock::now());
  }));
  REQUIRE(sid != quic_stream_id::none);

  // The hard error must produce a terminal CONNECTION_CLOSE: the conn enters
  // its closing period and the session survives to wind down over 3*PTO.
  // Before the fix the session simply closed, never entering the period.
  CHECK(WaitFor([&] {
    return runner.loop()->post_and_wait([&]() -> bool {
      return client_sess->plugin().conn().in_close_period();
    });
  }));
  CHECK(client_plugin.fins_seen.load(std::memory_order::acquire) == 0);
}

#pragma endregion
// NOLINTEND(readability-function-cognitive-complexity)
