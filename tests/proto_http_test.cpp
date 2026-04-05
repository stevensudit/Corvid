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
}

// ASCII and fully contained multibyte sequences leave the validator complete.
void Utf8Checker_Complete() {
  utf8_checker v;
  EXPECT_EQ(v.state(), utf8_checker::validation::complete);
  EXPECT_EQ(v.validate("hello"), utf8_checker::validation::complete);
  EXPECT_EQ(v.validate("\xE2\x82\xAC"), utf8_checker::validation::complete);
  EXPECT_TRUE(v.is_complete());
}

// A split multibyte sequence transitions to incomplete, then back to complete.
void Utf8Checker_IncompleteThenComplete() {
  utf8_checker v;
  EXPECT_EQ(v.validate("\xF0\x9F"), utf8_checker::validation::incomplete);
  EXPECT_TRUE(v.is_incomplete());
  EXPECT_EQ(v.validate("\x98\x80"), utf8_checker::validation::complete);
  EXPECT_TRUE(v.is_complete());
}

// Invalid leading and continuation bytes move the validator to sticky invalid.
void Utf8Checker_InvalidSticky() {
  utf8_checker v;
  EXPECT_EQ(v.validate("\x80"), utf8_checker::validation::failed);
  EXPECT_TRUE(v.is_failed());
  EXPECT_EQ(v.validate("abc"), utf8_checker::validation::failed);
  EXPECT_TRUE(v.is_failed());
}

// Reject overlongs, surrogate code points, and code points past U+10FFFF.
void Utf8Checker_RejectsInvalidSequences() {
  utf8_checker v;
  EXPECT_EQ(v.validate("\xE0\x80\x80"), utf8_checker::validation::failed);

  v.reset();
  EXPECT_EQ(v.validate("\xED\xA0\x80"), utf8_checker::validation::failed);

  v.reset();
  EXPECT_EQ(v.validate("\xF4\x90\x80\x80"), utf8_checker::validation::failed);
}

// Verify that `get()` returns `nullopt` for absent or non-canonical names.
void HttpHeaderBlock_HeaderGet() {
  http_headers h;
  EXPECT_TRUE(h.add("Host", "localhost"));
  const auto host = h.get("Host");
  ASSERT_TRUE(host);
  EXPECT_EQ(*host, "localhost");
  EXPECT_FALSE(h.get("Heist"));
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
    EXPECT_EQ(req.options.keep_alive(req.version), after_response::keep_alive);
  }
  {
    request_head req;
    ASSERT_TRUE(req.parse("GET / HTTP/1.0\r\n"));
    EXPECT_EQ(req.options.keep_alive(req.version), after_response::close);
  }
  {
    request_head req;
    ASSERT_TRUE(
        req.parse("GET / HTTP/1.1\r\nHost: h\r\nConnection: close\r\n"));
    EXPECT_EQ(req.options.keep_alive(req.version), after_response::close);
  }
  {
    request_head req;
    ASSERT_TRUE(req.parse("GET / HTTP/1.0\r\nConnection: keep-alive\r\n"));
    EXPECT_EQ(req.options.keep_alive(req.version), after_response::keep_alive);
  }
}

// Verify `keep_alive()` parses `Connection` as a comma-separated token list,
// with `"close"` taking precedence over `"keep-alive"`.
void HttpHeaderBlock_KeepAliveTokenList() {
  // Token list containing `"close"` among other tokens -> close.
  {
    request_head req;
    ASSERT_TRUE(req.parse(
        "GET / HTTP/1.1\r\nHost: h\r\nConnection: keep-alive, close\r\n"));
    EXPECT_EQ(req.options.keep_alive(req.version), after_response::close);
  }
  // Token list with only `"close"` and an unrelated token -> close.
  {
    request_head req;
    ASSERT_TRUE(req.parse(
        "GET / HTTP/1.1\r\nHost: h\r\nConnection: close, upgrade\r\n"));
    EXPECT_EQ(req.options.keep_alive(req.version), after_response::close);
  }
  // Token list with `"keep-alive"` and an unrelated token -> keep-alive.
  {
    request_head req;
    ASSERT_TRUE(req.parse(
        "GET / HTTP/1.1\r\nHost: h\r\nConnection: keep-alive, upgrade\r\n"));
    EXPECT_EQ(req.options.keep_alive(req.version), after_response::keep_alive);
  }
  // Case-insensitive: `"Close"` -> close.
  {
    request_head req;
    ASSERT_TRUE(
        req.parse("GET / HTTP/1.1\r\nHost: h\r\nConnection: Close\r\n"));
    EXPECT_EQ(req.options.keep_alive(req.version), after_response::close);
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
    // invalid version: returns empty.
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

// Verify that `create` with a null loop starts its own `epoll_loop_runner`.
void HttpServer_OwnLoop() {
  if (is_codex()) return;

  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0});
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}

// Verify that `create` with a shared loop stores and uses it.
void HttpServer_SharedLoop() {
  if (is_codex()) return;

  epoll_loop_runner runner;
  auto server =
      make_test_server(net_endpoint{ipv4_addr::loopback, 0}, runner.loop());
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}

// Verify that `create` returns null when the listen socket cannot be created
// (e.g., an invalid endpoint).
void HttpServer_Create_BadEndpoint() {
  auto server = make_test_server(net_endpoint{});
  EXPECT_FALSE(server);
}

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

// Verify that `create` with a shared `timing_wheel` stores and uses it.
void HttpServer_SharedWheel() {
  if (is_codex()) return;

  timing_wheel_runner wheel;
  auto server = make_test_server(net_endpoint{ipv4_addr::loopback, 0}, nullptr,
      wheel.wheel());
  ASSERT_TRUE(server);
  EXPECT_TRUE(server->local_endpoint());
}

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
// rejected: `std::nullopt` is returned and `name` is left unchanged.
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

// Verify `is_valid_field_value`: empty and printable are accepted; control
// characters and DEL are rejected; obs-text bytes (>= 0x80) are accepted.
void HttpHeaderBlock_IsValidFieldValue() {
  EXPECT_TRUE(http_headers::is_valid_field_value(""));
  EXPECT_TRUE(http_headers::is_valid_field_value("text/html; charset=utf-8"));
  EXPECT_TRUE(http_headers::is_valid_field_value(" padded value "));
  EXPECT_TRUE(http_headers::is_valid_field_value("value\twith\ttab"));
  // obs-text (>= 0x80): valid per RFC 9110 field-value grammar.
  EXPECT_TRUE(http_headers::is_valid_field_value("\x80\xff"));
  // Null byte: invalid.
  EXPECT_FALSE(
      http_headers::is_valid_field_value(std::string_view("a\0b", 3)));
  // CR and LF: invalid.
  EXPECT_FALSE(http_headers::is_valid_field_value("bad\rvalue"));
  EXPECT_FALSE(http_headers::is_valid_field_value("bad\nvalue"));
  // DEL (0x7F): invalid.
  EXPECT_FALSE(http_headers::is_valid_field_value("bad\x7fvalue"));
  // Other control chars (< 0x20, not HTAB): invalid.
  EXPECT_FALSE(http_headers::is_valid_field_value("bad\x01value"));
}

// Verify `content_length` in `http_options`: set when present and parseable,
// `std::nullopt` when absent or non-numeric.
void HttpHeaderBlock_ContentLength() {
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Content-Length", "42"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.content_length);
    EXPECT_EQ(*opts.content_length, 42ULL);
  }
  {
    // Absent: nullopt.
    http_headers h;
    http_options opts;
    opts.extract(h);
    EXPECT_FALSE(opts.content_length);
  }
  {
    // Non-numeric: nullopt.
    http_headers h;
    EXPECT_TRUE(h.add_raw("Content-Length", "abc"));
    http_options opts;
    opts.extract(h);
    EXPECT_FALSE(opts.content_length);
  }
  {
    // Zero is a valid value.
    http_headers h;
    EXPECT_TRUE(h.add_raw("Content-Length", "0"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.content_length);
    EXPECT_EQ(*opts.content_length, 0ULL);
  }
}

// Verify `transfer_encoding` in `http_options`: `chunked` when recognized
// (case-insensitive) as the last token of the last field, `std::nullopt`
// otherwise.
void HttpHeaderBlock_IsChunked() {
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Transfer-Encoding", "chunked"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.transfer_encoding);
    EXPECT_EQ(*opts.transfer_encoding, transfer_encoding_value::chunked);
  }
  {
    // Mixed case value still matches.
    http_headers h;
    EXPECT_TRUE(h.add_raw("Transfer-Encoding", "Chunked"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.transfer_encoding);
    EXPECT_EQ(*opts.transfer_encoding, transfer_encoding_value::chunked);
  }
  {
    // `chunked` works as the last.
    http_headers h;
    EXPECT_TRUE(h.add_raw("Transfer-Encoding", "gzip,   chUnKed  "));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.transfer_encoding);
    EXPECT_EQ(*opts.transfer_encoding, transfer_encoding_value::chunked);
  }
  {
    // `chunked` does not work before other encodings.
    http_headers h;
    EXPECT_TRUE(h.add_raw("Transfer-Encoding", "chunked, gzip"));
    http_options opts;
    opts.extract(h);
    EXPECT_FALSE(opts.transfer_encoding);
  }
  {
    // Multiple fields: last token of last field is `chunked`.
    http_headers h;
    EXPECT_TRUE(h.add_raw("Transfer-Encoding", "gzip"));
    EXPECT_TRUE(h.add_raw("Transfer-Encoding", "chunked"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.transfer_encoding);
    EXPECT_EQ(*opts.transfer_encoding, transfer_encoding_value::chunked);
  }
  {
    // Multiple fields: a later field appends after `chunked` -- not chunked.
    http_headers h;
    EXPECT_TRUE(h.add_raw("Transfer-Encoding", "gzip, chunked"));
    EXPECT_TRUE(h.add_raw("Transfer-Encoding", "deflate"));
    http_options opts;
    opts.extract(h);
    EXPECT_FALSE(opts.transfer_encoding);
  }
  {
    // Absent: nullopt.
    http_headers h;
    http_options opts;
    opts.extract(h);
    EXPECT_FALSE(opts.transfer_encoding);
  }
}

// Verify `empty()`, `size()`, and ordered iteration.
void HttpHeaderBlock_SizeAndEmpty() {
  http_headers h;
  EXPECT_TRUE(h.empty());
  EXPECT_EQ(h.size(), 0ULL);

  EXPECT_TRUE(h.add_raw("Host", "example.com"));
  EXPECT_FALSE(h.empty());
  EXPECT_EQ(h.size(), 1ULL);

  EXPECT_TRUE(h.add_raw("Accept", "text/html"));
  EXPECT_EQ(h.size(), 2ULL);

  // Iteration visits fields in insertion order.
  auto it = h.begin();
  EXPECT_EQ(it->name, "Host");
  ++it;
  EXPECT_EQ(it->name, "Accept");
  ++it;
  EXPECT_TRUE(it == h.end());
}

// Verify 3-arg `add_raw`: `field_name` is the index key (canonical form);
// `raw_field_name` is the wire name stored in the entry.
void HttpHeaderBlock_AddRawWithRawName() {
  http_headers h;
  // Index key is canonical "Content-Type"; wire name is lowercase.
  EXPECT_TRUE(h.add_raw("Content-Type", "text/html", "content-type"));

  // `get()` looks up via canonical index key.
  const auto ct = h.get("Content-Type");
  ASSERT_TRUE(ct);
  EXPECT_EQ(*ct, "text/html");

  // `serialize()` writes the raw (wire) name, not the canonical key.
  std::string out;
  h.serialize(out);
  EXPECT_NE(out.find("content-type: text/html\r\n"), std::string::npos);
  EXPECT_EQ(out.find("Content-Type"), std::string::npos);
}

