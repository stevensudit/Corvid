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
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../containers/core/hash_combiner.h"
#include "../math/endian.h"
#include "ipv4_addr.h"
#include "ipv6_addr.h"
#include "net_socket.h"

namespace corvid { inline namespace proto {

#pragma region net_endpoint

// Unified network endpoint: an IPv4/IPv6 address with port, or a Unix domain
// socket path; the mutable object that a `sockaddr_view` refers to.
//
// Stores the endpoint in a `sockaddr_storage`, using `ss_family` as the tag,
// along with the explicit address length that POSIX socket calls use as
// `addrlen`. Default-constructs to an empty state.
//
// Constructors do not throw: on failure, they leave the endpoint `empty`.
//
// IP construction: from an `ipv4_addr` or `ipv6_addr` plus a port, or by
// parsing text in "1.2.3.4:80" (IPv4) or "[2001:db8::1]:80" (IPv6) notation,
// where the port is mandatory but may be 0. Named factories `any_v4` and
// `any_v6` produce wildcard bind addresses. You can also use `dns_resolver`.
//
// UDS construction: from a path beginning with '/'. Afterwards, `uds_path`
// retrieves the path; `to_string` formats it as "unix:<path>". Note: UDS
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
// ANS (Abstract Name Sockets) are a UDS variant where `sun_path[0] == '\0'`.
// The name that follows is not zero-terminated, so only the length parameter
// determines where it ends. Note: ANS sockets are also called Abstract
// Sockets, as they are defined by an abstract name rather than a file path.
//
// They are constructed like any other UDS, except that the name has leading
// '@' (which is a non-terminating placeholder for the required '\0'),
// and all of the characters that follow are significant. While the name
// often looks pathlike, it can be literally anything: there's no
// connection with directory structure, and even embedded/trailing zeros are
// significant. They can be discovered by parsing "/proc/net/unix".
//
// An ANS is a UDS, so `is_uds` returns true for it, as does the
// more-specific `is_ans`. For an ANS, `uds_path` skips the leading '\0' and
// returns the length-delimited name; `to_string` formats it as "unix:@<name>".
// A name containing an embedded '\0' is displayed truncated there, with " (len
// N)" appended; that form does not round-trip through parsing, since the bytes
// past the '\0' are lost.
//
// Interop with `sockaddr_in`, `sockaddr_in6`, `sockaddr_un`, and
// `sockaddr_storage` is provided.
class net_endpoint {
#pragma region Construction
public:
  static constexpr auto max_sockaddr_size = sizeof(sockaddr_storage);
  static_assert(sizeof(sockaddr_un) <= max_sockaddr_size,
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
    addrlen_ = sizeof(sockaddr_in);
  }

  explicit net_endpoint(ipv6_addr addr, uint16_t port) noexcept {
    auto& raw = as_v6();
    raw.sin6_family = AF_INET6;
    raw.sin6_port = hton16(port);
    raw.sin6_addr = addr.to_in6_addr();
    addrlen_ = sizeof(sockaddr_in6);
  }

  // Construct from text: "1.2.3.4:80" (IPv4), "[2001:db8::1]:80" (IPv6),
  // "/run/user/[UID]/[appname].sock" (UDS), or "@abstract_name" (ANS).
  //
  // For IPv4 and IPv6, port is required but may be "0", as a wildcard. For an
  // ANS, the length is load-bearing. On failure, result is `empty`.
  explicit net_endpoint(std::string_view s) noexcept { *this = do_parse(s); }

  // Conversion constructors for interop.

  // Construct from a view over any POSIX socket address struct.
  //
  // Note that, for an ANS or `unnamed` Unix address, you will need to either
  // construct the view by passing in the length explicitly, or use the
  // `sockaddr, socklen_t` constructor.
  explicit net_endpoint(sockaddr_view view) noexcept {
    if (view.addr) (void)assign(view);
  }

  explicit net_endpoint(const sockaddr& addr, socklen_t len) noexcept {
    (void)assign(sockaddr_view{addr, len});
  }

