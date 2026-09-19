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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <ranges>
#include <span>

#include "../linalg/linear_algebra.h"
#include "../meta/containers.h"
#include "../meta/crossplatform.h"
#include "token_id.h"

// The transformer ops on the CPU, in fp32, one free function per op.
//
// Every op writes into a caller-owned output view. Activations are
// `matrix_view` rows of features, one row per token, and parameters are
// spans over the weight file. Shape mismatches are contract violations.
namespace corvid::llm {

using namespace corvid::linalg;

// The loop idiom of this file.
using std::views::zip;

#pragma region layer_norm

// The epsilon GPT-2 adds to the variance before the square root.
inline constexpr float layer_norm_eps = 1e-5F;

// Normalize `row_in` to a mean of 0 and a variance of 1, into `row_out`.
//
// The variance is the population variance (divided by the size), with
// `eps` added inside the square root. `row_out` and `row_in` must be the same
// size, and can refer to the same memory.
inline void standardize_row(float_row_span row_out,
    const_float_row_span row_in, float eps) noexcept {
  assert(row_out.size() == row_in.size());
  assert(is_same_or_disjoint(row_out, row_in));

  const auto row_mean = mean(row_in);
  const auto inv_std = inverse_std_dev(row_in, row_mean, eps);
  for (auto [out_value, in_value] : zip(row_out, row_in))
    out_value = standardize(in_value, row_mean, inv_std);
}

// Scale `row_in` by `weight` and shift by `bias`, elementwise, into
// `row_out`.
//
// All four spans must be the same size, and `row_out` and `row_in` can refer
// to the same memory.
constexpr void scale_shift_row(float_row_span row_out,
    const_float_row_span row_in, const_float_row_span weight,
    const_float_row_span bias) noexcept {
  assert(row_out.size() == row_in.size());
  assert(is_same_or_disjoint(row_out, row_in));
  assert((weight.size() == row_in.size()) && (bias.size() == row_in.size()));

  for (auto [out_value, in_value, w, b] : zip(row_out, row_in, weight, bias))
    out_value = scale_shift(in_value, w, b);
}

// Normalize each row of `in` to a mean of 0 and a variance of 1, then scale by
// `weight` and shift by `bias`, elementwise, into `out`, performing a diagonal
// affine transformation.
//
// This is `standardize_row` followed by `scale_shift_row`, one row at a time
// so that the row is still in cache for the second step.
//
// `out` and `in` must have the same extent, and its width must be the size of
// `weight` and `bias`. `out` can be the same view as `in`, normalizing in
// place, but must not otherwise overlap it.
inline void layer_norm(float_matrix_view out, const_float_matrix_view in,
    const_float_row_span weight, const_float_row_span bias,
    float eps = layer_norm_eps) noexcept {
  [[maybe_unused]] const auto width = in.col_extent();
  assert((out.row_extent() == in.row_extent()) && (out.col_extent() == width));
  assert((weight.size() == width) && (bias.size() == width));
  assert(is_same_or_disjoint(out.as_span(), in.as_span()));
  assert(is_disjoint(out.as_span(), weight));
  assert(is_disjoint(out.as_span(), bias));

  for (const auto [out_row, in_row] : zip(out.rows(), in.rows())) {
    standardize_row(out_row, in_row, eps);
    scale_shift_row(out_row, out_row, weight, bias);
  }
}

#pragma endregion
#pragma region gelu_new

// The `sqrt(2 / pi)` that scales the argument of the tanh.
inline constexpr float gelu_tanh_scale =
    std::numbers::sqrt2_v<float> * std::numbers::inv_sqrtpi_v<float>;

// The coefficient of the cubic term inside the tanh, from the original
// paper's fit of the tanh form to the exact one.
inline constexpr float gelu_cubic_coeff = 0.044715F;

// The GELU (Gaussian Error Linear Unit) of `x`, in the tanh form that GPT-2
// was trained with.
//
// GELU is `x` times the probability that a standard normal draw is below `x`.
// So it passes large positive inputs through unchanged, squashes large
// negative ones to zero, and bends smoothly between the two, dipping a little
// below zero on the way. The exact probability needs `erf`. This form
// replaces it with a tanh of a cubic, which is close but not identical:
//
//   0.5 * x * (1 + tanh(sqrt(2 / pi) * (x + 0.044715 * x^3)))
//
// The weights were fit to this curve, so this is the form that matches the
// oracle, and the `erf` form would not. Not `constexpr` because it uses
// `std::tanh`.
[[nodiscard]] CUDA_HOST_DEVICE inline float gelu_new(float x) noexcept {
  const auto inner = gelu_tanh_scale * (x + (gelu_cubic_coeff * x * x * x));
  return 0.5F * x * (1.0F + std::tanh(inner));
}

// Apply `gelu_new` to every element of `in`, into `out`.
//
// GPT-2 applies it once per block, to the 3072-wide rows that `c_fc`
// produces, before `mlp c_proj` projects them back down to 768. It is the
// only nonlinearity in the MLP, and without it the two projections would
// collapse into one.
//
// `out` and `in` must have the same extent. `out` can be the same view as
// `in`, applying it in place, but must not otherwise overlap it.
inline void
gelu_new(float_matrix_view out, const_float_matrix_view in) noexcept {
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == in.col_extent()));
  assert(is_same_or_disjoint(out.as_span(), in.as_span()));

  for (const auto [out_row, in_row] : zip(out.rows(), in.rows()))
    for (auto [out_value, in_value] : zip(out_row, in_row))
      out_value = gelu_new(in_value);
}

