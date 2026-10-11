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
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "corvid/containers/utils/interval.h"

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/llm/gpt2_engine.cuh"
#include "corvid/cuda/llm/gpt2_trainer.cuh"
#include "catch2_main.h"
#include "gpt2_device_oracle.cuh"
#include "gpt2_oracle.h"

// The whole test sits in a named namespace. A `using namespace corvid;` at
// global scope would make `cuda` (libcu++'s namespace against corvid::cuda)
// and `log` (corvid::infra::log against the C math function) ambiguous in the
// host code nvcc appends after the translation unit, and clang sees the same
// ambiguity wherever the test spells `cuda::`, which is why it is spelled
// `corvid::cuda::` throughout.
namespace corvid_tests {

using namespace corvid;
using namespace corvid::tests::gpt2;
using Catch::Matchers::WithinRel;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;
using corvid::cuda::llm::gpt2_engine;
using corvid::cuda::llm::gpt2_trainer;
using corvid::llm::gpt2_model;
using corvid::llm::token_id;
using matrix_types::col_ndx;
using matrix_types::row_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// The relative tolerance of the batch's loss against the oracle's, set above
// the 3.7e-7 measured.
constexpr auto loss_tolerance = 1e-5F;

// The ratio of a gradient's largest error to the oracle's largest value, set
// above the largest measured over the four sequences' logit gradients, 3.0e-5
// against a largest of 1 / 252.
constexpr auto gradient_ratio = 1e-4F;

// The same ratio for the gradients with respect to the activations behind the
// logits, set above the largest measured over the four sequences' trunk and
// residual gradients, 7.5e-5 (the residual stream's).
constexpr auto activation_gradient_ratio = 2e-4F;

// The same ratio for the gradients with respect to the parameters, set above
// the largest measured over the final layer norm's weight and bias and the
// head's share of `wte`, 5.3e-5 (the weight's).
constexpr auto parameter_gradient_ratio = 1e-4F;

// The same ratio for the parameter gradients of a block, set above the
// largest measured over the last block's six, 8.0e-4 (the ln_2 weight's).
//
// Each is a sum over every row of the batch of an activation gradient that
// already carries the error of the steps behind it, and the sum carries that
// error along even where the sum's own value is small, so the ratio is looser
// than the final layer norm's. The recomputation test below shows the sum
// itself is exact to a float ulp.
constexpr auto block_parameter_gradient_ratio = 1e-3F;

// The ratio for a gradient against the same formula summed in `double` on the
// host, set above the 1.9e-7 measured, which is a float ulp of the result.
constexpr auto recomputation_ratio = 1e-6F;

using trainer_t = gpt2_trainer<float>;

// Check each sequence's gradient, the member `select` names, against the
// oracle's `name`, which has a row per token with the sequences one after
// another.
template<typename Select>
void check_sequence_gradients(const trainer_t& trainer,
    const safetensors_file& grads, std::string_view name,
    const Select& select) {
  INFO(name);
  const auto expected = matrix_of(grads, name, n_embd);
  REQUIRE(expected.row_extent() == n_grad_batch * n_grad_seq);
  for (const auto [sequence_index, sequence] :
      std::views::enumerate(trainer.sequences()))
  {
    INFO("sequence " << sequence_index);
    const auto& actual = std::invoke(select, sequence);
    const auto first_row = static_cast<size_t>(sequence_index) * n_grad_seq;
    const auto extent = actual.extent();
    const auto storage = download(actual);
    check_close_to_scale(float_matrix_view(storage, extent),
        expected[{row_ndx{first_row}, col_ndx{0}}, extent],
        activation_gradient_ratio);
  }
}

// Check the parameter gradient `actual`, summed over the batch, against the
// oracle's `name`, a vector of `count` values, as one-row matrices.
void check_vector_gradient(const cuda_buffer<float>& actual,
    const safetensors_file& grads, std::string_view name, size_t count,
    float ratio) {
  INFO(name);
  REQUIRE(actual.size() == count);
  const matrix_types::matrix_extent row_extent{.row_count = 1,
      .col_count = count};
  const auto actual_storage = download(actual);
  const auto expected_storage = vector_of(grads, name, count);
  check_close_to_scale(float_matrix_view(actual_storage, row_extent),
      float_matrix_view(expected_storage, row_extent), ratio);
}

// Check the parameter gradient `actual`, summed over the batch, against the
// oracle's `name`, a matrix of the same extent.
void check_matrix_gradient(const cuda_matrix<float>& actual,
    const safetensors_file& grads, std::string_view name, float ratio) {
  INFO(name);
  const auto expected = matrix_of(grads, name, actual.col_extent());
  REQUIRE(expected.extent() == actual.extent());
  const auto actual_storage = download(actual);
  check_close_to_scale(float_matrix_view(actual_storage, actual.extent()),
      expected, ratio);
}

} // namespace

