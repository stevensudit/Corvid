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

// Linux-only: Windows sockets are a different API (Winsock `SOCKET`).
#ifdef _WIN32
#error "\"sockaddr_view.h\" is Linux-only."
#endif
#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string_view>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../math/endian.h"
#include "ipv4_addr.h"
#include "ipv6_addr.h"
#include "socket_enums.h"

namespace corvid { inline namespace proto {

#pragma region sockaddr_view

// Immutable, non-owning view of a POSIX socket address, containing a pointer
// to the `sockaddr` buffer and its length.
//
// Conversions can fail, such as when the contents do not match the length, but
// constructors do not throw: on failure, they leave the view `empty`.
//
// Note that, while the length can be derived from the contents for the common
// address families, it has to be explicit whenever it cannot be. This mostly
// means abstract-namespace Unix domain sockets (ANS UDS), whose layout is not
// self-describing, but also applies to address families not directly supported
// by this class. In either case, a stated length is accepted so long as it is
// plausible, but conversion without one fails politely.
struct sockaddr_view {
#pragma region Data

  const sockaddr* const addr{};
  const socklen_t addrlen{};

#pragma endregion
#pragma region Construction

  sockaddr_view() noexcept = default;

  // Construct from a pointer to a `sockaddr` buffer and its length, without
  // validating the length.
  //
  // This is the low-level constructor to use when you already have a validated
  // buffer and length, such as inside `net_endpoint`, or when it cannot be
  // validated, such as an ANS UDS.
  sockaddr_view(const sockaddr* address, socklen_t length) noexcept
      : addr{address}, addrlen{length} {}

  // Construct from a `sockaddr` buffer and its length, validating that the
  // length is consistent with the contents.
  //
  // It is an error to pass the length of the entire `sockaddr_storage` buffer
  // when the contents do not fill it completely.
  //
  // Also, when we cannot calculate the length from the contents (an ANS UDS
  // or an unsupported address family), this method accepts the presented
  // `length` as correct so long as it's plausible.
  sockaddr_view(const sockaddr& address, socklen_t length) noexcept
      : addr{&address}, addrlen{validate_stated_length(&address, length)} {}

  // Construct from a `sockaddr` buffer, calculating the length from the
  // contents.
  //
  // This will always fail for an ANS UDS or an unsupported address family,
  // since the length cannot be derived from the contents.
  sockaddr_view(const sockaddr& addr) noexcept
      : addr{&addr}, addrlen{calculate_valid_length(addr)} {}

  // Construct from a `sockaddr_in`, validating the contents.
  sockaddr_view(const sockaddr_in& address) noexcept
      : sockaddr_view{*as_addr(&address), sizeof(address)} {}

  // Construct from a `sockaddr_in6`, validating the contents.
  sockaddr_view(const sockaddr_in6& address) noexcept
      : sockaddr_view{*as_addr(&address), sizeof(address)} {}

  // Construct from a `sockaddr_un`, validating the contents.
  //
  // Note that this will not work for an ANS UDS; you will have to use a
  // constructor that takes a `length`.
  sockaddr_view(const sockaddr_un& address) noexcept
      : sockaddr_view{*as_addr(&address)} {}

  // Construct from a `sockaddr_storage`, validating the contents.
  //
  // Note that this will not work for an ANS UDS or an unknown address family;
  // you will have to use a constructor that takes a `length`.
  sockaddr_view(const sockaddr_storage& address) noexcept
      : sockaddr_view{*as_addr(&address)} {}

#pragma endregion
#pragma region Properties

  // A default-constructed instance, or one that failed conversion, is empty.
  [[nodiscard]] bool empty() const noexcept { return !addr || !addrlen; }

  [[nodiscard]] explicit operator bool() const noexcept { return !empty(); }

  // Terminate the arrow chain from `net_endpoint::operator->` by yielding a
  // pointer to this view.
  [[nodiscard]] const sockaddr_view* operator->() const noexcept {
    return this;
  }

  // Return whether the address is unnamed, which is to say that the entire
  // length is just `sizeof(sa_family_t)`.
  //
  // This is possible when calling `getsockaddr` on an unbound socket.
  [[nodiscard]] bool unnamed() const noexcept {
    return addrlen <= sizeof(sa_family_t);
  }

  [[nodiscard]] constexpr address_family family() const noexcept {
    if (empty()) return {};
    return address_family{addr->sa_family};
  }

