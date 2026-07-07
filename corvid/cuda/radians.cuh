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

#include <cmath>
#include <compare>
#include <numbers>

// Angle math for CUDA camera and shading: `radians` is the canonical unit and
// carries the trig; the `_deg` literal is an input convenience that converts
// degrees to it.

namespace corvid::cuda {

#pragma region radians

// An angle in radians.
//
// The operators and trig are hidden friends, found by argument-dependent
// lookup, so they add nothing at namespace scope.
struct radians {
  float value;

  static constexpr float per_degree = std::numbers::pi_v<float> / 180.0F;

  // Angles form an additive group: they add, subtract, negate, and scale by a
  // plain factor. There is deliberately no `radians * radians`.
  friend __host__ __device__ radians operator+(radians a, radians b) {
    return {a.value + b.value};
  }
  friend __host__ __device__ radians operator-(radians a, radians b) {
    return {a.value - b.value};
  }
  friend __host__ __device__ radians operator-(radians a) {
    return {-a.value};
  }
  friend __host__ __device__ radians operator*(radians a, float s) {
    return {a.value * s};
  }
  friend __host__ __device__ radians operator*(float s, radians a) {
    return {a.value * s};
  }
  friend __host__ __device__ radians operator/(radians a, float s) {
    return {a.value / s};
  }

  // Compound assignment, in terms of the binary operators above.
  friend __host__ __device__ radians& operator+=(radians& a, radians b) {
    return a = a + b;
  }
  friend __host__ __device__ radians& operator-=(radians& a, radians b) {
    return a = a - b;
  }
  friend __host__ __device__ radians& operator*=(radians& a, float s) {
    return a = a * s;
  }
  friend __host__ __device__ radians& operator/=(radians& a, float s) {
    return a = a / s;
  }

  // Ordering and equality, for clamping and comparison.
  friend __host__ __device__ bool operator==(radians a, radians b) = default;
  friend __host__ __device__ auto operator<=>(radians a, radians b) = default;

  // Trig: consume an angle, produce a plain ratio.
  [[nodiscard]] friend __host__ __device__ float sin(radians a) {
    return sinf(a.value);
  }
  [[nodiscard]] friend __host__ __device__ float cos(radians a) {
    return cosf(a.value);
  }
  [[nodiscard]] friend __host__ __device__ float tan(radians a) {
    return tanf(a.value);
  }
};

// Inverse trig: produce an angle from a plain ratio. They take a float, so
// they cannot be hidden friends of `radians` (argument-dependent lookup would
// not find them); the `radians_` prefix keeps them from colliding with the
// `std` trig names at namespace scope.
[[nodiscard]] __host__ __device__ inline radians radians_asin(float s) {
  return {asinf(s)};
}
[[nodiscard]] __host__ __device__ inline radians radians_acos(float s) {
  return {acosf(s)};
}
[[nodiscard]] __host__ __device__ inline radians radians_atan(float s) {
  return {atanf(s)};
}
[[nodiscard]] __host__ __device__ inline radians
radians_atan2(float y, float x) {
  return {atan2f(y, x)};
}

#pragma endregion
#pragma region literals

// Angle literals: `1.5_rad` is that many radians; `90_deg` (or `90.0_deg`) is
// that many degrees, converted to the canonical radians. Both integer and
// floating forms are supported.
[[nodiscard]] __host__ __device__ constexpr radians operator""_rad(
    long double v) {
  return {static_cast<float>(v)};
}
[[nodiscard]] __host__ __device__ constexpr radians operator""_rad(
    unsigned long long v) {
  return {static_cast<float>(v)};
}

[[nodiscard]] __host__ __device__ constexpr radians operator""_deg(
    long double v) {
  return {static_cast<float>(v) * radians::per_degree};
}
[[nodiscard]] __host__ __device__ constexpr radians operator""_deg(
    unsigned long long v) {
  return {static_cast<float>(v) * radians::per_degree};
}

#pragma endregion

} // namespace corvid::cuda
