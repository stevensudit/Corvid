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
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <netinet/in.h>

#include "../math/arithmetic.h"
#include "../strings/conversion.h"
#include "ipv4_addr.h"

namespace corvid { inline namespace proto {

#pragma region ipv6_addr

// IPv6 address.
//
// `ipv6_addr` wraps a 128-bit IPv6 address as 16 bytes in network order. It
// supports construction from eight 16-bit groups, a 16-byte array, or a
// colon-hex string. Comparison, classification predicates, and formatting are
// provided.
//
// Constructors do not throw: on failure, they leave it `empty`. See `parse`
// for the alternative.
class ipv6_addr {
public:
#pragma region Types

  using byte_array = std::array<uint8_t, 16>;
  using word_array = std::array<uint16_t, 8>;
  static_assert(sizeof(in6_addr) == sizeof(byte_array));
  static_assert(std::is_trivially_copyable_v<in6_addr>);

#pragma endregion
#pragma region Construction

  // Default-constructs to the "any" address (::), which is empty.
  constexpr ipv6_addr() noexcept = default;

  // Construct from eight 16-bit groups in network order (most significant
  // first), e.g., `ipv6_addr{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1}`.
  constexpr ipv6_addr(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
      uint16_t e, uint16_t f, uint16_t g, uint16_t h) noexcept
      : bytes_{extract_byte<1>(a), extract_byte<0>(a), extract_byte<1>(b),
            extract_byte<0>(b), extract_byte<1>(c), extract_byte<0>(c),
            extract_byte<1>(d), extract_byte<0>(d), extract_byte<1>(e),
            extract_byte<0>(e), extract_byte<1>(f), extract_byte<0>(f),
            extract_byte<1>(g), extract_byte<0>(g), extract_byte<1>(h),
            extract_byte<0>(h)} {}

  // Construct from the raw bytes in network order.
  explicit constexpr ipv6_addr(const byte_array& bytes) noexcept
      : bytes_{bytes} {}

  // Construct from a colon-hex string (e.g., "2001:db8::1").
  //
  // If not a valid IPv6 address, the result is `empty`. If you need to
  // distinguish between an invalid address and the "any" address ("::"), use
  // `parse` instead.
  explicit constexpr ipv6_addr(std::string_view s) {
    if (const auto parsed = parse(s); parsed) *this = *parsed;
  }

  constexpr explicit ipv6_addr(const in6_addr& a) noexcept
      : bytes_{std::bit_cast<byte_array>(a)} {}

#pragma endregion
#pragma region Constants

  static const ipv6_addr any;
  static const ipv6_addr loopback;

#pragma endregion
#pragma region Emptiness

  // Whether this address is empty, which is also the "any" address ("::").
  [[nodiscard]] constexpr bool empty() const noexcept {
    const auto qwords = std::bit_cast<std::array<uint64_t, 2>>(bytes_);
    return qwords[0] == 0 && qwords[1] == 0;
  }

  // Return whether this has a non-any address.
  [[nodiscard]] constexpr operator bool() const noexcept { return !empty(); }

#pragma endregion
#pragma region Parsing

  // Parse from IPv6 colon-hex notation, including "::" compression and
  // IPv4-embedded tails such as `::ffff:192.168.1.1`.
  // Returns `std::nullopt` if the string is not a valid IPv6 address.
  [[nodiscard]] static constexpr std::optional<ipv6_addr> parse(
      std::string_view s) {
    word_array groups{};
    size_t group_count{};
    auto double_colon = 8UZ;

    if (!do_parse_groups_loop(s, groups, group_count, double_colon))
      return std::nullopt;
    if (!do_finalize_groups(groups, group_count, double_colon))
      return std::nullopt;

    return ipv6_addr{groups_to_bytes(groups)};
  }

#pragma endregion
#pragma region Accessors

  // Return the 16 bytes in network order.
  [[nodiscard]] constexpr const byte_array& bytes() const noexcept {
    return bytes_;
  }

  // Return the eight 16-bit groups in network order.
  [[nodiscard]] constexpr word_array words() const noexcept {
    return {word_at(0), word_at(1), word_at(2), word_at(3), word_at(4),
        word_at(5), word_at(6), word_at(7)};
  }

#pragma endregion
#pragma region Classification

