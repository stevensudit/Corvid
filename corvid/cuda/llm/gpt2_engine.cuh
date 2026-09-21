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

#include "../../containers/utils/interval.h"
#include "../../llm/gpt2.h"
#include "../../meta/containers.h"
#include "../cuda_buffer.cuh"
#include "../cuda_cublas.cuh"
#include "../cuda_matrix.cuh"
#include "llm_ops.cuh"

// The GPT-2 inference engine on the device, in fp32.
//
// `gpt2_engine` uploads a `gpt2_model` once and runs it: one block, the
// forward pass from token IDs through every block to the final layer norm,
// and greedy generation on top of it, composing the ops of "llm_ops.cuh" as
// "gpt2_engine.h" composes the CPU ones. The activations of a block are
// caller-owned lenses, as on the CPU.
namespace corvid::cuda::llm {

using corvid::llm::gpt2_model;

#pragma region gpt2_engine

// The GPT-2 inference engine on the device, holding its own copy of a
// `gpt2_model`'s parameters.
//
// Every parameter is uploaded at construction, so the model may be destroyed
// afterward. Tokenizing text is `gpt2_tokenizer`'s job, so this takes and
// produces token IDs alone, and the greedy pick itself runs on the host over
// the downloaded logits.
//
//   const gpt2_engine engine(model);
//   std::vector<token_id> ids = ...;
//   if (!engine.generate(ids, 20)) ...
class gpt2_engine {
public:
#pragma region block_activations

  // The intermediate activations of `apply_block`, as caller-owned device
  // lenses.
  //
  // The names, shapes, and sharing rules are those of the CPU
  // `corvid::llm::gpt2_engine::block_activations`, except that `scores` is the
  // device `attend`'s HT x T scratch rather than one row.
  //
  // Note that the `const` on an instance is shallow.
  struct block_activations {
    cuda_matrix_lens<float> ln_1_out;
    cuda_matrix_lens<float> qkv;
    cuda_matrix_lens<float> heads_out;
    cuda_matrix_lens<float> attn_out;
    cuda_matrix_lens<float> ln_2_in;
    cuda_matrix_lens<float> ln_2_out;
    cuda_matrix_lens<float> hidden;
    cuda_matrix_lens<float> mlp_out;
    cuda_matrix_lens<float> scores;
  };

  // Owned device storage for every activation of `apply_block`, all distinct,
  // for `token_count` tokens of width `width` and MLP width `hidden_width`,
  // attended by `head_count` heads.
  struct block_activation_buffers {
    cuda_matrix<float> ln_1_out;
    cuda_matrix<float> qkv;
    cuda_matrix<float> heads_out;
    cuda_matrix<float> attn_out;
    cuda_matrix<float> ln_2_in;
    cuda_matrix<float> ln_2_out;
    cuda_matrix<float> hidden;
    cuda_matrix<float> mlp_out;
    cuda_matrix<float> scores;

    // Allocate every buffer, or throw.
    block_activation_buffers(size_t token_count, size_t width,
        size_t hidden_width, size_t head_count)
        : ln_1_out({.row_count = token_count, .col_count = width}),
          qkv({.row_count = token_count, .col_count = 3 * width}),
          heads_out({.row_count = token_count, .col_count = width}),
          attn_out({.row_count = token_count, .col_count = width}),
          ln_2_in({.row_count = token_count, .col_count = width}),
          ln_2_out({.row_count = token_count, .col_count = width}),
          hidden({.row_count = token_count, .col_count = hidden_width}),
          mlp_out({.row_count = token_count, .col_count = width}),
          scores({.row_count = head_count * token_count,
              .col_count = token_count}) {}

    // The lenses `apply_block` takes.
    [[nodiscard]] block_activations lenses() noexcept {
      return {
          .ln_1_out = ln_1_out,
          .qkv = qkv,
          .heads_out = heads_out,
          .attn_out = attn_out,
          .ln_2_in = ln_2_in,
          .ln_2_out = ln_2_out,
          .hidden = hidden,
          .mlp_out = mlp_out,
          .scores = scores,
      };
    }
  };

#pragma endregion
#pragma region Construction

