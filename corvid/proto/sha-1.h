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
#include <string>
#include <string_view>

namespace corvid { inline namespace proto {

// Small SHA-1 helper used by protocol code that needs a stable 20-byte digest.
// This is suitable for non-security-critical protocol work such as the
// WebSocket handshake.
//
// Not touched by human hands. Vibe-coded by Claude for use in WebSocket
// accept-key computation, then factored out by Codex.
struct sha_1 {
  using digest_t = std::array<uint32_t, 5>;
  using bytes_t = std::array<uint8_t, 20>;

  [[nodiscard]] static digest_t digest(std::string_view msg) {
    digest_t out{};
    digest_into(msg, out);
    return out;
  }

  static void digest_into(std::string_view msg, digest_t& h) {
    h = {0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};

    // Pre-process into 512-bit blocks with the RFC 3174 padding rules.
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8U;
    std::string padded{msg};
    padded.push_back('\x80');
    while (padded.size() % 64 != 56) padded.push_back('\0');
    for (int i = 7; i >= 0; --i)
      padded.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));

    for (size_t off = 0; off < padded.size(); off += 64) {
      uint32_t w[80];
      for (size_t i = 0; i < 16; ++i) {
        w[i] =
            (static_cast<uint32_t>(static_cast<uint8_t>(padded[off + (i * 4)]))
                << 24) |
            (static_cast<uint32_t>(
                 static_cast<uint8_t>(padded[off + (i * 4) + 1]))
                << 16) |
            (static_cast<uint32_t>(
                 static_cast<uint8_t>(padded[off + (i * 4) + 2]))
                << 8) |
            (static_cast<uint32_t>(
                static_cast<uint8_t>(padded[off + (i * 4) + 3])));
      }
      for (int i = 16; i < 80; ++i)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

      uint32_t a = h[0];
      uint32_t b = h[1];
      uint32_t c = h[2];
      uint32_t d = h[3];
      uint32_t e = h[4];
      for (int i = 0; i < 80; ++i) {
        uint32_t f{};
        uint32_t k{};
        if (i < 20) {
          f = (b & c) | (~b & d);
          k = 0x5A827999U;
        } else if (i < 40) {
          f = b ^ c ^ d;
          k = 0x6ED9EBA1U;
        } else if (i < 60) {
          f = (b & c) | (b & d) | (c & d);
          k = 0x8F1BBCDCU;
        } else {
          f = b ^ c ^ d;
          k = 0xCA62C1D6U;
        }
        const uint32_t tmp = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = tmp;
      }
      h[0] += a;
      h[1] += b;
      h[2] += c;
      h[3] += d;
      h[4] += e;
    }
  }

  [[nodiscard]] static bytes_t bytes(const digest_t& h) noexcept {
    bytes_t raw{};
    for (size_t i = 0; i < 5; ++i) {
      raw[i * 4] = static_cast<uint8_t>(h[i] >> 24);
      raw[(i * 4) + 1] = static_cast<uint8_t>(h[i] >> 16);
      raw[(i * 4) + 2] = static_cast<uint8_t>(h[i] >> 8);
      raw[(i * 4) + 3] = static_cast<uint8_t>(h[i]);
    }
    return raw;
  }

private:
  static constexpr uint32_t rol(uint32_t v, unsigned n) noexcept {
    return (v << n) | (v >> (32U - n));
  }
};

}} // namespace corvid::proto
