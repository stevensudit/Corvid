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

#include <cerrno>
#include <chrono>
#include <iostream>
#include <unistd.h>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

// `http_server` tests.

// Verify that `create` with a null loop starts its own `epoll_loop_runner`.
void HttpServer_OwnLoop() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}

// Verify that `create` with a shared loop stores and uses it.
void HttpServer_SharedLoop() {
  epoll_loop_runner runner;
  auto server =
      http_server::create(net_endpoint{ipv4_addr::loopback, 0}, runner.loop());
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}

// Verify that `create` returns null when the listen socket cannot be created
// (e.g., an invalid endpoint).
void HttpServer_Create_BadEndpoint() {
  auto server = http_server::create(net_endpoint{});
  EXPECT_FALSE(server);
}

// Verify that `GET /\r\n` produces an HTML response that includes the path.
void HttpServer_GetRoot() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("/"), std::string::npos);
}

// Verify that `GET /123\r\n` produces an HTML response that includes the
// numeric path component.
void HttpServer_GetPath() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /123\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("123"), std::string::npos);
}

// Verify that a request line that does not start with "GET /" causes the
// server to close the connection without sending a response.
void HttpServer_InvalidRequest() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("POST /foo\r\n"));
  EXPECT_TRUE(client.recv().empty());
}

// Verify that a request line exceeding the 8192-byte limit causes the server
// to close the connection.
void HttpServer_TooLongRequest() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  // Send may fail mid-way if the server closes before all bytes are written;
  // ignore the result and rely on `recv` to observe the close.
  (void)client.send(std::string(8200, 'x'));
  EXPECT_TRUE(client.recv().empty());
}

// Verify that a request arriving in two writes is handled correctly by the
// stateful `terminated_text_parser`. The two writes may or may not be
// coalesced by TCP, but the test verifies correct parsing in either case.
void HttpServer_PartialRequest() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /42"));
  EXPECT_TRUE(client.send("\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("42"), std::string::npos);
}

// Verify that the server can listen on an ANS (Abstract Name Socket) and
// respond correctly to a `GET` request from a `stream_sync` client.
void HttpServer_ANS() {
  const std::string name =
      "@corvid_proto_http_test." + std::to_string(getpid()) + ".sock";
  const net_endpoint ep{name};
  ASSERT_TRUE(ep.is_ans());

  auto server = http_server::create(ep);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(ep, 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /42\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("42"), std::string::npos);
}

// Verify that `create` with a shared `timing_wheel` stores and uses it.
void HttpServer_SharedWheel() {
  timing_wheel_runner wheel;
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      nullptr, wheel.wheel());
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}

// Verify that a normal GET request is served within the timeout window.
void HttpServer_RequestWithinTimeout() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      nullptr, nullptr, 5s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("/"), std::string::npos);
}

// Verify that an idle connection (no request sent) is forcefully closed by
// the server after the request timeout expires.
void HttpServer_IdleTimeout() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      nullptr, nullptr, 100ms);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint());
  ASSERT_TRUE(client);

  // Send nothing. The server should hang up after the 100ms timeout. A
  // watchdog aborts the process if `recv()` blocks for more than 5 seconds.
  jthread_stoppable_sleep sleep;
  std::jthread watchdog([&sleep](std::stop_token st) {
    if (!sleep.until(std::move(st), std::chrono::steady_clock::now() + 5s)) {
      std::cerr << "HttpServer_IdleTimeout: recv() blocked for >5s\n";
      std::abort();
    }
  });
  EXPECT_TRUE(client.recv().empty());
  EXPECT_NE(client.errno_on_close(), EAGAIN);
}

// Verify that the write timeout fires when the client stops reading.
//
// The client requests a 10 MB response but never reads, filling the kernel
// receive buffer and stalling the server's send path. The server should hang
// up the connection after the write timeout expires.
void HttpServer_WriteTimeout() {
  // Use a short write timeout so the test completes quickly. The timing
  // wheel has 100 ms precision, so allow generously for scheduling overhead.
  constexpr auto kWriteTimeout = 500ms;

  epoll_loop_runner loop;
  timing_wheel_runner wheel;

  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      loop.loop(), wheel.wheel(),
      /*request_timeout=*/30s,
      /*write_timeout=*/kWriteTimeout);
  ASSERT_TRUE(server);

  const auto ep = server->local_endpoint();
  ASSERT_TRUE(ep);

  // Connect a client that sends the request but never reads the response.
  // Without an `on_data` handler, `EPOLLIN` is not armed on the client
  // connection, so incoming bytes accumulate in the kernel receive buffer.
  // Once that buffer fills, TCP flow control prevents the server from
  // writing, stalling the drain and triggering the write timeout.
  notifiable<bool> closed{false};
  auto client = stream_conn_ptr::connect(loop.loop(), ep,
      {.on_drain =
              [sent = false](stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return true;
                return conn.send("GET /10000000\r\n");
              },
          .on_close =
              [&closed](stream_conn&) {
                closed.notify_one(true);
                return true;
              }});
  ASSERT_TRUE(client);

  // Allow 10x the write timeout for timing-wheel jitter and system overhead.
  ASSERT_TRUE(closed.wait_for_value(kWriteTimeout * 10, true));
}

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(HttpServer_OwnLoop, HttpServer_SharedLoop,
    HttpServer_Create_BadEndpoint, HttpServer_GetRoot, HttpServer_GetPath,
    HttpServer_InvalidRequest, HttpServer_TooLongRequest,
    HttpServer_PartialRequest, HttpServer_ANS, HttpServer_SharedWheel,
    HttpServer_RequestWithinTimeout, HttpServer_IdleTimeout,
    HttpServer_WriteTimeout);