// Verify that `get()` returns only the first value when a field name appears
// multiple times (use `get_combined()` for all values).
void HttpHeaderBlock_GetReturnsFirst() {
  http_headers h;
  EXPECT_TRUE(h.add("Accept", "text/html"));
  EXPECT_TRUE(h.add("Accept", "application/json"));
  EXPECT_TRUE(h.add("Accept", "image/webp"));

  const auto first = h.get("Accept");
  ASSERT_TRUE(first);
  EXPECT_EQ(*first, "text/html");

  // `get_combined()` joins all three.
  EXPECT_EQ(h.get_combined("Accept"),
      "text/html, application/json, image/webp");
}

// Verify `keep_alive()` for HTTP/0.9: always `close`, regardless of any
// `Connection` header (HTTP/0.9 headers don't exist, but guard against it).
void HttpHeaderBlock_KeepAliveHttp09() {
  // HTTP/0.9 always yields `close` regardless of any `Connection` header.
  http_headers h;
  http_options opts;
  opts.extract(h);
  EXPECT_EQ(opts.keep_alive(http_version::http_0_9), after_response::close);

  // Even if a `Connection: keep-alive` header were somehow present,
  // HTTP/0.9 must still return `close`.
  EXPECT_TRUE(h.add_raw("Connection", "keep-alive"));
  opts = {};
  opts.extract(h);
  EXPECT_EQ(opts.keep_alive(http_version::http_0_9), after_response::close);
}

// Verify that an HTTP/0.9 request line with trailing header text causes
// `request_head::parse` to fail (HTTP/0.9 does not allow headers).
void HttpHeaderBlock_Http09WithHeaders() {
  request_head req;
  EXPECT_FALSE(req.parse("GET /\r\nHost: example.com\r\n"));
}

// Verify that more than five consecutive leading CRLFs cause `parse` to fail
// (RFC 9112 section 2.2 imposes a limit).
void HttpHeaderBlock_TooManyLeadingCrlfs() {
  {
    // Exactly 5 leading CRLFs: should still parse successfully.
    request_head req;
    EXPECT_TRUE(req.parse("\r\n\r\n\r\n\r\n\r\nGET / HTTP/1.1\r\n"));
    EXPECT_EQ(req.method, http_method::GET);
  }
  {
    // Six leading CRLFs: parse fails.
    request_head req;
    EXPECT_FALSE(req.parse("\r\n\r\n\r\n\r\n\r\n\r\nGET / HTTP/1.1\r\n"));
  }
}

// Verify that a target not starting with `'/'` causes `parse` to fail.
void HttpHeaderBlock_TargetNotPath() {
  {
    // Absolute URI form (HTTP/1.1 proxies): not accepted by this parser.
    request_head req;
    EXPECT_FALSE(req.parse("GET http://example.com/ HTTP/1.1\r\n"));
  }
  {
    // Authority form.
    request_head req;
    EXPECT_FALSE(req.parse("CONNECT example.com:443 HTTP/1.1\r\n"));
  }
}

// Verify that `request_head::clear()` restores default-constructed state so
// the object can be reused for a second request.
void HttpHeaderBlock_ClearRequest() {
  request_head req;
  ASSERT_TRUE(req.parse(
      "GET /path HTTP/1.1\r\nHost: example.com\r\nAccept: text/html\r\n"));
  EXPECT_EQ(req.method, http_method::GET);
  EXPECT_FALSE(req.headers.empty());

  req.clear();
  EXPECT_EQ(req.version, http_version{});
  EXPECT_EQ(req.method, http_method{});
  EXPECT_TRUE(req.target.empty());
  EXPECT_TRUE(req.headers.empty());
}

// Verify that `response_head::clear()` restores default-constructed state.
void HttpHeaderBlock_ClearResponse() {
  response_head resp;
  resp.version = http_version::http_1_1;
  resp.status_code = http_status_code::OK;
  resp.reason = "OK";
  EXPECT_TRUE(resp.headers.add_raw("Content-Length", "0"));

  resp.clear();
  EXPECT_EQ(resp.version, http_version{});
  EXPECT_EQ(resp.status_code, http_status_code{});
  EXPECT_TRUE(resp.reason.empty());
  EXPECT_TRUE(resp.headers.empty());
}

// Verify that `response_head::serialize()` returns an empty string when the
// version is `http_version::invalid`.
void HttpHeaderBlock_ResponseSerializeInvalid() {
  response_head resp;
  // Default-constructed version is `invalid`.
  EXPECT_TRUE(resp.serialize().empty());

  // Explicitly set to invalid.
  resp.version = http_version::invalid;
  resp.status_code = http_status_code::OK;
  resp.reason = "OK";
  EXPECT_TRUE(resp.serialize().empty());
}

// Verify `make_error_response()` with default and custom arguments.
void HttpHeaderBlock_MakeErrorResponse() {
  {
    // Defaults: HTTP/1.1 400 Bad Request, Connection: close.
    const auto wire = response_head::make_error_response();
    EXPECT_NE(wire.find("HTTP/1.1"), std::string::npos);
    EXPECT_NE(wire.find("400"), std::string::npos);
    EXPECT_NE(wire.find("Bad Request"), std::string::npos);
    EXPECT_NE(wire.find("Connection: close"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\n"));
  }
  {
    // Custom: HTTP/1.0 405 Method Not Allowed, Connection: keep-alive.
    const auto wire = response_head::make_error_response(
        after_response::keep_alive, http_version::http_1_0,
        http_status_code::METHOD_NOT_ALLOWED, "Method Not Allowed");
    EXPECT_NE(wire.find("HTTP/1.0"), std::string::npos);
    EXPECT_NE(wire.find("405"), std::string::npos);
    EXPECT_NE(wire.find("Method Not Allowed"), std::string::npos);
    EXPECT_NE(wire.find("Connection: keep-alive"), std::string::npos);
  }
}

// Verify `response_head::parse()` edge cases: empty reason phrase (trailing
// space after status code with no text) succeeds; missing space after status
// code fails.
void HttpHeaderBlock_ResponseParseEdgeCases() {
  {
    // Empty reason: status line is "HTTP/1.1 204 ", reason is "".
    response_head resp;
    ASSERT_TRUE(resp.parse("HTTP/1.1 204 "));
    EXPECT_EQ(resp.version, http_version::http_1_1);
    EXPECT_EQ(resp.status_code, http_status_code::NO_CONTENT);
    EXPECT_TRUE(resp.reason.empty());
  }
  {
    // No SP after status code: fails.
    response_head resp;
    EXPECT_FALSE(resp.parse("HTTP/1.1 200\r\n"));
  }
  {
    // Status code below 100: fails.
    response_head resp;
    EXPECT_FALSE(resp.parse("HTTP/1.1 99 Too Low\r\n"));
  }
  {
    // Status code above 999: fails.
    response_head resp;
    EXPECT_FALSE(resp.parse("HTTP/1.1 1000 Too High\r\n"));
  }
}

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

// Verify `http_options::extract` for `content_type` and `upgrade`, and
// `http_options::apply` writing values back into headers.
void HttpHeaderBlock_HttpOptionsExtractApply() {
  // content_type: recognized media type, parameters stripped.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Content-Type", "text/html; charset=utf-8"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.content_type);
    EXPECT_EQ(*opts.content_type, content_type_value::text_html);
  }
  // content_type: exact match without parameters.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Content-Type", "application/json"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.content_type);
    EXPECT_EQ(*opts.content_type, content_type_value::application_json);
  }
  // content_type: unrecognized -> `unknown`.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Content-Type", "application/octet-stream"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.content_type);
    EXPECT_EQ(*opts.content_type, content_type_value::unknown);
  }
  // content_type: absent -> nullopt.
  {
    http_headers h;
    http_options opts;
    opts.extract(h);
    EXPECT_FALSE(opts.content_type);
  }
  // upgrade: websocket recognized, only when Connection is Upgrade
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Connection", "Upgrade"));
    EXPECT_TRUE(h.add_raw("Upgrade", "websocket"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.upgrade);
    EXPECT_EQ(*opts.upgrade, upgrade_value::websocket);
  }
  // upgrade: unrecognized token -> `unknown`.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Connection", "Upgrade"));
    EXPECT_TRUE(h.add_raw("Upgrade", "h2c"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.upgrade);
    EXPECT_EQ(*opts.upgrade, upgrade_value::unknown);
  }
  // upgrade: websocket wins in a token list.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Connection", "Upgrade"));
    EXPECT_TRUE(h.add_raw("Upgrade", "h2c, websocket"));
    http_options opts;
    opts.extract(h);
    ASSERT_TRUE(opts.upgrade);
    EXPECT_EQ(*opts.upgrade, upgrade_value::websocket);
  }
  // apply: writes known values into headers, updating existing entries.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Content-Length", "0"));
    http_options opts;
    opts.content_length = 42;
    opts.content_type = content_type_value::text_plain;
    opts.connection = after_response::keep_alive;
    opts.apply(h);
    const auto cl = h.get("Content-Length");
    ASSERT_TRUE(cl);
    EXPECT_EQ(*cl, "42");
    const auto ct = h.get("Content-Type");
    ASSERT_TRUE(ct);
    EXPECT_EQ(*ct, "text/plain");
    const auto conn = h.get("Connection");
    ASSERT_TRUE(conn);
    EXPECT_EQ(*conn, "keep-alive");
  }
  // apply: `unknown` enum values and nullopt are not written.
  {
    http_headers h;
    http_options opts;
    opts.content_type = content_type_value::unknown;
    opts.apply(h);
    EXPECT_FALSE(h.get("Content-Type"));
  }
}

// Verify `get_values()`: iteration over all values for a field, `size()`,
// `empty()`, and `iterator::index()`.
void HttpHeaderBlock_GetValues() {
  // Empty range when field not found.
  {
    http_headers h;
    const auto r = h.get_values("Accept");
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(r.size(), 0ULL);
    EXPECT_TRUE(r.begin() == r.end());
  }
  // Single entry: correct value and non-empty range.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Accept", "text/html"));
    const auto r = h.get_values("Accept");
    EXPECT_FALSE(r.empty());
    EXPECT_EQ(r.size(), 1ULL);
    auto it = r.begin();
    EXPECT_EQ(*it, "text/html");
    ++it;
    EXPECT_TRUE(it == r.end());
  }
  // Multiple entries for the same field: returned in insertion order.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Accept", "text/html"));
    EXPECT_TRUE(h.add_raw("Host", "example.com")); // interleaved
    EXPECT_TRUE(h.add_raw("Accept", "application/json"));
    const auto r = h.get_values("Accept");
    EXPECT_EQ(r.size(), 2ULL);
    auto it = r.begin();
    EXPECT_EQ(*it, "text/html");
    ++it;
    EXPECT_EQ(*it, "application/json");
    ++it;
    EXPECT_TRUE(it == r.end());
  }
  // `iterator::set()` replaces the value in place.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Accept", "text/html"));
    EXPECT_TRUE(h.add_raw("Accept", "application/json"));
    auto r = h.get_values("Accept");
    for (auto it = r.begin(); it != r.end(); ++it)
      if (*it == "text/html") it.set("text/plain");
    auto it = r.begin();
    EXPECT_EQ(*it, "text/plain");
    ++it;
    EXPECT_EQ(*it, "application/json");
  }
}

