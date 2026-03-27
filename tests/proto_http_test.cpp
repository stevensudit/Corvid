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

// `http_head_codec` unit tests.

// Verify that a well-formed HTTP/1.1 GET request is parsed correctly.
void HttpHeaderBlock_ParseHttp11() {
  request_head req;
  // The final crlf was parsed out by `terminated_text_parser`, as part of the
  // crlfcrlf sentinel.
  ASSERT_TRUE(req.parse(
      "GET /path HTTP/1.1\r\nHost: example.com\r\nAccept: text/html"));
  EXPECT_EQ(req.version, http_version::http_1_1);
  EXPECT_EQ(req.method, http_method::GET);
  EXPECT_EQ(req.target, "/path");
  const auto host = req.headers.get("Host");
  ASSERT_TRUE(host);
  EXPECT_EQ(*host, "example.com");
  const auto accept = req.headers.get("Accept");
  ASSERT_TRUE(accept);
  EXPECT_EQ(*accept, "text/html");
}

// Verify that a well-formed HTTP/1.0 request is parsed correctly.
void HttpHeaderBlock_ParseHttp10() {
  request_head req;
  ASSERT_TRUE(req.parse("POST /submit HTTP/1.0\r\n"));
  EXPECT_EQ(req.version, http_version::http_1_0);
  EXPECT_EQ(req.method, http_method::POST);
  EXPECT_EQ(req.target, "/submit");
}

// Verify that an unrecognized method token causes `parse` to fail.
void HttpHeaderBlock_UnknownMethod() {
  request_head req;
  EXPECT_FALSE(req.parse("BREW /coffee HTTP/1.1\r\n"));
}

// Verify that an unrecognized version token causes `parse` to fail.
void HttpHeaderBlock_InvalidVersion() {
  request_head req;
  EXPECT_FALSE(req.parse("GET / HTTP/2.0\r\n"));
}

// Verify that an HTTP/0.9-style request line (no version token) yields
// `http_version::http_0_9`.
void HttpHeaderBlock_Http09Style() {
  request_head req;
  ASSERT_TRUE(req.parse("GET /\r\n"));
  EXPECT_EQ(req.version, http_version::http_0_9);
  EXPECT_EQ(req.target, "/");
}

// Verify that a request line with no SP at all returns false.
void HttpHeaderBlock_NoSp() {
  request_head req;
  EXPECT_FALSE(req.parse("GETNOSPC\r\n"));
}

// Verify that `http_headers::get()` requires the canonical key form.
// `add()` folds to canonical form before indexing; lookups with
// non-canonical names return `nullopt`.
void HttpHeaderBlock_HeaderLookupCanonical() {
  http_headers h;
  // Add with mixed-case input; stored under "Content-Type".
  EXPECT_TRUE(h.add("content-TYPE", "text/plain"));
  // Exact canonical form finds the value.
  const auto content_type = h.get("Content-Type");
  ASSERT_TRUE(content_type);
  EXPECT_EQ(*content_type, "text/plain");
  // Non-canonical forms do not match.
  EXPECT_FALSE(h.get("content-type"));
  EXPECT_FALSE(h.get("CONTENT-TYPE"));
}

// Verify that `get()` returns `nullopt` for absent or non-canonical names.
void HttpHeaderBlock_HeaderGet() {
  http_headers h;
  EXPECT_TRUE(h.add("Host", "localhost"));
  const auto host = h.get("Host");
  ASSERT_TRUE(host);
  EXPECT_EQ(*host, "localhost");
  EXPECT_FALSE(h.get("host"));
  EXPECT_FALSE(h.get("Content-Type"));
}

// Verify that `get()` distinguishes an empty stored value from a missing one.
void HttpHeaderBlock_HeaderGetEmptyValue() {
  http_headers h;
  EXPECT_TRUE(h.add_raw("X-Empty", ""));
  const auto empty_value = h.get("X-Empty");
  ASSERT_TRUE(empty_value);
  EXPECT_TRUE(empty_value->empty());
  EXPECT_FALSE(h.get("Missing"));
}

// Verify that `http_headers::get_combined()` joins multiple values with ", ".
void HttpHeaderBlock_HeaderCombine() {
  http_headers h;
  EXPECT_TRUE(h.add("Accept", "text/html"));
  EXPECT_TRUE(h.add("Accept", "application/json"));
  EXPECT_EQ(h.get_combined("Accept"), "text/html, application/json");
  EXPECT_EQ(h.get_combined("Missing"), "");
}

