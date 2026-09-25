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
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

#include "../../llm/gpt2.h"
#include "../../meta/containers.h"
#include "../bfloat16.cuh"
#include "../cuda_buffer.cuh"
#include "../cuda_cublas.cuh"
#include "../cuda_matrix.cuh"
#include "llm_ops.cuh"

// The GPT-2 inference engine on the device, over `float`, `double`, or
// `bfloat16_t`.
//
// `gpt2_engine<T>` uploads a `gpt2_model` once, as `T`, and runs it: one
// block, the forward pass from token IDs through every block to the final
// layer norm, and greedy generation on top of it, composing the ops of
// "llm_ops.cuh" as "gpt2_engine.h" composes the CPU ones. The activations of a
// block are caller-owned lenses, as on the CPU.
//
// A token's keys and values never change once computed, so `forward` keeps
// them on the device in a `kv_cache` and runs only the tokens that are new.
namespace corvid::cuda::llm {

using corvid::llm::gpt2_model;

#pragma region gpt2_engine

// The GPT-2 inference engine on the device, holding its own copy of a
// `gpt2_model`'s parameters as `T`.
//
// Every parameter is uploaded at construction, converted from the model's
// `float` where `T` differs, so the model may be destroyed afterward. The
// logits are `float` whatever `T` is, so that a pick rests on the final
// projection's full precision. Tokenizing text is `gpt2_tokenizer`'s job, so
// this takes and produces token IDs alone.
//
//   const gpt2_engine<float> engine(model);
//   std::vector<token_id> ids = ...;
//   if (!engine.generate(ids, 20)) ...
template<typename T>
requires DeviceFloating<T> && GemmElement<T>
class gpt2_engine {
public:
  using element_t = T;

#pragma region block_activations

  // The intermediate activations of `apply_block`, as caller-owned device
  // lenses.
  //
  // The names, shapes, and sharing rules are those of the CPU
  // `corvid::llm::gpt2_engine::block_activations`, except that `scores` is the
  // device `attend`'s HN x T scratch rather than one row.
  //
  // Note that the `const` on an instance is shallow.
  struct block_activations {
    cuda_matrix_lens<element_t> ln_1_out;
    cuda_matrix_lens<element_t> heads_out;
    cuda_matrix_lens<element_t> attn_out;
    cuda_matrix_lens<element_t> ln_2_in;
    cuda_matrix_lens<element_t> ln_2_out;
    cuda_matrix_lens<element_t> hidden;
    cuda_matrix_lens<element_t> mlp_out;
    cuda_matrix_lens<element_t> scores;
  };

  // Owned device storage for every activation of `apply_block`, all distinct,
  // for `new_count` new tokens of width `width` and MLP width `hidden_width`,
  // attended by `head_count` heads after `cached_count` cached tokens.
  struct block_activation_buffers {
    cuda_matrix<element_t> ln_1_out;
    cuda_matrix<element_t> heads_out;
    cuda_matrix<element_t> attn_out;
    cuda_matrix<element_t> ln_2_in;
    cuda_matrix<element_t> ln_2_out;
    cuda_matrix<element_t> hidden;
    cuda_matrix<element_t> mlp_out;
    cuda_matrix<element_t> scores;

    // Allocate every buffer, or throw.
    block_activation_buffers(size_t new_count, size_t width,
        size_t hidden_width, size_t head_count, size_t cached_count = 0)
        : ln_1_out({.row_count = new_count, .col_count = width}),
          heads_out({.row_count = new_count, .col_count = width}),
          attn_out({.row_count = new_count, .col_count = width}),
          ln_2_in({.row_count = new_count, .col_count = width}),
          ln_2_out({.row_count = new_count, .col_count = width}),
          hidden({.row_count = new_count, .col_count = hidden_width}),
          mlp_out({.row_count = new_count, .col_count = width}),
          scores({.row_count = head_count * new_count,
              .col_count = cached_count + new_count}) {}

