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
#pragma region ParseHttp11

TEST_CASE("ParseHttp11", "[HttpHeaderBlock]") {
  request_head req;
  // The final crlf was parsed out by `terminated_text_parser`, as part of the
  // crlfcrlf sentinel.
  REQUIRE(req.parse(
      "GET /path HTTP/1.1\r\nHost: example.com\r\nAccept: text/html"));
  CHECK(req.version == http_version::http_1_1);
  CHECK(req.method == http_method::GET);
  CHECK(req.target == "/path");
  const auto host = req.headers.get("Host");
  REQUIRE(host);
  CHECK(*host == "example.com");
  const auto accept = req.headers.get("Accept");
  REQUIRE(accept);
  CHECK(*accept == "text/html");
}
#pragma endregion
#pragma region ParseHttp10

// Verify that a well-formed HTTP/1.0 request is parsed correctly.
TEST_CASE("ParseHttp10", "[HttpHeaderBlock]") {
  request_head req;
  REQUIRE(req.parse("POST /submit HTTP/1.0\r\n"));
  CHECK(req.version == http_version::http_1_0);
  CHECK(req.method == http_method::POST);
  CHECK(req.target == "/submit");
}
#pragma endregion
#pragma region UnknownMethod

// Verify that an unrecognized method token causes `parse` to fail.
TEST_CASE("UnknownMethod", "[HttpHeaderBlock]") {
  request_head req;
  CHECK_FALSE(req.parse("BREW /coffee HTTP/1.1\r\n"));
}
#pragma endregion
#pragma region InvalidVersion

// Verify that an unrecognized version token causes `parse` to fail.
TEST_CASE("InvalidVersion", "[HttpHeaderBlock]") {
  request_head req;
  CHECK_FALSE(req.parse("GET / HTTP/2.0\r\n"));
}
#pragma endregion
#pragma region Http09Style

// Verify that an HTTP/0.9-style request line (no version token) yields
// `http_version::http_0_9`.
TEST_CASE("Http09Style", "[HttpHeaderBlock]") {
  request_head req;
  REQUIRE(req.parse("GET /\r\n"));
  CHECK(req.version == http_version::http_0_9);
  CHECK(req.target == "/");
}
#pragma endregion
#pragma region NoSp

// Verify that a request line with no SP at all returns false.
TEST_CASE("NoSp", "[HttpHeaderBlock]") {
  request_head req;
  CHECK_FALSE(req.parse("GETNOSPC\r\n"));
}
#pragma endregion
#pragma region HeaderLookupCanonical

// Verify that `http_headers::get` requires the canonical key form.
// `add` folds to canonical form before indexing; lookups with
// non-canonical names return `nullopt`.
TEST_CASE("HeaderLookupCanonical", "[HttpHeaderBlock]") {
  http_headers h;
  // Add with mixed-case input; stored under "Content-Type".
  CHECK(h.add("content-TYPE", "text/plain"));
  // Exact canonical form finds the value.
  const auto content_type = h.get("Content-Type");
  REQUIRE(content_type);
  CHECK(*content_type == "text/plain");
}
#pragma endregion
#pragma region HeaderGet

// Verify that `get` returns `nullopt` for absent or non-canonical names.
TEST_CASE("HeaderGet", "[HttpHeaderBlock]") {
  http_headers h;
  CHECK(h.add("Host", "localhost"));
  const auto host = h.get("Host");
  REQUIRE(host);
  CHECK(*host == "localhost");
  CHECK_FALSE(h.get("Heist"));
  CHECK_FALSE(h.get("Content-Type"));
}
#pragma endregion
#pragma region HeaderGetEmptyValue

// Verify that `get` distinguishes an empty stored value from a missing one.
TEST_CASE("HeaderGetEmptyValue", "[HttpHeaderBlock]") {
  http_headers h;
  CHECK(h.add_raw("X-Empty", ""));
  const auto empty_value = h.get("X-Empty");
  REQUIRE(empty_value);
  CHECK(empty_value->empty());
  CHECK_FALSE(h.get("Missing"));
}
#pragma endregion
#pragma region HeaderCombine

// Verify that `http_headers::get_combined` joins multiple values with ", ".
TEST_CASE("HeaderCombine", "[HttpHeaderBlock]") {
  http_headers h;
  CHECK(h.add("Accept", "text/html"));
  CHECK(h.add("Accept", "application/json"));
  CHECK(h.get_combined("Accept") == "text/html, application/json");
  CHECK(h.get_combined("Missing") == "");
}
#pragma endregion
#pragma region KeepAlive

// Verify `keep_alive` for HTTP/1.1 (default on) and HTTP/1.0 (default off).
TEST_CASE("KeepAlive", "[HttpHeaderBlock]") {
  {
    request_head req;
    REQUIRE(req.parse("GET / HTTP/1.1\r\nHost: h\r\n"));
    CHECK(req.options.keep_alive(req.version) == after_response::keep_alive);
  }
  {
    request_head req;
    REQUIRE(req.parse("GET / HTTP/1.0\r\n"));
    CHECK(req.options.keep_alive(req.version) == after_response::close);
  }
  {
    request_head req;
    REQUIRE(req.parse("GET / HTTP/1.1\r\nHost: h\r\nConnection: close\r\n"));
    CHECK(req.options.keep_alive(req.version) == after_response::close);
  }
  {
    request_head req;
    REQUIRE(req.parse("GET / HTTP/1.0\r\nConnection: keep-alive\r\n"));
    CHECK(req.options.keep_alive(req.version) == after_response::keep_alive);
  }
}
#pragma endregion
#pragma region KeepAliveTokenList

