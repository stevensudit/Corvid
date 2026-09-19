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
#include <cmath>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "corvid/llm/llm_ops.h"
#include "catch2_main.h"
#include "catch2/catch_template_test_macros.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include "gpt2_oracle.h"

using namespace corvid;
using namespace corvid::llm;
using namespace corvid::tests::gpt2;
using Catch::Matchers::WithinAbs;

using row_ndx = float_matrix_view::row_ndx;
using col_ndx = float_matrix_view::col_ndx;

// The epsilon every layer norm test passes, GPT-2's own so the oracle case
// matches.
constexpr auto eps = 1e-5F;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Layer norm on hand-computed rows", "[LlmOpsTest]") {
  // Row 0 has mean 2.5 and biased variance 1.25. Row 1 is constant, so it
  // normalizes to zero and the output is the bias alone.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 5.0F, 5.0F,
      5.0F};
  std::vector<float> out_storage(in_storage.size());
  const const_float_matrix_view in(in_storage,
      {.row_count = 2, .col_count = 4});
  const float_matrix_view out(out_storage, {.row_count = 2, .col_count = 4});
  constexpr std::array weight{1.0F, 2.0F, 1.0F, 2.0F};
  constexpr std::array bias{0.0F, 0.0F, 0.5F, 0.5F};

  layer_norm(out, in, weight, bias, eps);

  constexpr auto tolerance = 1e-5;
  const auto inv_std = 1.0F / std::sqrt(1.25F + eps);
  const auto first = out[row_ndx{0}];
  CHECK_THAT(first[col_ndx{0}], WithinAbs(-1.5F * inv_std, tolerance));
  CHECK_THAT(first[col_ndx{1}], WithinAbs(-0.5F * inv_std * 2.0F, tolerance));
  CHECK_THAT(first[col_ndx{2}], WithinAbs((0.5F * inv_std) + 0.5F, tolerance));
  CHECK_THAT(first[col_ndx{3}],
      WithinAbs((1.5F * inv_std * 2.0F) + 0.5F, tolerance));
  const auto second = out[row_ndx{1}];
  CHECK_THAT(second[col_ndx{0}], WithinAbs(0.0, tolerance));
  CHECK_THAT(second[col_ndx{1}], WithinAbs(0.0, tolerance));
  CHECK_THAT(second[col_ndx{2}], WithinAbs(0.5, tolerance));
  CHECK_THAT(second[col_ndx{3}], WithinAbs(0.5, tolerance));
}

TEST_CASE("Row ops match layer norm step by step", "[LlmOpsTest]") {
  // Standardize into a fresh row, then scale and shift it in place, and
  // expect the same output as the fused op.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F};
  std::vector<float> out_storage(in_storage.size());
  std::vector<float> expected(in_storage.size());
  const const_float_matrix_view in(in_storage,
      {.row_count = 1, .col_count = 4});
  const float_matrix_view out(out_storage, {.row_count = 1, .col_count = 4});
  const float_matrix_view fused(expected, {.row_count = 1, .col_count = 4});
  constexpr std::array weight{1.0F, 2.0F, 1.0F, 2.0F};
  constexpr std::array bias{0.0F, 0.0F, 0.5F, 0.5F};

  const auto out_row = out[row_ndx{0}];
  standardize_row(out_row, in[row_ndx{0}], eps);

  constexpr auto tolerance = 1e-5;
  const auto inv_std = 1.0F / std::sqrt(1.25F + eps);
  CHECK_THAT(out_row[col_ndx{0}], WithinAbs(-1.5F * inv_std, tolerance));
  CHECK_THAT(out_row[col_ndx{3}], WithinAbs(1.5F * inv_std, tolerance));

  scale_shift_row(out_row, out_row, weight, bias);
  layer_norm(fused, in, weight, bias, eps);

  CHECK(out_storage == expected);
}

