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

#include "../corvid/proto/quic/http3_header.h"
#include "../corvid/strings/enum_conversion.h"

#include "catch2_main.h"

using namespace corvid;
using namespace corvid::proto::quic;
using namespace corvid::proto::quic::http3_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// Every known `qpack_token`, used to drive the round-trip checks below. Omits
// `unknown`, which has no canonical name.
constexpr qpack_token all_tokens[]{
    qpack_token::authority,
    qpack_token::method,
    qpack_token::path,
    qpack_token::scheme,
    qpack_token::status,
    qpack_token::accept,
    qpack_token::accept_encoding,
    qpack_token::accept_language,
    qpack_token::accept_ranges,
    qpack_token::access_control_allow_credentials,
    qpack_token::access_control_allow_headers,
    qpack_token::access_control_allow_methods,
    qpack_token::access_control_allow_origin,
    qpack_token::access_control_expose_headers,
    qpack_token::access_control_request_headers,
    qpack_token::access_control_request_method,
    qpack_token::age,
    qpack_token::alt_svc,
    qpack_token::authorization,
    qpack_token::cache_control,
    qpack_token::content_disposition,
    qpack_token::content_encoding,
    qpack_token::content_length,
    qpack_token::content_security_policy,
    qpack_token::content_type,
    qpack_token::cookie,
    qpack_token::date,
    qpack_token::early_data,
    qpack_token::etag,
    qpack_token::expect_ct,
    qpack_token::forwarded,
    qpack_token::if_modified_since,
    qpack_token::if_none_match,
    qpack_token::if_range,
    qpack_token::last_modified,
    qpack_token::link,
    qpack_token::location,
    qpack_token::origin,
    qpack_token::purpose,
    qpack_token::range,
    qpack_token::referer,
    qpack_token::server,
    qpack_token::set_cookie,
    qpack_token::strict_transport_security,
    qpack_token::timing_allow_origin,
    qpack_token::upgrade_insecure_requests,
    qpack_token::user_agent,
    qpack_token::vary,
    qpack_token::x_content_type_options,
    qpack_token::x_forwarded_for,
    qpack_token::x_frame_options,
    qpack_token::x_xss_protection,
    qpack_token::host,
    qpack_token::connection,
    qpack_token::keep_alive,
    qpack_token::proxy_connection,
    qpack_token::transfer_encoding,
    qpack_token::upgrade,
    qpack_token::te,
    qpack_token::protocol,
    qpack_token::priority,
};

} // namespace

TEST_CASE("Http3HeadersNameFromToken", "[http3]") {
  using H = http3_headers;
  if (true) {
    // Pseudo-headers carry their leading ':'.
    CHECK(H::name_from_token(qpack_token::authority) == ":authority");
    CHECK(H::name_from_token(qpack_token::method) == ":method");
    CHECK(H::name_from_token(qpack_token::protocol) == ":protocol");
    // Regular and hyphenated names match the static table.
    CHECK(H::name_from_token(qpack_token::content_length) == "content-length");
    CHECK(H::name_from_token(qpack_token::te) == "te");
  }
  if (true) {
    // `unknown` and out-of-range tokens have no name.
    CHECK(H::name_from_token(qpack_token::unknown).empty());
    CHECK(H::name_from_token(static_cast<qpack_token>(9999)).empty());
  }
  if (true) {
    // Every known token maps to a non-empty name.
    for (auto token : all_tokens)
      CHECK_FALSE(H::name_from_token(token).empty());
  }
}

TEST_CASE("Http3HeadersTokenFromName", "[http3]") {
  using H = http3_headers;
  if (true) {
    CHECK(H::token_from_name(":authority") == qpack_token::authority);
    CHECK(H::token_from_name(":protocol") == qpack_token::protocol);
    CHECK(H::token_from_name("content-length") == qpack_token::content_length);
    CHECK(H::token_from_name("te") == qpack_token::te);
  }
  if (true) {
    // Unrecognized names yield `unknown`.
    CHECK(H::token_from_name("") == qpack_token::unknown);
    CHECK(H::token_from_name("x-not-a-header") == qpack_token::unknown);
    // Lookup is exact; the table is lowercase, so mixed case does not match.
    CHECK(H::token_from_name("Content-Length") == qpack_token::unknown);
  }
}

TEST_CASE("Http3HeadersTokenNameRoundTrip", "[http3]") {
  using H = http3_headers;
  // The two maps are exact inverses for every known token.
  for (auto token : all_tokens)
    CHECK(H::token_from_name(H::name_from_token(token)) == token);
}

