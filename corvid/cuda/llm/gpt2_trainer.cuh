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
#include <ranges>
#include <span>
#include <vector>

#include "../../containers/utils/matrix_view.h"
#include "../../linalg/linear_algebra.h"
#include "../../llm/token_id.h"
#include "../cuda_buffer.cuh"
#include "../cuda_cublas.cuh"
#include "../cuda_matrix.cuh"
#include "../linalg/linear_algebra.cuh"
#include "gpt2_engine.cuh"
#include "llm_ops.cuh"

// Training GPT-2 on the device.
//
// `gpt2_trainer<T>` runs the model of a `gpt2_engine<T>` over a batch of token
// sequences and measures how badly it predicts each token from the ones
// before it, as one number, the loss. Every activation on the way is kept in
// its own storage, which is what the backward pass reads to find how much the
// loss moves per unit of each parameter, its gradient.
//
//   gpt2_trainer<float> trainer(engine, 4, 64);
//   float loss{};
//   if (!trainer.forward(loss, ids)) ...
//   if (!trainer.backward()) ...
//   const auto& gradients = trainer.gradients();
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
// shape. Each `forward` overwrites them, and each `backward` overwrites the
// gradients.
template<typename T>
requires DeviceFloating<T> && GemmElement<T>
class gpt2_trainer {
public:
  using element_t = T;
  using engine_t = gpt2_engine<element_t>;
  using wide_t = engine_t::wide_t;

#pragma region Buffers

  // What one block's pass over one sequence leaves behind.
  //
  // For T tokens of width C and MLP width F:
  //
  //   activations  the engine's `block_activation_buffers`
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
        size_t head_count)
        : activations(token_count, width, hidden_width, head_count),
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
  // For T tokens of width C through L blocks, with V vocabulary entries:
  //
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
  //                                        residual stream leaving the last
  //                                        block
  //
  // The last token of a sequence predicts nothing, since nothing follows it,
  // so the logits and losses have a row for every token but the last, and the
  // last row of the trunk's gradient is zero.
  struct sequence_buffers {
    std::vector<cuda_matrix<wide_t>> residuals;
    std::vector<block_buffers> blocks;
    cuda_matrix<element_t> trunk;
    cuda_matrix<wide_t> logit_gradient;
    cuda_buffer<wide_t> losses;
    cuda_matrix<wide_t> trunk_gradient;
    cuda_matrix<wide_t> residual_gradient;

    // Allocate every buffer for `token_count` tokens through the model of
    // `engine`, or throw.
    sequence_buffers(const engine_t& engine, size_t token_count)
        : trunk({.row_count = token_count,
              .col_count = engine.wte_.col_extent()}),
          logit_gradient({.row_count = token_count - 1,
              .col_count = engine.wte_.row_extent()}),
          losses(logit_gradient.row_extent()), trunk_gradient(trunk.extent()),
          residual_gradient(trunk.extent()) {
      // The backward pass writes every row of the trunk's gradient but the
      // last, which stays zero from here on.
      trunk_gradient.buffer().zero().or_throw();
      const auto block_count = engine.blocks_.size();
      const auto hidden_width =
          engine.blocks_.front().mlp_c_fc_weight.col_extent();
      residuals.reserve(block_count + 1);
      for (auto boundary = 0UZ; boundary <= block_count; ++boundary)
        residuals.emplace_back(trunk.extent());
      blocks.reserve(block_count);
      for (auto block = 0UZ; block < block_count; ++block)
        blocks.emplace_back(token_count, trunk.col_extent(), hidden_width,
            engine.head_count_);
    }

    // The rows of `trunk` that predict, which is every row but the last.
    [[nodiscard]] cuda_matrix_view<element_t> predicting() const noexcept {
      return trunk[{row_ndx{0}, col_ndx{0}},
          {.row_count = logit_gradient.row_extent()}];
    }
  };

  // The gradient of the batch's loss with respect to each parameter.
  //
  // For width C and V vocabulary entries:
  //
  //   wte          [V, C]  the token embeddings, as the output head reads
  //                        them
  //   ln_f_weight  [C]     the final layer norm's weight
  //   ln_f_bias    [C]     the final layer norm's bias
  //
  // Every gradient is `wide_t`, whatever its parameter holds.
  struct parameter_gradients {
    cuda_matrix<wide_t> wte;
    cuda_buffer<wide_t> ln_f_weight;
    cuda_buffer<wide_t> ln_f_bias;

    // Allocate a gradient for each parameter of `engine`, or throw.
    explicit parameter_gradients(const engine_t& engine)
        : wte(engine.wte_.extent()), ln_f_weight(engine.ln_f_weight_.size()),
          ln_f_bias(engine.ln_f_bias_.size()) {}
  };

