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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <netinet/in.h>
#endif

#include "ipv4_addr.h"

namespace corvid { inline namespace proto {

// IPv6 address.
//
// `ipv6_addr` wraps a 128-bit IPv6 address as 16 bytes in network order. It
// supports construction from eight 16-bit groups, a 16-byte array, or a
// colon-hex string. Comparison, classification predicates, and formatting are
// provided.
//
// Platform-specific interop (`in6_addr`) is isolated in a guarded section so
// that porting to a new OS requires changes only there.
class ipv6_addr {
public:
  // Named address factories.

  [[nodiscard]] static constexpr ipv6_addr any() noexcept {
    return ipv6_addr{};
  }

  [[nodiscard]] static constexpr ipv6_addr loopback() noexcept {
    return ipv6_addr{uint16_t{0}, uint16_t{0}, uint16_t{0}, uint16_t{0},
        uint16_t{0}, uint16_t{0}, uint16_t{0}, uint16_t{1}};
  }

  // Constructors.

  // Default-constructs to the "any" address (::).
  constexpr ipv6_addr() noexcept = default;

  // Construct from eight 16-bit groups in network order (most significant
  // first), e.g., `ipv6_addr{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1}`.
  constexpr ipv6_addr(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
      uint16_t e, uint16_t f, uint16_t g, uint16_t h) noexcept
      : bytes_{uint8_t(a >> 8), uint8_t(a), uint8_t(b >> 8), uint8_t(b),
            uint8_t(c >> 8), uint8_t(c), uint8_t(d >> 8), uint8_t(d),
            uint8_t(e >> 8), uint8_t(e), uint8_t(f >> 8), uint8_t(f),
            uint8_t(g >> 8), uint8_t(g), uint8_t(h >> 8), uint8_t(h)} {}

  // Construct from the raw bytes in network order.
  explicit constexpr ipv6_addr(std::array<uint8_t, 16> bytes) noexcept
      : bytes_{bytes} {}

  // Construct from a colon-hex string (e.g., "2001:db8::1").
  // Throws `std::invalid_argument` if the string is not a valid IPv6 address.
  explicit constexpr ipv6_addr(std::string_view s) {
    auto parsed = parse(s);
    if (!parsed) throw std::invalid_argument("Invalid IPv6 address");
    *this = *parsed;
  }

  // Parsing.

  // Parse from IPv6 colon-hex notation, including `::` compression and
  // IPv4-embedded tails such as `::ffff:192.168.1.1`.
  // Returns `std::nullopt` if the string is not a valid IPv6 address.
  [[nodiscard]] static constexpr std::optional<ipv6_addr> parse(
      std::string_view s) {
    std::array<uint16_t, 8> groups{};
    std::size_t group_count = 0;
    std::size_t double_colon = 8;
    std::size_t pos = 0;

    if (s.empty()) return std::nullopt;

    while (pos < s.size()) {
      if (s[pos] == ':') {
        if (pos + 1 >= s.size() || s[pos + 1] != ':' || double_colon != 8)
          return std::nullopt;
        double_colon = group_count;
        pos += 2;
        if (pos == s.size()) break;
        continue;
      }

      if (group_count == 8) return std::nullopt;
      auto token_end = pos;
      while (token_end < s.size() && s[token_end] != ':') ++token_end;
      const auto token = s.substr(pos, token_end - pos);
      if (token.contains('.')) {
        if (token_end != s.size() || group_count > 6) return std::nullopt;
        const auto ipv4 = ipv4_addr::parse(token);
        if (!ipv4) return std::nullopt;
        const auto octets = ipv4->octets();
        groups[group_count++] =
            uint16_t((uint16_t(octets[0]) << 8) | uint16_t(octets[1]));
        groups[group_count++] =
            uint16_t((uint16_t(octets[2]) << 8) | uint16_t(octets[3]));
        pos = token_end;
        break;
      }

      const auto group = parse_group(s, pos);
      if (!group) return std::nullopt;
      groups[group_count++] = *group;

      if (pos == s.size()) break;
      if (s[pos] != ':') return std::nullopt;
      if (pos + 1 < s.size() && s[pos + 1] == ':') {
        if (double_colon != 8) return std::nullopt;
        double_colon = group_count;
        pos += 2;
        if (pos == s.size()) break;
      } else {
        ++pos;
        if (pos == s.size()) return std::nullopt;
      }
    }

    if (double_colon == 8) {
      if (group_count != 8) return std::nullopt;
    } else {
      if (group_count == 8) return std::nullopt;
      const auto zeros = 8 - group_count;
      for (std::size_t i = group_count; i > double_colon; --i)
        groups[i + zeros - 1] = groups[i - 1];
      for (std::size_t i = 0; i < zeros; ++i) groups[double_colon + i] = 0;
    }

    if (group_count > 8) return std::nullopt;

    return ipv6_addr{groups_to_bytes(groups)};
  }

  // Accessors.

  // Return the 16 bytes in network order.
  [[nodiscard]] constexpr const std::array<uint8_t, 16>&
  bytes() const noexcept {
    return bytes_;
  }

  // Return the eight 16-bit groups in network order.
  [[nodiscard]] constexpr std::array<uint16_t, 8> words() const noexcept {
    return {word_at(0), word_at(1), word_at(2), word_at(3), word_at(4),
        word_at(5), word_at(6), word_at(7)};
  }

  // Classification predicates.

  [[nodiscard]] constexpr bool is_any() const noexcept {
    for (auto b : bytes_) {
      if (b != 0) return false;
    }
    return true;
  }