  // Upload every parameter of `model`, or throw.
  explicit gpt2_engine(const gpt2_model& model)
      : wte_(model.wte), wpe_(model.wpe), ln_f_weight_(model.ln_f_weight),
        ln_f_bias_(model.ln_f_bias), head_count_{model.head_count} {
    blocks_.reserve(model.blocks.size());
    for (const auto& host_block : model.blocks)
      blocks_.emplace_back(host_block);
  }

#pragma endregion
#pragma region Forward pass

  // Run block `block_index` over the residual, reading `in` and writing `out`.
  //
  // The contract is that of the CPU `corvid::llm::gpt2_engine::apply_block`,
  // which also holds the step table. `out` and `in` must have the same extent
  // and may be the same view. The activation views must have the extents
  // `block_activations` states for that extent and may share storage only as
  // it allows. Returns false when a launch is refused, leaving `out` and the
  // activations unspecified.
  [[nodiscard]] bool apply_block(cuda_matrix_lens<float> out,
      cuda_matrix_view<float> in, size_t block_index,
      const block_activations& acts) const {
    const auto& params = blocks_[block_index];
    // Each of these four writes is followed by an add that reads the residual
    // it would have destroyed; the ops' same-or-disjoint checks allow all
    // four, and the ops catch every other overlap.
    assert(is_disjoint(in.as_span(), acts.ln_1_out.as_span()));
    assert(is_disjoint(in.as_span(), acts.attn_out.as_span()));
    assert(is_disjoint(acts.ln_2_in.as_span(), acts.ln_2_out.as_span()));
    assert(is_disjoint(acts.ln_2_in.as_span(), acts.mlp_out.as_span()));

    if (!layer_norm(acts.ln_1_out, in, params.ln_1_weight, params.ln_1_bias,
            gpt2_model::layer_norm_eps))
      return false;
    if (!linear_projection(blas_, acts.qkv, acts.ln_1_out,
            params.attn_c_attn_weight, params.attn_c_attn_bias))
      return false;
    if (!attend(blas_, acts.heads_out, acts.qkv, head_count_, acts.scores))
      return false;
    if (!linear_projection(blas_, acts.attn_out, acts.heads_out,
            params.attn_c_proj_weight, params.attn_c_proj_bias))
      return false;
    if (!add(acts.ln_2_in, in, acts.attn_out)) return false;

    if (!layer_norm(acts.ln_2_out, acts.ln_2_in, params.ln_2_weight,
            params.ln_2_bias, gpt2_model::layer_norm_eps))
      return false;
    if (!linear_projection(blas_, acts.hidden, acts.ln_2_out,
            params.mlp_c_fc_weight, params.mlp_c_fc_bias))
      return false;
    if (!gelu_new(acts.hidden, acts.hidden)) return false;
    if (!linear_projection(blas_, acts.mlp_out, acts.hidden,
            params.mlp_c_proj_weight, params.mlp_c_proj_bias))
      return false;
    return add(out, acts.ln_2_in, acts.mlp_out);
  }

  // Run the model over `ids`, writing the final layer norm's output to `out`.
  //
  // The contract is that of the CPU `corvid::llm::gpt2_engine::forward`,
  // which also holds the step table. `out` doubles as the residual, so it
  // must have one row per ID and the model's width, and there must be no
  // more IDs than the context holds. `acts` is reused by every block, so it
  // holds the last block's activations on return; its `ln_2_in` may be
  // `out`, the in-place form, but no other buffer may overlap `out`. Returns
  // false when a launch is refused, leaving `out` and the activations
  // unspecified.
  [[nodiscard]] bool forward(cuda_matrix_lens<float> out,
      const cuda_buffer<token_id>& ids, const block_activations& acts) const {
    assert(ids.size() <= wpe_.row_extent());

    if (!embed_tokens(out, ids, wte_)) return false;
    if (!embed_positions(out, wpe_)) return false;
    for (const auto block_index : iota(blocks_.size()))
      if (!apply_block(out, out, block_index, acts)) return false;
    return layer_norm(out, out, ln_f_weight_, ln_f_bias_,
        gpt2_model::layer_norm_eps);
  }

#pragma endregion
#pragma region Generation

