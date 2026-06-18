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
#include <string_view>

#include "corvid/proto/quic/http3_server_stream.h"

#include "catch2_main.h"

using namespace corvid;
using namespace corvid::proto::quic;
using namespace std::string_view_literals;

namespace {

// Run the authority gate for a request carrying an optional `:authority` and
// an optional `Host`, against the configured server `expected`. Mirrors what
// `authority_reject_status` reads off a live stream, minus the router.
std::string_view gate(std::optional<std::string_view> authority,
    std::optional<std::string_view> host,
    std::string_view expected = "example.com"sv) {
  http3_headers headers;
  if (authority) headers.add(":authority", *authority);
  if (host) headers.add("host", *host);
  return http3_server_stream::authority_reject_status_for(headers, expected);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Http3ServerStreamAuthorityGate", "[http3]") {
  SECTION("no configured authority is a server misconfiguration") {
    CHECK(gate("example.com", std::nullopt, ""sv) == "500");
  }

  SECTION("a request naming neither authority nor host is misdirected") {
    CHECK(gate(std::nullopt, std::nullopt) == "421");
  }

  SECTION("a single authority is matched host-wise") {
    CHECK(gate("example.com", std::nullopt).empty());
    CHECK(gate("Example.COM", std::nullopt).empty());
    CHECK(gate("example.com:443", std::nullopt).empty());
    CHECK(gate("evil.com", std::nullopt) == "421");
  }

  SECTION("a lone Host stands in for the authority") {
    CHECK(gate(std::nullopt, "example.com").empty());
    CHECK(gate(std::nullopt, "example.com:8443").empty());
    CHECK(gate(std::nullopt, "evil.com") == "421");
  }

  SECTION("both present and consistent is accepted") {
    CHECK(gate("example.com", "example.com").empty());
  }

  SECTION("both present differing only by case or port is accepted") {
    // The regression: these name the same host, so neither a case nor a
    // ":port" difference between the two should provoke a 421.
    CHECK(gate("Example.com", "example.com").empty());
    CHECK(gate("example.com", "EXAMPLE.COM").empty());
    CHECK(gate("example.com", "example.com:443").empty());
    CHECK(gate("example.com:443", "example.com").empty());
    CHECK(gate("example.com:443", "example.com:8443").empty());
  }

  SECTION("either field naming a different host is rejected") {
    CHECK(gate("example.com", "evil.com") == "421");
    CHECK(gate("evil.com", "example.com") == "421");
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
