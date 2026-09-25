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

#include <vector>

#include "corvid/cuda/bfloat16.cuh"

// Host-side conversions between `float` and `bfloat16_t` vectors, for tests
// that build bf16 operands from float literals and read bf16 results back.
namespace corvid::tests {

// The values narrowed to `bfloat16_t`.
inline std::vector<corvid::cuda::bfloat16_t> narrowed(
    const std::vector<float>& values) {
  std::vector<corvid::cuda::bfloat16_t> result;
  result.reserve(values.size());
  for (const auto value : values) result.emplace_back(value);
  return result;
}

// The values widened to `float`.
inline std::vector<float> widened(
    const std::vector<corvid::cuda::bfloat16_t>& values) {
  std::vector<float> result;
  result.reserve(values.size());
  for (const auto value : values) result.push_back(static_cast<float>(value));
  return result;
}

} // namespace corvid::tests
