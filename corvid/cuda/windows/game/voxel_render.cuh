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
#include <numbers>

#include <cuda_runtime.h>

#include "../../../math/arithmetic.h"
#include "../../density_field.cuh"
#include "../../vec.cuh"
#include "./render_config.cuh"

// Per-ray rendering for the voxel viewer: the sky backdrop, the lit terrain
// sampled from the density field and the filtered color grid, and a 2D hexagon
// SDF. These read the viewer's `render_config`, so they live with the rest of
// the game viewer; the avatar and mirror compositing that builds on them is in
// scene_render.cuh.

namespace corvid::cuda {

#pragma region Rendering

// Sky color along `ray_dir`: a deep zenith easing to a bright, pale horizon,
// plus a warm sun glow toward the light direction (a tight core in a softer
// halo). The sun direction matches the terrain light, so the brightest sky
// sits where the ground is lit.
[[nodiscard]] __device__ inline vec3
sky_color(const render_config& cfg, vec3 ray_dir) {
  const render_config::sky_params& sky = cfg.sky;
  const float up = fmaxf(ray_dir.y, 0.0F);
  const float t = powf(up, sky.gradient_bias);
  vec3 col = (sky.zenith * t) + (sky.horizon * (1.0F - t));

  // Night: a dark sky with no sun, just a faint trace of the gradient.
  if (cfg.night) return col * 0.03F;

  const vec3 sun_dir = normalize(cfg.sun_direction);
  const float s = fmaxf(dot(ray_dir, sun_dir), 0.0F);
  col += sky.halo_color * (sky.halo_strength * powf(s, sky.halo_exponent));
  col += sky.core_color * powf(s, sky.core_exponent);
  return col;
}

// A sphere that occludes the flashlight: the ball, so it casts a shadow on the
// terrain instead of letting the beam pass through it. A non-positive `radius`
// disables the test, which the reflection paths (the ball does not shadow what
// it mirrors) pass by default.
struct shadow_sphere {
  pos3 center{};
  float radius = -1.0F;
};

// The flashlight's spot factor at `hit_point`: the soft cone (0 outside it,
// easing to 1 inside the inner half-angle), gated by the range and the
// `shadow` occlusion of the segment to the lamp. Returns 0 when the lamp is
// off, the point is out of cone or range, or the shadow sphere blocks it.
// Writes the unit surface-to-lamp direction and the distance for the caller's
// own falloff. Shared by the terrain light and the ball's glossy highlight.
[[nodiscard]] __device__ inline float
flashlight_spot(const render_config::flashlight_params& fl, pos3 hit_point,
    shadow_sphere shadow, vec3& to_lamp_dir, float& dist) {
  if (!fl.enabled) return 0.0F;
  const vec3 to_lamp = fl.origin - hit_point;
  dist = length(to_lamp);
  // Below this the lamp sits on the lit point; skip to avoid a zero-length
  // direction (and the divide-by-zero normalizing it).
  constexpr float min_light_dist = 1.0e-4F;
  if (dist < min_light_dist || dist > fl.range) return 0.0F;
  to_lamp_dir = to_lamp * (1.0F / dist);

  // Soft cone: full inside the inner half-angle, easing to zero at the outer.
  constexpr float deg_to_rad = std::numbers::pi_v<float> / 180.0F;
  const float cosang = dot(to_lamp_dir * -1.0F, normalize(fl.direction));
  const float cos_outer = cosf(fl.cone_degrees * deg_to_rad);
  const float cos_inner =
      cosf(fl.cone_degrees * (1.0F - fl.softness) * deg_to_rad);
  float cone = __saturatef((cosang - cos_outer) / (cos_inner - cos_outer));
  cone = cone * cone * (3.0F - (2.0F * cone)); // smoothstep
  if (cone <= 0.0F) return 0.0F;

  // Shadow: an occluder sphere between the surface and the lamp blocks the
  // beam, with a soft penumbra so the edge fades over a band (and so the
  // shadow shows around the ball, which hides the hard umbra directly behind
  // it). The test is on the ray's closest approach to the sphere center, not a
  // hard hit: full shadow within the radius, easing to lit across
  // `shadow_softness` x radius beyond it.
  float shadow_factor = 1.0F;
  if (shadow.radius > 0.0F) {
    const vec3 oc = hit_point - shadow.center;
    const float b = dot(oc, to_lamp_dir);
    const float tc = -b; // closest-approach distance along the segment
    if (tc > 1.0e-3F && tc < dist) {
      const float d_perp = sqrtf(fmaxf(dot(oc, oc) - (b * b), 0.0F));
      // The penumbra straddles the radius: full shadow within (radius -
      // penumbra) and lit at (radius + penumbra), so the soft band both fans
      // out past the ball and fans in to shrink the umbra, instead of leaving
      // a hard full-size umbra with only an outer fringe.
      // Floor so the penumbra (and the smoothstep divisor below) stays
      // positive when `shadow_softness` is zero.
      constexpr float denom_floor = 1.0e-4F;
      const float penumbra =
          fmaxf(shadow.radius * fl.shadow_softness, denom_floor);
      const float inner = fmaxf(shadow.radius - penumbra, 0.0F);
      const float outer = shadow.radius + penumbra;
      const float s = __saturatef((d_perp - inner) / (outer - inner));
      shadow_factor =
          s * s * (3.0F - (2.0F * s)); // smoothstep, 0 in the umbra
    }
  }
  return cone * shadow_factor;
}

// The flashlight's contribution at a terrain point: the spot cone with a
// quadratic distance falloff out to `range` and a Lambert term against the
// surface `normal`, shadowed by `shadow`. Zero outside the beam.
[[nodiscard]] __device__ inline vec3
flashlight_terrain(const render_config::flashlight_params& fl, pos3 hit_point,
    vec3 normal, vec3 albedo, shadow_sphere shadow) {
  vec3 to_lamp_dir{};
  float dist = 0.0F;
  const float cone = flashlight_spot(fl, hit_point, shadow, to_lamp_dir, dist);
  if (cone <= 0.0F) return vec3{};
  const float fade = __saturatef(1.0F - (dist / fl.range));
  const float ndotl = fmaxf(dot(normal, to_lamp_dir), 0.0F);
  return albedo * (fl.color * (fl.intensity * cone * fade * fade * ndotl));
}

// The flashlight's glossy highlight on the chrome ball: a bright Blinn-Phong
// lobe where the ball reflects the beam toward the viewer, so the chrome
// lights up and blows out through bloom, holding steady across poses unlike
// the small, jittery reflection of the emitter. `ray_dir` is the ray that
// struck the ball (a primary ray, or a mirror's reflected ray). The half
// vector of the surface-to-lamp and surface-to-viewer directions is used, not
// just the view: for a primary ray the lamp sits at the eye so it reduces to a
// view-facing lobe, but for a mirror's ray the lamp is elsewhere, so the half
// vector keeps the highlight on the lit face instead of smearing it onto the
// side the mirror happens to see. The ball does not self-shadow, so no
// occluder is passed.
[[nodiscard]] __device__ inline vec3
flashlight_gloss(const render_config::flashlight_params& fl, pos3 hit_point,
    vec3 normal, vec3 ray_dir) {
  vec3 to_lamp_dir{};
  float dist = 0.0F;
  const float cone =
      flashlight_spot(fl, hit_point, shadow_sphere{}, to_lamp_dir, dist);
  if (cone <= 0.0F) return vec3{};
  // No highlight on a face the lamp does not light (the far side a mirror
  // sees).
  if (dot(normal, to_lamp_dir) <= 0.0F) return vec3{};
  const float fade = __saturatef(1.0F - (dist / fl.range));
  const vec3 half_v = normalize(to_lamp_dir + (ray_dir * -1.0F));
  // Spread the lobe with distance so the spot grows (and `fade` dims it) the
  // way a cone footprint does; 0 grow holds a fixed-breadth spot. Floored so a
  // far spot does not flatten the whole ball to white.
  const float power =
      fmaxf(fl.gloss_power / (1.0F + (fl.gloss_grow * dist)), 1.0F);
  // Conserve the highlight's energy as it spreads: a broader (farther) lobe is
  // dimmer in proportion (its integral over the ball scales as 1 / (power +
  // 1)), so the spot fades as it grows, like a cone spreading the same light
  // over more area, instead of staying full-bright and blowing out into a flat
  // white disc when dollied out. Unity at the near breadth (`gloss_power`);
  // brighter as the lobe tightens toward the ball, dimmer as it broadens away.
  const float spread = (power + 1.0F) / (fl.gloss_power + 1.0F);
  const float lobe = powf(fmaxf(dot(normal, half_v), 0.0F), power);
  return fl.color * (fl.gloss_strength * spread * cone * fade * lobe);
}

// Lit terrain color at surface point `hit_point`, tinted by the smoothly
// filtered color grid. At night the sun is off and the ambient is dimmed, so
// the flashlight and emissives carry the scene. `shadow` is the ball occluding
// the flashlight (the primary and mirror views pass it; the reflection paths
// leave it at none).
[[nodiscard]] __device__ inline vec3
shade_terrain_hit(const density_field& field, cudaTextureObject_t color,
    const render_config& cfg, pos3 hit_point, shadow_sphere shadow = {}) {
  const vec3 normal = field.normal(hit_point);
  const vec3 light_dir = normalize(cfg.sun_direction);
  const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);
  // Albedo from the linearly filtered color grid: one hardware-filtered fetch
  // in the field's coordinates, so strata seams stay smooth like the density.
  const vec3 vf = field.to_voxel(hit_point);
  const auto c = tex3D<float4>(color, vf.x + 0.5F, vf.y + 0.5F, vf.z + 0.5F);
  const vec3 albedo{c.x, c.y, c.z};

