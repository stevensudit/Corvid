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

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

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
// Simple padded-page transaction used by `make_test_server`. Responds to GET
// requests with an HTML body that embeds the request path followed by N space
// characters, where N is the leading decimal number in the path (e.g.,
// `"/42"` -> 42 spaces). Returns 400 when the count exceeds 10 MB, and 405
// for non-GET methods.
struct padded_page_transaction: public http_transaction {
  static constexpr size_t max_pad{10ULL * 1024 * 1024};

  explicit padded_page_transaction(request_head&& req)
      : http_transaction{std::move(req)} {}

  [[nodiscard]] stream_claim handle_drain(const send_fn& send_cb) override {
    const auto& req = request_headers;

    if (req.method != http_method::GET) {
      close_after = after_response::close;
      (void)send_cb(response_head::make_error_response(close_after,
          req.version, http_status_code::METHOD_NOT_ALLOWED,
          "Method Not Allowed"));
      return stream_claim::release;
    }

    const size_t pad_count = parse_pad_count(req.target);
    if (pad_count > max_pad) {
      close_after = after_response::close;
      (void)send_cb(response_head::make_error_response(close_after,
          req.version, http_status_code::BAD_REQUEST, "Bad Request"));
      return stream_claim::release;
    }

    std::string body;
    body.reserve(req.target.size() + pad_count + 27);
    body += "<html><body>";
    body += req.target;
    body.append(pad_count, ' ');
    body += "</body></html>";

    if (req.version == http_version::http_0_9) {
      (void)send_cb(std::move(body));
      return stream_claim::release;
    }

    response_headers.version = req.version;
    response_headers.status_code = http_status_code::OK;
    response_headers.reason = "OK";
    response_headers.options.content_type = content_type_value::text_html;
    response_headers.options.content_length = body.size();
    response_headers.options.connection = close_after;

    (void)send_cb(response_headers.serialize());
    (void)send_cb(std::move(body));
    return stream_claim::release;
  }

private:
  [[nodiscard]] static size_t parse_pad_count(std::string_view target) {
    const auto pos = target.find_first_of("0123456789");
    if (pos == std::string_view::npos) return 0;
    size_t count{};
    (void)std::from_chars(target.data() + pos, target.data() + target.size(),
        count);
    return count;
  }
};

// Creates an `http_server` with `padded_page_transaction` registered as the
// `"/"` catch-all route. Forwards all arguments to `http_server::create`.
[[nodiscard]] static http_server::http_server_ptr make_test_server(
    const net_endpoint& endpoint, http_server::epoll_loop_ptr loop = nullptr,
    http_server::timing_wheel_ptr wheel = nullptr,
    http_server::duration_t request_timeout = 30s,
    http_server::duration_t write_timeout = 5s) {
  return http_server::create(
      endpoint,
      [](http_server& s) {
        return s.add_route({"", "/"},
            [](request_head&& req) -> transaction_ptr {
              return std::make_shared<padded_page_transaction>(std::move(req));
            });
      },
      std::move(loop), std::move(wheel), request_timeout, write_timeout);
}

#pragma region Http09

// `http_server` tests.

// Verify that an HTTP/0.9-style request (no version token, no headers)
// receives a response and the server then closes the connection.
void HttpServer_Http09() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /\r\n"));
  const auto response = client.recv_until("</html>");
  EXPECT_EQ(response.find("200"), std::string::npos);
  // HTTP/0.9 never keep-alive; server should close after the response.
  EXPECT_TRUE(client.recv().empty());
}
#pragma endregion
#pragma region LeadingCrlf

// Verify that leading bare CRLFs before the request line are silently
// skipped (RFC 9112 section 2.2) and the request is served normally.
void HttpServer_LeadingCrlf() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(
      client.send("\r\n\r\nGET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
}
#pragma endregion
#pragma region TooManyLeadingCrls

// Verify that more than `max_leading_crls` bare CRLFs before the request
// line cause the server to drop the connection.
void HttpServer_TooManyLeadingCrls() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  // Send 9 bare CRLFs (one more than the limit of 8) with no request line.
  EXPECT_TRUE(client.send("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n"));
  // The server should close the connection without responding.
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_TRUE(response.empty());
}
#pragma endregion
#pragma region OwnLoop