TEST_CASE("Http3HeadersAdd", "[http3]") {
  http3_headers h;
  CHECK(h.empty());
  CHECK(h.size() == 0);

  SECTION("add looks up the token from the name") {
    const auto ndx = h.add({"content-length", "0"});
    CHECK(ndx == 0);
    CHECK(h.size() == 1);
    CHECK_FALSE(h.empty());
    const auto& f = h[0];
    CHECK(f.token == qpack_token::content_length);
    CHECK(f.name == "content-length");
    CHECK(f.value == "0");
    CHECK(f.flags == nv_flags::none);
  }

  SECTION("an unknown name stores `unknown` and the verbatim name") {
    h.add({header_name::force("x-custom"), "v"});
    CHECK(h[0].token == qpack_token::unknown);
    CHECK(h[0].name == "x-custom");
    CHECK(h[0].value == "v");
  }

  SECTION("the explicit-token overload keeps the given token and name") {
    // The token is taken as given, not re-derived from the name.
    h.add({header_name::force("x-custom"), "v", nv_flags::never_index},
        qpack_token::method);
    CHECK(h[0].token == qpack_token::method);
    CHECK(h[0].name == "x-custom");
    CHECK(h[0].flags == nv_flags::never_index);
  }

  SECTION("add returns each new index in order") {
    CHECK(h.add({":method", "GET"}) == 0);
    CHECK(h.add({":path", "/"}) == 1);
    CHECK(h.add({":path", "/other"}) == 2);
    CHECK(h.size() == 3);
  }
}

TEST_CASE("Http3HeadersSet", "[http3]") {
  http3_headers h;

  SECTION("set on an absent name adds a new field with derived token") {
    const auto ndx = h.set_value("content-length", "0");
    CHECK(ndx == 0);
    CHECK(h.size() == 1);
    CHECK(h[0].token == qpack_token::content_length);
    CHECK(h[0].name == "content-length");
    CHECK(h[0].value == "0");
    CHECK(h[0].flags == nv_flags::none);
  }

  SECTION("set on an existing name modifies it in place") {
    h.add({"content-length", "0"});
    const auto ndx =
        h.set_value("content-length", "42", nv_flags::never_index);
    CHECK(ndx == 0);
    CHECK(h.size() == 1); // modified, not appended
    CHECK(h[0].value == "42");
    CHECK(h[0].flags == nv_flags::never_index);
    CHECK(h[0].token == qpack_token::content_length);
  }

  SECTION("an explicit token finds and modifies an existing field by token") {
    h.add({"content-length", "0"});
    // Match by token (not name); the field's value is updated in place.
    const auto ndx = h.set_value("content-length", "9");
    CHECK(ndx == 0);
    CHECK(h.size() == 1);
    CHECK(h[0].value == "9");
  }

  SECTION("set merges flags into an existing field rather than replacing") {
    h.add({"content-length", "0", nv_flags::no_copy_value});
    h.set_value("content-length", "1", nv_flags::never_index);
    // The pre-existing flag is preserved; the new one is OR'd in.
    CHECK(h[0].flags == (nv_flags::no_copy_value | nv_flags::never_index));
  }

  SECTION("an unknown name with no explicit token stores `unknown`") {
    auto a = header_name_and_enum::force("x-custom", qpack_token::unknown);
    h.set_value(a, "v");
    CHECK(h[0].token == qpack_token::unknown);
    CHECK(h[0].name == "x-custom");
  }

  SECTION("set updates only the first of several duplicates") {
    h.add({"accept", "a"});
    h.add({"accept", "b"});
    const auto ndx = h.set_value("accept", "c");
    CHECK(ndx == 0);
    CHECK(h.size() == 2);
    CHECK(h[0].value == "c");
    CHECK(h[1].value == "b"); // later duplicate left untouched
  }
}

TEST_CASE("Http3HeadersFind", "[http3]") {
  http3_headers h;
  h.add({":method", "GET"_method});
  h.add({"accept", "a"});
  h.add({"accept", "b"}); // duplicate name / token

  SECTION("find returns the first match by name or token") {
    auto* by_name = h.find("accept");
    REQUIRE(by_name != nullptr);
    CHECK(by_name->value == "a");
    auto* by_token = h.find(qpack_token::accept);
    REQUIRE(by_token != nullptr);
    CHECK(by_token->value == "a");
    CHECK(by_name == by_token);
  }

  SECTION("find returns nullptr when absent") {
    CHECK(h.find(header_name_and_enum::force("x-missing",
              qpack_token::unknown)) == nullptr);
    CHECK(h.find(qpack_token::status) == nullptr);
  }

  SECTION("count tallies all matches") {
    CHECK(h.count("accept") == 2);
    CHECK(h.count(qpack_token::accept) == 2);
    CHECK(h.count(":method") == 1);
    CHECK(h.count(header_name_and_enum::force("x-missing",
              qpack_token::unknown)) == 0);
  }

  SECTION("find_next walks the matches then yields npos") {
    const auto first = h.find_next("accept", 0);
    CHECK(first == 1);
    const auto second = h.find_next("accept", first + 1);
    CHECK(second == 2);
    CHECK(h.find_next("accept", second + 1) == http3_headers::npos);
  }

  SECTION("find_unique requires exactly one match") {
    auto* unique = h.find_unique(":method");
    REQUIRE(unique != nullptr);
    CHECK(unique->value == "GET");
    CHECK(h.find_unique("accept") == nullptr); // more than one
    CHECK(h.find_unique(header_name_and_enum::force("x-missing",
              qpack_token::unknown)) == nullptr); // none
    CHECK(h.find_unique(qpack_token::status) == nullptr);
  }
}

