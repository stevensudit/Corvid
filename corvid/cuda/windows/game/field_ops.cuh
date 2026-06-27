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

#include <cuda_runtime.h>

#include "../../cuda_kernel.cuh"
#include "../../density_field.cuh"
#include "../../vec.cuh"

// Per-frame operations on the live density field.
//
// The dig brush that carves it and the ground probe that settles the avatar
// onto it. Both sample the same field the renderer marches, so a freshly dug
// hole shows up in collision the next frame.

namespace corvid::cuda {

#pragma region Editing

// Where a dig will land: the world point the center ray hit, and whether it
// hit anything at all.
struct dig_probe {
  pos3 point;
  bool hit;
};

// Cast the center ray and record where it meets the surface, for the brush.
__global__ void
pick_kernel(density_field field, pos3 eye, vec3 dir, dig_probe* out) {
  const float dist = field.raymarch(eye, dir);
  out->hit = (dist >= 0.0F);
  // No need to set on miss, because we check for hit before reading the point.
  if (out->hit) out->point = eye + (dir * dist);
}

// Carve a spherical brush of `radius` around the picked point, subtracting
// `strength` from each voxel's density with a linear falloff so the center
// digs most; lowering density turns solid into air. A no-op if the pick
// missed. Reads and writes the field surface, so the next frame's march shows
// the hole.
__global__ void dig_kernel(cudaSurfaceObject_t surface, density_field field,
    const dig_probe* probe, float radius, float strength) {
  if (!probe->hit) return;

  // Skip voxels outside the brush's bounding cube.
  const int radius_voxels = static_cast<int>(ceilf(radius / field.voxel_size));
  const int span = (2 * radius_voxels) + 1;
  const int sx = cuda_kernel::x_index();
  const int sy = cuda_kernel::y_index();
  const int sz = cuda_kernel::z_index();
  if (sx >= span || sy >= span || sz >= span) return;

  // The picked point in voxel space, and the voxel this thread edits.
  const vec3 rel = field.to_voxel(probe->point);
  const int vx = static_cast<int>(lroundf(rel.x)) + (sx - radius_voxels);
  const int vy = static_cast<int>(lroundf(rel.y)) + (sy - radius_voxels);
  const int vz = static_cast<int>(lroundf(rel.z)) + (sz - radius_voxels);
  const int3 voxel = make_int3(vx, vy, vz);
  if (!field.contains(voxel)) return;

  // Skip voxels outside the brush sphere.
  const pos3 voxel_point = field.voxel_center(voxel);
  const float d = distance(voxel_point, probe->point);
  if (d > radius) return;

  float density = 0.0F;
  surf3Dread(&density, surface, vx * static_cast<int>(sizeof(float)), vy, vz);
  density -= strength * (1.0F - (d / radius));
  surf3Dwrite(density, surface, vx * static_cast<int>(sizeof(float)), vy, vz);
}

#pragma endregion
#pragma region Collision

// What the ground probe reports under the ball, read back to the host to
// settle the avatar onto the terrain: the outward surface normal at the ball
// center and the signed distance from the center to the surface (positive when
// the center is in air, negative when it has sunk into solid).
struct ground_probe {
  vec3 normal;
  float surface_dist;
};

// Sample the live density field at the ball center and report the surface
// normal and the signed distance to it, treating the density as an approximate
// signed-distance field. The density gradient (central differences) gives the
// outward normal and, with the density value, the perpendicular distance to
// the zero crossing: a step `g` of `sample(+e) - sample(-e)` is about
// `2 * e * grad`, so the distance is `-density / |grad| = -density * 2e /
// |g|`. One thread; the host reads the result back a frame later to push the
// ball out of the ground along the normal (see `avatar_rig::settle`). Because
// it samples the same field the dig edits, a freshly dug hole drops the ball
// into it.
__global__ void
ground_probe_kernel(density_field field, pos3 center, ground_probe* out) {
  const float e = field.voxel_size;
  const float d = field.sample_density(center);
  const vec3 dx{e, 0.0F, 0.0F};
  const vec3 dy{0.0F, e, 0.0F};
  const vec3 dz{0.0F, 0.0F, e};
  const vec3 g{
      field.sample_density(center + dx) - field.sample_density(center - dx),
      field.sample_density(center + dy) - field.sample_density(center - dy),
      field.sample_density(center + dz) - field.sample_density(center - dz)};
  const float glen = length(g);
  if (glen > 1.0e-6F) {
    out->normal = g * (-1.0F / glen); // outward = -grad / |grad|
    out->surface_dist = -d * 2.0F * e / glen;
  } else {
    // A flat region with no gradient: deep solid pushes straight up, open air
    // reports no contact.
    out->normal = vec3{0.0F, 1.0F, 0.0F};
    out->surface_dist = d > 0.0F ? -1.0e30F : 1.0e30F;
  }
}

#pragma endregion

} // namespace corvid::cuda
