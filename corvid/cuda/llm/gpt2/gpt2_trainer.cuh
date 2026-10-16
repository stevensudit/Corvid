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
#include <cstdint>
#include <random>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "../../../containers/utils/interval.h"
#include "../../../containers/utils/matrix_view.h"
#include "../../../linalg/linear_algebra.h"
#include "../../../llm/token_id.h"
#include "../../cuda_buffer.cuh"
#include "../../cuda_cublas.cuh"
#include "../../cuda_matrix.cuh"
#include "../../linalg/linear_algebra.cuh"
#include "gpt2_engine.cuh"
#include "../llm_ops.cuh"

// Training GPT-2 on the device.
//
// `gpt2_trainer<T>` runs the model of a `gpt2_engine<T>` over a batch of token
// sequences and measures how badly it predicts each token from the ones
// before it, as one number, the loss. Every activation on the way is kept in
// its own storage, which is what the backward pass reads to find how much the
// loss moves per unit of each parameter, its gradient. What training moves is
// not the model but a low-rank adapter (LoRA) on each block's attention
// projection, which `step` nudges by AdamW and the engine can then run with.
//
//   gpt2_trainer<float> trainer(engine, 4, 64);
//   float loss{};
//   if (!trainer.forward(loss, ids)) ...
//   if (!trainer.backward()) ...
//   const auto& gradients = trainer.gradients();
//   for (...) if (!trainer.step(loss, batch_ids)) ...
//   if (!engine.generate(ids, 20, trainer.adapter_views())) ...
namespace corvid::cuda::llm {

#pragma region gpt2_trainer

// The GPT-2 training pass on the device, over a `gpt2_engine` that must
// outlive it.
//
// Inference runs one sequence, reuses one set of activations for every block,
// and scores only the last token. Training runs a batch of sequences, keeps
// every block's activations, and scores every token against the one that
// actually follows it.
//
// The buffers for all of it are allocated at construction, for one batch
// shape, and so are the adapters, one per block. Each `forward` overwrites the
// buffers, each `backward` overwrites the gradients, and each `step` moves the
// adapters.
template<typename T>
requires TrainerElement<T>
class gpt2_trainer {
public:
  using element_t = T;
  using engine_t = gpt2_engine<element_t>;
  using wide_t = engine_t::wide_t;
  using c_attn_adapter = engine_t::c_attn_adapter;

#pragma region Adapters

  // The shape of the adapters on the attention projections.
  //
  //   rank   R, the width of an adapter's narrow middle
  //   alpha  the adapters' strength, applied as alpha / R
  //   seed   seeds the down projections' random start
  struct lora_options {
    size_t rank = 8;
    wide_t alpha = 16;
    uint32_t seed = 0;
  };

  // A parameter that the optimizer moves, with its gradient and its AdamW
  // state.
  //
  // For a parameter of extent E:
  //
  //   value        E  the parameter, as the forward pass reads it
  //   gradient     E  the gradient of the batch's loss with respect to it
  //   mean         E  AdamW's running average of the gradient
  //   mean_square  E  AdamW's running average of its square
  //
  // The averages start at zero, as `adamw_step` requires.
  struct trained_parameter {
    cuda_matrix<element_t> value;
    cuda_matrix<wide_t> gradient;
    cuda_buffer<wide_t> mean;
    cuda_buffer<wide_t> mean_square;

    // Take `start` as the parameter's value, allocating the rest, or throw.
    explicit trained_parameter(cuda_matrix<element_t>&& start)
        : value(std::move(start)), gradient(value.extent()),
          mean(value.size()), mean_square(value.size()) {
      mean.zero().or_throw();
      mean_square.zero().or_throw();
    }

    // Move `value` one AdamW step against `gradient`, the `step`th.
    [[nodiscard]] bool
    update(size_t step, const adamw_options<wide_t>& options) {
      return adamw_step(value.buffer(), gradient.buffer(), mean, mean_square,
          step, options);
    }
  };

  // One block's adapter on its attention projection, owned, as the engine's
  // `c_attn_adapter` views it.
  //
  // `a` starts small and random and `b` at zero, so a fresh adapter adds
  // nothing until its first step, and the model runs as it was.
  struct lora_adapter {
    trained_parameter a;
    trained_parameter b;
    wide_t scale;

    // Allocate for width `width` under `options`, drawing `a` from `rng`, or
    // throw.
    lora_adapter(size_t width, const lora_options& options, std::mt19937& rng)
        : a(uniform_matrix({.row_count = width, .col_count = options.rank},
              rng)),
          b(zero_matrix({.row_count = options.rank, .col_count = 3 * width})),
          scale(options.alpha / static_cast<wide_t>(options.rank)) {}

