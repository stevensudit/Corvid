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
#include <cassert>
#include <charconv>
#include <cstring>
#include <compare>
#include <cstdint>
#include <format>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ipv4_addr.h"
#include "ipv6_addr.h"

namespace corvid { inline namespace proto {

// Unified network endpoint: an IPv4/IPv6 address with port, or a Unix domain
// socket path.
//
// Stores the endpoint in a `sockaddr_storage`, using `ss_family` as the tag.
// Default-constructs to an invalid (empty) state; use `is_valid()` to check.
//
// IP construction: from an `ipv4_addr` or `ipv6_addr` plus a port, or by
// parsing text in `1.2.3.4:80` (IPv4) or `[2001:db8::1]:80` (IPv6) notation.
// Named factories `any_v4()` and `any_v6()` produce wildcard bind addresses.
//
// UDS construction: pass a path beginning with `/` to `parse()` or the
// `string_view` constructor. `uds_path()` retrieves the path; `to_string()`
// formats it as `unix:<path>`. Paths longer than `sun_path` capacity
// (107 chars) are silently truncated.
//
// On POSIX targets, interop with `sockaddr_in`, `sockaddr_in6`, `sockaddr_un`,
// and `sockaddr_storage` is provided.
class net_endpoint {
public:
  // Default-construct to an invalid state.
  constexpr net_endpoint() noexcept = default;

  // Construct from an `ipv4_addr` or `ipv6_addr` and a port number.
  explicit net_endpoint(ipv4_addr addr, uint16_t port) noexcept {
    auto& raw = as_v4();
    raw.sin_family = AF_INET;
    raw.sin_port = htons(port);
    raw.sin_addr = addr.to_in_addr();
  }

  explicit net_endpoint(ipv6_addr addr, uint16_t port) noexcept {
    auto& raw = as_v6();
    raw.sin6_family = AF_INET6;
    raw.sin6_port = htons(port);
    raw.sin6_addr = addr.to_in6_addr();
  }

  // Construct from text: `1.2.3.4:80` or `[2001:db8::1]:80`.
  explicit net_endpoint(std::string_view s) {
    const auto parsed = parse(s);
    if (!parsed) throw std::invalid_argument("Invalid IP endpoint");
    *this = *parsed;
  }

  // Create wildcard bind endpoints for IPv4 or IPv6 with the given port.
  [[nodiscard]] static net_endpoint any_v4(uint16_t port = 0) noexcept {
    return net_endpoint{ipv4_addr::any(), port};
  }

  [[nodiscard]] static net_endpoint any_v6(uint16_t port = 0) noexcept {
    return net_endpoint{ipv6_addr::any(), port};
  }

  // Parse an IP endpoint (`1.2.3.4:80`, `[2001:db8::1]:80`) or a UDS path
  // (any string beginning with `/`). Returns nullopt on failure.
  [[nodiscard]] static std::optional<net_endpoint> parse(std::string_view s) {
    if (s.empty()) return std::nullopt;

    if (s[0] == '/') return do_create_uds(s);

    if (s[0] == '[') {
      const auto close = s.find(']');
      if (close == std::string_view::npos || close + 1 >= s.size() ||
          s[close + 1] != ':')
        return std::nullopt;

      const auto addr = ipv6_addr::parse(s.substr(1, close - 1));
      const auto port = parse_port(s.substr(close + 2));
      if (!addr || !port) return std::nullopt;
      return net_endpoint{*addr, *port};
    }

    const auto colon = s.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    if (s.find(':') != colon) return std::nullopt;

    const auto addr = ipv4_addr::parse(s.substr(0, colon));
    const auto port = parse_port(s.substr(colon + 1));
    if (!addr || !port) return std::nullopt;
    return net_endpoint{*addr, *port};
  }

  // Return true if this endpoint holds a valid (non-default) address.
  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return family() == AF_INET || family() == AF_INET6 || family() == AF_UNIX;
  }

  // Return true if this endpoint holds an IPv4, IPv6, or UDS address.
  [[nodiscard]] constexpr bool is_v4() const noexcept {
    return family() == AF_INET;
  }

  [[nodiscard]] constexpr bool is_v6() const noexcept {
    return family() == AF_INET6;
  }

  [[nodiscard]] constexpr bool is_uds() const noexcept {
    return family() == AF_UNIX;
  }