#pragma region Tests

TEST_CASE("Device training forward pass matches the oracle's loss",
    "[Gpt2TrainerTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // The oracle's gradient batch, four sequences of 64 tokens, goes through the
  // trainer, and the mean loss of its 4 * 63 = 252 predictions is checked
  // against the loss the oracle dumped.
  const auto id_storage =
      id_rows_of(oracle.grads, "input_ids", n_grad_batch, n_grad_seq);
  const matrix_view<token_id> ids(id_storage,
      {.row_count = n_grad_batch, .col_count = n_grad_seq});
  const auto expected_loss = vector_of(oracle.grads, "loss", 1).front();
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);
  gpt2_trainer<float> trainer(engine, n_grad_batch, n_grad_seq);

  float loss{};
  REQUIRE(trainer.forward(loss, ids));
  CHECK_THAT(loss, WithinRel(expected_loss, loss_tolerance));

  // The gradient of that loss with respect to the logits, the first step
  // backward, against the oracle's. The oracle dumps a row per token, the
  // sequences one after another, and the trainer holds a row per prediction,
  // which is every token of a sequence but its last.
  const auto expected = matrix_of(oracle.grads, "logits", n_vocab);
  REQUIRE(expected.row_extent() == n_grad_batch * n_grad_seq);
  for (const auto [sequence_index, sequence] :
      std::views::enumerate(trainer.sequences()))
  {
    INFO("sequence " << sequence_index);
    const auto first_row = static_cast<size_t>(sequence_index) * n_grad_seq;
    const auto extent = sequence.logit_gradient.extent();
    REQUIRE(extent.row_count == n_grad_seq - 1);
    const auto actual = download(sequence.logit_gradient);
    check_close_to_scale(float_matrix_view(actual, extent),
        expected[{row_ndx{first_row}, col_ndx{0}}, extent], gradient_ratio);

    // The oracle runs the last token too and gives it no label, so its row
    // of the oracle's gradient is zero.
    const auto unlabeled = expected[row_ndx{first_row + n_grad_seq - 1}];
    CHECK(std::ranges::all_of(unlabeled, [](float x) { return (x == 0.0F); }));
  }
}

