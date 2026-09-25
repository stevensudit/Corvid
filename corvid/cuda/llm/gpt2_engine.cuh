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

using corvid::llm::gpt2_block_params;
using corvid::llm::gpt2_end_of_text;
using corvid::llm::gpt2_layer_norm_eps;
using corvid::llm::gpt2_model;
using corvid::llm::ParameterElement;

#pragma region gpt2_engine

// The GPT-2 inference engine on the device, holding its own copy of a
// `gpt2_model`'s parameters, with the operands of its products against them
// as `T` and everything else in `wide_t`, the type `T` computes in.
//
// `T` is the storage of what a product against the parameters reads: the
// token and position embeddings, the projection weights, the biases of the
// projections whose outputs feed another such product, the layer norm
// outputs, the values (and so the value cache), the attention weights, the
// heads' output, and the MLP's hidden activation. `wide_t` holds the residual
// stream, the two projection outputs added to it and their biases, the layer
// norms' weights and biases, and the queries, keys (and so the key cache),
// and scores, so a value that accumulates across the blocks is never narrowed
// and the scores are exact products. Every parameter is uploaded at
// construction, converted where its type differs from the model's, so the
// model may be destroyed afterward. The logits are `wide_t` too, so
// that a pick rests on the final projection's full precision. Tokenizing text
// is `gpt2_tokenizer`'s job, so this takes and produces token IDs alone.
//
//   const gpt2_engine<float> engine(model);
//   std::vector<token_id> ids = ...;
//   if (!engine.generate(ids, 20)) ...
template<typename T>
requires DeviceFloating<T> && GemmElement<T>
class gpt2_engine {
public:
  using element_t = T;
  // The type `element_t` computes in, `float` for `bfloat16_t` and
  // `element_t` itself otherwise, which holds whatever is not an operand of a
  // product against the parameters.
  using wide_t = compute_t<element_t>;

#pragma region block_activations

  // The intermediate activations of `apply_block`, as caller-owned device
  // lenses.
  //
  // The names, shapes, and sharing rules are those of the CPU
  // `corvid::llm::gpt2_engine::block_activations`, with the device `attend`'s
  // inputs and scratch in place of `qkv` and its one row of scores: the new
  // tokens' `queries`, and `scores` and `weights`, each HN x T. The queries,
  // scores, and the three activations on the residual stream (`attn_out`,
  // `ln_2_in`, and `mlp_out`) are `wide_t`. The rest feed a product against
  // the parameters and are `element_t`.
  //
  // Note that the `const` on an instance is shallow.
  struct block_activations {
    cuda_matrix_lens<element_t> ln_1_out;
    cuda_matrix_lens<wide_t> queries;
    cuda_matrix_lens<wide_t> scores;
    cuda_matrix_lens<element_t> weights;
    cuda_matrix_lens<element_t> heads_out;
    cuda_matrix_lens<wide_t> attn_out;
    cuda_matrix_lens<wide_t> ln_2_in;
    cuda_matrix_lens<element_t> ln_2_out;
    cuda_matrix_lens<element_t> hidden;
    cuda_matrix_lens<wide_t> mlp_out;
  };

  // Owned device storage for every activation of `apply_block`, all distinct,
  // for `new_count` new tokens of width `width` and MLP width `hidden_width`,
  // attended by `head_count` heads after `cached_count` cached tokens.
  struct block_activation_buffers {
    cuda_matrix<element_t> ln_1_out;
    cuda_matrix<wide_t> queries;
    cuda_matrix<wide_t> scores;
    cuda_matrix<element_t> weights;
    cuda_matrix<element_t> heads_out;
    cuda_matrix<wide_t> attn_out;
    cuda_matrix<wide_t> ln_2_in;
    cuda_matrix<element_t> ln_2_out;
    cuda_matrix<element_t> hidden;
    cuda_matrix<wide_t> mlp_out;

    // Allocate every buffer, or throw.
    block_activation_buffers(size_t new_count, size_t width,
        size_t hidden_width, size_t head_count, size_t cached_count = 0)
        : ln_1_out({.row_count = new_count, .col_count = width}),
          queries({.row_count = new_count, .col_count = width}),
          scores({.row_count = head_count * new_count,
              .col_count = cached_count + new_count}),
          weights(scores.extent()), heads_out(queries.extent()),
          attn_out(queries.extent()), ln_2_in(queries.extent()),
          ln_2_out(queries.extent()),
          hidden({.row_count = new_count, .col_count = hidden_width}),
          mlp_out(queries.extent()) {}

    // The lenses `apply_block` takes.
    [[nodiscard]] block_activations lenses() noexcept {
      return {
          .ln_1_out = ln_1_out,
          .queries = queries,
          .scores = scores,
          .weights = weights,
          .heads_out = heads_out,
          .attn_out = attn_out,
          .ln_2_in = ln_2_in,
          .ln_2_out = ln_2_out,
          .hidden = hidden,
          .mlp_out = mlp_out,
      };
    }
  };

#pragma endregion
#pragma region kv_cache

