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
#include "../corvid/enums/enum_conversion.h"

#include "catch2_main.h"

using namespace corvid;
using namespace corvid::proto::quic;
using namespace corvid::proto::quic::http3_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Http3HeaderLiterals", "[http3]") {
  // The `_header` literal validates the field name at compile time. It stores
  // only the name; `as_enum` resolves the token by lookup, not from a stored
  // value (carrying the token is what the `header_name_and_enum` child adds).
  // An unregistered name is a compile error, so only the success path is
  // testable here.
  CHECK(std::string_view{"content-length"_header} == "content-length");
  CHECK(("content-length"_header).as_enum() == qpack_token::content_length);
  // Pseudo-headers keep their leading ':'.
  CHECK(std::string_view{":authority"_header} == ":authority");
  CHECK((":authority"_header).as_enum() == qpack_token::authority);

  // The `_method` literal likewise validates the method name at compile time.
  CHECK(std::string_view{"GET"_method} == "GET");
  CHECK(("GET"_method).as_enum() == http3_method::GET);
  // Underscored enumerators spell as hyphenated names on the wire.
  CHECK(("VERSION-CONTROL"_method).as_enum() == http3_method::VERSION_CONTROL);
}

TEST_CASE("Http3FieldMake", "[http3]") {
  SECTION("make derives the token from a known name") {
    const auto f = http3_field::make(":status", "200");
    CHECK(f.token == qpack_token::status);
    CHECK(f.name == ":status");
    CHECK(f.value == "200");
    CHECK(f.flags == nv_flags::none);
  }

  SECTION("make carries flags and an unknown custom name") {
    const auto key =
        header_name_and_enum::force("x-custom", qpack_token::unknown);
    const auto f = http3_field::make(key, "v", nv_flags::never_index);
    CHECK(f.token == qpack_token::unknown);
    CHECK(f.name == "x-custom");
    CHECK(f.value == "v");
    CHECK(f.flags == nv_flags::never_index);
  }
}

TEST_CASE("Http3HeadersAdd", "[http3]") {
  http3_headers h;
  CHECK(h.empty());
  CHECK(h.size() == 0);

  SECTION("add looks up the token from the name") {
    const auto ndx = h.add("content-length", "0");
    CHECK(ndx == 0);
    CHECK(h.size() == 1);
    CHECK_FALSE(h.empty());
    const auto& f = h[0];
    CHECK(f.token == qpack_token::content_length);
    CHECK(f.name == "content-length");
    CHECK(f.value == "0");
    CHECK(f.flags == nv_flags::none);
  }

  SECTION("add returns each new index in order") {
    CHECK(h.add(":method", "GET"_method) == 0);
    CHECK(h.add(":path", "/") == 1);
    CHECK(h.add(":path", "/other") == 2);
    CHECK(h.size() == 3);
  }

  SECTION("add(http3_field) copies the whole field, token and all") {
    const auto field =
        http3_field::make("content-type", "text/plain", nv_flags::never_index);
    CHECK(h.add(field) == 0);
    const auto& f = h[0];
    CHECK(f.token == qpack_token::content_type);
    CHECK(f.name == "content-type");
    CHECK(f.value == "text/plain");
    CHECK(f.flags == nv_flags::never_index);
  }

  SECTION("add(http3_field) preserves an unknown token and custom name") {
    const http3_field field{qpack_token::unknown, "x-trace-id", "abc",
        nv_flags::none};
    CHECK(h.add(field) == 0);
    CHECK(h[0].token == qpack_token::unknown);
    CHECK(h[0].name == "x-trace-id");
    CHECK(h[0].value == "abc");
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
    h.add("content-length", "0");
    const auto ndx =
        h.set_value("content-length", "42", nv_flags::never_index);
    CHECK(ndx == 0);
    CHECK(h.size() == 1); // modified, not appended
    CHECK(h[0].value == "42");
    CHECK(h[0].flags == nv_flags::never_index);
    CHECK(h[0].token == qpack_token::content_length);
  }

  SECTION("an explicit token finds and modifies an existing field by token") {
    h.add("content-length", "0");
    // Match by token (not name); the field's value is updated in place.
    const auto ndx = h.set_value("content-length", "9");
    CHECK(ndx == 0);
    CHECK(h.size() == 1);
    CHECK(h[0].value == "9");
  }

  SECTION("set merges flags into an existing field rather than replacing") {
    h.add("content-length", "0", nv_flags::no_copy_value);
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
    h.add("accept", "a");
    h.add("accept", "b");
    const auto ndx = h.set_value("accept", "c");
    CHECK(ndx == 0);
    CHECK(h.size() == 2);
    CHECK(h[0].value == "c");
    CHECK(h[1].value == "b"); // later duplicate left untouched
  }
}

