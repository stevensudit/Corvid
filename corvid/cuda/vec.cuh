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
#include <limits>

#include "./radians.cuh"

// Small vector and position math for CUDA ray and shading code.
//
// `vec2`/`vec3` are vectors (directions, displacements, sizes); `pos2`/`pos3`
// are positions (points). Keeping them distinct lets the type system enforce
// the affine algebra: a point plus a displacement is a point, the difference
// of two points is a displacement, and adding two points is meaningless.
//
// All operators and helpers are hidden friends, found by argument-dependent
// lookup, so they add nothing at namespace scope (which would hide the
// enclosing-namespace bitmask-enum operators from unqualified lookup here).

namespace corvid::cuda {

// A large finite sentinel: the biggest finite float, for seeding a running
// minimum (the first real value always beats it) or marking a distance as off
// the scale (no surface within reach). Deliberately finite, not infinity, so
// it stays valid if a translation unit is later built with fast math (which
// assumes no inf/NaN) and never yields a NaN under arithmetic.
constexpr float big_value = std::numeric_limits<float>::max();

#pragma region vec2

// A two-component float vector, usable from host and device code.
struct vec2 {
  float x;
  float y;

  friend __host__ __device__ vec2 operator+(vec2 a, vec2 b) {
    return {a.x + b.x, a.y + b.y};
  }

  friend __host__ __device__ vec2 operator-(vec2 a, vec2 b) {
    return {a.x - b.x, a.y - b.y};
  }

  friend __host__ __device__ vec2 operator-(vec2 a) { return {-a.x, -a.y}; }

  friend __host__ __device__ vec2 operator*(vec2 a, float s) {
    return {a.x * s, a.y * s};
  }

  friend __host__ __device__ vec2 operator*(float s, vec2 a) {
    return {a.x * s, a.y * s};
  }

  friend __host__ __device__ vec2 operator/(vec2 a, float s) {
    return {a.x / s, a.y / s};
  }

  // Component-wise product.
  friend __host__ __device__ vec2 operator*(vec2 a, vec2 b) {
    return {a.x * b.x, a.y * b.y};
  }

  // Compound assignment, in terms of the binary operators above.
  friend __host__ __device__ vec2& operator+=(vec2& a, vec2 b) {
    return a = a + b;
  }
  friend __host__ __device__ vec2& operator-=(vec2& a, vec2 b) {
    return a = a - b;
  }
  friend __host__ __device__ vec2& operator*=(vec2& a, float s) {
    return a = a * s;
  }
  friend __host__ __device__ vec2& operator*=(vec2& a, vec2 b) {
    return a = a * b;
  }
  friend __host__ __device__ vec2& operator/=(vec2& a, float s) {
    return a = a / s;
  }

  [[nodiscard]] friend __host__ __device__ float dot(vec2 a, vec2 b) {
    return (a.x * b.x) + (a.y * b.y);
  }

  // The 2D cross product (perp-dot): the z of the 3D cross, a signed area.
  [[nodiscard]] friend __host__ __device__ float cross(vec2 a, vec2 b) {
    return (a.x * b.y) - (a.y * b.x);
  }

  [[nodiscard]] friend __host__ __device__ float length(vec2 a) {
    return sqrtf(dot(a, a));
  }

  // Unit vector along `a`. A zero-length `a` returns the zero vector rather
  // than dividing by zero, so a degenerate input stays finite instead of NaN.
  [[nodiscard]] friend __host__ __device__ vec2 normalize(vec2 a) {
    const float len = length(a);
    return len > 0.0F ? a / len : a;
  }
};

#pragma endregion
#pragma region vec3

// A three-component float vector, usable from host and device code.
struct vec3 {
  float x;
  float y;
  float z;

  // World-axis unit vectors, for a y-up, -z-forward convention: `+x` is
  // `right`, `+y` is `up`, `+z` is `back`. `forward` (`-z`) is omitted until
  // something needs it. Defined out of class (a static member cannot name its
  // own incomplete type in an in-class initializer).
  static const vec3 right, up, back;

  friend __host__ __device__ vec3 operator+(vec3 a, vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
  }

  friend __host__ __device__ vec3 operator-(vec3 a, vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
  }

  friend __host__ __device__ vec3 operator-(vec3 a) {
    return {-a.x, -a.y, -a.z};
  }

  friend __host__ __device__ vec3 operator*(vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
  }

  friend __host__ __device__ vec3 operator*(float s, vec3 a) {
    return {a.x * s, a.y * s, a.z * s};
  }

  friend __host__ __device__ vec3 operator/(vec3 a, float s) {
    return {a.x / s, a.y / s, a.z / s};
  }

  // Component-wise product, for modulating one color or scale by another.
  friend __host__ __device__ vec3 operator*(vec3 a, vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
  }

  // Compound assignment, in terms of the binary operators above.
  friend __host__ __device__ vec3& operator+=(vec3& a, vec3 b) {
    return a = a + b;
  }
  friend __host__ __device__ vec3& operator-=(vec3& a, vec3 b) {
    return a = a - b;
  }
  friend __host__ __device__ vec3& operator*=(vec3& a, float s) {
    return a = a * s;
  }
  friend __host__ __device__ vec3& operator*=(vec3& a, vec3 b) {
    return a = a * b;
  }
  friend __host__ __device__ vec3& operator/=(vec3& a, float s) {
    return a = a / s;
  }

