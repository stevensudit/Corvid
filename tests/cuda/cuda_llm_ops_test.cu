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
#include <limits>
#include <ranges>
#include <string>
#include <vector>

#include "corvid/containers/utils/interval.h"
#include "corvid/cuda/bfloat16.cuh"
#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/llm/llm_ops.cuh"
#include "bfloat16_helpers.cuh"
#include "catch2_main.h"
#include "catch2/catch_template_test_macros.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"
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
using Catch::Matchers::WithinAbs;
using corvid::cuda::bfloat16_t;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;
using corvid::llm::token_id;
using corvid::tests::narrowed;
using corvid::tests::widened;
using matrix_types::col_ndx;
using matrix_types::row_ndx;

// The epsilon every layer norm test passes, GPT-2's own so the oracle case
// matches.
constexpr auto eps = 1e-5F;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region layer_norm

TEMPLATE_TEST_CASE("Device layer norm on hand-computed rows",
    "[LlmOpsTest][cuda]", float, double) {
  using T = TestType;
  // Row 0 has mean 2.5 and biased variance 1.25. Row 1 is constant, so it
  // normalizes to zero and the output is the bias alone.
  const std::vector<T> in_storage{T{1}, T{2}, T{3}, T{4}, T{5}, T{5}, T{5},
      T{5}};
  const matrix_view<T> in_view(in_storage, {.row_count = 2, .col_count = 4});
  constexpr std::array weight_storage{T{1}, T{2}, T{1}, T{2}};
  constexpr std::array bias_storage{T{0}, T{0}, T{0.5}, T{0.5}};

  const cuda_matrix<T> in(in_view);
  cuda_buffer<T> weight(weight_storage.size());
  REQUIRE(weight.load(weight_storage));
  cuda_buffer<T> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  cuda_matrix<T> out(in.extent());

  REQUIRE(corvid::cuda::llm::layer_norm(out, in, weight, bias, eps));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.as_view().store(matrix_lens<T>(out_storage, out.extent())));
  constexpr auto tolerance = 1e-5;
  const auto inv_std = T{1} / std::sqrt(T{1.25} + T{eps});
  CHECK_THAT(out_storage[0], WithinAbs(T{-1.5} * inv_std, tolerance));
  CHECK_THAT(out_storage[1], WithinAbs(T{-0.5} * inv_std * T{2}, tolerance));
  CHECK_THAT(out_storage[2],
      WithinAbs((T{0.5} * inv_std) + T{0.5}, tolerance));
  CHECK_THAT(out_storage[3],
      WithinAbs((T{1.5} * inv_std * T{2}) + T{0.5}, tolerance));
  CHECK_THAT(out_storage[4], WithinAbs(0.0, tolerance));
  CHECK_THAT(out_storage[5], WithinAbs(0.0, tolerance));
  CHECK_THAT(out_storage[6], WithinAbs(0.5, tolerance));
  CHECK_THAT(out_storage[7], WithinAbs(0.5, tolerance));

  // In place gives the same values.
  cuda_matrix<T> same(in_view);
  REQUIRE(corvid::cuda::llm::layer_norm(same, same, weight, bias, eps));
  std::vector<T> same_storage(same.size());
  REQUIRE(same.as_view().store(matrix_lens<T>(same_storage, same.extent())));
  CHECK(same_storage == out_storage);
}

TEST_CASE("Device layer norm matches the oracle",
    "[LlmOpsTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // Every layer norm in the model, both per block and the final one, each fed
  // its own dumped input and compared against its dumped output, as the CPU
  // test does.
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
      const auto in_view = matrix_of(oracle.activations, dump + "/in", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/out", n_embd);
      REQUIRE(in_view.row_extent() == 14);

      const cuda_matrix<float> in(in_view);
      cuda_buffer<float> weight(n_embd);
      REQUIRE(
          weight.load(vector_of(oracle.weights, param + ".weight", n_embd)));
      cuda_buffer<float> bias(n_embd);
      REQUIRE(bias.load(vector_of(oracle.weights, param + ".bias", n_embd)));
      cuda_matrix<float> out(in_view.extent());
      std::vector<float> out_storage(out.size());
      const float_matrix_lens out_lens(out_storage, out.extent());

      REQUIRE(corvid::cuda::llm::layer_norm(out, in, weight, bias, eps));
      REQUIRE(out.as_view().store(out_lens));

      check_close(out_lens, expected, 1e-5F, 1e-5F);
    }
  }
}

