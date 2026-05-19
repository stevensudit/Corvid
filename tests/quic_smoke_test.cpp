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

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/opensslv.h>
#include <openssl/quic.h>

#include "catch2_main.h"

// NOLINTBEGIN(readability-function-cognitive-complexity)

// Smoke test for the QUIC build integration. Verifies that ngtcp2 and
// OpenSSL 3.5+ are linkable and report sensible versions. Not a functional
// test of the libraries themselves; that comes with the wrappers.
// nghttp3 will land alongside the HTTP/3 milestone.

TEST_CASE("QUIC dependencies link and report versions", "[quic]") {
  SECTION("OpenSSL is 3.5 or newer (native QUIC API)") {
    // 0x30500000L == 3.5.0. We need the QUIC API that landed in 3.5.
    CHECK(OPENSSL_VERSION_NUMBER >= 0x30500000L);
  }

  SECTION("ngtcp2 returns a valid version struct") {
    const ngtcp2_info* info = ngtcp2_version(0);
    REQUIRE(info != nullptr);
    CHECK(info->age >= 0);
    CHECK(info->version_str != nullptr);
    CHECK(info->version_num != 0);
  }

  SECTION("ngtcp2 OpenSSL crypto context constructs and destroys") {
    // `ngtcp2_crypto_ossl_ctx_new` allocates the per-connection crypto
    // context that ngtcp2 hangs off an `SSL*`. Passing nullptr exercises the
    // happy path of the allocator without requiring a full TLS handshake;
    // `ctx_del` tolerates the null `SSL*` cleanly.
    ngtcp2_crypto_ossl_ctx* ctx{nullptr};
    const int rv = ngtcp2_crypto_ossl_ctx_new(&ctx, nullptr);
    CHECK(rv == 0);
    REQUIRE(ctx != nullptr);
    ngtcp2_crypto_ossl_ctx_del(ctx);
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