  // Return the port number.
  [[nodiscard]] uint16_t port() const noexcept {
    if (is_v4()) return ntohs(as_v4().sin_port);
    if (is_v6()) return ntohs(as_v6().sin6_port);
    return 0;
  }

  // Return the held `ipv4_addr` or `ipv6_addr`, respectively, or nullopt if
  // the endpoint holds something else.
  [[nodiscard]] std::optional<ipv4_addr> v4() const noexcept {
    if (!is_v4()) return std::nullopt;
    return ipv4_addr{as_v4().sin_addr};
  }

  [[nodiscard]] std::optional<ipv6_addr> v6() const noexcept {
    if (!is_v6()) return std::nullopt;
    return ipv6_addr{as_v6().sin6_addr};
  }

  // Return the UDS path, or an empty `string_view` if not a UDS endpoint.
  [[nodiscard]] std::string_view uds_path() const noexcept {
    if (!is_uds()) return {};
    return as_uds().sun_path;
  }

  // Three-way comparison; endpoints are equal when both address and port
  // match.
  [[nodiscard]] friend bool
  operator==(const net_endpoint& lhs, const net_endpoint& rhs) noexcept {
    return (lhs <=> rhs) == 0;
  }

  [[nodiscard]] friend std::strong_ordering
  operator<=>(const net_endpoint& lhs, const net_endpoint& rhs) noexcept {
    if (const auto by_family = lhs.family() <=> rhs.family(); by_family != 0)
      return by_family;

    if (!lhs.is_valid()) return std::strong_ordering::equal;
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    if (lhs.is_v4()) {
      if (const auto by_addr = lhs.v4()->to_uint32() <=> rhs.v4()->to_uint32();
          by_addr != 0)
        return by_addr;
    } else if (lhs.is_v6()) {
      if (const auto by_addr = lhs.v6()->words() <=> rhs.v6()->words();
          by_addr != 0)
        return by_addr;
    } else if (lhs.is_uds()) {
      return lhs.uds_path() <=> rhs.uds_path();
    }
    // NOLINTEND(bugprone-unchecked-optional-access)
    return lhs.port() <=> rhs.port();
  }

  // Format as `1.2.3.4:80` (IPv4), `[2001:db8::1]:80` (IPv6),
  // `unix:<path>` (UDS), or `(invalid)` if default-constructed.
  [[nodiscard]] std::string to_string() const {
    if (const auto addr = v4())
      return std::format("{}:{}", addr->to_string(), port());
    if (const auto addr = v6())
      return std::format("[{}]:{}", addr->to_string(), port());
    if (is_uds()) return std::format("unix:{}", uds_path());
    return "(invalid)";
  }

  friend std::ostream& operator<<(std::ostream& os, const net_endpoint& ep) {
    return os << ep.to_string();
  }

  // Construct from a POSIX `sockaddr_in`, `sockaddr_in6`, or `sockaddr_un`.
  explicit net_endpoint(const sockaddr_in& addr) noexcept {
    assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), sizeof(addr));
  }

  explicit net_endpoint(const sockaddr_in6& addr) noexcept {
    assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), sizeof(addr));
  }

  explicit net_endpoint(const sockaddr_un& addr) noexcept {
    assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), sizeof(addr));
  }

  // Construct from a POSIX `sockaddr` plus its actual length. Only IPv4 and
  // IPv6 addresses with sufficient storage are recognized; anything else
  // leaves the endpoint in its default (invalid) state.
  explicit net_endpoint(const sockaddr& addr, socklen_t len) noexcept {
    assign_sockaddr(addr, len);
  }

  // Construct from a `sockaddr_storage`. Only IPv4 and IPv6 families are
  // recognized; any other family leaves the endpoint in its default
  // (invalid) state.
  explicit net_endpoint(const sockaddr_storage& addr) noexcept {
    assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), sizeof(addr));
  }

  // Convert to the corresponding POSIX socket address struct. `as_sockaddr_in`
  // and `as_sockaddr_in6` require the endpoint to hold the matching family;
  // `as_sockaddr_storage` works for either.
  [[nodiscard]] const sockaddr_in& as_sockaddr_in() const {
    assert(is_v4());
    return as_v4();
  }

  [[nodiscard]] const sockaddr_in6& as_sockaddr_in6() const {
    assert(is_v6());
    return as_v6();
  }

  [[nodiscard]] const sockaddr_un& as_sockaddr_un() const {
    assert(is_uds());
    return as_uds();
  }

  [[nodiscard]] constexpr const sockaddr_storage&
  as_sockaddr_storage() const noexcept {
    return storage_;
  }

  [[nodiscard]] constexpr operator const sockaddr_storage&() const noexcept {
    return storage_;
  }

  [[nodiscard]] socklen_t sockaddr_size() const noexcept {
    if (is_v4()) return sizeof(sockaddr_in);
    if (is_v6()) return sizeof(sockaddr_in6);
    if (is_uds())
      return static_cast<socklen_t>(
          offsetof(sockaddr_un, sun_path) + std::strlen(as_uds().sun_path) + 1);
    return sizeof(sockaddr_storage);
  }

  [[nodiscard]] constexpr std::pair<const sockaddr*, socklen_t>
  as_sockaddr() const noexcept {
    const auto addr = reinterpret_cast<const sockaddr*>(&storage_);
    return {addr, sockaddr_size()};
  }

  static const net_endpoint invalid;