TEST_CASE("Http3HeadersFind", "[http3]") {
  http3_headers h;
  h.add(":method", "GET"_method);
  h.add("accept", "a");
  h.add("accept", "b"); // duplicate name / token

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

  SECTION("lookups match an unknown-token field by name") {
    // With no token to match on, the lookups fall back to comparing the
    // field name. Append two duplicates after the fixture's three fields.
    const auto key =
        header_name_and_enum::force("x-custom", qpack_token::unknown);
    h.add(key, "1");
    h.add(key, "2");
    CHECK(h.count(key) == 2);
    auto* first = h.find(key);
    REQUIRE(first != nullptr);
    CHECK(first->value == "1");
    CHECK(h.find_unique(key) == nullptr); // two matches
    const auto i0 = h.find_next(key, 0);
    CHECK(i0 == 3);
    const auto i1 = h.find_next(key, i0 + 1);
    CHECK(i1 == 4);
    CHECK(h.find_next(key, i1 + 1) == http3_headers::npos);
  }
}

TEST_CASE("Http3HeadersIteration", "[http3]") {
  http3_headers h;
  h.add(":method", "GET"_method);
  h.add(":path", "/");

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
  h.add(":method", "GET"_method);
  h.add(":path", "/");
  h.add(":scheme", "https");

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

TEST_CASE("Http3HeadersChunkFin", "[http3]") {
  http3_headers h;

  SECTION("set_chunk_fin round-trips through chunk_fin") {
    h.set_chunk_fin(stream_chunk::fin);
    CHECK(h.chunk_fin() == stream_chunk::fin);
    h.set_chunk_fin(stream_chunk::more);
    CHECK(h.chunk_fin() == stream_chunk::more);
  }

  SECTION("clear resets the FIN marker to `more`") {
    h.add(":status", "200");
    h.set_chunk_fin(stream_chunk::fin);
    h.clear();
    CHECK(h.empty());
    CHECK(h.chunk_fin() == stream_chunk::more);
  }
}

TEST_CASE("Http3HeadersSpanConversion", "[http3]") {
  http3_headers h;
  h.add(":status", "200");
  h.add("content-length", "0");

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

TEST_CASE("HttpMethodString", "[http3]") {
  // Method names round-trip through `enum_as_string` / `parse_enum`.
  using namespace corvid;
  using namespace corvid::strings;
  using M = http3_method;
  if (true) {
    // `invalid` is excluded from the name list, so it prints numerically.
    CHECK(enum_as_string(M::invalid) == "0");
    CHECK(enum_as_string(M::GET) == "GET");
    CHECK(enum_as_string(M::CONNECT) == "CONNECT");
    // Underscored enumerators render as hyphenated wire names.
    CHECK(enum_as_string(M::BASELINE_CONTROL) == "BASELINE-CONTROL");
    CHECK(enum_as_string(M::VERSION_CONTROL) == "VERSION-CONTROL");
  }
  if (true) {
    constexpr M bad{0xff};
    CHECK(parse_enum("GET", bad) == M::GET);
    CHECK(parse_enum("BASELINE-CONTROL", bad) == M::BASELINE_CONTROL);
    CHECK(parse_enum("VERSION-CONTROL", bad) == M::VERSION_CONTROL);
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