TEST_CASE("Device layer norm over bfloat16_t is the float layer norm narrowed",
    "[LlmOpsTest][cuda]") {
  // The hand-computed rows again, whose values are exact in eight bits, so
  // the bf16 kernel widens them to the same floats and its output is the
  // float output rounded once, at the store.
  const std::vector<float> in_values{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 5.0F, 5.0F,
      5.0F};
  const std::vector<float> weight_values{1.0F, 2.0F, 1.0F, 2.0F};
  const std::vector<float> bias_values{0.0F, 0.0F, 0.5F, 0.5F};
  constexpr matrix_types::matrix_extent extent{.row_count = 2, .col_count = 4};

  const cuda_matrix<float> in(float_matrix_view(in_values, extent));
  const cuda_buffer<float> weight(weight_values);
  const cuda_buffer<float> bias(bias_values);
  cuda_matrix<float> out(extent);
  REQUIRE(corvid::cuda::llm::layer_norm(out, in, weight, bias, eps));
  std::vector<float> out_values(out.size());
  REQUIRE(out.as_view().store(float_matrix_lens(out_values, extent)));

  const auto in_narrowed = narrowed(in_values);
  const auto weight_narrowed = narrowed(weight_values);
  const auto bias_narrowed = narrowed(bias_values);
  const cuda_matrix<bfloat16_t> in_bf16(
      matrix_view<bfloat16_t>(in_narrowed, extent));
  const cuda_buffer<bfloat16_t> weight_bf16(weight_narrowed);
  const cuda_buffer<bfloat16_t> bias_bf16(bias_narrowed);
  cuda_matrix<bfloat16_t> out_bf16(extent);
  REQUIRE(corvid::cuda::llm::layer_norm(out_bf16, in_bf16, weight_bf16,
      bias_bf16, eps));
  std::vector<bfloat16_t> out_narrowed(out_bf16.size());
  REQUIRE(
      out_bf16.as_view().store(matrix_lens<bfloat16_t>(out_narrowed, extent)));
  CHECK(out_narrowed == narrowed(out_values));

  // And from the float rows straight into bfloat16, the residual stream's
  // form, which is the same computation with the one rounding at the end.
  cuda_matrix<bfloat16_t> out_mixed(extent);
  REQUIRE(corvid::cuda::llm::layer_norm(out_mixed, in, weight, bias, eps));
  std::vector<bfloat16_t> mixed_narrowed(out_mixed.size());
  REQUIRE(out_mixed.as_view().store(
      matrix_lens<bfloat16_t>(mixed_narrowed, extent)));
  CHECK(mixed_narrowed == narrowed(out_values));
}

#pragma endregion
#pragma region add

TEST_CASE("Device residual adds match the oracle",
    "[LlmOpsTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // Both adds of every block, each fed its own dumped operands. Adding two
  // fp32 values is the same IEEE operation on the device and in the oracle,
  // so the match is exact, with no tolerance at all.
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
      const auto a_view = matrix_of(oracle.activations, residual, n_embd);
      const auto b_view = matrix_of(oracle.activations, correction, n_embd);
      const auto expected = matrix_of(oracle.activations, sum, n_embd);
      REQUIRE(a_view.row_extent() == 14);

      const cuda_matrix<float> a(a_view);
      const cuda_matrix<float> b(b_view);
      cuda_matrix<float> out(a_view.extent());
      std::vector<float> out_storage(out.size());
      const float_matrix_lens out_lens(out_storage, out.extent());

      REQUIRE(corvid::cuda::linalg::add(out, a, b));
      REQUIRE(out.as_view().store(out_lens));

      check_close(out_lens, expected, 0.0F, 0.0F);
    }
  }
}

#pragma endregion
#pragma region gelu_new

