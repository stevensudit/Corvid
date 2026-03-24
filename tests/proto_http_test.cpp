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

#include <atomic>
#include <chrono>
#include <thread>

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

  epoll_loop_runner client_runner;
  std::string received;
  notifiable<bool> done{false};

  auto client = stream_conn_ptr::connect(client_runner,
      server->local_endpoint(),
      {.on_data =
              [&](stream_conn&, recv_buffer_view v) {
                std::string_view av = v;
                received.append(av);
                v.consume(av.size());
                done.notify_one(true);
                return true;
              },
          .on_drain =
              [sent = false](stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return false;
                return conn.send("GET /\r\n");
              }});
  ASSERT_TRUE(client);
  ASSERT_TRUE(done.wait_for_value(std::chrono::seconds{5}, true));
  EXPECT_NE(received.find("/"), std::string::npos);
}

// Verify that `GET /foo/bar\r\n` echoes the full path in the HTML response.
void HttpServer_GetPath() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  epoll_loop_runner client_runner;
  std::string received;
  notifiable<bool> done{false};

  auto client = stream_conn_ptr::connect(client_runner,
      server->local_endpoint(),
      {.on_data =
              [&](stream_conn&, recv_buffer_view v) {
                std::string_view av = v;
                received.append(av);
                v.consume(av.size());
                done.notify_one(true);
                return true;
              },
          .on_drain =
              [sent = false](stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return false;
                return conn.send("GET /foo/bar\r\n");
              }});
  ASSERT_TRUE(client);
  ASSERT_TRUE(done.wait_for_value(std::chrono::seconds{5}, true));
  EXPECT_NE(received.find("/foo/bar"), std::string::npos);
}

// Verify that a request line that does not start with "GET /" causes the
// server to close the connection without sending a response.
void HttpServer_InvalidRequest() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  epoll_loop_runner client_runner;
  notifiable<bool> closed{false};

  auto client = stream_conn_ptr::connect(client_runner,
      server->local_endpoint(),
      {.on_drain =
              [sent = false](stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return false;
                return conn.send("POST /foo\r\n");
              },
          .on_close =
              [&](stream_conn&) {
                closed.notify_one(true);
                return true;
              }});
  ASSERT_TRUE(client);
  EXPECT_TRUE(closed.wait_for_value(std::chrono::seconds{5}, true));
}

// Verify that a request line exceeding the 8192-byte limit causes the server
// to close the connection.
void HttpServer_TooLongRequest() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  epoll_loop_runner client_runner;
  notifiable<bool> closed{false};

  auto client = stream_conn_ptr::connect(client_runner,
      server->local_endpoint(),
      {.on_drain =
              [sent = false, oversized = std::string(8200, 'x')](
                  stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return false;
                return conn.send(std::move(oversized));
              },
          .on_close =
              [&](stream_conn&) {
                closed.notify_one(true);
                return true;
              }});
  ASSERT_TRUE(client);
  EXPECT_TRUE(closed.wait_for_value(std::chrono::seconds{5}, true));
}

// Verify that a request arriving in two chunks is handled correctly by the
// stateful `terminated_text_parser`.
void HttpServer_PartialRequest() {
  auto server =
      http_server::create(nullptr, net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  epoll_loop_runner client_runner;
  std::string received;
  notifiable<bool> partial_sent{false};
  notifiable<bool> done{false};

  auto client = stream_conn_ptr::connect(client_runner,
      server->local_endpoint(),
      {.on_data =
              [&](stream_conn&, recv_buffer_view v) {
                std::string_view av = v;
                received.append(av);
                v.consume(av.size());
                done.notify_one(true);
                return true;
              },
          .on_drain =
              [&, sent = false](stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return false;
                bool ok = conn.send("GET /partial");
                partial_sent.notify_one(true);
                return ok;
              }});
  ASSERT_TRUE(client);

  // Wait until the first chunk is sent, then pause to let the server process
  // it so that the second chunk exercises the parser's resume path.
  ASSERT_TRUE(partial_sent.wait_for_value(std::chrono::seconds{5}, true));
  std::this_thread::sleep_for(std::chrono::milliseconds{50});

  // `send` is thread-safe; complete the request from the main thread.
  EXPECT_TRUE(client->send("\r\n"));

  ASSERT_TRUE(done.wait_for_value(std::chrono::seconds{5}, true));
  EXPECT_NE(received.find("/partial"), std::string::npos);
}

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(HttpServer_OwnLoop, HttpServer_SharedLoop,
    HttpServer_Create_BadEndpoint, HttpServer_GetRoot, HttpServer_GetPath,
    HttpServer_InvalidRequest, HttpServer_TooLongRequest,
    HttpServer_PartialRequest);
