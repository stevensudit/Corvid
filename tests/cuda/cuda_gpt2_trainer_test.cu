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
#include <array>
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

// The same ratio for the gradients with respect to the first block's
// activations, which the walk through all twelve blocks reaches last, set
// above the largest measured over the four sequences, 8.4e-4 (sequence 1's
// `ln_1_out`).
constexpr auto walked_activation_gradient_ratio = 1e-3F;

// The same ratio for the parameter gradients of the blocks and the
// embeddings, set above the largest measured over the twelve blocks' and the
// two tables', 2.3e-3 (block 10's ln_2 weight).
//
// Each is a sum over every row of the batch of an activation gradient that
// already carries the error of the steps behind it, and the sum carries that
// error along even where the sum's own value is small, so the ratio is looser
// than the final layer norm's. The recomputation test below shows the sum
// itself is exact to a float ulp.
constexpr auto block_parameter_gradient_ratio = 3e-3F;

// The ratio for a gradient against the same formula summed in `double` on the
// host, set above the 1.9e-7 measured, which is a float ulp of the result.
constexpr auto recomputation_ratio = 1e-6F;

using trainer_t = gpt2_trainer<float>;

// Check each sequence's gradient, the member `select` names, against the
// oracle's `name`, which has a row per token with the sequences one after
// another.
template<typename Select>
void check_sequence_gradients(const trainer_t& trainer,
    const safetensors_file& grads, std::string_view name, const Select& select,
    float ratio) {
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
        expected[{row_ndx{first_row}, col_ndx{0}}, extent], ratio);
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

// The oracle's gradient batch (four sequences of 64 tokens) run forward
// through a trainer over the oracle's weights.
//
// The members are built in order, each from the ones before it, and the
// oracle's weights move into the model, so `oracle` keeps the dumps the tests
// compare against. The trainer is left after `forward`, for a test to walk
// backward from.
struct oracle_batch {
  oracle_dumps oracle;
  std::vector<token_id> id_storage;
  const gpt2_model<float> model;
  const gpt2_engine<float> engine;
  trainer_t trainer;
  float loss{};

  oracle_batch()
      : oracle(oracle_dumps::load()),
        id_storage(
            id_rows_of(oracle.grads, "input_ids", n_grad_batch, n_grad_seq)),
        model(gpt2_model<float>::load(std::move(oracle.weights))),
        engine(model), trainer(engine, n_grad_batch, n_grad_seq) {
    const matrix_view<token_id> ids(id_storage,
        {.row_count = n_grad_batch, .col_count = n_grad_seq});
    REQUIRE(trainer.forward(loss, ids));
  }
};

} // namespace

#pragma region Tests

TEST_CASE("Device training forward pass matches the oracle's loss",
    "[Gpt2TrainerTest][oracle][cuda]") {
  // The mean loss of the batch's 4 * 63 = 252 predictions is checked against
  // the loss the oracle dumped.
  oracle_batch batch;
  const auto expected_loss = vector_of(batch.oracle.grads, "loss", 1).front();
  CHECK_THAT(batch.loss, WithinRel(expected_loss, loss_tolerance));

  // The gradient of that loss with respect to the logits, the first step
  // backward, against the oracle's. The oracle dumps a row per token, the
  // sequences one after another, and the trainer holds a row per prediction,
  // which is every token of a sequence but its last.
  const auto expected = matrix_of(batch.oracle.grads, "logits", n_vocab);
  REQUIRE(expected.row_extent() == n_grad_batch * n_grad_seq);
  for (const auto [sequence_index, sequence] :
      std::views::enumerate(batch.trainer.sequences()))
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
  oracle_batch batch;
  REQUIRE(batch.trainer.backward());

  // The gradient with respect to the final layer norm's output (the trunk),
  // per sequence, which checks the head's backward step.
  //
  // The layer norm's own step is checked through its parameter gradients
  // below, since the trainer walks on into the blocks, and `residual_gradient`
  // holds the stream at the point it ended.
  check_sequence_gradients(batch.trainer, batch.oracle.grads, "ln_f/out",
      &trainer_t::sequence_buffers::trunk_gradient, activation_gradient_ratio);

  // The gradients with respect to the final layer norm's weight and bias,
  // summed over the batch.
  const auto& gradients = batch.trainer.gradients();
  check_vector_gradient(gradients.ln_f_weight, batch.oracle.grads,
      "transformer.ln_f.weight", n_embd, parameter_gradient_ratio);
  check_vector_gradient(gradients.ln_f_bias, batch.oracle.grads,
      "transformer.ln_f.bias", n_embd, parameter_gradient_ratio);
}

