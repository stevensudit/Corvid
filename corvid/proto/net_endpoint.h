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
#include <algorithm>
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

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../filesys/net_socket.h"
#include "endian.h"
#include "ipv4_addr.h"
#include "ipv6_addr.h"

namespace corvid { inline namespace proto {

// Unified network endpoint: an IPv4/IPv6 address with port, or a Unix domain
// socket path.
//
// Stores the endpoint in a `sockaddr_storage`, using `ss_family` as the tag.
// Default-constructs to an empty state.
//
// Constructors do not throw: on failure, they leave the endpoint `empty()`.
//
// IP construction: from an `ipv4_addr` or `ipv6_addr` plus a port, or by
// parsing text in "1.2.3.4:80" (IPv4) or "[2001:db8::1]:80" (IPv6) notation,
// where the port is mandatory but may be 0. Named factories `any_v4()` and
// `any_v6()` produce wildcard bind addresses. You can also use `dns_resolver`.
//
// UDS construction: from a path beginning with '/'. Afterwards, `uds_path()`
// retrieves the path; `to_string()` formats it as "unix:<path>". Note: UDS
// sockets are also called Pathname Sockets, as they are defined by a file
// path.
//
// This path references a placeholder file on the filesystem, which allows
// discovery but also requires filesystem permissions and potentially cleanup.
// Ideally, it goes in "/run/" or "/var/run" (with pathnames such as
// "/run/user/[UID]/[appname].sock"). Traditionally, `/tmp/` is often used,
// because everyone has access to it. Also, "/var/lib/[appname]/" is a good
// choice for persistent, app-specific sockets. Note that the file may linger
// if the process that created it shut down improperly, so you might need to
// manually delete it or else it will fail at `bind`.
//
// ANS (Abstract Name Sockets) are a UDS variant where `sun_path[0] ==
// '\0'` and the full remaining 107-byte buffer is the name. Note: ANS sockets
// are also called Abstract Sockets, as they are defined by an abstract name
// rather than a file path.
//
// They are constructed like any other UDS, except that there's a leading '@',
// and all of the characters that follow are significant. While the name often
// looks pathlike, it can be literally anything: there's no connection with
// directory structure, and even embedded zeros are significant. They can be
// discovered by parsing "/proc/net/unix".
//
// An ANS is a UDS, so `is_uds()` returns true for it, as does the
// more-specific `is_ans()`. For an ANS, `uds_path()` skips the leading
// '\0' and returns the full 107-byte remainder, including trailing zeros;
// `to_string()` formats it as "unix:@<name>", which does truncate at the first
// zero (after the '@').
//
// Interop with `sockaddr_in`, `sockaddr_in6`, `sockaddr_un`, and
// `sockaddr_storage` is provided.
class net_endpoint {
public:
  static_assert(sizeof(sockaddr_un) <= sizeof(sockaddr_storage),
      "`sockaddr_storage` is not large enough to hold `sockaddr_un`");

  // Constructors.
  constexpr net_endpoint() noexcept = default;

  // Construct from pieces.

  // Construct from an `ipv4_addr` or `ipv6_addr` and a port number.
  explicit net_endpoint(ipv4_addr addr, uint16_t port) noexcept {
    auto& raw = as_v4();
    raw.sin_family = AF_INET;
    raw.sin_port = hton16(port);
    raw.sin_addr = addr.to_in_addr();
  }

  explicit net_endpoint(ipv6_addr addr, uint16_t port) noexcept {
    auto& raw = as_v6();
    raw.sin6_family = AF_INET6;
    raw.sin6_port = hton16(port);
    raw.sin6_addr = addr.to_in6_addr();
  }

  // Construct from text: "1.2.3.4:80" (IPv4), "[2001:db8::1]:80" (IPv6),
  // "/run/user/[UID]/[appname].sock" (UDS), or "@abstract_name" (ANS). For
  // IPv4 and IPv6, port is required but may be "0", as a wildcard. On failure,
  // result is `empty()`.
  explicit net_endpoint(std::string_view s) { *this = do_parse(s); }

  // Conversion constructors for interop.

