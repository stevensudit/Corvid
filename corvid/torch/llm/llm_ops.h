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
#include <cstdint>
#include <limits>

#include <ATen/ATen.h>

#include "../../llm/token_id.h"
#include "../tensor_adapter.h"

// The transformer ops of the Llama family over tensors, one free function
// per op.
//
// Every op takes tensors and returns a new one, so autograd records each step
// whenever an input requires grad. Activations have a row of features per
// token, except between `split_heads` and `merge_heads`, where attention sees
// a matrix per head. The ops run in the type of the tensors they are given,
// widening to fp32 only where the published models do.
//
//   const auto normed = rms_norm(residual, block.input_layernorm_weight, eps);
//   const auto q = apply_rope(split_heads(queries, head_count), rope);
//
// Shape mismatches are contract violations, which torch reports by throwing
// `c10::Error`. Nothing here catches it.
namespace corvid::torch {

// A token ID is an `Int` to ATen, whose index ops take no unsigned type, so
// its bits are read as signed. `embed_tokens` keeps that true.
template<>
struct scalar_type_of<corvid::llm::token_id> {
  static constexpr auto value = ScalarType::Int;
};
static_assert(sizeof(corvid::llm::token_id) == sizeof(int32_t));

} // namespace corvid::torch

namespace corvid::torch::llm {

using namespace corvid::llm;

#pragma region embed_tokens

// `embed_tokens` looks up each ID's row of `table`.
//
// With T tokens, V vocabulary entries, and width C:
//
//   ids     [T]     one token ID per row of the result
//   table   [V, C]  a row per vocabulary entry, on the device the result is on
//   result  [T, C]  a row per ID, in `table`'s type
//
// Every ID must index a row of `table`, which must have no more rows than a
// signed 32-bit index reaches.
[[nodiscard]] inline Tensor
embed_tokens(const Tensor& table, token_ids_view ids) {
  // An ID past this many rows would read as a negative index.
  assert(table.size(0) <= std::numeric_limits<int32_t>::max());
  return at::embedding(table, as_tensor(ids).to(table.device()));
}

#pragma endregion
#pragma region rms_norm

// `rms_norm` scales each row of `in` to a root mean square of 1, then scales
// each feature by `weight`.
//
// With T tokens of width C:
//
//   in      [T, C]
//   weight  [C]
//   result  [T, C]  in `in`'s type
//
// Each element `x` of a row becomes `weight * x / sqrt(mean(x * x) + eps)`,
// with the mean over the row. Unlike a layer norm, no mean is subtracted and
// nothing is shifted afterward.
[[nodiscard]] inline Tensor
rms_norm(const Tensor& in, const Tensor& weight, double eps) {
  return at::rms_norm(in, {in.size(-1)}, weight, eps);
}

#pragma endregion
#pragma region rope

// The cosines and sines that rotary position embedding (RoPE) turns a head's
// features by, a row per position.
//
// With P positions and a head D wide:
//
//   cos, sin  [P, D]  the angle of position `p` for feature `i`, which is
//                     the same for `i` and `i + D / 2`
//
// Feature `i` of the first half of a head and feature `i` of the second half
// are a pair, and `apply_rope` turns each pair as a point in the plane. The
// pair at `i` = 0 turns one radian per position, and each later pair turns
// slower, down to nearly `1 / theta` radians per position for the last one,
// so the first pairs tell nearby tokens apart and the last ones tell distant
// tokens apart.
struct rope_tables {
  Tensor cos;
  Tensor sin;

