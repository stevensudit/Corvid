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
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "corvid/cuda/llm/gpt2_engine.cuh"
#include "catch2_main.h"
#include "gpt2_oracle.h"

// The whole test sits in a named namespace: a `using namespace corvid;` at
// global scope would make `cuda` (libcu++'s namespace against corvid::cuda)
// and `log` (corvid::infra::log against the C math function) ambiguous in the
// host code nvcc appends after the translation unit, and clang sees the same
// ambiguity wherever the test spells `cuda::`, which is why it is spelled
// `corvid::cuda::` throughout.
namespace corvid_tests {

using namespace corvid;
using namespace corvid::tests::gpt2;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;
using corvid::cuda::cuda_matrix_view;
using corvid::cuda::llm::gpt2_engine;
using corvid::llm::gpt2_model;
using corvid::llm::token_id;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

#pragma region Helpers

// Download `device` into a host vector, in row-major order.
std::vector<float> download(cuda_matrix_view<float> device) {
  std::vector<float> storage(device.row_extent() * device.col_extent());
  REQUIRE(device.store(float_matrix_lens(storage, device.extent())));
  return storage;
}

// Download `device` and check that it is close to `expected`.
void check_device_close(cuda_matrix_view<float> device,
    float_matrix_view expected, float atol, float rtol) {
  const auto storage = download(device);
  check_close(float_matrix_view(storage, device.extent()), expected, atol,
      rtol);
}

#pragma endregion

} // namespace

#pragma region Tests

TEST_CASE("Device block matches the oracle", "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const auto model = gpt2_model::load(std::move(oracle.weights));
  const gpt2_engine engine(model);

  // Every block, fed its own dumped residual, as the CPU test does. The
  // residual that leaves it is checked against the next block's `ln_1/in`
  // or, for the last block, `ln_f/in`, and the five activations the oracle
  // also dumped are checked on the way, at the CPU gate's tolerances.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto in_view =
          matrix_of(oracle.activations, dump + "/ln_1/in", n_embd);
      const auto exit =
          (n + 1 < n_layer)
              ? std::format("block_{}/ln_1/in", n + 1)
              : std::string{"ln_f/in"};
      const auto expected = matrix_of(oracle.activations, exit, n_embd);
      const auto token_count = in_view.row_extent();
      REQUIRE(token_count == 14);

      const cuda_matrix<float> in(in_view);
      cuda_matrix<float> out(in_view.extent());
      gpt2_engine::block_activation_buffers owned(token_count, n_embd,
          n_hidden, n_head);
      const auto acts = owned.lenses();
      REQUIRE(engine.apply_block(out, in, n, acts));

      check_device_close(out, expected, 1e-4F, 1e-4F);
      struct dumped {
        const char* name;
        cuda_matrix_view<float> actual;
        float tolerance;
      };
      for (const auto& [name, actual, tolerance] :
          {dumped{"ln_1/out", acts.ln_1_out, 1e-5F},
              dumped{"attn/out", acts.attn_out, 1e-4F},
              dumped{"ln_2/in", acts.ln_2_in, 1e-4F},
              dumped{"ln_2/out", acts.ln_2_out, 1e-4F},
              dumped{"mlp/out", acts.mlp_out, 1e-4F}})
      {
        INFO(name);
        check_device_close(actual,
            matrix_of(oracle.activations, dump + "/" + name, n_embd),
            tolerance, tolerance);
      }

      // In place, with `ln_2_in` aliased to the residual too, is the same
      // arithmetic in the same order, so it matches bit for bit.
      cuda_matrix<float> residual(in_view);
      auto in_place = acts;
      in_place.ln_2_in = residual;
      REQUIRE(engine.apply_block(residual, residual, n, in_place));
      CHECK(download(residual) == download(out));
    }
  }
}

TEST_CASE("Device forward pass matches the oracle",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // The bisect prompt from its IDs through every block to `ln_f/out`, with
  // the whole model uploaded once, at the block's gate.
  const auto id_storage =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  const auto token_count = id_storage.size();
  REQUIRE(token_count == 14);
  const auto model = gpt2_model::load(std::move(oracle.weights));
  const gpt2_engine engine(model);
  const auto expected = matrix_of(oracle.activations, "ln_f/out", n_embd);

  const cuda_buffer<token_id> ids(id_storage);
  cuda_matrix<float> out(expected.extent());
  gpt2_engine::block_activation_buffers owned(token_count, n_embd, n_hidden,
      n_head);
  REQUIRE(engine.forward(out, ids, owned.lenses()));

  check_device_close(out, expected, 1e-4F, 1e-4F);
}

TEST_CASE("Device model matches the oracle on every prompt",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const cublas_handle blas;

  // The whole model, IDs to logits, on each of the manifest's prompts, with
  // the head a GEMM against the transposed embedding, which the test uploads
  // for itself since the engine scores only the last token.
  const auto model = gpt2_model::load(std::move(oracle.weights));
  const gpt2_engine engine(model);
  const cuda_matrix<float> wte(model.wte);
  for (auto n = 0UZ; n < 5; ++n) {
    DYNAMIC_SECTION("prompt_" << n) {
      const auto prefix = std::format("prompt_{}", n);
      const auto id_storage = ids_of(oracle.logits, prefix + "/input_ids");
      const auto expected =
          matrix_of(oracle.logits, prefix + "/logits", n_vocab);
      const auto token_count = id_storage.size();
      REQUIRE(expected.row_extent() == token_count);

      const cuda_buffer<token_id> ids(id_storage);
      cuda_matrix<float> trunk(
          {.row_count = token_count, .col_count = n_embd});
      gpt2_engine::block_activation_buffers owned(token_count, n_embd,
          n_hidden, n_head);
      REQUIRE(engine.forward(trunk, ids, owned.lenses()));

      cuda_matrix<float> logits(expected.extent());
      REQUIRE(corvid::cuda::llm::compute_logits(blas, logits, trunk, wte));

      check_device_close(logits, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Device greedy decoding reproduces the manifest",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // The CPU test's gate, through the device engine: the twenty IDs the
  // manifest records and their text.
  const auto expected = read_greedy_continuation();
  REQUIRE(expected.ids.size() == 20);

  const auto model = gpt2_model::load(std::move(oracle.weights));
  const gpt2_engine engine(model);
  auto ids = ids_of(oracle.logits,
      std::format("prompt_{}/input_ids", expected.prompt));
  const auto prompt_count = ids.size();
  REQUIRE(engine.generate(ids, expected.ids.size()));
  const auto appended = std::span{ids}.subspan(prompt_count);
  const std::vector<token_id> generated(appended.begin(), appended.end());
  CHECK(generated == expected.ids);

  // And as text, through the tokenizer.
  CHECK(decode_ids(generated) == expected.text);

  // Nothing follows no tokens.
  token_id next{};
  CHECK(!engine.next_token(next, {}));
}

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
