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

#include <chrono>
#include <memory>
#include <string_view>
#include <thread>

#include "../corvid/proto/io_uring/iou_loop.h"
#include "../corvid/proto/quic/http3_plugins.h"
#include "../corvid/proto/quic/quic_conn.h"
#include "../corvid/proto/quic/quic_dgram_plugins.h"
#include "../corvid/proto/quic/quic_self_signed_cert.h"
#include "../corvid/proto/quic/quic_ssl_ctx.h"

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

// RFC 9114 ALPN for HTTP/3.
constexpr std::string_view h3_alpn = "h3";

// Both sides run the HTTP/3 bridge plugin; the router_plugin's role (set by
// its TLS context) decides client vs server behavior.
using protocol_t = quic_dgram_protocol<http3_router>;

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE(
    "http3_router establishes a connection and exchanges SETTINGS over the "
    "live iou_dgram_router",
    "[quic][router][http3]") {
  self_signed_cert ck;
  REQUIRE(ck);
  quic_ssl_ctx server_tls{ck, h3_alpn};
  REQUIRE(server_tls);
  quic_ssl_ctx client_tls{h3_alpn};
  REQUIRE(client_tls);

  iou_loop_runner runner;

  auto server_router =
      iou_dgram_router_handle<protocol_t::router_plugin>::bind(*runner.loop(),
          net_endpoint::loopback_v4(), shot_type::multi, server_tls);
  CHECK(server_router);
  const auto server_addr = server_router->local_endpoint();
  REQUIRE_FALSE(server_addr.empty());

  auto client_router =
      iou_dgram_router_handle<protocol_t::router_plugin>::bind(*runner.loop(),
          net_endpoint::loopback_v4(), shot_type::multi, client_tls);
  CHECK(client_router);
  REQUIRE_FALSE(client_router->local_endpoint().empty());

  std::shared_ptr<protocol_t::session_plugin::session_t> client_sess;
  REQUIRE(runner.loop()->post_and_wait([&]() -> bool {
    client_sess = protocol_t::session_plugin::make_client(
        *client_router.pointer(), server_addr);
    return client_sess != nullptr;
  }));
  REQUIRE(client_sess);

  auto& client_plugin = client_sess->plugin().protocol_plugin();

  // The client sees the server's SETTINGS only after the handshake completes,
  // both sides reach 1-RTT TX ready and bind their control streams, the server
  // ships SETTINGS, and the client decodes them. That single observable proves
  // the full bridge end to end (stream opening, the nghttp3 -> ngtcp2 drain,
  // and the ngtcp2 -> nghttp3 read path) on both roles. `peer_settings_` is
  // loop-thread-only, so read it through the loop.
  CHECK(WaitFor([&] {
    return runner.loop()->post_and_wait([&]() -> bool {
      return client_plugin.has_peer_settings();
    });
  }));
}
// NOLINTEND(readability-function-cognitive-complexity)
