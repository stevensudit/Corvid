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

#include <concepts>
#include <cstdint>

#include <cuda_bf16.h>
#include <cuda_runtime.h>

#include "../meta/concepts.h"
#include "./cuda_std.cuh"

// The bfloat16 element type for device data, and the constraint that admits
// it beside the floating-point types.
//
// A `bfloat16_t` is the top half of a `float`: the sign, the 8 exponent bits,
// and 7 of the 23 mantissa bits, so it has the range of a `float` with 8
// significant bits. It is storage, not arithmetic. Convert to `float`,
// compute, and convert the result back, on the host or in a kernel:
//
//   const bfloat16_t x{1.5F};
//   const bfloat16_t y{static_cast<float>(x) * 2};
//
// It stands in for the standard's `std::bfloat16_t` until every standard
// library ships one.
namespace corvid::cuda {

#pragma region bfloat16_t

// A 16-bit brain floating-point value, the toolkit's `__nv_bfloat16` behind
// explicit conversions.
//
// Construction from a `float` rounds to nearest even. The comparisons are
// those of the values as floats. It is trivially copyable, and default
// construction leaves it uninitialized, as with `float`.
class bfloat16_t {
public:
  bfloat16_t() = default;
  explicit __host__ __device__ bfloat16_t(float value)
      : raw_(__float2bfloat16(value)) {}

  // The value with the bit pattern `bits`, for a constant that a `float`
  // would round.
  [[nodiscard]] static constexpr __host__ __device__ bfloat16_t from_bits(
      uint16_t bits) noexcept {
    return bfloat16_t{__nv_bfloat16_raw{bits}};
  }

  [[nodiscard]] __host__ __device__ uint16_t bits() const noexcept {
    return static_cast<__nv_bfloat16_raw>(raw_).x;
  }

  [[nodiscard]] explicit __host__ __device__ operator float() const noexcept {
    return __bfloat162float(raw_);
  }

  [[nodiscard]] __host__ __device__ friend bool
  operator==(bfloat16_t a, bfloat16_t b) noexcept {
    return (static_cast<float>(a) == static_cast<float>(b));
  }
  [[nodiscard]] __host__ __device__ friend bool
  operator<(bfloat16_t a, bfloat16_t b) noexcept {
    return (static_cast<float>(a) < static_cast<float>(b));
  }

private:
  constexpr __host__ __device__ explicit bfloat16_t(
      __nv_bfloat16_raw raw) noexcept
      : raw_(raw) {}

  __nv_bfloat16 raw_;
};

// `T` must be a floating-point type or `bfloat16_t`, the element types the
// device ops compute over.
template<typename T>
concept DeviceFloating = Floating<T> || std::same_as<T, bfloat16_t>;

#pragma endregion

} // namespace corvid::cuda

#pragma region numeric_limits

// The limits of `bfloat16_t`, from its bit patterns, so that the device
// reductions and kernels find them where they find a `float`'s.
template<>
class cuda::std::numeric_limits<corvid::cuda::bfloat16_t> {
  using T = corvid::cuda::bfloat16_t;

public:
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = false;
  static constexpr bool is_exact = false;
  static constexpr bool has_infinity = true;
  static constexpr bool has_quiet_NaN = true;
  static constexpr int digits = 8;
  static constexpr int radix = 2;

  [[nodiscard]] static constexpr __host__ __device__ T min() noexcept {
    return T::from_bits(0x0080);
  }
  [[nodiscard]] static constexpr __host__ __device__ T max() noexcept {
    return T::from_bits(0x7F7F);
  }
  [[nodiscard]] static constexpr __host__ __device__ T lowest() noexcept {
    return T::from_bits(0xFF7F);
  }
  [[nodiscard]] static constexpr __host__ __device__ T epsilon() noexcept {
    return T::from_bits(0x3C00);
  }
  [[nodiscard]] static constexpr __host__ __device__ T infinity() noexcept {
    return T::from_bits(0x7F80);
  }
  [[nodiscard]] static constexpr __host__ __device__ T quiet_NaN() noexcept {
    return T::from_bits(0x7FC0);
  }
  [[nodiscard]] static constexpr __host__ __device__ T denorm_min() noexcept {
    return T::from_bits(0x0001);
  }
};

#pragma endregion