    // The adapter as `apply_block` takes it.
    [[nodiscard]] c_attn_adapter views() const noexcept {
      return {.a = a.value, .b = b.value, .scale = scale};
    }
  };

#pragma endregion
#pragma region Buffers

  // What one block's pass over one sequence leaves behind.
  //
  // For T tokens of width C and MLP width F, with an adapter of rank R:
  //
  //   activations  the engine's `block_activation_buffers`, its
  //                `adapter_hidden` [T, R] among them
  //   hidden_pre   [T, F]  the MLP's widened rows before `gelu_new`
  //   keys         [T, C]  the block's keys
  //   values       [T, C]  the block's values
  //
  // `hidden_pre` has storage of its own here, so `gelu_new` does not overwrite
  // what it read.
  struct block_buffers {
    engine_t::block_activation_buffers activations;
    cuda_matrix<element_t> hidden_pre;
    cuda_matrix<wide_t> keys;
    cuda_matrix<element_t> values;

    // Allocate every buffer, or throw.
    block_buffers(size_t token_count, size_t width, size_t hidden_width,
        size_t head_count, size_t lora_rank)
        : activations(token_count, width, hidden_width, head_count, 0,
              lora_rank),
          hidden_pre(activations.hidden.extent()),
          keys(activations.queries.extent()), values(keys.extent()) {}

    // The lenses `apply_block` takes.
    [[nodiscard]] engine_t::block_activations lenses() noexcept {
      auto result = activations.lenses();
      result.hidden_pre = hidden_pre;
      return result;
    }
  };

  // What the passes over one sequence leave behind.
  //
  // For T tokens of width C through L blocks of H heads and MLP width F, with
  // V vocabulary entries and adapters of rank R:
  //
  //   ids                [T]               the sequence's token IDs
  //   residuals          [L + 1] x [T, C]  the residual stream entering each
  //                                        block, then the one leaving the
  //                                        last
  //   blocks             [L]               each block's `block_buffers`
  //   trunk              [T, C]            the final layer norm's output
  //   logit_gradient     [T - 1, V]        the gradient of the batch's loss
  //                                        with respect to each logit
  //   losses             [T - 1]           each prediction's loss
  //   trunk_gradient     [T, C]            the gradient with respect to the
  //                                        trunk
  //   residual_gradient  [T, C]            the gradient with respect to the
  //                                        residual stream, at the point the
  //                                        backward pass has reached
  //   hidden_gradient    [T, F]            the gradient with respect to a
  //                                        block's `hidden`, then to its
  //                                        `hidden_pre`
  //   ln_2_out_gradient  [T, C]            the gradient with respect to a
  //                                        block's `ln_2_out`
  //   heads_out_gradient [T, C]            the gradient with respect to a
  //                                        block's `heads_out`
  //   qkv_gradient       [T, 3C]           the gradient with respect to a
  //                                        block's queries, keys, and
  //                                        values, as the thirds of one
  //                                        matrix
  //   weights_gradient   [HT, T]           the gradient with respect to a
  //                                        block's attention weights, then
  //                                        to its scores
  //   ln_1_out_gradient  [T, C]            the gradient with respect to a
  //                                        block's `ln_1_out`
  //   adapter_hidden_gradient  [T, R]      the gradient with respect to a
  //                                        block's `adapter_hidden`
  //
  // The last token of a sequence predicts nothing, since nothing follows it,
  // so the logits and losses have a row for every token but the last, and the
  // last row of the trunk's gradient is zero. The backward pass walks the
  // blocks from the last to the first, and the gradients with respect to a
  // block's activations are those of the block it reached last.
  struct sequence_buffers {
    cuda_buffer<token_id> ids;
    std::vector<cuda_matrix<wide_t>> residuals;
    std::vector<block_buffers> blocks;
    cuda_matrix<element_t> trunk;
    cuda_matrix<wide_t> logit_gradient;
    cuda_buffer<wide_t> losses;
    cuda_matrix<wide_t> trunk_gradient;
    cuda_matrix<wide_t> residual_gradient;
    cuda_matrix<wide_t> hidden_gradient;
    cuda_matrix<wide_t> ln_2_out_gradient;
    cuda_matrix<wide_t> heads_out_gradient;
    cuda_matrix<wide_t> qkv_gradient;
    cuda_matrix<wide_t> weights_gradient;
    cuda_matrix<wide_t> ln_1_out_gradient;
    cuda_matrix<wide_t> adapter_hidden_gradient;

