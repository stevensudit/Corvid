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
// primary ray can take the nearer of this and the marched terrain exactly. It
// also carries the rolling motion grid's state, a roll axis and an accumulated
// scroll phase plus a `glow` level, so `shade_ball` can wrap a flat hex grid
// onto the surface that flows at the roll rate while the ball moves.
struct metal_ball {
  pos3 center;
  float radius;

  // The rolling motion grid (a flat hex grid aligned to the ball's travel, see
  // `grid_uv`): `roll_axis` is the horizontal axis the ball rolls about
  // (across the current motion), `roll_phase` the accumulated roll angle that
  // scrolls the grid forward, `steer_phase` an accumulated sideways drift that
  // fakes the turn while steering (both radians, wrapped to a grid period by
  // the rig), and `glow` the eased intensity that fades the grid in while
  // moving, out at rest.
  vec3 roll_axis{0.0F, 0.0F, 1.0F};
  float roll_phase = 0.0F;
  float steer_phase = 0.0F;
  float glow = 0.0F;

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

  // The rolling motion grid's coordinates at surface normal `n`: a Mercator
  // (conformal) cylindrical projection around `roll_axis`, which wraps a flat
  // hex grid onto the ball keeping the cells the right shape rather than
  // squishing them into slivers toward the poles. `u` is the angle around the
  // roll axis minus the scroll `roll_phase`, so the grid flows at the roll
  // rate. `v` is the conformal latitude `atanh(a)` along the roll axis
  // (constant-`v` circles are the rolling circles, so a flat grid's edges
  // along `v` stay parallel to the ground) plus `steer_phase`, the sideways
  // turn fake. Near the equator `atanh(a)` ~ the latitude angle, so the
  // equatorial cells match the simple cylindrical map; only the poles stretch.
  // `axle` is the distance toward the roll-axis pole (zero at the equator, one
  // at the ball's far sides), so the shader can fade out the cells that shrink
  // toward that singular axle. The visible band, the back of the ball the
  // trailing head sees, is the equator, where `axle` is zero.
  struct grid_sample {
    float u;
    float v;
    float axle;
  };
  [[nodiscard]] __device__ grid_sample grid_uv(vec3 n) const {
    const vec3 world_up{0.0F, 1.0F, 0.0F};
    const vec3 motion = cross(roll_axis, world_up); // the original motion dir
    const float a = fminf(fmaxf(dot(n, roll_axis), -1.0F), 1.0F);
    const float theta = atan2f(dot(n, motion), dot(n, world_up));
    // Conformal latitude, clamped off the singular axle (where `axle` fades
    // the grid out regardless).
    const float lat = atanhf(fminf(fmaxf(a, -0.9999F), 0.9999F));
    return {theta - roll_phase, lat + steer_phase, fabsf(a)};
  }
};

#pragma endregion
#pragma region saucer_head

// The saucer head: a flying-saucer SDF that carries the viewpoint. A flat
// ellipsoid body blended with a small dome on top, sized by one radius. Its
// `up` axis (the disc normal) tilts so the saucer banks with the look, a
// `front` axis (the disc nose, in the disc plane) anchors the fixed hull
// decals, and a `spin` angle turns the belly pattern. Unlike the ball it has
// no closed-form hit, so it is sphere-traced.
struct saucer_head {
  pos3 center;
  vec3 up;      // disc normal (unit); the saucer banks by tilting this
  vec3 dome_up; // dome decal normal (unit); counter-rotates against the bank
  vec3 front;   // disc nose (unit, in the disc plane); anchors hull decals
  float radius; // disc radius
  float spin;   // belly-pattern rotation angle, radians

  // Shape, as fractions of `radius` so the saucer scales as one piece. The
  // defaults are the original literals; the tuning panel edits them live. A
  // wider `dome_blend` (or a lower `dome_offset`) fills the dome/disc seam, so
  // grazing rays no longer slip through it.
  float disc_height = 0.32F; // disc half-height / radius (smaller = flatter)
  float dome_offset = 0.20F; // dome center height / radius
  float dome_radius = 0.55F; // dome sphere radius / radius
  float dome_blend = 0.007F; // dome/disc smooth-union width / radius

  // The disc's upper half is clipped to a shallow cone so the saucer reads as
  // a craft rather than a vertically symmetric blob: the cone apex height /
  // radius, with the rim pinned at the disc equator. Below `disc_height` it
  // forms a cone; at or above it the top stays the rounded ellipsoid.
  float top_height = 0.18F;

  // Smooth-intersection width / radius that rounds the otherwise sharp brim
  // where the cone meets the disc bottom, softening the staircasing on that
  // silhouette edge.
  float rim_round = 0.03F;

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