private:
  void assign_sockaddr(const sockaddr& addr, socklen_t len) noexcept {
    if (addr.sa_family == AF_INET &&
        len >= static_cast<socklen_t>(sizeof(sockaddr_in)))
      as_v4() = *reinterpret_cast<const sockaddr_in*>(&addr);
    else if (addr.sa_family == AF_INET6 &&
             len >= static_cast<socklen_t>(sizeof(sockaddr_in6)))
      as_v6() = *reinterpret_cast<const sockaddr_in6*>(&addr);
    else if (addr.sa_family == AF_UNIX &&
             len >= static_cast<socklen_t>(sizeof(sa_family_t)))
      std::memcpy(&as_uds(), &addr,
          std::min(len, static_cast<socklen_t>(sizeof(sockaddr_un))));
  }

  // Create a UDS endpoint for `path`. Paths longer than `sun_path` capacity
  // (107 chars) are silently truncated.
  [[nodiscard]] static net_endpoint do_create_uds(std::string_view path) noexcept {
    net_endpoint ep;
    auto& raw = ep.as_uds();
    raw.sun_family = AF_UNIX;
    const auto len = path.copy(raw.sun_path, sizeof(raw.sun_path) - 1);
    raw.sun_path[len] = '\0';
    return ep;
  }

  [[nodiscard]] static constexpr std::optional<uint16_t> parse_port(
      std::string_view s) noexcept {
    uint32_t port = 0;
    const auto [ptr, ec] =
        std::from_chars(s.data(), s.data() + s.size(), port);
    if (ec != std::errc{} || ptr != s.data() + s.size() || port > 65535U)
      return std::nullopt;
    return static_cast<uint16_t>(port);
  }

  [[nodiscard]] constexpr sa_family_t family() const noexcept {
    return storage_.ss_family;
  }

  [[nodiscard]] sockaddr_in& as_v4() noexcept {
    return *reinterpret_cast<sockaddr_in*>(&storage_);
  }

  [[nodiscard]] const sockaddr_in& as_v4() const noexcept {
    return *reinterpret_cast<const sockaddr_in*>(&storage_);
  }

  [[nodiscard]] sockaddr_in6& as_v6() noexcept {
    return *reinterpret_cast<sockaddr_in6*>(&storage_);
  }

  [[nodiscard]] const sockaddr_in6& as_v6() const noexcept {
    return *reinterpret_cast<const sockaddr_in6*>(&storage_);
  }

  [[nodiscard]] sockaddr_un& as_uds() noexcept {
    return *reinterpret_cast<sockaddr_un*>(&storage_);
  }

  [[nodiscard]] const sockaddr_un& as_uds() const noexcept {
    return *reinterpret_cast<const sockaddr_un*>(&storage_);
  }

private:
  static_assert(sizeof(sockaddr_un) <= sizeof(sockaddr_storage),
      "`sockaddr_storage` is not large enough to hold `sockaddr_un`");

  sockaddr_storage storage_{};
};

inline const net_endpoint net_endpoint::invalid{};

}} // namespace corvid::proto