TEST_CASE("Device GELU on hand-computed values", "[LlmOpsTest][cuda]") {
  // The same reference values as the CPU test, from torch's gelu with
  // approximate="tanh".
  const std::vector<float> in_storage{0.0F, 1.0F, -1.0F, 2.0F, -2.0F, 10.0F,
      -10.0F};
  const float_matrix_view in_view(in_storage,
      {.row_count = 1, .col_count = in_storage.size()});
  const cuda_matrix<float> in(in_view);
  cuda_matrix<float> out(in.extent());

  REQUIRE(corvid::cuda::llm::gelu_new(out, in));

  std::vector<float> out_storage(out.size());
  REQUIRE(out.as_view().store(float_matrix_lens(out_storage, out.extent())));
  constexpr auto tolerance = 1e-6;
  CHECK(out_storage[0] == 0.0F);
  CHECK_THAT(out_storage[1], WithinAbs(0.841192, tolerance));
  CHECK_THAT(out_storage[2], WithinAbs(-0.158808, tolerance));
  CHECK_THAT(out_storage[3], WithinAbs(1.954598, tolerance));
  CHECK_THAT(out_storage[4], WithinAbs(-0.045402, tolerance));
  CHECK_THAT(out_storage[5], WithinAbs(10.0, tolerance));
  CHECK_THAT(out_storage[6], WithinAbs(0.0, tolerance));

  // In place gives the same values.
  cuda_matrix<float> same(in_view);
  REQUIRE(corvid::cuda::llm::gelu_new(same, same));
  std::vector<float> same_storage(same.size());
  REQUIRE(
      same.as_view().store(float_matrix_lens(same_storage, same.extent())));
  CHECK(same_storage == out_storage);
}

TEST_CASE("Device GELU over bfloat16_t is the float GELU narrowed",
    "[LlmOpsTest][cuda]") {
  // The same reference inputs, all exact in eight bits, so the bf16 result
  // is the float result rounded once, at the store.
  const std::vector<float> in_values{0.0F, 1.0F, -1.0F, 2.0F, -2.0F, 10.0F,
      -10.0F};
  const matrix_types::matrix_extent extent{.row_count = 1,
      .col_count = in_values.size()};

  const cuda_matrix<float> in(float_matrix_view(in_values, extent));
  cuda_matrix<float> out(extent);
  REQUIRE(corvid::cuda::llm::gelu_new(out, in));
  std::vector<float> out_values(out.size());
  REQUIRE(out.as_view().store(float_matrix_lens(out_values, extent)));

  const auto in_narrowed = narrowed(in_values);
  const cuda_matrix<bfloat16_t> in_bf16(
      matrix_view<bfloat16_t>(in_narrowed, extent));
  cuda_matrix<bfloat16_t> out_bf16(extent);
  REQUIRE(corvid::cuda::llm::gelu_new(out_bf16, in_bf16));
  std::vector<bfloat16_t> out_narrowed(out_bf16.size());
  REQUIRE(
      out_bf16.as_view().store(matrix_lens<bfloat16_t>(out_narrowed, extent)));
  CHECK(out_narrowed == narrowed(out_values));
}

#pragma endregion
#pragma region MLP

TEST_CASE("Device MLP path matches the oracle", "[LlmOpsTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const cublas_handle blas;

  // Every block's MLP, fed its own dumped `ln_2/out` and compared against its
  // dumped `mlp/out`, as the CPU test does, with all three ops on the device.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto param = std::format("h.{}.mlp", n);
      const auto in_view =
          matrix_of(oracle.activations, dump + "/ln_2/out", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/mlp/out", n_embd);
      REQUIRE(in_view.row_extent() == 14);

      const cuda_matrix<float> in(in_view);
      const cuda_matrix<float> fc_weight(
          matrix_of(oracle.weights, param + ".c_fc.weight", n_hidden));
      cuda_buffer<float> fc_bias(n_hidden);
      REQUIRE(fc_bias.load(
          vector_of(oracle.weights, param + ".c_fc.bias", n_hidden)));
      const cuda_matrix<float> proj_weight(
          matrix_of(oracle.weights, param + ".c_proj.weight", n_embd));
      cuda_buffer<float> proj_bias(n_embd);
      REQUIRE(proj_bias.load(
          vector_of(oracle.weights, param + ".c_proj.bias", n_embd)));

      cuda_matrix<float> hidden(
          {.row_count = in_view.row_extent(), .col_count = n_hidden});
      cuda_matrix<float> out(in_view.extent());
      std::vector<float> out_storage(out.size());
      const float_matrix_lens out_lens(out_storage, out.extent());

      REQUIRE(corvid::cuda::linalg::linear_projection(blas, hidden, in,
          fc_weight, fc_bias));
      REQUIRE(corvid::cuda::llm::gelu_new(hidden, hidden));
      REQUIRE(corvid::cuda::linalg::linear_projection(blas, out, hidden,
          proj_weight, proj_bias));
      REQUIRE(out.as_view().store(out_lens));

      check_close(out_lens, expected, 1e-4F, 1e-4F);
    }
  }
}

