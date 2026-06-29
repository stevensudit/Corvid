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

#include <cstdint>
#include <stdexcept>

#include <cuda_runtime.h>

#include "../../cuda_kernel.cuh"
#include "../../cuda_ptr.cuh"
#include "../../cuda_status.cuh"
#include "../../cuda_volume.cuh"
#include "../../density_field.cuh"
#include "../../material_volume.cuh"
#include "../../raycast.cuh"
#include "../../strata.cuh"
#include "../../terrain.cuh"
#include "../../vec.cuh"

// One-time GPU world generation for the voxel viewer: build a terrain
// heightfield, slump it to the soil's angle of repose, then fill the geometry
// density, material tier, and tier-seeded color grids from it.

namespace corvid::cuda {

// A filtered color grid: a uchar4 linear-unorm albedo per voxel, read back as
// a normalized float through a linearly filtered texture (a quarter the VRAM
// of float4, and the filtering still blends in float).
using color_volume = cuda_volume<uchar4, cudaReadModeNormalizedFloat>;

#pragma region Terrain

// Seed the heightfield from the terrain noise: one world-space surface height
// per (x, z) column, laid out row-major in z.
__global__ void init_height_kernel(float* height, int width, int depth,
    pos3 origin, float voxel_size) {
  const int ix = cuda_kernel::x_index();
  const int iz = cuda_kernel::y_index();
  if (ix >= width || iz >= depth) return;
  const float wx = origin.v.x + (static_cast<float>(ix) * voxel_size);
  const float wz = origin.v.z + (static_cast<float>(iz) * voxel_size);
  height[(iz * width) + ix] = terrain::height(wx, wz);
}

// One thermal-erosion pass: each column sheds material to its four neighbors
// wherever it stands steeper than the repose slope. Repeated, this settles
// every slope to at most `max_step` per cell, so no sharp faces survive. Reads
// `src`, writes `dst`; the caller ping-pongs them.
__global__ void erode_kernel(const float* src, float* dst, int width,
    int depth, float max_step, float rate) {
  const int ix = cuda_kernel::x_index();
  const int iz = cuda_kernel::y_index();
  if (ix >= width || iz >= depth) return;
  const int xm = ix > 0 ? ix - 1 : 0;
  const int xp = ix < width - 1 ? ix + 1 : width - 1;
  const int zm = iz > 0 ? iz - 1 : 0;
  const int zp = iz < depth - 1 ? iz + 1 : depth - 1;
  const float h = src[(iz * width) + ix];
  float delta = 0.0F;
  delta += terrain::talus_flow(h, src[(iz * width) + xm], max_step, rate);
  delta += terrain::talus_flow(h, src[(iz * width) + xp], max_step, rate);
  delta += terrain::talus_flow(h, src[(zm * width) + ix], max_step, rate);
  delta += terrain::talus_flow(h, src[(zp * width) + ix], max_step, rate);
  dst[(iz * width) + ix] = h + delta;
}

// Fill one voxel from the `surface_height` of its column: the geometry density
// (solid below the surface, air above), the material tier by depth, and the
// color seeded from that tier with per-cell brightness and tint variation, so
// a band is not one flat color. Shared by the world-gen fill and the flatten
// brush, which differ only in where the surface height comes from.
__device__ inline void fill_voxel(cudaSurfaceObject_t density_surface,
    cudaSurfaceObject_t material_surface, cudaSurfaceObject_t color_surface,
    const density_field& field, int3 voxel, float surface_height) {
  const vec3 w = field.voxel_center(voxel).v;
  const float density = surface_height - w.y;
  surf3Dwrite(density, density_surface,
      voxel.x * static_cast<int>(sizeof(float)), voxel.y, voxel.z);
  const uint16_t tier = strata::tier_for_depth(density);
  surf3Dwrite(tier, material_surface,
      voxel.x * static_cast<int>(sizeof(uint16_t)), voxel.y, voxel.z);

  // Vary brightness and tint with 3D fractal noise in world space, so the
  // filtered color mottles organically instead of revealing a grid.
  const vec3 base = strata::tier_color(tier);
  constexpr float noise_scale = 0.15F;
  const float n =
      terrain::fbm_3d(w.x * noise_scale, w.y * noise_scale, w.z * noise_scale);
  const float warm = terrain::fbm_3d((w.x + 100.0F) * noise_scale,
      w.y * noise_scale, w.z * noise_scale);
  const float shade = 0.78F + (0.50F * n);
  const vec3 tint{0.94F + (0.12F * warm), 1.0F, 1.06F - (0.12F * warm)};
  const vec3 c = base * shade * tint;
  surf3Dwrite(make_uchar4(to_unorm8(c.x), to_unorm8(c.y), to_unorm8(c.z), 255),
      color_surface, voxel.x * static_cast<int>(sizeof(uchar4)), voxel.y,
      voxel.z);
}

// Fill the grids from the (eroded) heightfield: each column's surface is its
// heightfield sample.
__global__ void fill_kernel(cudaSurfaceObject_t density_surface,
    cudaSurfaceObject_t material_surface, cudaSurfaceObject_t color_surface,
    const float* height, int height_width, density_field field) {
  const int3 voxel = make_int3(cuda_kernel::x_index(), cuda_kernel::y_index(),
      cuda_kernel::z_index());
  if (!field.contains(voxel)) return;
  fill_voxel(density_surface, material_surface, color_surface, field, voxel,
      height[(voxel.z * height_width) + voxel.x]);
}

// Overwrite every voxel with a level surface at world height `flat_height`: a
// flat test track for measuring speeds. The same per-voxel fill as world-gen
// with a constant surface height, so the strata coloring stays correct.
__global__ void flatten_kernel(cudaSurfaceObject_t density_surface,
    cudaSurfaceObject_t material_surface, cudaSurfaceObject_t color_surface,
    density_field field, float flat_height) {
  const int3 voxel = make_int3(cuda_kernel::x_index(), cuda_kernel::y_index(),
      cuda_kernel::z_index());
  if (!field.contains(voxel)) return;
  fill_voxel(density_surface, material_surface, color_surface, field, voxel,
      flat_height);
}

#pragma endregion
#pragma region World generation

// Generate the terrain into the three grids: build the surface heightfield,
// slump it to the soil's angle of repose, then fill the geometry density, the
// material tier, and the tier-seeded color from it. One-time GPU world-gen,
// run before the first frame.
inline void generate_world(const density_field& field,
    const cuda_volume<float>& volume, const material_volume& materials,
    const color_volume& colors) {
  const cudaExtent extent = field.extent;
  const int height_w = static_cast<int>(extent.width);
  const int height_d = static_cast<int>(extent.depth);

  // Build the heightfield and slump it to the soil's angle of repose, so
  // world-gen leaves no face steeper than `repose_slope` (no sharp corners to
  // alias). Each thermal-erosion pass sheds material off columns standing too
  // tall over a neighbor; a few dozen passes settle every slope.
  constexpr float repose_slope = 0.7F; // tangent, about 35 degrees
  constexpr float erode_rate = 0.15F;
  constexpr int erode_passes = 80;
  cuda_ptr<float> height_a{static_cast<size_t>(height_w) * height_d};
  cuda_ptr<float> height_b{static_cast<size_t>(height_w) * height_d};
  if (!height_a || !height_b)
    throw std::runtime_error{"failed to allocate heightfield"};
  const dim3 height_block{16, 16};
  const dim3 height_grid{cuda_kernel::ceil_div(extent.width, height_block.x),
      cuda_kernel::ceil_div(extent.depth, height_block.y)};
  init_height_kernel<<<height_grid, height_block>>>(height_a.get(), height_w,
      height_d, field.origin, field.voxel_size);
  float* height_src = height_a.get();
  float* height_dst = height_b.get();
  for (int pass = 0; pass < erode_passes; ++pass) {
    erode_kernel<<<height_grid, height_block>>>(height_src, height_dst,
        height_w, height_d, repose_slope * field.voxel_size, erode_rate);
    float* const tmp = height_src;
    height_src = height_dst;
    height_dst = tmp;
  }

  // Fill the geometry, material, and color grids from the eroded heightfield.
  const dim3 fill_block{8, 8, 8};
  const dim3 fill_grid{cuda_kernel::ceil_div(extent.width, fill_block.x),
      cuda_kernel::ceil_div(extent.height, fill_block.y),
      cuda_kernel::ceil_div(extent.depth, fill_block.z)};
  fill_kernel<<<fill_grid, fill_block>>>(volume.surface(), materials.surface(),
      colors.surface(), height_src, height_w, field);
  cuda_last_status{cudaDeviceSynchronize()}.or_throw();
}

// Flatten the whole world to a level surface at `flat_height` (world units): a
// test track for measuring speeds. Overwrites all three grids (the dug shape
// is lost); synchronous, a one-shot user action.
inline void flatten_world(const density_field& field,
    const cuda_volume<float>& volume, const material_volume& materials,
    const color_volume& colors, float flat_height) {
  const cudaExtent extent = field.extent;
  const dim3 block{8, 8, 8};
  const dim3 grid{cuda_kernel::ceil_div(extent.width, block.x),
      cuda_kernel::ceil_div(extent.height, block.y),
      cuda_kernel::ceil_div(extent.depth, block.z)};
  flatten_kernel<<<grid, block>>>(volume.surface(), materials.surface(),
      colors.surface(), field, flat_height);
  cuda_last_status{cudaDeviceSynchronize()}.or_throw();
}

#pragma endregion
#pragma region Test fixtures

// Carve a fan of straight cylindrical test tunnels into the geometry: `count`
// bores side by side, each angled a further `angle_step` (radians) below
// horizontal, all driving the same heading from a row of openings spaced
// `spacing` apart starting at `row_origin`. A reproducible grade fixture for
// measuring how the ball climbs and slips at a known angle. Subtracts from the
// density only (the air it opens needs no material or color) as a CSG
// difference (`min` against the bore's signed distance), so the field stays a
// usable distance estimate near the new walls. Pairs with `flatten_world`:
// flatten first, then carve.
__global__ void dig_tunnels_kernel(cudaSurfaceObject_t density_surface,
    density_field field, pos3 row_origin, vec3 bore_dir, vec3 row_dir,
    float spacing, float radius, float bore_length, int count,
    float angle_step) {
  const int3 voxel = make_int3(cuda_kernel::x_index(), cuda_kernel::y_index(),
      cuda_kernel::z_index());
  if (!field.contains(voxel)) return;
  const vec3 w = field.voxel_center(voxel).v;
  constexpr vec3 up{0.0F, 1.0F, 0.0F};

  // The nearest bore's signed distance (distance to the axis minus the radius,
  // negative inside). Each bore is a segment from its opening down into the
  // ground at its own grade.
  float carve = 1.0e30F;
  for (int i = 0; i < count; ++i) {
    const float theta = static_cast<float>(i + 1) * angle_step;
    const vec3 dir = (bore_dir * cosf(theta)) - (up * sinf(theta));
    const vec3 start =
        row_origin.v + (row_dir * (spacing * static_cast<float>(i)));
    const float t = fminf(fmaxf(dot(w - start, dir), 0.0F), bore_length);
    carve = fminf(carve, length(w - (start + (dir * t))) - radius);
  }

  float density = 0.0F;
  surf3Dread(&density, density_surface,
      voxel.x * static_cast<int>(sizeof(float)), voxel.y, voxel.z);
  if (carve < density) // open air where the bore cuts into solid
    surf3Dwrite(carve, density_surface,
        voxel.x * static_cast<int>(sizeof(float)), voxel.y, voxel.z);
}

// Carve the test-tunnel fan (`dig_tunnels_kernel`) into the geometry density.
// Synchronous, a one-shot user action; only the density grid changes.
inline void dig_tunnels(const density_field& field,
    const cuda_volume<float>& volume, pos3 row_origin, vec3 bore_dir,
    vec3 row_dir, float spacing, float radius, float bore_length, int count,
    float angle_step) {
  const cudaExtent extent = field.extent;
  const dim3 block{8, 8, 8};
  const dim3 grid{cuda_kernel::ceil_div(extent.width, block.x),
      cuda_kernel::ceil_div(extent.height, block.y),
      cuda_kernel::ceil_div(extent.depth, block.z)};
  dig_tunnels_kernel<<<grid, block>>>(volume.surface(), field, row_origin,
      bore_dir, row_dir, spacing, radius, bore_length, count, angle_step);
  cuda_last_status{cudaDeviceSynchronize()}.or_throw();
}

#pragma endregion

} // namespace corvid::cuda
