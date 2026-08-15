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
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>

#include "../infra/scope_exit.h"
#include "../strings/cstring_view.h"
#include "net_endpoint.h"
#include "socket_enums.h"

namespace corvid { inline namespace proto {

#pragma region dns_resolver

// Resolves hostnames to `net_endpoint` values via `getaddrinfo`.
//
// Resolution is synchronous and can block on network I/O, so it is fit for
// startup and configuration paths, not event loops.
struct dns_resolver {
  // Resolve a hostname to a list of `net_endpoint` values.
  //
  // `host` is a hostname or numeric address string (e.g. `"example.com"` or
  // `"127.0.0.1"`). `port` is the port number. `family` may be
  // `address_family::unspecified` (the default, returning both IPv4 and IPv6
  // results), `inet`, or `inet6`. `max_results` caps the number of endpoints
  // returned (default: unlimited).
  //
  // Returns an empty vector on failure (e.g. unknown host). Results are
  // limited to IPv4/IPv6; any other family is filtered out. Only
  // `SOCK_STREAM` results are requested, to avoid duplicate entries per
  // address.
  //
  // `AI_ADDRCONFIG` is deliberately not set: glibc does not count loopback as
  // a configured address, so it would break resolving `"::1"` on hosts
  // without a global IPv6 address.
  [[nodiscard]] static std::vector<net_endpoint> find_all(cstring_view host,
      uint16_t port, address_family family = address_family::unspecified,
      size_t max_results = std::numeric_limits<size_t>::max()) {
    addrinfo hints{};
    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_family = *family;
    hints.ai_socktype = SOCK_STREAM;

    // Render the port as a numeric service string.
    std::array<char, 6> service{};
    (void)std::to_chars(service.data(), service.data() + service.size() - 1,
        port);

    std::vector<net_endpoint> endpoints;
    endpoints.reserve(8);
    addrinfo* res{};
    if (::getaddrinfo(host.c_str(), service.data(), &hints, &res) != 0)
      return endpoints;
    scope_exit cleanup([&] { ::freeaddrinfo(res); });

    for (const auto* ai = res; ai && (endpoints.size() < max_results);
        ai = ai->ai_next)
      if (const auto fam = address_family{ai->ai_family};
          fam == address_family::inet || fam == address_family::inet6)
        endpoints.emplace_back(*ai->ai_addr, ai->ai_addrlen);

    return endpoints;
  }

  // Resolve a hostname to a single `net_endpoint`.
  //
  // Returns a default-constructed (invalid) `net_endpoint` on failure or if no
  // matching address was found.
  [[nodiscard]] static net_endpoint find_one(cstring_view host, uint16_t port,
      address_family family = address_family::unspecified) {
    const auto results = find_all(host, port, family, 1);
    return results.empty() ? net_endpoint{} : results.front();
  }
};

#pragma endregion
}} // namespace corvid::proto