    // The lenses `apply_block` takes.
    [[nodiscard]] block_activations lenses() noexcept {
      return {
          .ln_1_out = ln_1_out,
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
#pragma region kv_cache

  // The tokens that `forward` has already run, with each one's keys and values
  // in every block, on the device.
  //
  // The layout is that of the CPU `corvid::llm::gpt2_engine::kv_cache`, with
  // the IDs on the host and the rows on the device. For M cached tokens of
  // width C in a context of L positions:
  //
  //   ids     [M]      the cached tokens
  //   blocks  [L, 3C]  per block, a row per position, the first M in use
  //
  // Each block's store spans the whole context from the first `forward` on,
  // so a later pass writes its rows in place and nothing reallocates.
  //
  // A default-constructed cache holds no tokens. `forward` appends to it.
  // Shortening `ids` forgets the tokens cut off, and any other change to
  // either member breaks the pairing between them.
  struct kv_cache {
    std::vector<token_id> ids;
    std::vector<cuda_matrix<element_t>> blocks;
  };

#pragma endregion
#pragma region Construction

  // Upload every parameter of `model`, or throw.
  explicit gpt2_engine(const gpt2_model& model)
      : wte_(upload(model.wte)), wpe_(upload(model.wpe)),
        ln_f_weight_(upload(model.ln_f_weight)),
        ln_f_bias_(upload(model.ln_f_bias)), head_count_{model.head_count} {
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
  // and may be the same view. `qkv` must have at least as many rows as `in`,
  // the first of them holding the cached tokens' `attn.c_attn` output. The
  // activation views must have the extents `block_activations` states for
  // that extent and may share storage only as it allows. Returns false when a
  // launch is refused, leaving `out`, `qkv`, and the activations unspecified.
  [[nodiscard]] bool apply_block(cuda_matrix_lens<element_t> out,
      cuda_matrix_view<element_t> in, size_t block_index,
      const block_activations& acts, cuda_matrix_lens<element_t> qkv) const {
    const auto& params = blocks_[block_index];
    assert(qkv.row_extent() >= in.row_extent());
    const auto cached_count = qkv.row_extent() - in.row_extent();
    const auto new_qkv = qkv[{row_ndx{cached_count}, col_ndx{0}},
        {.row_count = in.row_extent(), .col_count = qkv.col_extent()}];
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
    if (!linear_projection(blas_, new_qkv, acts.ln_1_out,
            params.attn_c_attn_weight, params.attn_c_attn_bias))
      return false;
    if (!attend(blas_, acts.heads_out, qkv, head_count_, acts.scores))
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

  // Run the model over `new_ids`, which follow the tokens in `cache`, writing
  // the final layer norm's output to `out`.
  //
  // The contract is that of the CPU `corvid::llm::gpt2_engine::forward`,
  // which also holds the step table. `out` doubles as the residual, so it
  // must have one row per new ID and the model's width, and there must be no
  // more tokens, cached and new, than the context holds. `acts` is reused by
  // every block, so it holds the last block's activations on return. Its
  // `ln_2_in` may be `out`, the in-place form, but no other buffer may overlap
  // `out`. Returns false when a launch or transfer is refused, leaving `out`,
  // the activations, and `cache` unspecified.
  [[nodiscard]] bool forward(cuda_matrix_lens<element_t> out,
      std::span<const token_id> new_ids, const block_activations& acts,
      kv_cache& cache) const {
    const auto cached_count = cache.ids.size();
    const auto total_count = cached_count + new_ids.size();
    const auto context_length = wpe_.row_extent();
    assert(total_count <= context_length);
    const auto qkv_width = 3 * wte_.col_extent();

    const cuda_buffer<token_id> device_ids(new_ids);
    if (!embed_tokens(out, device_ids, wte_)) return false;
    if (!embed_positions(out, wpe_, cached_count)) return false;

    // Allocating the whole context up front means that no later pass
    // reallocates.
    if (cache.blocks.empty()) {
      cache.ids.reserve(context_length);
      cache.blocks.reserve(blocks_.size());
      for (auto block = 0UZ; block < blocks_.size(); ++block)
        cache.blocks.emplace_back(matrix_extent{.row_count = context_length,
            .col_count = qkv_width});
    }
    for (const auto [block_index, store] : std::views::enumerate(cache.blocks))
    {
      const auto qkv = store[{row_ndx{0}, col_ndx{0}},
          {.row_count = total_count, .col_count = qkv_width}];
      if (!apply_block(out, out, static_cast<size_t>(block_index), acts, qkv))
        return false;
    }
    if (!layer_norm(out, out, ln_f_weight_, ln_f_bias_,
            gpt2_model::layer_norm_eps))
      return false;

    cache.ids.insert(cache.ids.end(), new_ids.begin(), new_ids.end());
    return true;
  }

#pragma endregion
#pragma region Generation

  // Pick the token most likely to follow `ids`, writing into `out`.
  //
  // The engine caches the last list it ran, as the CPU
  // `corvid::llm::gpt2_engine::next_token` describes, and downloads only the
  // pick. The cache makes this one caller at a time, `const` notwithstanding.
  // On failure (no IDs, more than the context holds, or a refused launch or
  // transfer), returns false, leaving `out` untouched.
  [[nodiscard]] bool
  next_token(token_id& out, std::span<const token_id> ids) const {
    const auto total_count = ids.size();
    if (!total_count || (total_count > wpe_.row_extent())) return false;
    const auto width = wte_.col_extent();
    const auto vocab_size = wte_.row_extent();

    // Keep the cached tokens that `ids` starts with, short of its last one,
    // since the logits need that token's row of the trunk, which is not
    // cached.
    const auto matched_count = static_cast<size_t>(
        std::ranges::mismatch(cache_.ids, ids).in1 - cache_.ids.begin());
    const auto cached_count = std::min(matched_count, total_count - 1);
    cache_.ids.resize(cached_count);
    const auto new_ids = ids.subspan(cached_count);
    const auto new_count = new_ids.size();

    cuda_matrix<element_t> trunk({.row_count = new_count, .col_count = width});
    block_activation_buffers buffers(new_count, width,
        blocks_.front().mlp_c_fc_weight.col_extent(), head_count_,
        cached_count);
    if (!forward(trunk, new_ids, buffers.lenses(), cache_)) return false;

    cuda_matrix<float> logits({.row_count = 1, .col_count = vocab_size});
    const auto last_row =
        trunk[{row_ndx{new_count - 1}, col_ndx{0}}, matrix_extent::npos];
    if (!compute_logits(blas_, logits, last_row, wte_)) return false;
    cuda_buffer<token_id> device_pick;
    if (!pick_greedy(device_pick, logits)) return false;
    token_id pick{};
    if (!device_pick.store(pick)) return false;
    out = pick;
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
    cuda_buffer<element_t> ln_1_weight;
    cuda_buffer<element_t> ln_1_bias;
    cuda_matrix<element_t> attn_c_attn_weight;
    cuda_buffer<element_t> attn_c_attn_bias;
    cuda_matrix<element_t> attn_c_proj_weight;
    cuda_buffer<element_t> attn_c_proj_bias;
    cuda_buffer<element_t> ln_2_weight;
    cuda_buffer<element_t> ln_2_bias;
    cuda_matrix<element_t> mlp_c_fc_weight;
    cuda_buffer<element_t> mlp_c_fc_bias;
    cuda_matrix<element_t> mlp_c_proj_weight;
    cuda_buffer<element_t> mlp_c_proj_bias;

    // Allocate and upload every parameter of `host`, or throw.
    explicit block_params(const gpt2_model::block_params& host)
        : ln_1_weight(upload(host.ln_1_weight)),
          ln_1_bias(upload(host.ln_1_bias)),
          attn_c_attn_weight(upload(host.attn_c_attn_weight)),
          attn_c_attn_bias(upload(host.attn_c_attn_bias)),
          attn_c_proj_weight(upload(host.attn_c_proj_weight)),
          attn_c_proj_bias(upload(host.attn_c_proj_bias)),
          ln_2_weight(upload(host.ln_2_weight)),
          ln_2_bias(upload(host.ln_2_bias)),
          mlp_c_fc_weight(upload(host.mlp_c_fc_weight)),
          mlp_c_fc_bias(upload(host.mlp_c_fc_bias)),
          mlp_c_proj_weight(upload(host.mlp_c_proj_weight)),
          mlp_c_proj_bias(upload(host.mlp_c_proj_bias)) {}
  };

#pragma endregion
#pragma region Upload

  // Upload `host` as a matrix of `element_t`, or throw.
  //
  // A `float` parameter goes straight up. Any other type is staged as `float`
  // and converted on the device.
  [[nodiscard]] static cuda_matrix<element_t> upload(float_matrix_view host)
  requires std::same_as<element_t, float>
  {
    return cuda_matrix<element_t>(host);
  }
  [[nodiscard]] static cuda_matrix<element_t> upload(float_matrix_view host) {
    const cuda_matrix<float> staged(host);
    cuda_matrix<element_t> result(host.extent());
    if (!convert(result, staged)) raise_refused();
    return result;
  }

  // Upload `host` as a buffer of `element_t`, or throw.
  //
  // The same two paths as the matrix form, with the buffer converted as one
  // row.
  [[nodiscard]] static cuda_buffer<element_t>
  upload(std::span<const float> host)
  requires std::same_as<element_t, float>
  {
    return cuda_buffer<element_t>(host);
  }
  [[nodiscard]] static cuda_buffer<element_t> upload(
      std::span<const float> host) {
    const cuda_buffer<float> staged(host);
    cuda_buffer<element_t> result(host.size());
    const matrix_extent row{.row_count = 1, .col_count = host.size()};
    if (!convert(cuda_matrix_lens<element_t>(result.as_span(), row),
            cuda_matrix_view<float>(staged.as_span(), row)))
      raise_refused();
    return result;
  }

  // Throw for a conversion launch that was refused.
  [[noreturn]] static void raise_refused() {
    throw std::runtime_error{
        "gpt2_engine: converting a parameter on the device was refused"};
  }

#pragma endregion
#pragma region Data members
  cublas_handle blas_;
  cuda_matrix<element_t> wte_;
  cuda_matrix<element_t> wpe_;
  std::vector<block_params> blocks_;
  cuda_buffer<element_t> ln_f_weight_;
  cuda_buffer<element_t> ln_f_bias_;
  size_t head_count_;
  mutable kv_cache cache_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda::llm