TEST_CASE(
    "Device training backward pass through the head and the final layer "
    "norm matches the oracle",
    "[Gpt2TrainerTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const auto id_storage =
      id_rows_of(oracle.grads, "input_ids", n_grad_batch, n_grad_seq);
  const matrix_view<token_id> ids(id_storage,
      {.row_count = n_grad_batch, .col_count = n_grad_seq});
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);
  gpt2_trainer<float> trainer(engine, n_grad_batch, n_grad_seq);
  float loss{};
  REQUIRE(trainer.forward(loss, ids));
  REQUIRE(trainer.backward());

  // The gradient with respect to the final layer norm's output (the trunk),
  // per sequence, which checks the head's backward step.
  //
  // The layer norm's own step is checked through its parameter gradients
  // below, since the trainer walks on into the last block, and
  // `residual_gradient` holds the stream at that later point.
  check_sequence_gradients(trainer, oracle.grads, "ln_f/out",
      &trainer_t::sequence_buffers::trunk_gradient);

  // The gradients with respect to the final layer norm's weight and bias,
  // summed over the batch.
  const auto& gradients = trainer.gradients();
  check_vector_gradient(gradients.ln_f_weight, oracle.grads,
      "transformer.ln_f.weight", n_embd, parameter_gradient_ratio);
  check_vector_gradient(gradients.ln_f_bias, oracle.grads,
      "transformer.ln_f.bias", n_embd, parameter_gradient_ratio);

  // The gradient with respect to `wte`. The oracle's sums the table's two
  // uses, as the output head and as the embedding lookup, where the trainer's
  // is the head's share alone. The lookup's share is known. Each token's row
  // of the gradient at the embedding output lands in the row of the token's
  // ID, so it is taken back out of the oracle's before the comparison.
  const auto oracle_wte =
      matrix_of(oracle.grads, "transformer.wte.weight", n_embd);
  REQUIRE(oracle_wte.extent() == gradients.wte.extent());
  std::vector<float> expected_wte_storage(oracle_wte.as_span().begin(),
      oracle_wte.as_span().end());
  const float_matrix_lens expected_wte(expected_wte_storage,
      oracle_wte.extent());
  const auto embed_gradient = matrix_of(oracle.grads, "embed/out", n_embd);
  REQUIRE(embed_gradient.row_extent() == id_storage.size());
  for (const auto [position, id] : std::views::enumerate(id_storage)) {
    const auto target = expected_wte[row_ndx{static_cast<size_t>(*id)}];
    const auto share = embed_gradient[row_ndx{static_cast<size_t>(position)}];
    for (auto [acc, delta] : std::views::zip(target, share)) acc -= delta;
  }
  const auto actual_wte = download(gradients.wte);
  check_close_to_scale(float_matrix_view(actual_wte, gradients.wte.extent()),
      expected_wte, parameter_gradient_ratio);
}

TEST_CASE(
    "Device training backward pass through the last block's MLP half "
    "matches the oracle",
    "[Gpt2TrainerTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const auto id_storage =
      id_rows_of(oracle.grads, "input_ids", n_grad_batch, n_grad_seq);
  const matrix_view<token_id> ids(id_storage,
      {.row_count = n_grad_batch, .col_count = n_grad_seq});
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);
  trainer_t trainer(engine, n_grad_batch, n_grad_seq);
  float loss{};
  REQUIRE(trainer.forward(loss, ids));
  REQUIRE(trainer.backward());

  // The gradients with respect to the last block's `ln_2_out` and `ln_2_in`,
  // per sequence. The first checks the two projections and the GELU between
  // them, the second the second layer norm and the residual path's direct
  // share summed back in. The gradient with respect to `mlp_out` is the one
  // leaving the block, which the oracle dumps twice, as `ln_f/in` and as
  // `block_11/mlp/out`.
  constexpr auto last = n_layer - 1;
  const auto dump = std::format("block_{}/", last);
  check_sequence_gradients(trainer, oracle.grads, dump + "ln_2/out",
      &trainer_t::sequence_buffers::ln_2_out_gradient);
  check_sequence_gradients(trainer, oracle.grads, dump + "ln_2/in",
      &trainer_t::sequence_buffers::residual_gradient);

  // The block's six parameter gradients, summed over the batch.
  const auto param = std::format("transformer.h.{}.", last);
  const auto& block = trainer.gradients().blocks[last];
  check_matrix_gradient(block.mlp_c_proj_weight, oracle.grads,
      param + "mlp.c_proj.weight", block_parameter_gradient_ratio);
  check_vector_gradient(block.mlp_c_proj_bias, oracle.grads,
      param + "mlp.c_proj.bias", n_embd, block_parameter_gradient_ratio);
  check_matrix_gradient(block.mlp_c_fc_weight, oracle.grads,
      param + "mlp.c_fc.weight", block_parameter_gradient_ratio);
  check_vector_gradient(block.mlp_c_fc_bias, oracle.grads,
      param + "mlp.c_fc.bias", n_hidden, block_parameter_gradient_ratio);
  check_vector_gradient(block.ln_2_weight, oracle.grads, param + "ln_2.weight",
      n_embd, block_parameter_gradient_ratio);
  check_vector_gradient(block.ln_2_bias, oracle.grads, param + "ln_2.bias",
      n_embd, block_parameter_gradient_ratio);
}

