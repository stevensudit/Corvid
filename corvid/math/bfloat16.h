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
#include <compare>
#include <cstdint>

#include "../meta/crossplatform.h"

// The bfloat16 storage type, shared by host code and device code.
//
// A `bfloat16_t` is the top half of a `float`, retaining the sign, the 8
// exponent bits, and 7 of the 23 mantissa bits. As a result, it has the range
// of a `float` but with only 8 significant bits.
//
// It is storage, not arithmetic. Convert to `float`, compute, and convert the
// result back:
//
//   const bfloat16_t x{1.5F};
//   const bfloat16_t y{static_cast<float>(x) * 2};
//
// It stands in for the standard's `std::bfloat16_t` until every standard
// library ships one. The conversions are integer arithmetic on the bit
// patterns, callable from a kernel as well as from the host.
namespace corvid { inline namespace math { inline namespace floating {

#pragma region bfloat16_t

// A 16-bit brain floating-point value.
//
// Construction from a `float` rounds to nearest even, and every NaN becomes
// the one quiet NaN 0x7FFF, as the CUDA toolkit's conversion has it. The
// comparisons are those of the values as floats. It is trivially copyable,
// and default construction leaves it uninitialized, as with `float`.
class bfloat16_t {
public:
  bfloat16_t() = default;
  explicit constexpr CUDA_HOST_DEVICE bfloat16_t(float value) noexcept
      : bits_(bits_of(value)) {}

  // The value with the bit pattern `bits`, for a constant that a `float`
  // would round.
  [[nodiscard]] static constexpr CUDA_HOST_DEVICE bfloat16_t from_bits(
      uint16_t bits) noexcept {
    return bfloat16_t{raw{}, bits};
  }

  [[nodiscard]] constexpr CUDA_HOST_DEVICE uint16_t bits() const noexcept {
    return bits_;
  }

  [[nodiscard]] explicit constexpr CUDA_HOST_DEVICE
  operator float() const noexcept {
    return float_of(bits_);
  }

  [[nodiscard]] constexpr CUDA_HOST_DEVICE friend bool
  operator==(bfloat16_t a, bfloat16_t b) noexcept {
    return (static_cast<float>(a) == static_cast<float>(b));
  }
  [[nodiscard]] constexpr CUDA_HOST_DEVICE friend std::partial_ordering
  operator<=>(bfloat16_t a, bfloat16_t b) noexcept {
    return static_cast<float>(a) <=> static_cast<float>(b);
  }

private:
  struct raw {};

  constexpr CUDA_HOST_DEVICE bfloat16_t(raw, uint16_t bits) noexcept
      : bits_(bits) {}

  // The bit pattern of `value` rounded to nearest even.
  [[nodiscard]] static constexpr CUDA_HOST_DEVICE uint16_t bits_of(
      float value) noexcept {
    const auto pattern = std::bit_cast<uint32_t>(value);
    // A NaN is canonicalized, since adding the rounding bias could carry it
    // into an infinity.
    if ((pattern & 0x7FFF'FFFFU) > 0x7F80'0000U) return 0x7FFF;
    // Adding half an ulp less one, plus the kept half's low bit, rounds the
    // cut half to nearest and breaks a tie toward the even pattern.
    const auto bias = 0x7FFFU + ((pattern >> 16) & 1U);
    return static_cast<uint16_t>((pattern + bias) >> 16);
  }

  // The `float` whose top half is `bits`.
  [[nodiscard]] static constexpr CUDA_HOST_DEVICE float float_of(
      uint16_t bits) noexcept {
    return std::bit_cast<float>(static_cast<uint32_t>(bits) << 16);
  }

  uint16_t bits_;
};

#pragma endregion

}}} // namespace corvid::math::floating