// Verify `keep_alive` parses `Connection` as a comma-separated token list,
// with `"close"` taking precedence over `"keep-alive"`.
TEST_CASE("KeepAliveTokenList", "[HttpHeaderBlock]") {
  // Token list containing `"close"` among other tokens -> close.
  {
    request_head req;
    REQUIRE(req.parse(
        "GET / HTTP/1.1\r\nHost: h\r\nConnection: keep-alive, close\r\n"));
    CHECK(req.options.keep_alive(req.version) == after_response::close);
  }
  // Token list with only `"close"` and an unrelated token -> close.
  {
    request_head req;
    REQUIRE(req.parse(
        "GET / HTTP/1.1\r\nHost: h\r\nConnection: close, upgrade\r\n"));
    CHECK(req.options.keep_alive(req.version) == after_response::close);
  }
  // Token list with `"keep-alive"` and an unrelated token -> keep-alive.
  {
    request_head req;
    REQUIRE(req.parse(
        "GET / HTTP/1.1\r\nHost: h\r\nConnection: keep-alive, upgrade\r\n"));
    CHECK(req.options.keep_alive(req.version) == after_response::keep_alive);
  }
  // Case-insensitive: `"Close"` -> close.
  {
    request_head req;
    REQUIRE(req.parse("GET / HTTP/1.1\r\nHost: h\r\nConnection: Close\r\n"));
    CHECK(req.options.keep_alive(req.version) == after_response::close);
  }
}
#pragma endregion
#pragma region ResponseSerialize

// Verify that `response_head::serialize` produces the correct HTTP wire format
// (headers only; body is sent separately).
TEST_CASE("ResponseSerialize", "[HttpHeaderBlock]") {
  response_head resp;
  resp.version = http_version::http_1_1;
  resp.status_code = http_status_code::OK;
  resp.reason = "OK";
  CHECK(resp.headers.add_raw("Connection", "close"));
  CHECK(resp.headers.add_raw("Content-Type", "text/plain"));
  CHECK(resp.headers.add_raw("Content-Length", "5"));
  const auto wire = resp.serialize();
  CHECK(wire.contains("HTTP/1.1 200 OK\r\n"));
  CHECK(wire.contains("Connection: close\r\n"));
  CHECK(wire.contains("Content-Type: text/plain\r\n"));
  CHECK(wire.contains("Content-Length: 5\r\n"));
  // Wire format ends with the blank line; body is not included.
  CHECK(wire.ends_with("\r\n\r\n"));
}
#pragma endregion
#pragma region ExtractLeadingCrlf

// Verify that `request_head::parse` skips leading CRLF lines
// (RFC 9112 section 2.2) and that a request that is only leading CRLFs fails.
TEST_CASE("ExtractLeadingCrlf", "[HttpHeaderBlock]") {
  {
    request_head req;
    REQUIRE(req.parse("\r\n\r\nGET /path HTTP/1.1\r\nHost: example.com\r\n"));
    CHECK(req.method == http_method::GET);
    CHECK(req.target == "/path");
    CHECK(req.version == http_version::http_1_1);
    const auto host = req.headers.get("Host");
    REQUIRE(host);
    CHECK(*host == "example.com");
  }
  {
    // Only CRLFs, no request line: fails.
    request_head req;
    CHECK_FALSE(req.parse("\r\n\r\n"));
  }
}
#pragma endregion
#pragma region ExtractHeaderErrors

// Verify that malformed header-field lines cause `parse` to return false.
TEST_CASE("ExtractHeaderErrors", "[HttpHeaderBlock]") {
  {
    // Obs-fold with SP: rejected.
    request_head req;
    CHECK_FALSE(
        req.parse("GET / HTTP/1.1\r\nHost: example.com\r\n continued\r\n"));
  }
  {
    // Obs-fold with HTAB: rejected.
    request_head req;
    CHECK_FALSE(
        req.parse("GET / HTTP/1.1\r\nHost: example.com\r\n\tcontinued\r\n"));
  }
  {
    // Header line with no colon: rejected.
    request_head req;
    CHECK_FALSE(req.parse("GET / HTTP/1.1\r\nBadHeader\r\n"));
  }
  {
    // Invalid character (space) in field name: rejected.
    request_head req;
    CHECK_FALSE(req.parse("GET / HTTP/1.1\r\nBad Name: value\r\n"));
  }
}
#pragma endregion
#pragma region RequestSerialize