    // Allocate every buffer for `token_count` tokens through the model of
    // `engine` with adapters of rank `lora_rank`, or throw.
    sequence_buffers(const engine_t& engine, size_t token_count,
        size_t lora_rank)
        : ids(token_count),
          trunk({.row_count = token_count,
              .col_count = engine.wte_.col_extent()}),
          logit_gradient({.row_count = token_count - 1,
              .col_count = engine.wte_.row_extent()}),
          losses(logit_gradient.row_extent()), trunk_gradient(trunk.extent()),
          residual_gradient(trunk.extent()),
          hidden_gradient({.row_count = token_count,
              .col_count =
                  engine.blocks_.front().mlp_c_fc_weight.col_extent()}),
          ln_2_out_gradient(trunk.extent()),
          heads_out_gradient(trunk.extent()),
          qkv_gradient({.row_count = token_count,
              .col_count =
                  engine.blocks_.front().attn_c_attn_weight.col_extent()}),
          weights_gradient({.row_count = engine.head_count_ * token_count,
              .col_count = token_count}),
          ln_1_out_gradient(trunk.extent()),
          adapter_hidden_gradient(
              {.row_count = token_count, .col_count = lora_rank}) {
      // The backward pass writes every row of the trunk's gradient but the
      // last, which stays zero from here on.
      trunk_gradient.buffer().zero().or_throw();
      const auto block_count = engine.blocks_.size();
      const auto hidden_width = hidden_gradient.col_extent();
      residuals.reserve(block_count + 1);
      for (auto boundary = 0UZ; boundary <= block_count; ++boundary)
        residuals.emplace_back(trunk.extent());
      blocks.reserve(block_count);
      for (auto block = 0UZ; block < block_count; ++block)
        blocks.emplace_back(token_count, trunk.col_extent(), hidden_width,
            engine.head_count_, lora_rank);
    }

    // The rows of `trunk` that predict, which is every row but the last.
    [[nodiscard]] cuda_matrix_view<element_t> predicting() const noexcept {
      return trunk[{.row_count = logit_gradient.row_extent()}];
    }
  };

  // The gradient of the batch's loss with respect to each parameter of one
  // block.
  //
  // The names and shapes are those of `block_params`, except that the
  // attention projection's bias is whole, as `gpt2_block_params` holds it,
  // since the backward step takes that projection as one. For width C and MLP
  // width F:
  //
  //   ln_1_weight         [C]
  //   ln_1_bias           [C]
  //   attn_c_attn_weight  [C, 3C]
  //   attn_c_attn_bias    [3C]
  //   attn_c_proj_weight  [C, C]
  //   attn_c_proj_bias    [C]
  //   ln_2_weight         [C]
  //   ln_2_bias           [C]
  //   mlp_c_fc_weight     [C, F]
  //   mlp_c_fc_bias       [F]
  //   mlp_c_proj_weight   [F, C]
  //   mlp_c_proj_bias     [C]
  struct block_gradients {
    cuda_buffer<wide_t> ln_1_weight;
    cuda_buffer<wide_t> ln_1_bias;
    cuda_matrix<wide_t> attn_c_attn_weight;
    cuda_buffer<wide_t> attn_c_attn_bias;
    cuda_matrix<wide_t> attn_c_proj_weight;
    cuda_buffer<wide_t> attn_c_proj_bias;
    cuda_buffer<wide_t> ln_2_weight;
    cuda_buffer<wide_t> ln_2_bias;
    cuda_matrix<wide_t> mlp_c_fc_weight;
    cuda_buffer<wide_t> mlp_c_fc_bias;
    cuda_matrix<wide_t> mlp_c_proj_weight;
    cuda_buffer<wide_t> mlp_c_proj_bias;

    // Allocate a gradient for each parameter of `params`, or throw.
    explicit block_gradients(const engine_t::block_params& params)
        : ln_1_weight(params.ln_1_weight.size()),
          ln_1_bias(params.ln_1_bias.size()),
          attn_c_attn_weight(params.attn_c_attn_weight.extent()),
          attn_c_attn_bias(params.attn_c_attn_weight.col_extent()),
          attn_c_proj_weight(params.attn_c_proj_weight.extent()),
          attn_c_proj_bias(params.attn_c_proj_bias.size()),
          ln_2_weight(params.ln_2_weight.size()),
          ln_2_bias(params.ln_2_bias.size()),
          mlp_c_fc_weight(params.mlp_c_fc_weight.extent()),
          mlp_c_fc_bias(params.mlp_c_fc_bias.size()),
          mlp_c_proj_weight(params.mlp_c_proj_weight.extent()),
          mlp_c_proj_bias(params.mlp_c_proj_bias.size()) {}

    // Set every gradient to zero.
    [[nodiscard]] bool zero() {
      return ln_1_weight.zero() && ln_1_bias.zero() &&
             attn_c_attn_weight.buffer().zero() && attn_c_attn_bias.zero() &&
             attn_c_proj_weight.buffer().zero() && attn_c_proj_bias.zero() &&
             ln_2_weight.zero() && ln_2_bias.zero() &&
             mlp_c_fc_weight.buffer().zero() && mlp_c_fc_bias.zero() &&
             mlp_c_proj_weight.buffer().zero() && mlp_c_proj_bias.zero();
    }
  };

