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
#include <cstddef>
#include <span>

#include "../containers/utils/matrix_view.h"
#include "llm_ops.h"
#include "token_id.h"

// The GPT-2 architecture on the CPU, in fp32.
//
// The parameter and activation bundles of one block, the block itself, and
// the forward pass from token IDs through every block to the final layer
// norm. The ops it composes live in "llm_ops.h".
namespace corvid::llm {

#pragma region block

// The epsilon GPT-2 adds to the variance inside every layer norm.
inline constexpr float layer_norm_eps = 1e-5F;

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
// The names follow the dump points in "gpt-2.md", so every one of them can
// be read after the block returns. While storage may be distinct, so as to
// allow inspection at every point, if the caller doesn't plan to inspect them
// then some of the buffers may share memory. Specifically, `ln_1_out` can
// share with `ln_2_out`, and `attn_out` with `mlp_out`. `ln_2_in` may be the
// block's `in` or `out` view, which is the in-place form.
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
// share storage only as `block_activations` allows; in particular, no
// buffer may overlap the residual it is later added to.
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

  layer_norm(acts.ln_1_out, in, params.ln_1_weight, params.ln_1_bias,
      layer_norm_eps);
  linear_projection(acts.qkv, acts.ln_1_out, params.attn_c_attn_weight,
      params.attn_c_attn_bias);
  attention(acts.heads_out, acts.qkv, head_count, acts.scores);
  linear_projection(acts.attn_out, acts.heads_out, params.attn_c_proj_weight,
      params.attn_c_proj_bias);
  add(acts.ln_2_in, in, acts.attn_out);

  layer_norm(acts.ln_2_out, acts.ln_2_in, params.ln_2_weight, params.ln_2_bias,
      layer_norm_eps);
  linear_projection(acts.hidden, acts.ln_2_out, params.mlp_c_fc_weight,
      params.mlp_c_fc_bias);
  gelu_new(acts.hidden, acts.hidden);
  linear_projection(acts.mlp_out, acts.hidden, params.mlp_c_proj_weight,
      params.mlp_c_proj_bias);
  add(out, acts.ln_2_in, acts.mlp_out);
}

#pragma endregion
#pragma region forward

// The parameters of the whole model, as views over the weight file.
//
// For GPT-2, with V = 50257 vocabulary entries, a context of 1024, C = 768,
// and L = 12 blocks:
//
//   wte                     [V, C]     the token embedding, reused by the head
//   wpe                     [1024, C]  the position embedding
//   blocks                  [L]        one `block_params` per `h.N`, in order
//   ln_f_weight, ln_f_bias  [C]        the final layer norm
struct gpt2_params {
  const_float_matrix_view wte;
  const_float_matrix_view wpe;
  std::span<const block_params> blocks;
  const_float_row_span ln_f_weight;
  const_float_row_span ln_f_bias;
};

// Run the model over `ids`, writing the final layer norm's output to `out`.
//
// This is everything before the head: the residual stream starts as the
// token embedding plus the position embedding of each row, every block adds
// to it in place, and `ln_f` normalizes what leaves the last block. For
// GPT-2 with T tokens:
//
// step          |  reads          |  produces
// --------------+-----------------+-------------------
// embed_tokens  |  ids [T]        |  out [T, 768]
// add           |  out, wpe [T:]  |  out, in place
// block, x 12   |  out            |  out, in place
// ln_f          |  out            |  out, in place
//
// where `wpe [T:]` is the first T rows of the position table, so there must
// be no more IDs than it has rows.
//
// `out` doubles as the residual, so it must have one row per ID and the
// model's width. `acts` is reused by every block, so it holds the last
// block's activations on return; its `ln_2_in` may be `out`, the in-place
// form, but no other buffer may overlap `out`.
inline void forward(float_matrix_view out, std::span<const token_id> ids,
    const gpt2_params& params, const block_activations& acts,
    size_t head_count) noexcept {
  using row_ndx = float_matrix_view::row_ndx;
  using col_ndx = float_matrix_view::col_ndx;

  assert(ids.size() <= params.wpe.row_extent());

  embed_tokens(out, ids, params.wte);
  out += params.wpe.subview({row_ndx{0}, col_ndx{0}},
      {.row_count = ids.size(), .col_count = params.wpe.col_extent()});
  for (const auto& block_params : params.blocks)
    block(out, out, block_params, acts, head_count);
  layer_norm(out, out, params.ln_f_weight, params.ln_f_bias, layer_norm_eps);
}

#pragma endregion

} // namespace corvid::llm
