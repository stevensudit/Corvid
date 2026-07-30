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

#include "../../math/arithmetic.h"

namespace corvid { inline namespace proto {

#pragma region sha_1

// Small SHA-1 helper used by protocol code that needs a stable 20-byte digest.
// This is suitable for non-security-critical protocol work such as the
// WebSocket handshake.
//
// Not touched by human hands. Vibe-coded by Claude for use in WebSocket
// accept-key computation, then factored out by Codex.
struct sha_1 {
#pragma region Types

  using digest_t = std::array<uint32_t, 5>;
  using bytes_t = std::array<uint8_t, 20>;

#pragma endregion
#pragma region Operations

  [[nodiscard]] static digest_t digest(std::string_view msg) {
    digest_t out{};
    digest_into(msg, out);
    return out;
  }

  static void digest_into(std::string_view msg, digest_t& h) {
    h = {0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};

    // Pre-process into 512-bit blocks with the RFC 3174 padding rules.
    const auto bit_len = static_cast<uint64_t>(msg.size()) * 8U;
    std::string padded{msg};
    padded.push_back('\x80');
    while (padded.size() % 64 != 56) padded.push_back('\0');
    for (auto ndx = 7; ndx >= 0; --ndx)
      padded.push_back(static_cast<char>((bit_len >> (ndx * 8)) & 0xFF));

    for (auto off = 0UZ; off < padded.size(); off += 64) {
      // Deliberately uninitialized: only the written cells are ever read, and
      // zeroing 320 bytes per block is not free.
      uint32_t w[80];
      for (auto ndx = 0UZ; ndx < 16; ++ndx)
        w[ndx] = combine_bytes(
            static_cast<uint8_t>(padded[off + (ndx * 4) + 3]),
            static_cast<uint8_t>(padded[off + (ndx * 4) + 2]),
            static_cast<uint8_t>(padded[off + (ndx * 4) + 1]),
            static_cast<uint8_t>(padded[off + (ndx * 4)]));
      for (auto ndx = 16; ndx < 80; ++ndx)
        w[ndx] = rol(w[ndx - 3] ^ w[ndx - 8] ^ w[ndx - 14] ^ w[ndx - 16], 1);

      auto a = h[0];
      auto b = h[1];
      auto c = h[2];
      auto d = h[3];
      auto e = h[4];
      for (auto ndx = 0; ndx < 80; ++ndx) {
        uint32_t f{};
        uint32_t k{};
        if (ndx < 20) {
          f = (b & c) | (~b & d);
          k = 0x5A827999U;
        } else if (ndx < 40) {
          f = b ^ c ^ d;
          k = 0x6ED9EBA1U;
        } else if (ndx < 60) {
          f = (b & c) | (b & d) | (c & d);
          k = 0x8F1BBCDCU;
        } else {
          f = b ^ c ^ d;
          k = 0xCA62C1D6U;
        }
        const auto tmp = rol(a, 5) + f + e + k + w[ndx];
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
    for (auto ndx = 0UZ; ndx < 5; ++ndx) {
      raw[ndx * 4] = extract_byte<3>(h[ndx]);
      raw[(ndx * 4) + 1] = extract_byte<2>(h[ndx]);
      raw[(ndx * 4) + 2] = extract_byte<1>(h[ndx]);
      raw[(ndx * 4) + 3] = extract_byte<0>(h[ndx]);
    }
    return raw;
  }

#pragma endregion
#pragma region Implementation
private:
  static constexpr uint32_t rol(uint32_t v, unsigned n) noexcept {
    return (v << n) | (v >> (32U - n));
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::proto