  // The gradient of the batch's loss with respect to each parameter.
  //
  // For width C through L blocks, with V vocabulary entries and a context of
  // P positions:
  //
  //   wte          [V, C]  the token embeddings, over both their uses, as the
  //                        embedding lookup and as the output head
  //   wpe          [P, C]  the position embeddings
  //   blocks       [L]     each block's `block_gradients`
  //   ln_f_weight  [C]     the final layer norm's weight
  //   ln_f_bias    [C]     the final layer norm's bias
  //
  // Every gradient is `wide_t`, whatever its parameter holds.
  struct parameter_gradients {
    cuda_matrix<wide_t> wte;
    cuda_matrix<wide_t> wpe;
    std::vector<block_gradients> blocks;
    cuda_buffer<wide_t> ln_f_weight;
    cuda_buffer<wide_t> ln_f_bias;

    // Allocate a gradient for each parameter of `engine`, or throw.
    explicit parameter_gradients(const engine_t& engine)
        : wte(engine.wte_.extent()), wpe(engine.wpe_.extent()),
          ln_f_weight(engine.ln_f_weight_.size()),
          ln_f_bias(engine.ln_f_bias_.size()) {
      blocks.reserve(engine.blocks_.size());
      for (const auto& block : engine.blocks_) blocks.emplace_back(block);
    }

    // Set every gradient to zero.
    [[nodiscard]] bool zero() {
      if (!wte.buffer().zero() || !wpe.buffer().zero()) return false;
      for (auto& block : blocks)
        if (!block.zero()) return false;
      return ln_f_weight.zero() && ln_f_bias.zero();
    }
  };

#pragma endregion
#pragma region Construction

  // Allocate the buffers for batches of `sequence_count` sequences of
  // `token_count` tokens each, and the adapters `lora` shapes, or throw.
  //
  // There must be at least one sequence, of at least two tokens and no more
  // than the context holds, and the rank must not be zero. `adamw` sets how
  // `step` moves the adapters.
  gpt2_trainer(const engine_t& engine, size_t sequence_count,
      size_t token_count, lora_options lora = {},
      adamw_options<wide_t> adamw = {})
      : engine_{engine}, adamw_{adamw}, gradients_(engine),
        statistics_(token_count),
        norm_in_gradient_({.row_count = token_count,
            .col_count = engine.wte_.col_extent()}) {
    assert(sequence_count);
    assert((token_count > 1) && (token_count <= engine.wpe_.row_extent()));
    assert(lora.rank);
    sequences_.reserve(sequence_count);
    for (auto sequence = 0UZ; sequence < sequence_count; ++sequence)
      sequences_.emplace_back(engine, token_count, lora.rank);

    std::mt19937 rng(lora.seed);
    const auto block_count = engine.blocks_.size();
    adapters_.reserve(block_count);
    adapter_views_.reserve(block_count);
    for (auto block = 0UZ; block < block_count; ++block) {
      adapters_.emplace_back(engine.wte_.col_extent(), lora, rng);
      adapter_views_.push_back(adapters_.back().views());
    }
  }

#pragma endregion
#pragma region Forward pass

  // Run the model over the batch `ids`, a row per sequence, and set `loss` to
  // the mean cross-entropy loss of every token's prediction of the next one.
  //
  // Each sequence runs on its own, through the engine's `apply_block`, into
  // its own `sequence_buffers`. For GPT-2 with T tokens in a sequence:
  //
  // step           |  reads              |  produces
  // ---------------+---------------------+--------------------------------
  // embed          |  ids [T]            |  residuals[0] [T, 768]
  // block n        |  residuals[n]       |  residuals[n + 1] [T, 768]
  // ln_f           |  residuals[12]      |  trunk [T, 768]
  // logits         |  trunk [:T - 1]     |  logit_gradient [T - 1, 50257]
  // cross_entropy  |  logit_gradient,    |  logit_gradient, in place,
  //                |  ids [1:]           |  losses [T - 1]
  //
  // where `trunk [:T - 1]` is every row of the trunk but the last and
  // `ids [1:]` is every ID but the first. Row t of the trunk predicts token
  // t + 1, so its label (the ID `cross_entropy` scores the prediction
  // against) is the next ID of its own sequence, and the last row, which no
  // ID follows, predicts nothing.
  //
  // That leaves T - 1 predictions per sequence. With 4 sequences of 64 tokens,
  // it is 63 per sequence and 4 * 63 = 252 in the batch. The loss is the mean
  // over all of them, so 252 is also the count that `cross_entropy` divides
  // each gradient by.
  //
  // `ids` must have the batch shape given at construction. On failure (a
  // refused launch or transfer), returns false, leaving `loss` untouched and
  // the buffers unspecified.
  [[nodiscard]] bool forward(wide_t& loss, matrix_view<token_id> ids) {
    assert(ids.row_extent() == sequences_.size());
    assert(ids.col_extent() == sequences_.front().trunk.row_extent());
    const auto prediction_count = ids.col_extent() - 1;
    const auto label_count = sequences_.size() * prediction_count;
    for (auto [sequence, sequence_ids] :
        std::views::zip(sequences_, ids.rows()))
      if (!forward_sequence(sequence, sequence_ids, label_count)) return false;

    std::vector<wide_t> losses(prediction_count);
    wide_t total{};
    for (const auto& sequence : sequences_) {
      if (!sequence.losses.store(losses)) return false;
      total += corvid::linalg::sum(losses);
    }

    loss = total / static_cast<wide_t>(label_count);
    return true;
  }