  [[nodiscard]] constexpr bool is_any() const noexcept { return empty(); }

  [[nodiscard]] constexpr bool is_loopback() const noexcept {
    return *this == loopback;
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

#pragma endregion
#pragma region Comparison

  [[nodiscard]] friend constexpr auto
  operator<=>(const ipv6_addr&, const ipv6_addr&) noexcept = default;

#pragma endregion
#pragma region Formatting

  // Format using lowercase hex with RFC 5952-style zero-run compression.
  [[nodiscard]] constexpr std::string to_string() const {
    const auto groups = words();
    auto best_start = 8UZ;
    size_t best_len{};
    size_t cur_start{};
    size_t cur_len{};

    // Figure out how many empty groups to skip before we start.
    for (auto ndx = 0UZ; ndx < 8; ++ndx) {
      if (groups[ndx] == 0) {
        if (cur_len == 0) cur_start = ndx;
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
    for (auto ndx = 0UZ; ndx < 8; ++ndx) {
      if (ndx == best_start) {
        out += "::";
        ndx += best_len - 1;
        continue;
      }
      if (!out.empty() && out.back() != ':') out += ':';
      do_append_hex_group(out, groups[ndx]);
    }
    if (out.empty()) out = "::";
    return out;
  }

  friend std::ostream& operator<<(std::ostream& os, const ipv6_addr& a) {
    return os << a.to_string();
  }

  [[nodiscard]] constexpr in6_addr to_in6_addr() const noexcept {
    return std::bit_cast<in6_addr>(bytes_);
  }

#pragma endregion
#pragma region Implementation
private:
  // Parse the IPv4-embedded tail token (e.g., "192.168.1.1") and append two
  // 16-bit groups to `groups`, advancing `group_count`. Returns false if
  // `group_count > 6` or the token is not a valid IPv4 address.
  [[nodiscard]] static constexpr bool
  do_append_ipv4_groups(std::string_view token, word_array& groups,
      size_t& group_count) noexcept {
    if (group_count > 6) return false;
    const auto ipv4 = ipv4_addr::parse(token);
    if (!ipv4) return false;
    const auto octets = ipv4->octets();
    groups[group_count++] = combine_bytes(octets[1], octets[0]);
    groups[group_count++] = combine_bytes(octets[3], octets[2]);
    return true;
  }

  // Validate and consume the "::" that starts at `s[pos]`. Updates `pos` and
  // `double_colon`. Returns false if "::" is malformed or a second "::"
  // appears.
  [[nodiscard]] static constexpr bool
  do_advance_double_colon(std::string_view s, size_t& pos, size_t group_count,
      size_t& double_colon) noexcept {
    if (pos + 1 >= s.size() || s[pos + 1] != ':' || double_colon != 8)
      return false;
    double_colon = group_count;
    pos += 2;
    return true;
  }

  // Consume the separator that follows a parsed hex group. Returns `nullopt`
  // on error (bad char, second "::", trailing ':'), `false` when the string
  // is exhausted (caller should break), `true` to continue parsing groups.
  [[nodiscard]] static constexpr std::optional<bool>
  do_consume_separator(std::string_view s, size_t& pos, size_t group_count,
      size_t& double_colon) noexcept {
    if (pos == s.size()) return false;
    if (s[pos] != ':') return std::nullopt;
    if (pos + 1 < s.size() && s[pos + 1] == ':') {
      if (double_colon != 8) return std::nullopt;
      double_colon = group_count;
      pos += 2;
      return pos < s.size();
    }
    ++pos;
    if (pos == s.size()) return std::nullopt;
    return true;
  }

  // Parse one non-colon item (hex group or IPv4-embedded tail) at `s[pos]`,
  // append it to `groups`, then consume the following separator. Returns
  // `nullopt` on error, `false` when parsing is complete, `true` to continue.
  [[nodiscard]] static constexpr std::optional<bool>
  parse_one_item(std::string_view s, size_t& pos, word_array& groups,
      size_t& group_count, size_t& double_colon) {
    if (group_count == 8) return std::nullopt;
    auto token_end = pos;
    while (token_end < s.size() && s[token_end] != ':') ++token_end;
    const auto token = s.substr(pos, token_end - pos);
    if (token.contains('.')) {
      if (token_end != s.size()) return std::nullopt;
      if (!do_append_ipv4_groups(token, groups, group_count))
        return std::nullopt;
      pos = token_end;
      return false;
    }
    const auto group = do_parse_group(s, pos);
    if (!group) return std::nullopt;
    groups[group_count++] = *group;
    return do_consume_separator(s, pos, group_count, double_colon);
  }

  // Scan `s` and fill `groups[0..group_count)`, recording the "::" insertion
  // point in `double_colon` (value 8 means no "::" was seen). Returns false
  // on any syntax error.
  [[nodiscard]] static constexpr bool do_parse_groups_loop(std::string_view s,
      word_array& groups, size_t& group_count, size_t& double_colon) {
    if (s.empty()) return false;
    size_t pos{};
    while (pos < s.size()) {
      if (s[pos] == ':') {
        if (!do_advance_double_colon(s, pos, group_count, double_colon))
          return false;
        if (pos == s.size()) break;
        continue;
      }
      const auto result =
          parse_one_item(s, pos, groups, group_count, double_colon);
      if (!result) return false;
      if (!*result) break;
    }
    return true;
  }

  // Validate the group count and expand the "::" zero-run in `groups` in
  // place. `double_colon` is the insertion index, or 8 if no "::" was seen.
  // Returns false if the total group count is inconsistent.
  [[nodiscard]] static constexpr bool do_finalize_groups(word_array& groups,
      size_t group_count, size_t double_colon) noexcept {
    if (double_colon == 8) return group_count == 8;
    if (group_count == 8) return false;
    const auto zeros = 8 - group_count;
    for (auto ndx = group_count; ndx > double_colon; --ndx)
      groups[ndx + zeros - 1] = groups[ndx - 1];
    for (auto ndx = 0UZ; ndx < zeros; ++ndx) groups[double_colon + ndx] = 0;
    return true;
  }

  [[nodiscard]] static constexpr std::optional<uint16_t>
  do_parse_group(std::string_view s, size_t& pos) noexcept {
    if (pos >= s.size() || !strings::is_hex_digit(s[pos])) return std::nullopt;
    uint16_t value{};
    size_t digits{};
    while (pos < s.size() && strings::is_hex_digit(s[pos])) {
      value = static_cast<uint16_t>(
          (value << 4) | strings::hex_digit_value(s[pos]));
      ++pos;
      ++digits;
      if (digits > 4) return std::nullopt;
    }
    return value;
  }

  [[nodiscard]] static constexpr byte_array groups_to_bytes(
      const word_array& groups) noexcept {
    return {extract_byte<1>(groups[0]), extract_byte<0>(groups[0]),
        extract_byte<1>(groups[1]), extract_byte<0>(groups[1]),
        extract_byte<1>(groups[2]), extract_byte<0>(groups[2]),
        extract_byte<1>(groups[3]), extract_byte<0>(groups[3]),
        extract_byte<1>(groups[4]), extract_byte<0>(groups[4]),
        extract_byte<1>(groups[5]), extract_byte<0>(groups[5]),
        extract_byte<1>(groups[6]), extract_byte<0>(groups[6]),
        extract_byte<1>(groups[7]), extract_byte<0>(groups[7])};
  }

  [[nodiscard]] constexpr uint16_t word_at(size_t i) const noexcept {
    return combine_bytes(bytes_[(2 * i) + 1], bytes_[2 * i]);
  }

  static constexpr void do_append_hex_group(std::string& out, uint16_t value) {
    // Deliberately uninitialized: only the written cells are ever read.
    char buffer[4];
    int len{};

    do {
      buffer[len++] = strings::as_hex_lc_digit(value);
      value = static_cast<uint16_t>(value >> 4);
    } while (value != 0);

    while (len-- > 0) out += buffer[len];
  }

#pragma endregion
#pragma region Data members
private:
  byte_array bytes_{};

#pragma endregion
};

#pragma endregion
#pragma region Constant definitions

constexpr ipv6_addr ipv6_addr::any{};
constexpr ipv6_addr ipv6_addr::loopback{uint16_t{0}, uint16_t{0}, uint16_t{0},
    uint16_t{0}, uint16_t{0}, uint16_t{0}, uint16_t{0}, uint16_t{1}};

#pragma endregion
}} // namespace corvid::proto