// Verify `reset_raw()` upsert, `remove_entry()`, and `remove_key()`.
void HttpHeaderBlock_SetRawAndRemove() {
  // reset_raw adds when field is absent.
  {
    http_headers h;
    (void)h.reset_raw("Connection", "close");
    const auto v = h.get("Connection");
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, "close");
  }
  // reset_raw updates when one entry exists.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Connection", "keep-alive"));
    (void)h.reset_raw("Connection", "close");
    const auto v = h.get("Connection");
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, "close");
  }
  // reset_raw reduces multiple entries to one.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Accept", "text/html"));
    EXPECT_TRUE(h.add_raw("Accept", "application/json"));
    EXPECT_EQ(h.get_values("Accept").size(), 2ULL);
    (void)h.reset_raw("Accept", "text/plain");
    const auto v = h.get("Accept");
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, "text/plain");
    EXPECT_EQ(h.get_values("Accept").size(), 1ULL);
  }
  // reset_raw does not affect other fields.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Host", "example.com"));
    EXPECT_TRUE(h.add_raw("Accept", "text/html"));
    (void)h.reset_raw("Accept", "application/json");
    const auto host = h.get("Host");
    ASSERT_TRUE(host);
    EXPECT_EQ(*host, "example.com");
    const auto accept = h.get("Accept");
    ASSERT_TRUE(accept);
    EXPECT_EQ(*accept, "application/json");
  }
  // remove_entry: entry gone; other fields unaffected.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Host", "example.com"));
    EXPECT_TRUE(h.add_raw("Accept", "text/html"));
    h.remove_key("Accept");
    EXPECT_FALSE(h.get("Accept"));
    EXPECT_TRUE(h.get_values("Accept").empty());
    const auto host = h.get("Host");
    ASSERT_TRUE(host);
    EXPECT_EQ(*host, "example.com");
  }
  // remove_entry
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Accept", "text/html"));
    EXPECT_TRUE(h.add_raw("Accept", "application/json"));
    EXPECT_TRUE(h.add_raw("Accept", "image/webp"));
    const auto r = h.get_values("Accept");
    for (auto it = r.begin(); it != r.end(); ++it)
      if (*it == "application/json") it.tombstone();
    auto it = r.begin();
    ASSERT_TRUE(it != r.end());
    EXPECT_EQ(*it, "text/html");
    ++it;
    ASSERT_TRUE(it != r.end());
    EXPECT_EQ(*it, "image/webp");
    ++it;
    EXPECT_TRUE(it == r.end());
    const auto accept = h.get_combined("Accept");
    EXPECT_EQ(accept, "text/html, image/webp");
  }
  // remove_key: all entries for field gone; other fields unaffected.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Host", "example.com"));
    EXPECT_TRUE(h.add_raw("Accept", "text/html"));
    EXPECT_TRUE(h.add_raw("Accept", "application/json"));
    h.remove_key("Accept");
    EXPECT_FALSE(h.get("Accept"));
    EXPECT_TRUE(h.get_values("Accept").empty());
    const auto host = h.get("Host");
    ASSERT_TRUE(host);
    EXPECT_EQ(*host, "example.com");
  }
  // remove_key on absent field is a no-op.
  {
    http_headers h;
    EXPECT_TRUE(h.add_raw("Host", "example.com"));
    h.remove_key("Accept");
    const auto host = h.get("Host");
    ASSERT_TRUE(host);
    EXPECT_EQ(*host, "example.com");
  }
}

// `ws_frame_wrapper` and `http_websocket` unit tests.

// RFC 6455 section 1.3: known input -> known accept key.
void WebSocket_AcceptKey() {
  const auto key =
      ws_frame_view::compute_accept_key("dGhlIHNhbXBsZSBub25jZQ==");
  EXPECT_EQ(key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

// Serialize an unmasked text frame and verify the parsed header fields.
void WebSocket_FrameCodec_RoundTrip() {
  const std::string payload{"hello"};
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, payload);

  ASSERT_GE(frame.size(), 7ULL);
  ws_frame_view hdr{frame};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_TRUE(hdr.is_final());
  EXPECT_FALSE(hdr.is_masked());
  EXPECT_EQ(hdr.header_length(), 2ULL);
  EXPECT_EQ(hdr.payload_length(), 5ULL);
  EXPECT_EQ(hdr.total_length(), 7ULL);

  const std::string_view extracted{frame.data() + hdr.header_length(),
      hdr.payload_length()};
  EXPECT_EQ(extracted, payload);
}

// Client pump receives a single unmasked text frame from the server.
void WebSocket_Feed_SingleText() {
  std::string got_msg;
  ws_frame_control got_op{};
  http_websocket ws{[](any_strings&&) { return true; },
      connection_role::client};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    got_msg = std::move(p);
    got_op = op;
    return true;
  };
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "hello");
  std::string_view wire{frame};
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_EQ(got_msg, "hello");
  EXPECT_EQ(got_op, ws_frame_control::text);
}

// Invalid UTF-8 in a text frame fails the connection with close code 1007.
void WebSocket_Feed_SingleTextInvalidUtf8() {
  std::string sent_frame;
  http_websocket ws{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  bool msg_fired{};
  ws.on_message = [&](http_websocket&, std::string&&, ws_frame_control) {
    msg_fired = true;
    return true;
  };

  std::string wire_frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "\x80", 0x12345678U);
  std::string_view wire{wire_frame};
  EXPECT_EQ(ws.feed(wire), http_websocket::insatiable);
  EXPECT_FALSE(msg_fired);

  ws_frame_view hdr{sent_frame};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_EQ(hdr.opcode(), ws_frame_control::close);
  const std::string_view close_payload = std::string_view{sent_frame}.substr(
      hdr.header_length(), hdr.payload_length());
  ASSERT_GE(close_payload.size(), 2U);
  const uint16_t code =
      (static_cast<uint8_t>(close_payload[0]) << 8) |
      static_cast<uint8_t>(close_payload[1]);
  EXPECT_EQ(code, uint16_t{1007});
}

// Disabling UTF-8 validation allows invalid text payloads through.
void WebSocket_Feed_SingleTextInvalidUtf8Disabled() {
  std::string got_msg;
  ws_frame_control got_op{};
  bool msg_fired{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.validate_utf8 = false;
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    got_msg = std::move(p);
    got_op = op;
    msg_fired = true;
    return true;
  };

  std::string wire_frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "\x80", 0x12345678U);
  std::string_view wire{wire_frame};
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_TRUE(msg_fired);
  EXPECT_EQ(got_msg, std::string("\x80", 1));
  EXPECT_EQ(got_op, ws_frame_control::text);
}

// Server pump receives a masked binary frame and correctly unmasks it.
void WebSocket_Feed_MaskedBinary() {
  std::string got_msg;
  ws_frame_control got_op{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    got_msg = std::move(p);
    got_op = op;
    return true;
  };
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::binary, "world",
      uint32_t{0xDEADBEEF});
  std::string_view wire{frame};
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_EQ(got_msg, "world");
  EXPECT_EQ(got_op, ws_frame_control::binary);
}

// Server auto-pongs a ping frame and does not fire on_message.
void WebSocket_Feed_Ping() {
  std::string sent_frame;
  bool msg_fired{};
  http_websocket ws_server{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws_server.on_message =
      [&](http_websocket&, std::string&&, ws_frame_control) {
        msg_fired = true;
        return true;
      };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  EXPECT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::ping, "ping-payload"));
  std::string_view wire{received_frame};

  EXPECT_EQ(ws_server.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_FALSE(msg_fired);
  ASSERT_FALSE(sent_frame.empty());
  ws_frame_view hdr{sent_frame};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  const auto pong_op = static_cast<ws_frame_control>(
      static_cast<uint8_t>(hdr.opcode()) & 0x0FU);
  EXPECT_EQ(pong_op, ws_frame_control::pong);
  // Pong body must echo the ping payload.
  EXPECT_EQ(hdr.payload_length(), 12ULL);
}

// Server fires on_close with the correct status code and reason string.
void WebSocket_Feed_Close() {
  uint16_t got_code{};
  std::string got_reason;
  http_websocket ws_server{[](any_strings&&) { return true; }};
  ws_server.on_close =
      [&](http_websocket&, uint16_t code, std::string_view reason) {
        got_code = code;
        got_reason = reason;
      };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  EXPECT_TRUE(ws_client.send_close(1001, "going away"));
  std::string_view wire{received_frame};

  EXPECT_EQ(ws_server.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_EQ(got_code, uint16_t{1001});
  EXPECT_EQ(got_reason, "going away");
}

// Invalid UTF-8 in a close reason fails the connection with 1007.
void WebSocket_Feed_CloseInvalidUtf8Reason() {
  std::string sent_frame;
  bool close_fired{};
  http_websocket ws_server{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws_server.on_close = [&](http_websocket&, uint16_t, std::string_view) {
    close_fired = true;
  };
  std::string payload;
  payload.push_back(char{0x03});
  payload.push_back(static_cast<char>(0xE8)); // 1000
  payload.push_back(static_cast<char>(0x80)); // invalid UTF-8 reason byte
  const std::string received_frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::close, payload, 0x12345678U);
  std::string_view wire{received_frame};

  EXPECT_NE(ws_server.feed(wire), 0U);
  EXPECT_NE(wire.size(), 0U);
  EXPECT_FALSE(close_fired);

  ws_frame_view hdr{sent_frame};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_EQ(hdr.opcode(), ws_frame_control::close);
  const std::string_view close_payload = std::string_view{sent_frame}.substr(
      hdr.header_length(), hdr.payload_length());
  ASSERT_GE(close_payload.size(), 2U);
  const uint16_t code =
      (static_cast<uint8_t>(close_payload[0]) << 8) |
      static_cast<uint8_t>(close_payload[1]);
  EXPECT_EQ(code, uint16_t{1007});
}

// Three-frame fragmented message is assembled and delivered exactly once.
void WebSocket_Feed_Fragmented() {
  std::string got_msg;
  ws_frame_control got_op{};
  int msg_count{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    got_msg = std::move(p);
    got_op = op;
    ++msg_count;
    return true;
  };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  // Fragment 1: FIN=0, text opcode, "hel".
  EXPECT_TRUE(ws_client.send_frame(ws_frame_control::text, "hel"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_EQ(msg_count, 0);

  // Fragment 2: FIN=0, continuation, "lo ".
  EXPECT_TRUE(ws_client.send_frame(ws_frame_control::continuation, "lo "));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_EQ(msg_count, 0);

  // Fragment 3: FIN=1, continuation, "world" -> dispatch.
  EXPECT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "world"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_EQ(msg_count, 1);
  EXPECT_EQ(got_msg, "hello world");
  EXPECT_EQ(got_op, ws_frame_control::text);
}

// Three-frame fragmented message with deliver_fragments=true fires on_message
// once per frame. Non-final frames carry the data opcode without the fin bit;
// the final frame carries opcode|fin.
void WebSocket_Feed_FragmentedDelivery() {
  struct Call {
    std::string payload;
    ws_frame_control op{};
  };
  std::vector<Call> calls;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.deliver_fragments = true;
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    calls.push_back({std::move(p), op});
    return true;
  };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  // Fragment 1: FIN=0, text opcode, "hel".
  EXPECT_TRUE(ws_client.send_frame(ws_frame_control::text, "hel"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  ASSERT_EQ(calls.size(), 1U);
  EXPECT_EQ(calls[0].payload, "hel");
  EXPECT_EQ(calls[0].op, ws_frame_control::text);

  // Fragment 2: FIN=0, continuation, "lo ".
  EXPECT_TRUE(ws_client.send_frame(ws_frame_control::continuation, "lo "));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  ASSERT_EQ(calls.size(), 2U);
  EXPECT_EQ(calls[1].payload, "lo ");
  EXPECT_EQ(calls[1].op, ws_frame_control::text);

  // Fragment 3: FIN=1, continuation, "world" -> dispatched with fin bit.
  EXPECT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "world"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  ASSERT_EQ(calls.size(), 3U);
  EXPECT_EQ(calls[2].payload, "world");
  EXPECT_EQ(calls[2].op, ws_frame_control::fin | ws_frame_control::text);
}

// In fragment-delivery mode, a code point split across frames is accepted.
void WebSocket_Feed_FragmentedDeliverySplitUtf8() {
  struct Call {
    std::string payload;
    ws_frame_control op{};
  };
  std::vector<Call> calls;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.deliver_fragments = true;
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    calls.push_back({std::move(p), op});
    return true;
  };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  EXPECT_TRUE(ws_client.send_frame(ws_frame_control::text, "\xF0\x9F"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);

  EXPECT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "\x98\x80"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);

  ASSERT_EQ(calls.size(), 2U);
  EXPECT_EQ(calls[0].payload, "\xF0\x9F");
  EXPECT_EQ(calls[0].op, ws_frame_control::text);
  EXPECT_EQ(calls[1].payload, "\x98\x80");
  EXPECT_EQ(calls[1].op, ws_frame_control::fin | ws_frame_control::text);
}

// In fragment-delivery mode, invalid UTF-8 across frames fails with 1007.
void WebSocket_Feed_FragmentedDeliveryInvalidUtf8() {
  std::string sent_frame;
  http_websocket ws{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws.deliver_fragments = true;
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  EXPECT_TRUE(ws_client.send_frame(ws_frame_control::text, "\xF0"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);

  EXPECT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "A"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), http_websocket::insatiable);

  ws_frame_view hdr{sent_frame};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_EQ(hdr.opcode(), ws_frame_control::close);
  const std::string_view payload = std::string_view{sent_frame}.substr(
      hdr.header_length(), hdr.payload_length());
  ASSERT_GE(payload.size(), 2U);
  const uint16_t code =
      (static_cast<uint8_t>(payload[0]) << 8) |
      static_cast<uint8_t>(payload[1]);
  EXPECT_EQ(code, uint16_t{1007});
}

// An empty final fragment must still fail if the prior text ended
// mid-code-point.
void WebSocket_Feed_FragmentedDeliveryInvalidUtf8EmptyFinal() {
  std::string sent_frame;
  http_websocket ws{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws.deliver_fragments = true;
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  EXPECT_TRUE(ws_client.send_frame(ws_frame_control::text, "\xF0"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);

  EXPECT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, ""));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), http_websocket::insatiable);

  ws_frame_view hdr{sent_frame};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_EQ(hdr.opcode(), ws_frame_control::close);
  const std::string_view payload = std::string_view{sent_frame}.substr(
      hdr.header_length(), hdr.payload_length());
  ASSERT_GE(payload.size(), 2U);
  const uint16_t code =
      (static_cast<uint8_t>(payload[0]) << 8) |
      static_cast<uint8_t>(payload[1]);
  EXPECT_EQ(code, uint16_t{1007});
}

// Disabling UTF-8 validation also suppresses fragment-mode UTF-8 failures.
void WebSocket_Feed_FragmentedDeliveryInvalidUtf8Disabled() {
  struct Call {
    std::string payload;
    ws_frame_control op{};
  };
  std::vector<Call> calls;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.deliver_fragments = true;
  ws.validate_utf8 = false;
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control op) {
    calls.push_back({std::move(p), op});
    return true;
  };
  std::string received_frame;
  http_websocket ws_client{
      [&](any_strings&& frame) {
        received_frame = std::get<std::string>(std::move(frame));
        return true;
      },
      connection_role::client};
  std::string_view wire;

  EXPECT_TRUE(ws_client.send_frame(ws_frame_control::text, "\xF0"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);

  EXPECT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "A"));
  wire = received_frame;
  EXPECT_EQ(ws.feed(wire), 0U);

  ASSERT_EQ(calls.size(), 2U);
  EXPECT_EQ(calls[0].payload, std::string("\xF0", 1));
  EXPECT_EQ(calls[0].op, ws_frame_control::text);
  EXPECT_EQ(calls[1].payload, "A");
  EXPECT_EQ(calls[1].op, ws_frame_control::fin | ws_frame_control::text);
}

