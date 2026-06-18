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

#include <nghttp3/nghttp3.h>

#include "catch2_main.h"

// NOLINTBEGIN(readability-function-cognitive-complexity)

// Smoke test for the HTTP/3 build integration. Verifies that nghttp3 is
// linkable and exercises a couple of stable library entry points. Not a
// functional test of HTTP/3 itself; that arrives with the http3_router
// wrapping nghttp3_conn.

TEST_CASE("HTTP/3 dependency links and reports version", "[http3]") {
  SECTION("nghttp3 returns a valid version struct") {
    const nghttp3_info* info = nghttp3_version(0);
    REQUIRE(info != nullptr);
    CHECK(info->age >= 0);
    CHECK(info->version_str != nullptr);
    CHECK(info->version_num != 0);
  }

  SECTION("nghttp3 populates default settings") {
    // `nghttp3_settings_default` fills the caller's struct with library
    // defaults; a non-zero QPACK blocked-streams or max-table-capacity is
    // not guaranteed, so just confirm the symbol links and runs cleanly over
    // a real struct.
    nghttp3_settings settings;
    nghttp3_settings_default(&settings);
    SUCCEED("nghttp3_settings_default linked and ran");
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