  // Pick the token most likely to follow `ids`, writing into `out`.
  //
  // Runs the whole model over `ids`. On failure (no IDs, more than the
  // context holds, or a refused launch or transfer), returns false, leaving
  // `out` untouched.
  [[nodiscard]] bool
  next_token(token_id& out, std::span<const token_id> ids) const {
    const auto token_count = ids.size();
    if (!token_count || (token_count > wpe_.row_extent())) return false;
    const auto width = wte_.col_extent();
    const auto vocab_size = wte_.row_extent();

    const cuda_buffer<token_id> device_ids(ids);
    cuda_matrix<float> trunk({.row_count = token_count, .col_count = width});
    block_activation_buffers buffers(token_count, width,
        blocks_.front().mlp_c_fc_weight.col_extent(), head_count_);
    if (!forward(trunk, device_ids, buffers.lenses())) return false;

    cuda_matrix<float> logits({.row_count = 1, .col_count = vocab_size});
    const auto last_row = trunk[{row_ndx{token_count - 1}, col_ndx{0}},
        {.row_count = 1, .col_count = width}];
    if (!compute_logits(blas_, logits, last_row, wte_)) return false;
    std::vector<float> logits_storage(vocab_size);
    if (!logits.as_view().store(
            float_matrix_lens(logits_storage, logits.extent())))
      return false;
    out = corvid::llm::pick_greedy(const_float_row_span(logits_storage));
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
#pragma region block_params
private:
  // The parameters of one block, uploaded from `gpt2_model::block_params`.
  //
  // The names and shapes are those of the CPU bundle.
  struct block_params {
    cuda_buffer<float> ln_1_weight;
    cuda_buffer<float> ln_1_bias;
    cuda_matrix<float> attn_c_attn_weight;
    cuda_buffer<float> attn_c_attn_bias;
    cuda_matrix<float> attn_c_proj_weight;
    cuda_buffer<float> attn_c_proj_bias;
    cuda_buffer<float> ln_2_weight;
    cuda_buffer<float> ln_2_bias;
    cuda_matrix<float> mlp_c_fc_weight;
    cuda_buffer<float> mlp_c_fc_bias;
    cuda_matrix<float> mlp_c_proj_weight;
    cuda_buffer<float> mlp_c_proj_bias;

    // Allocate and upload every parameter of `host`, or throw.
    explicit block_params(const gpt2_model::block_params& host)
        : ln_1_weight(host.ln_1_weight), ln_1_bias(host.ln_1_bias),
          attn_c_attn_weight(host.attn_c_attn_weight),
          attn_c_attn_bias(host.attn_c_attn_bias),
          attn_c_proj_weight(host.attn_c_proj_weight),
          attn_c_proj_bias(host.attn_c_proj_bias),
          ln_2_weight(host.ln_2_weight), ln_2_bias(host.ln_2_bias),
          mlp_c_fc_weight(host.mlp_c_fc_weight),
          mlp_c_fc_bias(host.mlp_c_fc_bias),
          mlp_c_proj_weight(host.mlp_c_proj_weight),
          mlp_c_proj_bias(host.mlp_c_proj_bias) {}
  };

#pragma endregion
#pragma region Data members
  cublas_handle blas_;
  cuda_matrix<float> wte_;
  cuda_matrix<float> wpe_;
  std::vector<block_params> blocks_;
  cuda_buffer<float> ln_f_weight_;
  cuda_buffer<float> ln_f_bias_;
  size_t head_count_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda::llm