  // What the last `forward` left behind, per sequence.
  [[nodiscard]] std::span<const sequence_buffers> sequences() const noexcept {
    return sequences_;
  }

#pragma endregion
#pragma region Backward pass

  // Fill `gradients()` and the adapters' gradients by calculating how much the
  // loss of the last `forward` moves per unit of each parameter.
  //
  // Each sequence's backward pass adds into the one set of gradients, so the
  // set is the batch's. For GPT-2 with T tokens in a sequence, walking back
  // from the logits:
  //
  // step       |  reads                        |  produces
  // -----------+-------------------------------+---------------------------
  // head       |  logit_gradient [T - 1, V],   |  trunk_gradient [:T - 1]
  //            |  wte [V, C]                   |
  //            |  logit_gradient,              |  wte gradient [V, C], added
  //            |  trunk [:T - 1]               |  to
  // ln_f       |  trunk_gradient [T, C],       |  residual_gradient [T, C],
  //            |  residuals[12] [T, C],        |  ln_f weight and bias
  //            |  ln_f weight [C]              |  gradients [C], added to
  // block n,   |  residual_gradient,           |  residual_gradient, now with
  // n from 11  |  block n's activations and    |  respect to residuals[n],
  // down to 0  |  its parameters, its adapter, |  its twelve parameter
  //            |  and residuals[n]             |  gradients and its
  //            |                               |  adapter's two, added to
  // embed      |  residual_gradient, ids [T]   |  wpe gradient [:T] and wte
  //            |                               |  gradient, added to
  //
  // where `[:T - 1]` is every row but the last and `[:T]` the first T rows.
  // The last token predicts nothing, so the last row of `trunk_gradient` is
  // zero. A block walks back in two halves, the MLP's and then the
  // attention's, each from the stream after its residual add to the stream
  // before it.
  //
  // Must follow a successful `forward`. On failure (a refused launch or
  // fill), returns false, leaving the gradients and the buffers unspecified.
  [[nodiscard]] bool backward() {
    if (!gradients_.zero()) return false;
    for (auto& adapter : adapters_)
      if (!adapter.a.gradient.buffer().zero() ||
          !adapter.b.gradient.buffer().zero())
        return false;
    for (auto& sequence : sequences_)
      if (!backward_sequence(sequence)) return false;
    return true;
  }

  // What the last `backward` left behind.
  [[nodiscard]] const parameter_gradients& gradients() const noexcept {
    return gradients_;
  }

#pragma endregion
#pragma region Training step

  // Run `forward` and `backward` over the batch `ids`, then move every
  // adapter parameter one AdamW step, setting `loss` to the batch's loss
  // before the step.
  //
  // Only the adapters move. The model's own parameters keep their gradients
  // in `gradients()` and stay as they are. On failure, returns false, leaving
  // the adapters and the buffers unspecified.
  [[nodiscard]] bool step(wide_t& loss, matrix_view<token_id> ids) {
    if (!forward(loss, ids) || !backward()) return false;
    ++step_count_;
    for (auto& adapter : adapters_)
      if (!adapter.a.update(step_count_, adamw_) ||
          !adapter.b.update(step_count_, adamw_))
        return false;
    return true;
  }

  // The adapters, one per block.
  [[nodiscard]] std::span<const lora_adapter> adapters() const noexcept {
    return adapters_;
  }

