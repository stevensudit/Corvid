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

#include <optional>

#include "../corvid/proto/quic/http3_plugins.h"

#include "catch2_main.h"

using namespace corvid;
using namespace corvid::proto::quic;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

constexpr auto a_stream_id = static_cast<quic_stream_id>(0);

// Snapshots the header / trailer sections `http3_stream` delivers, so the
// default accumulation behavior can be asserted after the fact.
struct capture_stream: http3_stream {
  using http3_stream::http3_stream;

  std::optional<http3_headers> headers;
  std::optional<http3_headers> trailers;
  int header_sections{0};
  int trailer_sections{0};

  // The container is move-only and delivered by mutable reference, so the
  // consumer moves it out to retain it past the call.
  [[nodiscard]] bool on_recv_headers(http3_headers& h) override {
    ++header_sections;
    headers = std::move(h);
    return true;
  }

  [[nodiscard]] bool on_recv_trailers(http3_headers& t) override {
    ++trailer_sections;
    trailers = std::move(t);
    return true;
  }
};

} // namespace

TEST_CASE("Http3StreamHeaders", "[http3]") {
  capture_stream s{a_stream_id};
  CHECK(s.stream_id() == a_stream_id);

  SECTION("a section accumulates and is delivered on end") {
    CHECK(s.on_begin_headers());
    CHECK(s.on_recv_header(qpack_token::method, ":method"_header, "GET"_method,
        nv_flags::none));
    CHECK(s.on_recv_header(qpack_token::unknown, "x-foo", "bar",
        nv_flags::never_index));
    CHECK(s.on_end_headers(stream_chunk::fin));

    CHECK(s.header_sections == 1);
    REQUIRE(s.headers);
    REQUIRE(s.headers->size() == 2);
    // The token nghttp3 supplied is preserved (not re-derived).
    CHECK((*s.headers)[0].token == qpack_token::method);
    CHECK((*s.headers)[0].name == ":method");
    CHECK((*s.headers)[0].value == "GET");
    CHECK((*s.headers)[1].token == qpack_token::unknown);
    CHECK((*s.headers)[1].name == "x-foo");
    CHECK((*s.headers)[1].flags == nv_flags::never_index);
    // The end-of-section FIN is carried on the delivered container.
    CHECK(s.headers->chunk_fin() == stream_chunk::fin);
  }

  SECTION("on_begin_headers clears any prior accumulation") {
    CHECK(s.on_begin_headers());
    CHECK(s.on_recv_header(qpack_token::method, ":method"_header, "GET"_method,
        nv_flags::none));
    CHECK(s.on_begin_headers()); // resets before the real section
    CHECK(s.on_recv_header(qpack_token::status, ":status"_header, "200",
        nv_flags::none));
    CHECK(s.on_end_headers(stream_chunk::more));

    REQUIRE(s.headers);
    REQUIRE(s.headers->size() == 1);
    CHECK((*s.headers)[0].name == ":status");
    CHECK(s.headers->chunk_fin() == stream_chunk::more);
  }

  SECTION("over the field cap fails the callback") {
    for (size_t i = 0; i < http3_conn::max_submit_fields; ++i)
      CHECK(s.on_recv_header(qpack_token::unknown, "x", "y", nv_flags::none));
    // The (cap + 1)-th field is rejected rather than accumulated.
    CHECK_FALSE(
        s.on_recv_header(qpack_token::unknown, "x", "y", nv_flags::none));
  }
}

TEST_CASE("Http3StreamTrailers", "[http3]") {
  capture_stream s{a_stream_id};

  SECTION("a trailer section accumulates and is delivered on end") {
    CHECK(s.on_begin_trailers());
    CHECK(s.on_recv_trailer(qpack_token::unknown, "x-checksum", "abc",
        nv_flags::none));
    CHECK(s.on_end_trailers(stream_chunk::fin));

    CHECK(s.trailer_sections == 1);
    REQUIRE(s.trailers);
    REQUIRE(s.trailers->size() == 1);
    CHECK((*s.trailers)[0].name == "x-checksum");
    CHECK((*s.trailers)[0].value == "abc");
    CHECK(s.trailers->chunk_fin() == stream_chunk::fin);
  }

  SECTION("headers and trailers are independent sections") {
    // Leading header section (no FIN: a body follows).
    CHECK(s.on_begin_headers());
    CHECK(s.on_recv_header(qpack_token::status, ":status"_header, "200",
        nv_flags::none));
    CHECK(s.on_end_headers(stream_chunk::more));

    // Body (base no-op default).
    const std::array<uint8_t, 3> body{'a', 'b', 'c'};
    CHECK(s.on_recv_data(body));

    // Trailing section, which ends the stream.
    CHECK(s.on_begin_trailers());
    CHECK(s.on_recv_trailer(qpack_token::unknown, "x-checksum", "abc",
        nv_flags::none));
    CHECK(s.on_end_trailers(stream_chunk::fin));

    // Both sections were delivered separately, each with its own FIN.
    REQUIRE(s.headers);
    REQUIRE(s.trailers);
    CHECK(s.header_sections == 1);
    CHECK(s.trailer_sections == 1);
    CHECK(s.headers->size() == 1);
    CHECK((*s.headers)[0].name == ":status");
    CHECK(s.headers->chunk_fin() == stream_chunk::more);
    CHECK(s.trailers->size() == 1);
    CHECK((*s.trailers)[0].name == "x-checksum");
    CHECK(s.trailers->chunk_fin() == stream_chunk::fin);
  }
}

TEST_CASE("Http3StreamDefaults", "[http3]") {
  // The base class is concrete; its unoverridden hooks are no-op `true`.
  http3_stream s{a_stream_id};
  const std::array<uint8_t, 1> byte{'x'};
  CHECK(s.on_begin_headers());
  CHECK(s.on_recv_header(qpack_token::method, ":method"_header, "GET"_method,
      nv_flags::none));
  CHECK(s.on_end_headers(stream_chunk::more));
  CHECK(s.on_recv_data(byte));
  CHECK(s.on_begin_trailers());
  CHECK(s.on_end_trailers(stream_chunk::fin));
  CHECK(s.on_end_stream());
  CHECK(s.on_close(h3_error_code::no_error));
}
// NOLINTEND(readability-function-cognitive-complexity)
