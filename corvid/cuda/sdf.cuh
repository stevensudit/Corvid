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

#include "./vec.cuh"

// Signed distance field primitives and combinators for ray marching.
//
// Each returns the signed distance from a point to the surface: negative
// inside, zero on it, positive outside. A scene is built by combining these,
// then sphere-traced.

namespace corvid::cuda {

#pragma region Primitives

// Distance from `p` to a sphere of radius `r` centered at the origin.
[[nodiscard]] __device__ inline float sd_sphere(vec3 p, float r) {
  return length(p) - r;
}

// Distance from `p` to an axis-aligned ellipsoid of radii `r` centered at the
// origin. The result is a bounded approximation (exact distance to a general
// ellipsoid has no closed form), accurate enough for sphere tracing.
[[nodiscard]] __device__ inline float sd_ellipsoid(vec3 p, vec3 r) {
  const vec3 pr{p.x / r.x, p.y / r.y, p.z / r.z};
  const float k0 = length(pr);
  if (k0 == 0.0F) return -fminf(r.x, fminf(r.y, r.z));
  const vec3 pr2{p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z)};
  const float k1 = length(pr2);
  return k0 * (k0 - 1.0F) / k1;
}

// Distance from `p` to an axis-aligned box of half-extents `b`.
[[nodiscard]] __device__ inline float sd_box(vec3 p, vec3 b) {
  const vec3 d{fabsf(p.x) - b.x, fabsf(p.y) - b.y, fabsf(p.z) - b.z};
  const vec3 outside{fmaxf(d.x, 0.0F), fmaxf(d.y, 0.0F), fmaxf(d.z, 0.0F)};
  return length(outside) + fminf(fmaxf(d.x, fmaxf(d.y, d.z)), 0.0F);
}

// Distance from `p` to a capsule: the segment from `a` to `b` swept by radius
// `r` (a cylinder with hemispherical end caps). `a` and `b` must be distinct.
[[nodiscard]] __device__ inline float
sd_capsule(vec3 p, vec3 a, vec3 b, float r) {
  const vec3 pa = p - a;
  const vec3 ba = b - a;
  const float h = __saturatef(dot(pa, ba) / dot(ba, ba));
  return length(pa - (ba * h)) - r;
}

// Distance from `p` to a plane with unit normal `n` whose surface passes at
// offset `h` along that normal.
[[nodiscard]] __device__ inline float sd_plane(vec3 p, vec3 n, float h) {
  return dot(p, n) + h;
}

// Distance from `p` to an infinite cone whose apex is at the origin and whose
// solid opens downward along -y, its lateral surface at the half-angle from
// that axis given by `sin_cos` = {sin, cos} of the angle.
//
// Negative below the apex inside the cone, positive outside; exact on the
// lateral surface below the apex and a conservative underestimate above it, so
// it is safe to sphere-trace and to intersect with a bounded solid.
[[nodiscard]] __device__ inline float sd_cone(vec3 p, vec2 sin_cos) {
  const float radial =
      sqrtf((p.x * p.x) + (p.z * p.z)); // distance from -y axis
  const float depth = -p.y;             // distance down the axis from the apex
  return (radial * sin_cos.y) - (depth * sin_cos.x);
}

#pragma endregion
#pragma region Combinators

// Union: the nearer of two surfaces.
[[nodiscard]] __device__ inline float op_union(float a, float b) {
  return fminf(a, b);
}

// Intersection: the region common to both.
[[nodiscard]] __device__ inline float op_intersect(float a, float b) {
  return fmaxf(a, b);
}

// Subtraction: `a` with `b` carved out of it.
[[nodiscard]] __device__ inline float op_subtract(float a, float b) {
  return fmaxf(a, -b);
}

// Union with a smooth blend of width `k` where the two surfaces meet.
[[nodiscard]] __device__ inline float
op_smooth_union(float a, float b, float k) {
  const float h = fmaxf(k - fabsf(a - b), 0.0F) / k;
  return fminf(a, b) - (h * h * k * 0.25F);
}

// Intersection with a smooth blend of width `k` where the two surfaces meet,
// the mirror of `op_smooth_union` for rounding an otherwise sharp intersection
// edge. `k` must be positive.
[[nodiscard]] __device__ inline float
op_smooth_intersect(float a, float b, float k) {
  const float h = fmaxf(k - fabsf(a - b), 0.0F) / k;
  return fmaxf(a, b) + (h * h * k * 0.25F);
}

#pragma endregion

} // namespace corvid::cuda
