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
#include "../cuda_matrix.cuh"
#include "gpt2_engine.cuh"
#include "llm_ops.cuh"

// Training GPT-2 on the device.
//
// `gpt2_trainer<T>` runs the model of a `gpt2_engine<T>` over a batch of token
// sequences and measures how badly it predicts each token from the ones
// before it, as one number, the loss. Every activation on the way is kept in
// its own storage, which is what a backward pass reads.
//
//   gpt2_trainer<float> trainer(engine, 4, 64);
//   float loss{};
//   if (!trainer.forward(loss, ids)) ...
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
// shape. Each `forward` overwrites them.
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

  // What the pass over one sequence leaves behind.
  //
  // For T tokens of width C through L blocks, with V vocabulary entries:
  //
  //   residuals       [L + 1] x [T, C]  the residual stream entering each
  //                                     block, then the one leaving the last
  //   blocks          [L]               each block's `block_buffers`
  //   trunk           [T, C]            the final layer norm's output
  //   logit_gradient  [T - 1, V]        the gradient of the batch's loss with
  //                                     respect to each logit
  //   losses          [T - 1]           each prediction's loss
  //
  // The last token of a sequence predicts nothing, since nothing follows it,
  // so the logits and losses have a row for every token but the last.
  struct sequence_buffers {
    std::vector<cuda_matrix<wide_t>> residuals;
    std::vector<block_buffers> blocks;
    cuda_matrix<element_t> trunk;
    cuda_matrix<wide_t> logit_gradient;
    cuda_buffer<wide_t> losses;

    // Allocate every buffer for `token_count` tokens through the model of
    // `engine`, or throw.
    sequence_buffers(const engine_t& engine, size_t token_count)
        : trunk({.row_count = token_count,
              .col_count = engine.wte_.col_extent()}),
          logit_gradient({.row_count = token_count - 1,
              .col_count = engine.wte_.row_extent()}),
          losses(logit_gradient.row_extent()) {
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
      : engine_{engine} {
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
    const auto predicting = sequence.trunk[{row_ndx{0}, col_ndx{0}},
        {.row_count = device_labels.size()}];
    if (!compute_logits(engine_.blas_, sequence.logit_gradient, predicting,
            engine_.wte_))
      return false;
    return cross_entropy(sequence.logit_gradient, sequence.losses,
        sequence.logit_gradient, device_labels, label_count);
  }

#pragma endregion
#pragma region Data members

  const engine_t& engine_;
  std::vector<sequence_buffers> sequences_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda::llm
