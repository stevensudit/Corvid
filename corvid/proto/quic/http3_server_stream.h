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
#include <cstddef>
#include <string_view>

#include "../../strings/cases.h"
#include "http3_plugins.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region http3_server_stream

// A server-side HTTP/3 stream.
//
// `http3_server_router<http3_server_stream>` (or a subclass) mints one per
// peer-initiated request and orients it as a server, so the inbound request
// accumulates into `request_headers` / `request_trailers` / `receive_queue`
// and `:status` is seeded into `response_headers`.
//
// Common override points:
//   * `build_response` generates the reply once the full request has arrived
//     (`on_end_stream`): set `:status` and any other fields on
//     `response_headers`, optionally append body chunks to `send_queue`,
//     optionally add `response_trailers`. The base submits it, with a body
//     whenever `send_queue` holds any.
//   * `on_recv_headers` is the right place to decide what to do with the
//     request before we see the body or trailers. Sending an early response
//     from there, such as a failure status, prevents `build_response` from
//     being called.
//   * `authority_reject_status` gates the request on the `:authority` it
//     targets, before any handler runs (see below). Override it to serve more
//     than one authority.
class http3_server_stream: public http3_stream {
public:
  // Build the response into `response_headers` / `send_queue` /
  // `response_trailers`. Called after the full request is available in
  // `request_headers` / `receive_queue`, and only if nothing has responded
  // yet. Returning false fails the stream.
  [[nodiscard]] virtual bool build_response() {
    assert(router()->is_loop_thread());
    return true;
  }

  // The HTTP status with which to reject this request before it reaches a
  // handler, or empty to accept it.
  //
  // The default enforces the single authority configured at the server
  // (`router()->server_name`): "500" when the server has no configured
  // authority (a misconfiguration the client is not at fault for), "421"
  // (Misdirected Request, RFC 9110 sec. 15.5.20) when the request's
  // `:authority` (or `Host`) names a different host.
  //
  // A server that answers for several authorities overrides this.
  [[nodiscard]] virtual std::string_view authority_reject_status() const {
    assert(router()->is_loop_thread());
    return authority_reject_status_for(request_headers(),
        router()->server_name());
  }

  // Gate the completed request HEADERS on the authority before handing them to
  // the per-stream `on_recv_headers`. On rejection, submit the error response
  // (header-only) and stop; `submit_response` sets the responded flag, so the
  // later `on_end_stream` does not respond again.
  [[nodiscard]] bool on_end_headers(stream_chunk chunk_fin) override {
    assert(router()->is_loop_thread());
    if (const auto status = authority_reject_status(); !status.empty()) {
      response_headers().set_value(":status", status);
      return submit_response();
    }
    return http3_stream::on_end_headers(chunk_fin);
  }

  // Build and submit the response once the full request has arrived, unless an
  // earlier hook (the authority gate, or application logic) already responded.
  [[nodiscard]] bool on_end_stream() override {
    assert(router()->is_loop_thread());
    if (!http3_stream::on_end_stream()) return false;
    if (responded()) return true;
    if (!build_response()) return false;
    return submit_response();
  }

  // Helpers.

  // The reject status for a request targeting `expected` (the configured
  // server authority host), or empty to accept. "500" when `expected` is
  // empty (a server misconfiguration), else "421" unless every authority the
  // request carries names `expected`: the `:authority`, the `Host`, or both,
  // compared host-wise so a differing case or ":port" does not reject. A
  // request that carries neither is rejected. The pure core of
  // `authority_reject_status`, factored out so it can be tested without a
  // live router.
  [[nodiscard]] static std::string_view authority_reject_status_for(
      const http3_headers& headers, std::string_view expected) noexcept {
    if (expected.empty()) return "500";
    const auto* authority = headers.find(":authority");
    const auto* host = headers.find("host");
    if (!authority && !host) return "421";
    if (authority && !host_matches(authority->value, expected)) return "421";
    if (host && !host_matches(host->value, expected)) return "421";
    return {};
  }

  // Case-insensitive match of an authority's host (any trailing ":port"
  // dropped) against the configured name.
  [[nodiscard]] static bool
  host_matches(std::string_view authority, std::string_view name) noexcept {
    return strings::ci_equal(host_of(authority), name);
  }

  // The host portion of an authority, dropping a trailing ":port" (a bare host
  // or an IPv6 literal without a port is returned unchanged).
  // NOLINTBEGIN(bugprone-exception-escape)
  [[nodiscard]] static std::string_view host_of(
      std::string_view authority) noexcept {
    const auto colon = authority.rfind(':');
    if (colon == std::string_view::npos) return authority;
    const auto port = authority.substr(colon + 1);
    if (port.empty()) return authority;
    // Only strip a suffix that is an actual numeric port. If any character
    // after the last ':' is not a digit, that colon belongs to the host itself
    // (e.g. an IPv6 literal), so leave the authority unchanged.
    for (const char c : port)
      if (!strings::is_digit(c)) return authority;
    return authority.substr(0, colon);
  }
  // NOLINTEND(bugprone-exception-escape)
};

#pragma endregion

}}} // namespace corvid::proto::quic