#pragma endregion
#pragma region embed_tokens

TEMPLATE_TEST_CASE("Device embed tokens on hand-computed rows",
    "[LlmOpsTest][cuda]", float, double, bfloat16_t) {
  using T = TestType;
  // Three tokens in the vocabulary, width two, as the CPU test has them.
  const std::vector<T> table_storage{T{1}, T{2}, T{10}, T{20}, T{100}, T{200}};
  const matrix_view<T> table_view(table_storage,
      {.row_count = 3, .col_count = 2});
  const std::vector<token_id> id_storage{token_id{2}, token_id{0}};

  const cuda_matrix<T> table(table_view);
  cuda_buffer<token_id> ids(id_storage.size());
  REQUIRE(ids.load(id_storage));
  cuda_matrix<T> out({.row_count = id_storage.size(), .col_count = 2});

  REQUIRE(corvid::cuda::llm::embed_tokens(out, ids, table));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.as_view().store(matrix_lens<T>(out_storage, out.extent())));
  CHECK(out_storage == std::vector<T>{T{100}, T{200}, T{1}, T{2}});
}

TEMPLATE_TEST_CASE("Device embed positions on hand-computed rows",
    "[LlmOpsTest][cuda]", float, double, bfloat16_t) {
  using T = TestType;
  // A context of three positions, width two, under two tokens, as the CPU
  // test has them.
  const std::vector<T> table_storage{T{1}, T{2}, T{10}, T{20}, T{100}, T{200}};
  const cuda_matrix<T> table(
      matrix_view<T>(table_storage, {.row_count = 3, .col_count = 2}));
  const std::vector<T> in_storage{T{0.5}, T{0.25}, T{-10}, T{5}};
  cuda_matrix<T> out(
      matrix_view<T>(in_storage, {.row_count = 2, .col_count = 2}));

  REQUIRE(corvid::cuda::llm::embed_positions(out, table));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.as_view().store(matrix_lens<T>(out_storage, out.extent())));
  CHECK(out_storage == std::vector<T>{T{1.5}, T{2.25}, T{0}, T{25}});
}

TEST_CASE("Device embeddings from bfloat16_t tables into a float residual",
    "[LlmOpsTest][cuda]") {
  // The two napkin tables held as `bfloat16_t`, gathered and added into a
  // `float` residual, as the engine holds them. The values are exact in
  // eight bits, so both halves are exact.
  const auto table_narrowed =
      narrowed({1.0F, 2.0F, 10.0F, 20.0F, 100.0F, 200.0F});
  const cuda_matrix<bfloat16_t> table(matrix_view<bfloat16_t>(table_narrowed,
      {.row_count = 3, .col_count = 2}));
  const std::vector<token_id> id_storage{token_id{2}, token_id{0}};
  cuda_buffer<token_id> ids(id_storage.size());
  REQUIRE(ids.load(id_storage));
  cuda_matrix<float> out({.row_count = id_storage.size(), .col_count = 2});

  REQUIRE(corvid::cuda::llm::embed_tokens(out, ids, table));
  std::vector<float> out_storage(out.size());
  REQUIRE(out.as_view().store(float_matrix_lens(out_storage, out.extent())));
  CHECK(out_storage == std::vector<float>{100.0F, 200.0F, 1.0F, 2.0F});

  REQUIRE(corvid::cuda::llm::embed_positions(out, table));
  REQUIRE(out.as_view().store(float_matrix_lens(out_storage, out.extent())));
  CHECK(out_storage == std::vector<float>{101.0F, 202.0F, 11.0F, 22.0F});
}