// Verify that `create` with a null loop starts its own `epoll_loop_runner`.
void HttpServer_OwnLoop() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}
#pragma endregion
#pragma region SharedLoop

// Verify that `create` with a shared loop stores and uses it.
void HttpServer_SharedLoop() {
  if (is_codex()) return;

  epoll_loop_runner runner;
  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0},
      runner.loop()->self());
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}
#pragma endregion
#pragma region Create_BadEndpoint

// Verify that `create` returns null when the listen socket cannot be created
// (e.g., an invalid endpoint).
void HttpServer_Create_BadEndpoint() {
  auto server = make_test_server(net_endpoint{});
  EXPECT_FALSE(server);
}
#pragma endregion
#pragma region GetRoot

// Verify that `GET / HTTP/1.1` produces a 200 HTML response.
void HttpServer_GetRoot() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
}
#pragma endregion
#pragma region GetPath

// Verify that `GET /123 HTTP/1.1` produces an HTML response that includes
// the numeric path component.
void HttpServer_GetPath() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /123 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  const auto body = client.recv_until("</html>");
  EXPECT_NE(body.find("123"), std::string::npos);
}
#pragma endregion
#pragma region RouteBasePath

// Verify that `route_base_path` extracts the leading path component from the
// request target path and ignores any query or fragment suffix.
void HttpServer_RouteBasePath() {
  struct test_case {
    std::string_view target;
    std::string_view base_path;
  };

  constexpr test_case cases[]{{"/", "/"}, {"/ws", "/ws"}, {"/ws/", "/ws"},
      {"/ws/chat", "/ws"}, {"/ws?token=abc", "/ws"}, {"/ws#frag", "/ws"},
      {"/ws/chat?token=abc#frag", "/ws"}, {"/?token=abc", "/"},
      {"/#frag", "/"}, {"?token=abc", ""}, {"#frag", ""}, {"", ""}};

  for (const auto& tc : cases)
    EXPECT_EQ(http_server::route_base_path(tc.target), tc.base_path);
}
#pragma endregion
#pragma region InvalidRequest

// Verify that a POST request yields a 405 response (not a silent close).
void HttpServer_InvalidRequest() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("POST /foo HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("405"), std::string::npos);
}
#pragma endregion
#pragma region TooLongRequest

// Verify that a request line exceeding the 8192-byte limit causes the server
// to hang up immediately without sending any response.
void HttpServer_TooLongRequest() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  // Send may fail mid-way if the server closes before all bytes are written;
  // ignore the result and rely on connection close.
  (void)client.send(std::string(8200, 'x'));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_EQ(response.size(), 0ULL);
}
#pragma endregion
#pragma region PartialRequest

// Verify that a request arriving in two writes is handled correctly by the
// stateful `terminated_text_parser`. The two writes may or may not be
// coalesced by TCP, but the test verifies correct parsing in either case.
void HttpServer_PartialRequest() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /42 HTTP/1.1\r\nHost: localhost"));
  EXPECT_TRUE(client.send("\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  const auto body = client.recv_until("</html>");
  EXPECT_NE(body.find("42"), std::string::npos);
}
#pragma endregion
#pragma region ANS

// Verify that the server can listen on an ANS (Abstract Name Socket) and
// respond correctly to a `GET` request from a `stream_sync` client.
void HttpServer_ANS() {
  if (is_codex()) return;

  const std::string name =
      "@corvid_proto_http_test." + std::to_string(getpid()) + ".sock";
  const net_endpoint ep{name};
  ASSERT_TRUE(ep.is_ans());

  auto server = make_test_server(ep);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(ep, 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /42 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  const auto body = client.recv_until("</html>");
  EXPECT_NE(body.find("42"), std::string::npos);
}
#pragma endregion
#pragma region SharedWheel

// Verify that `create` with a shared `timing_wheel` stores and uses it.
void HttpServer_SharedWheel() {
  if (is_codex()) return;

  timing_wheel_runner wheel;
  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      wheel.wheel());
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}
#pragma endregion
#pragma region RequestWithinTimeout

// Verify that a normal GET request is served within the timeout window.
void HttpServer_RequestWithinTimeout() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 5s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
}
#pragma endregion
#pragma region IdleTimeout

