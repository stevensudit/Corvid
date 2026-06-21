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

#include "./sdf.cuh"
#include "./vec.cuh"

// The player's avatar as a pair of free signed-distance objects the ray-march
// tests directly against the terrain, with no grid writes: the metallic ball
// body the player drives, and the saucer head that rides it and carries the
// viewpoint (and, later, the cursor and flashlight).

namespace corvid::cuda {

#pragma region metal_ball

// The metallic ball body: a single analytic sphere in world space. Because it
// is one sphere, its ray hit is closed-form rather than sphere-traced, so the
// primary ray can take the nearer of this and the marched terrain exactly.
struct metal_ball {
  pos3 center;
  float radius;

  // Distance along unit `dir` from `eye` to the nearest surface hit ahead of
  // the eye, or a negative value on a miss. The far root is returned when the
  // eye is inside the sphere, so the surface is still found from within.
  [[nodiscard]] __device__ float intersect(pos3 eye, vec3 dir) const {
    const vec3 oc = eye - center;
    const float b = dot(oc, dir);
    const float c = dot(oc, oc) - (radius * radius);
    const float disc = (b * b) - c; // `dir` is unit, so the quadratic's a == 1
    if (disc < 0.0F) return -1.0F;

    const float root = sqrtf(disc);
    const float t_near = -b - root;
    if (t_near > 0.0F) return t_near;

    const float t_far = -b + root;
    return t_far > 0.0F ? t_far : -1.0F;
  }

  // Outward unit normal at surface point `p`.
  [[nodiscard]] __device__ vec3 normal(pos3 p) const {
    return normalize(p - center);
  }
};

#pragma endregion
#pragma region saucer_head

// The saucer head: a flying-saucer SDF that carries the viewpoint. A flat
// ellipsoid body blended with a small dome on top, sized by one radius. Its
// `up` axis (the disc normal) tilts so the saucer banks with the look, and a
// `spin` angle turns the belly pattern. Unlike the ball it has no closed-form
// hit, so it is sphere-traced.
struct saucer_head {
  pos3 center;
  vec3 up;      // disc normal (unit); the saucer banks by tilting this
  float radius; // disc radius
  float spin;   // belly-pattern rotation angle, radians
  float thrust; // propulsion glow, 0 (idle) to 1 (full)

  // Point `p` in the saucer's local frame: x and z span the disc, y runs along
  // `up`. The belly shading reads its polar coordinates from this.
  [[nodiscard]] __device__ vec3 to_local(pos3 p) const {
    const vec3 q = p - center;
    const vec3 ref =
        fabsf(up.y) < 0.99F ? vec3{0.0F, 1.0F, 0.0F} : vec3{1.0F, 0.0F, 0.0F};
    const vec3 ex = normalize(cross(ref, up));
    const vec3 ez = cross(up, ex);
    return vec3{dot(q, ex), dot(q, up), dot(q, ez)};
  }

  // Signed distance from `p` to the saucer surface, evaluated in the tilted
  // local frame.
  [[nodiscard]] __device__ float sdf(pos3 p) const {
    const vec3 ql = to_local(p);
    const float body = sd_ellipsoid(ql, vec3{radius, radius * 0.32F, radius});
    const float dome =
        sd_sphere(ql - vec3{0.0F, radius * 0.20F, 0.0F}, radius * 0.55F);
    return op_smooth_union(body, dome, radius * 0.25F);
  }

  // Outward unit normal at surface point `p`, from the SDF gradient by central
  // differences.
  [[nodiscard]] __device__ vec3 normal(pos3 p) const {
    constexpr float e = 1.0e-3F;
    const vec3 dx{e, 0.0F, 0.0F};
    const vec3 dy{0.0F, e, 0.0F};
    const vec3 dz{0.0F, 0.0F, e};
    return normalize(vec3{sdf(p + dx) - sdf(p - dx), sdf(p + dy) - sdf(p - dy),
        sdf(p + dz) - sdf(p - dz)});
  }

  // Distance along unit `dir` from `eye` to the nearest surface hit, sphere-
  // traced, or a negative value on a miss. Only the ball's reflection ray
  // calls this, always from the ball surface, so the start is outside the
  // head.
  [[nodiscard]] __device__ float raymarch(pos3 eye, vec3 dir) const {
    constexpr int max_steps = 48;
    constexpr float hit_epsilon = 1.0e-3F;
    constexpr float max_dist = 64.0F;
    float dist = 0.0F;
    for (int step = 0; step < max_steps; ++step) {
      const float d = sdf(eye + (dir * dist));
      if (d < hit_epsilon) return dist;
      dist += d;
      if (dist > max_dist) break;
    }
    return -1.0F;
  }
};

#pragma endregion

} // namespace corvid::cuda
