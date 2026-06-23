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

// A flat rectangular mirror: an axis-aligned vertical wall the primary ray can
// reflect off, so the scene (and the avatar the camera rides inside) can be
// seen undistorted, unlike the convex ball. Lives in world space along one
// border of the terrain.

namespace corvid::cuda {

#pragma region flat_mirror

// A finite mirror on the plane z == `plane_z`, facing along `normal`, bounded
// to the rectangle [`lo`, `hi`] in (x, y).
//
// The shader reflects the view ray about `normal` at the hit and marches the
// world from there.
struct flat_mirror {
  float plane_z;
  vec2 lo;     // (x, y) min corner of the rectangle
  vec2 hi;     // (x, y) max corner
  vec3 normal; // unit, faces the scene (+z for a wall on the -z border)

  // Distance along unit `dir` from `eye` to the mirror within its rectangle,
  // or a negative value on a miss.
  //
  // Only the front face is hit: a ray approaching from the `normal` side.
  [[nodiscard]] __device__ float intersect(pos3 eye, vec3 dir) const {
    const float denom = dot(dir, normal);
    if (denom >= 0.0F) return -1.0F; // parallel, or hitting the back
    const float t = (plane_z - eye.v.z) / dir.z;
    if (t <= 0.0F) return -1.0F;
    const pos3 hit = eye + (dir * t);
    if (hit.v.x < lo.x || hit.v.x > hi.x || hit.v.y < lo.y || hit.v.y > hi.y)
      return -1.0F;
    return t;
  }
};

#pragma endregion

} // namespace corvid::cuda