// Verify that an idle connection (no request sent) is forcefully closed by
// the server after the request timeout expires.
void HttpServer_IdleTimeout() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 100ms);
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
#pragma endregion
#pragma region WriteTimeout

// Verify that the write timeout fires when the client stops reading.
//
// The client requests a 10 MB response but never reads, filling the kernel
// receive buffer and stalling the server's send path. The server should hang
// up the connection after the write timeout expires.
void HttpServer_WriteTimeout() {
  if (is_codex()) return;

  // Use a short write timeout so the test completes quickly. The timing
  // wheel has 100 ms precision, so allow generously for scheduling overhead.
  constexpr auto kWriteTimeout = 300ms;

  epoll_loop_runner loop;
  timing_wheel_runner wheel;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0},
      loop.loop()->self(), wheel.wheel(),
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
  auto client = stream_conn_ptr::connect(loop.loop()->self(), ep,
      {.on_drain =
              [sent = false](stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return true;
                return conn.send(
                    "GET /10000000 HTTP/1.1\r\nHost: localhost\r\n\r\n"s);
              },
          .on_close =
              [&closed](stream_conn&) {
                closed.notify_one(true);
                return true;
              }});
  ASSERT_TRUE(client);

  // Shrink the client-side receive buffer so that TCP flow control kicks in
  // well before the 10 MB response drains, making the write-timeout path
  // deterministic regardless of kernel autotuning. The kernel doubles the
  // value but the resulting ~8 KB ceiling is still tiny relative to the
  // response size.
  EXPECT_TRUE(client->sock().set_recv_buffer_size(4096));

  const auto start = std::chrono::steady_clock::now();

  // Allow 10x the write timeout for timing-wheel jitter and system overhead.
  ASSERT_TRUE(closed.wait_for_value(kWriteTimeout * 10, true));

  // The connection must not close before the write timeout has had time to
  // fire. If it closes immediately the write-timeout path was not exercised
  // (e.g., the response drained before backpressure engaged).
  EXPECT_GE(std::chrono::steady_clock::now() - start, kWriteTimeout / 2);
}
#pragma endregion
#pragma region MissingHost

// Verify that an HTTP/1.1 request without a `Host` header receives a 400
// response, and the server then closes the connection.
void HttpServer_MissingHost() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET / HTTP/1.1\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("400"), std::string::npos);
  // Server closes after the error response.
  EXPECT_TRUE(client.recv().empty());
}
#pragma endregion
#pragma region KeepAlive

