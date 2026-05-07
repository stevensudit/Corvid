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
#pragma region ParseHttp11

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
#pragma endregion
#pragma region ParseHttp10

// Verify that a well-formed HTTP/1.0 request is parsed correctly.
void HttpHeaderBlock_ParseHttp10() {
  request_head req;
  ASSERT_TRUE(req.parse("POST /submit HTTP/1.0\r\n"));
  EXPECT_EQ(req.version, http_version::http_1_0);
  EXPECT_EQ(req.method, http_method::POST);
  EXPECT_EQ(req.target, "/submit");
}
#pragma endregion
#pragma region UnknownMethod

// Verify that an unrecognized method token causes `parse` to fail.
void HttpHeaderBlock_UnknownMethod() {
  request_head req;
  EXPECT_FALSE(req.parse("BREW /coffee HTTP/1.1\r\n"));
}
#pragma endregion
#pragma region InvalidVersion

// Verify that an unrecognized version token causes `parse` to fail.
void HttpHeaderBlock_InvalidVersion() {
  request_head req;
  EXPECT_FALSE(req.parse("GET / HTTP/2.0\r\n"));
}
#pragma endregion
#pragma region Http09Style

// Verify that an HTTP/0.9-style request line (no version token) yields
// `http_version::http_0_9`.
void HttpHeaderBlock_Http09Style() {
  request_head req;
  ASSERT_TRUE(req.parse("GET /\r\n"));
  EXPECT_EQ(req.version, http_version::http_0_9);
  EXPECT_EQ(req.target, "/");
}
#pragma endregion
#pragma region NoSp

// Verify that a request line with no SP at all returns false.
void HttpHeaderBlock_NoSp() {
  request_head req;
  EXPECT_FALSE(req.parse("GETNOSPC\r\n"));
}
#pragma endregion
#pragma region HeaderLookupCanonical

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
#pragma endregion
#pragma region HeaderGet

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
#pragma endregion
#pragma region HeaderGetEmptyValue

// Verify that `get()` distinguishes an empty stored value from a missing one.
void HttpHeaderBlock_HeaderGetEmptyValue() {
  http_headers h;
  EXPECT_TRUE(h.add_raw("X-Empty", ""));
  const auto empty_value = h.get("X-Empty");
  ASSERT_TRUE(empty_value);
  EXPECT_TRUE(empty_value->empty());
  EXPECT_FALSE(h.get("Missing"));
}
#pragma endregion
#pragma region HeaderCombine

// Verify that `http_headers::get_combined()` joins multiple values with ", ".
void HttpHeaderBlock_HeaderCombine() {
  http_headers h;
  EXPECT_TRUE(h.add("Accept", "text/html"));
  EXPECT_TRUE(h.add("Accept", "application/json"));
  EXPECT_EQ(h.get_combined("Accept"), "text/html, application/json");
  EXPECT_EQ(h.get_combined("Missing"), "");
}
#pragma endregion
#pragma region KeepAlive

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
#pragma endregion
#pragma region KeepAliveTokenList

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
#pragma endregion
#pragma region ResponseSerialize

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
#pragma endregion
#pragma region ExtractLeadingCrlf

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
#pragma endregion
#pragma region ExtractHeaderErrors

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
#pragma endregion
#pragma region RequestSerialize

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
#pragma endregion
#pragma region ResponseExtract

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
#pragma endregion
#pragma region NormalizeCasing

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
#pragma endregion
#pragma region NormalizeSpecialChars

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
#pragma endregion
#pragma region NormalizeInvalidChars

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
#pragma endregion
#pragma region NormalizeEdgeCases

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
#pragma endregion
#pragma region IsValidFieldValue

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
#pragma endregion
#pragma region ContentLength

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
#pragma endregion
#pragma region IsChunked

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
#pragma endregion
#pragma region SizeAndEmpty

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
#pragma endregion
#pragma region AddRawWithRawName

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
#pragma endregion
#pragma region GetReturnsFirst

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
#pragma endregion
#pragma region KeepAliveHttp09

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
#pragma endregion
#pragma region Http09WithHeaders

// Verify that an HTTP/0.9 request line with trailing header text causes
// `request_head::parse` to fail (HTTP/0.9 does not allow headers).
void HttpHeaderBlock_Http09WithHeaders() {
  request_head req;
  EXPECT_FALSE(req.parse("GET /\r\nHost: example.com\r\n"));
}
#pragma endregion
#pragma region TooManyLeadingCrlfs

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
#pragma endregion
#pragma region TargetNotPath

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
#pragma endregion
#pragma region ClearRequest

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
#pragma endregion
#pragma region ClearResponse

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
#pragma endregion
#pragma region ResponseSerializeInvalid

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
#pragma endregion
#pragma region MakeErrorResponse

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
#pragma endregion
#pragma region ResponseParseEdgeCases

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
#pragma endregion
#pragma region HttpOptionsExtractApply

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
#pragma endregion
#pragma region GetValues

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
#pragma endregion
#pragma region SetRawAndRemove

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
#pragma endregion

MAKE_TEST_LIST(HttpHeaderBlock_ParseHttp11, HttpHeaderBlock_ParseHttp10,
    HttpHeaderBlock_UnknownMethod, HttpHeaderBlock_InvalidVersion,
    HttpHeaderBlock_Http09Style, HttpHeaderBlock_NoSp,
    HttpHeaderBlock_HeaderLookupCanonical, HttpHeaderBlock_HeaderGet,
    HttpHeaderBlock_HeaderGetEmptyValue, HttpHeaderBlock_HeaderCombine,
    HttpHeaderBlock_KeepAlive, HttpHeaderBlock_KeepAliveTokenList,
    HttpHeaderBlock_ResponseSerialize, HttpHeaderBlock_ExtractLeadingCrlf,
    HttpHeaderBlock_ExtractHeaderErrors, HttpHeaderBlock_RequestSerialize,
    HttpHeaderBlock_ResponseExtract, HttpHeaderBlock_NormalizeCasing,
    HttpHeaderBlock_NormalizeSpecialChars,
    HttpHeaderBlock_NormalizeInvalidChars, HttpHeaderBlock_NormalizeEdgeCases,
    HttpHeaderBlock_IsValidFieldValue, HttpHeaderBlock_ContentLength,
    HttpHeaderBlock_IsChunked, HttpHeaderBlock_SizeAndEmpty,
    HttpHeaderBlock_AddRawWithRawName, HttpHeaderBlock_GetReturnsFirst,
    HttpHeaderBlock_KeepAliveHttp09, HttpHeaderBlock_Http09WithHeaders,
    HttpHeaderBlock_TooManyLeadingCrlfs, HttpHeaderBlock_TargetNotPath,
    HttpHeaderBlock_ClearRequest, HttpHeaderBlock_ClearResponse,
    HttpHeaderBlock_ResponseSerializeInvalid,
    HttpHeaderBlock_MakeErrorResponse, HttpHeaderBlock_ResponseParseEdgeCases,
    HttpHeaderBlock_HttpOptionsExtractApply, HttpHeaderBlock_GetValues,
    HttpHeaderBlock_SetRawAndRemove);

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
