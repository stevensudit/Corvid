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
#include <cstddef>
#include <ranges>
#include <utility>
#include <vector>

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

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