#pragma endregion
#pragma region Construction

  // Allocate the buffers for batches of `sequence_count` sequences of
  // `token_count` tokens each, or throw.
  //
  // There must be at least one sequence, of at least two tokens and no more
  // than the context holds.
  gpt2_trainer(const engine_t& engine, size_t sequence_count,
      size_t token_count)
      : engine_{engine}, gradients_(engine), statistics_(token_count) {
    assert(sequence_count);
    assert((token_count > 1) && (token_count <= engine.wpe_.row_extent()));
    sequences_.reserve(sequence_count);
    for (auto sequence = 0UZ; sequence < sequence_count; ++sequence)
      sequences_.emplace_back(engine, token_count);
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

  // Fill `gradients()` by calculating how much the loss of the last `forward`
  // moves per unit of each parameter.
  //
  // Each sequence's backward pass adds into the one set of gradients, so the
  // set is the batch's. For GPT-2 with T tokens in a sequence, walking back
  // from the logits:
  //
  // step  |  reads                        |  produces
  // ------+-------------------------------+--------------------------------
  // head  |  logit_gradient [T - 1, V],   |  trunk_gradient [:T - 1]
  //       |  wte [V, C]                   |
  //       |  logit_gradient,              |  wte gradient [V, C], added to
  //       |  trunk [:T - 1]               |
  // ln_f  |  trunk_gradient [T, C],       |  residual_gradient [T, C],
  //       |  residuals[12] [T, C],        |  ln_f weight and bias gradients
  //       |  ln_f weight [C]              |  [C], added to
  //
  // where `[:T - 1]` is every row but the last. The last token predicts
  // nothing, so the last row of `trunk_gradient` is zero.
  //
  // Must follow a successful `forward`. On failure (a refused launch or
  // fill), returns false, leaving the gradients and the buffers unspecified.
  [[nodiscard]] bool backward() {
    if (!gradients_.wte.buffer().zero()) return false;
    if (!gradients_.ln_f_weight.zero()) return false;
    if (!gradients_.ln_f_bias.zero()) return false;
    for (auto& sequence : sequences_)
      if (!backward_sequence(sequence)) return false;
    return true;
  }

  // What the last `backward` left behind.
  [[nodiscard]] const parameter_gradients& gradients() const noexcept {
    return gradients_;
  }

#pragma endregion
#pragma region Helpers
private:
  // Run the model over one sequence, `ids`, filling `sequence`, with the
  // gradient that of a mean loss over `label_count` predictions.
  [[nodiscard]] bool forward_sequence(sequence_buffers& sequence,
      std::span<const token_id> ids, size_t label_count) const {
    auto& residuals = sequence.residuals;
    const cuda_buffer<token_id> device_ids(ids);
    if (!embed_tokens(residuals.front(), device_ids, engine_.wte_))
      return false;
    if (!embed_positions(residuals.front(), engine_.wpe_)) return false;

    for (const auto [block_index, block] :
        std::views::enumerate(sequence.blocks))
    {
      const auto entering = static_cast<size_t>(block_index);
      if (!engine_.apply_block(residuals[entering + 1], residuals[entering],
              entering, block.lenses(), block.keys, block.values))
        return false;
    }
    if (!layer_norm(sequence.trunk, residuals.back(), engine_.ln_f_weight_,
            engine_.ln_f_bias_, gpt2_layer_norm_eps))
      return false;

    // The label of each row is the ID after it, and the last row has none.
    const cuda_buffer<token_id> device_labels(ids.subspan(1));
    if (!compute_logits(engine_.blas_, sequence.logit_gradient,
            sequence.predicting(), engine_.wte_))
      return false;
    return cross_entropy(sequence.logit_gradient, sequence.losses,
        sequence.logit_gradient, device_labels, label_count);
  }

  // Walk the gradient of the loss back from the logits through the head and
  // the final layer norm for one sequence, filling `sequence`'s gradients and
  // adding to the parameters'.
  [[nodiscard]] bool backward_sequence(sequence_buffers& sequence) {
    const auto& logit_gradient = sequence.logit_gradient;
    const auto predicting = sequence.predicting();
    const auto predicting_gradient =
        sequence.trunk_gradient[{row_ndx{0}, col_ndx{0}}, predicting.extent()];

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

    return layer_norm_backward(sequence.residual_gradient,
        gradients_.ln_f_weight, gradients_.ln_f_bias, sequence.trunk_gradient,
        sequence.residuals.back(), engine_.ln_f_weight_, gpt2_layer_norm_eps,
        statistics_);
  }

#pragma endregion
#pragma region Data members

  const engine_t& engine_;
  std::vector<sequence_buffers> sequences_;
  parameter_gradients gradients_;
  cuda_buffer<row_statistics<wide_t>> statistics_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda::llm
