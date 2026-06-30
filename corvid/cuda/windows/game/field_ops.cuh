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
#include "../../raycast.cuh"
#include "../../vec.cuh"

// Per-frame operations on the live density field.
//
// The dig brush that carves it and the ground probe that settles the avatar
// onto it. Both sample the same field the renderer marches, so a freshly dug
// hole shows up in collision the next frame.

namespace corvid::cuda {

#pragma region Editing

// Where a dig will land: the world point the aim ray hit, and whether it hit
// at all.
struct dig_probe {
  pos3 point;
  bool hit;
};

// Cast the aim ray and record where it meets the surface, for the brush and
// the in-world target reticle.
__global__ void
pick_kernel(density_field field, pos3 eye, vec3 dir, dig_probe* out) {
  const float dist = field.raymarch(eye, dir);
  out->hit = (dist >= 0.0F);
  // No need to set on miss, because we check for hit before reading the point.
  if (out->hit) {
    // Snap the aim hit onto the true surface, like the reticle's own pixels.
    //
    // A grazing aim (a cave ceiling, a hill peak) is accepted within the
    // march's tolerance and so terraces frame to frame as the camera
    // micro-moves; that jitters the reticle center, which swings the inner
    // crosshair's azimuth (its `d = hit - center` is short, so a small center
    // wobble turns it a lot).
    out->point = field.refine_hit(eye + (dir * dist), dir);
  }
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

// Wear a track into the dirt under the rolling ball: over a `radius` footprint
// at `contact`, subtract `crush` from the density (lowering the surface into a
// shallow groove) and fade the color toward dark by `darken` (a stain), both
// with the same linear falloff as the dig. `crush` and `darken` are the
// already-distance-scaled amounts for this frame: the caller multiplies each
// by how far the ball rolled laterally, so a ball settling straight down from
// compaction (no lateral roll) edits nothing and the single-pass depth does
// not depend on speed; rolling back and forth wears it deeper. Either amount 0
// skips that half. The stain never falls below `darken_floor` so a track does
// not crush to black. A no-op outside the brush sphere or the field.
__global__ void crush_kernel(cudaSurfaceObject_t density_surface,
    cudaSurfaceObject_t color_surface, density_field field, pos3 contact,
    float radius, float crush, float darken, float darken_floor) {
  const int radius_voxels = static_cast<int>(ceilf(radius / field.voxel_size));
  const int span = (2 * radius_voxels) + 1;
  const int sx = cuda_kernel::x_index();
  const int sy = cuda_kernel::y_index();
  const int sz = cuda_kernel::z_index();
  if (sx >= span || sy >= span || sz >= span) return;

  const vec3 rel = field.to_voxel(contact);
  const int vx = static_cast<int>(lroundf(rel.x)) + (sx - radius_voxels);
  const int vy = static_cast<int>(lroundf(rel.y)) + (sy - radius_voxels);
  const int vz = static_cast<int>(lroundf(rel.z)) + (sz - radius_voxels);
  const int3 voxel = make_int3(vx, vy, vz);
  if (!field.contains(voxel)) return;

  const pos3 voxel_point = field.voxel_center(voxel);
  const float d = distance(voxel_point, contact);
  if (d > radius) return;
  const float falloff = 1.0F - (d / radius);

  if (crush > 0.0F) {
    float density = 0.0F;
    surf3Dread(&density, density_surface, vx * static_cast<int>(sizeof(float)),
        vy, vz);
    density -= crush * falloff;
    surf3Dwrite(density, density_surface, vx * static_cast<int>(sizeof(float)),
        vy, vz);
  }

  if (darken > 0.0F) {
    uchar4 packed{};
    surf3Dread(&packed, color_surface, vx * static_cast<int>(sizeof(uchar4)),
        vy, vz);
    const float fade = fmaxf(1.0F - (darken * falloff), 0.0F);
    const float r =
        fmaxf((static_cast<float>(packed.x) / 255.0F) * fade, darken_floor);
    const float g =
        fmaxf((static_cast<float>(packed.y) / 255.0F) * fade, darken_floor);
    const float b =
        fmaxf((static_cast<float>(packed.z) / 255.0F) * fade, darken_floor);
    surf3Dwrite(
        make_uchar4(to_unorm8(r), to_unorm8(g), to_unorm8(b), packed.w),
        color_surface, vx * static_cast<int>(sizeof(uchar4)), vy, vz);
  }
}

#pragma endregion
#pragma region Collision

// What the collision probe reports under the ball, read back to the host to
// resolve the avatar against the terrain.
//
// `normal` and `surface_dist` are the nearest-surface contact at the ball
// center (outward normal, signed distance, positive in air and negative when
// sunk into solid): the floor, for gravity and the slope/climb test. `push`
// lifts the ball out of a wall or ceiling the center sample misses (a
// symmetric pit cancels in the center gradient, so the sides would clip
// without it). `wall_normal` is the steepest horizontal face the ball presses
// into (unit, or zero), for the velocity stop and the stain; `overhead` flags
// a ceiling or too-short tunnel, the confined signal the camera will later
// dolly on.
struct ground_probe {
  vec3 normal;
  float surface_dist;
  vec3 push;
  vec3 wall_normal;
  bool overhead;
};

// A `surface_dist` magnitude meaning no surface is within reach: reported
// (positive) in open air, and negated when buried in solid with no gradient to
// climb. Far past any real voxel distance, so it never reads as contact.
constexpr float no_contact = big_value;

// Resolve a sphere of `radius` at the ball `center` against the terrain for
// one frame, sampling the live density field as an approximate signed-distance
// field.
//
// The center gradient (central differences) gives the nearest-surface contact:
// the outward normal and, with the density value, the perpendicular distance
// to the zero crossing (a step `g` of `sample(+e) - sample(-e)` is about `2 *
// e * grad`, so the distance is `-density * 2e / |g|`). That is the floor, for
// gravity and the slope/climb test, as before.
//
// Then a fixed set of points on the ball surface catches the contacts the
// center gradient cancels: a ring around the equator for walls and an upper
// fan for ceilings. A sample inside solid (density above zero) is a
// penetration of about that depth; push the ball out along the radial back
// toward the center, summing all of them so the opposite faces of a snug pit
// cancel to a stable rest. One thread; the host reads it back a frame later.
__global__ void ground_probe_kernel(density_field field, pos3 center,
    float radius, ground_probe* out) {
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
  // A near-zero gradient is a flat region with no surface direction; below
  // this, fall back to the no-gradient case (also guards the divide by glen).
  constexpr float gradient_epsilon = 1.0e-6F;
  if (glen > gradient_epsilon) {
    out->normal = g * (-1.0F / glen); // outward = -grad / |grad|
    out->surface_dist = -d * 2.0F * e / glen;
  } else {
    // A flat region with no gradient: deep solid pushes straight up, open air
    // reports no contact.
    out->normal = vec3{0.0F, 1.0F, 0.0F};
    out->surface_dist = d > 0.0F ? -no_contact : no_contact;
  }

  // The surface probe directions: eight around the equator (walls), then the
  // pole and an upper fan (ceilings and the mouth of a too-short tunnel).
  constexpr float s = 0.70710678F;
  const vec3 dirs[13] = {{1.0F, 0.0F, 0.0F}, {s, 0.0F, s}, {0.0F, 0.0F, 1.0F},
      {-s, 0.0F, s}, {-1.0F, 0.0F, 0.0F}, {-s, 0.0F, -s}, {0.0F, 0.0F, -1.0F},
      {s, 0.0F, -s}, {0.0F, 1.0F, 0.0F}, {s, s, 0.0F}, {0.0F, s, s},
      {-s, s, 0.0F}, {0.0F, s, -s}};

  // Sum every penetrating contact's radial push, rather than picking the
  // single deepest one.
  //
  // The opposite faces of a snug pit then cancel to a stable centered rest,
  // where the deepest-only resolve spins frame to frame (near- tied
  // penetrations plus the one-frame-stale probe) and shivers the ball.
  // `contact_tol` ignores resting contact so a seated ball does not push at
  // all. The deepest horizontal contact also reports the wall normal, for the
  // velocity stop and the stain.
  constexpr float contact_tol = 0.05F; // rest contact does not push
  vec3 push{0.0F, 0.0F, 0.0F};
  vec3 wall_normal{0.0F, 0.0F, 0.0F};
  float worst_wall = 0.0F;
  bool overhead = false;
  for (const vec3 u : dirs) {
    const float dp = field.sample_density(center + (u * radius)) - contact_tol;
    if (dp <= 0.0F) continue;
    push = push + ((u * -1.0F) * dp); // out along the radial, back to center
    if (u.y > 0.5F) {
      overhead = true;
    } else if (dp > worst_wall) {
      worst_wall = dp;
      wall_normal = u * -1.0F; // outward from the wall, toward the ball
    }
  }
  out->push = push;
  out->wall_normal = wall_normal;
  out->overhead = overhead;
}

// Probe the terrain along the camera boom axis, for the auto-merge.
//
// The head's seat runs from the ball out along this ray (up by the boom rise,
// back along the heading); the rig needs to know how far it stays clear so the
// boom can be clamped short of dirt, gliding the camera into the ball (the
// glass-lens viewpoint) when even the jockey seat will not fit, as in a
// tunnel.
//
// Reports the clear distance from `origin` along unit `dir` to the first
// solid, `no_contact` when the ray stays clear, or 0 when `origin` is already
// buried (so the camera merges fully). One thread; the host reads it back a
// frame later, like the ground probe.
__global__ void
boom_probe_kernel(density_field field, pos3 origin, vec3 dir, float* out) {
  if (field.sample_density(origin) > 0.0F) {
    *out = 0.0F; // the seat base is in solid: no room to dolly out
    return;
  }
  const float t = field.raymarch(origin, dir);
  *out = (t >= 0.0F) ? t : no_contact;
}

#pragma endregion

} // namespace corvid::cuda
