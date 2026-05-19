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
#pragma once

#include <chrono>

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "quic_ssl_ctx.h"

namespace corvid { inline namespace proto { namespace quic {

using namespace std::chrono_literals;

#pragma region self_signed_cert

// Self-signed certificate generator. Builds an RSA-2048 key plus an
// X509v3 cert with CN="localhost", valid for `valid_for` starting now.
// The primary consumer is the TLS-handshake tests, which need a server
// cert without going through a real CA; the same helper is also useful
// for development servers and any other path that needs a throwaway
// identity.
//
// Derived from `ssl_identity`, so the resulting `cert` and `key` members
// are publicly accessible and the type plugs directly into any API that
// takes an `ssl_identity`.
class self_signed_cert: public ssl_identity {
public:
  explicit self_signed_cert(std::chrono::seconds valid_for = 24h) noexcept {
    evp_pkey_ptr signed_key{
        EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", static_cast<size_t>(2048))};
    if (!signed_key) return;

    x509_ptr signed_cert{X509_new()};
    if (!signed_cert) return;
    auto* raw = signed_cert.get();

    if (!X509_set_version(raw, X509_VERSION_3)) return;
    if (!ASN1_INTEGER_set(X509_get_serialNumber(raw), 1)) return;
    if (!X509_gmtime_adj(X509_getm_notBefore(raw), 0)) return;
    if (!X509_gmtime_adj(X509_getm_notAfter(raw), valid_for.count())) return;
    if (!X509_set_pubkey(raw, signed_key.get())) return;

    X509_NAME* name = X509_get_subject_name(raw);
    if (!X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0))
      return;
    if (!X509_set_issuer_name(raw, name)) return;

    if (!X509_sign(raw, signed_key.get(), EVP_sha256())) return;

    cert = std::move(signed_cert);
    key = std::move(signed_key);
  }
};

#pragma endregion

}}} // namespace corvid::proto::quic