// Verify that `request_head::serialize` produces correct wire format and that
// a round-trip through `parse` is lossless.
TEST_CASE("RequestSerialize", "[HttpHeaderBlock]") {
  {
    // HTTP/1.1 with headers.
    request_head req;
    REQUIRE(req.parse(
        "GET /path HTTP/1.1\r\nHost: example.com\r\nAccept: text/html\r\n"));
    const auto wire = req.serialize();
    CHECK(wire.contains("GET /path HTTP/1.1\r\n"));
    CHECK(wire.contains("Host: example.com\r\n"));
    CHECK(wire.contains("Accept: text/html\r\n"));
    CHECK(wire.ends_with("\r\n\r\n"));

    // Round-trip: strip the terminal "\r\n" blank line before passing to
    // `parse` (which expects the block without the "\r\n\r\n" sentinel).
    request_head req2;
    REQUIRE(req2.parse(std::string_view{wire}.substr(0, wire.size() - 2)));
    CHECK(req2.version == http_version::http_1_1);
    CHECK(req2.method == http_method::GET);
    CHECK(req2.target == "/path");
    const auto host = req2.headers.get("Host");
    REQUIRE(host);
    CHECK(*host == "example.com");
    const auto accept = req2.headers.get("Accept");
    REQUIRE(accept);
    CHECK(*accept == "text/html");
  }
  {
    // HTTP/1.0, no headers.
    request_head req;
    REQUIRE(req.parse("POST /submit HTTP/1.0\r\n"));
    const auto wire = req.serialize();
    CHECK(wire.contains("POST /submit HTTP/1.0\r\n"));
    CHECK(wire.ends_with("\r\n\r\n"));
  }
  {
    // HTTP/0.9: no version token in the output.
    request_head req;
    REQUIRE(req.parse("GET /\r\n"));
    const auto wire = req.serialize();
    CHECK(wire.contains("GET /\r\n"));
    CHECK_FALSE(wire.contains("HTTP/"));
    CHECK(wire.ends_with("\r\n\r\n"));
  }
  {
    // invalid version: returns empty.
    request_head req;
    CHECK(req.serialize().empty());
  }
}
#pragma endregion
#pragma region ResponseExtract

// Verify that a well-formed HTTP response is parsed by
// `response_head::parse`.
TEST_CASE("ResponseExtract", "[HttpHeaderBlock]") {
  {
    // HTTP/1.1 200 with headers.
    response_head resp;
    REQUIRE(resp.parse(
        "HTTP/1.1 200 OK\r\nContent-Type: "
        "text/html\r\nContent-Length: 42\r\n"));
    CHECK(resp.version == http_version::http_1_1);
    CHECK(resp.status_code == http_status_code{200});
    CHECK(resp.reason == "OK");
    const auto content_type = resp.headers.get("Content-Type");
    REQUIRE(content_type);
    CHECK(*content_type == "text/html");
    const auto content_length = resp.headers.get("Content-Length");
    REQUIRE(content_length);
    CHECK(*content_length == "42");
  }
  {
    // HTTP/1.0 with multi-word reason phrase.
    response_head resp;
    REQUIRE(resp.parse("HTTP/1.0 404 Not Found\r\n"));
    CHECK(resp.version == http_version::http_1_0);
    CHECK(resp.status_code == http_status_code{404});
    CHECK(resp.reason == "Not Found");
  }
  {
    // Unknown version: false.
    response_head resp;
    CHECK_FALSE(resp.parse("HTTP/2.0 200 OK\r\n"));
  }
  {
    // No SP after version: false.
    response_head resp;
    CHECK_FALSE(resp.parse("HTTP/1.1\r\n"));
  }
  {
    // Non-numeric status code: false.
    response_head resp;
    CHECK_FALSE(resp.parse("HTTP/1.1 abc OK\r\n"));
  }
  {
    // Round-trip: build a response, serialize it, re-parse, check fields.
    response_head resp;
    resp.version = http_version::http_1_1;
    resp.status_code = http_status_code{201};
    resp.reason = "Created";
    CHECK(resp.headers.add_raw("Location", "/new/resource"));
    const auto wire = resp.serialize();
    response_head resp2;
    REQUIRE(resp2.parse(wire.substr(0, wire.size() - 2)));
    CHECK(resp2.version == http_version::http_1_1);
    CHECK(resp2.status_code == http_status_code{201});
    CHECK(resp2.reason == "Created");
    const auto location = resp2.headers.get("Location");
    REQUIRE(location);
    CHECK(*location == "/new/resource");
  }
}
#pragma endregion
#pragma region NormalizeCasing

// Verify normalization of valid header names: case folding and hyphen
// word-boundary detection.
TEST_CASE("NormalizeCasing", "[HttpHeaderBlock]") {
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
    CHECK(actual == expected);
  }

  {
    // Lowercase input: changed, result is title case.
    std::string name{"content-type"};
    CHECK(http_headers::normalize(name).value());
    CHECK(name == "Content-Type");
  }
  {
    // All-caps input: changed, result is title case.
    std::string name{"ACCEPT-ENCODING"};
    CHECK(http_headers::normalize(name).value());
    CHECK(name == "Accept-Encoding");
  }
  {
    // Mixed case: changed, result is title case.
    std::string name{"X-fOrWaRdEd-fOr"};
    CHECK(http_headers::normalize(name).value());
    CHECK(name == "X-Forwarded-For");
  }
  {
    // Already canonical: not changed, name unchanged.
    std::string name{"Content-Type"};
    CHECK_FALSE(http_headers::normalize(name).value());
    CHECK(name == "Content-Type");
  }
  {
    // Multi-segment all-lowercase.
    std::string name{"x-forwarded-for"};
    CHECK(http_headers::normalize(name).value());
    CHECK(name == "X-Forwarded-For");
  }
}
#pragma endregion
#pragma region NormalizeSpecialChars

