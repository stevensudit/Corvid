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
#include <vector>

#include "../containers/utils/interval.h"
#include "../containers/utils/matrix_view.h"
#include "../meta/containers.h"
#include "gpt2.h"
#include "llm_ops.h"
#include "token_id.h"

// The GPT-2 inference engine on the CPU, in fp32.
//
// `gpt2_engine` runs a `gpt2_model` at three levels. `apply_block` runs one
// block over a residual, `forward` embeds the token IDs and runs every block
// and the final layer norm over them, and `generate` picks greedily on top
// of `forward`. The ops it composes live in "llm_ops.h", and the activations
// of a block are caller-owned views, so every one of them can be inspected.
namespace corvid::llm {

#pragma region gpt2_engine

// The GPT-2 inference engine on the CPU, over a `gpt2_model` that must
// outlive it.
//
// Tokenizing text is `gpt2_tokenizer`'s job, so this takes and produces
// token IDs alone.
//
//   const gpt2_engine engine(model);
//   std::vector<token_id> ids = ...;
//   if (!engine.generate(ids, 20)) ...
class gpt2_engine {
public:
#pragma region block_activations

  // The intermediate activations of `apply_block`, as caller-owned views.
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
  // allow inspection at every point, if the caller doesn't plan to inspect
  // them then some of the buffers may share memory. Specifically, `ln_1_out`
  // can share with `ln_2_out`, and `attn_out` with `mlp_out`. `ln_2_in` may be
  // the block's `in` or `out` view, which is the in-place form.
  //
  // Note that the `const` on this struct is shallow.
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

  // Owned storage for every activation of `apply_block`, all distinct, for
  // `token_count` tokens of width `width` and MLP width `hidden_width`.
  struct block_activation_buffers {
    std::vector<float> ln_1_out;
    std::vector<float> qkv;
    std::vector<float> heads_out;
    std::vector<float> attn_out;
    std::vector<float> ln_2_in;
    std::vector<float> ln_2_out;
    std::vector<float> hidden;
    std::vector<float> mlp_out;
    std::vector<float> scores;
    size_t token_count;
    size_t width;
    size_t hidden_width;

    block_activation_buffers(size_t token_count, size_t width,
        size_t hidden_width)
        : ln_1_out(token_count * width), qkv(token_count * 3 * width),
          heads_out(token_count * width), attn_out(token_count * width),
          ln_2_in(token_count * width), ln_2_out(token_count * width),
          hidden(token_count * hidden_width), mlp_out(token_count * width),
          scores(token_count), token_count{token_count}, width{width},
          hidden_width{hidden_width} {}

    // The views `apply_block` takes.
    [[nodiscard]] block_activations views() noexcept {
      const auto rows = [&](std::vector<float>& storage, size_t cols) {
        return float_matrix_view(storage,
            {.row_count = token_count, .col_count = cols});
      };
      return {
          .ln_1_out = rows(ln_1_out, width),
          .qkv = rows(qkv, 3 * width),
          .heads_out = rows(heads_out, width),
          .attn_out = rows(attn_out, width),
          .ln_2_in = rows(ln_2_in, width),
          .ln_2_out = rows(ln_2_out, width),
          .hidden = rows(hidden, hidden_width),
          .mlp_out = rows(mlp_out, width),
          .scores = scores,
      };
    }
  };

#pragma endregion
#pragma region Construction

  explicit gpt2_engine(const gpt2_model& model) noexcept : model_{model} {}

#pragma endregion
#pragma region Forward pass