TEST_CASE(
    "Device ln_2 weight gradient of the last block matches a host "
    "recomputation in double",
    "[Gpt2TrainerTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const auto id_storage =
      id_rows_of(oracle.grads, "input_ids", n_grad_batch, n_grad_seq);
  const matrix_view<token_id> ids(id_storage,
      {.row_count = n_grad_batch, .col_count = n_grad_seq});
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);
  trainer_t trainer(engine, n_grad_batch, n_grad_seq);
  float loss{};
  REQUIRE(trainer.forward(loss, ids));
  REQUIRE(trainer.backward());

  // The weight gradient is the sum, over every row of the batch, of the
  // gradient with respect to `ln_2_out` times the standardized `ln_2_in`.
  //
  // Recomputed in `double` from the trainer's own retained input and its own
  // output gradient, it checks the kernel apart from the error its inputs
  // carry. Recomputed from the oracle's output gradient instead, it shows how
  // far those inputs account for the gap to the oracle's weight gradient.
  constexpr auto last = n_layer - 1;
  const auto oracle_out_gradient =
      matrix_of(oracle.grads, std::format("block_{}/ln_2/out", last), n_embd);
  const auto width = static_cast<double>(n_embd);
  const auto eps = static_cast<double>(corvid::llm::gpt2_layer_norm_eps);
  std::vector<double> from_own(n_embd);
  std::vector<double> from_oracle(n_embd);
  for (const auto [sequence_index, sequence] :
      std::views::enumerate(trainer.sequences()))
  {
    const auto in = download(sequence.blocks[last].activations.ln_2_in);
    const auto own_gradient = download(sequence.ln_2_out_gradient);
    const auto first_row = static_cast<size_t>(sequence_index) * n_grad_seq;
    for (const auto row : iota(n_grad_seq)) {
      const std::span<const float> in_row(in.data() + (row * n_embd), n_embd);
      const std::span<const float> own_row(
          own_gradient.data() + (row * n_embd), n_embd);
      const auto oracle_row = oracle_out_gradient[row_ndx{first_row + row}];

      // The row's statistics, as the layer norm has them.
      auto mean = 0.0;
      for (const auto x : in_row) mean += x;
      mean /= width;
      auto variance = 0.0;
      for (const auto x : in_row) {
        const auto deviation = x - mean;
        variance += deviation * deviation;
      }
      variance /= width;
      const auto inv_std = 1.0 / std::sqrt(variance + eps);

      for (auto [own_total, oracle_total, x, own, theirs] :
          std::views::zip(from_own, from_oracle, in_row, own_row, oracle_row))
      {
        const auto standardized = (x - mean) * inv_std;
        own_total += own * standardized;
        oracle_total += theirs * standardized;
      }
    }
  }

  const matrix_types::matrix_extent row_extent{.row_count = 1,
      .col_count = n_embd};
  const std::vector<float> from_own_narrowed(from_own.begin(), from_own.end());
  const std::vector<float> from_oracle_narrowed(from_oracle.begin(),
      from_oracle.end());
  const auto device = download(trainer.gradients().blocks[last].ln_2_weight);
  const auto expected = vector_of(oracle.grads,
      std::format("transformer.h.{}.ln_2.weight", last), n_embd);

  // The kernel against the same inputs summed in double.
  check_close_to_scale(float_matrix_view(device, row_extent),
      float_matrix_view(from_own_narrowed, row_extent), recomputation_ratio);

  // The same formula fed the oracle's output gradient, against the oracle's
  // weight gradient.
  check_close_to_scale(float_matrix_view(from_oracle_narrowed, row_extent),
      float_matrix_view(expected, row_extent), recomputation_ratio);

  // And fed the trainer's, which carries the whole gap to the oracle's.
  check_close_to_scale(float_matrix_view(from_own_narrowed, row_extent),
      float_matrix_view(expected, row_extent), block_parameter_gradient_ratio);
}

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
