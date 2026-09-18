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

#include "../../containers/utils/matrix_view.h"
#include "../../meta/containers.h"
#include "../../meta/crossplatform.h"
#include "gpt2_tokenizer.h"

// The GPT-2 forward pass on the CPU, in fp32, one free function per op.
//
// Every op writes into a caller-owned output view. Activations are
// `matrix_view` rows of features, one row per token, and parameters are
// spans over the weight file. Shape mismatches are contract violations
// (asserted).
namespace corvid::llm {

// The loop idiom of this file.
using std::views::zip;

#pragma region Reductions

// Reductions over a row of features, kept here until a second consumer earns
// them a home in `corvid/math`.

// The sum of `values`.
[[nodiscard]] constexpr float sum(const_float_span values) noexcept {
  float total{};
  for (const auto x : values) total += x;
  return total;
}

// The arithmetic mean of `values`, NaN when empty.
//
// cl C4723: Division by zero is intentionally allowed.
PRAGMA_DIAG(push)
PRAGMA_MSVC_IGNORED(4723)
[[nodiscard]] constexpr float mean(const_float_span values) noexcept {
  return sum(values) / static_cast<float>(values.size());
}
PRAGMA_DIAG(pop)

// The sum of the squared deviations of `values` from `center`.
[[nodiscard]] constexpr float
squared_deviation_sum(const_float_span values, float center) noexcept {
  float total{};
  for (const auto x : values) {
    const auto deviation = x - center;
    total += deviation * deviation;
  }
  return total;
}

// The biased variance of `values` around their `mean`. It's biased because the
// squared deviations are divided by the count, not the count minus one (in
// other words, Bessel's correction is not applied). NaN when empty.
//
// cl C4723: as for `mean`.
PRAGMA_DIAG(push)
PRAGMA_MSVC_IGNORED(4723)
[[nodiscard]] constexpr float
variance(const_float_span values, float mean) noexcept {
  return squared_deviation_sum(values, mean) /
         static_cast<float>(values.size());
}
PRAGMA_DIAG(pop)

// The reciprocal of the standard deviation of `values` around their `mean`,
// with `eps` added to the variance inside the square root.
[[nodiscard]] inline float
inverse_std_dev(const_float_span values, float mean, float eps) noexcept {
  return 1.0F / std::sqrt(variance(values, mean) + eps);
}

// The dot product of `a` and `b`, which must be the same size.
[[nodiscard]] constexpr float
dot(const_float_span a, const_float_span b) noexcept {
  assert(a.size() == b.size());

  float total{};
  for (const auto [x, y] : zip(a, b)) total += x * y;
  return total;
}

#pragma endregion
#pragma region Elementwise

// The z-score of `x`, which is its distance from `mean` in units of the
// standard deviation (which is the reciprocal of `inv_std`).
[[nodiscard]] constexpr float
standardize(float x, float mean, float inv_std) noexcept {
  return (x - mean) * inv_std;
}

// `x` scaled by `weight`, then shifted by `bias`.
[[nodiscard]] constexpr float
scale_shift(float x, float weight, float bias) noexcept {
  return (x * weight) + bias;
}

// Add `scale` times each of `values` to the matching element of `acc`.
// Conceptually: `acc += scale * values`
//
// BLAS calls this operation BLAS axpy, because it's "a times x plus y".
constexpr void
add_scaled(float_span acc, float scale, const_float_span values) noexcept {
  assert(acc.size() == values.size());

  for (auto [acc_value, value] : zip(acc, values)) acc_value += scale * value;
}

#pragma endregion
#pragma region layer_norm

// The epsilon GPT-2 adds to the variance before the square root.
inline constexpr float layer_norm_eps = 1e-5F;

// Normalize `row_in` to a mean of 0 and a variance of 1, into `row_out`.
//
// The variance is biased (divided by the size, not the size minus one), with
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
#pragma region linear

// Project each row of `in` through `weight` and add `bias`, into `out`.
//
// Each row of `in` holds the input features of one token, and the same row of
// `out` receives that token's output features. However, the number of features
// for a token may differ on the `in` and `out` sides, with the actual values
// depending on which projection is being applied:
//
// call         |  in features (i)  |  out features (j)  |  factor
// -------------+-------------------+--------------------+---------
// c_attn       |       768         |      2304          |    3
// attn c_proj  |       768         |       768          |    1
// c_fc         |       768         |      3072          |    4
// mlp c_proj   |       3072        |       768          |    1/4
//
// Every output value is a weighted sum of the token's input features, plus a
// bias, with the weights for output feature `j` in column `j` of `weight`:
//
//   out[t][j] = bias[j] + sum over i of in[t][i] * weight[i][j]
//
// The three indices each play one part:
//
//   t  token           row of `in`, row of `out`
//   i  input feature   column of `in`, row of `weight`
//   j  output feature  column of `weight`, column of `out`, index of `bias`
//
// The summed index, `i`, is the one the operands must agree on, so `in` has as
// many columns as `weight` has rows. The surviving indices label `out`, which
// has a row per token and a column per output feature. In matrix terms, this
// is: `out = in * weight + bias`, with `bias` added to every row.
//
// Consider a column `j` of `weight` as a direction. The value `out[t][j]` is
// the dot product of the token's feature vector with that direction, plus a
// bias. When the two vectors point the same way, the dot product is large and
// positive. So each output feature answers one question: How much does this
// token resemble learned pattern `j`?
//
// `weight` is laid out as GPT-2 stores its projections (Conv1D): one row per
// input feature and one column per output feature. That is the transpose of
// nn.Linear, which keeps one row per output feature.
//
// `out` must not overlap `in`, `weight`, or `bias`.
inline void linear(float_matrix_view out, const_float_matrix_view in,
    const_float_matrix_view weight, const_float_row_span bias) noexcept {
  assert(weight.row_extent() == in.col_extent());
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == weight.col_extent()));
  assert(bias.size() == weight.col_extent());
  assert(is_disjoint(out.as_span(), in.as_span()));
  assert(is_disjoint(out.as_span(), weight.as_span()) &&
         is_disjoint(out.as_span(), bias));

  // Loop over each row of the input matrix, which corresponds to a token, and
  // in which each column contains a feature, together with the row of the
  // output matrix that will hold the projected features for the same token.
  for (const auto [out_row, in_row] : zip(out.rows(), in.rows())) {
    // We start by adding in the biases for this token.
    std::ranges::copy(bias, out_row.begin());

    // We then sum up the scaled contributions of each input feature to the
    // output features. Input feature `i` pairs with row `i` of the weight.
    for (const auto [in_value, weight_row] : zip(in_row, weight.rows()))
      add_scaled(out_row, in_value, weight_row);
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
[[nodiscard]] inline float gelu_new(float x) noexcept {
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
#pragma region softmax

// Turn the scores in `row_in` into weights that sum to 1, into `row_out`.
//
// Each weight is the exponential of its score divided by the sum of all the
// exponentials, so a larger score gets a larger share and the gaps between
// scores are sharpened. Large scores do not overflow.
//
// `row_out` and `row_in` must be the same size, and can refer to the same
// memory. Both must be non-empty.
inline void softmax_row(float_span row_out, const_float_span row_in) noexcept {
  assert(row_out.size() == row_in.size());
  assert(is_same_or_disjoint(row_out, row_in));
  assert(!row_in.empty());

  // Shifting every score by the same amount leaves the weights unchanged, and
  // shifting by the maximum keeps `exp` at or below 1.
  const auto peak = std::ranges::max(row_in);
  float total{};
  for (auto [out_value, in_value] : zip(row_out, row_in)) {
    out_value = std::exp(in_value - peak);
    total += out_value;
  }
  for (auto& weight : row_out) weight /= total;
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
      weight = dot(query, key) * scale;

    softmax_row(weights, weights);

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
#pragma region add

// Add `a` and `b` elementwise, into `out`.
//
// This is the residual add. Each of a block's two sublayers reads the
// residual stream through a layer norm and produces a correction the same
// shape as the stream, and this op adds that correction back onto it. For
// GPT-2 with T tokens:
//
// step        |  a                  |  b                  |  produces
// ------------+---------------------+---------------------+------------------
// after attn  |  residual [T, 768]  |  attn/out [T, 768]  |  residual [T, 768]
// after mlp   |  residual [T, 768]  |  mlp/out  [T, 768]  |  residual [T, 768]
//
// All three views must have the same extent. `out` can be the same view as
// `a` or as `b`, adding in place, but must not otherwise overlap either.
inline void add(float_matrix_view out, const_float_matrix_view a,
    const_float_matrix_view b) noexcept {
  assert((a.row_extent() == b.row_extent()) &&
         (a.col_extent() == b.col_extent()));
  assert((out.row_extent() == a.row_extent()) &&
         (out.col_extent() == a.col_extent()));
  assert(is_same_or_disjoint(out.as_span(), a.as_span()));
  assert(is_same_or_disjoint(out.as_span(), b.as_span()));

  for (const auto [out_row, a_row, b_row] :
      zip(out.rows(), a.rows(), b.rows()))
    for (auto [out_value, a_value, b_value] : zip(out_row, a_row, b_row))
      out_value = a_value + b_value;
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
    const auto token_row = wte.row_as_span(row_ndx{*id});
    // Loop over features for each token.
    for (auto [out_value, token_value, position_value] :
        zip(out_row, token_row, position_row))
      out_value = token_value + position_value;
  }
}

#pragma endregion
#pragma region block

// The parameters of one block, as views over the weight file.
//
// The names follow the tensor names under `h.N`, with the sublayer prefixed
// so the two `c_proj` projections can be told apart. For GPT-2, with C = 768
// and F = 3072:
//
//   ln_1_weight, ln_1_bias                [C]
//   attn_c_attn_weight, attn_c_attn_bias  [C, 3C], [3C]
//   attn_c_proj_weight, attn_c_proj_bias  [C, C], [C]
//   ln_2_weight, ln_2_bias                [C]
//   mlp_c_fc_weight, mlp_c_fc_bias        [C, F], [F]
//   mlp_c_proj_weight, mlp_c_proj_bias    [F, C], [C]
struct block_params {
  const_float_row_span ln_1_weight;
  const_float_row_span ln_1_bias;
  const_float_matrix_view attn_c_attn_weight;
  const_float_row_span attn_c_attn_bias;
  const_float_matrix_view attn_c_proj_weight;
  const_float_row_span attn_c_proj_bias;
  const_float_row_span ln_2_weight;
  const_float_row_span ln_2_bias;
  const_float_matrix_view mlp_c_fc_weight;
  const_float_row_span mlp_c_fc_bias;
  const_float_matrix_view mlp_c_proj_weight;
  const_float_row_span mlp_c_proj_bias;
};

// The intermediate activations of `block`, as caller-owned views.
//
// For T tokens of width C and MLP width F, in the order written:
//
//   ln_1_out   [T, C]   the first layer norm's output
//   qkv        [T, 3C]  the `attn.c_attn` output
//   heads_out  [T, C]   the attention output before `attn.c_proj`
//   attn_out   [T, C]   the attention sublayer's correction
//   ln_2_in    [T, C]   the residual after the attention add
//   ln_2_out   [T, C]   the second layer norm's output
//   hidden     [T, F]   the MLP's widened rows
//   mlp_out    [T, C]   the MLP sublayer's correction
//   scores     [T]      the attention scratch
//
// Buffers for intermediate activations within a block.
//
// The names follow the dump points in "gpt-2.md", so every one of them can
// be read after the block returns. While storage may be distinct, so as to
// allow inspection at every point, if the caller doesn't plan to inspect them
// then some of buffers may share memory. Specifically, `ln_1_out` can share
// with `ln_2_out`, and `attn_out` with `mlp_out`. `ln_2_in` may be the block's
// `in` or `out` view, which is the in-place form.
struct block_activations {
  float_matrix_view ln_1_out;
  float_matrix_view qkv;
  float_matrix_view heads_out;
  float_matrix_view attn_out;
  float_matrix_view ln_2_in;
  float_matrix_view ln_2_out;
  float_matrix_view hidden;
  float_matrix_view mlp_out;
  float_col_span scores;
};

// Run one block over the residual, reading `in` and writing `out`.
//
// Each sublayer reads the residual through its layer norm, computes a
// correction of the same shape, and adds it back. For GPT-2 with T tokens:
//
// step        |  reads              |  produces
// ------------+---------------------+---------------------
// ln_1        |  in [T, 768]        |  ln_1_out [T, 768]
// attn.c_attn |  ln_1_out           |  qkv [T, 2304]
// attention   |  qkv                |  heads_out [T, 768]
// attn.c_proj |  heads_out          |  attn_out [T, 768]
// add         |  in, attn_out       |  ln_2_in [T, 768]
// ln_2        |  ln_2_in            |  ln_2_out [T, 768]
// mlp.c_fc    |  ln_2_out           |  hidden [T, 3072]
// gelu_new    |  hidden             |  hidden, in place
// mlp.c_proj  |  hidden             |  mlp_out [T, 768]
// add         |  ln_2_in, mlp_out   |  out [T, 768]
//
// `out` and `in` must have the same extent and may be the same view. The
// activation views must have the extents above for that extent and may
// share storage only as `block_activations` allows. A buffer that overlaps
// the residual it is later added to is asserted against, since the ops'
// own checks would pass it. Parameter shapes are asserted by the ops.
//
// Note that the `const` on `acts` is shallow.
inline void block(float_matrix_view out, const_float_matrix_view in,
    const block_params& params, const block_activations& acts,
    size_t head_count) noexcept {
  // Each of these four writes is followed by an add that reads the residual
  // it would have destroyed; the ops' same-or-disjoint checks allow all
  // four, and the ops catch every other overlap.
  assert(is_disjoint(in.as_span(), acts.ln_1_out.as_span()));
  assert(is_disjoint(in.as_span(), acts.attn_out.as_span()));
  assert(is_disjoint(acts.ln_2_in.as_span(), acts.ln_2_out.as_span()));
  assert(is_disjoint(acts.ln_2_in.as_span(), acts.mlp_out.as_span()));

  layer_norm(acts.ln_1_out, in, params.ln_1_weight, params.ln_1_bias);
  linear(acts.qkv, acts.ln_1_out, params.attn_c_attn_weight,
      params.attn_c_attn_bias);
  attention(acts.heads_out, acts.qkv, head_count, acts.scores);
  linear(acts.attn_out, acts.heads_out, params.attn_c_proj_weight,
      params.attn_c_proj_bias);
  add(acts.ln_2_in, in, acts.attn_out);

  layer_norm(acts.ln_2_out, acts.ln_2_in, params.ln_2_weight,
      params.ln_2_bias);
  linear(acts.hidden, acts.ln_2_out, params.mlp_c_fc_weight,
      params.mlp_c_fc_bias);
  gelu_new(acts.hidden, acts.hidden);
  linear(acts.mlp_out, acts.hidden, params.mlp_c_proj_weight,
      params.mlp_c_proj_bias);
  add(out, acts.ln_2_in, acts.mlp_out);
}

#pragma endregion
} // namespace corvid::llm
