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

// `http_header_block` unit tests.

// Verify that a well-formed HTTP/1.1 GET request is parsed correctly.
void HttpHeaderBlock_ExtractHttp11() {
  request_header_block req;
  ASSERT_TRUE(req.extract(
      "GET /path HTTP/1.1\r\nHost: example.com\r\nAccept: text/html\r\n"));
  EXPECT_EQ(req.version, http_version::http_11);
  EXPECT_EQ(req.method, http_method::GET);
  EXPECT_EQ(req.target, "/path");
  EXPECT_EQ(req.headers.get("Host"), "example.com");
  EXPECT_EQ(req.headers.get("Accept"), "text/html");
}

// Verify that a well-formed HTTP/1.0 request is parsed correctly.
void HttpHeaderBlock_ExtractHttp10() {
  request_header_block req;
  ASSERT_TRUE(req.extract("POST /submit HTTP/1.0\r\n"));
  EXPECT_EQ(req.version, http_version::http_10);
  EXPECT_EQ(req.method, http_method::POST);
  EXPECT_EQ(req.target, "/submit");
}

// Verify that an unrecognized method token causes `extract` to fail.
void HttpHeaderBlock_UnknownMethod() {
  request_header_block req;
  EXPECT_FALSE(req.extract("BREW /coffee HTTP/1.1\r\n"));
}

// Verify that an unrecognized version token causes `extract` to fail.
void HttpHeaderBlock_InvalidVersion() {
  request_header_block req;
  EXPECT_FALSE(req.extract("GET / HTTP/2.0\r\n"));
}

// Verify that an HTTP/0.9-style request line (no version token) yields
// `http_version::http_09`.
void HttpHeaderBlock_Http09Style() {
  request_header_block req;
  ASSERT_TRUE(req.extract("GET /\r\n"));
  EXPECT_EQ(req.version, http_version::http_09);
  EXPECT_EQ(req.target, "/");
}

// Verify that a request line with no SP at all returns false.
void HttpHeaderBlock_NoSp() {
  request_header_block req;
  EXPECT_FALSE(req.extract("GETNOSPC\r\n"));
}

// Verify that `http_headers::get()` requires the canonical key form.
// `add_canonical` folds to canonical before indexing; lookups with
// non-canonical names return empty.
void HttpHeaderBlock_HeaderLookupCanonical() {
  http_headers h;
  // Add with mixed-case input; stored under "Content-Type".
  EXPECT_TRUE(h.add_canonical("content-TYPE", "text/plain"));
  // Exact canonical form finds the value.
  EXPECT_EQ(h.get("Content-Type"), "text/plain");
  // Non-canonical forms do not match.
  EXPECT_EQ(h.get("content-type"), "");
  EXPECT_EQ(h.get("CONTENT-TYPE"), "");
}

// Verify that `get()` returns empty for absent or non-canonical names.
void HttpHeaderBlock_HeaderGet() {
  http_headers h;
  EXPECT_TRUE(h.add_canonical("Host", "localhost"));
  EXPECT_FALSE(h.get("Host").empty());
  EXPECT_TRUE(h.get("host").empty());
  EXPECT_TRUE(h.get("Content-Type").empty());
}

// Verify that `http_headers::combine()` joins multiple values with ", ".
void HttpHeaderBlock_HeaderCombine() {
  http_headers h;
  EXPECT_TRUE(h.add_canonical("Accept", "text/html"));
  EXPECT_TRUE(h.add_canonical("Accept", "application/json"));
  EXPECT_EQ(h.combine("Accept"), "text/html, application/json");
  EXPECT_EQ(h.combine("Missing"), "");
}

