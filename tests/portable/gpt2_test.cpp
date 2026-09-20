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

#include "corvid/llm/gpt2.h"
#include "catch2_main.h"
#include "gpt2_oracle.h"

using namespace corvid;
using namespace corvid::llm;
using namespace corvid::tests::gpt2;

using row_ndx = float_matrix_view::row_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

#pragma region Block

// Owned storage for every activation of `block`, all distinct, sized for
// `token_count` tokens of the model's widths.
struct owned_block_activations {
  std::vector<float> ln_1_out;
  std::vector<float> qkv;
  std::vector<float> heads_out;
  std::vector<float> attn_out;
  std::vector<float> ln_2_in;
  std::vector<float> ln_2_out;
  std::vector<float> hidden;
  std::vector<float> mlp_out;
  std::vector<float> scores;
  size_t token_count{};

  explicit owned_block_activations(size_t token_count)
      : ln_1_out(token_count * n_embd), qkv(token_count * n_qkv),
        heads_out(token_count * n_embd), attn_out(token_count * n_embd),
        ln_2_in(token_count * n_embd), ln_2_out(token_count * n_embd),
        hidden(token_count * n_hidden), mlp_out(token_count * n_embd),
        scores(token_count), token_count{token_count} {}

  // The views `block` takes.
  block_activations views() {
    const auto rows = [&](std::vector<float>& storage, size_t cols) {
      return float_matrix_view(storage,
          {.row_count = token_count, .col_count = cols});
    };
    return {
        .ln_1_out = rows(ln_1_out, n_embd),
        .qkv = rows(qkv, n_qkv),
        .heads_out = rows(heads_out, n_embd),
        .attn_out = rows(attn_out, n_embd),
        .ln_2_in = rows(ln_2_in, n_embd),
        .ln_2_out = rows(ln_2_out, n_embd),
        .hidden = rows(hidden, n_hidden),
        .mlp_out = rows(mlp_out, n_embd),
        .scores = scores,
    };
  }
};

#pragma endregion

} // namespace

TEST_CASE("Block matches the oracle", "[Gpt2Test][oracle]") {
  oracle_dumps oracle;
  oracle.load();

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
      const auto params = block_params_of(oracle, n);
      const auto token_count = in.row_extent();
      REQUIRE(token_count == 14);

      std::vector<float> out_storage(in.size());
      const float_matrix_view out(out_storage, in.extent());
      owned_block_activations owned(token_count);
      const auto acts = owned.views();
      block(out, in, params, acts, n_head);

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
      block(residual, residual, params, in_place, n_head);
      CHECK(residual_storage == out_storage);
    }
  }
}

TEST_CASE("Forward pass matches the oracle", "[Gpt2Test][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // The bisect prompt from its IDs through every block to `ln_f/out`, the
  // last dump before the head. Nothing here is fed a dumped intermediate, so
  // this is the first check that the ops chain, and the gate is the block's.
  const auto ids =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  const auto token_count = ids.size();
  REQUIRE(token_count == 14);
  const oracle_params model(oracle);
  const auto expected = matrix_of(oracle.activations, "ln_f/out", n_embd);

  std::vector<float> out_storage(token_count * n_embd);
  const float_matrix_view out(out_storage, expected.extent());
  owned_block_activations owned(token_count);
  forward(out, ids, model.params, owned.views(), n_head);

  check_close(out, expected, 1e-4F, 1e-4F);
}

TEST_CASE("Model matches the oracle on every prompt", "[Gpt2Test][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // The whole model, IDs to logits, on each of the manifest's prompts. The
  // logits dump holds all five; only the bisect prompt has activations.
  const oracle_params model(oracle);
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
      owned_block_activations owned(token_count);
      forward(trunk, ids, model.params, owned.views(), n_head);

      std::vector<float> storage(expected.size());
      const float_matrix_view out(storage, expected.extent());
      compute_all_logits(out, trunk, model.params.wte);

      check_close(out, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Greedy decoding reproduces the manifest", "[Gpt2Test][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // The manifest records the oracle's greedy continuation of one prompt: the
  // twenty IDs it appended, and their text. Each step here runs the whole
  // model over the IDs so far and appends the most likely next token, so
  // one wrong pick would derail every later one.
  const auto expected = read_greedy_continuation();
  REQUIRE(expected.ids.size() == 20);

  const oracle_params model(oracle);
  auto ids = ids_of(oracle.logits,
      std::format("prompt_{}/input_ids", expected.prompt));
  const auto prompt_count = ids.size();
  std::vector<float> logits_storage(n_vocab);
  const float_row_span next_logits(logits_storage);
  for (auto step = 0UZ; step < expected.ids.size(); ++step) {
    const auto token_count = ids.size();
    std::vector<float> trunk_storage(token_count * n_embd);
    const float_matrix_view trunk(trunk_storage,
        {.row_count = token_count, .col_count = n_embd});
    owned_block_activations owned(token_count);
    forward(trunk, ids, model.params, owned.views(), n_head);
    compute_token_logits(next_logits, trunk[row_ndx{token_count - 1}],
        model.params.wte);
    ids.push_back(pick_greedy(next_logits));
  }
  const auto appended = std::span{ids}.subspan(prompt_count);
  const std::vector<token_id> generated(appended.begin(), appended.end());
  CHECK(generated == expected.ids);

  // And as text, through the tokenizer.
  CHECK(decode_ids(generated) == expected.text);
}

// NOLINTEND(readability-function-cognitive-complexity)