#pragma endregion
#pragma region attention

// Let each token read from the tokens at or before it, for one head.
//
// For T tokens and a head D features wide, the shapes are:
//
//   q, k, v   [T, D]   the head's queries, keys, and values, a row per token
//   out       [T, D]   a row per token, written
//   scores    [T]      scratch for one row of weights, at least T long
//
// In GPT-2, D is 64 and the views are column slices of the `attn.c_attn`
// output. For each token `i`, we compute the dot product of its query,
// `q[i]`, against the keys of all non-subsequent tokens, `k[j]`, where `j`
// ranges from 0 to `i`, inclusive. This is a measure of how relevant that
// non-subsequent token is to the current token. Tokens after `i` get no weight
// at all, which is the causal rule.
//
// We scale the dot product by `1/sqrt(D)` to prevent the scores from growing
// too large with the width, and then apply the softmax to convert the scores
// into probabilities that add up to 1.
//
// These probabilities are the attention weights, and they're used to compute
// the weighted sum of the value rows corresponding to each key. Those weighted
// sums form the output rows.
//
// So, for example, for token `i`, we sum up the value rows for tokens 0
// through `i`, weighing each with the corresponding attention weight. That
// ends up in `out[i]`.
//
// All four views must have the same row count, and `out` must not overlap
// any of the others.
inline void attention_head(float_matrix_view out, const_float_matrix_view q,
    const_float_matrix_view k, const_float_matrix_view v,
    float_col_span scores) noexcept {
  [[maybe_unused]] const auto token_count = q.row_extent();
  const auto width = q.col_extent();
  assert((k.row_extent() == token_count) && (k.col_extent() == width));
  assert((v.row_extent() == token_count) && (v.col_extent() == width));
  assert((out.row_extent() == token_count) && (out.col_extent() == width));
  assert(scores.size() >= token_count);
  assert(is_disjoint(out.as_span(), q.as_span()) &&
         is_disjoint(out.as_span(), k.as_span()) &&
         is_disjoint(out.as_span(), v.as_span()));
  assert(is_disjoint(out.as_span(), scores));
  assert(is_disjoint(scores, v.as_span()));

  const auto scale = 1.0F / std::sqrt(static_cast<float>(width));

  for (const auto [i, query, out_row] :
      zip(std::views::iota(size_t{0}), q.rows(), out.rows()))
  {
    // Only tokens 0 through `i` get a weight, so the mask is the length of
    // `weights`. Zipping it against all the key or value rows stops there.
    const auto weights = scores.first(i + 1);
    for (auto [weight, key] : zip(weights, k.rows()))
      weight = dot_product(query, key) * scale;

    softmax(weights, weights);

    std::ranges::fill(out_row, 0.0F);
    for (const auto [weight, value] : zip(weights, v.rows()))
      add_scaled(out_row, weight, value);
  }
}