// Verify `keep_alive()` for HTTP/1.1 (default on) and HTTP/1.0 (default off).
void HttpHeaderBlock_KeepAlive() {
  {
    request_head req;
    ASSERT_TRUE(req.parse("GET / HTTP/1.1\r\nHost: h\r\n"));
    EXPECT_EQ(req.headers.keep_alive(req.version), after_response::keep_alive);
  }
  {
    request_head req;
    ASSERT_TRUE(req.parse("GET / HTTP/1.0\r\n"));
    EXPECT_EQ(req.headers.keep_alive(req.version), after_response::close);
  }
  {
    request_head req;
    ASSERT_TRUE(
        req.parse("GET / HTTP/1.1\r\nHost: h\r\nConnection: close\r\n"));
    EXPECT_EQ(req.headers.keep_alive(req.version), after_response::close);
  }
  {
    request_head req;
    ASSERT_TRUE(req.parse("GET / HTTP/1.0\r\nConnection: keep-alive\r\n"));
    EXPECT_EQ(req.headers.keep_alive(req.version), after_response::keep_alive);
  }
}

// Verify that `response_head::serialize()` produces the correct
// HTTP wire format (headers only; body is sent separately).
void HttpHeaderBlock_ResponseSerialize() {
  response_head resp;
  resp.version = http_version::http_1_1;
  resp.status_code = http_status_code::OK;
  resp.reason = "OK";
  EXPECT_TRUE(resp.headers.add_raw("Connection", "close"));
  EXPECT_TRUE(resp.headers.add_raw("Content-Type", "text/plain"));
  EXPECT_TRUE(resp.headers.add_raw("Content-Length", "5"));
  const auto wire = resp.serialize();
  EXPECT_NE(wire.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
  EXPECT_NE(wire.find("Connection: close\r\n"), std::string::npos);
  EXPECT_NE(wire.find("Content-Type: text/plain\r\n"), std::string::npos);
  EXPECT_NE(wire.find("Content-Length: 5\r\n"), std::string::npos);
  // Wire format ends with the blank line; body is not included.
  EXPECT_TRUE(wire.ends_with("\r\n\r\n"));
}

// Verify that `request_head::parse` skips leading CRLF lines
// (RFC 9112 section 2.2) and that a request that is only leading CRLFs fails.
void HttpHeaderBlock_ExtractLeadingCrlf() {
  {
    request_head req;
    ASSERT_TRUE(
        req.parse("\r\n\r\nGET /path HTTP/1.1\r\nHost: example.com\r\n"));
    EXPECT_EQ(req.method, http_method::GET);
    EXPECT_EQ(req.target, "/path");
    EXPECT_EQ(req.version, http_version::http_1_1);
    const auto host = req.headers.get("Host");
    ASSERT_TRUE(host);
    EXPECT_EQ(*host, "example.com");
  }
  {
    // Only CRLFs, no request line: fails.
    request_head req;
    EXPECT_FALSE(req.parse("\r\n\r\n"));
  }
}

// Verify that malformed header-field lines cause `parse` to return false.
void HttpHeaderBlock_ExtractHeaderErrors() {
  {
    // Obs-fold with SP: rejected.
    request_head req;
    EXPECT_FALSE(
        req.parse("GET / HTTP/1.1\r\nHost: example.com\r\n continued\r\n"));
  }
  {
    // Obs-fold with HTAB: rejected.
    request_head req;
    EXPECT_FALSE(
        req.parse("GET / HTTP/1.1\r\nHost: example.com\r\n\tcontinued\r\n"));
  }
  {
    // Header line with no colon: rejected.
    request_head req;
    EXPECT_FALSE(req.parse("GET / HTTP/1.1\r\nBadHeader\r\n"));
  }
  {
    // Invalid character (space) in field name: rejected.
    request_head req;
    EXPECT_FALSE(req.parse("GET / HTTP/1.1\r\nBad Name: value\r\n"));
  }
}

// Verify that `request_head::serialize()` produces correct wire format
// and that a round-trip through `parse` is lossless.
void HttpHeaderBlock_RequestSerialize() {
  {
    // HTTP/1.1 with headers.
    request_head req;
    ASSERT_TRUE(req.parse(
        "GET /path HTTP/1.1\r\nHost: example.com\r\nAccept: text/html\r\n"));
    const auto wire = req.serialize();
    EXPECT_NE(wire.find("GET /path HTTP/1.1\r\n"), std::string::npos);
    EXPECT_NE(wire.find("Host: example.com\r\n"), std::string::npos);
    EXPECT_NE(wire.find("Accept: text/html\r\n"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\n"));

    // Round-trip: strip the terminal "\r\n" blank line before passing to
    // `parse` (which expects the block without the "\r\n\r\n" sentinel).
    request_head req2;
    ASSERT_TRUE(req2.parse(std::string_view{wire}.substr(0, wire.size() - 2)));
    EXPECT_EQ(req2.version, http_version::http_1_1);
    EXPECT_EQ(req2.method, http_method::GET);
    EXPECT_EQ(req2.target, "/path");
    const auto host = req2.headers.get("Host");
    ASSERT_TRUE(host);
    EXPECT_EQ(*host, "example.com");
    const auto accept = req2.headers.get("Accept");
    ASSERT_TRUE(accept);
    EXPECT_EQ(*accept, "text/html");
  }
  {
    // HTTP/1.0, no headers.
    request_head req;
    ASSERT_TRUE(req.parse("POST /submit HTTP/1.0\r\n"));
    const auto wire = req.serialize();
    EXPECT_NE(wire.find("POST /submit HTTP/1.0\r\n"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\n"));
  }
  {
    // HTTP/0.9: no version token in the output.
    request_head req;
    ASSERT_TRUE(req.parse("GET /\r\n"));
    const auto wire = req.serialize();
    EXPECT_NE(wire.find("GET /\r\n"), std::string::npos);
    EXPECT_EQ(wire.find("HTTP/"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\n"));
  }
  {
    // invalid method: returns empty.
    request_head req;
    EXPECT_TRUE(req.serialize().empty());
  }
}

// Verify that a well-formed HTTP response is parsed by
// `response_head::parse()`.
void HttpHeaderBlock_ResponseExtract() {
  {
    // HTTP/1.1 200 with headers.
    response_head resp;
    ASSERT_TRUE(resp.parse(
        "HTTP/1.1 200 OK\r\nContent-Type: "
        "text/html\r\nContent-Length: 42\r\n"));
    EXPECT_EQ(resp.version, http_version::http_1_1);
    EXPECT_EQ(resp.status_code, http_status_code{200});
    EXPECT_EQ(resp.reason, "OK");
    const auto content_type = resp.headers.get("Content-Type");
    ASSERT_TRUE(content_type);
    EXPECT_EQ(*content_type, "text/html");
    const auto content_length = resp.headers.get("Content-Length");
    ASSERT_TRUE(content_length);
    EXPECT_EQ(*content_length, "42");
  }
  {
    // HTTP/1.0 with multi-word reason phrase.
    response_head resp;
    ASSERT_TRUE(resp.parse("HTTP/1.0 404 Not Found\r\n"));
    EXPECT_EQ(resp.version, http_version::http_1_0);
    EXPECT_EQ(resp.status_code, http_status_code{404});
    EXPECT_EQ(resp.reason, "Not Found");
  }
  {
    // Unknown version: false.
    response_head resp;
    EXPECT_FALSE(resp.parse("HTTP/2.0 200 OK\r\n"));
  }
  {
    // No SP after version: false.
    response_head resp;
    EXPECT_FALSE(resp.parse("HTTP/1.1\r\n"));
  }
  {
    // Non-numeric status code: false.
    response_head resp;
    EXPECT_FALSE(resp.parse("HTTP/1.1 abc OK\r\n"));
  }
  {
    // Round-trip: build a response, serialize it, re-parse, check fields.
    response_head resp;
    resp.version = http_version::http_1_1;
    resp.status_code = http_status_code{201};
    resp.reason = "Created";
    EXPECT_TRUE(resp.headers.add_raw("Location", "/new/resource"));
    const auto wire = resp.serialize();
    response_head resp2;
    ASSERT_TRUE(resp2.parse(wire.substr(0, wire.size() - 2)));
    EXPECT_EQ(resp2.version, http_version::http_1_1);
    EXPECT_EQ(resp2.status_code, http_status_code{201});
    EXPECT_EQ(resp2.reason, "Created");
    const auto location = resp2.headers.get("Location");
    ASSERT_TRUE(location);
    EXPECT_EQ(*location, "/new/resource");
  }
}

// `http_server` tests.

// Verify that an HTTP/0.9-style request (no version token, no headers)
// receives a response and the server then closes the connection.
void HttpServer_Http09() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.send("GET /\r\n"));
  const auto response = client.recv_until("</html>");
  EXPECT_EQ(response.find("200"), std::string::npos);
  // HTTP/0.9 never keep-alive; server should close after the response.
  EXPECT_TRUE(client.recv().empty());
}

// Verify that leading bare CRLFs before the request line are silently
// skipped (RFC 9112 section 2.2) and the request is served normally.
void HttpServer_LeadingCrlf() {
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);
  EXPECT_TRUE(
      client.send("\r\n\r\nGET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  const auto response = client.recv_until("\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
}

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
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      nullptr, nullptr, 100000s, 100000s);
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
  EXPECT_EQ(response.size(), 0ULL);
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

// Verify normalization of valid header names: case folding and hyphen
// word-boundary detection.
void HttpHeaderBlock_NormalizeCasing() {
  const std::vector<std::pair<std::string, std::string>> test_cases = {
      // --- 1. Standard Normalization (Train-Case) ---
      // Basic alphabetical case-insensitivity and hyphen-based
      // capitalization.
      {"content-type", "Content-Type"}, {"CONTENT-TYPE", "Content-Type"},
      {"CoNtEnT-tYpE", "Content-Type"}, {"User-Agent", "User-Agent"},
      {"x-request-id", "X-Request-Id"},

      // --- 2. Hyphen & Symbol Trigger Logic ---
      // Verifies that hyphens trigger uppercase, but symbols like '$' or '!'
      // reset to lowercase.
      {"-type", "-Type"}, // Leading hyphen triggers uppercase
      {"type-", "Type-"}, // Trailing hyphen sets state but no char follows
      {"x--forwarded",
          "X--Forwarded"},    // Double hyphen; both trigger "next_upper"
      {"$abc", "$abc"},       // Symbol resets start-of-string cap to lowercase
      {"abc$def", "Abc$def"}, // Mid-word symbol resets to lowercase
      {"!important-", "!important-"}, // Leading symbol kills initial cap
      {"99problems", "99problems"},   // Number resets cap
      {"99-problems", "99-Problems"}, // Number resets cap; hyphen re-triggers

      // --- 3. Valid RFC "tchar" Tokens (Non-Alpha) ---
      // These characters are legal in header names but should not trigger
      // capitalization.
      {"my_header", "My_header"}, // Underscore is a valid token
      {"a1b2-c3", "A1b2-C3"},     // Alphanumeric mix
      {"my.header", "My.header"}, // Period is a valid token
      {"#tag", "#tag"},           // Hash is valid
      {"~tilde", "~tilde"},       // Tilde is valid
      {"**star**", "**star**"},   // Asterisks are valid

      // --- 4. Invalid Characters (Should return std::nullopt / "INVALID")
      // ---
      // Characters strictly forbidden by RFC 9110 (delimiters and control
      // chars).
      {"Header Name", "INVALID"},  // Space (Illegal)
      {"Header:Name", "INVALID"},  // Colon (Separator, not token)
      {"Abc(Def)", "INVALID"},     // Parentheses (Delimiter)
      {"Key/Value", "INVALID"},    // Forward slash (Delimiter)
      {"@Home", "INVALID"},        // At-sign (Delimiter)
      {"[bracket]", "INVALID"},    // Square brackets (Delimiter)
      {"{brace}", "INVALID"},      // Curly braces (Delimiter)
      {"comma,header", "INVALID"}, // Comma (Delimiter)
      {"ctrl\nchar", "INVALID"},   // Newline (Security Risk)
      {std::string("null\0byte", 9), "INVALID"}, // Null byte (Security Risk)

      // --- 5. Edge Cases ---
      // Minimum lengths and weird but legal "tchar" sequences.
      {"", "INVALID"}, // Empty string is not a valid token
      {"a", "A"},      // Single char
      {"-", "-"},      // Only a hyphen
      {"---", "---"},  // Multiple hyphens
      {"123", "123"}   // Only numbers
  };

  for (const auto& [input, expected] : test_cases) {
    std::string actual = input;
    if (!http_headers::normalize(actual)) actual = "INVALID";
    EXPECT_EQ(actual, expected);
  }

  {
    // Lowercase input: changed, result is title case.
    std::string name{"content-type"};
    EXPECT_TRUE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "Content-Type");
  }
  {
    // All-caps input: changed, result is title case.
    std::string name{"ACCEPT-ENCODING"};
    EXPECT_TRUE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "Accept-Encoding");
  }
  {
    // Mixed case: changed, result is title case.
    std::string name{"X-fOrWaRdEd-fOr"};
    EXPECT_TRUE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "X-Forwarded-For");
  }
  {
    // Already canonical: not changed, name unchanged.
    std::string name{"Content-Type"};
    EXPECT_FALSE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "Content-Type");
  }
  {
    // Multi-segment all-lowercase.
    std::string name{"x-forwarded-for"};
    EXPECT_TRUE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "X-Forwarded-For");
  }
}

