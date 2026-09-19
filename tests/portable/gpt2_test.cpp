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
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "corvid/llm/gpt2.h"
#include "corvid/llm/gpt2_tokenizer.h"
#include "corvid/proto/misc/json_parser.h"
#include "corvid/strings/conversion.h"
#include "catch2_main.h"
#include "gpt2_oracle.h"

using namespace corvid;
using namespace corvid::llm;
using namespace corvid::strings::conversion;
using namespace corvid::tests::gpt2;

using row_ndx = float_matrix_view::row_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

#pragma region Block

// The parameters of block `n`, as views over the oracle's weights.
block_params block_params_of(const oracle_dumps& oracle, size_t n) {
  const auto& w = oracle.weights;
  const auto h = std::format("h.{}", n);
  return {
      .ln_1_weight = vector_of(w, h + ".ln_1.weight", n_embd),
      .ln_1_bias = vector_of(w, h + ".ln_1.bias", n_embd),
      .attn_c_attn_weight = matrix_of(w, h + ".attn.c_attn.weight", n_qkv),
      .attn_c_attn_bias = vector_of(w, h + ".attn.c_attn.bias", n_qkv),
      .attn_c_proj_weight = matrix_of(w, h + ".attn.c_proj.weight", n_embd),
      .attn_c_proj_bias = vector_of(w, h + ".attn.c_proj.bias", n_embd),
      .ln_2_weight = vector_of(w, h + ".ln_2.weight", n_embd),
      .ln_2_bias = vector_of(w, h + ".ln_2.bias", n_embd),
      .mlp_c_fc_weight = matrix_of(w, h + ".mlp.c_fc.weight", n_hidden),
      .mlp_c_fc_bias = vector_of(w, h + ".mlp.c_fc.bias", n_hidden),
      .mlp_c_proj_weight = matrix_of(w, h + ".mlp.c_proj.weight", n_embd),
      .mlp_c_proj_bias = vector_of(w, h + ".mlp.c_proj.bias", n_embd),
  };
}

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
#pragma region Model

// The whole model's parameters, as views over the oracle's weights, with the
// per-block views owned here since `gpt2_params` only spans them.
struct oracle_params {
  std::vector<block_params> blocks;
  gpt2_params params;

  explicit oracle_params(const oracle_dumps& oracle) {
    for (auto n = 0UZ; n < n_layer; ++n)
      blocks.push_back(block_params_of(oracle, n));
    const auto& w = oracle.weights;
    params = {
        .wte = matrix_of(w, "wte.weight", n_embd),
        .wpe = matrix_of(w, "wpe.weight", n_embd),
        .blocks = blocks,
        .ln_f_weight = vector_of(w, "ln_f.weight", n_embd),
        .ln_f_bias = vector_of(w, "ln_f.bias", n_embd),
    };
    REQUIRE(params.wte.row_extent() == n_vocab);
    REQUIRE(params.wpe.row_extent() == n_ctx);
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
      logits(out, trunk, model.params.wte);

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
  const auto manifest_text = read_file(fixture_path("manifest.json"));
  json_value_view root;
  REQUIRE(parse_json(manifest_text, root));
  const auto spec = root.as_object().get_object("greedy");
  const auto prompt = spec.get_number<size_t>("prompt");
  REQUIRE(prompt);
  std::vector<token_id> expected_ids;
  for (const auto item : spec.get_array("tokens")) {
    const auto id = item.as_number<uint32_t>();
    REQUIRE(id);
    expected_ids.push_back(token_id{*id});
  }
  REQUIRE(expected_ids.size() == 20);
  std::string expected_text;
  REQUIRE(spec.get_string("text", expected_text));

  const oracle_params model(oracle);
  auto ids =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", *prompt));
  const auto prompt_count = ids.size();
  std::vector<float> logits_storage(n_vocab);
  const float_row_span next_logits(logits_storage);
  for (auto step = 0UZ; step < expected_ids.size(); ++step) {
    const auto token_count = ids.size();
    std::vector<float> trunk_storage(token_count * n_embd);
    const float_matrix_view trunk(trunk_storage,
        {.row_count = token_count, .col_count = n_embd});
    owned_block_activations owned(token_count);
    forward(trunk, ids, model.params, owned.views(), n_head);
    token_logits(next_logits, trunk[row_ndx{token_count - 1}],
        model.params.wte);
    ids.push_back(greedy(next_logits));
  }
  const auto appended = std::span{ids}.subspan(prompt_count);
  const std::vector<token_id> generated(appended.begin(), appended.end());
  CHECK(generated == expected_ids);

  // And as text, through the tokenizer.
  gpt2_tokenizer tok;
  const auto merges = read_file(fixture_path("merges.txt"));
  const auto merges_span = as_byte_span<char8_t>(merges);
  REQUIRE(tok.load({merges_span.data(), merges_span.size()}));
  std::u8string bytes;
  REQUIRE(tok.decode(bytes, generated));
  CHECK(std::string(bytes.begin(), bytes.end()) == expected_text);
}

// NOLINTEND(readability-function-cognitive-complexity)