  // Construct from the local address bound to `sock` via `getsockname`.
  [[nodiscard]] static net_endpoint local_of(const net_socket& sock) noexcept {
    sockaddr_storage addr{};
    socklen_t len{sizeof(addr)};
    auto* ptr = reinterpret_cast<sockaddr*>(&addr);
    if (::getsockname(sock.handle(), ptr, &len)) return {};
    return net_endpoint{*ptr, len};
  }

  // Query the peer address of `sock` via `getpeername`. On failure, result
  // is `empty`.
  [[nodiscard]] static net_endpoint peer_of(const net_socket& sock) noexcept {
    sockaddr_storage addr{};
    socklen_t len{sizeof(addr)};
    auto* ptr = reinterpret_cast<sockaddr*>(&addr);
    if (::getpeername(sock.handle(), ptr, &len)) return {};
    return net_endpoint{*ptr, len};
  }

  // Create wildcard bind endpoints for IPv4 or IPv6 with the given port.
  [[nodiscard]] static net_endpoint any_v4(uint16_t port = 0) noexcept {
    return net_endpoint{ipv4_addr::any, port};
  }

  [[nodiscard]] static net_endpoint any_v6(uint16_t port = 0) noexcept {
    return net_endpoint{ipv6_addr::any, port};
  }

  [[nodiscard]] static net_endpoint loopback_v4(uint16_t port = 0) noexcept {
    return net_endpoint{ipv4_addr::loopback, port};
  }

  [[nodiscard]] static net_endpoint loopback_v6(uint16_t port = 0) noexcept {
    return net_endpoint{ipv6_addr::loopback, port};
  }

  // Assign from a `sockaddr_view`, whose constructors have already validated
  // and normalized the length.
  //
  // On failure, resets the endpoint to empty and returns false.
  //
  // A view aliasing this endpoint's own storage updates only the stored
  // length; the kernel-completion paths use this after writing an address in
  // place.
  [[nodiscard]] bool assign(sockaddr_view view) noexcept {
    if (view.empty()) return reset();
    if (view.addr == reinterpret_cast<const sockaddr*>(&storage_)) {
      addrlen_ = view.addrlen;
      return true;
    }

    addrlen_ = view.addrlen;
    std::memcpy(&storage_, view.addr, addrlen_);
    return true;
  }

#pragma endregion
#pragma region Accessors

  // Return whether this endpoint is empty (i.e., has no valid address).
  [[nodiscard]] constexpr bool empty() const noexcept { return !addrlen_; }

  // Return whether this endpoint has an address.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return !empty();
  }

  // Return whether the address is unnamed, which is to say that the entire
  // length is just `sizeof(sa_family_t)`.
  //
  // This is possible when calling `getsockaddr` on an unbound socket.
  [[nodiscard]] bool unnamed() const noexcept {
    return addrlen_ <= sizeof(sa_family_t);
  }

  bool reset() noexcept {
    storage_ = {};
    addrlen_ = 0;
    return false;
  }

  // Family, categorization, and UDS path accessors live on `sockaddr_view`,
  // so call them through `operator->`, as in `ep->uds_path()`.

  // Return the held `ipv4_addr` or `ipv6_addr`, respectively, or nullopt if
  // the endpoint holds something else.
  [[nodiscard]] std::optional<ipv4_addr> v4() const noexcept {
    return as_sockaddr_view().v4();
  }

  [[nodiscard]] std::optional<ipv6_addr> v6() const noexcept {
    return as_sockaddr_view().v6();
  }

  // Return the port number in host order.
  //
  // For UDS/ANS (or `empty` or `unnamed`), returns 0.
  [[nodiscard]] uint16_t port() const noexcept {
    return as_sockaddr_view().port();
  }

#pragma endregion
#pragma region Comparison

  // See comments for `sockaddr_view`.

  [[nodiscard]] friend bool
  operator==(const net_endpoint& lhs, const net_endpoint& rhs) noexcept {
    return lhs.as_sockaddr_view() == rhs.as_sockaddr_view();
  }

