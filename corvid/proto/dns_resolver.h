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
#include <memory>
#include <string>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>

#include "../strings/cstring_view.h"
#include "net_endpoint.h"

namespace corvid { inline namespace proto {

// Resolves hostnames to `net_endpoint` values via `getaddrinfo`.
struct dns_resolver {
  // Resolve a hostname to a list of `net_endpoint` values.
  //
  // `host` is a hostname or numeric address string (e.g. `"example.com"` or
  // `"127.0.0.1"`). `port` is the port number. `family` may be `AF_UNSPEC`
  // (default, returns both IPv4 and IPv6 results), `AF_INET`, or `AF_INET6`.
  // `max_results` caps the number of endpoints returned (default: unlimited).
  //
  // Returns an empty vector on failure (e.g. unknown host) or if the resolver
  // returned only address families other than `AF_INET` / `AF_INET6`. Only
  // `SOCK_STREAM` results are requested, to avoid duplicate entries per
  // address.
  [[nodiscard]] static std::vector<net_endpoint> find_all(cstring_view host,
      uint16_t port, int family = AF_UNSPEC, size_t max_results = SIZE_MAX) {
    addrinfo hints{};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    std::vector<net_endpoint> endpoints;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
            &res) != 0)
      return endpoints;

    const auto info = std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)>{res,
        ::freeaddrinfo};
    res = nullptr;

    for (const addrinfo* ai = info.get(); ai && endpoints.size() < max_results;
        ai = ai->ai_next)
    {
      endpoints.emplace_back(*ai->ai_addr, ai->ai_addrlen);
      if (endpoints.back().empty()) endpoints.pop_back();
    }

    return endpoints;
  }

  // Resolve a hostname to a single `net_endpoint`.
  //
  // Returns a default-constructed (invalid) `net_endpoint` on failure or if no
  // matching address was found.
  [[nodiscard]] static net_endpoint
  find_one(cstring_view host, uint16_t port, int family = AF_UNSPEC) {
    const auto results = find_all(host, port, family, 1);
    return results.empty() ? net_endpoint{} : results.front();
  }
};

}} // namespace corvid::proto