  [[nodiscard]] constexpr bool is_v4() const noexcept {
    return family() == address_family::inet;
  }

  [[nodiscard]] constexpr bool is_v6() const noexcept {
    return family() == address_family::inet6;
  }

  [[nodiscard]] constexpr bool is_uds() const noexcept {
    return family() == address_family::unix;
  }

  [[nodiscard]] bool is_ans() const noexcept {
    const auto path = raw_uds_path();
    return (!path.empty() && path.front() == '\0');
  }

  // Return the viewed `ipv4_addr` or `ipv6_addr`, respectively, or nullopt if
  // the view is over something else.
  [[nodiscard]] std::optional<ipv4_addr> v4() const noexcept {
    if (!is_v4()) return std::nullopt;
    return ipv4_addr{as_sockaddr_in().sin_addr};
  }

  [[nodiscard]] std::optional<ipv6_addr> v6() const noexcept {
    if (!is_v6()) return std::nullopt;
    return ipv6_addr{as_sockaddr_in6().sin6_addr};
  }

  // Return the port number in host order.
  //
  // For UDS/ANS (or `empty` or `unnamed`), returns 0.
  [[nodiscard]] uint16_t port() const noexcept {
    if (unnamed()) return 0;
    if (is_v4()) return ntoh16(as_sockaddr_in().sin_port);
    if (is_v6()) return ntoh16(as_sockaddr_in6().sin6_port);
    return 0;
  }

  // Return the UDS path, or an empty `string_view` if not a UDS address (or
  // if it is an unnamed one).
  //
  // For ANS, skips the leading '\0' and returns the length-delimited name,
  // where embedded and trailing zeros are significant.
  [[nodiscard]] std::string_view uds_path() const noexcept {
    // Remove leading or trailing '\0', as appropriate.
    auto path = raw_uds_path();
    if (path.empty()) return {};

    if (path.front() == '\0')
      path.remove_prefix(1);
    else if (path.back() == '\0')
      path.remove_suffix(1);

    return path;
  }

  // Return the raw `sun_path` bytes of a named UDS address.
  //
  // The length is derived from `addrlen`, not the contents of `sun_path`.
  // Therefore, the returned value includes the leading `\0` for ANS and the
  // trailing `\0` for a UDS pathname.
  [[nodiscard]] std::string_view raw_uds_path() const noexcept {
    if (!is_uds() || unnamed()) return {};
    return {as_sockaddr_un().sun_path,
        addrlen - offsetof(sockaddr_un, sun_path)};
  }

  // Return a span over the raw address bytes, providing the basis for
  // comparison and hashing.
  [[nodiscard]] std::span<const std::byte> as_span() const noexcept {
    return {reinterpret_cast<const std::byte*>(addr), addrlen};
  }

#pragma endregion
#pragma region Comparison

  // Comparison operators.
  //
  // Only endpoints with the same family can be equal: there is no special
  // handling for IPv4-Mapped IPv6 Addresses. Comparison delegates to
  // `sockaddr_view`, whose length-delimited bytes also feed
  // `std::hash<net_endpoint>`. For IPv6, this means endpoints differing only
  // in `sin6_scope_id` or `sin6_flowinfo` compare unequal. Since link-local
  // addresses with different scopes are distinct destinations, this is
  // correct. The stored address length participates, too, so that ANS names
  // differing only in length compare unequal.

  // Comparison is over the length-delimited address bytes, so the family,
  // the contents, and (for ANS) the length all participate.
  [[nodiscard]] friend bool
  operator==(const sockaddr_view& lhs, const sockaddr_view& rhs) noexcept {
    return std::ranges::equal(lhs.as_span(), rhs.as_span());
  }

  [[nodiscard]] friend std::strong_ordering
  operator<=>(const sockaddr_view& lhs, const sockaddr_view& rhs) noexcept {
    const auto lhs_bytes = lhs.as_span();
    const auto rhs_bytes = rhs.as_span();
    return std::lexicographical_compare_three_way(lhs_bytes.begin(),
        lhs_bytes.end(), rhs_bytes.begin(), rhs_bytes.end());
  }

  [[nodiscard]] const sockaddr_in& as_sockaddr_in() const {
    assert(is_v4());
    return reinterpret_cast<const sockaddr_in&>(*addr);
  }

  [[nodiscard]] const sockaddr_in6& as_sockaddr_in6() const {
    assert(is_v6());
    return reinterpret_cast<const sockaddr_in6&>(*addr);
  }

