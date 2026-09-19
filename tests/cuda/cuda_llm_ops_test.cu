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
#include <string>
#include <vector>

#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/llm/llm_ops.cuh"
#include "catch2_main.h"
#include "catch2/catch_template_test_macros.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include "gpt2_oracle.h"

using namespace corvid;
using namespace corvid::tests::gpt2;
using Catch::Matchers::WithinAbs;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;

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
  const matrix_view<const T> in_view(in_storage,
      {.row_count = 2, .col_count = 4});
  constexpr std::array weight_storage{T{1}, T{2}, T{1}, T{2}};
  constexpr std::array bias_storage{T{0}, T{0}, T{0.5}, T{0.5}};

  const cuda_matrix<T> in(in_view);
  cuda_buffer<T> weight(weight_storage.size());
  REQUIRE(weight.load(weight_storage));
  cuda_buffer<T> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  cuda_matrix<T> out(in.extent());

  REQUIRE(cuda::llm::layer_norm(out, in, weight, bias, eps));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.view().store(matrix_view<T>(out_storage, out.extent())));
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
  REQUIRE(cuda::llm::layer_norm(same, same, weight, bias, eps));
  std::vector<T> same_storage(same.size());
  REQUIRE(same.view().store(matrix_view<T>(same_storage, same.extent())));
  CHECK(same_storage == out_storage);
}

TEST_CASE("Device layer norm matches the oracle",
    "[LlmOpsTest][oracle][cuda]") {
  oracle_dumps oracle;
  oracle.load();

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
      const float_matrix_view out_view(out_storage, out.extent());

      REQUIRE(cuda::llm::layer_norm(out, in, weight, bias, eps));
      REQUIRE(out.view().store(out_view));

      check_close(out_view, expected, 1e-5F, 1e-5F);
    }
  }
}

#pragma endregion
#pragma region add

TEST_CASE("Device residual adds match the oracle",
    "[LlmOpsTest][oracle][cuda]") {
  oracle_dumps oracle;
  oracle.load();

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
      const float_matrix_view out_view(out_storage, out.extent());

      REQUIRE(cuda::linalg::add(out, a, b));
      REQUIRE(out.view().store(out_view));

      check_close(out_view, expected, 0.0F, 0.0F);
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
  const const_float_matrix_view in_view(in_storage,
      {.row_count = 1, .col_count = in_storage.size()});
  const cuda_matrix<float> in(in_view);
  cuda_matrix<float> out(in.extent());

  REQUIRE(cuda::llm::gelu_new(out, in));

  std::vector<float> out_storage(out.size());
  REQUIRE(out.view().store(float_matrix_view(out_storage, out.extent())));
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
  REQUIRE(cuda::llm::gelu_new(same, same));
  std::vector<float> same_storage(same.size());
  REQUIRE(same.view().store(float_matrix_view(same_storage, same.extent())));
  CHECK(same_storage == out_storage);
}

#pragma endregion
#pragma region MLP

TEST_CASE("Device MLP path matches the oracle", "[LlmOpsTest][oracle][cuda]") {
  oracle_dumps oracle;
  oracle.load();
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
      const float_matrix_view out_view(out_storage, out.extent());

      REQUIRE(cuda::linalg::linear_projection(blas, hidden, in, fc_weight,
          fc_bias));
      REQUIRE(cuda::llm::gelu_new(hidden, hidden));
      REQUIRE(cuda::linalg::linear_projection(blas, out, hidden, proj_weight,
          proj_bias));
      REQUIRE(out.view().store(out_view));

      check_close(out_view, expected, 1e-4F, 1e-4F);
    }
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