TEST_CASE("Device embed tokens match the oracle",
    "[LlmOpsTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // The bisect prompt's IDs come from the logits dump, the only place the
  // oracle wrote them. The dumped `embed/out` is the token embedding plus the
  // position embedding, so `embed_positions` supplies the second half, and
  // both halves are exact.
  const auto id_storage =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  REQUIRE(id_storage.size() == 14);
  const auto wte_view = matrix_of(oracle.weights, "wte.weight", n_embd);
  const auto wpe_view = matrix_of(oracle.weights, "wpe.weight", n_embd);
  REQUIRE(wte_view.row_extent() == n_vocab);
  REQUIRE(wpe_view.row_extent() == n_ctx);
  const auto expected = matrix_of(oracle.activations, "embed/out", n_embd);

  cuda_buffer<token_id> ids(id_storage.size());
  REQUIRE(ids.load(id_storage));
  const cuda_matrix<float> wte(wte_view);
  const cuda_matrix<float> wpe(wpe_view);
  cuda_matrix<float> out(expected.extent());
  std::vector<float> out_storage(out.size());
  const float_matrix_lens out_lens(out_storage, out.extent());

  REQUIRE(corvid::cuda::llm::embed_tokens(out, ids, wte));
  REQUIRE(corvid::cuda::llm::embed_positions(out, wpe));
  REQUIRE(out.as_view().store(out_lens));

  check_close(out_lens, expected, 0.0F, 0.0F);
}

#pragma endregion
#pragma region attend

TEMPLATE_TEST_CASE("Device causal mask blanks the columns after the diagonal",
    "[LlmOpsTest][cuda]", float, double, bfloat16_t) {
  using T = TestType;
  const std::vector<T> ones(3UZ * 3, T{1});
  cuda_matrix<T> scores(
      matrix_view<T>(ones, {.row_count = 3, .col_count = 3}));
  REQUIRE(corvid::cuda::llm::causal_mask(scores));

  std::vector<T> storage(scores.size());
  REQUIRE(scores.as_view().store(matrix_lens<T>(storage, scores.extent())));
  const T blank{-std::numeric_limits<float>::infinity()};
  CHECK(
      storage ==
      std::vector<T>{T{1}, blank, blank, T{1}, T{1}, blank, T{1}, T{1}, T{1}});
}

TEST_CASE("Device causal mask blanks each square of a stack on its own",
    "[LlmOpsTest][cuda]") {
  const std::vector<float> ones(4UZ * 2, 1.0F);
  cuda_matrix<float> scores(
      float_matrix_view(ones, {.row_count = 4, .col_count = 2}));
  REQUIRE(corvid::cuda::llm::causal_mask(scores));

  std::vector<float> storage(scores.size());
  REQUIRE(scores.as_view().store(float_matrix_lens(storage, scores.extent())));
  constexpr auto blank = -std::numeric_limits<float>::infinity();
  CHECK(storage ==
        std::vector<float>{1.0F, blank, 1.0F, 1.0F, 1.0F, blank, 1.0F, 1.0F});
}

TEST_CASE("Device causal mask over cached tokens starts past them",
    "[LlmOpsTest][cuda]") {
  // Two new tokens after one cached, for two heads. In each head's matrix,
  // row 0 is token 1 and keeps columns 0 and 1, and row 1 is token 2 and
  // keeps all three.
  const std::vector<float> ones(4UZ * 3, 1.0F);
  cuda_matrix<float> scores(
      float_matrix_view(ones, {.row_count = 4, .col_count = 3}));
  REQUIRE(corvid::cuda::llm::causal_mask(scores, 1));

  std::vector<float> storage(scores.size());
  REQUIRE(scores.as_view().store(float_matrix_lens(storage, scores.extent())));
  constexpr auto blank = -std::numeric_limits<float>::infinity();
  CHECK(storage == std::vector<float>{1.0F, 1.0F, blank, 1.0F, 1.0F, 1.0F,
                       1.0F, 1.0F, blank, 1.0F, 1.0F, 1.0F});
}