  // One block's keys and values for every position of the context, on the
  // device.
  struct block_store {
    cuda_matrix<wide_t> keys;
    cuda_matrix<element_t> values;
  };

  // The tokens that `forward` has already run, with each one's keys and values
  // in every block, on the device.
  //
  // The layout is that of the CPU `corvid::llm::gpt2_engine::kv_cache`, with
  // the IDs on the host, the rows on the device, and the keys and values held
  // apart, since the keys are `wide_t` and the values `element_t`. For M
  // cached tokens of width C in a context of L positions:
  //
  //   ids     [M]      the cached tokens
  //   blocks  [L]      per block, its keys and its values, each [L, C], a row
  //                    per position, the first M in use
  //
  // Each block's store spans the whole context from the first `forward` on,
  // so a later pass writes its rows in place and nothing reallocates.
  //
  // A default-constructed cache holds no tokens. `forward` appends to it.
  // Shortening `ids` forgets the tokens cut off, and any other change to
  // either member breaks the pairing between them.
  struct kv_cache {
    std::vector<token_id> ids;
    std::vector<block_store> blocks;
  };

#pragma endregion
#pragma region Construction

  // Upload every parameter of `model`, or throw.
  template<ParameterElement H>
  explicit gpt2_engine(const gpt2_model<H>& model)
      : wte_(upload<element_t>(model.wte)), wpe_(upload<element_t>(model.wpe)),
        ln_f_weight_(upload<wide_t>(model.ln_f_weight)),
        ln_f_bias_(upload<wide_t>(model.ln_f_bias)),
        head_count_{model.head_count} {
    blocks_.reserve(model.blocks.size());
    for (const auto& host_block : model.blocks)
      blocks_.emplace_back(host_block);
  }

#pragma endregion
#pragma region Forward pass