  // Run block `block_index` over the residual, reading `in` and writing `out`.
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
  void apply_block(float_matrix_view out, const_float_matrix_view in,
      size_t block_index, const block_activations& acts) const noexcept {
    const auto& params = model_.blocks[block_index];
    // Each of these four writes is followed by an add that reads the residual
    // it would have destroyed; the ops' same-or-disjoint checks allow all
    // four, and the ops catch every other overlap.
    assert(is_disjoint(in.as_span(), acts.ln_1_out.as_span()));
    assert(is_disjoint(in.as_span(), acts.attn_out.as_span()));
    assert(is_disjoint(acts.ln_2_in.as_span(), acts.ln_2_out.as_span()));
    assert(is_disjoint(acts.ln_2_in.as_span(), acts.mlp_out.as_span()));

    layer_norm(acts.ln_1_out, in, params.ln_1_weight, params.ln_1_bias,
        gpt2_model::layer_norm_eps);
    linear_projection(acts.qkv, acts.ln_1_out, params.attn_c_attn_weight,
        params.attn_c_attn_bias);
    attend(acts.heads_out, acts.qkv, model_.head_count, acts.scores);
    linear_projection(acts.attn_out, acts.heads_out, params.attn_c_proj_weight,
        params.attn_c_proj_bias);
    add(acts.ln_2_in, in, acts.attn_out);

    layer_norm(acts.ln_2_out, acts.ln_2_in, params.ln_2_weight,
        params.ln_2_bias, gpt2_model::layer_norm_eps);
    linear_projection(acts.hidden, acts.ln_2_out, params.mlp_c_fc_weight,
        params.mlp_c_fc_bias);
    gelu_new(acts.hidden, acts.hidden);
    linear_projection(acts.mlp_out, acts.hidden, params.mlp_c_proj_weight,
        params.mlp_c_proj_bias);
    add(out, acts.ln_2_in, acts.mlp_out);
  }

  // Run the model over `ids`, writing the final layer norm's output to `out`.
  //
  // This is everything before the head: the residual stream starts as the
  // token embedding plus the position embedding of each row, every block
  // adds to it in place, and `ln_f` normalizes what leaves the last block.
  // For GPT-2 with T tokens:
  //
  // step             |  reads          |  produces
  // -----------------+-----------------+-------------------
  // embed_tokens     |  ids [T]        |  out [T, 768]
  // embed_positions  |  out, wpe [T:]  |  out, in place
  // block, x 12      |  out            |  out, in place
  // ln_f             |  out            |  out, in place
  //
  // where `wpe [T:]` is the first T rows of the position table, so there
  // must be no more IDs than the context holds.
  //
  // `out` doubles as the residual, so it must have one row per ID and the
  // model's width. `acts` is reused by every block, so it holds the last
  // block's activations on return; its `ln_2_in` may be `out`, the in-place
  // form, but no other buffer may overlap `out`.
  void forward(float_matrix_view out, std::span<const token_id> ids,
      const block_activations& acts) const noexcept {
    assert(ids.size() <= model_.context_length());

    embed_tokens(out, ids, model_.wte);
    embed_positions(out, model_.wpe);
    for (const auto block_index : iota(model_.blocks.size()))
      apply_block(out, out, block_index, acts);
    layer_norm(out, out, model_.ln_f_weight, model_.ln_f_bias,
        gpt2_model::layer_norm_eps);
  }

#pragma endregion
#pragma region Generation

  // Pick the token most likely to follow `ids`, writing into `out`.
  //
  // Runs the whole model over `ids`. On failure (no IDs, or more than the
  // context holds), returns false, leaving `out` untouched.
  [[nodiscard]] bool
  next_token(token_id& out, std::span<const token_id> ids) const {
    const auto token_count = ids.size();
    if (!token_count || (token_count > model_.context_length())) return false;
    const auto width = model_.width();

    std::vector<float> trunk_storage(token_count * width);
    const float_matrix_view trunk(trunk_storage,
        {.row_count = token_count, .col_count = width});
    block_activation_buffers buffers(token_count, width,
        model_.hidden_width());
    forward(trunk, ids, buffers.views());

    std::vector<float> logits_storage(model_.vocab_size());
    const float_row_span logits(logits_storage);
    compute_token_logits(logits, trunk[row_ndx{token_count - 1}], model_.wte);
    out = pick_greedy(logits);
    return true;
  }

  // Append up to `count` greedy picks to `ids`, one `next_token` at a time,
  // stopping early at `end_of_text`, which is not appended.
  //
  // On failure (`next_token` fails), returns false, leaving the picks made
  // so far appended.
  [[nodiscard]] bool generate(std::vector<token_id>& ids, size_t count) const {
    for (auto step = 0UZ; step < count; ++step) {
      token_id next{};
      if (!next_token(next, ids)) return false;
      if (next == gpt2_model::end_of_text) return true;
      ids.push_back(next);
    }
    return true;
  }

#pragma endregion
#pragma region Data members
private:
  const gpt2_model& model_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::llm