  // The adapters as the engine takes them, to run the model with them.
  [[nodiscard]] std::span<const c_attn_adapter>
  adapter_views() const noexcept {
    return adapter_views_;
  }

#pragma endregion
#pragma region Helpers
private:
  // Run the model over one sequence, `ids`, filling `sequence`, with the
  // gradient that of a mean loss over `label_count` predictions.
  [[nodiscard]] bool forward_sequence(sequence_buffers& sequence,
      std::span<const token_id> ids, size_t label_count) const {
    auto& residuals = sequence.residuals;
    if (!sequence.ids.load(ids)) return false;
    if (!embed_tokens(residuals.front(), sequence.ids, engine_.wte_))
      return false;
    if (!embed_positions(residuals.front(), engine_.wpe_)) return false;

    for (const auto [block_index, block] :
        std::views::enumerate(sequence.blocks))
    {
      const auto entering = static_cast<size_t>(block_index);
      if (!engine_.apply_block(residuals[entering + 1], residuals[entering],
              entering, block.lenses(), block.keys, block.values,
              adapter_views_[entering]))
        return false;
    }
    if (!layer_norm(sequence.trunk, residuals.back(), engine_.ln_f_weight_,
            engine_.ln_f_bias_, gpt2_layer_norm_eps))
      return false;

    if (!compute_logits(engine_.blas_, sequence.logit_gradient,
            sequence.predicting(), engine_.wte_))
      return false;
    // The label of each row is the ID after it, and the last row has none.
    return cross_entropy(sequence.logit_gradient, sequence.losses,
        sequence.logit_gradient, sequence.ids.as_span().subspan(1),
        label_count);
  }

  // Walk the gradient of the loss back from the logits through the head, the
  // final layer norm, every block, and the embeddings for one sequence,
  // filling `sequence`'s gradients and adding to the parameters'.
  [[nodiscard]] bool backward_sequence(sequence_buffers& sequence) {
    const auto& logit_gradient = sequence.logit_gradient;
    const auto predicting = sequence.predicting();
    const auto predicting_gradient =
        sequence.trunk_gradient[predicting.extent()];

    // Fill `predicting_gradient`.
    //
    // A logit is the dot product of a trunk row with a `wte` row:
    //
    //   logits[t, v] = sum over c of trunk[t, c] * wte[v, c]
    //
    // Nudging `trunk[t, c]` by a small `step` moves `logits[t, v]` by
    // `wte[v, c] * step` for every `v`, and the loss by `logit_gradient[t, v]`
    // times that, so the trunk's gradient at `[t, c]` is the sum over `v` of
    // `logit_gradient[t, v] * wte[v, c]`. That is the product of
    // `logit_gradient` [T - 1, V] and `wte` [V, C].
    if (!gemm(engine_.blas_, predicting_gradient, logit_gradient,
            engine_.wte_))
      return false;

    // Fill `gradients_.wte`.
    //
    // Nudging `wte[v, c]` by `step` moves `logits[t, v]` by `step` times
    // `trunk[t, c]` in every row t, so its gradient is the sum over `t` of
    // `logit_gradient[t, v] * trunk[t, c]`. That is the product of the
    // transposed `logit_gradient` [V, T - 1] and the predicting rows of the
    // `trunk` [T - 1, C]. The sum over this sequence's rows is the product
    // itself, and the sum over the batch's sequences is the product added to
    // what the earlier sequences left.
    if (!gemm(engine_.blas_, gradients_.wte, logit_gradient, predicting,
            {.op_a = cublas_operation::transpose}, gradients_.wte))
      return false;

    if (!layer_norm_backward(sequence.residual_gradient,
            gradients_.ln_f_weight, gradients_.ln_f_bias,
            sequence.trunk_gradient, sequence.residuals.back(),
            engine_.ln_f_weight_, gpt2_layer_norm_eps, statistics_))
      return false;

    for (const auto block_index :
        std::views::reverse(iota(sequence.blocks.size())))
    {
      if (!backward_mlp(sequence, block_index)) return false;
      if (!backward_attention(sequence, block_index)) return false;
    }

    // Add to `gradients_.wpe` and `gradients_.wte`.
    //
    // The stream entered block 0 as `wte[ids[t]] + wpe[t]` in each row `t`.
    // Nudging either by a small `step` moves the row by `step`, so each row of
    // `residual_gradient`, now with respect to that stream, adds to row `t` of
    // wpe's gradient and to the row of wte's that the token's ID names.
    const auto& residual_gradient = sequence.residual_gradient;
    const auto wpe_gradient = gradients_.wpe[residual_gradient.extent()];
    if (!add(wpe_gradient, wpe_gradient, residual_gradient)) return false;
    return embed_tokens_backward(gradients_.wte, residual_gradient,
        sequence.ids);
  }