  const float sun_on = cfg.night ? 0.0F : 1.0F;
  const float ambient_scale = cfg.night ? 0.1F : 1.0F;
  const vec3 lit =
      albedo *
      ((cfg.terrain.ambient * ambient_scale) +
          (cfg.terrain.sun * (diffuse * sun_on)));
  return lit +
         flashlight_terrain(cfg.flashlight, hit_point, normal, albedo, shadow);
}

// Signed distance from 2D point (`x`, `y`) to a regular hexagon of radius `r`
// (the apothem, its flat-to-flat half-width), negative inside. Inigo Quilez's
// hexagon SDF, used to stamp the cockpit eyes as crisp shapes.
[[nodiscard]] __device__ inline float hexagon_sd(float x, float y, float r) {
  constexpr float kx = -std::numbers::sqrt3_v<float> / 2.0F; // -cos(30 deg)
  constexpr float ky = 0.5F;                                 // +sin(30 deg)
  constexpr float kz = std::numbers::inv_sqrt3_v<float>;     // +tan(30 deg)

  x = fabsf(x);
  y = fabsf(y);
  const float m = 2.0F * fminf((kx * x) + (ky * y), 0.0F);
  x -= m * kx;
  y -= m * ky;
  x -= fminf(fmaxf(x, -kz * r), kz * r);
  y -= r;
  return copysignf(sqrtf((x * x) + (y * y)), y);
}

