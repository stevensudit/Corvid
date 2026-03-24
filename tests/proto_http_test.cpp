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

#include <chrono>
#include <unistd.h>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

// `http_server` tests.

// Verify that `create` with a null loop starts its own `epoll_loop_runner`.
void HttpServer_OwnLoop() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}

// Verify that `create` with a shared loop stores and uses it.
void HttpServer_SharedLoop() {
  epoll_loop_runner runner;
  auto server =
      http_server::create(runner.loop(), net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}

// Verify that `create` returns null when the listen socket cannot be created
// (e.g., an invalid endpoint).
void HttpServer_Create_BadEndpoint() {
  auto server = http_server::create(nullptr, net_endpoint{});
  EXPECT_FALSE(server);
}

// Verify that `GET /\r\n` produces an HTML response that includes the path.
void HttpServer_GetRoot() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client =
      stream_sync::connect(server->local_endpoint(), std::chrono::seconds{5});
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("/"), std::string::npos);
}

// Verify that `GET /foo/bar\r\n` echoes the full path in the HTML response.
void HttpServer_GetPath() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client =
      stream_sync::connect(server->local_endpoint(), std::chrono::seconds{5});
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /foo/bar\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("/foo/bar"), std::string::npos);
}

// Verify that a request line that does not start with "GET /" causes the
// server to close the connection without sending a response.
void HttpServer_InvalidRequest() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client =
      stream_sync::connect(server->local_endpoint(), std::chrono::seconds{5});
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("POST /foo\r\n"));
  EXPECT_TRUE(client.recv().empty());
}

// Verify that a request line exceeding the 8192-byte limit causes the server
// to close the connection.
void HttpServer_TooLongRequest() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client =
      stream_sync::connect(server->local_endpoint(), std::chrono::seconds{5});
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
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client =
      stream_sync::connect(server->local_endpoint(), std::chrono::seconds{5});
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /partial"));
  EXPECT_TRUE(client.send("\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("/partial"), std::string::npos);
}

// Verify that the server can listen on an ANS (Abstract Name Socket) and
// respond correctly to a `GET` request from a `stream_sync` client.
void HttpServer_ANS() {
  const std::string name =
      "@corvid_proto_http_test." + std::to_string(getpid()) + ".sock";
  const net_endpoint ep{name};
  ASSERT_TRUE(ep.is_ans());

  auto server = http_server::create(nullptr, ep);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(ep, std::chrono::seconds{5});
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /hello\r\n"));
  const auto response = client.recv_until("\r\n");
  EXPECT_NE(response.find("/hello"), std::string::npos);
}

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(HttpServer_OwnLoop, HttpServer_SharedLoop,
    HttpServer_Create_BadEndpoint, HttpServer_GetRoot, HttpServer_GetPath,
    HttpServer_InvalidRequest, HttpServer_TooLongRequest,
    HttpServer_PartialRequest, HttpServer_ANS);