// Verify `keep_alive()` for HTTP/1.1 (default on) and HTTP/1.0 (default off).
void HttpHeaderBlock_KeepAlive() {
  {
    request_header_block req;
    ASSERT_TRUE(req.extract("GET / HTTP/1.1\r\nHost: h\r\n"));
    EXPECT_TRUE(req.headers.keep_alive(req.version));
  }
  {
    request_header_block req;
    ASSERT_TRUE(req.extract("GET / HTTP/1.0\r\n"));
    EXPECT_FALSE(req.headers.keep_alive(req.version));
  }
  {
    request_header_block req;
    ASSERT_TRUE(
        req.extract("GET / HTTP/1.1\r\nHost: h\r\nConnection: close\r\n"));
    EXPECT_FALSE(req.headers.keep_alive(req.version));
  }
  {
    request_header_block req;
    ASSERT_TRUE(
        req.extract("GET / HTTP/1.0\r\nConnection: keep-alive\r\n"));
    EXPECT_TRUE(req.headers.keep_alive(req.version));
  }
}

// Verify that `response_header_block::serialize()` produces the correct
// HTTP wire format (headers only; body is sent separately).
void HttpHeaderBlock_ResponseSerialize() {
  response_header_block resp;
  resp.status_code = 200;
  resp.reason = "OK";
  EXPECT_TRUE(resp.headers.add_canonical("Connection", "close"));
  EXPECT_TRUE(resp.headers.add("Content-Type", "text/plain"));
  EXPECT_TRUE(resp.headers.add("Content-Length", "5"));
  const auto wire = resp.serialize();
  EXPECT_NE(wire.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
  EXPECT_NE(wire.find("Connection: close\r\n"), std::string::npos);
  EXPECT_NE(wire.find("Content-Type: text/plain\r\n"), std::string::npos);
  EXPECT_NE(wire.find("Content-Length: 5\r\n"), std::string::npos);
  // Wire format ends with the blank line; body is not included.
  EXPECT_TRUE(wire.ends_with("\r\n\r\n"));
}

// Verify that malformed header-field lines cause `extract` to return false.
void HttpHeaderBlock_ExtractHeaderErrors() {
  {
    // Obs-fold with SP: rejected.
    request_header_block req;
    EXPECT_FALSE(req.extract(
        "GET / HTTP/1.1\r\nHost: example.com\r\n continued\r\n"));
  }
  {
    // Obs-fold with HTAB: rejected.
    request_header_block req;
    EXPECT_FALSE(req.extract(
        "GET / HTTP/1.1\r\nHost: example.com\r\n\tcontinued\r\n"));
  }
  {
    // Header line with no colon: rejected.
    request_header_block req;
    EXPECT_FALSE(req.extract("GET / HTTP/1.1\r\nBadHeader\r\n"));
  }
  {
    // Invalid character (space) in field name: rejected.
    request_header_block req;
    EXPECT_FALSE(req.extract("GET / HTTP/1.1\r\nBad Name: value\r\n"));
  }
}

// Verify that `request_header_block::serialize()` produces correct wire format
// and that a round-trip through `extract` is lossless.
void HttpHeaderBlock_RequestSerialize() {
  {
    // HTTP/1.1 with headers.
    request_header_block req;
    ASSERT_TRUE(req.extract(
        "GET /path HTTP/1.1\r\nHost: example.com\r\nAccept: text/html\r\n"));
    const auto wire = req.serialize();
    EXPECT_NE(wire.find("GET /path HTTP/1.1\r\n"), std::string::npos);
    EXPECT_NE(wire.find("Host: example.com\r\n"), std::string::npos);
    EXPECT_NE(wire.find("Accept: text/html\r\n"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\n"));

    // Round-trip: strip the terminal "\r\n" blank line before passing to
    // extract (which expects the block without the "\r\n\r\n" sentinel).
    request_header_block req2;
    ASSERT_TRUE(req2.extract(std::string_view{wire}.substr(0, wire.size() - 2)));
    EXPECT_EQ(req2.version, http_version::http_11);
    EXPECT_EQ(req2.method, http_method::GET);
    EXPECT_EQ(req2.target, "/path");
    EXPECT_EQ(req2.headers.get("Host"), "example.com");
    EXPECT_EQ(req2.headers.get("Accept"), "text/html");
  }
  {
    // HTTP/1.0, no headers.
    request_header_block req;
    ASSERT_TRUE(req.extract("POST /submit HTTP/1.0\r\n"));
    const auto wire = req.serialize();
    EXPECT_NE(wire.find("POST /submit HTTP/1.0\r\n"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\n"));
  }
  {
    // HTTP/0.9: no version token in the output.
    request_header_block req;
    ASSERT_TRUE(req.extract("GET /\r\n"));
    const auto wire = req.serialize();
    EXPECT_NE(wire.find("GET /\r\n"), std::string::npos);
    EXPECT_EQ(wire.find("HTTP/"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\n"));
  }
  {
    // invalid method: returns empty.
    request_header_block req;
    EXPECT_TRUE(req.serialize().empty());
  }
}

// Verify that a well-formed HTTP response is parsed by
// `response_header_block::extract()`.
void HttpHeaderBlock_ResponseExtract() {
  {
    // HTTP/1.1 200 with headers.
    response_header_block resp;
    ASSERT_TRUE(resp.extract(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 42\r\n"));
    EXPECT_EQ(resp.version, http_version::http_11);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_EQ(resp.reason, "OK");
    EXPECT_EQ(resp.headers.get("Content-Type"), "text/html");
    EXPECT_EQ(resp.headers.get("Content-Length"), "42");
  }
  {
    // HTTP/1.0 with multi-word reason phrase.
    response_header_block resp;
    ASSERT_TRUE(resp.extract("HTTP/1.0 404 Not Found\r\n"));
    EXPECT_EQ(resp.version, http_version::http_10);
    EXPECT_EQ(resp.status_code, 404);
    EXPECT_EQ(resp.reason, "Not Found");
  }
  {
    // Unknown version: false.
    response_header_block resp;
    EXPECT_FALSE(resp.extract("HTTP/2.0 200 OK\r\n"));
  }
  {
    // No SP after version: false.
    response_header_block resp;
    EXPECT_FALSE(resp.extract("HTTP/1.1\r\n"));
  }
  {
    // Non-numeric status code: false.
    response_header_block resp;
    EXPECT_FALSE(resp.extract("HTTP/1.1 abc OK\r\n"));
  }
  {
    // Round-trip: build a response, serialize it, re-extract, check fields.
    response_header_block resp;
    resp.status_code = 201;
    resp.reason = "Created";
    EXPECT_TRUE(resp.headers.add("Location", "/new/resource"));
    const auto wire = resp.serialize();
    response_header_block resp2;
    ASSERT_TRUE(resp2.extract(wire.substr(0, wire.size() - 2)));
    EXPECT_EQ(resp2.status_code, 201);
    EXPECT_EQ(resp2.reason, "Created");
    EXPECT_EQ(resp2.headers.get("Location"), "/new/resource");
  }
}

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

// Verify that `GET / HTTP/1.1` produces a 200 HTML response.
void HttpServer_GetRoot() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
}

// Verify that `GET /123 HTTP/1.1` produces an HTML response that includes
// the numeric path component.
void HttpServer_GetPath() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /123 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  const auto body = client.recv_until("</html>");
  EXPECT_NE(body.find("123"), std::string::npos);
}

// Verify that a POST request yields a 405 response (not a silent close).
void HttpServer_InvalidRequest() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("POST /foo HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("405"), std::string::npos);
}

// Verify that a request line exceeding the 8192-byte limit causes the server
// to send a 400 response and then close the connection.
void HttpServer_TooLongRequest() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  // Send may fail mid-way if the server closes before all bytes are written;
  // ignore the result and rely on `recv_until` to read the error response.
  (void)client.send(std::string(8200, 'x'));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("400"), std::string::npos);
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
  EXPECT_TRUE(client.send("GET /42 HTTP/1.1\r\nHost: localhost"));
  EXPECT_TRUE(client.send("\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  const auto body = client.recv_until("</html>");
  EXPECT_NE(body.find("42"), std::string::npos);
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
  EXPECT_TRUE(client.send("GET /42 HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  const auto body = client.recv_until("</html>");
  EXPECT_NE(body.find("42"), std::string::npos);
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
  EXPECT_TRUE(client.send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
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
                return conn.send(
                    "GET /10000000 HTTP/1.1\r\nHost: localhost\r\n\r\n");
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

// Verify that an HTTP/1.1 request without a `Host` header receives a 400
// response, and the server then closes the connection.
void HttpServer_MissingHost() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET / HTTP/1.1\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("400"), std::string::npos);
  // Server closes after the error response.
  EXPECT_TRUE(client.recv().empty());
}

// Verify that a keep-alive connection accepts a second request after the
// first response is received.
void HttpServer_KeepAlive() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
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

// Verify that two requests sent back-to-back (before any response is read)
// are both served in order -- the pipelining property.
void HttpServer_Pipeline() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  // Send both requests before reading any response.
  EXPECT_TRUE(client.send(
      "GET /10 HTTP/1.1\r\nHost: localhost\r\n\r\n"
      "GET /20 HTTP/1.1\r\nHost: localhost\r\n\r\n"));

  const auto r1 = client.recv_until("\r\n\r\n");
  EXPECT_NE(r1.find("200"), std::string::npos);
  (void)client.recv_until("</html>");

  const auto r2 = client.recv_until("\r\n\r\n");
  EXPECT_NE(r2.find("200"), std::string::npos);
  (void)client.recv_until("</html>");
}

// Verify that `Connection: close` causes the server to close the connection
// after the response.
void HttpServer_ConnectionClose() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
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

// Verify that an HTTP/1.0 request (no `Host` header) receives a 200
// response and the server closes the connection (HTTP/1.0 default is close).
void HttpServer_Http10NoKeepAlive() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
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

// Verify canonicalization of valid header names: case folding and hyphen
// word-boundary detection.
void HttpHeaderBlock_CanonicalizeCasing() {
  {
    // Lowercase input: changed, result is title case.
    std::string name{"content-type"};
    EXPECT_TRUE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "Content-Type");
  }
  {
    // All-caps input: changed, result is title case.
    std::string name{"ACCEPT-ENCODING"};
    EXPECT_TRUE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "Accept-Encoding");
  }
  {
    // Mixed case: changed, result is title case.
    std::string name{"X-fOrWaRdEd-fOr"};
    EXPECT_TRUE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "X-Forwarded-For");
  }
  {
    // Already canonical: not changed, name unchanged.
    std::string name{"Content-Type"};
    EXPECT_FALSE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "Content-Type");
  }
  {
    // Multi-segment all-lowercase.
    std::string name{"x-forwarded-for"};
    EXPECT_TRUE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "X-Forwarded-For");
  }
}

// Verify that valid token special characters are accepted and that names
// containing them are canonicalized correctly.
void HttpHeaderBlock_CanonicalizeSpecialChars() {
  {
    // Underscore and dot are valid; alpha segments are title-cased.
    std::string name{"x-custom_header.v2"};
    EXPECT_TRUE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "X-Custom_header.v2");
  }
  {
    // A name composed entirely of the special set "!#$%&'*+.^_`|~" is
    // valid; none are alpha so to_upper/to_lower are no-ops.
    std::string name{"!#$%&'*+.^_`|~"};
    EXPECT_FALSE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "!#$%&'*+.^_`|~");
  }
  {
    // Hyphen triggers capitalization of the following character.
    std::string name{"a-b-c"};
    EXPECT_TRUE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "A-B-C");
  }
  {
    // Leading hyphen: valid, first alpha after it is uppercased.
    std::string name{"-foo"};
    EXPECT_TRUE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "-Foo");
  }
}