  [[nodiscard]] constexpr bool is_loopback() const noexcept {
    return bytes_[0] == 0 && bytes_[1] == 0 && bytes_[2] == 0 &&
           bytes_[3] == 0 && bytes_[4] == 0 && bytes_[5] == 0 &&
           bytes_[6] == 0 && bytes_[7] == 0 && bytes_[8] == 0 &&
           bytes_[9] == 0 && bytes_[10] == 0 && bytes_[11] == 0 &&
           bytes_[12] == 0 && bytes_[13] == 0 && bytes_[14] == 0 &&
           bytes_[15] == 1;
  }

  // True if this is in ff00::/8.
  [[nodiscard]] constexpr bool is_multicast() const noexcept {
    return bytes_[0] == 0xff;
  }

  // True if this is in fe80::/10.
  [[nodiscard]] constexpr bool is_link_local() const noexcept {
    return bytes_[0] == 0xfe && (bytes_[1] & 0xc0U) == 0x80U;
  }

  // True if this is in fc00::/7.
  [[nodiscard]] constexpr bool is_unique_local() const noexcept {
    return (bytes_[0] & 0xfeU) == 0xfcU;
  }

  [[nodiscard]] friend constexpr auto
  operator<=>(const ipv6_addr&, const ipv6_addr&) noexcept = default;

  // Formatting.

  // Format using lowercase hex with RFC 5952-style zero-run compression.
  [[nodiscard]] constexpr std::string to_string() const {
    auto groups = words();
    std::size_t best_start = 8;
    std::size_t best_len = 0;
    std::size_t cur_start = 0;
    std::size_t cur_len = 0;

    for (std::size_t i = 0; i < 8; ++i) {
      if (groups[i] == 0) {
        if (cur_len == 0) cur_start = i;
        ++cur_len;
      } else {
        if (cur_len > best_len) {
          best_start = cur_start;
          best_len = cur_len;
        }
        cur_len = 0;
      }
    }
    if (cur_len > best_len) {
      best_start = cur_start;
      best_len = cur_len;
    }
    if (best_len < 2) best_start = 8;

    std::string out;
    out.reserve(39); // Max length of an IPv6 address string.
    for (std::size_t i = 0; i < 8; ++i) {
      if (i == best_start) {
        out += "::";
        i += best_len - 1;
        continue;
      }
      if (!out.empty() && out.back() != ':') out += ':';
      append_hex_group(out, groups[i]);
    }
    if (out.empty()) out = "::";
    return out;
  }

  friend std::ostream& operator<<(std::ostream& os, const ipv6_addr& a) {
    return os << a.to_string();
  }

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  constexpr explicit ipv6_addr(const in6_addr& a) noexcept
      : bytes_{uint8_t(a.s6_addr[0]), uint8_t(a.s6_addr[1]),
            uint8_t(a.s6_addr[2]), uint8_t(a.s6_addr[3]),
            uint8_t(a.s6_addr[4]), uint8_t(a.s6_addr[5]),
            uint8_t(a.s6_addr[6]), uint8_t(a.s6_addr[7]),
            uint8_t(a.s6_addr[8]), uint8_t(a.s6_addr[9]),
            uint8_t(a.s6_addr[10]), uint8_t(a.s6_addr[11]),
            uint8_t(a.s6_addr[12]), uint8_t(a.s6_addr[13]),
            uint8_t(a.s6_addr[14]), uint8_t(a.s6_addr[15])} {}

  [[nodiscard]] constexpr in6_addr to_in6_addr() const noexcept {
    in6_addr a{};
    for (std::size_t i = 0; i < 16; ++i) a.s6_addr[i] = bytes_[i];
    return a;
  }
#endif

private:
  [[nodiscard]] static constexpr bool is_hex_digit(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  }

  [[nodiscard]] static constexpr uint16_t hex_value(char c) noexcept {
    return c >= '0' && c <= '9' ? uint16_t(c - '0')
           : c >= 'a' && c <= 'f'
               ? uint16_t(c - 'a' + 10)
               : uint16_t(c - 'A' + 10);
  }

  [[nodiscard]] static constexpr std::optional<uint16_t>
  parse_group(std::string_view s, std::size_t& pos) noexcept {
    if (pos >= s.size() || !is_hex_digit(s[pos])) return std::nullopt;
    uint16_t value = 0;
    std::size_t digits = 0;
    while (pos < s.size() && is_hex_digit(s[pos])) {
      value = uint16_t((value << 4) | hex_value(s[pos]));
      ++pos;
      ++digits;
      if (digits > 4) return std::nullopt;
    }
    return value;
  }

  [[nodiscard]] static constexpr std::array<uint8_t, 16> groups_to_bytes(
      const std::array<uint16_t, 8>& groups) noexcept {
    return {uint8_t(groups[0] >> 8), uint8_t(groups[0]),
        uint8_t(groups[1] >> 8), uint8_t(groups[1]), uint8_t(groups[2] >> 8),
        uint8_t(groups[2]), uint8_t(groups[3] >> 8), uint8_t(groups[3]),
        uint8_t(groups[4] >> 8), uint8_t(groups[4]), uint8_t(groups[5] >> 8),
        uint8_t(groups[5]), uint8_t(groups[6] >> 8), uint8_t(groups[6]),
        uint8_t(groups[7] >> 8), uint8_t(groups[7])};
  }

  [[nodiscard]] constexpr uint16_t word_at(std::size_t i) const noexcept {
    return uint16_t((uint16_t(bytes_[2 * i]) << 8) | bytes_[(2 * i) + 1]);
  }

  static constexpr void append_hex_group(std::string& out, uint16_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    char buffer[4];
    int len = 0;

    do {
      buffer[len++] = digits[value & 0xfU];
      value = uint16_t(value >> 4);
    } while (value != 0);

    while (len-- > 0) out += buffer[len];
  }

  std::array<uint8_t, 16> bytes_{};
};

}} // namespace corvid::proto
