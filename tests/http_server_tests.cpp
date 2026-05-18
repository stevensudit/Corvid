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
// Simple padded-page transaction used by `make_test_server`. Responds to GET
// requests with an HTML body that embeds the request path followed by N space
// characters, where N is the leading decimal number in the path (e.g.,
// `"/42"` -> 42 spaces). Returns 400 when the count exceeds 10 MB, and 405
// for non-GET methods.
struct padded_page_transaction: public epoll_http_transaction {
  static constexpr size_t max_pad{10ULL * 1024 * 1024};

  explicit padded_page_transaction(request_head&& req)
      : epoll_http_transaction{std::move(req)} {}

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

// Creates an `epoll_http_server` with `padded_page_transaction` registered as
// the `"/"` catch-all route. Forwards all arguments to
// `epoll_http_server::create`.
[[nodiscard]] static epoll_http_server::http_server_ptr
make_test_server(const net_endpoint& endpoint,
    epoll_http_server::epoll_loop_ptr loop = nullptr,
    epoll_http_server::timing_wheel_ptr wheel = nullptr,
    epoll_http_server::duration_t request_timeout = 30s,
    epoll_http_server::duration_t write_timeout = 5s) {
  return epoll_http_server::create(
      endpoint,
      [](epoll_http_server& s) {
        return s.add_route({"", "/"},
            [](request_head&& req) -> epoll_http_transaction_ptr {
              return std::make_shared<padded_page_transaction>(std::move(req));
            });
      },
      std::move(loop), std::move(wheel), request_timeout, write_timeout);
}

#pragma region Http09

// `epoll_http_server` tests.

// Verify that an HTTP/0.9-style request (no version token, no headers)
// receives a response and the server then closes the connection.
TEST_CASE("Http09", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("GET /\r\n"));
  const auto response = client.recv_until("</html>");
  CHECK_FALSE(response.contains("200"));
  // HTTP/0.9 never keep-alive; server should close after the response.
  CHECK(client.recv().empty());
}
#pragma endregion
#pragma region LeadingCrlf

// Verify that leading bare CRLFs before the request line are silently
// skipped (RFC 9112 section 2.2) and the request is served normally.
TEST_CASE("LeadingCrlf", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("\r\n\r\nGET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("200"));
}
#pragma endregion
#pragma region TooManyLeadingCrls

// Verify that more than `max_leading_crls` bare CRLFs before the request
// line cause the server to drop the connection.
TEST_CASE("TooManyLeadingCrls", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  // Send 9 bare CRLFs (one more than the limit of 8) with no request line.
  CHECK(client.send("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n"));
  // The server should close the connection without responding.
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.empty());
}
#pragma endregion
#pragma region OwnLoop

// Verify that `create` with a null loop starts its own `epoll_loop_runner`.
TEST_CASE("OwnLoop", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);
  CHECK(server->local_endpoint());
}
#pragma endregion
#pragma region SharedLoop

// Verify that `create` with a shared loop stores and uses it.
TEST_CASE("SharedLoop", "[HttpServer]") {
  if (is_codex()) return;

  epoll_loop_runner runner;
  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0},
      runner.loop()->self());
  REQUIRE(server);
  CHECK(server->local_endpoint());
}
#pragma endregion
#pragma region Create_BadEndpoint

// Verify that `create` returns null when the listen socket cannot be created
// (e.g., an invalid endpoint).
TEST_CASE("Create_BadEndpoint", "[HttpServer]") {
  auto server = make_test_server(net_endpoint{});
  CHECK_FALSE(server);
}
#pragma endregion
#pragma region GetRoot

// Verify that `GET / HTTP/1.1` produces a 200 HTML response.
TEST_CASE("GetRoot", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("200"));
}
#pragma endregion
#pragma region GetPath