// Let each token read from the tokens at or before it, across all
// attention heads. Takes `qkv`, which is the output of the `attn.c_attn`
// projection, and writes the weighted sum to `out`.
//
// The `qkv` matrix has one row per token, which contains its queries, then
// its keys, then its values, each as wide as `out` (which is 768 for GPT-2).
//
// token_row: 768 * q, 768 * k, 768 * v
//
// This matrix is cut up into `q`, `k`, and `v` matrices, each of which is [T,
// 768]. Then each attention head is passed a slice of these columns. In other
// words, the first head gets the first 64 columns of `q`, `k`, and `v`,
// respectively; and so on.
//
// This means that a given head cannot query using the key features from
// another head. However, as each head's inputs were computed by `attn.c_attn`
// from all input features, they still capture information from all of them.
// The heads partition the projection, not the input, and `attn.c_proj`
// afterward mixes their outputs back together.
//
// Each head operates independently on its slice of the queries, keys, and
// values. It writes to the portion of `out` corresponding to its input. In
// other words, the first head writes to the first 64 columns of `out`, and so
// on.
//
// For GPT-2 with T tokens:
//
// step             |  reads                 |  produces
// -----------------+------------------------+----------------------------
// split            |  qkv [T, 2304]         |  q, k, v each [T, 768]
// heads            |  q, k, v [T, 768]      |  12 slices each of [T, 64]
// attention_head   |  [T, 64] q/k/v slices  |  one [T, 64] slice of out
// (all heads)      |                        |  out [T, 768]
//
// where 2304 = 3 x 768 and 64 = 768 / 12.
//
// `scores` is scratch for one row of weights, and must hold at least one
// element per token. `qkv` must be three times as wide as `out`, whose
// width must divide evenly by `head_count`. `out` must not overlap `qkv`
// or `scores`.
inline void attention(float_matrix_view out, const_float_matrix_view qkv,
    size_t head_count, float_col_span scores) noexcept {
  using row_ndx = float_matrix_view::row_ndx;
  using col_ndx = float_matrix_view::col_ndx;

  const auto token_count = out.row_extent();
  const auto width = out.col_extent();
  assert(qkv.row_extent() == token_count);
  assert(qkv.col_extent() == 3 * width);
  assert(head_count && (width % head_count == 0));
  const auto head_width = width / head_count;

  const auto one_third = [&](size_t which) {
    return qkv.subview({row_ndx{0}, col_ndx{which * width}},
        {.row_count = token_count, .col_count = width});
  };
  const auto q = one_third(0);
  const auto k = one_third(1);
  const auto v = one_third(2);

  for (size_t head = 0; head < head_count; ++head) {
    const auto slice = [&](const auto& m) {
      return m.subview({row_ndx{0}, col_ndx{head * head_width}},
          {.row_count = token_count, .col_count = head_width});
    };
    attention_head(slice(out), slice(q), slice(k), slice(v), scores);
  }
}

#pragma endregion
#pragma region embed