  [[nodiscard]] friend std::strong_ordering
  operator<=>(const net_endpoint& lhs, const net_endpoint& rhs) noexcept {
    return lhs.as_sockaddr_view() <=> rhs.as_sockaddr_view();
  }

#pragma endregion
#pragma region Formatting

  // Format as "1.2.3.4:80" (IPv4), "[2001:db8::1]:80" (IPv6), "unix:<path>"
  // (regular UDS), "unix:@<name>" (ANS), or "(invalid)".
  //
  // An ANS name with an embedded '\0' is truncated there, with " (len N)"
  // appended.
  //
  // Defined after the formatter, which it delegates to.
  [[nodiscard]] std::string to_string() const;

  friend std::ostream& operator<<(std::ostream& os, const net_endpoint& ep) {
    return os << ep.to_string();
  }

#pragma endregion
#pragma region Interop

  // Convert to the corresponding POSIX socket address struct.
  //
  // For `as_sockaddr_in`, `as_sockaddr_in6`, and `as_sockaddr_un`, call
  // through `operator->`, as in `ep->as_sockaddr_in()`.

  [[nodiscard]] constexpr const sockaddr_storage&
  as_sockaddr_storage() const noexcept {
    return storage_;
  }

  // Implicit conversion for interop.
  [[nodiscard]] constexpr operator const sockaddr_storage&() const noexcept {
    return storage_;
  }

  // Return the address length: the `addrlen` that POSIX socket calls take
  // alongside the `sockaddr` pointer.
  [[nodiscard]] constexpr socklen_t sockaddr_size() const noexcept {
    return addrlen_;
  }

  // Explicit conversion to the underlying view.
  [[nodiscard]] sockaddr_view as_sockaddr_view() const noexcept {
    return {reinterpret_cast<const sockaddr*>(&storage_), addrlen_};
  }

  // Return mutable references to the underlying storage and length, so that
  // calls like `net_socket::accept` can fill the storage and address in place.
  [[nodiscard]] sockaddr_buffer_ref as_ref() noexcept {
    return {storage_, addrlen_};
  }

  // Implicit conversion, which carries the address length.
  //
  // This is what allows interop with `net_socket`.
  [[nodiscard]] operator sockaddr_view() const noexcept {
    return as_sockaddr_view();
  }

  // Drill down to the underlying `sockaddr_view`, so that its accessors can
  // be called directly, as in `ep->uds_path()`.
  [[nodiscard]] sockaddr_view operator->() const noexcept {
    return as_sockaddr_view();
  }

  // Expose raw pointer to sockaddr.
  [[nodiscard]] auto as_sockaddr_ptr(this auto& self) noexcept {
    using self_t = std::remove_reference_t<decltype(self)>;
    using sockaddr_t =
        std::conditional_t<std::is_const_v<self_t>, const sockaddr, sockaddr>;
    return reinterpret_cast<sockaddr_t*>(&self.storage_);
  }

  // Return a pointer and length suitable for passing to POSIX socket
  // functions.
  [[nodiscard]] auto as_sockaddr(this auto& self) noexcept {
    return std::pair{self.as_sockaddr_ptr(), self.sockaddr_size()};
  }

  // Like `as_sockaddr`, but static.
  //
  // Since `ep` can be null, so can the pointer this method returns.
  [[nodiscard]] static std::pair<sockaddr*, socklen_t> to_sockaddr(
      net_endpoint* ep) noexcept {
    if (!ep) return {nullptr, 0};
    return ep->as_sockaddr();
  }