TEST_CASE(
    "Device training backward pass through every block and the embeddings "
    "matches the oracle",
    "[Gpt2TrainerTest][oracle][cuda]") {
  oracle_batch batch;
  REQUIRE(batch.trainer.backward());

  // The gradients with respect to the first block's `ln_2_out`, `ln_1_out`,
  // and entering stream, per sequence. The walk ends there, so each carries
  // the error of every step behind it. The entering stream is the embedding's
  // output, which the oracle dumps twice, as `embed/out` and as
  // `block_0/ln_1/in`.
  const auto& grads = batch.oracle.grads;
  check_sequence_gradients(batch.trainer, grads, "block_0/ln_2/out",
      &trainer_t::sequence_buffers::ln_2_out_gradient,
      walked_activation_gradient_ratio);
  check_sequence_gradients(batch.trainer, grads, "block_0/ln_1/out",
      &trainer_t::sequence_buffers::ln_1_out_gradient,
      walked_activation_gradient_ratio);
  check_sequence_gradients(batch.trainer, grads, "embed/out",
      &trainer_t::sequence_buffers::residual_gradient,
      walked_activation_gradient_ratio);

  // Every block's twelve parameter gradients, summed over the batch.
  const auto& gradients = batch.trainer.gradients();
  for (const auto block_index : iota(n_layer)) {
    INFO("block " << block_index);
    const auto param = std::format("transformer.h.{}.", block_index);
    const auto& block = gradients.blocks[block_index];
    check_vector_gradient(block.ln_1_weight, grads, param + "ln_1.weight",
        n_embd, block_parameter_gradient_ratio);
    check_vector_gradient(block.ln_1_bias, grads, param + "ln_1.bias", n_embd,
        block_parameter_gradient_ratio);
    check_matrix_gradient(block.attn_c_attn_weight, grads,
        param + "attn.c_attn.weight", block_parameter_gradient_ratio);
    check_vector_gradient(block.attn_c_attn_bias, grads,
        param + "attn.c_attn.bias", n_qkv, block_parameter_gradient_ratio);
    check_matrix_gradient(block.attn_c_proj_weight, grads,
        param + "attn.c_proj.weight", block_parameter_gradient_ratio);
    check_vector_gradient(block.attn_c_proj_bias, grads,
        param + "attn.c_proj.bias", n_embd, block_parameter_gradient_ratio);
    check_vector_gradient(block.ln_2_weight, grads, param + "ln_2.weight",
        n_embd, block_parameter_gradient_ratio);
    check_vector_gradient(block.ln_2_bias, grads, param + "ln_2.bias", n_embd,
        block_parameter_gradient_ratio);
    check_matrix_gradient(block.mlp_c_fc_weight, grads,
        param + "mlp.c_fc.weight", block_parameter_gradient_ratio);
    check_vector_gradient(block.mlp_c_fc_bias, grads, param + "mlp.c_fc.bias",
        n_hidden, block_parameter_gradient_ratio);
    check_matrix_gradient(block.mlp_c_proj_weight, grads,
        param + "mlp.c_proj.weight", block_parameter_gradient_ratio);
    check_vector_gradient(block.mlp_c_proj_bias, grads,
        param + "mlp.c_proj.bias", n_embd, block_parameter_gradient_ratio);
  }

  // The embeddings' gradients, summed over the batch. The oracle's `wte` sums
  // the table's two uses, as the embedding lookup and as the output head, as
  // the trainer's does. Only the first 64 rows of `wpe` were used, and the
  // rest are zero in both.
  check_matrix_gradient(gradients.wte, grads, "transformer.wte.weight",
      block_parameter_gradient_ratio);
  check_matrix_gradient(gradients.wpe, grads, "transformer.wpe.weight",
      block_parameter_gradient_ratio);
}