  // Construct from a POSIX `sockaddr_in`, `sockaddr_in6`, or `sockaddr_un`.
  explicit net_endpoint(const sockaddr_in& addr) noexcept {
    do_assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), sizeof(addr));
  }

  explicit net_endpoint(const sockaddr_in6& addr) noexcept {
    do_assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), sizeof(addr));
  }

  explicit net_endpoint(const sockaddr_un& addr) noexcept {
    do_assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), sizeof(addr));
  }

  explicit net_endpoint(const sockaddr& addr, socklen_t len) noexcept {
    do_assign_sockaddr(addr, len);
  }

  // Only supports recognized families (AF_INET, AF_INET6, AF_UNIX).
  explicit net_endpoint(const sockaddr_storage& addr) noexcept {
    do_assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), sizeof(addr));
  }

  // Construct by querying the local address bound to `sock` via `getsockname`.
  // On failure, result is `empty()`.
  explicit net_endpoint(const net_socket& sock) noexcept {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(sock.handle(), reinterpret_cast<sockaddr*>(&addr),
            &len) == 0)
      do_assign_sockaddr(reinterpret_cast<const sockaddr&>(addr), len);
  }

  // Create wildcard bind endpoints for IPv4 or IPv6 with the given port.
  [[nodiscard]] static net_endpoint any_v4(uint16_t port = 0) noexcept {
    return net_endpoint{ipv4_addr::any, port};
  }

  [[nodiscard]] static net_endpoint any_v6(uint16_t port = 0) noexcept {
    return net_endpoint{ipv6_addr::any, port};
  }

  // Return whether this endpoint is empty (i.e., has no valid address).
  [[nodiscard]] constexpr bool empty() const noexcept { return !family(); }

  // Return whether this endpoint has an address.
  [[nodiscard]] constexpr operator bool() const noexcept { return !empty(); }

  // Access family flag.
  [[nodiscard]] constexpr sa_family_t family() const noexcept {
    return storage_.ss_family;
  }

  // Return whether this endpoint holds an IPv4, IPv6, UDS, or ANS address,
  // respectively.
  [[nodiscard]] constexpr bool is_v4() const noexcept {
    return family() == AF_INET;
  }

  [[nodiscard]] constexpr bool is_v6() const noexcept {
    return family() == AF_INET6;
  }

  [[nodiscard]] constexpr bool is_uds() const noexcept {
    return family() == AF_UNIX;
  }

  [[nodiscard]] constexpr bool is_ans() const noexcept {
    return is_uds() && as_uds().sun_path[0] == '\0';
  }

  // Return the port number. For UDS/ANS (or `empty()`), returns 0.
  [[nodiscard]] constexpr uint16_t port() const noexcept {
    if (is_v4()) return ntoh16(as_v4().sin_port);
    if (is_v6()) return ntoh16(as_v6().sin6_port);
    return 0;
  }

  // Return the held `ipv4_addr` or `ipv6_addr`, respectively, or nullopt if
  // the endpoint holds something else.
  [[nodiscard]] constexpr std::optional<ipv4_addr> v4() const noexcept {
    if (!is_v4()) return std::nullopt;
    return ipv4_addr{as_v4().sin_addr};
  }

  [[nodiscard]] constexpr std::optional<ipv6_addr> v6() const noexcept {
    if (!is_v6()) return std::nullopt;
    return ipv6_addr{as_v6().sin6_addr};
  }

  // Return the UDS path, or an empty `string_view` if not a UDS endpoint.
  // For ANS, skips the leading `\0` and returns the full 107-byte remainder
  // (including trailing zeros, which are significant for ANS).
  [[nodiscard]] constexpr std::string_view uds_path() const noexcept {
    if (!is_uds()) return {};
    const auto& sun = as_uds().sun_path;
    if (sun[0]) return sun;            // UDS.
    return {sun + 1, sizeof(sun) - 1}; // ANS.
  }

  // Return the raw UDS/ANS path buffer, including leading '\0' for ANS and all
  // trailing zeros, or an empty `string_view` if not a UDS endpoint. This is
  // useful for ANS, where every byte is significant.
  [[nodiscard]] constexpr std::string_view raw_uds_path() const noexcept {
    if (!is_uds()) return {};
    return do_raw_uds_path();
  }

  // Comparison operators.
  // Only endpoints with the same family can be equal: there is no special
  // handling for IPv4-Mapped IPv6 Addresses.

  [[nodiscard]] friend constexpr bool
  operator==(const net_endpoint& lhs, const net_endpoint& rhs) noexcept {
    return (lhs <=> rhs) == 0;
  }

  [[nodiscard]] friend constexpr std::strong_ordering
  operator<=>(const net_endpoint& lhs, const net_endpoint& rhs) noexcept {
    if (const auto by_family = lhs.family() <=> rhs.family(); by_family != 0)
      return by_family;

    if (lhs.empty()) return std::strong_ordering::equal;
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
      return lhs.do_raw_uds_path() <=> rhs.do_raw_uds_path();
    }
    // NOLINTEND(bugprone-unchecked-optional-access)
    return lhs.port() <=> rhs.port();
  }

  // Format as "1.2.3.4:80" (IPv4), "[2001:db8::1]:80" (IPv6), "unix:<path>"
  // (regular UDS), "unix:@<name>" (terminated ANS),  or "(invalid)".
  [[nodiscard]] constexpr std::string to_string() const {
    if (const auto addr = v4())
      return std::format("{}:{}", addr->to_string(), port());
    if (const auto addr = v6())
      return std::format("[{}]:{}", addr->to_string(), port());
    if (is_ans()) {
      const auto name = uds_path();
      const auto null_pos = name.find('\0');
      const auto display = name.substr(0, null_pos);
      const auto npos = std::string_view::npos;
      const bool has_more =
          null_pos != npos && name.find_first_not_of('\0', null_pos) != npos;
      return std::format("unix:@{}{}", display, has_more ? " (+)" : "");
    }
    if (is_uds()) return std::format("unix:{}", uds_path());
    return "(invalid)";
  }

  friend std::ostream& operator<<(std::ostream& os, const net_endpoint& ep) {
    return os << ep.to_string();
  }

  // Convert to the corresponding POSIX socket address struct.
  // `as_sockaddr_in`, `as_sockaddr_in6`, and `as_sockaddr_un` require the
  // endpoint to hold the matching family; `as_sockaddr_storage` works for
  // any.
  [[nodiscard]] constexpr const sockaddr_in& as_sockaddr_in() const {
    assert(is_v4());
    return as_v4();
  }

  [[nodiscard]] constexpr const sockaddr_in6& as_sockaddr_in6() const {
    assert(is_v6());
    return as_v6();
  }

  [[nodiscard]] constexpr const sockaddr_un& as_sockaddr_un() const {
    assert(is_uds());
    return as_uds();
  }

  [[nodiscard]] constexpr const sockaddr_storage&
  as_sockaddr_storage() const noexcept {
    return storage_;
  }

  // Implicit conversion for interop.
  [[nodiscard]] constexpr operator const sockaddr_storage&() const noexcept {
    return storage_;
  }

  // Return the size of the sockaddr struct corresponding to the held endpoint.
  [[nodiscard]] socklen_t sockaddr_size() const noexcept {
    return net_socket::sockaddr_size(storage_);
  }

  // Return a pointer and length suitable for passing to POSIX socket
  // functions.
  [[nodiscard]] constexpr std::pair<const sockaddr*, socklen_t>
  as_sockaddr() const noexcept {
    const auto addr = reinterpret_cast<const sockaddr*>(&storage_);
    return {addr, sockaddr_size()};
  }

  // Convenient invalid endpoint.
  static const net_endpoint invalid;