  [[nodiscard]] const sockaddr_un& as_sockaddr_un() const {
    assert(is_uds());
    return reinterpret_cast<const sockaddr_un&>(*addr);
  }

#pragma endregion
#pragma region Helpers

  [[nodiscard]] static socklen_t calculate_length(
      const sockaddr& addr) noexcept {
    const auto fam = address_family{addr.sa_family};

    if (fam == address_family::inet) return sizeof(sockaddr_in);

    if (fam == address_family::inet6) return sizeof(sockaddr_in6);

    if (fam == address_family::unix) {
      const auto& sun = reinterpret_cast<const sockaddr_un&>(addr);
      const char* path = sun.sun_path;
      const auto path_size = sizeof(sun.sun_path);
      if (path[0]) {
        const auto name_len = strnlen(path, path_size);
        const auto termination = (name_len < path_size) ? 1UZ : 0UZ;
        const auto calculated =
            offsetof(sockaddr_un, sun_path) + name_len + termination;
        if (calculated <= sizeof(sockaddr_un)) return calculated;
      }
    }
    // Fail if ANS or unknown family.
    return 0;
  }

private:
  [[nodiscard]] static const sockaddr* as_addr(
      const void* any_address) noexcept {
    return reinterpret_cast<const sockaddr*>(any_address);
  }

  static constexpr socklen_t max_length = sizeof(sockaddr_storage);

  // Calculate the length of a `sockaddr` buffer based on its contents,
  // truncating to 0 if it won't fit (such as when a UDS pathname is too long).
  //
  // The purpose is to allow passing in an IPv4 address in a full
  // `sockaddr_storage` and using just the correct length.
  [[nodiscard]] static socklen_t calculate_valid_length(
      const sockaddr& address) noexcept {
    const auto calculated = calculate_length(address);
    if (calculated > max_length) return 0;
    return calculated;
  }

  // Validate the stated length, returning 0 if it doesn't match our own
  // calculations.
  //
  // When no length can be calculated from the contents (an ANS UDS or an
  // unknown address family), the stated length is accepted so long as it is
  // plausible.
  [[nodiscard]] static socklen_t
  validate_stated_length(const void* any_address, socklen_t length) noexcept {
    if (!any_address || !length || length > max_length) return 0;
    const auto address = as_addr(any_address);
    const auto calculated = calculate_length(*address);
    if (calculated && calculated != length) return 0;
    return length;
  }
#pragma endregion
};

#pragma endregion
#pragma region sockaddr_buffer_ref

// Mutable references to a socket-address buffer and its length.
//
// Used as an output parameter by calls that fill an address in place, such as
// `net_socket::accept`.
//
// Obtain one over a `net_endpoint` via its `as_ref`.
struct sockaddr_buffer_ref {
  sockaddr_storage& addr;
  socklen_t& addrlen;
};

#pragma endregion
}} // namespace corvid::proto

#pragma region formatter

// Format a `sockaddr_view` as "1.2.3.4:80" (IPv4), "[2001:db8::1]:80" (IPv6),
// "unix:<path>" (UDS), "unix:@<name>" (ANS), or "(invalid)". An ANS name with
// an embedded '\0' is truncated there, with " (len N)" appended. No format
// spec is supported.
//
// The addresses themselves are formatted as `ipv4_addr` and `ipv6_addr` are.
template<>
struct std::formatter<corvid::proto::sockaddr_view> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    auto it = ctx.begin();
    if (it != ctx.end() && *it != '}')
      throw std::format_error("sockaddr_view accepts no format spec");
    return it;
  }

  static auto
  format(const corvid::proto::sockaddr_view& view, std::format_context& ctx) {
    if (const auto addr = view.v4())
      return std::format_to(ctx.out(), "{}:{}", *addr, view.port());
    if (const auto addr = view.v6())
      return std::format_to(ctx.out(), "[{}]:{}", *addr, view.port());
    if (view.is_ans()) {
      const auto name = view.uds_path();
      const auto null_pos = name.find('\0');
      if (null_pos == std::string_view::npos)
        return std::format_to(ctx.out(), "unix:@{}", name);
      return std::format_to(ctx.out(), "unix:@{} (len {})",
          name.substr(0, null_pos), name.size());
    }
    if (view.is_uds())
      return std::format_to(ctx.out(), "unix:{}", view.uds_path());
    return std::format_to(ctx.out(), "(invalid)");
  }
};

#pragma endregion