  // Walk the gradient back through the MLP half of block `block_index` for one
  // sequence, from `residual_gradient` as the gradient with respect to the
  // stream leaving the block to the gradient with respect to its `ln_2_in`,
  // adding to the block's parameter gradients.
  //
  // The block leaves `ln_2_in + mlp_out` on the stream. Nudging either by a
  // small `step` moves the sum by `step`, so the gradient with respect to
  // `mlp_out` is `residual_gradient` as it stands, and so is the direct share
  // of `ln_2_in`'s, which the walk through the MLP adds its own share to.
  [[nodiscard]] bool
  backward_mlp(sequence_buffers& sequence, size_t block_index) {
    const auto& params = engine_.blocks_[block_index];
    auto& gradients = gradients_.blocks[block_index];
    const auto& block = sequence.blocks[block_index];
    const auto& acts = block.activations;
    auto& residual_gradient = sequence.residual_gradient;

    // Fill `hidden_gradient` and add to c_proj's gradients.
    //
    // `mlp_out` is `hidden` [T, F] projected through c_proj, so this is the
    // projection's backward step with `hidden` as the input.
    if (!linear_projection_backward(engine_.blas_, sequence.hidden_gradient,
            gradients.mlp_c_proj_weight, gradients.mlp_c_proj_bias,
            residual_gradient, acts.hidden, params.mlp_c_proj_weight))
      return false;

    // Replace `hidden_gradient` with the gradient with respect to
    // `hidden_pre`, in place.
    if (!gelu_new_backward(sequence.hidden_gradient, sequence.hidden_gradient,
            block.hidden_pre))
      return false;

    // Fill `ln_2_out_gradient` and add to c_fc's gradients.
    //
    // `hidden_pre` is `ln_2_out` [T, C] projected through c_fc.
    if (!linear_projection_backward(engine_.blas_, sequence.ln_2_out_gradient,
            gradients.mlp_c_fc_weight, gradients.mlp_c_fc_bias,
            sequence.hidden_gradient, acts.ln_2_out, params.mlp_c_fc_weight))
      return false;

    // Fill `norm_in_gradient_` and add to ln_2's gradients, then add the
    // direct share, which is `residual_gradient` as it stands.
    if (!layer_norm_backward(norm_in_gradient_, gradients.ln_2_weight,
            gradients.ln_2_bias, sequence.ln_2_out_gradient, acts.ln_2_in,
            params.ln_2_weight, gpt2_layer_norm_eps, statistics_))
      return false;
    return add(residual_gradient, residual_gradient, norm_in_gradient_);
  }

  // `backward_attention` walks the gradient back through the attention half of
  // block `block_index` for one sequence, from `residual_gradient` as the
  // gradient with respect to its `ln_2_in` to the gradient with respect to the
  // stream entering the block, adding to the block's parameter gradients.
  //
  // The block's `ln_2_in` is its entering stream plus `attn_out`, so the
  // gradient with respect to `attn_out` is `residual_gradient` as it stands,
  // and so is the direct share of the entering stream's, as in `backward_mlp`.
  [[nodiscard]] bool
  backward_attention(sequence_buffers& sequence, size_t block_index) {
    const auto& params = engine_.blocks_[block_index];
    auto& gradients = gradients_.blocks[block_index];
    const auto& block = sequence.blocks[block_index];
    const auto& acts = block.activations;
    auto& residual_gradient = sequence.residual_gradient;

    // Fill `heads_out_gradient` and add to c_proj's gradients.
    //
    // `attn_out` is `heads_out` [T, C] projected through c_proj.
    if (!linear_projection_backward(engine_.blas_, sequence.heads_out_gradient,
            gradients.attn_c_proj_weight, gradients.attn_c_proj_bias,
            residual_gradient, acts.heads_out, params.attn_c_proj_weight))
      return false;

    // Fill the thirds of `qkv_gradient`.
    //
    // The forward pass projected `ln_1_out` into the queries, keys, and values
    // a third at a time, so that each landed in its own store. Their gradients
    // land side by side, as the thirds of one [T, 3C] matrix, so that the
    // projection's backward step is one call over the whole weight.
    const auto third = acts.queries.extent();
    const auto width = third.col_count;
    if (!attend_backward(engine_.blas_, sequence.qkv_gradient[third],
            sequence.qkv_gradient[{row_ndx{0}, col_ndx{width}}, third],
            sequence.qkv_gradient[{row_ndx{0}, col_ndx{2 * width}}, third],
            sequence.heads_out_gradient, acts.queries, block.keys,
            block.values, acts.weights, engine_.head_count_,
            sequence.weights_gradient))
      return false;

    // Fill `ln_1_out_gradient` and add to c_attn's gradients.
    //
    // Nudging `ln_1_out[t, c]` by a small `step` moves all 3C columns of the
    // projection's output in row `t`, so its gradient is a sum over all of
    // them, which one step over the whole [C, 3C] weight takes.
    if (!linear_projection_backward(engine_.blas_, sequence.ln_1_out_gradient,
            gradients.attn_c_attn_weight, gradients.attn_c_attn_bias,
            sequence.qkv_gradient, acts.ln_1_out, params.attn_c_attn_weight))
      return false;
    if (!backward_adapter(sequence, block_index)) return false;

    // Fill `norm_in_gradient_` and add to ln_1's gradients, then add the
    // direct share, which is `residual_gradient` as it stands. The layer
    // norm's input is the stream entering the block.
    if (!layer_norm_backward(norm_in_gradient_, gradients.ln_1_weight,
            gradients.ln_1_bias, sequence.ln_1_out_gradient,
            sequence.residuals[block_index], params.ln_1_weight,
            gpt2_layer_norm_eps, statistics_))
      return false;
    return add(residual_gradient, residual_gradient, norm_in_gradient_);
  }