// Look up each token's embedding, into `out`.
//
// The residual stream starts here. Row `t` of `out` is the row of `wte`
// indexed by `ids[t]`, the token embedding, plus row `t` of `wpe`, the
// position embedding. Nothing after this step reads the IDs. For GPT-2 with
// T tokens:
//
// step   |  reads    |  looks up                           |  produces
// -------+-----------+-------------------------------------+---------------
// embed  |  ids [T]  |  wte [50257, 768], wpe [1024, 768]  |  out [T, 768]
//
// where 50257 is the vocabulary size and 1024 the context length.
//
// `out` must have one row per ID and the width of both tables, every ID must
// index a row of `wte`, and there must be no more IDs than rows of `wpe`.
// `out` must not overlap either table.
inline void embed(float_matrix_view out, std::span<const token_id> ids,
    const_float_matrix_view wte, const_float_matrix_view wpe) noexcept {
  using row_ndx = float_matrix_view::row_ndx;

  [[maybe_unused]] const auto width = out.col_extent();
  assert(out.row_extent() == ids.size());
  assert((wte.col_extent() == width) && (wpe.col_extent() == width));
  assert(ids.size() <= wpe.row_extent());
  assert(is_disjoint(out.as_span(), wte.as_span()) &&
         is_disjoint(out.as_span(), wpe.as_span()));

  // Loop over token IDs.
  for (const auto [id, out_row, position_row] :
      zip(ids, out.rows(), wpe.rows()))
  {
    assert(*id < wte.row_extent());
    const auto token_row = wte[row_ndx{*id}];
    // Loop over features for each token.
    for (auto [out_value, token_value, position_value] :
        zip(out_row, token_row, position_row))
      out_value = token_value + position_value;
  }
}

#pragma endregion
#pragma region logits

// Score every vocabulary entry as the next token after `features`, into
// `out`.
//
// `features` is one token's row of the final layer norm's output, and each
// score is its dot product with that vocabulary entry's row of `wte`, the
// same table that embedded the input. There is no bias. For GPT-2:
//
//   features  [768]         one row of `ln_f/out`
//   wte       [50257, 768]  the token embedding, read row by row
//   out       [50257]       one logit per vocabulary entry, written
//
// `out` must have one element per row of `wte`, `features` must be as wide
// as `wte`, and `out` must not overlap either.
inline void token_logits(float_row_span out, const_float_row_span features,
    const_float_matrix_view wte) noexcept {
  assert(out.size() == wte.row_extent());
  assert(features.size() == wte.col_extent());
  assert(is_disjoint(out, features) && is_disjoint(out, wte.as_span()));

  for (auto [logit, embedding] : zip(out, wte.rows()))
    logit = dot_product(features, embedding);
}

// Score every vocabulary entry after every token of `in`, into `out`.
//
// Row `t` of `out` is `token_logits` of row `t` of `in`. Generation only
// needs the last row, and calls `token_logits` on it directly; every row is
// what the oracle dumps. For GPT-2 with T tokens:
//
// step          |  reads              |  produces
// --------------+---------------------+------------------
// token_logits  |  one row of `in`    |  one row of out
// (all rows)    |  in [T, 768]        |  out [T, 50257]
//
// `out` and `in` must have the same row count, and the shapes of each row
// are as `token_logits` requires.
inline void logits(float_matrix_view out, const_float_matrix_view in,
    const_float_matrix_view wte) noexcept {
  assert(out.row_extent() == in.row_extent());

  for (const auto [out_row, in_row] : zip(out.rows(), in.rows()))
    token_logits(out_row, in_row, wte);
}

#pragma endregion
#pragma region greedy

// The vocabulary entry with the largest logit, the first on a tie.
//
// This is greedy decoding: the next token is the single most likely one,
// with no sampling. `logits` must not be empty.
[[nodiscard]] inline token_id greedy(const_float_row_span logits) noexcept {
  assert(!logits.empty());

  const auto largest = std::ranges::max_element(logits);
  return token_id{static_cast<uint32_t>(largest - logits.begin())};
}

#pragma endregion

} // namespace corvid::llm