  // Convenient invalid endpoint; defined after class is complete.
  static const net_endpoint invalid;

#pragma endregion
#pragma region Implementation
private:
  // Create a UDS or ANS endpoint from `path`. If the path or name does not
  // fit in `sun_path`, returns an empty endpoint.
  //
  // - Regular UDS (`/`-prefixed): copies the null-terminated path ensuring
  //   that the stored length includes the terminator.
  // - ANS (`@`-prefixed): `sun_path[0] = '\0'`, the name follows without a
  //   terminator, and the stored length delimits it exactly.
  //
  // NOLINTNEXTLINE(bugprone-exception-escape): copy positions are in-bounds.
  [[nodiscard]] static net_endpoint do_parse_uds(
      std::string_view path) noexcept {
    net_endpoint ep;
    auto& raw = ep.as_uds();

    // Either way, `sun_path` stores the path bytes verbatim: for an ANS, the
    // leading '@' becomes the '\0', while a pathname instead appends one as a
    // terminator (already present from zero-initialization).
    const auto is_ans = (path[0] == '@');
    const auto stored = path.size() + (is_ans ? 0UZ : 1UZ);
    if (stored > sizeof(raw.sun_path)) return {};
    path.copy(raw.sun_path, path.size());
    if (is_ans) raw.sun_path[0] = '\0';

    raw.sun_family = AF_UNIX;
    ep.addrlen_ =
        static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + stored);
    return ep;
  }

  // Parse a decimal port number.
  [[nodiscard]] static constexpr std::optional<uint16_t> do_parse_port(
      std::string_view s) noexcept {
    uint16_t port{};
    const auto [ptr, ec] =
        std::from_chars(s.data(), s.data() + s.size(), port);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return port;
  }

  // Internal reinterpretation. Note that `auto this` doesn't work well in this
  // use case.

  [[nodiscard]] sockaddr_in& as_v4() noexcept {
    return *reinterpret_cast<sockaddr_in*>(&storage_);
  }

  [[nodiscard]] sockaddr_in6& as_v6() noexcept {
    return *reinterpret_cast<sockaddr_in6*>(&storage_);
  }

  [[nodiscard]] sockaddr_un& as_uds() noexcept {
    return *reinterpret_cast<sockaddr_un*>(&storage_);
  }

  // NOLINTNEXTLINE(bugprone-exception-escape): substr positions are in-bounds.
  [[nodiscard]] static net_endpoint do_parse(std::string_view s) noexcept {
    if (s.empty()) return {};

    // UDS or ANS.
    if (s[0] == '/' || s[0] == '@') return do_parse_uds(s);

    // IPv6.
    if (s[0] == '[') {
      const auto close = s.find(']');
      if (close == s.npos || close + 1 >= s.size() || s[close + 1] != ':')
        return {};

      const auto addr = ipv6_addr::parse(s.substr(1, close - 1));
      const auto port = do_parse_port(s.substr(close + 2));
      if (!addr || !port) return {};
      return net_endpoint{*addr, *port};
    }

    // IPv4.
    const auto colon = s.rfind(':');
    if (colon == s.npos) return {};
    if (s.find(':') != colon) return {};

    const auto addr = ipv4_addr::parse(s.substr(0, colon));
    const auto port = do_parse_port(s.substr(colon + 1));
    if (!addr || !port) return {};
    return net_endpoint{*addr, *port};
  }

#pragma endregion
#pragma region Data members
private:
  sockaddr_storage storage_{};
  socklen_t addrlen_{};

#pragma endregion
};

inline const net_endpoint net_endpoint::invalid;

#pragma endregion

}} // namespace corvid::proto

#pragma region formatter

// Format a `net_endpoint` exactly as the `sockaddr_view` over it.
template<>
struct std::formatter<corvid::proto::net_endpoint> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    auto it = ctx.begin();
    if (it != ctx.end() && *it != '}')
      throw std::format_error("net_endpoint accepts no format spec");
    return it;
  }

  static auto
  format(const corvid::proto::net_endpoint& ep, std::format_context& ctx) {
    return std::format_to(ctx.out(), "{}", ep.as_sockaddr_view());
  }
};

#pragma endregion
#pragma region to_string definition

inline std::string corvid::proto::net_endpoint::to_string() const {
  return std::format("{}", *this);
}

#pragma endregion

namespace std {
template<>
struct hash<corvid::net_endpoint> {
  [[nodiscard]] size_t operator()(
      const corvid::net_endpoint& ep) const noexcept {
    corvid::hash_combiner combiner;
    for (const auto b : ep.as_sockaddr_view().as_span())
      combiner.combine(std::to_integer<unsigned char>(b));
    return combiner.value();
  }
};
} // namespace std
