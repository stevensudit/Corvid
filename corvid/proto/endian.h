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
#include <bit>
#include <cstdint>

namespace corvid { inline namespace proto {

// Byte-order conversion between host and network (big-endian) order.
//
// Analogous to the POSIX `htons`/`ntohs`/`htonl`/`ntohl` family, but
// extended to 16, 32, 64, and 128 bits with consistent naming:
//   `hton16`, `hton32`, `hton64`, `hton128`  -- host -> network
//   `ntoh16`, `ntoh32`, `ntoh64`, `ntoh128`  -- network -> host
//
// Since byte-swapping is its own inverse, `htonN` and `ntohN` are
// identical in implementation; both names are provided so that call
// sites read naturally.

inline auto swap_always(auto v) noexcept { return std::byteswap(v); }

inline __uint128_t swap_always(__uint128_t v) noexcept {
  auto lo = static_cast<uint64_t>(v);
  auto hi = static_cast<uint64_t>(v >> 64);
  return (static_cast<__uint128_t>(std::byteswap(lo)) << 64) |
         std::byteswap(hi);
}

inline auto swap_not_big(auto v) noexcept {
  if constexpr (std::endian::native == std::endian::big) return v;
  return swap_always(v);
}

inline auto swap_not_little(auto v) noexcept {
  if constexpr (std::endian::native == std::endian::little) return v;
  return swap_always(v);
}

inline uint16_t hton16(uint16_t v) noexcept { return swap_not_big(v); }
inline uint16_t ntoh16(uint16_t v) noexcept { return swap_not_big(v); }
inline uint32_t hton32(uint32_t v) noexcept { return swap_not_big(v); }
inline uint32_t ntoh32(uint32_t v) noexcept { return swap_not_big(v); }
inline uint64_t hton64(uint64_t v) noexcept { return swap_not_big(v); }
inline uint64_t ntoh64(uint64_t v) noexcept { return swap_not_big(v); }
inline __uint128_t hton128(__uint128_t v) noexcept { return swap_not_big(v); }
inline __uint128_t ntoh128(__uint128_t v) noexcept { return swap_not_big(v); }

}} // namespace corvid::proto