TEST_CASE(
    "Device ln_2 weight gradient of the first block matches a host "
    "recomputation in double",
    "[Gpt2TrainerTest][oracle][cuda]") {
  oracle_batch batch;
  REQUIRE(batch.trainer.backward());

  // The weight gradient is the sum, over every row of the batch, of the
  // gradient with respect to `ln_2_out` times the standardized `ln_2_in`.
  //
  // Recomputed in `double` from the trainer's own retained input and its own
  // output gradient, it checks the kernel apart from the error its inputs
  // carry. Recomputed from the oracle's output gradient instead, it shows how
  // far those inputs account for the gap to the oracle's weight gradient. The
  // first block is the one whose `ln_2_out` gradient the trainer still holds.
  constexpr auto first = 0UZ;
  const auto oracle_out_gradient = matrix_of(batch.oracle.grads,
      std::format("block_{}/ln_2/out", first), n_embd);
  const auto width = static_cast<double>(n_embd);
  const auto eps = static_cast<double>(corvid::llm::gpt2_layer_norm_eps);
  std::vector<double> from_own(n_embd);
  std::vector<double> from_oracle(n_embd);
  for (const auto [sequence_index, sequence] :
      std::views::enumerate(batch.trainer.sequences()))
  {
    const auto in = download(sequence.blocks[first].activations.ln_2_in);
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
  const auto device =
      download(batch.trainer.gradients().blocks[first].ln_2_weight);
  const auto expected = vector_of(batch.oracle.grads,
      std::format("transformer.h.{}.ln_2.weight", first), n_embd);

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

TEST_CASE(
    "Device adapter gradients of the first block match a host recomputation "
    "in double",
    "[Gpt2TrainerTest][oracle][cuda]") {
  // One step moves `b` off zero, so that `a`'s gradient has something to be.
  // Then a fresh forward and backward over the stepped adapters leave block
  // 0's scratches, the ones the walk reaches last, for the recomputation.
  //
  // The gradient with respect to `adapter_hidden` is `scale` times
  // `qkv_gradient x b^T`, `b`'s is `scale` times `adapter_hidden^T x
  // qkv_gradient`, and `a`'s is `ln_1_out^T` times the first, each summed over
  // the batch's rows in `double`.
  oracle_batch batch;
  const matrix_view<token_id> ids(batch.id_storage,
      {.row_count = n_grad_batch, .col_count = n_grad_seq});
  float loss{};
  REQUIRE(batch.trainer.step(loss, ids));
  REQUIRE(batch.trainer.forward(loss, ids));
  REQUIRE(batch.trainer.backward());

  constexpr auto first = 0UZ;
  const auto& adapter = batch.trainer.adapters()[first];
  const auto rank = adapter.a.value.col_extent();
  const auto scale = static_cast<double>(adapter.scale);
  const auto b = download(adapter.b.value);
  std::vector<double> a_gradient(n_embd * rank);
  std::vector<double> b_gradient(rank * n_qkv);
  std::vector<double> hidden_gradient_row(rank);
  for (const auto& sequence : batch.trainer.sequences()) {
    const auto qkv_gradient = download(sequence.qkv_gradient);
    const auto hidden =
        download(sequence.blocks[first].activations.adapter_hidden);
    const auto in = download(sequence.blocks[first].activations.ln_1_out);
    for (const auto row : iota(n_grad_seq)) {
      const std::span<const float> gradient_row(
          qkv_gradient.data() + (row * n_qkv), n_qkv);
      const std::span<const float> hidden_row(hidden.data() + (row * rank),
          rank);
      const std::span<const float> in_row(in.data() + (row * n_embd), n_embd);

      for (const auto [k_signed, hidden_value] :
          std::views::enumerate(hidden_row))
      {
        const auto k = static_cast<size_t>(k_signed);
        const std::span<const float> b_row(b.data() + (k * n_qkv), n_qkv);
        const std::span<double> b_gradient_row(b_gradient.data() + (k * n_qkv),
            n_qkv);

        // The row's gradient with respect to `adapter_hidden[row, k]`, and
        // its share of `b`'s gradient.
        auto total = 0.0;
        for (const auto [gradient, weight] :
            std::views::zip(gradient_row, b_row))
          total += gradient * weight;
        hidden_gradient_row[k] = scale * total;
        for (auto [acc, gradient] :
            std::views::zip(b_gradient_row, gradient_row))
          acc += scale * hidden_value * gradient;

        // And its share of `a`'s.
        for (const auto [c_signed, x] : std::views::enumerate(in_row))
          a_gradient[(static_cast<size_t>(c_signed) * rank) + k] +=
              x * hidden_gradient_row[k];
      }
    }
  }

  const matrix_types::matrix_extent a_extent{.row_count = n_embd,
      .col_count = rank};
  const matrix_types::matrix_extent b_extent{.row_count = rank,
      .col_count = n_qkv};
  const std::vector<float> a_narrowed(a_gradient.begin(), a_gradient.end());
  const std::vector<float> b_narrowed(b_gradient.begin(), b_gradient.end());
  const auto a_device = download(adapter.a.gradient);
  const auto b_device = download(adapter.b.gradient);
  INFO("a");
  check_close_to_scale(float_matrix_view(a_device, a_extent),
      float_matrix_view(a_narrowed, a_extent), recomputation_ratio);
  INFO("b");
  check_close_to_scale(float_matrix_view(b_device, b_extent),
      float_matrix_view(b_narrowed, b_extent), recomputation_ratio);
}

TEST_CASE(
    "Device LoRA training lowers the loss on a text and changes its "
    "continuation",
    "[Gpt2TrainerTest][oracle][cuda]") {
  // A fresh adapter adds nothing, so the first step's loss is the base
  // model's on that batch. A few passes over the text, a batch of 4 x 64
  // tokens per step, should leave each pass's mean loss below the one
  // before, and the adapted model continuing the text's opening more like
  // the text than the base model does.
  auto oracle = oracle_dumps::load();
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);
  const auto ids = encode_text(read_file(fixture_path("park.txt")));
  constexpr auto sequence_count = 4UZ;
  constexpr auto token_count = 64UZ;
  constexpr auto batch_size = sequence_count * token_count;
  constexpr auto batch_count = 8UZ;
  constexpr auto pass_count = 4UZ;
  REQUIRE(ids.size() >= batch_count * batch_size);
  const auto batch_of = [&](size_t batch) {
    return matrix_view<token_id>(
        std::span{ids}.subspan(batch * batch_size, batch_size),
        {.row_count = sequence_count, .col_count = token_count});
  };
  // A learning rate well above the default, since a few dozen steps must show.
  trainer_t trainer(engine, sequence_count, token_count, {},
      {.learning_rate = 1e-3F});

  // How many of the first `continuation_count` tokens the model picks after
  // the text's opening are the text's own.
  constexpr auto prompt_count = 16UZ;
  constexpr auto continuation_count = 16UZ;
  const std::vector<token_id> prompt(ids.begin(), ids.begin() + prompt_count);
  const auto continued =
      std::span{ids}.subspan(prompt_count, continuation_count);
  const auto matches_of = [&](std::span<const token_id> generated) {
    auto matches = 0UZ;
    for (const auto [pick, actual] :
        std::views::zip(generated.subspan(prompt_count), continued))
      matches += (pick == actual);
    return matches;
  };

  // The base model's loss on the first batch, and its continuation.
  float base_loss{};
  REQUIRE(trainer.forward(base_loss, batch_of(0)));
  auto before = prompt;
  REQUIRE(engine.generate(before, continuation_count));
  INFO("before: " << decode_ids(std::span{before}.subspan(prompt_count)));

  std::array<float, pass_count> pass_losses{};
  for (auto [pass, pass_loss] : std::views::enumerate(pass_losses)) {
    for (const auto batch : iota(batch_count)) {
      float loss{};
      REQUIRE(trainer.step(loss, batch_of(batch)));
      if (!pass && !batch) CHECK(loss == base_loss);
      pass_loss += loss;
    }
    pass_loss /= static_cast<float>(batch_count);
    if (pass) CHECK(pass_loss < pass_losses[pass - 1]);
  }
  std::string loss_report;
  for (const auto pass_loss : pass_losses)
    loss_report += std::format("{} ", pass_loss);
  INFO("mean loss by pass: " << loss_report);

  auto after = prompt;
  REQUIRE(engine.generate(after, continuation_count, trainer.adapter_views()));
  INFO("after: " << decode_ids(std::span{after}.subspan(prompt_count)));
  INFO("text: " << decode_ids(continued));
  CHECK(matches_of(after) > matches_of(before));
}

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