TEST_CASE("Layer norm in place", "[LlmOpsTest]") {
  std::vector<float> storage{1.0F, 2.0F, 3.0F, 4.0F};
  std::vector<float> expected(storage.size());
  const float_matrix_view m(storage, {.row_count = 1, .col_count = 4});
  const float_matrix_view out(expected, {.row_count = 1, .col_count = 4});
  constexpr std::array weight{1.0F, 2.0F, 1.0F, 2.0F};
  constexpr std::array bias{0.0F, 0.0F, 0.5F, 0.5F};

  layer_norm(out, m, weight, bias, eps);
  layer_norm(m, m, weight, bias, eps);

  CHECK(storage == expected);
}

TEST_CASE("Layer norm honors the stride of both views", "[LlmOpsTest]") {
  // The row lives in the first two columns of a three-column buffer, and
  // writes land in the last two columns of another, leaving the rest alone.
  const std::vector<float> in_storage{2.0F, 4.0F, -1.0F};
  std::vector<float> out_storage{-1.0F, -1.0F, -1.0F};
  const auto in =
      const_float_matrix_view(in_storage, {.row_count = 1, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{0}}, {.row_count = 1, .col_count = 2});
  const auto out =
      float_matrix_view(out_storage, {.row_count = 1, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{1}});
  constexpr std::array weight{1.0F, 1.0F};
  constexpr std::array bias{0.0F, 0.0F};

  layer_norm(out, in, weight, bias, eps);

  constexpr auto tolerance = 1e-5;
  CHECK(out_storage[0] == -1.0F);
  CHECK_THAT(out_storage[1], WithinAbs(-1.0, tolerance));
  CHECK_THAT(out_storage[2], WithinAbs(1.0, tolerance));
}

TEMPLATE_TEST_CASE("GELU on hand-computed values", "[LlmOpsTest]", float,
    double) {
  using T = TestType;
  // Reference values from torch's gelu with approximate="tanh". Zero maps to
  // zero, the far tails pass through or vanish, and the negative side dips
  // below zero before it does.
  constexpr auto tolerance = 1e-6;
  CHECK(gelu_new(T{0}) == T{0});
  CHECK_THAT(gelu_new(T{1}), WithinAbs(0.841192, tolerance));
  CHECK_THAT(gelu_new(T{-1}), WithinAbs(-0.158808, tolerance));
  CHECK_THAT(gelu_new(T{2}), WithinAbs(1.954598, tolerance));
  CHECK_THAT(gelu_new(T{-2}), WithinAbs(-0.045402, tolerance));
  CHECK_THAT(gelu_new(T{10}), WithinAbs(10.0, tolerance));
  CHECK_THAT(gelu_new(T{-10}), WithinAbs(0.0, tolerance));
}

TEST_CASE("GELU honors the stride of both views", "[LlmOpsTest]") {
  // Two rows of two features inside three-column buffers: the input's rows
  // occupy the first two columns and the output's the last two, so the
  // untouched column of each output row proves the stride is honored.
  const std::vector<float> in_storage{1.0F, -1.0F, 99.0F, 2.0F, 0.0F, 99.0F};
  std::vector<float> out_storage(in_storage.size(), -1.0F);
  const auto in =
      const_float_matrix_view(in_storage, {.row_count = 2, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{0}}, {.row_count = 2, .col_count = 2});
  const auto out =
      float_matrix_view(out_storage, {.row_count = 2, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{1}});

  gelu_new(out, in);

  constexpr auto tolerance = 1e-6;
  CHECK(out_storage[0] == -1.0F);
  CHECK_THAT(out_storage[1], WithinAbs(0.841192, tolerance));
  CHECK_THAT(out_storage[2], WithinAbs(-0.158808, tolerance));
  CHECK(out_storage[3] == -1.0F);
  CHECK_THAT(out_storage[4], WithinAbs(1.954598, tolerance));
  CHECK(out_storage[5] == 0.0F);
}

TEST_CASE("GELU in place", "[LlmOpsTest]") {
  std::vector<float> storage{1.0F, -1.0F, 2.0F, 0.5F};
  std::vector<float> expected(storage.size());
  const float_matrix_view m(storage, {.row_count = 2, .col_count = 2});
  const float_matrix_view out(expected, {.row_count = 2, .col_count = 2});

  gelu_new(out, m);
  gelu_new(m, m);

  CHECK(storage == expected);
}

