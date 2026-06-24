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

#include "../../sdf.cuh"
#include "../../vec.cuh"

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
// `up` axis (the disc normal) tilts so the saucer banks with the look, a
// `front` axis orients the cockpit eyes, and a `spin` angle turns the belly
// pattern. Unlike the ball it has no closed-form hit, so it is sphere-traced.
struct saucer_head {
  pos3 center;
  vec3 up;      // disc normal (unit); the saucer banks by tilting this
  vec3 front;   // forward direction (unit); orients the cockpit eyes
  float radius; // disc radius
  float spin;   // belly-pattern rotation angle, radians

  // Shape, as fractions of `radius` so the saucer scales as one piece. The
  // defaults are the original literals; the tuning panel edits them live. A
  // wider `dome_blend` (or a lower `dome_offset`) fills the dome/disc seam, so
  // grazing rays no longer slip through it.
  float body_height = 0.32F; // disc half-height / radius (smaller = flatter)
  float dome_offset = 0.20F; // dome center height / radius
  float dome_radius = 0.55F; // dome sphere radius / radius
  float dome_blend = 0.007F; // dome/disc smooth-union width / radius

  // The disc's upper half is clipped to a shallow cone so the saucer reads as
  // a craft rather than a vertically symmetric blob: the cone apex height /
  // radius, with the rim pinned at the disc equator. Below `body_height` it
  // forms a cone; at or above it the top stays the rounded ellipsoid.
  float top_height = 0.18F;

  // Smooth-intersection width / radius that rounds the otherwise sharp brim
  // where the cone meets the disc bottom, softening the staircasing on that
  // silhouette edge.
  float rim_round = 0.03F;

  // Per-frame offset added to the cockpit eye's `eye_forward` lean, swinging
  // the eye against the saucer's motion-driven nose tilt so the orb reads as a
  // stabilized gimbal. Zero leaves the eye at its tuned placement.
  float eye_counter_offset = 0.0F;

  // The cockpit eye's placement direction (unit), computed per frame in
  // `camera_rig::head` and shared by the shader's eye decal and the antenna.
  vec3 eye_dir{0.0F, 1.0F, 0.0F};

  // Antenna: a thin rod with a ball tip standing off the dome top along
  // `antenna_dir`, a unit world-space direction set per frame to wag with the
  // eye's gimbal so it reads as an exaggerated tilt signal. Lengths are
  // fractions of `radius`; `antenna_length` 0 disables it.
  vec3 antenna_dir{0.0F, 1.0F, 0.0F};
  float antenna_length = 0.0F;    // rod length / radius (0 disables)
  float antenna_thickness = 0.0F; // rod radius / radius
  float antenna_ball = 0.0F;      // tip ball radius / radius
  float antenna_collar = 0.04F;   // base collar (shading detect) / radius

  // Caps the silhouette hit tolerance (fraction of radius) so the far-mirror
  // reflection does not fatten into a dark halo; see `raymarch`.
  float hit_cap = 0.05F;

  // The antenna rod's endpoints, relative to `center`: the base sits on the
  // dome top along `antenna_dir`, the tip one `antenna_length` further out.
  [[nodiscard]] __device__ vec3 antenna_base() const {
    return (up * (radius * dome_offset)) +
           (antenna_dir * (radius * dome_radius));
  }
  [[nodiscard]] __device__ vec3 antenna_tip() const {
    return antenna_base() + (antenna_dir * (radius * antenna_length));
  }

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

  // Signed distance from `p` to the saucer body alone, disc plus dome and no
  // antenna, evaluated in the tilted local frame. Split out of `sdf` so the
  // shader can tell whether the antenna or the body is the nearer surface at a
  // hit and keep the antenna from bleeding through the body.
  [[nodiscard]] __device__ float saucer_sdf(pos3 p) const {
    const vec3 ql = to_local(p);
    const float body =
        sd_ellipsoid(ql, vec3{radius, radius * body_height, radius});
    // Clip the disc's top half to a shallow cone whose apex sits up the local
    // +y axis and whose surface passes through the rim, leaving the rounded
    // ellipsoid bottom untouched. `inv` is sin of the cone's half-angle and
    // `top_height * inv` its cos, the {sin, cos} `sd_cone` wants.
    const float inv = 1.0F / sqrtf(1.0F + (top_height * top_height));
    const float cone = sd_cone(ql - vec3{0.0F, radius * top_height, 0.0F},
        vec2{inv, top_height * inv});
    const float clipped = op_smooth_intersect(body, cone, radius * rim_round);
    const float dome = sd_sphere(ql - vec3{0.0F, radius * dome_offset, 0.0F},
        radius * dome_radius);
    return op_smooth_union(clipped, dome, radius * dome_blend);
  }

  // Signed distance from `p` to the saucer surface, evaluated in the tilted
  // local frame.
  [[nodiscard]] __device__ float sdf(pos3 p) const {
    const float saucer = saucer_sdf(p);
    if (antenna_length <= 0.0F) return saucer;
    // The antenna is a separate protrusion in world-relative coordinates (its
    // direction tilts with the gimbal, off the local frame), hard-unioned on.
    const vec3 pc = p - center;
    const vec3 base = antenna_base();
    const vec3 tip = antenna_tip();
    const float rod = sd_capsule(pc, base, tip, radius * antenna_thickness);
    const float ball = sd_sphere(pc - tip, radius * antenna_ball);
    return op_union(saucer, op_union(rod, ball));
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
  // traced, or a negative value on a miss. Called by the ball's reflection ray
  // (from just off the head) and by the flat mirror's world ray (from across
  // the scene), so `max_dist` spans the whole world: a too-short cap hid the
  // head in a distant mirror until the camera closed within range.
  [[nodiscard]] __device__ float raymarch(pos3 eye, vec3 dir) const {
    constexpr int max_steps = 96;
    constexpr float max_dist = 512.0F;
    // Accept a hit within a tolerance that grows with distance (a cone around
    // the ray) rather than a single tight threshold. `sdf` is not an exact
    // distance (the flat `sd_ellipsoid` approximates; `op_smooth_union` is
    // non-Lipschitz in the blend), so a ray grazing the concave dome/disc seam
    // stays just above a fixed threshold and never registers, leaving a "sky
    // line" there even though the surface is continuous. The growing tolerance
    // closes that seam; at the saucer's reflected size the silhouette rounding
    // is sub-pixel.
    constexpr float hit_base = 1.0e-3F;
    constexpr float hit_slope = 1.0e-2F;
    // Cap the growing tolerance at a small fraction of the radius. The slope
    // closes the grazing dome/disc seam at the near (ball-reflection) range,
    // but uncapped it balloons at far (flat-mirror) range into a fat, dark
    // silhouette halo; the cap holds the near behavior while tightening the
    // far.
    const float hit_max = radius * hit_cap;
    float dist = 0.0F;
    for (int step = 0; step < max_steps; ++step) {
      const float d = sdf(eye + (dir * dist));
      if (d < fminf(hit_base + (hit_slope * dist), hit_max)) return dist;
      dist += d;
      if (dist > max_dist) break;
    }
    return -1.0F;
  }
};

#pragma endregion

} // namespace corvid::cuda
