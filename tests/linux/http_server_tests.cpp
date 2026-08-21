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

#include "corvid/proto.h"
#include "corvid/concurrency/jthread_stoppable_sleep.h"
#include "corvid/infra/scope_exit.h"

#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  static constexpr auto max_pad = 10 * 1024UZ * 1024UZ;

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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("GET /\r\n"));
  const auto response = client.recv_sync_until(buf, "</html>");
  CHECK_FALSE(response.contains("200"));
  // HTTP/0.9 never keep-alive; server should close after the response.
  CHECK(client.recv_sync_drain_to_eof());
}

#pragma endregion
#pragma region LeadingCrlf

// Verify that leading bare CRLFs before the request line are silently
// skipped (RFC 9112 section 2.2) and the request is served normally.
TEST_CASE("LeadingCrlf", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all(
      "\r\n\r\nGET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  // Send 9 bare CRLFs (one more than the limit of 8) with no request line.
  CHECK(client.send_sync_all("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n"));
  // The server should close the connection without responding.
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("GET /123 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("200"));
  const auto body = client.recv_sync_until(buf, "</html>");
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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("POST /foo HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("405"));
}

#pragma endregion
#pragma region Connect501

// CONNECT parses (authority-form) but tunneling is not implemented, so the
// server responds "501 Not Implemented" rather than treating the request as
// malformed.
TEST_CASE("Connect501", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  // The 501 also forces close: a CONNECT client may pipeline tunnel bytes
  // right behind the head (the tunnel only exists after a 2xx), and those
  // must not parse as requests on a kept-alive connection.
  if (true) {
    auto client = net_socket::create_sync_connected(server->local_endpoint());
    std::string buf;
    REQUIRE(client.is_open());
    CHECK(client.send_sync_all(
        "CONNECT example.com:443 HTTP/1.1\r\nHost: example.com\r\n\r\n"));
    const auto response = client.recv_sync_until(buf, "\r\n\r\n");
    CHECK(response.contains("501"));
    CHECK(response.contains("Connection: close"));
    CHECK(client.recv_sync_drain_to_eof());
  }

  // RFC 9112 sec. 3.2: an HTTP/1.1 request without `Host` gets 400, with no
  // method carve-out, so the Host check outranks the CONNECT rejection.
  if (true) {
    auto client = net_socket::create_sync_connected(server->local_endpoint());
    std::string buf;
    REQUIRE(client.is_open());
    CHECK(client.send_sync_all("CONNECT example.com:443 HTTP/1.1\r\n\r\n"));
    const auto response = client.recv_sync_until(buf, "\r\n\r\n");
    CHECK(response.contains("400"));
    CHECK(client.recv_sync_drain_to_eof());
  }
}

#pragma endregion
#pragma region TransferEncodingRejected

// The server implements no transfer codings, so any `Transfer-Encoding`
// request is rejected before dispatch: chunked-final answers "501 Not
// Implemented" (RFC 9112 sec. 6.1), anything else "400 Bad Request" (sec.
// 6.3's MUST when chunked is not the final coding). Both force close, so an
// unread body cannot desync a kept-alive connection.
TEST_CASE("TransferEncodingRejected", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  // Chunked: understood but not implemented, so 501 with a forced close.
  // The chunked body smuggles a request line; the close keeps it from ever
  // parsing, so exactly one response comes back before EOF.
  if (true) {
    auto client = net_socket::create_sync_connected(server->local_endpoint());
    std::string buf;
    REQUIRE(client.is_open());
    CHECK(client.send_sync_all(
        "POST / HTTP/1.1\r\nHost: localhost\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "27\r\n"
        "GET /evil HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "\r\n0\r\n\r\n"));
    const auto response = client.recv_sync_until(buf, "\r\n\r\n");
    CHECK(response.contains("501"));
    CHECK(response.contains("Connection: close"));
    CHECK(client.recv_sync_drain_to_eof());
  }

  // A non-chunked final coding leaves the body length indeterminate: 400
  // and close per RFC 9112 sec. 6.3.
  if (true) {
    auto client = net_socket::create_sync_connected(server->local_endpoint());
    std::string buf;
    REQUIRE(client.is_open());
    CHECK(client.send_sync_all(
        "POST / HTTP/1.1\r\nHost: localhost\r\n"
        "Transfer-Encoding: gzip\r\n\r\n"));
    const auto response = client.recv_sync_until(buf, "\r\n\r\n");
    CHECK(response.contains("400"));
    CHECK(response.contains("Connection: close"));
    CHECK(client.recv_sync_drain_to_eof());
  }
}

#pragma endregion
#pragma region HeaderFieldsTooLarge

// A request over the field-line cap answers "431 Request Header Fields Too
// Large" (RFC 6585 sec. 5) rather than a generic 400: the request is
// well-formed, just too big, and the status is the client's cue that
// trimming its header set would fix it. The HTTP/3 stack answers its
// analogous cap with 431 as well; the block byte cap is covered by
// `TooLongHeaders` below.
TEST_CASE("HeaderFieldsTooLarge", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  // 101 short, valid lines fit well under the byte cap, so the line cap is
  // what trips.
  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  std::string request{"GET / HTTP/1.1\r\nHost: localhost\r\n"};
  for (auto ndx = 0; ndx < 100; ++ndx) request += "a:b\r\n";
  request += "\r\n";
  CHECK(client.send_sync_all(request));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("431"));
  CHECK(client.recv_sync_drain_to_eof());
}

#pragma endregion
#pragma region TooLongRequest

// Verify that a request line exceeding the 8192-byte limit causes the server
// to hang up immediately without sending any response.
TEST_CASE("TooLongRequest", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  // Send may fail mid-way if the server closes before all bytes are written;
  // ignore the result and rely on connection close.
  (void)client.send_sync_all(std::string(8200, 'x'));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("GET /42 HTTP/1.1\r\nHost: localhost"));
  CHECK(client.send_sync_all("\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("200"));
  const auto body = client.recv_sync_until(buf, "</html>");
  CHECK(body.contains("42"));
}

#pragma endregion
#pragma region ANS

// Verify that the server can listen on an ANS (Abstract Name Socket) and
// respond correctly to a `GET` request from a blocking client.
TEST_CASE("ANS", "[HttpServer]") {
  if (is_codex()) return;

  const std::string name =
      "@corvid_proto_http_test." + std::to_string(getpid()) + ".sock";
  const net_endpoint ep{name};
  REQUIRE(ep.as_sockaddr_view().is_ans());

  auto server = make_test_server(ep);
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(ep);
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("GET /42 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("200"));
  const auto body = client.recv_sync_until(buf, "</html>");
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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
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

  // No recv timeout: the server's idle timeout should fire first. A watchdog
  // aborts the process if `recv` blocks for more than 5 seconds.
  auto client =
      net_socket::create_sync_connected(server->local_endpoint(), 0ms);
  REQUIRE(client.is_open());

  // Send nothing. The server should hang up after the 100ms timeout.
  jthread_stoppable_sleep sleep;
  std::jthread watchdog([&sleep](std::stop_token st) {
    if (!sleep.until(std::move(st), std::chrono::steady_clock::now() + 5s)) {
      std::cerr << "HttpServer_IdleTimeout: recv() blocked for >5s\n";
      std::abort();
    }
  });
  // The server's idle-timeout path force-closes the connection (RST, not
  // FIN), so any `recv` failure within the watchdog window counts as a pass.
  std::string buf;
  no_zero{buf}.enlarge_to(4096);
  CHECK_FALSE(client.recv(buf));
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
  //
  // The connection's socket is owned by the loop thread once registered, so
  // set the option on that thread via `post_and_wait`; calling it directly
  // from here would race the loop's close path.
  CHECK(loop.loop()->post_and_wait([&] {
    return client->sock().set_recv_buffer_size(4096);
  }));

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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("GET / HTTP/1.1\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("400"));
  // Server closes after the error response.
  CHECK(client.recv_sync_drain_to_eof());
}

#pragma endregion
#pragma region KeepAlive

// Verify that a keep-alive connection accepts a second request after the
// first response is received.
TEST_CASE("KeepAlive", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());

  CHECK(client.send_sync_all("GET /10 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r1 = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(r1.contains("200"));
  (void)client.recv_sync_until(buf, "</html>");

  CHECK(client.send_sync_all("GET /20 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r2 = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(r2.contains("200"));
  (void)client.recv_sync_until(buf, "</html>");
}

#pragma endregion
#pragma region Pipeline

// Verify that two requests sent back-to-back (before any response is read)
// are both served in order -- the pipelining property.
TEST_CASE("Pipeline", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());

  // Send both requests before reading any response.
  CHECK(client.send_sync_all(
      "GET /10 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /20 "
      "HTTP/1.1\r\nHost: localhost\r\n\r\n"));

  const auto r1 = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(r1.contains("200"));
  (void)client.recv_sync_until(buf, "</html>");

  const auto r2 = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(r2.contains("200"));
  (void)client.recv_sync_until(buf, "</html>");
}

#pragma endregion
#pragma region PipelineAfterBody

// Transaction that consumes a `Content-Length`-sized request body from the
// receive buffer view across `handle_data` calls, responding with a small
// 200 page as soon as it becomes the writer.
struct body_sink_transaction: public epoll_http_transaction {
  explicit body_sink_transaction(request_head&& req)
      : epoll_http_transaction{std::move(req)},
        remaining_{request_headers.options.content_length.value_or(0)} {}

  [[nodiscard]] stream_claim handle_data(
      epoll_recv_buffer_view& view) override {
    const auto data = view.active_view();
    const auto take = std::min(remaining_, data.size());
    view.consume(take);
    remaining_ -= take;
    return remaining_ ? stream_claim::claim : stream_claim::release;
  }

  [[nodiscard]] stream_claim handle_drain(const send_fn& send_cb) override {
    response_headers.version = request_headers.version;
    response_headers.status_code = http_status_code::OK;
    response_headers.reason = "OK";
    response_headers.options.content_length = 4;
    response_headers.options.connection = close_after;
    (void)send_cb(response_headers.serialize());
    (void)send_cb("sunk"s);
    return stream_claim::release;
  }

private:
  size_t remaining_;
};

// Verify that a pipelined request already sitting in the receive buffer when
// a body-consuming transaction releases the input stream is parsed
// immediately, rather than stalling until more bytes arrive from the client.
TEST_CASE("PipelineAfterBody", "[HttpServer]") {
  if (is_codex()) return;

  auto server = epoll_http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      [](epoll_http_server& s) {
        return s.add_route({"", "/sink"},
                   [](request_head&& req) -> epoll_http_transaction_ptr {
                     return std::make_shared<body_sink_transaction>(
                         std::move(req));
                   }) &&
               s.add_route({"", "/"},
                   [](request_head&& req) -> epoll_http_transaction_ptr {
                     return std::make_shared<padded_page_transaction>(
                         std::move(req));
                   });
      });
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());

  // First write: POST headers plus the first 3 of 10 body bytes. The sink
  // transaction consumes them and claims the stream, leaving the connection
  // in body phase; its response confirms the write was processed.
  CHECK(client.send_sync_all(
      "POST /sink HTTP/1.1\r\nHost: "
      "localhost\r\nContent-Length: 10\r\n\r\n123"));
  const auto r1 = client.recv_sync_until(buf, "sunk");
  CHECK(r1.contains("200"));

  // Second write: the remaining 7 body bytes plus a complete pipelined GET
  // in a single segment. The GET must be answered without any further client
  // bytes.
  CHECK(client.send_sync_all(
      "4567890GET /5 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r2 = client.recv_sync_until(buf, "</html>");
  CHECK(r2.contains("200"));
}

#pragma endregion
#pragma region ConnectionClose

// Verify that `Connection: close` causes the server to close the connection
// after the response.
TEST_CASE("ConnectionClose", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all(
      "GET /5 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("200"));
  CHECK(response.contains("Connection: close"));
  (void)client.recv_sync_until(buf, "</html>");
  // Server should close after the response.
  CHECK(client.recv_sync_drain_to_eof());
}

#pragma endregion
#pragma region Http10NoKeepAlive

// Verify that an HTTP/1.0 request (no `Host` header) receives a 200
// response and the server closes the connection (HTTP/1.0 default is close).
TEST_CASE("Http10NoKeepAlive", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("GET /5 HTTP/1.0\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("200"));
  (void)client.recv_sync_until(buf, "</html>");
  // HTTP/1.0 default is close; server should close after the response.
  CHECK(client.recv_sync_drain_to_eof());
}

#pragma endregion
#pragma region BodyTooLarge

// Verify that a path encoding a body size exceeding 10 MB yields a 400.
TEST_CASE("BodyTooLarge", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  // 10 * 1024 * 1024 + 1 = 10485761, just over the 10 MB limit.
  CHECK(client.send_sync_all(
      "GET /10485761 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("400"));
}

#pragma endregion
#pragma region TooLongHeaders

// Verify that a header block exceeding the 8192-byte limit yields a "431
// Request Header Fields Too Large" (RFC 6585 sec. 5 covers the total size,
// not just the line count) and the server closes the connection.
TEST_CASE("TooLongHeaders", "[HttpServer]") {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      nullptr, 0s, 0s);
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  // The header block (everything between the request line and \r\n\r\n)
  // must exceed 8192 bytes. "X-Pad: " (7) + 8192 'a' + "\r\n" (2) = 8201.
  const std::string long_header = "X-Pad: " + std::string(8192, 'a') + "\r\n";
  CHECK(client.send_sync_all("GET / HTTP/1.1\r\n" + long_header + "\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("431"));
  CHECK(client.recv_sync_drain_to_eof()); // server closes after error
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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());
  CHECK(client.send_sync_all("BREW /coffee HTTP/1.1\r\n\r\n"));
  const auto response = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(response.contains("400"));
  CHECK(client.recv_sync_drain_to_eof()); // server closes after error
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

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());

  CHECK(client.send_sync_all(
      "GET /5 HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
  const auto r1 = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(r1.contains("200"));
  CHECK(r1.contains("Connection: keep-alive"));
  (void)client.recv_sync_until(buf, "</html>");

  // Connection is still open; second request succeeds.
  CHECK(client.send_sync_all(
      "GET /10 HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
  const auto r2 = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(r2.contains("200"));
  (void)client.recv_sync_until(buf, "</html>");
}

#pragma endregion
#pragma region StaticFiles

// Verify that the static file transaction serves GET with a body, serves
// HEAD with the same head but no body, and rejects other methods with a 405
// listing the supported methods in `Allow`.
TEST_CASE("StaticFiles", "[HttpServer]") {
  if (is_codex()) return;

  namespace fs = std::filesystem;
  // The pid suffix keeps a stale directory from an aborted run (or a
  // concurrent one) from inflating the cache-size check below.
  const auto web_root =
      fs::temp_directory_path() /
      ("corvid_http_static_test_" + std::to_string(getpid()));
  fs::create_directories(web_root);
  // The guard's destructor is noexcept, so the non-throwing overload is
  // required; a failed cleanup leaks one temp directory instead of
  // terminating the test run.
  scope_exit cleanup([&] {
    std::error_code ec;
    fs::remove_all(web_root, ec);
  });
  if (std::ofstream f(web_root / "index.html", std::ios::binary); true)
    f << "<html><body>static</body></html>";

  auto cache = std::make_shared<const epoll_static_file_cache>(web_root);
  REQUIRE(cache->size() == 2); // "/index.html" plus the "/" alias

  auto server = epoll_http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      [&cache](epoll_http_server& s) {
        return s.add_route({"", "/"},
            [cache](request_head&& req) -> epoll_http_transaction_ptr {
              return std::make_shared<epoll_static_file_transaction>(
                  std::move(req), cache);
            });
      });
  REQUIRE(server);

  auto client = net_socket::create_sync_connected(server->local_endpoint());
  std::string buf;
  REQUIRE(client.is_open());

  CHECK(client.send_sync_all(
      "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r1 = client.recv_sync_until(buf, "</html>");
  CHECK(r1.contains("200"));
  CHECK(r1.contains("Content-Type: text/html"));

  CHECK(client.send_sync_all(
      "HEAD /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r2 = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(r2.contains("200"));
  CHECK(r2.contains("Content-Length: 32"));

  // The next bytes on the wire must be the 405's status line; a body after
  // the HEAD response head would land at the front of `r3` instead.
  CHECK(client.send_sync_all(
      "DELETE /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto r3 = client.recv_sync_until(buf, "\r\n\r\n");
  CHECK(r3.starts_with("HTTP/1.1 405"));
  CHECK(r3.contains("Allow: GET, HEAD"));
}

#pragma endregion

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