// Verify that valid token special characters are accepted and that names
// containing them are normalized correctly.
TEST_CASE("NormalizeSpecialChars", "[HttpHeaderBlock]") {
  {
    // Underscore and dot are valid; alpha segments are title-cased.
    std::string name{"x-custom_header.v2"};
    CHECK(http_headers::normalize(name).value());
    CHECK(name == "X-Custom_header.v2");
  }
  {
    // A name composed entirely of the special set "!#$%&'*+.^_`|~" is
    // valid; none are alpha so to_upper/to_lower are no-ops.
    std::string name{"!#$%&'*+.^_`|~"};
    CHECK_FALSE(http_headers::normalize(name).value());
    CHECK(name == "!#$%&'*+.^_`|~");
  }
  {
    // Hyphen triggers capitalization of the following character.
    std::string name{"a-b-c"};
    CHECK(http_headers::normalize(name).value());
    CHECK(name == "A-B-C");
  }
  {
    // Leading hyphen: valid, first alpha after it is uppercased.
    std::string name{"-foo"};
    CHECK(http_headers::normalize(name).value());
    CHECK(name == "-Foo");
  }
}
#pragma endregion
#pragma region NormalizeInvalidChars

// Verify that names containing characters outside the allowed set are
// rejected: `std::nullopt` is returned and `name` is left unchanged.
TEST_CASE("NormalizeInvalidChars", "[HttpHeaderBlock]") {
  // Returns true iff `normalize` rejected the name (nullopt) and left it
  // unchanged.
  auto bad = [](std::string name) {
    const std::string orig{name};
    return !http_headers::normalize(name) && name == orig;
  };

  CHECK(bad("Bad Name"));  // space
  CHECK(bad("Bad:Name"));  // colon
  CHECK(bad("bad@name"));  // at-sign
  CHECK(bad("bad\\name")); // backslash
  CHECK(bad("bad\tname")); // tab (control character)
  CHECK(bad("bad/name"));  // slash
  CHECK(bad("bad\"name")); // double-quote
  CHECK(bad("bad(name"));  // open paren
  CHECK(bad("bad)name"));  // close paren
  CHECK(bad("bad<name"));  // less-than
  CHECK(bad("bad>name"));  // greater-than
}
#pragma endregion
#pragma region NormalizeEdgeCases

// Verify edge cases: empty name and single-character names.
TEST_CASE("NormalizeEdgeCases", "[HttpHeaderBlock]") {
  {
    // Empty string: no invalid chars, no change, returns nullopt.
    std::string name;
    CHECK_FALSE(http_headers::normalize(name));
  }
  {
    // Single valid alpha: uppercased, returns true.
    std::string name{"a"};
    CHECK(http_headers::normalize(name).value());
    CHECK(name == "A");
  }
  {
    // Single valid alpha already uppercase: no change, returns false.
    std::string name{"A"};
    CHECK_FALSE(http_headers::normalize(name).value());
    CHECK(name == "A");
  }
  {
    // Single digit: valid, no alpha casing, returns false.
    std::string name{"3"};
    CHECK_FALSE(http_headers::normalize(name).value());
    CHECK(name == "3");
  }
  {
    // Single invalid char: returns nullopt, name unchanged.
    std::string name{" "};
    CHECK_FALSE(http_headers::normalize(name));
    CHECK(name == " ");
  }
  {
    // Invalid char mid-name: returns nullopt, name unchanged.
    std::string name{"Content Type"};
    CHECK_FALSE(http_headers::normalize(name));
    CHECK(name == "Content Type");
  }
}
#pragma endregion
#pragma region IsValidFieldValue

// Verify `is_valid_field_value`: empty and printable are accepted; control
// characters and DEL are rejected; obs-text bytes (>= 0x80) are accepted.
TEST_CASE("IsValidFieldValue", "[HttpHeaderBlock]") {
  CHECK(http_headers::is_valid_field_value(""));
  CHECK(http_headers::is_valid_field_value("text/html; charset=utf-8"));
  CHECK(http_headers::is_valid_field_value(" padded value "));
  CHECK(http_headers::is_valid_field_value("value\twith\ttab"));
  // obs-text (>= 0x80): valid per RFC 9110 field-value grammar.
  CHECK(http_headers::is_valid_field_value("\x80\xff"));
  // Null byte: invalid.
  CHECK_FALSE(http_headers::is_valid_field_value(std::string_view("a\0b", 3)));
  // CR and LF: invalid.
  CHECK_FALSE(http_headers::is_valid_field_value("bad\rvalue"));
  CHECK_FALSE(http_headers::is_valid_field_value("bad\nvalue"));
  // DEL (0x7F): invalid.
  CHECK_FALSE(http_headers::is_valid_field_value("bad\x7fvalue"));
  // Other control chars (< 0x20, not HTAB): invalid.
  CHECK_FALSE(http_headers::is_valid_field_value("bad\x01value"));
}
#pragma endregion
#pragma region ContentLength