  // `backward_adapter` walks the gradient back through block `block_index`'s
  // adapter for one sequence, from `qkv_gradient` to `ln_1_out_gradient`,
  // adding to the adapter's gradients.
  //
  // The adapter added `scale * adapter_hidden x b` to the projection's output
  // [T, 3C], with `adapter_hidden` as `ln_1_out x a` [T, R]. Nudging
  // `adapter_hidden[t, k]` by a small `step` moves the output's `[t, j]` by
  // `scale * b[k, j] * step` for every `j`, and the loss by `qkv_gradient[t,
  // j]` times that, so the gradient with respect to `adapter_hidden` is
  // `scale` times `qkv_gradient x b^T`. Nudging `b[k, j]` moves `[t, j]` by
  // `scale * adapter_hidden[t, k] * step` in every row `t`, so b's gradient is
  // `scale` times `adapter_hidden^T x qkv_gradient`. From there the walk is a
  // projection's without a bias. a's gradient is `ln_1_out^T x
  // adapter_hidden_gradient`, and `ln_1_out` gets a second share,
  // `adapter_hidden_gradient x a^T`, beside the one the weight gave it.
  [[nodiscard]] bool
  backward_adapter(sequence_buffers& sequence, size_t block_index) {
    auto& adapter = adapters_[block_index];
    const auto& acts = sequence.blocks[block_index].activations;
    const auto& blas = engine_.blas_;
    auto& hidden_gradient = sequence.adapter_hidden_gradient;

    if (!gemm(blas, hidden_gradient, sequence.qkv_gradient, adapter.b.value,
            {.scale = adapter.scale, .op_b = cublas_operation::transpose}))
      return false;
    if (!gemm(blas, adapter.b.gradient, acts.adapter_hidden,
            sequence.qkv_gradient,
            {.scale = adapter.scale, .op_a = cublas_operation::transpose},
            adapter.b.gradient))
      return false;
    if (!gemm(blas, adapter.a.gradient, acts.ln_1_out, hidden_gradient,
            {.op_a = cublas_operation::transpose}, adapter.a.gradient))
      return false;
    return gemm(blas, sequence.ln_1_out_gradient, hidden_gradient,
        adapter.a.value, {.op_b = cublas_operation::transpose},
        sequence.ln_1_out_gradient);
  }

  // A matrix of `extent` filled with values drawn evenly from `-bound` to
  // `bound`, with `bound` 1 over the square root of the row count, which keeps
  // a product against it about the size of its input.
  [[nodiscard]] static cuda_matrix<element_t>
  uniform_matrix(matrix_extent extent, std::mt19937& rng) {
    const auto bound = 1 / std::sqrt(static_cast<wide_t>(extent.row_count));
    std::uniform_real_distribution<wide_t> draw(-bound, bound);
    std::vector<element_t> host(extent.size());
    for (auto& value : host) value = static_cast<element_t>(draw(rng));
    return cuda_matrix<element_t>(matrix_view<element_t>(host, extent));
  }

  // A matrix of `extent` filled with zeros.
  [[nodiscard]] static cuda_matrix<element_t> zero_matrix(
      matrix_extent extent) {
    cuda_matrix<element_t> result(extent);
    result.buffer().zero().or_throw();
    return result;
  }

#pragma endregion
#pragma region Data members

  const engine_t& engine_;
  adamw_options<wide_t> adamw_;
  std::vector<sequence_buffers> sequences_;
  parameter_gradients gradients_;
  // One adapter per block, and the same as the engine takes them.
  std::vector<lora_adapter> adapters_;
  std::vector<c_attn_adapter> adapter_views_;
  // How many steps `step` has taken, which AdamW's corrections count from.
  size_t step_count_{};
  cuda_buffer<row_statistics<wide_t>> statistics_;
  // A layer norm's input gradient, before the residual path's direct share is
  // added to it.
  cuda_matrix<wide_t> norm_in_gradient_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda::llm