TEST_CASE("Device attention on three tokens of width two",
    "[LlmOpsTest][cuda]") {
  // The CPU test's napkin example, starting from its `qkv`. There, q and k
  // equal the input rows [1, 0], [0, 1], [1, 1], and v is the input with its
  // columns swapped.
  const std::vector<float> qkv_storage{1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F,
      0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
  const cuda_matrix<float> qkv(
      float_matrix_view(qkv_storage, {.row_count = 3, .col_count = 6}));
  const cublas_handle blas;
  cuda_matrix<float> out({.row_count = 3, .col_count = 2});
  std::vector<float> out_storage(out.size());
  const float_matrix_lens out_lens(out_storage, out.extent());
  constexpr auto tolerance = 1e-5;

  SECTION("one head of width two") {
    cuda_matrix<float> scores({.row_count = 3, .col_count = 3});
    REQUIRE(corvid::cuda::llm::attend(blas, out, qkv, 1, scores));
    REQUIRE(out.as_view().store(out_lens));
    CHECK_THAT(out_storage[0], WithinAbs(0.0, tolerance));
    CHECK_THAT(out_storage[1], WithinAbs(1.0, tolerance));
    CHECK_THAT(out_storage[2], WithinAbs(0.669762, tolerance));
    CHECK_THAT(out_storage[3], WithinAbs(0.330238, tolerance));
    CHECK_THAT(out_storage[4], WithinAbs(0.751745, tolerance));
    CHECK_THAT(out_storage[5], WithinAbs(0.751745, tolerance));
  }

  SECTION("two heads of width one") {
    cuda_matrix<float> scores({.row_count = 2UZ * 3, .col_count = 3});
    REQUIRE(corvid::cuda::llm::attend(blas, out, qkv, 2, scores));
    REQUIRE(out.as_view().store(out_lens));
    CHECK_THAT(out_storage[0], WithinAbs(0.0, tolerance));
    CHECK_THAT(out_storage[1], WithinAbs(1.0, tolerance));
    CHECK_THAT(out_storage[2], WithinAbs(0.5, tolerance));
    CHECK_THAT(out_storage[3], WithinAbs(0.268941, tolerance));
    CHECK_THAT(out_storage[4], WithinAbs(0.577681, tolerance));
    CHECK_THAT(out_storage[5], WithinAbs(0.577681, tolerance));
  }
}

TEST_CASE("Device attention over bfloat16_t on three tokens of width two",
    "[LlmOpsTest][cuda]") {
  // The napkin example held as `bfloat16_t`. Its scores and weights are
  // rounded to eight bits between the launches, so the rows match the float
  // ones within a stated tolerance rather than bit for bit.
  const auto qkv_narrowed = narrowed({1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F,
      1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F});
  const cuda_matrix<bfloat16_t> qkv(
      matrix_view<bfloat16_t>(qkv_narrowed, {.row_count = 3, .col_count = 6}));
  const cublas_handle blas;
  cuda_matrix<bfloat16_t> out({.row_count = 3, .col_count = 2});
  cuda_matrix<bfloat16_t> scores({.row_count = 3, .col_count = 3});
  REQUIRE(corvid::cuda::llm::attend(blas, out, qkv, 1, scores));

  std::vector<bfloat16_t> out_narrowed(out.size());
  REQUIRE(out.as_view().store(
      matrix_lens<bfloat16_t>(out_narrowed, out.extent())));
  const auto out_values = widened(out_narrowed);
  constexpr auto tolerance = 1e-2;
  CHECK_THAT(out_values[0], WithinAbs(0.0, tolerance));
  CHECK_THAT(out_values[1], WithinAbs(1.0, tolerance));
  CHECK_THAT(out_values[2], WithinAbs(0.669762, tolerance));
  CHECK_THAT(out_values[3], WithinAbs(0.330238, tolerance));
  CHECK_THAT(out_values[4], WithinAbs(0.751745, tolerance));
  CHECK_THAT(out_values[5], WithinAbs(0.751745, tolerance));
}

TEST_CASE("Device attention over cached tokens matches a full pass",
    "[LlmOpsTest][cuda]") {
  // The napkin example's `qkv`, as the CPU test uses it. Attending the last
  // two tokens with one cached, or the last token with two cached, gives the
  // rows that the full pass gives those tokens.
  const std::vector<float> qkv_storage{1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F,
      0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
  const cuda_matrix<float> qkv(
      float_matrix_view(qkv_storage, {.row_count = 3, .col_count = 6}));
  const cublas_handle blas;

  for (const auto head_count : {1UZ, 2UZ}) {
    DYNAMIC_SECTION(head_count << " heads") {
      cuda_matrix<float> full({.row_count = 3, .col_count = 2});
      cuda_matrix<float> full_scores(
          {.row_count = head_count * 3, .col_count = 3});
      REQUIRE(
          corvid::cuda::llm::attend(blas, full, qkv, head_count, full_scores));
      std::vector<float> full_storage(full.size());
      REQUIRE(full.as_view().store(
          float_matrix_lens(full_storage, full.extent())));
      const float_matrix_view full_view(full_storage, full.extent());

      for (const auto new_count : {1UZ, 2UZ, 3UZ}) {
        cuda_matrix<float> out({.row_count = new_count, .col_count = 2});
        cuda_matrix<float> scores(
            {.row_count = head_count * new_count, .col_count = 3});
        REQUIRE(corvid::cuda::llm::attend(blas, out, qkv, head_count, scores));
        std::vector<float> out_storage(out.size());
        REQUIRE(
            out.as_view().store(float_matrix_lens(out_storage, out.extent())));
        check_close(float_matrix_view(out_storage, out.extent()),
            full_view[{row_ndx{3 - new_count}, col_ndx{0}}, out.extent()],
            0.0F, 0.0F);
      }
    }
  }
}

TEST_CASE("Device attention path matches the oracle",
    "[LlmOpsTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const cublas_handle blas;

  // Every block's attention, fed its own dumped `ln_1/out` and compared
  // against its dumped `attn/out`, as the CPU test does, with the two
  // projections and the heads all on the device.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto param = std::format("h.{}.attn", n);
      const auto in_view =
          matrix_of(oracle.activations, dump + "/ln_1/out", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/attn/out", n_embd);
      const auto token_count = in_view.row_extent();
      REQUIRE(token_count == 14);

      const cuda_matrix<float> in(in_view);
      const cuda_matrix<float> attn_weight(
          matrix_of(oracle.weights, param + ".c_attn.weight", n_qkv));
      cuda_buffer<float> attn_bias(n_qkv);
      REQUIRE(attn_bias.load(
          vector_of(oracle.weights, param + ".c_attn.bias", n_qkv)));
      const cuda_matrix<float> proj_weight(
          matrix_of(oracle.weights, param + ".c_proj.weight", n_embd));
      cuda_buffer<float> proj_bias(n_embd);
      REQUIRE(proj_bias.load(
          vector_of(oracle.weights, param + ".c_proj.bias", n_embd)));

      cuda_matrix<float> qkv({.row_count = token_count, .col_count = n_qkv});
      cuda_matrix<float> heads_out(in_view.extent());
      cuda_matrix<float> scores(
          {.row_count = n_head * token_count, .col_count = token_count});
      cuda_matrix<float> out(in_view.extent());
      std::vector<float> out_storage(out.size());
      const float_matrix_lens out_lens(out_storage, out.extent());

      REQUIRE(corvid::cuda::linalg::linear_projection(blas, qkv, in,
          attn_weight, attn_bias));
      REQUIRE(corvid::cuda::llm::attend(blas, heads_out, qkv, n_head, scores));
      REQUIRE(corvid::cuda::linalg::linear_projection(blas, out, heads_out,
          proj_weight, proj_bias));
      REQUIRE(out.as_view().store(out_lens));

      check_close(out_lens, expected, 1e-4F, 1e-4F);

      // The last five tokens with nine cached get the same head outputs,
      // bit for bit, as on the CPU.
      constexpr auto new_count = 5UZ;
      cuda_matrix<float> suffix_out(
          {.row_count = new_count, .col_count = n_embd});
      cuda_matrix<float> suffix_scores(
          {.row_count = n_head * new_count, .col_count = token_count});
      REQUIRE(corvid::cuda::llm::attend(blas, suffix_out, qkv, n_head,
          suffix_scores));
      std::vector<float> heads_storage(heads_out.size());
      const float_matrix_lens heads_lens(heads_storage, heads_out.extent());
      REQUIRE(heads_out.as_view().store(heads_lens));
      std::vector<float> suffix_storage(suffix_out.size());
      const float_matrix_lens suffix_lens(suffix_storage, suffix_out.extent());
      REQUIRE(suffix_out.as_view().store(suffix_lens));
      check_close(suffix_lens,
          heads_lens[{row_ndx{token_count - new_count}, col_ndx{0}},
              suffix_out.extent()],
          0.0F, 0.0F);
    }
  }
}

#pragma endregion
#pragma region logits

TEST_CASE("Device logits over bfloat16_t are stored as float",
    "[LlmOpsTest][cuda]") {
  // Two tokens of width three against a vocabulary of two, with values whose
  // float partial sums are exact, so the logits are the exact products and
  // are never rounded to bfloat16.
  const auto in_narrowed =
      narrowed({1.5F, 2.25F, 3.125F, 4.0625F, 5.5F, 6.75F});
  const auto vocab_narrowed =
      narrowed({1.25F, 0.75F, 1.0F, 0.5F, 1.125F, 2.5F});
  constexpr matrix_types::matrix_extent extent{.row_count = 2, .col_count = 3};
  const cublas_handle blas;
  const cuda_matrix<bfloat16_t> in(
      matrix_view<bfloat16_t>(in_narrowed, extent));
  const cuda_matrix<bfloat16_t> vocab(
      matrix_view<bfloat16_t>(vocab_narrowed, extent));
  cuda_matrix<float> logits({.row_count = 2, .col_count = 2});
  REQUIRE(corvid::cuda::llm::compute_logits(blas, logits, in, vocab));

  std::vector<float> logit_values(logits.size());
  REQUIRE(logits.as_view().store(
      float_matrix_lens(logit_values, logits.extent())));
  CHECK(logit_values ==
        std::vector<float>{6.6875F, 11.09375F, 15.953125F, 25.09375F});
}

#pragma endregion
#pragma region pick_greedy

TEST_CASE("Device greedy picks the largest logit in each row",
    "[LlmOpsTest][cuda]") {
  // Five rows, each wider than a block of threads so that every thread walks
  // several columns: the largest first, last, in the middle, tied (the first
  // wins), and every value alike.
  constexpr auto cols = 1000UZ;
  struct row_case {
    std::vector<size_t> peaks;
    token_id expected;
  };
  const std::vector<row_case> rows{
      {{0}, token_id{0}},
      {{cols - 1}, token_id{cols - 1}},
      {{500}, token_id{500}},
      {{300, 700}, token_id{300}},
      {{}, token_id{0}},
  };
  std::vector<float> storage(rows.size() * cols, -1.0F);
  const float_matrix_lens logits_lens(storage,
      {.row_count = rows.size(), .col_count = cols});
  for (const auto [spec, logits_row] :
      std::views::zip(rows, logits_lens.rows()))
    for (const auto peak : spec.peaks) logits_row[col_ndx{peak}] = -0.5F;

  const cuda_matrix<float> logits(
      float_matrix_view(storage, logits_lens.extent()));
  cuda_buffer<token_id> picks(rows.size());
  REQUIRE(corvid::cuda::llm::pick_greedy(picks, logits));
  std::vector<token_id> picked(rows.size());
  REQUIRE(picks.store(picked));

  for (const auto [spec, pick] : std::views::zip(rows, picked))
    CHECK(pick == spec.expected);
}

TEST_CASE("Device greedy picks match the host on every prompt",
    "[LlmOpsTest][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // Every row of each prompt's dumped logits, picked on the device and on the
  // host.
  for (const auto prompt : iota(5)) {
    DYNAMIC_SECTION("prompt_" << prompt) {
      const auto logits_view = matrix_of(oracle.logits,
          std::format("prompt_{}/logits", prompt), n_vocab);
      const cuda_matrix<float> logits(logits_view);
      cuda_buffer<token_id> picks(logits_view.row_extent());
      REQUIRE(corvid::cuda::llm::pick_greedy(picks, logits));
      std::vector<token_id> picked(logits_view.row_extent());
      REQUIRE(picks.store(picked));

      for (const auto [pick, logits_row] :
          std::views::zip(picked, logits_view.rows()))
        CHECK(pick == corvid::llm::pick_greedy(logits_row));
    }
  }
}

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