// Verify `content_length` in `http_options`: set when present and parseable,
// `std::nullopt` when absent or non-numeric.
TEST_CASE("ContentLength", "[HttpHeaderBlock]") {
  {
    http_headers h;
    CHECK(h.add_raw("Content-Length", "42"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.content_length);
    CHECK(*opts.content_length == 42ULL);
  }
  {
    // Absent: nullopt.
    http_headers h;
    http_options opts;
    opts.extract(h);
    CHECK_FALSE(opts.content_length);
  }
  {
    // Non-numeric: nullopt.
    http_headers h;
    CHECK(h.add_raw("Content-Length", "abc"));
    http_options opts;
    opts.extract(h);
    CHECK_FALSE(opts.content_length);
  }
  {
    // Zero is a valid value.
    http_headers h;
    CHECK(h.add_raw("Content-Length", "0"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.content_length);
    CHECK(*opts.content_length == 0ULL);
  }
}
#pragma endregion
#pragma region IsChunked

// Verify `transfer_encoding` in `http_options`: `chunked` when recognized
// (case-insensitive) as the last token of the last field, `std::nullopt`
// otherwise.
TEST_CASE("IsChunked", "[HttpHeaderBlock]") {
  {
    http_headers h;
    CHECK(h.add_raw("Transfer-Encoding", "chunked"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.transfer_encoding);
    CHECK(*opts.transfer_encoding == transfer_encoding_value::chunked);
  }
  {
    // Mixed case value still matches.
    http_headers h;
    CHECK(h.add_raw("Transfer-Encoding", "Chunked"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.transfer_encoding);
    CHECK(*opts.transfer_encoding == transfer_encoding_value::chunked);
  }
  {
    // `chunked` works as the last.
    http_headers h;
    CHECK(h.add_raw("Transfer-Encoding", "gzip,   chUnKed  "));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.transfer_encoding);
    CHECK(*opts.transfer_encoding == transfer_encoding_value::chunked);
  }
  {
    // `chunked` does not work before other encodings.
    http_headers h;
    CHECK(h.add_raw("Transfer-Encoding", "chunked, gzip"));
    http_options opts;
    opts.extract(h);
    CHECK_FALSE(opts.transfer_encoding);
  }
  {
    // Multiple fields: last token of last field is `chunked`.
    http_headers h;
    CHECK(h.add_raw("Transfer-Encoding", "gzip"));
    CHECK(h.add_raw("Transfer-Encoding", "chunked"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.transfer_encoding);
    CHECK(*opts.transfer_encoding == transfer_encoding_value::chunked);
  }
  {
    // Multiple fields: a later field appends after `chunked` -- not chunked.
    http_headers h;
    CHECK(h.add_raw("Transfer-Encoding", "gzip, chunked"));
    CHECK(h.add_raw("Transfer-Encoding", "deflate"));
    http_options opts;
    opts.extract(h);
    CHECK_FALSE(opts.transfer_encoding);
  }
  {
    // Absent: nullopt.
    http_headers h;
    http_options opts;
    opts.extract(h);
    CHECK_FALSE(opts.transfer_encoding);
  }
}
#pragma endregion
#pragma region SizeAndEmpty

// Verify `empty`, `size`, and ordered iteration.
TEST_CASE("SizeAndEmpty", "[HttpHeaderBlock]") {
  http_headers h;
  CHECK(h.empty());
  CHECK(h.size() == 0ULL);

  CHECK(h.add_raw("Host", "example.com"));
  CHECK_FALSE(h.empty());
  CHECK(h.size() == 1ULL);

  CHECK(h.add_raw("Accept", "text/html"));
  CHECK(h.size() == 2ULL);

  // Iteration visits fields in insertion order.
  auto it = h.begin();
  CHECK(it->name == "Host");
  ++it;
  CHECK(it->name == "Accept");
  ++it;
  CHECK(it == h.end());
}
#pragma endregion
#pragma region AddRawWithRawName

// Verify 3-arg `add_raw`: `field_name` is the index key (canonical form);
// `raw_field_name` is the wire name stored in the entry.
TEST_CASE("AddRawWithRawName", "[HttpHeaderBlock]") {
  http_headers h;
  // Index key is canonical "Content-Type"; wire name is lowercase.
  CHECK(h.add_raw("Content-Type", "text/html", "content-type"));

  // `get` looks up via canonical index key.
  const auto ct = h.get("Content-Type");
  REQUIRE(ct);
  CHECK(*ct == "text/html");

  // `serialize` writes the raw (wire) name, not the canonical key.
  std::string out;
  h.serialize(out);
  CHECK(out.contains("content-type: text/html\r\n"));
  CHECK_FALSE(out.contains("Content-Type"));
}
#pragma endregion
#pragma region GetReturnsFirst

// Verify that `get` returns only the first value when a field name appears
// multiple times (use `get_combined` for all values).
TEST_CASE("GetReturnsFirst", "[HttpHeaderBlock]") {
  http_headers h;
  CHECK(h.add("Accept", "text/html"));
  CHECK(h.add("Accept", "application/json"));
  CHECK(h.add("Accept", "image/webp"));

  const auto first = h.get("Accept");
  REQUIRE(first);
  CHECK(*first == "text/html");

  // `get_combined` joins all three.
  CHECK((h.get_combined("Accept")) ==
        ("text/html, application/json, image/webp"));
}
#pragma endregion
#pragma region KeepAliveHttp09

// Verify `keep_alive` for HTTP/0.9: always `close`, regardless of any
// `Connection` header (HTTP/0.9 headers don't exist, but guard against it).
TEST_CASE("KeepAliveHttp09", "[HttpHeaderBlock]") {
  // HTTP/0.9 always yields `close` regardless of any `Connection` header.
  http_headers h;
  http_options opts;
  opts.extract(h);
  CHECK(opts.keep_alive(http_version::http_0_9) == after_response::close);

  // Even if a `Connection: keep-alive` header were somehow present,
  // HTTP/0.9 must still return `close`.
  CHECK(h.add_raw("Connection", "keep-alive"));
  opts = {};
  opts.extract(h);
  CHECK(opts.keep_alive(http_version::http_0_9) == after_response::close);
}
#pragma endregion
#pragma region Http09WithHeaders

// Verify that an HTTP/0.9 request line with trailing header text causes
// `request_head::parse` to fail (HTTP/0.9 does not allow headers).
TEST_CASE("Http09WithHeaders", "[HttpHeaderBlock]") {
  request_head req;
  CHECK_FALSE(req.parse("GET /\r\nHost: example.com\r\n"));
}
#pragma endregion
#pragma region TooManyLeadingCrlfs

// Verify that more than five consecutive leading CRLFs cause `parse` to fail
// (RFC 9112 section 2.2 imposes a limit).
TEST_CASE("TooManyLeadingCrlfs", "[HttpHeaderBlock]") {
  {
    // Exactly 5 leading CRLFs: should still parse successfully.
    request_head req;
    CHECK(req.parse("\r\n\r\n\r\n\r\n\r\nGET / HTTP/1.1\r\n"));
    CHECK(req.method == http_method::GET);
  }
  {
    // Six leading CRLFs: parse fails.
    request_head req;
    CHECK_FALSE(req.parse("\r\n\r\n\r\n\r\n\r\n\r\nGET / HTTP/1.1\r\n"));
  }
}
#pragma endregion
#pragma region TargetNotPath

// Verify that a target not starting with `'/'` causes `parse` to fail.
TEST_CASE("TargetNotPath", "[HttpHeaderBlock]") {
  {
    // Absolute URI form (HTTP/1.1 proxies): not accepted by this parser.
    request_head req;
    CHECK_FALSE(req.parse("GET http://example.com/ HTTP/1.1\r\n"));
  }
  {
    // Authority form.
    request_head req;
    CHECK_FALSE(req.parse("CONNECT example.com:443 HTTP/1.1\r\n"));
  }
}
#pragma endregion
#pragma region ClearRequest

// Verify that `request_head::clear` restores default-constructed state so the
// object can be reused for a second request.
TEST_CASE("ClearRequest", "[HttpHeaderBlock]") {
  request_head req;
  REQUIRE(req.parse(
      "GET /path HTTP/1.1\r\nHost: example.com\r\nAccept: text/html\r\n"));
  CHECK(req.method == http_method::GET);
  CHECK_FALSE(req.headers.empty());

  req.clear();
  CHECK(req.version == http_version{});
  CHECK(req.method == http_method{});
  CHECK(req.target.empty());
  CHECK(req.headers.empty());
}
#pragma endregion
#pragma region ClearResponse

// Verify that `response_head::clear` restores default-constructed state.
TEST_CASE("ClearResponse", "[HttpHeaderBlock]") {
  response_head resp;
  resp.version = http_version::http_1_1;
  resp.status_code = http_status_code::OK;
  resp.reason = "OK";
  CHECK(resp.headers.add_raw("Content-Length", "0"));

  resp.clear();
  CHECK(resp.version == http_version{});
  CHECK(resp.status_code == http_status_code{});
  CHECK(resp.reason.empty());
  CHECK(resp.headers.empty());
}
#pragma endregion
#pragma region ResponseSerializeInvalid

// Verify that `response_head::serialize` returns an empty string when the
// version is `http_version::invalid`.
TEST_CASE("ResponseSerializeInvalid", "[HttpHeaderBlock]") {
  response_head resp;
  // Default-constructed version is `invalid`.
  CHECK(resp.serialize().empty());

  // Explicitly set to invalid.
  resp.version = http_version::invalid;
  resp.status_code = http_status_code::OK;
  resp.reason = "OK";
  CHECK(resp.serialize().empty());
}
#pragma endregion
#pragma region MakeErrorResponse

// Verify `make_error_response` with default and custom arguments.
TEST_CASE("MakeErrorResponse", "[HttpHeaderBlock]") {
  {
    // Defaults: HTTP/1.1 400 Bad Request, Connection: close.
    const auto wire = response_head::make_error_response();
    CHECK(wire.contains("HTTP/1.1"));
    CHECK(wire.contains("400"));
    CHECK(wire.contains("Bad Request"));
    CHECK(wire.contains("Connection: close"));
    CHECK(wire.ends_with("\r\n\r\n"));
  }
  {
    // Custom: HTTP/1.0 405 Method Not Allowed, Connection: keep-alive.
    const auto wire = response_head::make_error_response(
        after_response::keep_alive, http_version::http_1_0,
        http_status_code::METHOD_NOT_ALLOWED, "Method Not Allowed");
    CHECK(wire.contains("HTTP/1.0"));
    CHECK(wire.contains("405"));
    CHECK(wire.contains("Method Not Allowed"));
    CHECK(wire.contains("Connection: keep-alive"));
  }
}
#pragma endregion
#pragma region ResponseParseEdgeCases

// Verify `response_head::parse` edge cases: empty reason phrase (trailing
// space after status code with no text) succeeds; missing space after status
// code fails.
TEST_CASE("ResponseParseEdgeCases", "[HttpHeaderBlock]") {
  {
    // Empty reason: status line is "HTTP/1.1 204 ", reason is "".
    response_head resp;
    REQUIRE(resp.parse("HTTP/1.1 204 "));
    CHECK(resp.version == http_version::http_1_1);
    CHECK(resp.status_code == http_status_code::NO_CONTENT);
    CHECK(resp.reason.empty());
  }
  {
    // No SP after status code: fails.
    response_head resp;
    CHECK_FALSE(resp.parse("HTTP/1.1 200\r\n"));
  }
  {
    // Status code below 100: fails.
    response_head resp;
    CHECK_FALSE(resp.parse("HTTP/1.1 99 Too Low\r\n"));
  }
  {
    // Status code above 999: fails.
    response_head resp;
    CHECK_FALSE(resp.parse("HTTP/1.1 1000 Too High\r\n"));
  }
}
#pragma endregion
#pragma region HttpOptionsExtractApply

// Verify `http_options::extract` for `content_type` and `upgrade`, and
// `http_options::apply` writing values back into headers.
TEST_CASE("HttpOptionsExtractApply", "[HttpHeaderBlock]") {
  // content_type: recognized media type, parameters stripped.
  {
    http_headers h;
    CHECK(h.add_raw("Content-Type", "text/html; charset=utf-8"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.content_type);
    CHECK(*opts.content_type == content_type_value::text_html);
  }
  // content_type: exact match without parameters.
  {
    http_headers h;
    CHECK(h.add_raw("Content-Type", "application/json"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.content_type);
    CHECK(*opts.content_type == content_type_value::application_json);
  }
  // content_type: unrecognized -> `unknown`.
  {
    http_headers h;
    CHECK(h.add_raw("Content-Type", "application/octet-stream"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.content_type);
    CHECK(*opts.content_type == content_type_value::unknown);
  }
  // content_type: absent -> nullopt.
  {
    http_headers h;
    http_options opts;
    opts.extract(h);
    CHECK_FALSE(opts.content_type);
  }
  // upgrade: websocket recognized, only when Connection is Upgrade
  {
    http_headers h;
    CHECK(h.add_raw("Connection", "Upgrade"));
    CHECK(h.add_raw("Upgrade", "websocket"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.upgrade);
    CHECK(*opts.upgrade == upgrade_value::websocket);
  }
  // upgrade: unrecognized token -> `unknown`.
  {
    http_headers h;
    CHECK(h.add_raw("Connection", "Upgrade"));
    CHECK(h.add_raw("Upgrade", "h2c"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.upgrade);
    CHECK(*opts.upgrade == upgrade_value::unknown);
  }
  // upgrade: websocket wins in a token list.
  {
    http_headers h;
    CHECK(h.add_raw("Connection", "Upgrade"));
    CHECK(h.add_raw("Upgrade", "h2c, websocket"));
    http_options opts;
    opts.extract(h);
    REQUIRE(opts.upgrade);
    CHECK(*opts.upgrade == upgrade_value::websocket);
  }
  // apply: writes known values into headers, updating existing entries.
  {
    http_headers h;
    CHECK(h.add_raw("Content-Length", "0"));
    http_options opts;
    opts.content_length = 42;
    opts.content_type = content_type_value::text_plain;
    opts.connection = after_response::keep_alive;
    opts.apply(h);
    const auto cl = h.get("Content-Length");
    REQUIRE(cl);
    CHECK(*cl == "42");
    const auto ct = h.get("Content-Type");
    REQUIRE(ct);
    CHECK(*ct == "text/plain");
    const auto conn = h.get("Connection");
    REQUIRE(conn);
    CHECK(*conn == "keep-alive");
  }
  // apply: `unknown` enum values and nullopt are not written.
  {
    http_headers h;
    http_options opts;
    opts.content_type = content_type_value::unknown;
    opts.apply(h);
    CHECK_FALSE(h.get("Content-Type"));
  }
}
#pragma endregion
#pragma region GetValues

// Verify `get_values`: iteration over all values for a field, `size`, `empty`,
// and `iterator::index`.
TEST_CASE("GetValues", "[HttpHeaderBlock]") {
  // Empty range when field not found.
  {
    http_headers h;
    const auto r = h.get_values("Accept");
    CHECK(r.empty());
    CHECK(r.size() == 0ULL);
    CHECK(r.begin() == r.end());
  }
  // Single entry: correct value and non-empty range.
  {
    http_headers h;
    CHECK(h.add_raw("Accept", "text/html"));
    const auto r = h.get_values("Accept");
    CHECK_FALSE(r.empty());
    CHECK(r.size() == 1ULL);
    auto it = r.begin();
    CHECK(*it == "text/html");
    ++it;
    CHECK(it == r.end());
  }
  // Multiple entries for the same field: returned in insertion order.
  {
    http_headers h;
    CHECK(h.add_raw("Accept", "text/html"));
    CHECK(h.add_raw("Host", "example.com")); // interleaved
    CHECK(h.add_raw("Accept", "application/json"));
    const auto r = h.get_values("Accept");
    CHECK(r.size() == 2ULL);
    auto it = r.begin();
    CHECK(*it == "text/html");
    ++it;
    CHECK(*it == "application/json");
    ++it;
    CHECK(it == r.end());
  }
  // `iterator::set` replaces the value in place.
  {
    http_headers h;
    CHECK(h.add_raw("Accept", "text/html"));
    CHECK(h.add_raw("Accept", "application/json"));
    auto r = h.get_values("Accept");
    for (auto it = r.begin(); it != r.end(); ++it)
      if (*it == "text/html") it.set("text/plain");
    auto it = r.begin();
    CHECK(*it == "text/plain");
    ++it;
    CHECK(*it == "application/json");
  }
}
#pragma endregion
#pragma region SetRawAndRemove

// Verify `reset_raw` upsert, `remove_entry`, and `remove_key`.
TEST_CASE("SetRawAndRemove", "[HttpHeaderBlock]") {
  // reset_raw adds when field is absent.
  {
    http_headers h;
    (void)h.reset_raw("Connection", "close");
    const auto v = h.get("Connection");
    REQUIRE(v);
    CHECK(*v == "close");
  }
  // reset_raw updates when one entry exists.
  {
    http_headers h;
    CHECK(h.add_raw("Connection", "keep-alive"));
    (void)h.reset_raw("Connection", "close");
    const auto v = h.get("Connection");
    REQUIRE(v);
    CHECK(*v == "close");
  }
  // reset_raw reduces multiple entries to one.
  {
    http_headers h;
    CHECK(h.add_raw("Accept", "text/html"));
    CHECK(h.add_raw("Accept", "application/json"));
    CHECK(h.get_values("Accept").size() == 2ULL);
    (void)h.reset_raw("Accept", "text/plain");
    const auto v = h.get("Accept");
    REQUIRE(v);
    CHECK(*v == "text/plain");
    CHECK(h.get_values("Accept").size() == 1ULL);
  }
  // reset_raw does not affect other fields.
  {
    http_headers h;
    CHECK(h.add_raw("Host", "example.com"));
    CHECK(h.add_raw("Accept", "text/html"));
    (void)h.reset_raw("Accept", "application/json");
    const auto host = h.get("Host");
    REQUIRE(host);
    CHECK(*host == "example.com");
    const auto accept = h.get("Accept");
    REQUIRE(accept);
    CHECK(*accept == "application/json");
  }
  // remove_entry: entry gone; other fields unaffected.
  {
    http_headers h;
    CHECK(h.add_raw("Host", "example.com"));
    CHECK(h.add_raw("Accept", "text/html"));
    h.remove_key("Accept");
    CHECK_FALSE(h.get("Accept"));
    CHECK(h.get_values("Accept").empty());
    const auto host = h.get("Host");
    REQUIRE(host);
    CHECK(*host == "example.com");
  }
  // remove_entry
  {
    http_headers h;
    CHECK(h.add_raw("Accept", "text/html"));
    CHECK(h.add_raw("Accept", "application/json"));
    CHECK(h.add_raw("Accept", "image/webp"));
    const auto r = h.get_values("Accept");
    for (auto it = r.begin(); it != r.end(); ++it)
      if (*it == "application/json") it.tombstone();
    auto it = r.begin();
    REQUIRE(it != r.end());
    CHECK(*it == "text/html");
    ++it;
    REQUIRE(it != r.end());
    CHECK(*it == "image/webp");
    ++it;
    CHECK(it == r.end());
    const auto accept = h.get_combined("Accept");
    CHECK(accept == "text/html, image/webp");
  }
  // remove_key: all entries for field gone; other fields unaffected.
  {
    http_headers h;
    CHECK(h.add_raw("Host", "example.com"));
    CHECK(h.add_raw("Accept", "text/html"));
    CHECK(h.add_raw("Accept", "application/json"));
    h.remove_key("Accept");
    CHECK_FALSE(h.get("Accept"));
    CHECK(h.get_values("Accept").empty());
    const auto host = h.get("Host");
    REQUIRE(host);
    CHECK(*host == "example.com");
  }
  // remove_key on absent field is a no-op.
  {
    http_headers h;
    CHECK(h.add_raw("Host", "example.com"));
    h.remove_key("Accept");
    const auto host = h.get("Host");
    REQUIRE(host);
    CHECK(*host == "example.com");
  }
}
#pragma endregion

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