// Verify that a keep-alive connection accepts a second request after the
// first response is received.
void HttpServer_KeepAlive() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  EXPECT_TRUE(client.send("GET /10 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r1 = client.recv_until("\r\n\r\n");
  EXPECT_NE(r1.find("200"), std::string::npos);
  (void)client.recv_until("</html>");

  EXPECT_TRUE(client.send("GET /20 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r2 = client.recv_until("\r\n\r\n");
  EXPECT_NE(r2.find("200"), std::string::npos);
  (void)client.recv_until("</html>");
}
#pragma endregion
#pragma region Pipeline

// Verify that two requests sent back-to-back (before any response is read)
// are both served in order -- the pipelining property.
void HttpServer_Pipeline() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  // Send both requests before reading any response.
  EXPECT_TRUE(client.send(
      "GET /10 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /20 "
      "HTTP/1.1\r\nHost: localhost\r\n\r\n"));

  const auto r1 = client.recv_until("\r\n\r\n");
  EXPECT_NE(r1.find("200"), std::string::npos);
  (void)client.recv_until("</html>");

  const auto r2 = client.recv_until("\r\n\r\n");
  EXPECT_NE(r2.find("200"), std::string::npos);
  (void)client.recv_until("</html>");
}
#pragma endregion
#pragma region ConnectionClose

// Verify that `Connection: close` causes the server to close the connection
// after the response.
void HttpServer_ConnectionClose() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send(
      "GET /5 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  EXPECT_NE(response.find("Connection: close"), std::string::npos);
  (void)client.recv_until("</html>");
  // Server should close after the response.
  EXPECT_TRUE(client.recv().empty());
}
#pragma endregion
#pragma region Http10NoKeepAlive

// Verify that an HTTP/1.0 request (no `Host` header) receives a 200
// response and the server closes the connection (HTTP/1.0 default is close).
void HttpServer_Http10NoKeepAlive() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /5 HTTP/1.0\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  (void)client.recv_until("</html>");
  // HTTP/1.0 default is close; server should close after the response.
  EXPECT_TRUE(client.recv().empty());
}
#pragma endregion
#pragma region BodyTooLarge

// Verify that a path encoding a body size exceeding 10 MB yields a 400.
void HttpServer_BodyTooLarge() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  // 10 * 1024 * 1024 + 1 = 10485761, just over the 10 MB limit.
  EXPECT_TRUE(
      client.send("GET /10485761 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("400"), std::string::npos);
}
#pragma endregion
#pragma region TooLongHeaders

// Verify that a header block exceeding the 8192-byte limit yields a 400
// response and the server closes the connection.
void HttpServer_TooLongHeaders() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  // The header block (everything between the request line and \r\n\r\n)
  // must exceed 8192 bytes. "X-Pad: " (7) + 8192 'a' + "\r\n" (2) = 8201.
  const std::string long_header = "X-Pad: " + std::string(8192, 'a') + "\r\n";
  EXPECT_TRUE(client.send("GET / HTTP/1.1\r\n" + long_header + "\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("400"), std::string::npos);
  EXPECT_TRUE(client.recv().empty()); // server closes after error
}
#pragma endregion
#pragma region MalformedRequestLine

// Verify that a request line with an unrecognized method yields a 400
// response and the server closes the connection.
void HttpServer_MalformedRequestLine() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("BREW /coffee HTTP/1.1\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("400"), std::string::npos);
  EXPECT_TRUE(client.recv().empty()); // server closes after error
}
#pragma endregion
#pragma region Http10KeepAlive

// Verify that an HTTP/1.0 request with `Connection: keep-alive` keeps the
// connection open for a second request.
void HttpServer_Http10KeepAlive() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  EXPECT_TRUE(
      client.send("GET /5 HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
  const auto r1 = client.recv_until("\r\n\r\n");
  EXPECT_NE(r1.find("200"), std::string::npos);
  EXPECT_NE(r1.find("Connection: keep-alive"), std::string::npos);
  (void)client.recv_until("</html>");

  // Connection is still open; second request succeeds.
  EXPECT_TRUE(
      client.send("GET /10 HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
  const auto r2 = client.recv_until("\r\n\r\n");
  EXPECT_NE(r2.find("200"), std::string::npos);
  (void)client.recv_until("</html>");
}
#pragma endregion

MAKE_TEST_LIST(HttpServer_Http09, HttpServer_LeadingCrlf,
    HttpServer_TooManyLeadingCrls, HttpServer_OwnLoop, HttpServer_SharedLoop,
    HttpServer_Create_BadEndpoint, HttpServer_GetRoot, HttpServer_GetPath,
    HttpServer_RouteBasePath, HttpServer_InvalidRequest,
    HttpServer_TooLongRequest, HttpServer_PartialRequest, HttpServer_ANS,
    HttpServer_SharedWheel, HttpServer_RequestWithinTimeout,
    HttpServer_IdleTimeout, HttpServer_WriteTimeout, HttpServer_MissingHost,
    HttpServer_KeepAlive, HttpServer_Pipeline, HttpServer_ConnectionClose,
    HttpServer_Http10NoKeepAlive, HttpServer_BodyTooLarge,
    HttpServer_TooLongHeaders, HttpServer_MalformedRequestLine,
    HttpServer_Http10KeepAlive);

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