// Distance from a 2D point into its cell of a flat-top hexagonal tiling: zero
// on a cell border, growing to the apothem (0.5 here) at a center. Flat-top so
// the two horizontal edges stay parallel to the ground when the grid is
// wrapped onto the rolling ball. The tiling repeats over a `sqrt3` by `1` cell
// holding two centers (the offset rows of a hex lattice); the nearer of the
// two gives the cell, and the hexagon's three slab half-widths give the border
// distance. A flat tiling of one orientation, unlike `geodesic_grid_edge`'s
// whole-sphere Goldberg grid whose cells point every which way.
[[nodiscard]] __device__ inline float hex_grid_edge(float x, float y) {
  constexpr float sx = std::numbers::sqrt3_v<float>; // column repeat
  constexpr float sy = 1.0F;                         // row repeat

  // The two candidate centers: the base lattice and the half-cell-offset one.
  const float ax = x - (sx * floorf((x / sx) + 0.5F));
  const float ay = y - (sy * floorf((y / sy) + 0.5F));
  const float xo = x - (0.5F * sx);
  const float yo = y - (0.5F * sy);
  const float bx = xo - (sx * floorf((xo / sx) + 0.5F));
  const float by = yo - (sy * floorf((yo / sy) + 0.5F));
  const bool a_near = ((ax * ax) + (ay * ay)) < ((bx * bx) + (by * by));
  const float gx = a_near ? ax : bx;
  const float gy = a_near ? ay : by;

  // Flat-top hexagon support: the largest of the three slab projections, the
  // apothem (0.5) minus it is the distance to the nearest border.
  const float hr = fmaxf(fabsf(gy),
      fmaxf(fabsf((cos_30_v<> * gx) + (0.5F * gy)),
          fabsf((-cos_30_v<> * gx) + (0.5F * gy))));
  return 0.5F - hr;
}

