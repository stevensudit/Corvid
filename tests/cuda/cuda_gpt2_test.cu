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
#include <vector>

#include "corvid/cuda/llm/gpt2.cuh"
#include "catch2_main.h"
#include "gpt2_oracle.h"

using namespace corvid;
using namespace corvid::tests::gpt2;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;
using corvid::cuda::cuda_matrix_view;
using corvid::llm::pick_greedy;
using corvid::llm::token_id;

using row_ndx = cuda_matrix<float>::view_t::row_ndx;
using col_ndx = cuda_matrix<float>::view_t::col_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

#pragma region Helpers

// Owned device storage for every activation of `block`, all distinct, sized
// for `token_count` tokens of the model's widths.
struct owned_block_activations {
  cuda_matrix<float> ln_1_out;
  cuda_matrix<float> qkv;
  cuda_matrix<float> heads_out;
  cuda_matrix<float> attn_out;
  cuda_matrix<float> ln_2_in;
  cuda_matrix<float> ln_2_out;
  cuda_matrix<float> hidden;
  cuda_matrix<float> mlp_out;
  cuda_matrix<float> scores;

  explicit owned_block_activations(size_t token_count)
      : ln_1_out({.row_count = token_count, .col_count = n_embd}),
        qkv({.row_count = token_count, .col_count = n_qkv}),
        heads_out({.row_count = token_count, .col_count = n_embd}),
        attn_out({.row_count = token_count, .col_count = n_embd}),
        ln_2_in({.row_count = token_count, .col_count = n_embd}),
        ln_2_out({.row_count = token_count, .col_count = n_embd}),
        hidden({.row_count = token_count, .col_count = n_hidden}),
        mlp_out({.row_count = token_count, .col_count = n_embd}),
        scores({.row_count = token_count, .col_count = token_count}) {}

  // The views `block` takes.
  cuda::llm::block_activations views() {
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

// Download `device` into a host vector, in row-major order.
std::vector<float> download(cuda_matrix_view<const float> device) {
  std::vector<float> storage(device.row_extent() * device.col_extent());
  REQUIRE(device.store(float_matrix_view(storage, device.extent())));
  return storage;
}

// Download `device` and check that it is close to `expected`.
void check_device_close(cuda_matrix_view<const float> device,
    const_float_matrix_view expected, float atol, float rtol) {
  const auto storage = download(device);
  check_close(const_float_matrix_view(storage, device.extent()), expected,
      atol, rtol);
}

#pragma endregion

} // namespace

#pragma region Tests

TEST_CASE("Device block matches the oracle", "[Gpt2Test][oracle][cuda]") {
  oracle_dumps oracle;
  oracle.load();
  const cublas_handle blas;

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
      const cuda::llm::block_params params(block_params_of(oracle, n));
      const auto token_count = in_view.row_extent();
      REQUIRE(token_count == 14);

      const cuda_matrix<float> in(in_view);
      cuda_matrix<float> out(in_view.extent());
      owned_block_activations owned(token_count);
      const auto acts = owned.views();
      REQUIRE(cuda::llm::block(blas, out, in, params, acts, n_head));

      check_device_close(out, expected, 1e-4F, 1e-4F);
      struct dumped {
        const char* name;
        cuda_matrix_view<const float> actual;
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
      REQUIRE(cuda::llm::block(blas, residual, residual, params, in_place,
          n_head));
      CHECK(download(residual) == download(out));
    }
  }
}

TEST_CASE("Device forward pass matches the oracle",
    "[Gpt2Test][oracle][cuda]") {
  oracle_dumps oracle;
  oracle.load();
  const cublas_handle blas;

  // The bisect prompt from its IDs through every block to `ln_f/out`, with
  // the whole model uploaded once, at the block's gate.
  const auto id_storage =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  const auto token_count = id_storage.size();
  REQUIRE(token_count == 14);
  const oracle_params model(oracle);
  const cuda::llm::gpt2_params params(model.params);
  const auto expected = matrix_of(oracle.activations, "ln_f/out", n_embd);

  const cuda_buffer<token_id> ids(id_storage);
  cuda_matrix<float> out(expected.extent());
  owned_block_activations owned(token_count);
  REQUIRE(cuda::llm::forward(blas, out, ids, params, owned.views(), n_head));

  check_device_close(out, expected, 1e-4F, 1e-4F);
}

TEST_CASE("Device model matches the oracle on every prompt",
    "[Gpt2Test][oracle][cuda]") {
  oracle_dumps oracle;
  oracle.load();
  const cublas_handle blas;

  // The whole model, IDs to logits, on each of the manifest's prompts, with
  // the head a GEMM against the transposed embedding.
  const oracle_params model(oracle);
  const cuda::llm::gpt2_params params(model.params);
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
      owned_block_activations owned(token_count);
      REQUIRE(
          cuda::llm::forward(blas, trunk, ids, params, owned.views(), n_head));

      cuda_matrix<float> logits(expected.extent());
      REQUIRE(cuda::llm::compute_logits(blas, logits, trunk, params.wte));

      check_device_close(logits, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Device greedy decoding reproduces the manifest",
    "[Gpt2Test][oracle][cuda]") {
  oracle_dumps oracle;
  oracle.load();
  const cublas_handle blas;

  // The CPU test's loop, on the device. Each step runs the whole model over
  // the IDs so far, scores the last token alone, and downloads that one row
  // of logits for the pick.
  const auto expected = read_greedy_continuation();
  REQUIRE(expected.ids.size() == 20);

  const oracle_params model(oracle);
  const cuda::llm::gpt2_params params(model.params);
  auto ids = ids_of(oracle.logits,
      std::format("prompt_{}/input_ids", expected.prompt));
  const auto prompt_count = ids.size();
  cuda_matrix<float> next_logits({.row_count = 1, .col_count = n_vocab});
  std::vector<float> logits_storage(n_vocab);
  const float_matrix_view logits_view(logits_storage, next_logits.extent());
  for (auto step = 0UZ; step < expected.ids.size(); ++step) {
    const auto token_count = ids.size();
    const cuda_buffer<token_id> device_ids(ids);
    cuda_matrix<float> trunk({.row_count = token_count, .col_count = n_embd});
    owned_block_activations owned(token_count);
    REQUIRE(cuda::llm::forward(blas, trunk, device_ids, params, owned.views(),
        n_head));
    const auto last_row = trunk.subview({row_ndx{token_count - 1}, col_ndx{0}},
        {.row_count = 1, .col_count = n_embd});
    REQUIRE(
        cuda::llm::compute_logits(blas, next_logits, last_row, params.wte));
    REQUIRE(next_logits.as_view().store(logits_view));
    ids.push_back(pick_greedy(logits_view[row_ndx{0}]));
  }
  const auto appended = std::span{ids}.subspan(prompt_count);
  const std::vector<token_id> generated(appended.begin(), appended.end());
  CHECK(generated == expected.ids);

  // And as text, through the tokenizer.
  CHECK(decode_ids(generated) == expected.text);
}

// NOLINTEND(readability-function-cognitive-complexity)
#pragma endregion
