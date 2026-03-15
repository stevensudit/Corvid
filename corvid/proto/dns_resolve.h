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
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <netdb.h>
#include <sys/socket.h>
#endif

#include "ip_endpoint.h"

namespace corvid { inline namespace proto {

// Resolves hostnames to `ip_endpoint` values via `getaddrinfo`.
struct dns_resolver {
  // Resolve a hostname to a list of `ip_endpoint` values.
  //
  // `host` is a hostname or numeric address string (e.g. `"example.com"` or
  // `"127.0.0.1"`). `port` is the port number. `family` may be `AF_UNSPEC`
  // (default, returns both IPv4 and IPv6 results), `AF_INET`, or `AF_INET6`.
  // `max_results` caps the number of endpoints returned (default: unlimited).
  //
  // Returns an empty vector on failure (e.g. unknown host) or if the resolver
  // returned only address families other than `AF_INET` / `AF_INET6`. Only
  // `SOCK_STREAM` results are requested to avoid duplicate entries per
  // address.
  [[nodiscard]] static std::vector<ip_endpoint> find_all(std::string_view host,
      uint16_t port, int family = AF_UNSPEC, size_t max_results = SIZE_MAX) {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    addrinfo hints{};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    // Convert port number to a decimal string for `getaddrinfo`.
    const std::string port_str = std::to_string(port);

    // `getaddrinfo` requires a null-terminated host string.
    const std::string host_str{host};

    std::vector<ip_endpoint> endpoints;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res) != 0)
      return endpoints;

    const auto info = std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)>{res,
        ::freeaddrinfo};
    res = nullptr;

    for (const addrinfo* ai = info.get(); ai && endpoints.size() < max_results;
        ai = ai->ai_next)
    {
      if (ai->ai_family == AF_INET &&
          ai->ai_addrlen >= static_cast<socklen_t>(sizeof(sockaddr_in)))
        endpoints.emplace_back(
            *reinterpret_cast<const sockaddr_in*>(ai->ai_addr));
      else if (ai->ai_family == AF_INET6 &&
               ai->ai_addrlen >= static_cast<socklen_t>(sizeof(sockaddr_in6)))
        endpoints.emplace_back(
            *reinterpret_cast<const sockaddr_in6*>(ai->ai_addr));
    }

    return endpoints;
#else
    (void)host;
    (void)port;
    (void)family;
    (void)max_results;
    return {};
#endif
  }

  // Resolve a hostname to a single `ip_endpoint`.
  //
  // Returns a default-constructed (invalid) `ip_endpoint` on failure or if no
  // matching address was found.
  [[nodiscard]] static ip_endpoint
  find_one(std::string_view host, uint16_t port, int family = AF_UNSPEC) {
    const auto results = find_all(host, port, family, 1);
    return results.empty() ? ip_endpoint{} : results.front();
  }
};

}} // namespace corvid::proto