// Angular distance from unit direction `dir` to the nearest cell border of a
// Goldberg geodesic hex grid: a vertex-up icosahedron with every face
// subdivided into a frequency-`freq` triangular lattice, whose vertices are
// the cell centers. Near-uniform hexagons over the whole sphere with exactly
// twelve pentagons at the icosahedron vertices, which a single flat-grid
// projection cannot achieve (a sphere has no isometric flattening). Zero on a
// seam, growing into each cell.
//
// The nearest face holds `dir` (its three vertices have the largest summed
// alignment, the sums being equal-magnitude); `dir` projects gnomonically into
// that face's plane, and the barycentric cell it lands in has three corner
// geodesic vertices as the candidate cell centers. The nearest two give the
// Voronoi border as their angular half-gap. Per-face tiling leaves a faint
// kink along face edges and slight irregularity at the pentagons, both
// sub-pixel in the reflected dome.
[[nodiscard]] __device__ inline float
geodesic_grid_edge(vec3 dir, int freq, vec3 eye_dir, vec3 eye_tan) {
  // The 12 unit icosahedron vertices, pole-aligned: two poles and two pentagon
  // rings at latitude +/-atan(1/2). All coordinates derive from the golden
  // ratio (sqrt5 = 2*phi - 1): ring latitude +/-1/sqrt5, ring radius 2/sqrt5,
  // and each pentagon vertex is that radius times cos/sin of a multiple of 36
  // deg. The cos factors are closed-form in phi; the sin factors need a sqrt
  // (not constexpr here), so the two canonical icosphere constants stay as
  // documented literals.
  constexpr float phi = std::numbers::phi_v<float>;
  constexpr float ring_y =
      1.0F / ((2.0F * phi) - 1.0F);       // 1/sqrt5, ring latitude
  constexpr float ring_r = 2.0F * ring_y; // 2/sqrt5, ring radius
  constexpr float rx72 = ring_r * ((phi - 1.0F) / 2.0F); // 2/sqrt5 * cos72
  constexpr float rx36 = ring_r * (phi / 2.0F);          // 2/sqrt5 * cos36
  constexpr float rz72 = 0.8506508F;                     // 2/sqrt5 * sin72
  constexpr float rz36 = 0.5257311F;                     // 2/sqrt5 * sin36
  const vec3 vtx[12] = {{0.0F, 1.0F, 0.0F}, {ring_r, ring_y, 0.0F},
      {rx72, ring_y, rz72}, {-rx36, ring_y, rz36}, {-rx36, ring_y, -rz36},
      {rx72, ring_y, -rz72}, {rx36, -ring_y, rz36}, {-rx72, -ring_y, rz72},
      {-ring_r, -ring_y, 0.0F}, {-rx72, -ring_y, -rz72},
      {rx36, -ring_y, -rz36}, {0.0F, -1.0F, 0.0F}};
  const int face[20][3] = {{0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 5},
      {0, 5, 1}, {1, 2, 6}, {2, 3, 7}, {3, 4, 8}, {4, 5, 9}, {5, 1, 10},
      {1, 6, 10}, {2, 7, 6}, {3, 8, 7}, {4, 9, 8}, {5, 10, 9}, {11, 6, 7},
      {11, 7, 8}, {11, 8, 9}, {11, 9, 10}, {11, 10, 6}};

  // Re-express `dir` in the icosahedron frame so the geodesic vertex nearest
  // face 0's centroid sits at `eye_dir`, with `eye_tan` (a unit vector
  // perpendicular to `eye_dir`) fixing the rotation about it. That nests the
  // cockpit eye in a cell rather than letting the fixed grid fall where it
  // may.
  const int n = static_cast<int>(lroundf(static_cast<float>(freq) / 3.0F));
  const vec3 usel = normalize(
      (vtx[0] * static_cast<float>(n)) + (vtx[1] * static_cast<float>(n)) +
      (vtx[2] * static_cast<float>(freq - (2 * n))));
  const vec3 utan = normalize(vtx[0] - (usel * dot(vtx[0], usel)));
  const vec3 ubi = cross(usel, utan);
  const vec3 ebi = cross(eye_dir, eye_tan);
  dir = (usel * dot(dir, eye_dir)) + (utan * dot(dir, eye_tan)) +
        (ubi * dot(dir, ebi));

  int best = 0;
  float best_dot = -3.0F;
  for (int f = 0; f < 20; ++f) {
    const vec3 sum = vtx[face[f][0]] + vtx[face[f][1]] + vtx[face[f][2]];
    const float d = dot(dir, sum);
    if (d > best_dot) {
      best_dot = d;
      best = f;
    }
  }
  const vec3 a = vtx[face[best][0]];
  const vec3 b = vtx[face[best][1]];
  const vec3 c = vtx[face[best][2]];

  // Gnomonic projection onto the face plane, then barycentric coordinates.
  const vec3 nrm = cross(b - a, c - a);
  const vec3 p = dir * (dot(a, nrm) / dot(dir, nrm));
  const vec3 e0 = b - a;
  const vec3 e1 = c - a;
  const vec3 e2 = p - a;
  const float d00 = dot(e0, e0);
  const float d01 = dot(e0, e1);
  const float d11 = dot(e1, e1);
  const float den = (d00 * d11) - (d01 * d01);
  const float wb = ((d11 * dot(e2, e0)) - (d01 * dot(e2, e1))) / den;
  const float wc = ((d00 * dot(e2, e1)) - (d01 * dot(e2, e0))) / den;
  const float wa = 1.0F - wb - wc;

  // The triangular-lattice cell containing the point, scaled by the frequency:
  // its three integer corners are the candidate geodesic vertices.
  const auto fm = static_cast<float>(freq);
  const float la = wa * fm;
  const float lb = wb * fm;
  const float lc = wc * fm;
  const float ga = floorf(la);
  const float gb = floorf(lb);
  const float gc = floorf(lc);
  // The three floored remainders sum to 1 (an upward sub-triangle) or 2 (a
  // downward one); split at 1.5.
  const int sub = ((la - ga) + (lb - gb) + (lc - gc)) < 1.5F ? 0 : 1;
  // Corner offsets for the upward (sub == 0) and downward sub-triangle.
  const int off[2][3][3] = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
      {{0, 1, 1}, {1, 0, 1}, {1, 1, 0}}};
  float t[3] = {0.0F, 0.0F, 0.0F};
  for (int n = 0; n < 3; ++n) {
    const vec3 corner =
        (a * (ga + static_cast<float>(off[sub][n][0]))) +
        (b * (gb + static_cast<float>(off[sub][n][1]))) +
        (c * (gc + static_cast<float>(off[sub][n][2])));
    t[n] = dot(dir, normalize(corner));
  }

  // The border is the angular half-gap between the nearest two corners (the
  // two largest dots).
  const float hi = fmaxf(t[0], fmaxf(t[1], t[2]));
  const float lo = fminf(t[0], fminf(t[1], t[2]));
  const float mid = (t[0] + t[1] + t[2]) - hi - lo;
  return 0.5F * (acosf(fminf(mid, 1.0F)) - acosf(fminf(hi, 1.0F)));
}

