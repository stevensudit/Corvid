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

// Procedural terrain from value-noise fractal Brownian motion: a stack of pure
// `__device__` noise functions and the world-space height field they drive.
// The height at a horizontal (x, z) gives the terrain surface, against which a
// voxel density field is filled (solid below, air above).

namespace corvid::cuda::terrain {

#pragma region Noise

// Hash of a lattice point to a pseudo-random value in [0, 1).
[[nodiscard]] __device__ inline float hash2(float x, float y) {
  const float h = sinf((x * 127.1F) + (y * 311.7F)) * 43758.547F;
  return h - floorf(h);
}

// Smooth value noise over the integer lattice, in [0, 1).
[[nodiscard]] __device__ inline float value_noise(float x, float y) {
  const float xi = floorf(x);
  const float yi = floorf(y);
  const float xf = x - xi;
  const float yf = y - yi;
  // Smoothstep weights, so the lattice cells blend without creases.
  const float u = xf * xf * (3.0F - (2.0F * xf));
  const float v = yf * yf * (3.0F - (2.0F * yf));
  const float a = hash2(xi, yi);
  const float b = hash2(xi + 1.0F, yi);
  const float c = hash2(xi, yi + 1.0F);
  const float d = hash2(xi + 1.0F, yi + 1.0F);
  const float ab = a + (u * (b - a));
  const float cd = c + (u * (d - c));
  return ab + (v * (cd - ab));
}

// Fractal value noise: octaves at halving amplitude and doubling frequency.
[[nodiscard]] __device__ inline float fbm(float x, float y) {
  float sum = 0.0F;
  float amplitude = 0.5F;
  float frequency = 1.0F;
  for (int octave = 0; octave < 4; ++octave) {
    sum += amplitude * value_noise(x * frequency, y * frequency);
    amplitude *= 0.5F;
    frequency *= 2.0F;
  }
  return sum;
}

// Ridged fractal noise: octaves of inverted, squared value noise, so the field
// peaks along sharp crests (ridgelines and dune spines) rather than rounded
// bumps. In [0, ~1).
[[nodiscard]] __device__ inline float ridged(float x, float y) {
  float sum = 0.0F;
  float amplitude = 0.5F;
  float frequency = 1.0F;
  for (int octave = 0; octave < 4; ++octave) {
    const float n = value_noise(x * frequency, y * frequency);
    const float r = 1.0F - fabsf((2.0F * n) - 1.0F);
    sum += amplitude * r * r;
    amplitude *= 0.5F;
    frequency *= 2.0F;
  }
  return sum;
}

// Hash of a 3D lattice point to a pseudo-random value in [0, 1).
[[nodiscard]] __device__ inline float hash3(float x, float y, float z) {
  const float h = sinf((x * 127.1F) + (y * 311.7F) + (z * 74.7F)) * 43758.547F;
  return h - floorf(h);
}

// Smooth 3D value noise over the integer lattice, in [0, 1).
[[nodiscard]] __device__ inline float
value_noise_3d(float x, float y, float z) {
  const float xi = floorf(x);
  const float yi = floorf(y);
  const float zi = floorf(z);
  const float xf = x - xi;
  const float yf = y - yi;
  const float zf = z - zi;
  // Smoothstep weights, so the lattice cells blend without creases.
  const float u = xf * xf * (3.0F - (2.0F * xf));
  const float v = yf * yf * (3.0F - (2.0F * yf));
  const float w = zf * zf * (3.0F - (2.0F * zf));
  const float c000 = hash3(xi, yi, zi);
  const float c100 = hash3(xi + 1.0F, yi, zi);
  const float c010 = hash3(xi, yi + 1.0F, zi);
  const float c110 = hash3(xi + 1.0F, yi + 1.0F, zi);
  const float c001 = hash3(xi, yi, zi + 1.0F);
  const float c101 = hash3(xi + 1.0F, yi, zi + 1.0F);
  const float c011 = hash3(xi, yi + 1.0F, zi + 1.0F);
  const float c111 = hash3(xi + 1.0F, yi + 1.0F, zi + 1.0F);
  const float x00 = c000 + (u * (c100 - c000));
  const float x10 = c010 + (u * (c110 - c010));
  const float x01 = c001 + (u * (c101 - c001));
  const float x11 = c011 + (u * (c111 - c011));
  const float y0 = x00 + (v * (x10 - x00));
  const float y1 = x01 + (v * (x11 - x01));
  return y0 + (w * (y1 - y0));
}

// Fractal 3D value noise: octaves at halving amplitude and doubling frequency.
[[nodiscard]] __device__ inline float fbm_3d(float x, float y, float z) {
  float sum = 0.0F;
  float amplitude = 0.5F;
  float frequency = 1.0F;
  for (int octave = 0; octave < 3; ++octave) {
    sum += amplitude *
           value_noise_3d(x * frequency, y * frequency, z * frequency);
    amplitude *= 0.5F;
    frequency *= 2.0F;
  }
  return sum;
}

#pragma endregion
#pragma region Height

// World height of the terrain surface at horizontal position (`x`, `z`).
// Domain-warped so features bend and flow (wind- and water-carved) rather than
// reading as isotropic noise, with ridged crests for dune spines over a
// rolling fbm base. Steep crests are expected to be slumped to a stable angle
// by a later erosion pass.
[[nodiscard]] __device__ inline float height(float x, float z) {
  constexpr float base = -3.0F;
  constexpr float scale = 0.035F;      // feature scale (larger = broader)
  constexpr float warp_scale = 0.015F; // frequency of the warp noise
  constexpr float warp = 15.0F;        // warp displacement, world units
  constexpr float dune_amp = 8.0F;     // ridged crest height
  constexpr float roll_amp = 7.0F;     // rolling-hill height

  // Displace the sample point by a low-frequency noise (domain warping).
  const float wx = fbm(x * warp_scale, z * warp_scale) - 0.5F;
  const float wz =
      fbm((x * warp_scale) + 4.7F, (z * warp_scale) + 2.1F) - 0.5F;
  const float sx = (x + (warp * wx)) * scale;
  const float sz = (z + (warp * wz)) * scale;

  return base + (dune_amp * ridged(sx, sz)) + (roll_amp * fbm(sx, sz));
}

#pragma endregion

} // namespace corvid::cuda::terrain