// Verify that `GET /123 HTTP/1.1` produces an HTML response that includes
// the numeric path component.
TEST_CASE("GetPath", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("GET /123 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("200"));
  const auto body = client.recv_until("</html>");
  CHECK(body.contains("123"));
}
#pragma endregion
#pragma region RouteBasePath

// Verify that `route_base_path` extracts the leading path component from the
// request target path and ignores any query or fragment suffix.
TEST_CASE("RouteBasePath", "[HttpServer]") {
  struct test_case {
    std::string_view target;
    std::string_view base_path;
  };

  constexpr test_case cases[]{{"/", "/"}, {"/ws", "/ws"}, {"/ws/", "/ws"},
      {"/ws/chat", "/ws"}, {"/ws?token=abc", "/ws"}, {"/ws#frag", "/ws"},
      {"/ws/chat?token=abc#frag", "/ws"}, {"/?token=abc", "/"},
      {"/#frag", "/"}, {"?token=abc", ""}, {"#frag", ""}, {"", ""}};

  for (const auto& tc : cases)
    CHECK(epoll_http_server::route_base_path(tc.target) == tc.base_path);
}
#pragma endregion
#pragma region InvalidRequest

// Verify that a POST request yields a 405 response (not a silent close).
TEST_CASE("InvalidRequest", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("POST /foo HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("405"));
}
#pragma endregion
#pragma region TooLongRequest

// Verify that a request line exceeding the 8192-byte limit causes the server
// to hang up immediately without sending any response.
TEST_CASE("TooLongRequest", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  // Send may fail mid-way if the server closes before all bytes are written;
  // ignore the result and rely on connection close.
  (void)client.send(std::string(8200, 'x'));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.size() == 0ULL);
}
#pragma endregion
#pragma region PartialRequest

// Verify that a request arriving in two writes is handled correctly by the
// stateful `terminated_text_parser`. The two writes may or may not be
// coalesced by TCP, but the test verifies correct parsing in either case.
TEST_CASE("PartialRequest", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("GET /42 HTTP/1.1\r\nHost: localhost"));
  CHECK(client.send("\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("200"));
  const auto body = client.recv_until("</html>");
  CHECK(body.contains("42"));
}
#pragma endregion
#pragma region ANS

// Verify that the server can listen on an ANS (Abstract Name Socket) and
// respond correctly to a `GET` request from an `epoll_stream_sync` client.
TEST_CASE("ANS", "[HttpServer]") {
  if (is_codex()) return;

  const std::string name =
      "@corvid_proto_http_test." + std::to_string(getpid()) + ".sock";
  const net_endpoint ep{name};
  REQUIRE(ep.is_ans());

  auto server = make_test_server(ep);
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(ep, 1s);
  REQUIRE(client);
  CHECK(client.send("GET /42 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("200"));
  const auto body = client.recv_until("</html>");
  CHECK(body.contains("42"));
}
#pragma endregion
#pragma region SharedWheel

// Verify that `create` with a shared `timing_wheel` stores and uses it.
TEST_CASE("SharedWheel", "[HttpServer]") {
  if (is_codex()) return;

  timing_wheel_runner wheel;
  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      wheel.wheel());
  REQUIRE(server);
  CHECK(server->local_endpoint());
}
#pragma endregion
#pragma region RequestWithinTimeout

// Verify that a normal GET request is served within the timeout window.
TEST_CASE("RequestWithinTimeout", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 5s);
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("200"));
}
#pragma endregion
#pragma region IdleTimeout

// Verify that an idle connection (no request sent) is forcefully closed by
// the server after the request timeout expires.
TEST_CASE("IdleTimeout", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 100ms);
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint());
  REQUIRE(client);

  // Send nothing. The server should hang up after the 100ms timeout. A
  // watchdog aborts the process if `recv()` blocks for more than 5 seconds.
  jthread_stoppable_sleep sleep;
  std::jthread watchdog([&sleep](std::stop_token st) {
    if (!sleep.until(std::move(st), std::chrono::steady_clock::now() + 5s)) {
      std::cerr << "HttpServer_IdleTimeout: recv() blocked for >5s\n";
      std::abort();
    }
  });
  CHECK(client.recv().empty());
  CHECK(client.errno_on_close() != EAGAIN);
}
#pragma endregion
#pragma region WriteTimeout

// Verify that the write timeout fires when the client stops reading.
//
// The client requests a 10 MB response but never reads, filling the kernel
// receive buffer and stalling the server's send path. The server should hang
// up the connection after the write timeout expires.
TEST_CASE("WriteTimeout", "[HttpServer]") {
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
  REQUIRE(server);

  const auto ep = server->local_endpoint();
  REQUIRE(ep);

  // Connect a client that sends the request but never reads the response.
  // Without an `on_data` handler, `EPOLLIN` is not armed on the client
  // connection, so incoming bytes accumulate in the kernel receive buffer.
  // Once that buffer fills, TCP flow control prevents the server from
  // writing, stalling the drain and triggering the write timeout.
  notifiable<bool> closed{false};
  auto client = epoll_stream_conn_ptr::connect(loop.loop()->self(), ep,
      {.on_drain =
              [sent = false](epoll_stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return true;
                return conn.send(
                    "GET /10000000 HTTP/1.1\r\nHost: localhost\r\n\r\n"s);
              },
          .on_close =
              [&closed](epoll_stream_conn&) {
                closed.notify_one(true);
                return true;
              }});
  REQUIRE(client);

  // Shrink the client-side receive buffer so that TCP flow control kicks in
  // well before the 10 MB response drains, making the write-timeout path
  // deterministic regardless of kernel autotuning. The kernel doubles the
  // value but the resulting ~8 KB ceiling is still tiny relative to the
  // response size.
  CHECK(client->sock().set_recv_buffer_size(4096));

  const auto start = std::chrono::steady_clock::now();

  // Allow 10x the write timeout for timing-wheel jitter and system overhead.
  REQUIRE(closed.wait_for_value(kWriteTimeout * 10, true));

  // The connection must not close before the write timeout has had time to
  // fire. If it closes immediately the write-timeout path was not exercised
  // (e.g., the response drained before backpressure engaged).
  CHECK((std::chrono::steady_clock::now() - start) >= (kWriteTimeout / 2));
}
#pragma endregion
#pragma region MissingHost

