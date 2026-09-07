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

#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>

#include "../../containers/utils/matrix_view.h"

// The GPT-2 forward pass on the CPU, in fp32, one free function per op.
//
// Every op writes into a caller-owned output view. Activations are
// `matrix_view` rows of features, one row per token, and parameters are
// spans over the weight file. Shape mismatches are contract violations
// (asserted).
namespace corvid::llm {

#pragma region layer_norm

// The epsilon GPT-2 adds to the variance before the square root.
inline constexpr float layer_norm_eps = 1e-5F;

// Normalize each row of `in` to mean 0 and variance 1, then scale by
// `weight` and shift by `bias`, elementwise, into `out`.
//
// The variance is biased (divided by the width, not the width minus one),
// with `eps` added inside the square root. `out` and `in` must have the same
// extent, and its width must be the size of `weight` and `bias` (asserted).
inline void layer_norm(matrix_view<float> out, matrix_view<const float> in,
    std::span<const float> weight, std::span<const float> bias,
    float eps = layer_norm_eps) noexcept {
  const auto width = in.col_extent();
  assert((out.row_extent() == in.row_extent()) && (out.col_extent() == width));
  assert((weight.size() == width) && (bias.size() == width));
  const auto scale = 1.0F / static_cast<float>(width);
  for (const auto r : in.row_interval()) {
    const auto in_row = in.row_span(r);
    float sum{};
    for (const auto x : in_row) sum += x;
    const auto mean = sum * scale;
    float sq_sum{};
    for (const auto x : in_row) sq_sum += (x - mean) * (x - mean);
    const auto inv_std = 1.0F / std::sqrt((sq_sum * scale) + eps);
    const auto out_row = out.row_span(r);
    for (size_t col = 0; col < width; ++col)
      out_row[col] =
          ((in_row[col] - mean) * inv_std * weight[col]) + bias[col];
  }
}

#pragma endregion
} // namespace corvid::llm