  // Run block `block_index` over the residual, reading `in` and writing `out`.
  //
  // The contract is that of the CPU `corvid::llm::gpt2_engine::apply_block`,
  // which also holds the step table, with the block's `keys` and `values` in
  // place of `qkv`. `out` and `in` must have the same extent and may be the
  // same view. `keys` and `values` must have the same extent, the width of
  // `in` and at least as many rows, the first of them holding the cached
  // tokens' keys and values, and the new tokens' rows are written. The
  // activation views must have the extents `block_activations` states for
  // that extent and may share storage only as it allows. Returns false when a
  // launch is refused, leaving `out`, the new rows of `keys` and `values`,
  // and the activations unspecified.
  [[nodiscard]] bool apply_block(cuda_matrix_lens<wide_t> out,
      cuda_matrix_view<wide_t> in, size_t block_index,
      const block_activations& acts, cuda_matrix_lens<wide_t> keys,
      cuda_matrix_lens<element_t> values) const {
    const auto& params = blocks_[block_index];
    assert(keys.row_extent() >= in.row_extent());
    assert(values.extent() == keys.extent());
    const auto cached_count = keys.row_extent() - in.row_extent();
    const auto new_keys =
        keys[{row_ndx{cached_count}, col_ndx{0}}, matrix_extent::npos];
    const auto new_values =
        values[{row_ndx{cached_count}, col_ndx{0}}, matrix_extent::npos];
    // Each of these four writes is followed by an add that reads the residual
    // it would have destroyed; the ops' same-or-disjoint checks allow all
    // four, and the ops catch every other overlap.
    assert(is_disjoint(in.as_span(), acts.ln_1_out.as_span()));
    assert(is_disjoint(in.as_span(), acts.attn_out.as_span()));
    assert(is_disjoint(acts.ln_2_in.as_span(), acts.ln_2_out.as_span()));
    assert(is_disjoint(acts.ln_2_in.as_span(), acts.mlp_out.as_span()));

    if (!layer_norm(acts.ln_1_out, in, params.ln_1_weight, params.ln_1_bias,
            gpt2_layer_norm_eps))
      return false;
    // The attention projection goes a third at a time, so that the new keys
    // and values land in their stores and the queries beside them, each in
    // its own type.
    if (!linear_projection(blas_, acts.queries, acts.ln_1_out,
            params.attn_c_attn_query_weight(), params.attn_c_attn_query_bias))
      return false;
    if (!linear_projection(blas_, new_keys, acts.ln_1_out,
            params.attn_c_attn_key_weight(), params.attn_c_attn_key_bias))
      return false;
    if (!linear_projection(blas_, new_values, acts.ln_1_out,
            params.attn_c_attn_value_weight(), params.attn_c_attn_value_bias))
      return false;
    if (!attend(blas_, acts.heads_out, acts.queries, keys, values, head_count_,
            acts.scores, acts.weights))
      return false;
    if (!linear_projection(blas_, acts.attn_out, acts.heads_out,
            params.attn_c_proj_weight, params.attn_c_proj_bias))
      return false;
    if (!add(acts.ln_2_in, in, acts.attn_out)) return false;

    if (!layer_norm(acts.ln_2_out, acts.ln_2_in, params.ln_2_weight,
            params.ln_2_bias, gpt2_layer_norm_eps))
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
  // which also holds the step table, except that the residual stream runs
  // through `residual`, caller-owned scratch of `out`'s extent, and the final
  // layer norm writes `out` from it. `out` must have one row per new ID and
  // the model's width, and there must be no more tokens, cached and new, than
  // the context holds. `acts` is reused by every block, so it holds the last
  // block's activations on return. Its `ln_2_in` may be `residual`, the
  // in-place form, but no other buffer may overlap `residual`, and none may
  // overlap `out`. Returns false when a launch or transfer is refused,
  // leaving `out`, `residual`, the activations, and `cache` unspecified.
  [[nodiscard]] bool forward(cuda_matrix_lens<element_t> out,
      cuda_matrix_lens<wide_t> residual, std::span<const token_id> new_ids,
      const block_activations& acts, kv_cache& cache) const {
    const auto cached_count = cache.ids.size();
    const auto total_count = cached_count + new_ids.size();
    const auto context_length = wpe_.row_extent();
    const auto width = wte_.col_extent();
    assert(total_count <= context_length);
    assert(residual.extent() == out.extent());
    assert(is_disjoint(out.as_span(), residual.as_span()));

    const cuda_buffer<token_id> device_ids(new_ids);
    if (!embed_tokens(residual, device_ids, wte_)) return false;
    if (!embed_positions(residual, wpe_, cached_count)) return false;

    // Allocating the whole context up front means that no later pass
    // reallocates.
    if (cache.blocks.empty()) {
      const matrix_extent context{.row_count = context_length,
          .col_count = width};
      cache.ids.reserve(context_length);
      cache.blocks.reserve(blocks_.size());
      for (auto block = 0UZ; block < blocks_.size(); ++block)
        cache.blocks.emplace_back(block_store{
            .keys = cuda_matrix<wide_t>(context),
            .values = cuda_matrix<element_t>(context)});
    }
    const matrix_extent in_use{.row_count = total_count, .col_count = width};
    for (const auto [block_index, store] : std::views::enumerate(cache.blocks))
    {
      const auto keys = store.keys[{row_ndx{0}, col_ndx{0}}, in_use];
      const auto values = store.values[{row_ndx{0}, col_ndx{0}}, in_use];
      if (!apply_block(residual, residual, static_cast<size_t>(block_index),
              acts, keys, values))
        return false;
    }
    if (!layer_norm(out, residual, ln_f_weight_, ln_f_bias_,
            gpt2_layer_norm_eps))
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

    const matrix_extent extent{.row_count = new_count, .col_count = width};
    cuda_matrix<element_t> trunk(extent);
    cuda_matrix<wide_t> residual(extent);
    block_activation_buffers buffers(new_count, width,
        blocks_.front().mlp_c_fc_weight.col_extent(), head_count_,
        cached_count);
    if (!forward(trunk, residual, new_ids, buffers.lenses(), cache_))
      return false;

    cuda_matrix<wide_t> logits({.row_count = 1, .col_count = vocab_size});
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
      if (next == gpt2_end_of_text) return true;
      ids.push_back(next);
    }
    return true;
  }

#pragma endregion
#pragma region block_params
private:
  // The parameters of one block, uploaded from `gpt2_block_params`.
  //
  // The names and shapes are those of the CPU bundle, except that the
  // attention projection's bias is held as its three thirds, since the
  // queries' and keys' are `wide_t` and the values' `element_t`. Its weight
  // stays whole, read a third at a time. The layer norms' weights and biases
  // and the biases of the two projections that write to the residual stream
  // are `wide_t`. The rest are `element_t`.
  struct block_params {
    cuda_buffer<wide_t> ln_1_weight;
    cuda_buffer<wide_t> ln_1_bias;
    cuda_matrix<element_t> attn_c_attn_weight;
    cuda_buffer<wide_t> attn_c_attn_query_bias;
    cuda_buffer<wide_t> attn_c_attn_key_bias;
    cuda_buffer<element_t> attn_c_attn_value_bias;
    cuda_matrix<element_t> attn_c_proj_weight;
    cuda_buffer<wide_t> attn_c_proj_bias;
    cuda_buffer<wide_t> ln_2_weight;
    cuda_buffer<wide_t> ln_2_bias;
    cuda_matrix<element_t> mlp_c_fc_weight;
    cuda_buffer<element_t> mlp_c_fc_bias;
    cuda_matrix<element_t> mlp_c_proj_weight;
    cuda_buffer<wide_t> mlp_c_proj_bias;