// Verify that names containing characters outside the allowed set are
// rejected: `name` is cleared and false is returned.
void HttpHeaderBlock_CanonicalizeInvalidChars() {
  // Returns true iff `canonicalize` rejected the name (nullopt) and left it
  // unchanged.
  auto bad = [](std::string name) {
    const std::string orig{name};
    return !http_headers::canonicalize(name) && name == orig;
  };

  EXPECT_TRUE(bad("Bad Name"));  // space
  EXPECT_TRUE(bad("Bad:Name"));  // colon
  EXPECT_TRUE(bad("bad@name"));  // at-sign
  EXPECT_TRUE(bad("bad\\name")); // backslash
  EXPECT_TRUE(bad("bad\tname")); // tab (control character)
  EXPECT_TRUE(bad("bad/name"));  // slash
  EXPECT_TRUE(bad("bad\"name")); // double-quote
  EXPECT_TRUE(bad("bad(name"));  // open paren
  EXPECT_TRUE(bad("bad)name"));  // close paren
  EXPECT_TRUE(bad("bad<name"));  // less-than
  EXPECT_TRUE(bad("bad>name"));  // greater-than
}

// Verify edge cases: empty name and single-character names.
void HttpHeaderBlock_CanonicalizeEdgeCases() {
  {
    // Empty string: no invalid chars, no change, returns false.
    std::string name;
    EXPECT_FALSE(http_headers::canonicalize(name).value());
    EXPECT_TRUE(name.empty());
  }
  {
    // Single valid alpha: uppercased, returns true.
    std::string name{"a"};
    EXPECT_TRUE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "A");
  }
  {
    // Single valid alpha already uppercase: no change, returns false.
    std::string name{"A"};
    EXPECT_FALSE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "A");
  }
  {
    // Single digit: valid, no alpha casing, returns false.
    std::string name{"3"};
    EXPECT_FALSE(http_headers::canonicalize(name).value());
    EXPECT_EQ(name, "3");
  }
  {
    // Single invalid char: returns nullopt, name unchanged.
    std::string name{" "};
    EXPECT_FALSE(http_headers::canonicalize(name));
    EXPECT_EQ(name, " ");
  }
  {
    // Invalid char mid-name: returns nullopt, name unchanged.
    std::string name{"Content Type"};
    EXPECT_FALSE(http_headers::canonicalize(name));
    EXPECT_EQ(name, "Content Type");
  }
}

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(HttpHeaderBlock_ExtractHttp11, HttpHeaderBlock_ExtractHttp10,
    HttpHeaderBlock_UnknownMethod, HttpHeaderBlock_InvalidVersion,
    HttpHeaderBlock_Http09Style, HttpHeaderBlock_NoSp,
    HttpHeaderBlock_HeaderLookupCanonical, HttpHeaderBlock_HeaderGet,
    HttpHeaderBlock_HeaderCombine, HttpHeaderBlock_KeepAlive,
    HttpHeaderBlock_ResponseSerialize, HttpHeaderBlock_ExtractHeaderErrors,
    HttpHeaderBlock_RequestSerialize,
    HttpHeaderBlock_ResponseExtract, HttpServer_OwnLoop,
    HttpServer_SharedLoop, HttpServer_Create_BadEndpoint, HttpServer_GetRoot,
    HttpServer_GetPath, HttpServer_InvalidRequest, HttpServer_TooLongRequest,
    HttpServer_PartialRequest, HttpServer_ANS, HttpServer_SharedWheel,
    HttpServer_RequestWithinTimeout, HttpServer_IdleTimeout,
    HttpServer_WriteTimeout, HttpServer_MissingHost, HttpServer_KeepAlive,
    HttpServer_Pipeline, HttpServer_ConnectionClose,
    HttpServer_Http10NoKeepAlive, HttpHeaderBlock_CanonicalizeCasing,
    HttpHeaderBlock_CanonicalizeSpecialChars,
    HttpHeaderBlock_CanonicalizeInvalidChars,
    HttpHeaderBlock_CanonicalizeEdgeCases);
