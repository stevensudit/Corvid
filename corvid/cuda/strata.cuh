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

#include "./vec.cuh"

// The hardness strata of the voxel world: the material tier laid down at each
// depth below the surface, and the albedo each tier shows.

namespace corvid::cuda::strata {

#pragma region Strata

// The hardness tier of material at `depth` below the terrain surface (the
// solid voxel's density, in world units). Deeper is harder, laid down once at
// fill time so digging reveals the bands. Stored as the material grid value;
// objects (a band above these) come later. See voxel_world.md.
[[nodiscard]] __device__ inline uint16_t tier_for_depth(float depth) {
  if (depth < 2.0F) return 0;  // topsoil
  if (depth < 6.0F) return 1;  // dirt
  if (depth < 14.0F) return 2; // clay
  return 3;                    // rock
}

// The albedo of a hardness tier, one distinct color per band.
[[nodiscard]] __device__ inline vec3 tier_color(uint16_t tier) {
  const vec3 palette[4] = {
      {0.45F, 0.34F, 0.20F}, // topsoil
      {0.34F, 0.25F, 0.16F}, // dirt
      {0.50F, 0.30F, 0.24F}, // clay
      {0.46F, 0.47F, 0.50F}, // rock
  };
  return palette[tier < 4 ? tier : 3];
}

#pragma endregion

} // namespace corvid::cuda::strata