    // Allocate and upload every parameter of `host`, or throw.
    template<ParameterElement H>
    explicit block_params(const gpt2_block_params<H>& host)
        : ln_1_weight(upload<wide_t>(host.ln_1_weight)),
          ln_1_bias(upload<wide_t>(host.ln_1_bias)),
          attn_c_attn_weight(upload<element_t>(host.attn_c_attn_weight)),
          attn_c_attn_query_bias(
              upload<wide_t>(attn_c_attn_bias_third(host, 0))),
          attn_c_attn_key_bias(
              upload<wide_t>(attn_c_attn_bias_third(host, 1))),
          attn_c_attn_value_bias(
              upload<element_t>(attn_c_attn_bias_third(host, 2))),
          attn_c_proj_weight(upload<element_t>(host.attn_c_proj_weight)),
          attn_c_proj_bias(upload<wide_t>(host.attn_c_proj_bias)),
          ln_2_weight(upload<wide_t>(host.ln_2_weight)),
          ln_2_bias(upload<wide_t>(host.ln_2_bias)),
          mlp_c_fc_weight(upload<element_t>(host.mlp_c_fc_weight)),
          mlp_c_fc_bias(upload<element_t>(host.mlp_c_fc_bias)),
          mlp_c_proj_weight(upload<element_t>(host.mlp_c_proj_weight)),
          mlp_c_proj_bias(upload<wide_t>(host.mlp_c_proj_bias)) {}

    // The columns of the attention projection's weight that produce the
    // queries, the keys, and the values.
    [[nodiscard]] cuda_matrix_view<element_t>
    attn_c_attn_query_weight() const {
      return attn_c_attn_weight_third(0);
    }
    [[nodiscard]] cuda_matrix_view<element_t> attn_c_attn_key_weight() const {
      return attn_c_attn_weight_third(1);
    }
    [[nodiscard]] cuda_matrix_view<element_t>
    attn_c_attn_value_weight() const {
      return attn_c_attn_weight_third(2);
    }

  private:
    [[nodiscard]] cuda_matrix_view<element_t> attn_c_attn_weight_third(
        size_t which) const {
      const auto width = attn_c_attn_weight.row_extent();
      return attn_c_attn_weight[{row_ndx{0}, col_ndx{which * width}},
          {.row_count = width, .col_count = width}];
    }

    // The third of `host`'s attention projection bias that `which` names.
    template<ParameterElement H>
    [[nodiscard]] static gpt2_block_params<H>::vector_t
    attn_c_attn_bias_third(const gpt2_block_params<H>& host, size_t which) {
      const auto width = host.attn_c_attn_weight.row_extent();
      return host.attn_c_attn_bias.subspan(col_ndx{which * width}, width);
    }
  };

#pragma endregion
#pragma region Upload

  // Upload `host`, a parameter held as `H`, as a matrix of `U`, or throw.
  //
  // A parameter already held as `U` goes straight up. Any other type is
  // staged as held and converted on the device.
  template<DeviceFloating U, ParameterElement H>
  requires std::same_as<U, H>
  [[nodiscard]] static cuda_matrix<U> upload(matrix_view<H> host) {
    return cuda_matrix<U>(host);
  }
  template<DeviceFloating U, ParameterElement H>
  [[nodiscard]] static cuda_matrix<U> upload(matrix_view<H> host) {
    const cuda_matrix<H> staged(host);
    cuda_matrix<U> result(host.extent());
    if (!convert(result, staged)) raise_refused();
    return result;
  }

  // Upload `host`, a parameter held as `H`, as a buffer of `U`, or throw.
  //
  // The same two paths as the matrix form, with the buffer converted as one
  // row.
  template<DeviceFloating U, ParameterElement H>
  requires std::same_as<U, H>
  [[nodiscard]] static cuda_buffer<U>
  upload(enum_span<const H, col_ndx> host) {
    return cuda_buffer<U>(host);
  }
  template<DeviceFloating U, ParameterElement H>
  [[nodiscard]] static cuda_buffer<U>
  upload(enum_span<const H, col_ndx> host) {
    const cuda_buffer<H> staged(host);
    cuda_buffer<U> result(host.size());
    const matrix_extent row{.row_count = 1, .col_count = host.size()};
    if (!convert(cuda_matrix_lens<U>(result.as_span(), row),
            cuda_matrix_view<H>(staged.as_span(), row)))
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
  cuda_buffer<wide_t> ln_f_weight_;
  cuda_buffer<wide_t> ln_f_bias_;
  size_t head_count_;
  mutable kv_cache cache_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda::llm