private:
  void constexpr do_assign_sockaddr(const sockaddr& addr,
      socklen_t len) noexcept {
    const auto count = static_cast<size_t>(len);
    if (addr.sa_family == AF_INET && count >= sizeof(sockaddr_in))
      as_v4() = *reinterpret_cast<const sockaddr_in*>(&addr);
    else if (addr.sa_family == AF_INET6 && count >= sizeof(sockaddr_in6))
      as_v6() = *reinterpret_cast<const sockaddr_in6*>(&addr);
    else if (addr.sa_family == AF_UNIX && count >= sizeof(sa_family_t))
      std::memcpy(&as_uds(), &addr, std::min(count, sizeof(sockaddr_un)));
  }

  // Create a UDS or ANS endpoint from `path`.
  // - Regular UDS (`/`-prefixed): copies up to 107 chars, null-terminated.
  // - ANS (`@`-prefixed): `sun_path[0] = '\0'`, name occupies
  // `sun_path[1..107]`
  //   without an added null terminator (the full buffer is the name; trailing
  //   zeros from zero-initialization are significant, not padding).
  [[nodiscard]] static constexpr net_endpoint do_parse_uds(
      std::string_view path) {
    net_endpoint ep;
    auto& raw = ep.as_uds();
    raw.sun_family = AF_UNIX;

    if (!path.empty() && path[0] == '@')
      path.substr(1).copy(raw.sun_path + 1, sizeof(raw.sun_path) - 1);
    else
      path.copy(raw.sun_path, sizeof(raw.sun_path) - 1);

    return ep;
  }

  [[nodiscard]] static constexpr std::optional<uint16_t> do_parse_port(
      std::string_view s) noexcept {
    uint32_t port = 0;
    const auto [ptr, ec] =
        std::from_chars(s.data(), s.data() + s.size(), port);
    if (ec != std::errc{} || ptr != s.data() + s.size() || port > 65535U)
      return std::nullopt;
    return static_cast<uint16_t>(port);
  }

  // Internal reinterpretation. Note that `auto this` doesn't work well in this
  // use case.

  [[nodiscard]] constexpr sockaddr_in& as_v4() noexcept {
    return *reinterpret_cast<sockaddr_in*>(&storage_);
  }

  [[nodiscard]] constexpr const sockaddr_in& as_v4() const noexcept {
    return *reinterpret_cast<const sockaddr_in*>(&storage_);
  }

  [[nodiscard]] constexpr sockaddr_in6& as_v6() noexcept {
    return *reinterpret_cast<sockaddr_in6*>(&storage_);
  }

  [[nodiscard]] constexpr const sockaddr_in6& as_v6() const noexcept {
    return *reinterpret_cast<const sockaddr_in6*>(&storage_);
  }

  [[nodiscard]] constexpr sockaddr_un& as_uds() noexcept {
    return *reinterpret_cast<sockaddr_un*>(&storage_);
  }

  [[nodiscard]] constexpr const sockaddr_un& as_uds() const noexcept {
    return *reinterpret_cast<const sockaddr_un*>(&storage_);
  }

  [[nodiscard]] static constexpr net_endpoint do_parse(std::string_view s) {
    if (s.empty()) return {};

    // UDS or ANS.
    if (s[0] == '/' || s[0] == '@') return do_parse_uds(s);

    // IPv6.
    if (s[0] == '[') {
      const auto close = s.find(']');
      if (close == std::string_view::npos || close + 1 >= s.size() ||
          s[close + 1] != ':')
        return {};

      const auto addr = ipv6_addr::parse(s.substr(1, close - 1));
      const auto port = do_parse_port(s.substr(close + 2));
      if (!addr || !port) return {};
      return net_endpoint{*addr, *port};
    }

    // IPv4.
    const auto colon = s.rfind(':');
    if (colon == std::string_view::npos) return {};
    if (s.find(':') != colon) return {};

    const auto addr = ipv4_addr::parse(s.substr(0, colon));
    const auto port = do_parse_port(s.substr(colon + 1));
    if (!addr || !port) return {};
    return net_endpoint{*addr, *port};
  }

  [[nodiscard]] constexpr std::string_view do_raw_uds_path() const noexcept {
    return {as_uds().sun_path, sizeof(as_uds().sun_path)};
  }

private:
  sockaddr_storage storage_{};
};

inline const net_endpoint net_endpoint::invalid{};

}} // namespace corvid::proto