  // Beacon animation, set per frame in `avatar_rig::head` and read by
  // `shade_antenna`: `blink` is the 0..1 on/off blink waveform, `reversing` is
  // how much the Head is backing up (0..1, reddens the beacon while moving),
  // `motion` is the Head's planar speed (0..1, which gates the on/off blink
  // off at rest and blends moving versus idle color), and `idle_mix` is the
  // idle color selector (0..1) tied to the belly idle spin, so the beacon
  // alternates in tune with it at rest.
  float blink = 0.0F;
  float reversing = 0.0F;
  float motion = 0.0F;
  float idle_mix = 0.0F;

  // Caps the silhouette hit tolerance (fraction of radius) so the far-mirror
  // reflection does not fatten into a dark halo; see `raymarch`.
  float hit_cap = 0.05F;

  // The antenna rod's endpoints, relative to `center`: the base sits on the
  // dome top along `antenna_dir`, the tip one `antenna_length` further out.
  [[nodiscard]] __device__ vec3 antenna_base() const {
    return (dome_up * (radius * dome_offset)) +
           (antenna_dir * (radius * dome_radius));
  }
  [[nodiscard]] __device__ vec3 antenna_tip() const {
    return antenna_base() + (antenna_dir * (radius * antenna_length));
  }

  // Point `p` in the saucer's local frame: x and z span the disc, y runs along
  // `up`. The belly shading reads its polar coordinates from this.
  [[nodiscard]] __host__ __device__ vec3 to_local(pos3 p) const {
    const vec3 q = p - center;
    const vec3 ref =
        fabsf(up.y) < 0.99F ? vec3{0.0F, 1.0F, 0.0F} : vec3{1.0F, 0.0F, 0.0F};
    const vec3 ex = normalize(cross(ref, up));
    const vec3 ez = cross(up, ex);
    return vec3{dot(q, ex), dot(q, up), dot(q, ez)};
  }

  // Azimuth of `p` about the disc axis, measured from the hull's `front`, so a
  // decal placed by it stays bolted to the hull as the disc banks and dips.
  // `to_local` instead floats its azimuth on world up, which is fine for the
  // spinning, radially symmetric belly but slides a fixed decal whenever the
  // disc tilts. `front` is the disc nose, already a unit vector in the disc
  // plane (perpendicular to `up`), so the frame stays continuous through a
  // full nose-down dip with no projection to collapse.
  [[nodiscard]] __device__ float hull_azimuth(pos3 p) const {
    const vec3 q = p - center;
    const vec3 right = cross(up, front);
    return atan2f(dot(q, right), dot(q, front));
  }

  // The disc body and the dome sphere distances that `saucer_sdf`
  // smooth-unions, in the tilted local frame. The shader compares them to tell
  // which surface a hit belongs to, so the dome decal draws only on the dome
  // sphere (the disc occludes the rest) and the seam falls at their join.
  struct parts {
    float disc;
    float dome;
  };
  [[nodiscard]] __host__ __device__ parts parts_at(pos3 p) const {
    const vec3 ql = to_local(p);
    const float body =
        sd_ellipsoid(ql, vec3{radius, radius * disc_height, radius});
    // Clip the disc's top half to a shallow cone whose apex sits up the local
    // +y axis and whose surface passes through the rim, leaving the rounded
    // ellipsoid bottom untouched. `inv` is sin of the cone's half-angle and
    // `top_height * inv` its cos, the {sin, cos} `sd_cone` wants.
    const float inv = 1.0F / sqrtf(1.0F + (top_height * top_height));
    const float cone = sd_cone(ql - vec3{0.0F, radius * top_height, 0.0F},
        vec2{inv, top_height * inv});
    const float clipped = op_smooth_intersect(body, cone, radius * rim_round);
    // The dome sphere is centered along `dome_up`, the dome's own frame (it
    // counter-rotates against the disc's motion bank, the steadycam), so the
    // whole dome -- sphere, grid, eye, and antenna -- is one rigid piece. The
    // shader's eye decal and `antenna_base` place themselves along `dome_up`
    // too; the disc stays in the local (`up`) frame, so the two lean apart.
    const vec3 pc = p - center;
    const float dome = sd_sphere(pc - (dome_up * (radius * dome_offset)),
        radius * dome_radius);
    return {clipped, dome};
  }

  // Signed distance from `p` to the saucer body alone, disc plus dome and no
  // antenna, evaluated in the tilted local frame. Split out of `sdf` so the
  // shader can tell whether the antenna or the body is the nearer surface at a
  // hit and keep the antenna from bleeding through the body.
  [[nodiscard]] __device__ float saucer_sdf(pos3 p) const {
    const parts s = parts_at(p);
    return op_smooth_union(s.disc, s.dome, radius * dome_blend);
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