// Feeding only the header bytes of a frame returns true (awaiting payload).
void WebSocket_Feed_PartialFrame() {
  bool msg_fired{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&&, ws_frame_control) {
    msg_fired = true;
    return true;
  };
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "Hello", 0);
  const size_t frame_size = frame.size();
  std::string buf;
  std::string_view wire;

  // With 1 byte, it has no idea.
  buf = frame;
  buf.resize(1);
  wire = buf;
  EXPECT_EQ(ws.feed(wire), 2ULL);
  EXPECT_FALSE(msg_fired);

  // With 2, it knows what the frame is and knows it needs a mask, but won't
  // look any further.
  buf = frame;
  buf.resize(2);
  wire = buf;
  EXPECT_EQ(ws.feed(wire), 6ULL);
  EXPECT_FALSE(msg_fired);

  // With 6, it has the full frame, and knows it needs the payload.
  buf = frame;
  buf.resize(6);
  wire = buf;
  EXPECT_EQ(ws.feed(wire), frame_size);
  EXPECT_FALSE(msg_fired);

  // With 8, it knows it only has a partial payload.
  buf = frame;
  buf.resize(8);
  wire = buf;
  EXPECT_EQ(ws.feed(wire), frame_size);
  EXPECT_FALSE(msg_fired);

  // With the whole thing it works.
  buf = frame;
  wire = buf;
  EXPECT_EQ(ws.feed(wire), 0ULL);
  EXPECT_TRUE(msg_fired);
}

// `feed(recv_buffer_view&)` requests growth to the full frame size when a
// frame prefix fills the buffer but the completed frame would not fit after
// compaction.
void WebSocket_Feed_RecvBufferViewRequestsFrameSizedGrowth() {
  recv_buffer rb;
  rb.buffer.resize(256);
  const size_t capacity = rb.buffer.capacity();
  rb.min_capacity = capacity;
  rb.begin.store(0, std::memory_order::relaxed);
  rb.end.store(capacity, std::memory_order::relaxed);

  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text,
      std::string(capacity, 'x'));
  ASSERT_GT(frame.size(), capacity);
  std::memcpy(rb.buffer.data(), frame.data(), capacity);

  size_t resume_new_size{};
  size_t resume_last_seen_end{};
  {
    recv_buffer_view view{rb,
        [&](size_t n, size_t lse) {
          resume_new_size = n;
          resume_last_seen_end = lse;
        }};
    http_websocket ws{[](any_strings&&) { return true; }};
    EXPECT_TRUE(ws.feed(view));
  }

  EXPECT_EQ(resume_new_size, frame.size());
  EXPECT_EQ(resume_last_seen_end, capacity);
}

// Two complete frames in one buffer each fire on_message.
void WebSocket_Feed_MultipleFrames() {
  std::vector<std::string> msgs;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control) {
    msgs.emplace_back(std::move(p));
    return true;
  };
  const std::string both =
      ws_frame_lens::serialize_frame(
          ws_frame_control::fin | ws_frame_control::text, "foo", 0) +
      ws_frame_lens::serialize_frame(
          ws_frame_control::fin | ws_frame_control::text, "bar", 0);
  std::string_view wire{both};
  EXPECT_EQ(ws.feed(wire), 0ULL);
  ASSERT_EQ(msgs.size(), 2ULL);
  EXPECT_EQ(msgs[0], "foo");
  EXPECT_EQ(msgs[1], "bar");
}

// Two complete frames in one `recv_buffer_view` call: both must be delivered.
// This exercises `feed(recv_buffer_view&)` specifically, not the
// `feed(std::string_view&)` overload.
void WebSocket_Feed_MultipleFramesViaView() {
  std::vector<std::string> msgs;
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&& p, ws_frame_control) {
    msgs.emplace_back(std::move(p));
    return true;
  };
  const std::string both =
      ws_frame_lens::serialize_frame(
          ws_frame_control::fin | ws_frame_control::text, "foo", 0) +
      ws_frame_lens::serialize_frame(
          ws_frame_control::fin | ws_frame_control::text, "bar", 0);
  recv_buffer buf;
  buf.reads_enabled = false;
  buf.resize(both.size() + 1);
  std::memcpy(buf.buffer.data(), both.data(), both.size());
  buf.end.store(both.size(), std::memory_order::relaxed);
  recv_buffer_view view{buf, [](size_t, size_t) {}};
  EXPECT_TRUE(ws.feed(view));
  ASSERT_EQ(msgs.size(), 2ULL);
  EXPECT_EQ(msgs[0], "foo");
  EXPECT_EQ(msgs[1], "bar");
}

// A continuation frame without a prior start fragment is a protocol error.
void WebSocket_Feed_BadContinuation() {
  http_websocket ws{[](any_strings&&) { return true; }};
  std::string buf = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "data");
  std::string_view wire{buf};
  EXPECT_EQ(ws.feed(wire), -1ULL);
}

// A non-continuation data frame arriving mid-fragment is a protocol error.
void WebSocket_Feed_InterleavedData() {
  http_websocket ws{[](any_strings&&) { return true; }};
  std::string buf =
      ws_frame_lens::serialize_frame(ws_frame_control::text, "start", 0);
  std::string_view wire{buf};
  EXPECT_EQ(ws.feed(wire), 0ULL);

  // This shouldn't be marked as text.
  buf = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "bad", 0);
  wire = buf;
  EXPECT_EQ(ws.feed(wire), -1ULL);
}

// Data frame received after we sent a close is silently discarded.
// `on_message` must not fire and `feed` must succeed (not return
// `insatiable`).
void WebSocket_Feed_DataAfterSentClose() {
  bool msg_fired{};
  http_websocket ws{[](any_strings&&) { return true; }};
  ws.on_message = [&](http_websocket&, std::string&&, ws_frame_control) {
    msg_fired = true;
    return true;
  };
  ASSERT_TRUE(ws.send_close(1000));
  ASSERT_TRUE(ws.is_close_started());

  // Peer sends a text frame after we've already sent our close.
  const std::string frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "late", 0);
  std::string_view wire{frame};
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_FALSE(msg_fired);
}

// Data frame received after we received a close is silently discarded.
// `on_message` must not fire and `feed` must succeed (not return
// `insatiable`).
void WebSocket_Feed_DataAfterReceivedClose() {
  bool msg_fired{};
  std::string sent_frame;
  http_websocket ws{[&](any_strings&& f) {
    sent_frame = std::get<std::string>(std::move(f));
    return true;
  }};
  ws.on_message = [&](http_websocket&, std::string&&, ws_frame_control) {
    msg_fired = true;
    return true;
  };

  // Feed an inbound close from a masked client frame.
  std::string close_frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::close, {}, 0x12345678U);
  std::string_view wire{close_frame};
  ASSERT_EQ(ws.feed(wire), 0U);
  ASSERT_TRUE(ws.is_close_started());

  // Peer sends a text frame after their own close frame.
  const std::string frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "late", 0x12345678U);
  wire = frame;
  EXPECT_EQ(ws.feed(wire), 0U);
  EXPECT_EQ(wire.size(), 0U);
  EXPECT_FALSE(msg_fired);
}

// Server pump sends unmasked frames.
void WebSocket_Send_Server() {
  std::string sent;
  http_websocket ws{[&](any_strings&& f) {
    sent = std::get<std::string>(std::move(f));
    return true;
  }};
  EXPECT_TRUE(ws.send_text("hi"));
  ASSERT_FALSE(sent.empty());
  ws_frame_view hdr{sent};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_FALSE(hdr.is_masked());
  EXPECT_EQ(hdr.payload_length(), 2ULL);
  // Payload is plaintext; verify it directly.
  const std::string_view pl{sent.data() + hdr.header_length(), 2};
  EXPECT_EQ(pl, "hi");
}