  [[nodiscard]] friend __host__ __device__ float dot(vec3 a, vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
  }

  [[nodiscard]] friend __host__ __device__ vec3 cross(vec3 a, vec3 b) {
    return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x)};
  }

  [[nodiscard]] friend __host__ __device__ float length(vec3 a) {
    return sqrtf(dot(a, a));
  }

  // Unit vector along `a`. A zero-length `a` returns the zero vector rather
  // than dividing by zero, so a degenerate input stays finite instead of NaN.
  [[nodiscard]] friend __host__ __device__ vec3 normalize(vec3 a) {
    const float len = length(a);
    return len > 0.0F ? a / len : a;
  }

  // Reflect incident direction `d` about unit normal `n`.
  [[nodiscard]] friend __host__ __device__ vec3 reflect(vec3 d, vec3 n) {
    return d - (n * (2.0F * dot(d, n)));
  }

  // Refract incident direction `d` (unit) through a surface with unit normal
  // `n` oriented against `d` (so `dot(d, n) < 0`), for the relative index
  // `eta` = n_incident / n_transmitted. Returns the zero vector on total
  // internal reflection, so the caller can fall back to `reflect`.
  [[nodiscard]] friend __host__ __device__ vec3 refract(vec3 d, vec3 n,
      float eta) {
    const float ci = dot(d, n);
    const float k = 1.0F - (eta * eta * (1.0F - (ci * ci)));
    if (k < 0.0F) return vec3{};
    return (d * eta) - (n * ((eta * ci) + sqrtf(k)));
  }

  // The component of `v` perpendicular to unit `n` (the vector rejection of
  // `v` from `n`): `v` with its projection onto `n` removed, flattening it
  // onto the plane through the origin with normal `n`.
  [[nodiscard]] friend __host__ __device__ vec3 reject(vec3 v, vec3 n) {
    return v - (n * dot(v, n));
  }

  // Rotate `v` about unit `axis` by `angle` (Rodrigues' rotation formula).
  [[nodiscard]] friend __host__ __device__ vec3 rotate_about(vec3 v, vec3 axis,
      radians angle) {
    const float c = cos(angle);
    const float s = sin(angle);
    return (v * c) + (cross(axis, v) * s) +
           (axis * (dot(axis, v) * (1.0F - c)));
  }
};

inline constexpr vec3 vec3::right{1.0F, 0.0F, 0.0F};
inline constexpr vec3 vec3::up{0.0F, 1.0F, 0.0F};
inline constexpr vec3 vec3::back{0.0F, 0.0F, 1.0F};

#pragma endregion
#pragma region pos2

// A position in 2D space, e.g. a pixel coordinate.
//
// Distinct from a vec2 (a direction or size) so the two are never silently
// interchanged. The operators are the affine ones: translate a point by a
// displacement, and subtract two points for the displacement between them.
// There is deliberately no `pos2 + pos2`.
struct pos2 {
  vec2 v;

  friend __host__ __device__ pos2 operator+(pos2 a, vec2 d) {
    return {a.v + d};
  }

  friend __host__ __device__ pos2 operator+(vec2 d, pos2 a) {
    return {a.v + d};
  }

  friend __host__ __device__ pos2 operator-(pos2 a, vec2 d) {
    return {a.v - d};
  }

  friend __host__ __device__ vec2 operator-(pos2 a, pos2 b) {
    return a.v - b.v;
  }

  // Translate a point in place by a displacement.
  friend __host__ __device__ pos2& operator+=(pos2& a, vec2 d) {
    return a = a + d;
  }
  friend __host__ __device__ pos2& operator-=(pos2& a, vec2 d) {
    return a = a - d;
  }

  [[nodiscard]] friend __host__ __device__ float distance(pos2 a, pos2 b) {
    return length(a.v - b.v);
  }
};

#pragma endregion
#pragma region pos3

// A position in 3D space. Distinct from a vec3 (a direction or displacement)
// so a point and a vector are never silently interchanged.
//
// The operators are the affine ones: translate a point by a displacement, and
// subtract two points for the displacement between them. There is deliberately
// no `pos3 + pos3`.
struct pos3 {
  vec3 v;

  friend __host__ __device__ pos3 operator+(pos3 a, vec3 d) {
    return {a.v + d};
  }

  friend __host__ __device__ pos3 operator+(vec3 d, pos3 a) {
    return {a.v + d};
  }

  friend __host__ __device__ pos3 operator-(pos3 a, vec3 d) {
    return {a.v - d};
  }

  friend __host__ __device__ vec3 operator-(pos3 a, pos3 b) {
    return a.v - b.v;
  }

  // Translate a point in place by a displacement.
  friend __host__ __device__ pos3& operator+=(pos3& a, vec3 d) {
    return a = a + d;
  }
  friend __host__ __device__ pos3& operator-=(pos3& a, vec3 d) {
    return a = a - d;
  }

  [[nodiscard]] friend __host__ __device__ float distance(pos3 a, pos3 b) {
    return length(a.v - b.v);
  }
};

#pragma endregion

} // namespace corvid::cuda
