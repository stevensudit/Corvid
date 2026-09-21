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
#include <cstddef>
#include <ranges>
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
//
// A token's keys and values never change once computed, so `forward` keeps
// them in a `kv_cache` and runs only the tokens that are new.
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
  // For N new tokens after M cached ones, of width C and MLP width F, in the
  // order written:
  //
  //   ln_1_out   [N, C]   the first layer norm's output
  //   heads_out  [N, C]   the attention output before `attn.c_proj`
  //   attn_out   [N, C]   the attention sublayer's correction
  //   ln_2_in    [N, C]   the residual after the attention add
  //   ln_2_out   [N, C]   the second layer norm's output
  //   hidden     [N, F]   the MLP's widened rows
  //   mlp_out    [N, C]   the MLP sublayer's correction
  //   scores     [M + N]  the attention scratch
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
    float_matrix_view heads_out;
    float_matrix_view attn_out;
    float_matrix_view ln_2_in;
    float_matrix_view ln_2_out;
    float_matrix_view hidden;
    float_matrix_view mlp_out;
    float_col_span scores;
  };

  // Owned storage for every activation of `apply_block`, all distinct, for
  // `new_count` new tokens of width `width` and MLP width `hidden_width`,
  // after `cached_count` cached ones.
  struct block_activation_buffers {
    std::vector<float> ln_1_out;
    std::vector<float> heads_out;
    std::vector<float> attn_out;
    std::vector<float> ln_2_in;
    std::vector<float> ln_2_out;
    std::vector<float> hidden;
    std::vector<float> mlp_out;
    std::vector<float> scores;
    size_t new_count;
    size_t width;
    size_t hidden_width;

    block_activation_buffers(size_t new_count, size_t width,
        size_t hidden_width, size_t cached_count = 0)
        : ln_1_out(new_count * width), heads_out(new_count * width),
          attn_out(new_count * width), ln_2_in(new_count * width),
          ln_2_out(new_count * width), hidden(new_count * hidden_width),
          mlp_out(new_count * width), scores(cached_count + new_count),
          new_count{new_count}, width{width}, hidden_width{hidden_width} {}

    // The views `apply_block` takes.
    [[nodiscard]] block_activations views() noexcept {
      const auto rows = [&](std::vector<float>& storage, size_t cols) {
        return float_matrix_view(storage,
            {.row_count = new_count, .col_count = cols});
      };
      return {
          .ln_1_out = rows(ln_1_out, width),
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
#pragma region kv_cache

  // The tokens that `forward` has already run, with each one's keys and values
  // in every block.
  //
  // For M cached tokens of width C, each block holds the `attn.c_attn` output
  // of those tokens, which is their queries, keys, and values:
  //
  //   ids     [M]      the cached tokens
  //   blocks  [M, 3C]  per block, a row per cached token, packed
  //
  // The queries are never read again. They stay so that the projection can
  // write a new token's row in place.
  //
  // A default-constructed cache holds no tokens. `forward` appends to it.
  // Shortening `ids` forgets the tokens cut off, and any other change to
  // either member breaks the pairing between them.
  struct kv_cache {
    std::vector<token_id> ids;
    std::vector<std::vector<float>> blocks;
  };

#pragma endregion
#pragma region Construction

  explicit gpt2_engine(const gpt2_model& model) noexcept : model_{model} {}

#pragma endregion
#pragma region Forward pass

  // Run block `block_index` over the residual, reading `in` and writing `out`.
  //
  // Each sublayer reads the residual through its layer norm, computes a
  // correction of the same shape, and adds it back. For GPT-2 with N new
  // tokens after M cached ones:
  //
  // step        |  reads              |  produces
  // ------------+---------------------+---------------------
  // ln_1        |  in [N, 768]        |  ln_1_out [N, 768]
  // attn.c_attn |  ln_1_out           |  qkv [M:, 2304]
  // attention   |  qkv [M + N, 2304]  |  heads_out [N, 768]
  // attn.c_proj |  heads_out          |  attn_out [N, 768]
  // add         |  in, attn_out       |  ln_2_in [N, 768]
  // ln_2        |  ln_2_in            |  ln_2_out [N, 768]
  // mlp.c_fc    |  ln_2_out           |  hidden [N, 3072]
  // gelu_new    |  hidden             |  hidden, in place
  // mlp.c_proj  |  hidden             |  mlp_out [N, 768]
  // add         |  ln_2_in, mlp_out   |  out [N, 768]
  //
  // where `qkv [M:, 2304]` is the rows of `qkv` after the first M. `qkv` comes
  // in holding the block's `attn.c_attn` output for the cached tokens in its
  // first M rows, and leaves holding that of the new tokens in the rest. With
  // nothing cached, it is plain scratch with a row per token.
  //
  // `out` and `in` must have the same extent and may be the same view. `qkv`
  // must have at least as many rows as `in`. The activation views must have
  // the extents above and may share storage only as `block_activations`
  // allows; in particular, no buffer may overlap the residual it is later
  // added to.
  //
  // Note that the `const` on `acts` is shallow.
  void apply_block(float_matrix_view out, const_float_matrix_view in,
      size_t block_index, const block_activations& acts,
      float_matrix_view qkv) const noexcept {
    const auto& params = model_.blocks[block_index];
    assert(qkv.row_extent() >= in.row_extent());
    const auto cached_count = qkv.row_extent() - in.row_extent();
    const auto new_qkv = qkv.subview({row_ndx{cached_count}, col_ndx{0}},
        {.row_count = in.row_extent(), .col_count = qkv.col_extent()});
    // Each of these four writes is followed by an add that reads the residual
    // it would have destroyed; the ops' same-or-disjoint checks allow all
    // four, and the ops catch every other overlap.
    assert(is_disjoint(in.as_span(), acts.ln_1_out.as_span()));
    assert(is_disjoint(in.as_span(), acts.attn_out.as_span()));
    assert(is_disjoint(acts.ln_2_in.as_span(), acts.ln_2_out.as_span()));
    assert(is_disjoint(acts.ln_2_in.as_span(), acts.mlp_out.as_span()));

    layer_norm(acts.ln_1_out, in, params.ln_1_weight, params.ln_1_bias,
        gpt2_model::layer_norm_eps);
    linear_projection(new_qkv, acts.ln_1_out, params.attn_c_attn_weight,
        params.attn_c_attn_bias);
    attend(acts.heads_out, qkv, model_.head_count, acts.scores);
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

  // Run the model over `new_ids`, which follow the tokens in `cache`, writing
  // the final layer norm's output to `out`.
  //
  // This is everything before the head: the residual stream starts as the
  // token embedding plus the position embedding of each row, every block
  // adds to it in place, and `ln_f` normalizes what leaves the last block.
  // For GPT-2 with N new tokens after M cached ones:
  //
  // step             |  reads               |  produces
  // -----------------+----------------------+-------------------
  // embed_tokens     |  new_ids [N]         |  out [N, 768]
  // embed_positions  |  out, wpe, M         |  out, in place
  // block, x 12      |  out, cache          |  out, in place
  // ln_f             |  out                 |  out, in place
  //
  // where M is the position of the first new token, so there must be no more
  // tokens, cached and new, than the context holds.
  //
  // Only the new tokens are run. Each block reads the cached tokens' keys and
  // values from `cache`, which on return holds the new tokens as well. An
  // empty cache runs the whole model over `new_ids`.
  //
  // `out` doubles as the residual, so it must have one row per new ID and the
  // model's width. `acts` is reused by every block, so it holds the last
  // block's activations on return; its `ln_2_in` may be `out`, the in-place
  // form, but no other buffer may overlap `out`.
  void forward(float_matrix_view out, std::span<const token_id> new_ids,
      const block_activations& acts, kv_cache& cache) const {
    const auto cached_count = cache.ids.size();
    const auto total_count = cached_count + new_ids.size();
    assert(total_count <= model_.context_length());
    const auto qkv_width = 3 * model_.width();

    embed_tokens(out, new_ids, model_.wte);
    embed_positions(out, model_.wpe, cached_count);

    // Reserving the whole context up front means that no later pass
    // reallocates, and a repeated reserve does nothing.
    const auto context_length = model_.context_length();
    cache.ids.reserve(context_length);
    cache.blocks.resize(model_.blocks.size());
    for (const auto [block_index, storage] :
        std::views::enumerate(cache.blocks))
    {
      // Rows are packed at a fixed width, so growing the storage keeps every
      // cached row where it was.
      storage.reserve(context_length * qkv_width);
      if (storage.size() < total_count * qkv_width)
        storage.resize(total_count * qkv_width);
      const float_matrix_view qkv(storage,
          {.row_count = total_count, .col_count = qkv_width});
      apply_block(out, out, static_cast<size_t>(block_index), acts, qkv);
    }
    layer_norm(out, out, model_.ln_f_weight, model_.ln_f_bias,
        gpt2_model::layer_norm_eps);

    cache.ids.insert(cache.ids.end(), new_ids.begin(), new_ids.end());
  }

#pragma endregion
#pragma region Generation

  // Pick the token most likely to follow `ids`, writing into `out`.
  //
  // The engine caches the last list it ran. The tokens that `ids` starts with
  // in common with that list are not run again, so extending the previous list
  // costs only the tokens added, while an unrelated list runs in full and
  // replaces it. The pick is the same either way.
  //
  // The cache makes this one caller at a time, `const` notwithstanding. On
  // failure (no IDs, or more than the context holds), returns false, leaving
  // `out` untouched.
  [[nodiscard]] bool
  next_token(token_id& out, std::span<const token_id> ids) const {
    const auto total_count = ids.size();
    if (!total_count || (total_count > model_.context_length())) return false;
    const auto width = model_.width();

    // Keep the cached tokens that `ids` starts with, short of its last one,
    // since the logits need that token's row of the trunk, which is not
    // cached.
    const auto matched_count = static_cast<size_t>(
        std::ranges::mismatch(cache_.ids, ids).in1 - cache_.ids.begin());
    const auto cached_count = std::min(matched_count, total_count - 1);
    cache_.ids.resize(cached_count);
    const auto new_ids = ids.subspan(cached_count);
    const auto new_count = new_ids.size();

    std::vector<float> trunk_storage(new_count * width);
    const float_matrix_view trunk(trunk_storage,
        {.row_count = new_count, .col_count = width});
    block_activation_buffers buffers(new_count, width, model_.hidden_width(),
        cached_count);
    forward(trunk, new_ids, buffers.views(), cache_);

    std::vector<float> logits_storage(model_.vocab_size());
    const float_row_span logits(logits_storage);
    compute_token_logits(logits, trunk[row_ndx{new_count - 1}], model_.wte);
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
  mutable kv_cache cache_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::llm