// Verify that valid token special characters are accepted and that names
// containing them are normalized correctly.
void HttpHeaderBlock_NormalizeSpecialChars() {
  {
    // Underscore and dot are valid; alpha segments are title-cased.
    std::string name{"x-custom_header.v2"};
    EXPECT_TRUE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "X-Custom_header.v2");
  }
  {
    // A name composed entirely of the special set "!#$%&'*+.^_`|~" is
    // valid; none are alpha so to_upper/to_lower are no-ops.
    std::string name{"!#$%&'*+.^_`|~"};
    EXPECT_FALSE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "!#$%&'*+.^_`|~");
  }
  {
    // Hyphen triggers capitalization of the following character.
    std::string name{"a-b-c"};
    EXPECT_TRUE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "A-B-C");
  }
  {
    // Leading hyphen: valid, first alpha after it is uppercased.
    std::string name{"-foo"};
    EXPECT_TRUE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "-Foo");
  }
}

// Verify that names containing characters outside the allowed set are
// rejected: `name` is cleared and false is returned.
void HttpHeaderBlock_NormalizeInvalidChars() {
  // Returns true iff `normalize` rejected the name (nullopt) and left it
  // unchanged.
  auto bad = [](std::string name) {
    const std::string orig{name};
    return !http_headers::normalize(name) && name == orig;
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
void HttpHeaderBlock_NormalizeEdgeCases() {
  {
    // Empty string: no invalid chars, no change, returns nullopt.
    std::string name;
    EXPECT_FALSE(http_headers::normalize(name));
  }
  {
    // Single valid alpha: uppercased, returns true.
    std::string name{"a"};
    EXPECT_TRUE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "A");
  }
  {
    // Single valid alpha already uppercase: no change, returns false.
    std::string name{"A"};
    EXPECT_FALSE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "A");
  }
  {
    // Single digit: valid, no alpha casing, returns false.
    std::string name{"3"};
    EXPECT_FALSE(http_headers::normalize(name).value());
    EXPECT_EQ(name, "3");
  }
  {
    // Single invalid char: returns nullopt, name unchanged.
    std::string name{" "};
    EXPECT_FALSE(http_headers::normalize(name));
    EXPECT_EQ(name, " ");
  }
  {
    // Invalid char mid-name: returns nullopt, name unchanged.
    std::string name{"Content Type"};
    EXPECT_FALSE(http_headers::normalize(name));
    EXPECT_EQ(name, "Content Type");
  }
}

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(HttpHeaderBlock_ParseHttp11, HttpHeaderBlock_ParseHttp10,
    HttpHeaderBlock_UnknownMethod, HttpHeaderBlock_InvalidVersion,
    HttpHeaderBlock_Http09Style, HttpHeaderBlock_NoSp,
    HttpHeaderBlock_HeaderLookupCanonical, HttpHeaderBlock_HeaderGet,
    HttpHeaderBlock_HeaderGetEmptyValue, HttpHeaderBlock_HeaderCombine,
    HttpHeaderBlock_KeepAlive, HttpHeaderBlock_ExtractLeadingCrlf,
    HttpHeaderBlock_ResponseSerialize, HttpHeaderBlock_ExtractHeaderErrors,
    HttpHeaderBlock_RequestSerialize, HttpHeaderBlock_ResponseExtract,
    HttpServer_OwnLoop, HttpServer_SharedLoop, HttpServer_Create_BadEndpoint,
    HttpServer_GetRoot, HttpServer_GetPath, HttpServer_InvalidRequest,
    HttpServer_TooLongRequest, HttpServer_PartialRequest, HttpServer_ANS,
    HttpServer_SharedWheel, HttpServer_RequestWithinTimeout,
    HttpServer_IdleTimeout, HttpServer_WriteTimeout, HttpServer_MissingHost,
    HttpServer_KeepAlive, HttpServer_Pipeline, HttpServer_ConnectionClose,
    HttpServer_Http10NoKeepAlive, HttpServer_Http09, HttpServer_LeadingCrlf,
    HttpHeaderBlock_NormalizeCasing, HttpHeaderBlock_NormalizeSpecialChars,
    HttpHeaderBlock_NormalizeInvalidChars, HttpHeaderBlock_NormalizeEdgeCases);
