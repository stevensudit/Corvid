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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace corvid { inline namespace proto {

namespace detail {
constexpr std::string_view alpha =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Builds the Base64 decode lookup table at compile time. Values 0-63 map a
// valid Base64 character to its 6-bit index; 0xFF marks an invalid character.
constexpr std::array<uint8_t, 256> make_base64_decode_table() noexcept {
  std::array<uint8_t, 256> t{};
  t.fill(0xFF);
  for (uint8_t i{}; i < 64; ++i) t[uint8_t(detail::alpha[i])] = i;
  return t;
}

inline constexpr auto base64_decode_table = make_base64_decode_table();
} // namespace detail

// Small Base64 helper for protocol code that needs RFC 4648-style text
// encoding of arbitrary bytes.
//
// Barely touched by human hands. Vibe-coded by Claude for use in WebSocket
// accept-key computation. Note that, according to Claude, this would not be
// optimized by resizing without initialization because -o3 is smart enough to
// elide the expansion checks due to knowing the size up front.
struct base_64 {
  [[nodiscard]] static std::string encode(std::span<const uint8_t> bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    size_t i{};
    for (; i + 2 < bytes.size(); i += 3) {
      const uint32_t triple =
          (static_cast<uint32_t>(bytes[i]) << 16) |
          (static_cast<uint32_t>(bytes[i + 1]) << 8) | bytes[i + 2];
      out.push_back(detail::alpha[(triple >> 18) & 0x3F]);
      out.push_back(detail::alpha[(triple >> 12) & 0x3F]);
      out.push_back(detail::alpha[(triple >> 6) & 0x3F]);
      out.push_back(detail::alpha[triple & 0x3F]);
    }

    const size_t remain = bytes.size() - i;
    if (remain == 1) {
      const uint32_t triple = static_cast<uint32_t>(bytes[i]) << 16;
      out.push_back(detail::alpha[(triple >> 18) & 0x3F]);
      out.push_back(detail::alpha[(triple >> 12) & 0x3F]);
      out += "==";
    } else if (remain == 2) {
      const uint32_t triple =
          (static_cast<uint32_t>(bytes[i]) << 16) |
          (static_cast<uint32_t>(bytes[i + 1]) << 8);
      out.push_back(detail::alpha[(triple >> 18) & 0x3F]);
      out.push_back(detail::alpha[(triple >> 12) & 0x3F]);
      out.push_back(detail::alpha[(triple >> 6) & 0x3F]);
      out.push_back('=');
    }

    return out;
  }

  [[nodiscard]] static std::string encode(std::string_view bytes) {
    return encode(std::span{reinterpret_cast<const uint8_t*>(bytes.data()),
        bytes.size()});
  }

  // Decode a Base64-encoded string (RFC 4648). Returns the decoded bytes, or
  // an empty vector if `encoded` contains invalid characters or has a length
  // that is not a multiple of 4.
  [[nodiscard]] static std::vector<uint8_t> decode(std::string_view encoded) {
    if (encoded.size() % 4 != 0) return {};
    if (encoded.empty()) return std::vector<uint8_t>{};

    // Count trailing '=' padding characters.
    size_t pad{};
    if (encoded.back() == '=') {
      ++pad;
      if (encoded[encoded.size() - 2] == '=') ++pad;
    }

    std::vector<uint8_t> out;
    out.reserve(((encoded.size() / 4) * 3) - pad);

    const size_t groups{encoded.size() / 4};
    for (size_t g{}; g < groups; ++g) {
      const size_t pos{g * 4};
      const bool is_last{g + 1 == groups};

      const auto& dt = detail::base64_decode_table;
      const uint8_t a{dt[uint8_t(encoded[pos])]};
      const uint8_t b{dt[uint8_t(encoded[pos + 1])]};
      if ((a | b) >= 64) {
        out.clear();
        return out;
      }

      if (is_last && pad == 2) {
        // 2-padding: 2 base64 chars -> 1 output byte.
        out.push_back(uint8_t((uint32_t(a) << 2) | (uint32_t(b) >> 4)));
        break;
      }

      const uint8_t c{dt[uint8_t(encoded[pos + 2])]};
      if (c >= 64) {
        out.clear();
        return out;
      }

      if (is_last && pad == 1) {
        // 1-padding: 3 base64 chars -> 2 output bytes.
        const uint32_t triple{
            (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(c) << 6)};
        out.push_back(uint8_t(triple >> 16));
        out.push_back(uint8_t((triple >> 8) & 0xFF));
        break;
      }

      const uint8_t d{dt[uint8_t(encoded[pos + 3])]};
      if (d >= 64) {
        out.clear();
        return out;
      }

      const uint32_t triple{
          (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(c) << 6) | d};
      out.push_back(uint8_t(triple >> 16));
      out.push_back(uint8_t((triple >> 8) & 0xFF));
      out.push_back(uint8_t(triple & 0xFF));
    }

    return out;
  }
};

}} // namespace corvid::proto
