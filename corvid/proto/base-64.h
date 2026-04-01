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

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace corvid { inline namespace proto {

// Small Base64 helper for protocol code that needs RFC 4648-style text
// encoding of arbitrary bytes.
//
// Not touched by human hands. Vibe-coded by Claude for use in WebSocket
// accept-key computation, then factored out by Codex.
struct base_64 {
  [[nodiscard]] static std::string encode(std::span<const uint8_t> bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    size_t i{};
    for (; i + 2 < bytes.size(); i += 3) {
      const uint32_t triple =
          (static_cast<uint32_t>(bytes[i]) << 16) |
          (static_cast<uint32_t>(bytes[i + 1]) << 8) | bytes[i + 2];
      out.push_back(chars_[(triple >> 18) & 0x3F]);
      out.push_back(chars_[(triple >> 12) & 0x3F]);
      out.push_back(chars_[(triple >> 6) & 0x3F]);
      out.push_back(chars_[triple & 0x3F]);
    }

    const size_t remain = bytes.size() - i;
    if (remain == 1) {
      const uint32_t triple = static_cast<uint32_t>(bytes[i]) << 16;
      out.push_back(chars_[(triple >> 18) & 0x3F]);
      out.push_back(chars_[(triple >> 12) & 0x3F]);
      out += "==";
    } else if (remain == 2) {
      const uint32_t triple =
          (static_cast<uint32_t>(bytes[i]) << 16) |
          (static_cast<uint32_t>(bytes[i + 1]) << 8);
      out.push_back(chars_[(triple >> 18) & 0x3F]);
      out.push_back(chars_[(triple >> 12) & 0x3F]);
      out.push_back(chars_[(triple >> 6) & 0x3F]);
      out.push_back('=');
    }

    return out;
  }

  [[nodiscard]] static std::string encode(std::string_view bytes) {
    return encode(std::span{reinterpret_cast<const uint8_t*>(bytes.data()),
        bytes.size()});
  }

private:
  static constexpr std::string_view chars_ =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
};

}} // namespace corvid::proto