// Client pump sends masked frames; payload round-trips via unmask.
void WebSocket_Send_Client() {
  std::string sent;
  http_websocket ws{
      [&](any_strings&& f) {
        sent = std::get<std::string>(std::move(f));
        return true;
      },
      connection_role::client};
  EXPECT_TRUE(ws.send_text("hi"));
  ASSERT_FALSE(sent.empty());
  ws_frame_view hdr{sent};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_TRUE(hdr.is_masked());
  EXPECT_EQ(hdr.payload_length(), 2ULL);
  char unmasked[2]{};
  EXPECT_TRUE(hdr.mask_payload_copy(unmasked,
      {sent.data() + hdr.header_length(), hdr.payload_length()}));
  EXPECT_EQ(std::string_view(unmasked, 2), "hi");
}

// `header()`, `header_view()`, and `payload_view()` return correctly bounded
// views after `parse`.
void WebSocket_FrameWrapper_HeaderAndViews() {
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "abc");
  ws_frame_view hdr{frame.data(), frame.size()};
  ASSERT_TRUE(hdr.is_complete() && hdr.parse());

  // `header_view` covers exactly the header bytes and begins at `header()`.
  EXPECT_EQ(hdr.header_view().size(), hdr.header_length());
  EXPECT_EQ(hdr.header_view().data(),
      reinterpret_cast<const char*>(&hdr.header()));

  // `payload_view` covers the payload bytes immediately after the header.
  EXPECT_EQ(hdr.payload_view().size(), hdr.payload_length());
  EXPECT_EQ(hdr.payload_view(), "abc");
}

// `copy_to` copies the header bytes into a caller-supplied buffer.
void WebSocket_FrameWrapper_CopyTo() {
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::binary, "xy");
  ws_frame_view hdr{frame.data(), frame.size()};
  ASSERT_TRUE(hdr.is_complete() && hdr.parse());

  char buf[14]{};
  EXPECT_TRUE(hdr.copy_to(buf));
  EXPECT_EQ(std::memcmp(buf, frame.data(), hdr.header_length()), 0);

  // A default-constructed (null) wrapper has no header to copy.
  ws_frame_view empty{};
  char buf2[14]{};
  EXPECT_FALSE(empty.copy_to(buf2));
}

// `mask_payload` unmasks the payload bytes of a masked frame in-place,
// exercising `mask_key()` and the mutable `variable_section()` accessor.
void WebSocket_FrameWrapper_MaskPayloadInPlace() {
  const std::string payload{"hello"};
  const uint32_t key = 0xDEADBEEF;
  std::string frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, payload, key);

  ws_frame_lens lens{frame};
  ASSERT_TRUE(lens.is_complete() && lens.parse());
  EXPECT_TRUE(lens.is_masked());
  EXPECT_NE(lens.mask_key(), 0U);

  // The mutable `variable_section()` pointer falls right after the two fixed
  // header bytes.
  EXPECT_EQ(reinterpret_cast<const char*>(lens.variable_section()),
      frame.data() + 2);

  // In-place unmask: payload must round-trip back to the original.
  EXPECT_TRUE(lens.mask_payload(frame));
  const std::string_view unmasked{frame.data() + lens.header_length(),
      lens.payload_length()};
  EXPECT_EQ(unmasked, payload);

  // Applying `mask_payload` again re-masks; the round-trip is symmetric.
  EXPECT_TRUE(lens.mask_payload(frame));
  EXPECT_NE(std::string_view(frame.data() + lens.header_length(),
                lens.payload_length()),
      payload);
}

// Verify that `mask_payload_copy` applies the mask in RFC 6455 byte order:
// each payload byte is XOR'd with masking-key-octet-(i mod 4), where the
// octets are taken in frame (network) order, not as a 32-bit integer.
//
// Uses the concrete example from RFC 6455 Section 5.7:
//   masking key bytes: 0x37 0xfa 0x21 0x3d
//   payload:           "Hello"
//   masked:            0x7f 0x9f 0x4d 0x51 0x58
//
// The second case ("Hello, World!") exercises both the 8-byte-at-a-time main
// loop and the trailing straggler loop.
void WebSocket_FrameWrapper_MaskPayloadCopyByteOrder() {
  // `build` stores mask_ in host order and writes hton32(mask_) to the frame.
  // To get frame bytes [0x37, 0xfa, 0x21, 0x3d] on a little-endian host,
  // hton32(mask_val) must equal 0x3d21fa37, so mask_val = 0x37fa213d.
  const uint32_t mask_val = 0x37FA213D;

  // Short payload: exercises only the straggler loop (n < 8).
  {
    const std::string_view payload = "Hello";
    const std::string expected{"\x7f\x9f\x4d\x51\x58", 5};

    std::string frame;
    auto lens = ws_frame_lens::build(frame,
        ws_frame_control::fin | ws_frame_control::text, payload.size(),
        mask_val);
    ASSERT_EQ(lens.mask_key(), mask_val);

    std::string dst(payload.size(), '\0');
    ASSERT_TRUE(lens.mask_payload_copy(dst.data(), payload));
    EXPECT_EQ(dst, expected);
  }

  // Longer payload: exercises the 8-byte main loop plus the straggler (13
  // bytes = 8 + 5).
  {
    const std::string_view payload = "Hello, World!";
    const std::string expected{
        "\x7f\x9f\x4d\x51\x58\xd6\x01\x6a\x58\x88\x4d\x59\x16", 13};

    std::string frame;
    auto lens = ws_frame_lens::build(frame,
        ws_frame_control::fin | ws_frame_control::text, payload.size(),
        mask_val);
    ASSERT_EQ(lens.mask_key(), mask_val);

    std::string dst(payload.size(), '\0');
    ASSERT_TRUE(lens.mask_payload_copy(dst.data(), payload));
    EXPECT_EQ(dst, expected);
  }
}

// `send_binary` sends a FIN+binary frame with the correct opcode.
void WebSocket_Send_Binary() {
  std::string sent;
  http_websocket ws{[&](any_strings&& f) {
    sent = std::get<std::string>(std::move(f));
    return true;
  }};
  EXPECT_TRUE(ws.send_binary("data"));
  ASSERT_FALSE(sent.empty());
  ws_frame_view hdr{sent};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_TRUE(hdr.is_final());
  EXPECT_EQ(hdr.opcode(), ws_frame_control::binary);
  EXPECT_EQ(hdr.payload_length(), 4ULL);
  EXPECT_EQ(std::string_view(sent.data() + hdr.header_length(), 4), "data");
}

// `send_pong` sends a FIN+pong frame even after `send_close` would otherwise
// block outbound data.
void WebSocket_Send_Pong_Direct() {
  std::string sent;
  http_websocket ws{[&](any_strings&& f) {
    sent = std::get<std::string>(std::move(f));
    return true;
  }};
  ASSERT_TRUE(ws.send_close(1000));
  EXPECT_TRUE(ws.send_pong("echo"));
  ws_frame_view hdr{sent};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_EQ(hdr.opcode(), ws_frame_control::pong);
  EXPECT_EQ(std::string_view(sent.data() + hdr.header_length(),
                hdr.payload_length()),
      "echo");
}

// `send_frame(std::string&&)` delivers a pre-serialized frame directly to the
// send callback.
void WebSocket_Send_Frame_Prebuilt() {
  std::string sent;
  http_websocket ws{[&](any_strings&& f) {
    sent = std::get<std::string>(std::move(f));
    return true;
  }};
  std::string frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "pre");
  EXPECT_TRUE(ws.send_frame(std::move(frame)));
  ws_frame_view hdr{sent};
  ASSERT_TRUE(hdr.is_complete());
  ASSERT_TRUE(hdr.parse());
  EXPECT_EQ(hdr.opcode(), ws_frame_control::text);
  EXPECT_EQ(hdr.payload_length(), 3ULL);
}

// `hangup` invokes the send callback with `std::monostate` to signal RST.
void WebSocket_Hangup() {
  bool called{};
  bool got_monostate{};
  http_websocket ws{[&](any_strings&& f) {
    called = true;
    got_monostate = std::holds_alternative<std::monostate>(f);
    return true;
  }};
  EXPECT_FALSE(ws.hangup());
  EXPECT_TRUE(called);
  EXPECT_TRUE(got_monostate);
}

// `fail` sends a close frame and returns false. `fail_proto` wraps `fail` with
// code 1002 and a "Protocol failure: " prefix. `fail_insatiable` sends close
// and hangup then returns `insatiable`.
void WebSocket_Fail() {
  // `fail` with no prior close sends a close frame.
  {
    std::string sent;
    http_websocket ws{[&](any_strings&& f) {
      sent = std::get<std::string>(std::move(f));
      return true;
    }};
    EXPECT_FALSE(ws.fail(1001, "bye"));
    ws_frame_view hdr{sent};
    ASSERT_TRUE(hdr.is_complete());
    ASSERT_TRUE(hdr.parse());
    EXPECT_EQ(hdr.opcode(), ws_frame_control::close);
  }

  // `fail_proto` sends close code 1002 with a prefixed reason string.
  {
    std::string sent;
    http_websocket ws{[&](any_strings&& f) {
      sent = std::get<std::string>(std::move(f));
      return true;
    }};
    EXPECT_FALSE(ws.fail_proto("test reason"));
    ws_frame_view hdr{sent};
    ASSERT_TRUE(hdr.is_complete());
    ASSERT_TRUE(hdr.parse());
    EXPECT_EQ(hdr.opcode(), ws_frame_control::close);
    const std::string_view close_pl{sent.data() + hdr.header_length(),
        hdr.payload_length()};
    ASSERT_GE(close_pl.size(), 2U);
    const uint16_t code =
        (static_cast<uint8_t>(close_pl[0]) << 8) |
        static_cast<uint8_t>(close_pl[1]);
    EXPECT_EQ(code, uint16_t{1002});
  }

  // `fail_insatiable` returns `insatiable`.
  {
    http_websocket ws{[](any_strings&&) { return true; }};
    EXPECT_EQ(ws.fail_insatiable(1000, "error"), http_websocket::insatiable);
  }
}

// `http_websocket_transaction` unit tests.

// Load `data` into `buf` and return a live view with a no-op resume callback.
// `buf` must outlive the returned view.
[[nodiscard]] recv_buffer_view
wstx_make_view(recv_buffer& buf, std::string_view data = {}) {
  buf.reads_enabled = false;
  buf.resize(std::max(data.size() + 1, size_t{256}));
  if (!data.empty()) std::memcpy(buf.buffer.data(), data.data(), data.size());
  buf.end.store(data.size(), std::memory_order::relaxed);
  buf.begin.store(0, std::memory_order::relaxed);
  return recv_buffer_view{buf, [](size_t, size_t) {}};
}

// Build a well-formed WebSocket upgrade `request_head` without parsing.
[[nodiscard]] request_head wstx_make_upgrade_req(
    std::string* accept_key_ptr = nullptr) {
  http_websocket hws{[](any_strings&&) { return true; }};
  std::string accept_key;
  request_head req =
      http_websocket::generate_upgrade_request("/ws", accept_key);
  if (accept_key_ptr) *accept_key_ptr = std::move(accept_key);
  return req;
}

void wstx_reextract_options(request_head& req) {
  req.options = {};
  req.options.extract(req.headers);
}

// `on_drain` callback on `http_websocket_transaction` is invoked from
// `handle_drain` after the upgrade response is flushed.
void WebSocketTransaction_OnDrain() {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());

  int drain_count{};
  tx->on_drain = [&](http_transaction&, const http_transaction::send_fn&) {
    ++drain_count;
    return stream_claim::claim;
  };

  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    ASSERT_EQ(tx->handle_data(view), stream_claim::claim);
  }

  http_transaction::send_fn send_fn{[](any_strings&&) { return true; }};
  // First drain flushes the 101 response, then falls through to `on_drain`.
  EXPECT_EQ(tx->handle_drain(send_fn), stream_claim::claim);
  EXPECT_EQ(drain_count, 1);

  // Subsequent drains with no pending response also route through `on_drain`.
  EXPECT_EQ(tx->handle_drain(send_fn), stream_claim::claim);
  EXPECT_EQ(drain_count, 2);
}

