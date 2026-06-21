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

#pragma endregion
#pragma region Height

// World height of the terrain surface at horizontal position (`x`, `z`).
[[nodiscard]] __device__ inline float height(float x, float z) {
  constexpr float scale = 0.06F;
  constexpr float amplitude = 10.0F;
  constexpr float base = -3.0F;
  return base + (amplitude * fbm(x * scale, z * scale));
}

#pragma endregion

} // namespace corvid::cuda::terrain