TEST_CASE("Http3HeadersIteration", "[http3]") {
  http3_headers h;
  h.add({":method", "GET"});
  h.add({":path", "/"});

  SECTION("range-for visits fields in insertion order") {
    std::vector<std::string_view> names;
    for (const auto& f : h) names.push_back(f.name);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == ":method");
    CHECK(names[1] == ":path");
  }

  SECTION("fields are mutable through a non-const instance") {
    for (auto& f : h) f.value = "x";
    h[0].flags = nv_flags::never_index;
    CHECK(h[0].value == "x");
    CHECK(h[1].value == "x");
    CHECK(h[0].flags == nv_flags::never_index);
    // `find` likewise yields a mutable pointer.
    h.find(":path")->value = "y";
    CHECK(h[1].value == "y");
  }

  SECTION("a const instance yields const access") {
    const auto& ch = h;
    static_assert(std::is_same_v<decltype(h[0]), http3_field&>);
    static_assert(std::is_same_v<decltype(ch[0]), const http3_field&>);
    static_assert(std::is_same_v<decltype(*ch.begin()), const http3_field&>);
    static_assert(
        std::is_same_v<decltype(ch.find(":method")), const http3_field*>);
    CHECK(ch.size() == 2);
    CHECK(ch[0].name == ":method");
  }
}

TEST_CASE("Http3HeadersEraseAndClear", "[http3]") {
  http3_headers h;
  h.add({":method", "GET"});
  h.add({":path", "/"});
  h.add({":scheme", "https"});

  SECTION("erase removes the field and shifts the rest down") {
    CHECK(h.erase(1));
    CHECK(h.size() == 2);
    CHECK(h[0].name == ":method");
    CHECK(h[1].name == ":scheme");
  }

  SECTION("erase out of range returns false and leaves the set intact") {
    CHECK_FALSE(h.erase(3));
    CHECK_FALSE(h.erase(http3_headers::npos));
    CHECK(h.size() == 3);
  }

  SECTION("clear empties the set") {
    h.clear();
    CHECK(h.empty());
    CHECK(h.size() == 0);
  }
}

TEST_CASE("Http3HeadersSpanConversion", "[http3]") {
  http3_headers h;
  h.add({":status", "200"});
  h.add({"content-length", "0"});

  // The implicit conversion exposes the fields as a contiguous span for the
  // submit path, in the same order.
  std::span<const http3_field> fields = h;
  REQUIRE(fields.size() == 2);
  CHECK(fields[0].name == ":status");
  CHECK(fields[0].value == "200");
  CHECK(fields[1].name == "content-length");
  CHECK(fields.data() == &h[0]);
}

TEST_CASE("NvFlagsString", "[http3]") {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  using namespace corvid;
  using namespace corvid::strings;
  using F = nv_flags;
  if (true) {
    CHECK(enum_as_string(F::never_index) == "never_index");
    CHECK(enum_as_string(F::no_copy_name) == "no_copy_name");
    CHECK(enum_as_string(F::no_copy_value) == "no_copy_value");
    CHECK(enum_as_string(F::try_index) == "try_index");
  }
  if (true) {
    // Higher bits print first.
    CHECK(enum_as_string(F::try_index | F::never_index) ==
          "try_index + never_index");
  }
  if (true) {
    constexpr F bad{0xff};
    CHECK(parse_enum("never_index", bad) == F::never_index);
    CHECK(parse_enum("try_index", bad) == F::try_index);
    CHECK(parse_enum("try_index + never_index", bad) ==
          (F::try_index | F::never_index));
  }
}

TEST_CASE("StreamChunkString", "[http3]") {
  // Both sequence values round-trip through `enum_as_string` / `parse_enum`.
  using namespace corvid;
  using namespace corvid::strings;
  using C = stream_chunk;
  if (true) {
    CHECK(enum_as_string(C::more) == "more");
    CHECK(enum_as_string(C::fin) == "fin");
  }
  if (true) {
    constexpr C bad{0xff};
    CHECK(parse_enum("more", bad) == C::more);
    CHECK(parse_enum("fin", bad) == C::fin);
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