// Valid upgrade handshake: `handle_data` returns `claim`.
void WebSocketTransaction_UpgradeSuccess() {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  EXPECT_EQ(tx->handle_data(view), stream_claim::claim);
}

// After upgrade, `handle_drain` sends the 101 response and returns `claim`.
void WebSocketTransaction_DrainSendsResponse() {
  std::string expected_accept_key;
  auto tx = std::make_shared<http_websocket_transaction>(
      wstx_make_upgrade_req(&expected_accept_key));
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    ASSERT_EQ(tx->handle_data(view), stream_claim::claim);
  }

  std::string sent;
  http_transaction::send_fn send_fn{[&](any_strings&& data) {
    sent = std::get<std::string>(std::move(data));
    return true;
  }};
  EXPECT_EQ(tx->handle_drain(send_fn), stream_claim::claim);
  ASSERT_FALSE(sent.empty());

  // `parse()` expects the wire text without the trailing blank-line CRLF.
  response_head resp;
  ASSERT_TRUE(resp.parse(sent.substr(0, sent.size() - 2)));
  EXPECT_EQ(resp.status_code, http_status_code::SWITCHING_PROTOCOLS);

  const auto accept = resp.headers.get("Sec-Websocket-Accept");
  ASSERT_TRUE(accept);
  EXPECT_EQ(*accept, expected_accept_key);
}

// `handle_drain` before `handle_data` has been called returns `release`.
void WebSocketTransaction_DrainBeforeUpgrade() {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());
  http_transaction::send_fn send_fn{[](any_strings&&) { return true; }};
  EXPECT_EQ(tx->handle_drain(send_fn), stream_claim::release);
}

// Non-GET method: `handle_data` returns `release`.
void WebSocketTransaction_BadMethod() {
  auto req = wstx_make_upgrade_req();
  req.method = http_method::POST;
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  EXPECT_EQ(tx->handle_data(view), stream_claim::release);
}

// No `Upgrade` option: `handle_data` returns `release`.
void WebSocketTransaction_MissingUpgrade() {
  auto req = wstx_make_upgrade_req();
  req.options.upgrade = std::nullopt;
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  EXPECT_EQ(tx->handle_data(view), stream_claim::release);
}

// Missing `Connection` header: `handle_data` returns `release`.
void WebSocketTransaction_MissingConnection() {
  auto req = wstx_make_upgrade_req();
  req.headers.remove_key("Connection");
  wstx_reextract_options(req);
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  EXPECT_EQ(tx->handle_data(view), stream_claim::release);
}

// Wrong `Sec-Websocket-Version`: `handle_data` returns `release`.
void WebSocketTransaction_WrongVersion() {
  auto req = wstx_make_upgrade_req();
  req.headers.remove_key("Sec-Websocket-Version");
  (void)req.headers.add_raw("Sec-Websocket-Version", "8");
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  EXPECT_EQ(tx->handle_data(view), stream_claim::release);
}

// Missing `Sec-Websocket-Key`: `handle_data` returns `release`.
void WebSocketTransaction_MissingKey() {
  auto req = wstx_make_upgrade_req();
  req.headers.remove_key("Sec-Websocket-Key");
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  auto view = wstx_make_view(buf);
  EXPECT_EQ(tx->handle_data(view), stream_claim::release);
}

// Wrong `Sec-Websocket-Version`: drain sends 426 with the required headers and
// `close_after` stays `keep_alive` so the connection remains open for retry.
void WebSocketTransaction_UpgradeRequiredDrain() {
  auto req = wstx_make_upgrade_req();
  req.headers.remove_key("Sec-Websocket-Version");
  (void)req.headers.add_raw("Sec-Websocket-Version", "8");
  // Do not call wstx_reextract_options: `Sec-Websocket-Version` is read
  // directly from headers (not stored in options), so no re-extraction is
  // needed, and re-extracting would clobber `options.upgrade` since
  // `generate_upgrade_request` sets it directly without a header.
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    ASSERT_EQ(tx->handle_data(view), stream_claim::release);
  }

  std::string sent;
  http_transaction::send_fn send_fn{[&](any_strings&& data) {
    sent = std::get<std::string>(std::move(data));
    return true;
  }};
  EXPECT_EQ(tx->handle_drain(send_fn), stream_claim::release);
  ASSERT_FALSE(sent.empty());

  response_head resp;
  ASSERT_TRUE(resp.parse(sent.substr(0, sent.size() - 2)));
  EXPECT_EQ(resp.status_code, http_status_code::UPGRADE_REQUIRED);

  const auto upgrade = resp.headers.get("Upgrade");
  ASSERT_TRUE(upgrade);
  EXPECT_EQ(*upgrade, "websocket");

  const auto connection = resp.headers.get("Connection");
  ASSERT_TRUE(connection);
  EXPECT_EQ(*connection, "Upgrade");

  const auto version = resp.headers.get("Sec-Websocket-Version");
  ASSERT_TRUE(version);
  EXPECT_EQ(*version, "13");

  EXPECT_EQ(tx->close_after, after_response::keep_alive);
}

// After a rejected upgrade, `handle_drain` sends the 400 error and returns
// `release`.
void WebSocketTransaction_BadRequestDrain() {
  auto req = wstx_make_upgrade_req();
  req.method = http_method::POST;
  auto tx = std::make_shared<http_websocket_transaction>(std::move(req));
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    ASSERT_EQ(tx->handle_data(view), stream_claim::release);
  }

  std::string sent;
  http_transaction::send_fn send_fn{[&](any_strings&& data) {
    sent = std::get<std::string>(std::move(data));
    return true;
  }};
  EXPECT_EQ(tx->handle_drain(send_fn), stream_claim::release);
  ASSERT_FALSE(sent.empty());

  response_head resp;
  ASSERT_TRUE(resp.parse(sent.substr(0, sent.size() - 2)));
  EXPECT_EQ(resp.status_code, http_status_code::BAD_REQUEST);
}

// After upgrade, a text frame fires `on_message` and `handle_data` returns
// `claim`.
void WebSocketTransaction_FeedAfterUpgrade() {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());

  std::string got_msg;
  ws_frame_control got_op{};
  tx->websocket().on_message =
      [&](http_websocket&, std::string&& p, ws_frame_control op) {
        got_msg = std::move(p);
        got_op = op;
        return true;
      };
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    ASSERT_EQ(tx->handle_data(view), stream_claim::claim);
  }
  http_transaction::send_fn send_fn{[](any_strings&&) { return true; }};
  ASSERT_EQ(tx->handle_drain(send_fn), stream_claim::claim);

  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::text, "hello", 0);
  auto view2 = wstx_make_view(buf, frame);
  EXPECT_EQ(tx->handle_data(view2), stream_claim::claim);
  EXPECT_EQ(got_msg, "hello");
  EXPECT_EQ(got_op, ws_frame_control::text);
}

// After upgrade, a protocol-error frame causes `handle_data` to keep the
// stream claimed, latch close-pending, and let `handle_drain` begin graceful
// shutdown.
void WebSocketTransaction_FeedProtocolError() {
  auto tx =
      std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());
  recv_buffer buf;
  {
    auto view = wstx_make_view(buf);
    ASSERT_EQ(tx->handle_data(view), stream_claim::claim);
  }
  http_transaction::send_fn send_fn{[](any_strings&&) { return true; }};
  ASSERT_EQ(tx->handle_drain(send_fn), stream_claim::claim);

  // A continuation frame with no prior start fragment is a protocol error.
  const auto frame = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "data", 0);
  auto view2 = wstx_make_view(buf, frame);
  EXPECT_EQ(tx->handle_data(view2), stream_claim::claim);
  EXPECT_TRUE(tx->websocket().is_close_pending());
  EXPECT_EQ(tx->handle_drain(send_fn), stream_claim::release);
}

// When `on_protocol` is set and the client offers subprotocols, the chosen
// protocol appears in the 101 response. When the client offers no protocols,
// the callback is not invoked and the response has no protocol header.
void WebSocketTransaction_SubprotocolNegotiation() {
  // Case 1: client offers "chat, superchat"; callback picks "chat".
  {
    auto req = wstx_make_upgrade_req();
    (void)req.headers.add_raw("Sec-Websocket-Protocol", "chat, superchat");
    auto tx = std::make_shared<http_websocket_transaction>(std::move(req));

    bool called{};
    tx->on_protocol = [&](std::string_view offered) -> std::string {
      called = true;
      EXPECT_EQ(offered, "chat, superchat");
      return "chat";
    };

    recv_buffer buf;
    {
      auto view = wstx_make_view(buf);
      ASSERT_EQ(tx->handle_data(view), stream_claim::claim);
    }

    std::string sent;
    http_transaction::send_fn send_fn{[&](any_strings&& data) {
      sent = std::get<std::string>(std::move(data));
      return true;
    }};
    ASSERT_EQ(tx->handle_drain(send_fn), stream_claim::claim);
    EXPECT_TRUE(called);

    response_head resp;
    ASSERT_TRUE(resp.parse(sent.substr(0, sent.size() - 2)));
    EXPECT_EQ(resp.status_code, http_status_code::SWITCHING_PROTOCOLS);
    const auto proto = resp.headers.get("Sec-Websocket-Protocol");
    ASSERT_TRUE(proto);
    EXPECT_EQ(*proto, "chat");
  }

  // Case 2: client sends no protocol header; callback is not invoked and the
  // response has no `Sec-Websocket-Protocol` header.
  {
    auto tx =
        std::make_shared<http_websocket_transaction>(wstx_make_upgrade_req());

    bool called{};
    tx->on_protocol = [&](std::string_view) -> std::string {
      called = true;
      return "chat";
    };

    recv_buffer buf;
    {
      auto view = wstx_make_view(buf);
      ASSERT_EQ(tx->handle_data(view), stream_claim::claim);
    }

    std::string sent;
    http_transaction::send_fn send_fn{[&](any_strings&& data) {
      sent = std::get<std::string>(std::move(data));
      return true;
    }};
    ASSERT_EQ(tx->handle_drain(send_fn), stream_claim::claim);
    EXPECT_FALSE(called);

    response_head resp;
    ASSERT_TRUE(resp.parse(sent.substr(0, sent.size() - 2)));
    EXPECT_EQ(resp.status_code, http_status_code::SWITCHING_PROTOCOLS);
    EXPECT_FALSE(resp.headers.get("Sec-Websocket-Protocol"));
  }
}

// `make_factory` creates an `http_websocket_transaction` and invokes the
// configure callback.
void WebSocketTransaction_MakeFactory() {
  bool configured{};
  auto factory = http_websocket_transaction::make_factory(
      [&](http_websocket_transaction& wstx) {
        wstx.websocket().on_message =
            [](http_websocket&, std::string&&, ws_frame_control) {
              return true;
            };
        configured = true;
        return true;
      });

  auto tx = factory(wstx_make_upgrade_req());
  ASSERT_TRUE(tx);
  EXPECT_TRUE(configured);

  recv_buffer buf;
  auto view = wstx_make_view(buf);
  EXPECT_EQ(tx->handle_data(view), stream_claim::claim);
}

