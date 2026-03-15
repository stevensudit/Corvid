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
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "ipv4_addr.h"
#include "ipv6_addr.h"

namespace corvid { inline namespace proto {

// Unified IP endpoint: an IPv4 or IPv6 address paired with a port number.
//
// Stores the address as a `std::variant<std::monostate, ipv4_addr,
// ipv6_addr>`. Default-constructs to an invalid (empty) state; use
// `is_valid()` to check. Construction is available from an `ipv4_addr` or
// `ipv6_addr` plus a port, or by parsing text in `1.2.3.4:80` (IPv4) or
// `[2001:db8::1]:80` (IPv6) notation. Named factories `any_v4()` and
// `any_v6()` produce wildcard bind addresses. On POSIX targets, interop with
// `sockaddr_in`, `sockaddr_in6`, and `sockaddr_storage` is provided.
class ip_endpoint {
public:
  // Default-construct to an invalid state.
  constexpr ip_endpoint() noexcept = default;

  // Construct from an `ipv4_addr` or `ipv6_addr` and a port number.
  explicit constexpr ip_endpoint(ipv4_addr addr, uint16_t port) noexcept
      : addr_{addr}, port_{port} {}

  explicit constexpr ip_endpoint(ipv6_addr addr, uint16_t port) noexcept
      : addr_{addr}, port_{port} {}

  // Construct from text: `1.2.3.4:80` or `[2001:db8::1]:80`.
  explicit constexpr ip_endpoint(std::string_view s) {
    auto parsed = parse(s);
    if (!parsed) throw std::invalid_argument("Invalid IP endpoint");
    *this = *parsed;
  }

  // Create wildcard bind endpoints for IPv4 or IPv6 with the given port.
  [[nodiscard]] static constexpr ip_endpoint any_v4(
      uint16_t port = 0) noexcept {
    return ip_endpoint{ipv4_addr::any(), port};
  }

  [[nodiscard]] static constexpr ip_endpoint any_v6(
      uint16_t port = 0) noexcept {
    return ip_endpoint{ipv6_addr::any(), port};
  }

  // Parse IP and port, returning nullopt on failure.
  [[nodiscard]] static constexpr std::optional<ip_endpoint> parse(
      std::string_view s) {
    if (s.empty()) return std::nullopt;

    if (s[0] == '[') {
      const auto close = s.find(']');
      if (close == std::string_view::npos || close + 1 >= s.size() ||
          s[close + 1] != ':')
        return std::nullopt;

      const auto addr = ipv6_addr::parse(s.substr(1, close - 1));
      const auto port = parse_port(s.substr(close + 2));
      if (!addr || !port) return std::nullopt;
      return ip_endpoint{*addr, *port};
    }

    const auto colon = s.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    if (s.find(':') != colon) return std::nullopt;

    const auto addr = ipv4_addr::parse(s.substr(0, colon));
    const auto port = parse_port(s.substr(colon + 1));
    if (!addr || !port) return std::nullopt;
    return ip_endpoint{*addr, *port};
  }

  // Return true if this endpoint holds a valid (non-default) address.
  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return !std::holds_alternative<std::monostate>(addr_);
  }

  // Return true if this endpoint holds an IPv4 or IPv6 address, respectively.
  [[nodiscard]] constexpr bool is_v4() const noexcept {
    return std::holds_alternative<ipv4_addr>(addr_);
  }

  [[nodiscard]] constexpr bool is_v6() const noexcept {
    return std::holds_alternative<ipv6_addr>(addr_);
  }

  // Return the port number.
  [[nodiscard]] constexpr uint16_t port() const noexcept { return port_; }

  // Return a pointer to the held `ipv4_addr` or `ipv6_addr`, or null if the
  // endpoint holds the other family.
  [[nodiscard]] constexpr auto v4(this auto& self) noexcept {
    return std::get_if<ipv4_addr>(&self.addr_);
  }

  [[nodiscard]] constexpr auto v6(this auto& self) noexcept {
    return std::get_if<ipv6_addr>(&self.addr_);
  }

  // Three-way comparison; endpoints are equal when both address and port
  // match.
  [[nodiscard]] friend constexpr auto
  operator<=>(const ip_endpoint&, const ip_endpoint&) noexcept = default;

  // Format as `1.2.3.4:80` (IPv4) or `[2001:db8::1]:80` (IPv6), or
  // `(invalid)` if default-constructed.
  [[nodiscard]] std::string to_string() const {
    if (const auto addr = v4())
      return std::format("{}:{}", addr->to_string(), port_);
    if (const auto addr = v6())
      return std::format("[{}]:{}", addr->to_string(), port_);
    return "(invalid)";
  }

  friend std::ostream& operator<<(std::ostream& os, const ip_endpoint& ep) {
    return os << ep.to_string();
  }

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Construct from a POSIX `sockaddr_in` or `sockaddr_in6`.
  explicit ip_endpoint(const sockaddr_in& addr) noexcept
      : addr_{ipv4_addr{addr.sin_addr}}, port_{ntohs(addr.sin_port)} {}

  explicit ip_endpoint(const sockaddr_in6& addr) noexcept
      : addr_{ipv6_addr{addr.sin6_addr}}, port_{ntohs(addr.sin6_port)} {}

  // Convert to the corresponding POSIX socket address struct. `to_sockaddr_in`
  // and `to_sockaddr_in6` require the endpoint to hold the matching family;
  // `to_sockaddr_storage` works for either.
  [[nodiscard]] sockaddr_in to_sockaddr_in() const {
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_port = htons(port_);
    out.sin_addr = std::get<ipv4_addr>(addr_).to_in_addr();
    return out;
  }

  [[nodiscard]] sockaddr_in6 to_sockaddr_in6() const {
    sockaddr_in6 out{};
    out.sin6_family = AF_INET6;
    out.sin6_port = htons(port_);
    out.sin6_addr = std::get<ipv6_addr>(addr_).to_in6_addr();
    return out;
  }

  [[nodiscard]] sockaddr_storage to_sockaddr_storage() const noexcept {
    sockaddr_storage out{};
    if (const auto addr = v4()) {
      auto v4addr = sockaddr_in{};
      v4addr.sin_family = AF_INET;
      v4addr.sin_port = htons(port_);
      v4addr.sin_addr = addr->to_in_addr();
      *reinterpret_cast<sockaddr_in*>(&out) = v4addr;
    } else if (const auto addr = v6()) {
      auto v6addr = sockaddr_in6{};
      v6addr.sin6_family = AF_INET6;
      v6addr.sin6_port = htons(port_);
      v6addr.sin6_addr = v6()->to_in6_addr();
      *reinterpret_cast<sockaddr_in6*>(&out) = v6addr;
    } else {
      // Leave `out` zero-initialized, which is not a valid address but is
      // better than returning uninitialized data.
    }
    return out;
  }
#endif

private:
  [[nodiscard]] static constexpr std::optional<uint16_t> parse_port(
      std::string_view s) noexcept {
    uint32_t port = 0;
    const auto [ptr, ec] =
        std::from_chars(s.data(), s.data() + s.size(), port);
    if (ec != std::errc{} || ptr != s.data() + s.size() || port > 65535U)
      return std::nullopt;
    return static_cast<uint16_t>(port);
  }

  std::variant<std::monostate, ipv4_addr, ipv6_addr> addr_;
  uint16_t port_{};
};

}} // namespace corvid::proto