// Verify that an HTTP/1.1 request without a `Host` header receives a 400
// response, and the server then closes the connection.
TEST_CASE("MissingHost", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("GET / HTTP/1.1\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("400"));
  // Server closes after the error response.
  CHECK(client.recv().empty());
}
#pragma endregion
#pragma region KeepAlive

// Verify that a keep-alive connection accepts a second request after the
// first response is received.
TEST_CASE("KeepAlive", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);

  CHECK(client.send("GET /10 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r1 = client.recv_until("\r\n\r\n");
  CHECK(r1.contains("200"));
  (void)client.recv_until("</html>");

  CHECK(client.send("GET /20 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r2 = client.recv_until("\r\n\r\n");
  CHECK(r2.contains("200"));
  (void)client.recv_until("</html>");
}
#pragma endregion
#pragma region Pipeline

// Verify that two requests sent back-to-back (before any response is read)
// are both served in order -- the pipelining property.
TEST_CASE("Pipeline", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);

  // Send both requests before reading any response.
  CHECK(client.send(
      "GET /10 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /20 "
      "HTTP/1.1\r\nHost: localhost\r\n\r\n"));

  const auto r1 = client.recv_until("\r\n\r\n");
  CHECK(r1.contains("200"));
  (void)client.recv_until("</html>");

  const auto r2 = client.recv_until("\r\n\r\n");
  CHECK(r2.contains("200"));
  (void)client.recv_until("</html>");
}
#pragma endregion
#pragma region ConnectionClose

// Verify that `Connection: close` causes the server to close the connection
// after the response.
TEST_CASE("ConnectionClose", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send(
      "GET /5 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("200"));
  CHECK(response.contains("Connection: close"));
  (void)client.recv_until("</html>");
  // Server should close after the response.
  CHECK(client.recv().empty());
}
#pragma endregion
#pragma region Http10NoKeepAlive

// Verify that an HTTP/1.0 request (no `Host` header) receives a 200
// response and the server closes the connection (HTTP/1.0 default is close).
TEST_CASE("Http10NoKeepAlive", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("GET /5 HTTP/1.0\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("200"));
  (void)client.recv_until("</html>");
  // HTTP/1.0 default is close; server should close after the response.
  CHECK(client.recv().empty());
}
#pragma endregion
#pragma region BodyTooLarge

// Verify that a path encoding a body size exceeding 10 MB yields a 400.
TEST_CASE("BodyTooLarge", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  // 10 * 1024 * 1024 + 1 = 10485761, just over the 10 MB limit.
  CHECK(client.send("GET /10485761 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("400"));
}
#pragma endregion
#pragma region TooLongHeaders

// Verify that a header block exceeding the 8192-byte limit yields a 400
// response and the server closes the connection.
TEST_CASE("TooLongHeaders", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  // The header block (everything between the request line and \r\n\r\n)
  // must exceed 8192 bytes. "X-Pad: " (7) + 8192 'a' + "\r\n" (2) = 8201.
  const std::string long_header = "X-Pad: " + std::string(8192, 'a') + "\r\n";
  CHECK(client.send("GET / HTTP/1.1\r\n" + long_header + "\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("400"));
  CHECK(client.recv().empty()); // server closes after error
}
#pragma endregion
#pragma region MalformedRequestLine

// Verify that a request line with an unrecognized method yields a 400
// response and the server closes the connection.
TEST_CASE("MalformedRequestLine", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);
  CHECK(client.send("BREW /coffee HTTP/1.1\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  CHECK(response.contains("400"));
  CHECK(client.recv().empty()); // server closes after error
}
#pragma endregion
#pragma region Http10KeepAlive

// Verify that an HTTP/1.0 request with `Connection: keep-alive` keeps the
// connection open for a second request.
TEST_CASE("Http10KeepAlive", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  REQUIRE(server);

  auto client = epoll_stream_sync::connect(server->local_endpoint(), 1s);
  REQUIRE(client);

  CHECK(client.send("GET /5 HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
  const auto r1 = client.recv_until("\r\n\r\n");
  CHECK(r1.contains("200"));
  CHECK(r1.contains("Connection: keep-alive"));
  (void)client.recv_until("</html>");

  // Connection is still open; second request succeeds.
  CHECK(client.send("GET /10 HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
  const auto r2 = client.recv_until("\r\n\r\n");
  CHECK(r2.contains("200"));
  (void)client.recv_until("</html>");
}
#pragma endregion

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