  // Build the tables for positions 0 through `position_count` - 1 of heads
  // `head_width` wide, with base `theta`, as tensors of `options`.
  //
  // The angles are taken in fp32 and narrowed to `options`'s type afterward,
  // which is where the published model narrows them.
  [[nodiscard]] static rope_tables create(int64_t position_count,
      int64_t head_width, double theta, const TensorOptions& options) {
    const auto wide_options = options.dtype(ScalarType::Float);
    // Pair `i` of `head_width / 2` turns `theta ^ (-2 * i / head_width)`
    // radians per position.
    const auto exponents =
        at::arange(0, head_width, 2, wide_options) /
        static_cast<double>(head_width);
    const auto radians_per_position = at::pow(theta, exponents).reciprocal();
    const auto positions = at::arange(position_count, wide_options);
    const auto angles = at::outer(positions, radians_per_position);
    const auto both_halves = at::cat({angles, angles}, -1);
    return {.cos = both_halves.cos().to(options),
        .sin = both_halves.sin().to(options)};
  }
};

// `apply_rope` turns each head's feature pairs by its token's angle.
//
// With H heads over T tokens of D features:
//
//   heads   [H, T, D]  the queries or the keys, a matrix per head
//   tables  [P, D]     from `rope_tables::create`, with P at least T
//   result  [H, T, D]  in `heads`'s type
//
// Row `t` of `heads` is at position `t`, so it turns by row `t` of the tables.
// The dot product of a query at position `i` and a key at position `j`, both
// turned this way, depends only on `j - i`, so the attention scores see how
// far apart two tokens are and nothing about their absolute positions.
[[nodiscard]] inline Tensor
apply_rope(const Tensor& heads, const rope_tables& tables) {
  // The tables run to the last position the model allows, so the first T rows
  // are the ones that broadcast against [H, T, D].
  const auto token_count = heads.size(1);
  const auto cos = tables.cos.narrow(0, 0, token_count);
  const auto sin = tables.sin.narrow(0, 0, token_count);
  // Turning the pair (x1, x2) by `a` gives (x1 cos a - x2 sin a, x2 cos a +
  // x1 sin a). Over a whole row that is `heads * cos + rotated * sin`, where
  // `rotated` puts the second half of each row first and negated, so that
  // (x1, x2) becomes (-x2, x1).
  const auto halves = heads.chunk(2, -1);
  const auto rotated = at::cat({-halves[1], halves[0]}, -1);
  return heads * cos + rotated * sin;
}

#pragma endregion
#pragma region attention

// `split_heads` rearranges a row of features per token into a matrix per head.
//
// With T tokens and H heads of D features:
//
//   in      [T, H * D]  each row holds head 0's features, then head 1's, ...
//   result  [H, T, D]   over `in`'s memory
//
// `in` must be contiguous, as a projection's output is.
[[nodiscard]] inline Tensor split_heads(const Tensor& in, int64_t head_count) {
  return in.view({in.size(0), head_count, -1}).transpose(0, 1);
}

// `merge_heads` rearranges a matrix per head back into a row of features per
// token, undoing `split_heads`.
//
// With H heads over T tokens of D features:
//
//   heads   [H, T, D]
//   result  [T, H * D]  each row holds head 0's features, then head 1's, ...
//
// The result is a copy, since a token's features lie a head apart in `heads`
// and side by side in the result.
[[nodiscard]] inline Tensor merge_heads(const Tensor& heads) {
  return heads.transpose(0, 1).reshape({heads.size(1), -1});
}

// `attend` lets each token read from the tokens at or before it, for every
// head.
//
// With H query heads sharing K key-value heads, over T tokens of D features:
//
//   q       [H, T, D]  the queries, a matrix per head
//   k, v    [K, T, D]  the keys and values, a matrix per key-value head
//   result  [H, T, D]  in `q`'s type
//
// H must be a multiple of K. Query head `h` reads key-value head `h / (H /
// K)`, so each key-value head serves a run of consecutive query heads, which
// is grouped-query attention. The scores are the dot products of each query
// against the keys of the tokens at or before it (scaled by 1 / sqrt(D)),
// taken through the softmax in fp32 whatever `q`'s type, and the result is
// the values weighted by them.
[[nodiscard]] inline Tensor
attend(const Tensor& q, const Tensor& k, const Tensor& v) {
  const auto head_count = q.size(0);
  const auto kv_head_count = k.size(0);
  const auto token_count = q.size(1);
  const auto head_width = q.size(2);
  const auto share_count = head_count / kv_head_count;
  const auto keys = k.repeat_interleave(share_count, 0);
  const auto values = v.repeat_interleave(share_count, 0);

  const auto scale = 1.0 / std::sqrt(static_cast<double>(head_width));
  const auto scores = at::matmul(q, keys.transpose(-2, -1)) * scale;
  // A token sees itself and the tokens before it, so every score above the
  // diagonal is sent to minus infinity, where the softmax weighs it zero.
  const auto future =
      at::ones({token_count, token_count}, q.options().dtype(ScalarType::Bool))
          .triu(1);
  const auto masked =
      scores.masked_fill(future, -std::numeric_limits<float>::infinity());
  const auto weights =
      at::softmax(masked, -1, ScalarType::Float).to(q.scalar_type());
  return at::matmul(weights, values);
}

#pragma endregion
#pragma region swiglu

// `swiglu` gates `up` by `gate`, element by element.
//
// With T tokens of MLP width F:
//
//   gate, up  [T, F]  the two widening projections of the same input
//   result    [T, F]  in their type
//
// Each element of `gate` passes through the SiLU, `x * sigmoid(x)`, and the
// matching element of `up` is scaled by it. The SiLU is near zero for a
// negative `x` and near `x` for a positive one, so `gate` decides how much of
// `up` gets through.
[[nodiscard]] inline Tensor swiglu(const Tensor& gate, const Tensor& up) {
  return at::silu(gate) * up;
}

#pragma endregion

} // namespace corvid::torch::llm
