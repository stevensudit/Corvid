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

  const vec3 sun_dir = normalize(cfg.sun_direction);
  const float s = fmaxf(dot(ray_dir, sun_dir), 0.0F);
  col = col +
        (sky.halo_color * (sky.halo_strength * powf(s, sky.halo_exponent)));
  col = col + (sky.core_color * powf(s, sky.core_exponent));
  return col;
}

// Lit terrain color at surface point `hit_point`, tinted by the smoothly
// filtered color grid.
[[nodiscard]] __device__ inline vec3
shade_terrain_hit(const density_field& field, cudaTextureObject_t color,
    const render_config& cfg, pos3 hit_point) {
  const vec3 normal = field.normal(hit_point);
  const vec3 light_dir = normalize(cfg.sun_direction);
  const float diffuse = fmaxf(dot(normal, light_dir), 0.0F);
  // Albedo from the linearly filtered color grid: one hardware-filtered fetch
  // in the field's coordinates, so strata seams stay smooth like the density.
  const vec3 vf = field.to_voxel(hit_point);
  const auto c = tex3D<float4>(color, vf.x + 0.5F, vf.y + 0.5F, vf.z + 0.5F);
  return vec3{c.x, c.y, c.z} *
         (cfg.terrain.ambient + (cfg.terrain.sun * diffuse));
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
  const vec3 vtx[12] = {{0.0F, 1.0F, 0.0F}, {0.8944272F, 0.4472136F, 0.0F},
      {0.2763932F, 0.4472136F, 0.8506508F},
      {-0.7236068F, 0.4472136F, 0.5257311F},
      {-0.7236068F, 0.4472136F, -0.5257311F},
      {0.2763932F, 0.4472136F, -0.8506508F},
      {0.7236068F, -0.4472136F, 0.5257311F},
      {-0.2763932F, -0.4472136F, 0.8506508F}, {-0.8944272F, -0.4472136F, 0.0F},
      {-0.2763932F, -0.4472136F, -0.8506508F},
      {0.7236068F, -0.4472136F, -0.5257311F}, {0.0F, -1.0F, 0.0F}};
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
  constexpr float half_pi = 1.5707963F;
  return {0.5F * (angle_sum / 6.0F), half_pi - atan2f(q, p)};
}

#pragma endregion

} // namespace corvid::cuda