// The geodesic cell the eye nests on (the lattice point near face 0's centroid
// that `geodesic_grid_edge` reorients onto the eye), as its angular `apothem`
// (half the mean spacing to its six neighbors) and the grid `phase` about the
// eye that brings the cell flat-top: a neighbor placed at `+tv` (a quarter
// turn) so the iris hexagon and the cell share edges.
//
// Both depend on `freq` alone and use no floor-based cell lookup, which is
// degenerate at the exact cell center (the barycentric coordinates fall on
// integers there) and returns a wrong, motion-jittered value.
struct geodesic_eye_cell {
  float apothem; // angular half flat-to-flat of the eye cell
  float phase;   // grid rotation about the eye that makes the cell flat-top
};

[[nodiscard]] __host__ __device__ inline geodesic_eye_cell
geodesic_eye_cell_of(int freq) {
  const vec3 v0{0.0F, 1.0F, 0.0F};
  const vec3 v1{0.8944272F, 0.4472136F, 0.0F};
  const vec3 v2{0.2763932F, 0.4472136F, 0.8506508F};
  const int n = static_cast<int>(lroundf(static_cast<float>(freq) / 3.0F));
  const int bi = n;
  const int bj = n;
  const int bk = freq - (2 * n);
  const vec3 cell = normalize(
      (v0 * static_cast<float>(bi)) + (v1 * static_cast<float>(bj)) +
      (v2 * static_cast<float>(bk)));

  // The lattice tangent frame `geodesic_grid_edge` builds at this cell.
  const vec3 utan = normalize(v0 - (cell * dot(v0, cell)));
  const vec3 ubi = cross(cell, utan);

  // Scan the six neighbor lattice points: sum their angles for the mean
  // apothem, and keep the nearest (in the tangent frame) for the phase.
  const int off[6][3] = {{1, -1, 0}, {-1, 1, 0}, {1, 0, -1}, {-1, 0, 1},
      {0, 1, -1}, {0, -1, 1}};
  float best = -1.0F;
  float p = 1.0F;
  float q = 0.0F;
  float angle_sum = 0.0F;
  for (const auto& o : off) {
    const vec3 nb = normalize(
        (v0 * static_cast<float>(bi + o[0])) +
        (v1 * static_cast<float>(bj + o[1])) +
        (v2 * static_cast<float>(bk + o[2])));
    const float d = dot(cell, nb);
    angle_sum += acosf(fminf(d, 1.0F));
    if (d > best) {
      best = d;
      p = dot(nb, utan);
      q = dot(nb, ubi);
    }
  }

  // Phase: the nearest neighbor's eye-frame angle is `atan2(q, p) + phase`;
  // place it at `+tv` (a quarter turn) for a flat-top cell. Apothem: half the
  // MEAN neighbor angle, not the nearest. At a multiple-of-3 `freq` the eye
  // sits on the face centroid, whose six neighbors are equidistant by
  // symmetry, so a regular-hexagon iris is exactly flush all around (mean ==
  // min); off the centroid the cell is a slightly irregular hexagon, and the
  // mean centers the iris so the near and far edges split the mismatch
  // symmetrically instead of three sitting flush and three gapping.
  constexpr float half_pi = std::numbers::pi_v<float> / 2.0F;
  return {0.5F * (angle_sum / 6.0F), half_pi - atan2f(q, p)};
}

#pragma endregion

} // namespace corvid::cuda