// Verify `send_ping` uses an auto-incrementing 4-byte counter payload and that
// a matching pong fires `on_pong` while a mismatched pong does not.
void WebSocket_PingCounter() {
  using namespace std::chrono_literals;

  std::string sent_frame;
  http_websocket ws{
      [&](any_strings&& f) {
        sent_frame = std::get<std::string>(std::move(f));
        return true;
      },
      connection_role::server};

  bool pong_fired{};
  ws.on_pong = [&](http_websocket&) {
    pong_fired = true;
    return true;
  };

  // First ping: counter becomes 1, payload is {0,0,0,1}.
  EXPECT_FALSE(ws.pong_pending());
  ASSERT_TRUE(ws.send_ping());
  EXPECT_TRUE(ws.pong_pending());
  ASSERT_FALSE(sent_frame.empty());

  ws_frame_view hdr1{sent_frame};
  ASSERT_TRUE(hdr1.is_complete());
  ASSERT_TRUE(hdr1.parse());
  EXPECT_EQ(hdr1.opcode(), ws_frame_control::ping);
  EXPECT_EQ(hdr1.payload_length(), 4ULL);
  // Payload encodes counter value 1 big-endian.
  std::string_view p1 =
      std::string_view{sent_frame}.substr(hdr1.header_length(), 4);
  EXPECT_EQ(static_cast<uint8_t>(p1[3]), 1U);

  // Feed a masked pong with a wrong payload: `on_pong` must not fire.
  // Server-mode ws requires masked client frames (RFC 6455 section 5.3).
  std::string bad_pong = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::pong, "xxxx", 0x12345678U);
  std::string_view bad_sv{bad_pong};
  EXPECT_NE(ws.feed(bad_sv), http_websocket::insatiable);
  EXPECT_FALSE(pong_fired);
  EXPECT_TRUE(ws.pong_pending()); // still pending

  // Feed a masked pong with the correct 4-byte counter payload.
  std::string good_pong = ws_frame_lens::serialize_frame(
      ws_frame_control::fin | ws_frame_control::pong, p1.substr(0, 4),
      0x12345678U);
  std::string_view good_sv{good_pong};
  EXPECT_NE(ws.feed(good_sv), http_websocket::insatiable);
  EXPECT_TRUE(pong_fired);
  EXPECT_FALSE(ws.pong_pending());

  // Second ping: counter becomes 2.
  pong_fired = false;
  ASSERT_TRUE(ws.send_ping());
  ws_frame_view hdr2{sent_frame};
  ASSERT_TRUE(hdr2.is_complete());
  ASSERT_TRUE(hdr2.parse());
  std::string_view p2 =
      std::string_view{sent_frame}.substr(hdr2.header_length(), 4);
  EXPECT_EQ(static_cast<uint8_t>(p2[3]), 2U);
}

// Verify that a live WebSocket server sends periodic pings when keepalive is
// enabled and that a client responding with matching pongs keeps the
// connection open. Uses 100 ms intervals so the test runs quickly.
void HttpServer_WebSocket_Keepalive() {
  if (is_codex()) return;

  using namespace std::chrono_literals;

  epoll_loop_runner loop_runner;
  timing_wheel_runner wheel_runner;

  auto server = http_server::create(
      net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [loop = s.loop(), wheel = s.wheel()](
                    http_websocket_transaction& tx) {
                  return tx.enable_keepalive(loop, wheel, 100ms, 100ms);
                }));
      },
      loop_runner.loop(), wheel_runner.wheel(),
      /*request_timeout=*/0s, /*write_timeout=*/0s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  // Perform WebSocket upgrade.
  std::string accept_key;
  http_transaction::send_fn send_fn{[&](any_strings&& f) {
    return client.send(std::get<std::string>(f));
  }};
  http_websocket ws_client{std::move(send_fn), connection_role::client};
  auto req = http_websocket::generate_upgrade_request("/ws", accept_key);
  (void)req.headers.add_raw("Host", "localhost");
  ASSERT_TRUE(client.send(req.serialize()));

  const auto resp_wire = client.recv_until("\r\n\r\n");
  ASSERT_FALSE(resp_wire.empty());
  auto resp_sv = std::string_view{resp_wire};
  resp_sv.remove_suffix(2);
  response_head resp;
  ASSERT_TRUE(resp.parse(resp_sv));
  EXPECT_EQ(resp.status_code, http_status_code::SWITCHING_PROTOCOLS);

  // Respond to several pings from the server; verify connection stays open.
  int pings_answered{};
  bool got_close{};
  ws_client.on_close = [&](http_websocket&, uint16_t, std::string_view) {
    got_close = true;
  };

  // Loop: receive frames from server, answer pings, stop after 3 answers.
  // Each recv() blocks for up to the connect timeout (1 s); pings arrive
  // every 100 ms so each iteration completes quickly.
  while (pings_answered < 3 && !got_close) {
    auto chunk = client.recv();
    if (chunk.empty()) break;
    std::string_view sv{chunk};
    while (!sv.empty()) {
      ws_frame_view hdr{sv.data(), sv.size()};
      if (!hdr.is_complete() || !hdr.parse()) break;
      if (hdr.opcode() == ws_frame_control::ping) {
        // Echo the payload back as a pong.
        std::string_view payload =
            sv.substr(hdr.header_length(), hdr.payload_length());
        EXPECT_TRUE(ws_client.send_pong(payload));
        ++pings_answered;
      }
      sv.remove_prefix(hdr.total_length());
    }
  }

  EXPECT_GE(pings_answered, 3);
  EXPECT_FALSE(got_close);

  // Clean close: client initiates, waits for server echo so the connection
  // is fully torn down before the test's `server` shared_ptr goes out of
  // scope. Without this, the 4th ping timer fires while `server` (captured
  // raw in the send_fn lambda) has already been destroyed, causing UB.
  EXPECT_TRUE(ws_client.send_close(1000, ""));
  while (!ws_client.is_close_pending()) {
    auto chunk = client.recv();
    if (chunk.empty()) break;
    std::string_view sv{chunk};
    (void)ws_client.feed(sv);
  }
}

// Verify that the server closes the connection with code 1001 when the client
// ignores pings and the pong timeout expires.
void HttpServer_WebSocket_KeepaliveTimeout() {
  if (is_codex()) return;

  using namespace std::chrono_literals;

  epoll_loop_runner loop_runner;
  timing_wheel_runner wheel_runner;

  auto server = http_server::create(
      net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [loop = s.loop(), wheel = s.wheel()](
                    http_websocket_transaction& tx) {
                  return tx.enable_keepalive(loop, wheel, 100ms, 100ms);
                }));
      },
      loop_runner.loop(), wheel_runner.wheel(),
      /*request_timeout=*/0s, /*write_timeout=*/0s);
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  // Perform WebSocket upgrade.
  std::string accept_key;
  http_transaction::send_fn send_fn{[&](any_strings&& f) {
    return client.send(std::get<std::string>(f));
  }};
  http_websocket ws_client{std::move(send_fn), connection_role::client};
  auto req = http_websocket::generate_upgrade_request("/ws", accept_key);
  (void)req.headers.add_raw("Host", "localhost");
  ASSERT_TRUE(client.send(req.serialize()));

  const auto resp_wire = client.recv_until("\r\n\r\n");
  ASSERT_FALSE(resp_wire.empty());
  auto resp_sv = std::string_view{resp_wire};
  resp_sv.remove_suffix(2);
  response_head resp;
  ASSERT_TRUE(resp.parse(resp_sv));
  EXPECT_EQ(resp.status_code, http_status_code::SWITCHING_PROTOCOLS);

  // Do not respond to pings; wait for the server to close with code 1001.
  // The close arrives ~200 ms after upgrade (100 ms ping + 100 ms pong
  // timeout). The connect timeout (1 s) gives enough headroom.
  //
  // Note: do NOT call ws_client.feed() here -- that would auto-pong every
  // ping, defeating the purpose of this test. Parse frames manually and
  // discard pings without replying.
  uint16_t got_close_code{};
  while (got_close_code == 0) {
    auto chunk = client.recv();
    if (chunk.empty()) break; // recv() timed out or EOF
    std::string_view sv{chunk};
    while (!sv.empty()) {
      ws_frame_view hdr{sv.data(), sv.size()};
      if (!hdr.is_complete() || !hdr.parse()) break;
      if (hdr.opcode() == ws_frame_control::close) {
        std::string_view payload =
            sv.substr(hdr.header_length(), hdr.payload_length());
        got_close_code =
            (payload.size() >= 2)
                ? static_cast<uint16_t>(
                      (static_cast<uint8_t>(payload[0]) << 8) |
                      static_cast<uint8_t>(payload[1]))
                : 1000U;
      }
      // Pings are intentionally ignored (no pong sent).
      sv.remove_prefix(hdr.total_length());
    }
  }

  EXPECT_EQ(got_close_code, 1001U);
}