TEST_CASE("Attention on three tokens of width two", "[LlmOpsTest]") {
  // The napkin example: three tokens with features [1, 0], [0, 1], [1, 1].
  // The `c_attn` weight copies the features into the query and key columns
  // and swaps them into the value columns, with no bias, so q and k equal
  // the input and v is the input with its columns swapped.
  const std::vector<float> in_storage{1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
  const std::vector<float> c_attn_storage{1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F,
      0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F};
  constexpr std::array no_bias{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
  const const_float_matrix_view in(in_storage,
      {.row_count = 3, .col_count = 2});
  const const_float_matrix_view c_attn(c_attn_storage,
      {.row_count = 2, .col_count = 6});

  std::vector<float> qkv_storage(3UZ * 6);
  const float_matrix_view qkv(qkv_storage, {.row_count = 3, .col_count = 6});
  linear_projection(qkv, in, c_attn, no_bias);
  CHECK(qkv_storage ==
        std::vector<float>{1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F,
            0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F});

  std::vector<float> out_storage(3UZ * 2);
  const float_matrix_view out(out_storage, {.row_count = 3, .col_count = 2});
  std::array<float, 3> scores{};
  constexpr auto tolerance = 1e-5;

  SECTION("one head of width two") {
    // Token 0 sees only itself, so its output is its own value, [0, 1].
    // Token 1 scores itself 0.707 and token 0 zero, for weights 0.670 and
    // 0.330. Token 2 scores itself 1.414 and the others 0.707, for weights
    // 0.503, 0.248, and 0.248.
    attend(out, qkv, 1, scores);
    CHECK_THAT(out_storage[0], WithinAbs(0.0, tolerance));
    CHECK_THAT(out_storage[1], WithinAbs(1.0, tolerance));
    CHECK_THAT(out_storage[2], WithinAbs(0.669762, tolerance));
    CHECK_THAT(out_storage[3], WithinAbs(0.330238, tolerance));
    CHECK_THAT(out_storage[4], WithinAbs(0.751745, tolerance));
    CHECK_THAT(out_storage[5], WithinAbs(0.751745, tolerance));
  }

  SECTION("two heads of width one") {
    // Each head sees one column of q, k, and v. For token 1, head 0 has a
    // zero query, so both scores are zero and it takes half of each value:
    // (0 + 1) / 2. Head 1 scores itself 1 and token 0 zero, for weights
    // 0.731 and 0.269 on values 0 and 1.
    attend(out, qkv, 2, scores);
    CHECK_THAT(out_storage[0], WithinAbs(0.0, tolerance));
    CHECK_THAT(out_storage[1], WithinAbs(1.0, tolerance));
    CHECK_THAT(out_storage[2], WithinAbs(0.5, tolerance));
    CHECK_THAT(out_storage[3], WithinAbs(0.268941, tolerance));
    CHECK_THAT(out_storage[4], WithinAbs(0.577681, tolerance));
    CHECK_THAT(out_storage[5], WithinAbs(0.577681, tolerance));
  }
}

TEST_CASE("Embed tokens on hand-computed rows", "[LlmOpsTest]") {
  // Three tokens in the vocabulary, width two.
  const std::vector<float> table_storage{1.0F, 2.0F, 10.0F, 20.0F, 100.0F,
      200.0F};
  const const_float_matrix_view table(table_storage,
      {.row_count = 3, .col_count = 2});
  const std::vector<token_id> ids{token_id{2}, token_id{0}};

  std::vector<float> storage(ids.size() * 2);
  const float_matrix_view out(storage, {.row_count = 2, .col_count = 2});
  embed_tokens(out, ids, table);

  CHECK(storage == std::vector<float>{100.0F, 200.0F, 1.0F, 2.0F});
}

TEST_CASE("Logits on hand-computed rows", "[LlmOpsTest]") {
  // Two tokens of width two against a three-entry vocabulary.
  const std::vector<float> wte_storage{1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
  const const_float_matrix_view wte(wte_storage,
      {.row_count = 3, .col_count = 2});
  const std::vector<float> in_storage{2.0F, 3.0F, -1.0F, 0.5F};
  const const_float_matrix_view in(in_storage,
      {.row_count = 2, .col_count = 2});

  std::vector<float> storage(2UZ * 3);
  const float_matrix_view out(storage, {.row_count = 2, .col_count = 3});
  compute_all_logits(out, in, wte);

  CHECK(storage == std::vector<float>{2.0F, 3.0F, 5.0F, -1.0F, 0.5F, -0.5F});
}

TEST_CASE("Greedy picks the largest logit", "[LlmOpsTest]") {
  const std::vector<float> rising{-1.0F, 3.0F, 2.0F};
  CHECK(pick_greedy(rising) == token_id{1});
  const std::vector<float> tied{2.0F, 2.0F, 1.0F};
  CHECK(pick_greedy(tied) == token_id{0});
}

TEST_CASE("Layer norm matches the oracle", "[LlmOpsTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Every layer norm in the model, fed its own dumped input, so no error in
  // an earlier op can reach it.
  struct site {
    std::string dump;
    std::string param;
  };
  std::vector<site> sites;
  for (auto n = 0UZ; n < n_layer; ++n)
    for (const auto* ln : {"ln_1", "ln_2"})
      sites.push_back(
          {std::format("block_{}/{}", n, ln), std::format("h.{}.{}", n, ln)});
  sites.push_back({"ln_f", "ln_f"});

  for (const auto& [dump, param] : sites) {
    DYNAMIC_SECTION(dump) {
      const auto in = matrix_of(oracle.activations, dump + "/in", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/out", n_embd);
      const auto weight = vector_of(oracle.weights, param + ".weight", n_embd);
      const auto bias = vector_of(oracle.weights, param + ".bias", n_embd);
      REQUIRE(in.row_extent() == 14);

      std::vector<float> storage(in.size());
      const float_matrix_view out(storage, in.extent());
      layer_norm(out, in, weight, bias, eps);

      check_close(out, expected, 1e-5F, 1e-5F);
    }
  }
}

TEST_CASE("MLP path matches the oracle", "[LlmOpsTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Every block's MLP, fed its own dumped `ln_2/out` and compared against its
  // dumped `mlp/out`, which is the first point after `c_fc`, `gelu_new`, and
  // `c_proj` that the oracle captures. This gates `linear` as well, since no
  // dump sits between the two projections.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto param = std::format("h.{}.mlp", n);
      const auto in =
          matrix_of(oracle.activations, dump + "/ln_2/out", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/mlp/out", n_embd);
      const auto fc_weight =
          matrix_of(oracle.weights, param + ".c_fc.weight", n_hidden);
      const auto fc_bias =
          vector_of(oracle.weights, param + ".c_fc.bias", n_hidden);
      const auto proj_weight =
          matrix_of(oracle.weights, param + ".c_proj.weight", n_embd);
      const auto proj_bias =
          vector_of(oracle.weights, param + ".c_proj.bias", n_embd);
      REQUIRE(in.row_extent() == 14);

      std::vector<float> hidden_storage(in.row_extent() * n_hidden);
      const float_matrix_view hidden(hidden_storage,
          {.row_count = in.row_extent(), .col_count = n_hidden});
      std::vector<float> out_storage(in.size());
      const float_matrix_view out(out_storage, in.extent());

      linear_projection(hidden, in, fc_weight, fc_bias);
      gelu_new(hidden, hidden);
      linear_projection(out, hidden, proj_weight, proj_bias);

      check_close(out, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Attention path matches the oracle", "[LlmOpsTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Every block's attention, fed its own dumped `ln_1/out` and compared
  // against its dumped `attn/out`, which is the first point after `c_attn`,
  // the heads, and `c_proj` that the oracle captures.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto param = std::format("h.{}.attn", n);
      const auto in =
          matrix_of(oracle.activations, dump + "/ln_1/out", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/attn/out", n_embd);
      const auto attn_weight =
          matrix_of(oracle.weights, param + ".c_attn.weight", n_qkv);
      const auto attn_bias =
          vector_of(oracle.weights, param + ".c_attn.bias", n_qkv);
      const auto proj_weight =
          matrix_of(oracle.weights, param + ".c_proj.weight", n_embd);
      const auto proj_bias =
          vector_of(oracle.weights, param + ".c_proj.bias", n_embd);
      const auto token_count = in.row_extent();
      REQUIRE(token_count == 14);

      std::vector<float> qkv_storage(token_count * n_qkv);
      const float_matrix_view qkv(qkv_storage,
          {.row_count = token_count, .col_count = n_qkv});
      std::vector<float> heads_storage(in.size());
      const float_matrix_view heads_out(heads_storage, in.extent());
      std::vector<float> scores(token_count);
      std::vector<float> out_storage(in.size());
      const float_matrix_view out(out_storage, in.extent());

      linear_projection(qkv, in, attn_weight, attn_bias);
      attend(heads_out, qkv, n_head, scores);
      linear_projection(out, heads_out, proj_weight, proj_bias);

      check_close(out, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Residual adds match the oracle", "[LlmOpsTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Both adds of every block, each fed its own dumped operands. Adding two
  // fp32 values is the same IEEE operation here and in the oracle, so the
  // match is exact, with no tolerance at all.
  struct site {
    std::string residual;
    std::string correction;
    std::string sum;
  };
  std::vector<site> sites;
  for (auto n = 0UZ; n < n_layer; ++n) {
    const auto block = std::format("block_{}", n);
    const auto after_mlp =
        (n + 1 < n_layer)
            ? std::format("block_{}/ln_1/in", n + 1)
            : std::string{"ln_f/in"};
    sites.push_back(
        {block + "/ln_1/in", block + "/attn/out", block + "/ln_2/in"});
    sites.push_back({block + "/ln_2/in", block + "/mlp/out", after_mlp});
  }

  for (const auto& [residual, correction, sum] : sites) {
    DYNAMIC_SECTION(sum) {
      const auto a = matrix_of(oracle.activations, residual, n_embd);
      const auto b = matrix_of(oracle.activations, correction, n_embd);
      const auto expected = matrix_of(oracle.activations, sum, n_embd);
      REQUIRE(a.row_extent() == 14);

      std::vector<float> storage(a.size());
      const float_matrix_view out(storage, a.extent());
      add(out, a, b);

      check_close(out, expected, 0.0F, 0.0F);
    }
  }
}

TEST_CASE("Embed matches the oracle", "[LlmOpsTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // The bisect prompt's IDs come from the logits dump, the only place the
  // oracle wrote them. As with the residual add, the sum of two fp32 rows is
  // exact.
  const auto ids =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  REQUIRE(ids.size() == 14);
  const auto wte = matrix_of(oracle.weights, "wte.weight", n_embd);
  const auto wpe = matrix_of(oracle.weights, "wpe.weight", n_embd);
  REQUIRE(wte.row_extent() == n_vocab);
  REQUIRE(wpe.row_extent() == n_ctx);
  const auto expected = matrix_of(oracle.activations, "embed/out", n_embd);

  std::vector<float> storage(ids.size() * n_embd);
  const float_matrix_view out(storage, expected.extent());
  embed_tokens(out, ids, wte);
  out += wpe.subview({row_ndx{0}, col_ndx{0}},
      {.row_count = ids.size(), .col_count = n_embd});

  check_close(out, expected, 0.0F, 0.0F);
}

TEST_CASE("Logits match the oracle", "[LlmOpsTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // The head alone, fed the dumped `ln_f/out`, against the dumped logits of
  // the bisect prompt.
  const auto in = matrix_of(oracle.activations, "ln_f/out", n_embd);
  const auto expected = matrix_of(oracle.activations, "logits", n_vocab);
  const auto wte = matrix_of(oracle.weights, "wte.weight", n_embd);
  REQUIRE(in.row_extent() == 14);

  std::vector<float> storage(expected.size());
  const float_matrix_view out(storage, expected.extent());
  compute_all_logits(out, in, wte);

  check_close(out, expected, 1e-4F, 1e-4F);
}

// NOLINTEND(readability-function-cognitive-complexity)
