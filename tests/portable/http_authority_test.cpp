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
#include "corvid/proto/misc/http_authority.h"

#include <string_view>

#include "catch2_main.h"

using namespace corvid;
using namespace std::string_view_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region HttpAuthorityStripHostPort

TEST_CASE("HttpAuthorityStripHostPort", "[HttpAuthority]") {
  SECTION("a numeric port is stripped") {
    CHECK(strip_host_port("example.com:8080"sv) == "example.com"sv);
    CHECK(strip_host_port("[::1]:443"sv) == "[::1]"sv);
    CHECK(strip_host_port("[2001:db8::7]:8443"sv) == "[2001:db8::7]"sv);
  }

  SECTION("a bare host is unchanged") {
    CHECK(strip_host_port("example.com"sv) == "example.com"sv);
    CHECK(strip_host_port("[::1]"sv) == "[::1]"sv);
    CHECK(strip_host_port(""sv).empty());
  }

  SECTION("anything but a genuine port is left unrepaired") {
    CHECK(strip_host_port("example.com:"sv) == "example.com:"sv);
    CHECK(strip_host_port("example.com:80a0"sv) == "example.com:80a0"sv);
    CHECK(strip_host_port("::1"sv) == "::1"sv);
    CHECK(strip_host_port("[::1]:"sv) == "[::1]:"sv);
    CHECK(strip_host_port("[::1]:80a0"sv) == "[::1]:80a0"sv);
    CHECK(strip_host_port("[::1]junk"sv) == "[::1]junk"sv);
    CHECK(strip_host_port("[::1"sv) == "[::1"sv);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