// Semi-integration test: `http_server` with `http_websocket_transaction` as
// the route handler; client uses `stream_sync` for I/O and `http_websocket`
// for WebSocket framing.
//
// Flow:
//   1. Server registers an echo handler under `"/ws"`.
//   2. Client sends an HTTP/1.1 upgrade request and receives 101.
//   3. Client sends a masked text frame; server echoes it back unmasked.
//   4. Client decodes the echo and verifies the payload.
void HttpServer_WebSocket() {
  if (is_codex()) return;

  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [](http_websocket_transaction& tx) {
                  tx.websocket().on_message =
                      [](http_websocket& ws, std::string&& p,
                          ws_frame_control) { return ws.send_text(p); };
                  return true;
                }));
      });
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  // Send a valid HTTP/1.1 WebSocket upgrade request. The RFC 6455 test-vector
  // key produces accept value `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`.
  ASSERT_TRUE(client.send(
      "GET /ws/ HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-Websocket-Version: 13\r\n"
      "Sec-Websocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "\r\n"));

  // Receive and verify the 101 Switching Protocols response.
  const auto resp_wire = client.recv_until("\r\n\r\n");
  ASSERT_FALSE(resp_wire.empty());
  auto resp_head_wire = std::string_view{resp_wire};
  ASSERT_GE(resp_head_wire.size(), 2U);
  resp_head_wire.remove_suffix(2);
  response_head resp;
  ASSERT_TRUE(resp.parse(resp_head_wire));
  EXPECT_EQ(resp.status_code, http_status_code::SWITCHING_PROTOCOLS);
  const auto accept = resp.headers.get("Sec-Websocket-Accept");
  ASSERT_TRUE(accept);
  EXPECT_EQ(*accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

  // Build a client-side WebSocket pump that writes through `client`.
  std::string got_msg;
  ws_frame_control got_op{};
  http_transaction::send_fn client_send{[&](any_strings&& frame) {
    return client.send(std::get<std::string>(frame));
  }};
  http_websocket ws_client{std::move(client_send), connection_role::client};
  ws_client.on_message =
      [&](http_websocket&, std::string&& p, ws_frame_control op) {
        got_msg = std::move(p);
        got_op = op;
        return true;
      };

  // Send a masked text frame; server echoes it back unmasked.
  ASSERT_TRUE(ws_client.send_text("hello"));

  // On loopback, the echo arrives in a single recv.
  const auto echo = client.recv();
  ASSERT_FALSE(echo.empty());
  std::string_view echo_sv{echo};
  EXPECT_EQ(ws_client.feed(echo_sv), 0ULL);
  EXPECT_EQ(got_msg, "hello");
  EXPECT_EQ(got_op, ws_frame_control::text);
}

// Query strings and fragments must not affect route matching; only the target
// path determines the registered `base_path`.
void HttpServer_WebSocket_QueryAndFragmentRoute() {
  if (is_codex()) return;

  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [](http_websocket_transaction& tx) {
                  tx.websocket().on_message =
                      [](http_websocket& ws, std::string&& p,
                          ws_frame_control) { return ws.send_text(p); };
                  return true;
                }));
      });
  ASSERT_TRUE(server);

  auto client = stream_sync::connect(server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  ASSERT_TRUE(client.send(
      "GET /ws?token=abc#frag HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-Websocket-Version: 13\r\n"
      "Sec-Websocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "\r\n"));

  const auto resp_wire = client.recv_until("\r\n\r\n");
  ASSERT_FALSE(resp_wire.empty());
  auto resp_head_wire = std::string_view{resp_wire};
  ASSERT_GE(resp_head_wire.size(), 2U);
  resp_head_wire.remove_suffix(2);
  response_head resp;
  ASSERT_TRUE(resp.parse(resp_head_wire));
  EXPECT_EQ(resp.status_code, http_status_code::SWITCHING_PROTOCOLS);
  const auto accept = resp.headers.get("Sec-Websocket-Accept");
  ASSERT_TRUE(accept);
  EXPECT_EQ(*accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

// Semi-integration test exercising four frame-sequence scenarios against a
// live `http_server` with an `http_websocket_transaction` echo route.
//
// The upgrade request is built with `generate_upgrade_request` so the
// random client key and its derived accept value are both verified.
//
// Scenarios:
//   1. Single text frame echoed back.
//   2. Two-fragment message ("hel" + "lo") reassembled to "hello".
//   3. Three-fragment message interleaved with a ping (triggers auto-pong)
//      and a client-originated pong (silently absorbed); reassembled echo
//      returned after the pong.
//   4. Close frame (code 1001): server mirrors the code; `on_close` fires.
void HttpServer_WebSocket_Frames() {
  if (is_codex()) return;

  // Server echoes text messages and mirrors close frames.
  auto web_server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      [](http_server& s) {
        return s.add_route({"", "/ws"},
            http_websocket_transaction::make_factory(
                [](http_websocket_transaction& tx) {
                  tx.websocket().on_message =
                      [](http_websocket& ws, std::string&& p,
                          ws_frame_control) { return ws.send_text(p); };
                  return true;
                }));
      });
  ASSERT_TRUE(web_server);

  auto client = stream_sync::connect(web_server->local_endpoint(), 1s);
  ASSERT_TRUE(client);

  http_transaction::send_fn client_send{[&](any_strings&& frame) {
    return client.send(std::get<std::string>(frame));
  }};
  http_websocket ws_client{std::move(client_send), connection_role::client};

  std::string got_msg;
  ws_frame_control got_op{};
  uint16_t got_close_code{};
  ws_client.on_message =
      [&](http_websocket&, std::string&& p, ws_frame_control op) {
        got_msg = std::move(p);
        got_op = op;
        return true;
      };
  ws_client.on_close = [&](http_websocket&, uint16_t code, std::string_view) {
    got_close_code = code;
  };

  // Build the upgrade request via `generate_upgrade_request`; the method
  // returns the `request_head` and stores the expected accept key.
  std::string accept_key;
  auto req = http_websocket::generate_upgrade_request("/ws", accept_key);
  (void)req.headers.add_raw("Host", "localhost");
  ASSERT_TRUE(client.send(req.serialize()));

  // Verify the response.
  const auto resp_wire = client.recv_until("\r\n\r\n");
  ASSERT_FALSE(resp_wire.empty());
  auto resp_head_wire = std::string_view{resp_wire};
  ASSERT_GE(resp_head_wire.size(), 2U);
  resp_head_wire.remove_suffix(2);
  response_head resp;
  ASSERT_TRUE(resp.parse(resp_head_wire));
  EXPECT_EQ(resp.status_code, http_status_code::SWITCHING_PROTOCOLS);
  const auto accept = resp.headers.get("Sec-Websocket-Accept");
  ASSERT_TRUE(accept);
  EXPECT_EQ(*accept, accept_key);

  // Receive chunks from the socket and feed them through `ws_client` until
  // `on_message` fires.  Handles both single-recv and split-recv delivery.
  const auto recv_msg = [&]() {
    got_msg.clear();
    while (got_msg.empty()) {
      auto chunk = client.recv();
      if (chunk.empty()) break;
      std::string_view sv{chunk};
      (void)ws_client.feed(sv);
    }
  };

  // Case 1: single text frame.
  ASSERT_TRUE(ws_client.send_text("hello"));
  recv_msg();
  EXPECT_EQ(got_msg, "hello");
  EXPECT_EQ(got_op, ws_frame_control::text);

  // Case 2: two-fragment text message.
  //   Fragment 1: FIN=0, text opcode, "hel"
  //   Fragment 2: FIN=1, continuation, "lo"
  //   -> server reassembles and echoes "hello"
  ASSERT_TRUE(ws_client.send_frame(ws_frame_control::text, "hel"));
  ASSERT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "lo"));
  recv_msg();
  EXPECT_EQ(got_msg, "hello");

  // Case 3: three fragments interleaved with a ping and a pong.
  //   FIN=0 text       "foo"     -> accumulate
  //   FIN=1 ping       "payload" -> server auto-pongs to client
  //   FIN=0 cont       "bar"     -> accumulate
  //   FIN=1 pong       "payload" -> silently absorbed by server
  //   FIN=1 cont       "baz"     -> assemble "foobarbaz", echo
  //
  // `recv_msg` feeds chunks until `on_message` fires, transparently
  // consuming any pong frames that arrive before the echo.
  ASSERT_TRUE(ws_client.send_frame(ws_frame_control::text, "foo"));
  ASSERT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::ping, "payload"));
  ASSERT_TRUE(ws_client.send_frame(ws_frame_control::continuation, "bar"));
  ASSERT_TRUE(ws_client.send_pong("payload"));
  ASSERT_TRUE(ws_client.send_frame(
      ws_frame_control::fin | ws_frame_control::continuation, "baz"));
  recv_msg();
  EXPECT_EQ(got_msg, "foobarbaz");

  // Case 4: close frame (code 1001).
  //   Client sends close; server `on_close` mirrors the code back.
  //   Client feeds received data until its `on_close` fires.
  ASSERT_TRUE(ws_client.send_close(1001, "done"));
  while (got_close_code == 0) {
    auto chunk = client.recv();
    if (chunk.empty()) break;
    std::string_view sv{chunk};
    (void)ws_client.feed(sv);
  }
  EXPECT_EQ(got_close_code, uint16_t{1001});
}

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(HttpHeaderBlock_ParseHttp11, HttpHeaderBlock_ParseHttp10,
    HttpHeaderBlock_UnknownMethod, HttpHeaderBlock_InvalidVersion,
    HttpHeaderBlock_Http09Style, HttpHeaderBlock_NoSp,
    HttpHeaderBlock_HeaderLookupCanonical, Utf8Checker_Complete,
    Utf8Checker_IncompleteThenComplete, Utf8Checker_InvalidSticky,
    Utf8Checker_RejectsInvalidSequences, HttpHeaderBlock_HeaderGet,
    HttpHeaderBlock_HeaderGetEmptyValue, HttpHeaderBlock_HeaderCombine,
    HttpHeaderBlock_KeepAlive, HttpHeaderBlock_KeepAliveTokenList,
    HttpHeaderBlock_ExtractLeadingCrlf, HttpHeaderBlock_ResponseSerialize,
    HttpHeaderBlock_ExtractHeaderErrors, HttpHeaderBlock_RequestSerialize,
    HttpHeaderBlock_ResponseExtract, HttpServer_OwnLoop, HttpServer_SharedLoop,
    HttpServer_Create_BadEndpoint, HttpServer_GetRoot, HttpServer_GetPath,
    HttpServer_RouteBasePath, HttpServer_InvalidRequest,
    HttpServer_TooLongRequest, HttpServer_PartialRequest, HttpServer_ANS,
    HttpServer_SharedWheel, HttpServer_RequestWithinTimeout,
    HttpServer_IdleTimeout, HttpServer_WriteTimeout, HttpServer_MissingHost,
    HttpServer_KeepAlive, HttpServer_Pipeline, HttpServer_ConnectionClose,
    HttpServer_Http10NoKeepAlive, HttpServer_Http09, HttpServer_LeadingCrlf,
    HttpServer_TooManyLeadingCrls, HttpHeaderBlock_NormalizeCasing,
    HttpHeaderBlock_NormalizeSpecialChars,
    HttpHeaderBlock_NormalizeInvalidChars, HttpHeaderBlock_NormalizeEdgeCases,
    HttpHeaderBlock_IsValidFieldValue, HttpHeaderBlock_ContentLength,
    HttpHeaderBlock_IsChunked, HttpHeaderBlock_HttpOptionsExtractApply,
    HttpHeaderBlock_SizeAndEmpty, HttpHeaderBlock_AddRawWithRawName,
    HttpHeaderBlock_GetReturnsFirst, HttpHeaderBlock_KeepAliveHttp09,
    HttpHeaderBlock_Http09WithHeaders, HttpHeaderBlock_TooManyLeadingCrlfs,
    HttpHeaderBlock_TargetNotPath, HttpHeaderBlock_ClearRequest,
    HttpHeaderBlock_ClearResponse, HttpHeaderBlock_ResponseSerializeInvalid,
    HttpHeaderBlock_MakeErrorResponse, HttpHeaderBlock_ResponseParseEdgeCases,
    HttpServer_BodyTooLarge, HttpServer_TooLongHeaders,
    HttpServer_MalformedRequestLine, HttpServer_Http10KeepAlive,
    HttpHeaderBlock_GetValues, HttpHeaderBlock_SetRawAndRemove,
    WebSocket_AcceptKey, WebSocket_FrameCodec_RoundTrip,
    WebSocket_Feed_SingleText, WebSocket_Feed_SingleTextInvalidUtf8,
    WebSocket_Feed_SingleTextInvalidUtf8Disabled, WebSocket_Feed_MaskedBinary,
    WebSocket_Feed_Ping, WebSocket_Feed_Close,
    WebSocket_Feed_CloseInvalidUtf8Reason, WebSocket_Feed_Fragmented,
    WebSocket_Feed_FragmentedDelivery,
    WebSocket_Feed_FragmentedDeliverySplitUtf8,
    WebSocket_Feed_FragmentedDeliveryInvalidUtf8,
    WebSocket_Feed_FragmentedDeliveryInvalidUtf8EmptyFinal,
    WebSocket_Feed_FragmentedDeliveryInvalidUtf8Disabled,
    WebSocket_Feed_PartialFrame,
    WebSocket_Feed_RecvBufferViewRequestsFrameSizedGrowth,
    WebSocket_Feed_MultipleFrames, WebSocket_Feed_MultipleFramesViaView,
    WebSocket_Feed_BadContinuation, WebSocket_Feed_InterleavedData,
    WebSocket_Feed_DataAfterSentClose, WebSocket_Feed_DataAfterReceivedClose,
    WebSocket_Send_Server, WebSocket_Send_Client,
    WebSocket_FrameWrapper_HeaderAndViews, WebSocket_FrameWrapper_CopyTo,
    WebSocket_FrameWrapper_MaskPayloadInPlace,
    WebSocket_FrameWrapper_MaskPayloadCopyByteOrder, WebSocket_Send_Binary,
    WebSocket_Send_Pong_Direct, WebSocket_Send_Frame_Prebuilt,
    WebSocket_Hangup, WebSocket_Fail, WebSocketTransaction_UpgradeSuccess,
    WebSocketTransaction_DrainSendsResponse,
    WebSocketTransaction_DrainBeforeUpgrade, WebSocketTransaction_BadMethod,
    WebSocketTransaction_MissingUpgrade,
    WebSocketTransaction_MissingConnection, WebSocketTransaction_WrongVersion,
    WebSocketTransaction_MissingKey, WebSocketTransaction_UpgradeRequiredDrain,
    WebSocketTransaction_BadRequestDrain,
    WebSocketTransaction_FeedAfterUpgrade,
    WebSocketTransaction_FeedProtocolError,
    WebSocketTransaction_SubprotocolNegotiation,
    WebSocketTransaction_MakeFactory, WebSocketTransaction_OnDrain,
    WebSocket_PingCounter, HttpServer_WebSocket_Keepalive,
    HttpServer_WebSocket_KeepaliveTimeout, HttpServer_WebSocket,
    HttpServer_WebSocket_QueryAndFragmentRoute, HttpServer_WebSocket_Frames);
