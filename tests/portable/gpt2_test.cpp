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
#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "corvid/llm/gpt2_engine.h"
#include "catch2_main.h"
#include "gpt2_oracle.h"

using namespace corvid;
using namespace corvid::llm;
using namespace corvid::tests::gpt2;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Block matches the oracle", "[Gpt2Test][oracle]") {
  auto oracle = oracle_dumps::load();
  const auto model = gpt2_model::load(std::move(oracle.weights));
  const gpt2_engine engine(model);

  // Every block, fed its own dumped residual. The residual that leaves it is
  // the next block's `ln_1/in` or, for the last block, `ln_f/in`, and the
  // four activations the oracle also dumped are checked on the way. The
  // gate is the attention and MLP paths' 1e-4, except `ln_1/out`, which
  // reads the dumped input directly and so gets the layer norm's 1e-5.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto in = matrix_of(oracle.activations, dump + "/ln_1/in", n_embd);
      const auto exit =
          (n + 1 < n_layer)
              ? std::format("block_{}/ln_1/in", n + 1)
              : std::string{"ln_f/in"};
      const auto expected = matrix_of(oracle.activations, exit, n_embd);
      const auto token_count = in.row_extent();
      REQUIRE(token_count == 14);

      std::vector<float> out_storage(in.size());
      const float_matrix_view out(out_storage, in.extent());
      gpt2_engine::block_activation_buffers owned(token_count, n_embd,
          n_hidden);
      const auto acts = owned.views();
      engine.apply_block(out, in, n, acts);

      check_close(out, expected, 1e-4F, 1e-4F);
      struct dumped {
        const char* name;
        const_float_matrix_view actual;
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
        check_close(actual,
            matrix_of(oracle.activations, dump + "/" + name, n_embd),
            tolerance, tolerance);
      }

      // In place, with `ln_2_in` aliased to the residual too, is the same
      // arithmetic in the same order, so it matches bit for bit.
      std::vector<float> residual_storage(in.as_span().begin(),
          in.as_span().end());
      const float_matrix_view residual(residual_storage, in.extent());
      auto in_place = acts;
      in_place.ln_2_in = residual;
      engine.apply_block(residual, residual, n, in_place);
      CHECK(residual_storage == out_storage);
    }
  }
}

TEST_CASE("Forward pass matches the oracle", "[Gpt2Test][oracle]") {
  auto oracle = oracle_dumps::load();

  // The bisect prompt from its IDs through every block to `ln_f/out`, the
  // last dump before the head. Nothing here is fed a dumped intermediate, so
  // this is the first check that the ops chain, and the gate is the block's.
  const auto ids =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  const auto token_count = ids.size();
  REQUIRE(token_count == 14);
  const auto model = gpt2_model::load(std::move(oracle.weights));
  const gpt2_engine engine(model);
  const auto expected = matrix_of(oracle.activations, "ln_f/out", n_embd);

  std::vector<float> out_storage(token_count * n_embd);
  const float_matrix_view out(out_storage, expected.extent());
  gpt2_engine::block_activation_buffers owned(token_count, n_embd, n_hidden);
  engine.forward(out, ids, owned.views());

  check_close(out, expected, 1e-4F, 1e-4F);
}

TEST_CASE("Model matches the oracle on every prompt", "[Gpt2Test][oracle]") {
  auto oracle = oracle_dumps::load();

  // The whole model, IDs to logits, on each of the manifest's prompts. The
  // logits dump holds all five; only the bisect prompt has activations.
  const auto model = gpt2_model::load(std::move(oracle.weights));
  const gpt2_engine engine(model);
  for (auto n = 0UZ; n < 5; ++n) {
    DYNAMIC_SECTION("prompt_" << n) {
      const auto prefix = std::format("prompt_{}", n);
      const auto ids = ids_of(oracle.logits, prefix + "/input_ids");
      const auto expected =
          matrix_of(oracle.logits, prefix + "/logits", n_vocab);
      const auto token_count = ids.size();
      REQUIRE(expected.row_extent() == token_count);

      std::vector<float> trunk_storage(token_count * n_embd);
      const float_matrix_view trunk(trunk_storage,
          {.row_count = token_count, .col_count = n_embd});
      gpt2_engine::block_activation_buffers owned(token_count, n_embd,
          n_hidden);
      engine.forward(trunk, ids, owned.views());

      std::vector<float> storage(expected.size());
      const float_matrix_view out(storage, expected.extent());
      compute_all_logits(out, trunk, model.wte);

      check_close(out, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Greedy decoding reproduces the manifest", "[Gpt2Test][oracle]") {
  auto oracle = oracle_dumps::load();

  // The manifest records the oracle's greedy continuation of one prompt: the
  // twenty IDs it appended, and their text. Each step runs the whole model
  // over the IDs so far and appends the most likely next token, so one wrong
  // pick would derail every later one.
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

TEST_CASE("Model rejects a file without the weights", "[Gpt2Test]") {
  // A valid file with no tensors: the 8-byte header length, then `{}`.
  constexpr std::array image{std::byte{2}, std::byte{}, std::byte{},
      std::byte{}, std::byte{}, std::byte{}, std::byte{}, std::byte{},
      std::byte{'{'}, std::byte{'}'}};
  CHECK_THROWS_AS(gpt2_model::load(safetensors_file::parse(image)),
      std::runtime_error);
}

// NOLINTEND(readability-function-cognitive-complexity)
