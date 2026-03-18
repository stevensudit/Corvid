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
#include <cstdint>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include <netinet/in.h>

namespace corvid { inline namespace proto {

// IPv4 address, stored as a 32-bit value in host byte order.
//
// Default-constructs to an empty state. Construction is available from four
// octets (most-significant first), a host-byte-order `uint32_t`, or by parsing
// dotted-decimal notation. Interop with `in_addr` is provided via the
// constructor and `to_in_addr()`.
//
// Constructors do not throw: on failure, they leave it `empty()`. See
// `parse()` for the alternative.
//
// All address arithmetic uses host byte order internally; network byte order
// is only introduced at the `in_addr` boundary.
class ipv4_addr {
public:
  using byte_array = std::array<uint8_t, 4>;

  // Constructors.

  // Default-constructs to the "any" address (0.0.0.0), which is empty.
  constexpr ipv4_addr() noexcept = default;

  // Construct from four octets in network order (most significant first),
  // e.g., `ipv4_addr{192, 168, 1, 1}`.
  explicit constexpr ipv4_addr(uint8_t a, uint8_t b, uint8_t c,
      uint8_t d) noexcept
      : addr_{(uint32_t{a} << 24) | (uint32_t{b} << 16) | (uint32_t{c} << 8) |
              uint32_t{d}} {}

  // Construct from a host-byte-order `uint32_t`.
  explicit constexpr ipv4_addr(uint32_t host_order) noexcept
      : addr_{host_order} {}

  // Construct from a dotted-decimal string (e.g., "192.168.1.1").
  // If not a valid IPv4 address, the result is `empty()`. If you need to
  // distinguish between an invalid address and the "any" address ("0.0.0.0"),
  // use `parse()` instead.
  explicit constexpr ipv4_addr(std::string_view s) {
    if (const auto parsed = parse(s); parsed) *this = *parsed;
  }

  // Construct from a POSIX `in_addr` (which is in network byte order).
  explicit ipv4_addr(const in_addr& a) noexcept : addr_{ntohl(a.s_addr)} {}

  // Named address constants.

  // The "any" address (0.0.0.0): typically used to bind to all interfaces.
  static const ipv4_addr any;

  // The loopback address (127.0.0.1).
  static const ipv4_addr loopback;

  // The limited broadcast address (255.255.255.255).
  static const ipv4_addr broadcast;

  // Whether this address is empty, which is also the "any" address
  // ("0.0.0.0").
  [[nodiscard]] constexpr bool empty() const noexcept { return !addr_; }

  // Return whether this has a non-any address.
  [[nodiscard]] constexpr operator bool() const noexcept { return !empty(); }

  // Parsing.

  // Parse from dotted-decimal notation (e.g., "192.168.1.1").
  // Returns `std::nullopt` if the string is not a valid IPv4 address.
  // Leading zeros in any octet are rejected (e.g., "01.0.0.1" is invalid).
  [[nodiscard]] static constexpr std::optional<ipv4_addr> parse(
      std::string_view s) noexcept {
    uint32_t result{};
    for (int i = 0; i < 4; ++i) {
      // Remove leading dot, after first octet.
      if (i > 0) {
        if (s.empty() || s[0] != '.') return std::nullopt;
        s.remove_prefix(1);
      }
      if (s.empty() || !is_digit(s[0])) return std::nullopt;
      // Reject leading zeros to avoid ambiguity.
      if (s[0] == '0' && s.size() > 1 && is_digit(s[1])) return std::nullopt;
      uint32_t octet{};
      while (!s.empty() && is_digit(s[0])) {
        octet = (octet * 10) + uint32_t(s[0] - '0');
        if (octet > 255) return std::nullopt;
        s.remove_prefix(1);
      }
      result = (result << 8) | octet;
    }
    if (!s.empty()) return std::nullopt;
    return ipv4_addr{result};
  }

  // Accessors.

  // Return the four octets in network order (most significant first).
  [[nodiscard]] constexpr byte_array octets() const noexcept {
    return {uint8_t(addr_ >> 24), uint8_t(addr_ >> 16), uint8_t(addr_ >> 8),
        uint8_t(addr_)};
  }

  // Return the raw address in host byte order.
  [[nodiscard]] constexpr uint32_t to_uint32() const noexcept { return addr_; }

  // Classification predicates.

  // True if this is the "any" address (0.0.0.0).
  [[nodiscard]] constexpr bool is_any() const noexcept { return !addr_; }

  // True if this is in the loopback range (127.0.0.0/8).
  [[nodiscard]] constexpr bool is_loopback() const noexcept {
    return (addr_ >> 24) == 127U;
  }

  // True if this is a multicast address (224.0.0.0/4).
  [[nodiscard]] constexpr bool is_multicast() const noexcept {
    return (addr_ >> 28) == 0xeU;
  }

  // True if this is the limited broadcast address (255.255.255.255).
  [[nodiscard]] constexpr bool is_broadcast() const noexcept {
    return addr_ == 0xffffffffU;
  }

  // True if this is in an RFC 1918 private range:
  //   10.0.0.0/8, 172.16.0.0/12, or 192.168.0.0/16.
  [[nodiscard]] constexpr bool is_private() const noexcept {
    return (addr_ & 0xff000000U) == 0x0a000000U ||
           (addr_ & 0xfff00000U) == 0xac100000U ||
           (addr_ & 0xffff0000U) == 0xc0a80000U;
  }

  // Comparison (lexicographic on the host-byte-order value, which is also
  // numerically correct for IPv4 ordering).
  [[nodiscard]] friend constexpr auto
  operator<=>(const ipv4_addr&, const ipv4_addr&) noexcept = default;

  // Formatting.

  // Format as dotted-decimal (e.g., "192.168.1.1").
  [[nodiscard]] constexpr std::string to_string() const {
    const auto o = octets();
    return std::format("{}.{}.{}.{}", o[0], o[1], o[2], o[3]);
  }

  friend std::ostream& operator<<(std::ostream& os, const ipv4_addr& a) {
    return os << a.to_string();
  }

  // Convert to a POSIX `in_addr` (network byte order).
  [[nodiscard]] in_addr to_in_addr() const noexcept {
    return in_addr{.s_addr = htonl(addr_)};
  }

private:
  [[nodiscard]] static constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
  }

private:
  uint32_t addr_{}; // Host byte order.
};

inline constexpr ipv4_addr ipv4_addr::any{uint32_t{0}};
inline constexpr ipv4_addr ipv4_addr::loopback{uint32_t{0x7f000001U}};
inline constexpr ipv4_addr ipv4_addr::broadcast{uint32_t{0xffffffffU}};

}} // namespace corvid::proto
