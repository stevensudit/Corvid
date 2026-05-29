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

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "../../enums/bool_enums.h"
#include "../../strings/conversion.h"

namespace corvid { inline namespace proto { namespace quic {

using connection_role = enums::bool_enums::connection_role;

#pragma region ssl_identity

// Server identity: an X509 cert, plus its matching private key, both
// RAII-owned. Used as the parameter bundle for any `SSL_CTX` that needs to
// present a server cert, and as the base for cert generators (e.g.,
// `self_signed_cert`).
struct ssl_identity {
  using evp_pkey_ptr = std::unique_ptr<EVP_PKEY,
      decltype([](EVP_PKEY* p) noexcept { EVP_PKEY_free(p); })>;
  using x509_ptr =
      std::unique_ptr<X509, decltype([](X509* p) noexcept { X509_free(p); })>;

  x509_ptr cert;
  evp_pkey_ptr key;

  [[nodiscard]] explicit operator bool() const noexcept { return cert && key; }
};

#pragma endregion
#pragma region quic_ssl_ctx

// RAII wrapper around `SSL_CTX` parameterized by QUIC role and a single ALPN
// protocol. Servers also install the supplied cert and private key. QUIC
// requires TLS 1.3 (RFC 9001 sec. 4); both factories pin min/max proto version
// to TLS 1.3.
//
// Non-movable: the server-side ALPN selector callback registered on the
// SSL_CTX holds a pointer back into this object's `alpn_wire_` storage, so
// moving the wrapper would invalidate that callback's `user_data`. Hold one by
// value at a stable address or via `std::unique_ptr`.
class quic_ssl_ctx {
public:
#pragma region Construction

  // Server SSL_CTX. `identity` carries the cert and private key the server
  // presents; both are referenced (refcounted) by the context, so the caller's
  // `ssl_identity` keeps its owning unique_ptrs. `alpn` is the single QUIC
  // application protocol the server accepts.
  //
  // Note: It can technically throw on a string alloc, but if this happens, the
  // process is already dead.
  quic_ssl_ctx(const ssl_identity& identity, std::string_view alpn) noexcept
      : alpn_wire_{to_alpn_wire(alpn)}, role_{connection_role::server} {
    if (!identity) return;
    ssl_ctx_ptr ctx{SSL_CTX_new(TLS_server_method())};
    if (!ctx) return;
    if (!SSL_CTX_set_min_proto_version(ctx.get(), TLS1_3_VERSION)) return;
    if (!SSL_CTX_set_max_proto_version(ctx.get(), TLS1_3_VERSION)) return;
    if (SSL_CTX_use_certificate(ctx.get(), identity.cert.get()) != 1) return;
    if (SSL_CTX_use_PrivateKey(ctx.get(), identity.key.get()) != 1) return;
    if (SSL_CTX_check_private_key(ctx.get()) != 1) return;
    SSL_CTX_set_alpn_select_cb(ctx.get(), &alpn_select_cb, this);
    ctx_ = std::move(ctx);
  }

  // Client SSL_CTX. `alpn` is the application protocol the client offers.
  // Peer-certificate verification is disabled (tests use self-signed certs);
  // callers needing verification should configure the returned `native`
  // directly.
  explicit quic_ssl_ctx(std::string_view alpn)
      : alpn_wire_{to_alpn_wire(alpn)}, role_{connection_role::client} {
    ssl_ctx_ptr ctx{SSL_CTX_new(TLS_client_method())};
    if (!ctx) return;
    if (!SSL_CTX_set_min_proto_version(ctx.get(), TLS1_3_VERSION)) return;
    if (!SSL_CTX_set_max_proto_version(ctx.get(), TLS1_3_VERSION)) return;
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    // `SSL_CTX_set_alpn_protos` returns 0 on success, non-zero on failure
    // (one of OpenSSL's inverted-return APIs).
    const auto wire = strings::as_byte_span(alpn_wire_);
    if (SSL_CTX_set_alpn_protos(ctx.get(), wire.data(),
            static_cast<unsigned int>(wire.size())) != 0)
      return;
    ctx_ = std::move(ctx);
  }

  quic_ssl_ctx(const quic_ssl_ctx&) = delete;
  quic_ssl_ctx& operator=(const quic_ssl_ctx&) = delete;
  quic_ssl_ctx(quic_ssl_ctx&&) = delete;
  quic_ssl_ctx& operator=(quic_ssl_ctx&&) = delete;

#pragma endregion
#pragma region Accessors

  [[nodiscard]] explicit operator bool() const noexcept { return !!ctx_; }
  [[nodiscard]] SSL_CTX* native() noexcept { return ctx_.get(); }
  [[nodiscard]] connection_role role() const noexcept { return role_; }

#pragma endregion
#pragma region Internals
private:
  using ssl_ctx_ptr = std::unique_ptr<SSL_CTX,
      decltype([](SSL_CTX* p) noexcept { SSL_CTX_free(p); })>;

  // Server-side ALPN selector. Matches the client's offered list against
  // our single accepted protocol and either picks it or fails the
  // handshake with `no_application_protocol`.
  static int alpn_select_cb(SSL* /*ssl*/, const unsigned char** out,
      unsigned char* outlen, const unsigned char* in, unsigned int inlen,
      void* arg) noexcept {
    auto* self = static_cast<quic_ssl_ctx*>(arg);
    unsigned char* sel = nullptr;
    const auto wire = strings::as_byte_span<unsigned char>(self->alpn_wire_);
    const int rv = SSL_select_next_proto(&sel, outlen, wire.data(),
        static_cast<unsigned int>(wire.size()), in, inlen);
    if (rv == OPENSSL_NPN_NEGOTIATED) {
      *out = sel;
      return SSL_TLSEXT_ERR_OK;
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  // Build the wire-format ALPN list (`<len><bytes>...`) for a single
  // protocol.
  [[nodiscard]] static std::string to_alpn_wire(
      std::string_view proto) noexcept {
    auto s = try_or_log(
        [&] {
          std::string s;
          if (proto.empty() || proto.size() > 255)
            throw std::length_error("ALPN protocol name must be 1-255 bytes");
          s.reserve(proto.size() + 1);
          s.push_back(static_cast<char>(proto.size()));
          s.append(proto);
          return s;
        },
        std::string{});
    if (s.empty()) log::terminate();
    return s;
  }

  ssl_ctx_ptr ctx_;
  std::string alpn_wire_;
  connection_role role_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::quic
