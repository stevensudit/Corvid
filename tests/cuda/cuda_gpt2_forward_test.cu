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
#include <vector>

#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/llm/gpt2_forward.cuh"
#include "corvid/cuda/llm/gpt2_forward.h"
#include "catch2_main.h"
#include "gpt2_oracle.h"

using namespace corvid;
using namespace corvid::tests::gpt2;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_ptr;
using corvid::cuda::llm::device_matrix;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Linear

TEST_CASE("Device linear on hand-computed rows", "[Gpt2ForwardTest][cuda]") {
  // Two rows of three features through a three-by-two weight: the first
  // output column sums features 0 and 2, the second sums features 1 and 2,
  // and each gets its bias.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
  const std::vector<float> weight_storage{1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
  constexpr std::array bias_storage{10.0F, 20.0F};

  const cublas_handle blas;
  const device_matrix in(
      const_float_matrix_view(in_storage, {.row_count = 2, .col_count = 3}));
  const device_matrix weight(const_float_matrix_view(weight_storage,
      {.row_count = 3, .col_count = 2}));
  cuda_ptr<float> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  device_matrix out({.row_count = 2, .col_count = 2});

  REQUIRE(cuda::llm::linear(blas, out, in, weight, bias));

  std::vector<float> out_storage(out.size());
  REQUIRE(out.store(float_matrix_view(out_storage, out.extent())));
  CHECK(out_storage == std::vector<float>{14.0F, 25.0F, 20.0F, 31.0F});
}

TEST_CASE("Device MLP path matches the oracle",
    "[Gpt2ForwardTest][oracle][cuda]") {
  oracle_dumps oracle;
  oracle.load();
  const cublas_handle blas;

  // Every block's MLP, fed its own dumped `ln_2/out` and compared against its
  // dumped `mlp/out`, as the CPU test does. Both projections run on the
  // device; `gelu_new` runs on the host, so the hidden matrix round-trips
  // between them.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto param = std::format("h.{}.mlp", n);
      const auto in_view =
          matrix_of(oracle.activations, dump + "/ln_2/out", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/mlp/out", n_embd);
      REQUIRE(in_view.row_extent() == 14);

      const device_matrix in(in_view);
      const device_matrix fc_weight(
          matrix_of(oracle.weights, param + ".c_fc.weight", n_hidden));
      cuda_ptr<float> fc_bias(n_hidden);
      REQUIRE(fc_bias.load(
          vector_of(oracle.weights, param + ".c_fc.bias", n_hidden)));
      const device_matrix proj_weight(
          matrix_of(oracle.weights, param + ".c_proj.weight", n_embd));
      cuda_ptr<float> proj_bias(n_embd);
      REQUIRE(proj_bias.load(
          vector_of(oracle.weights, param + ".c_proj.bias", n_embd)));

      device_matrix hidden(
          {.row_count = in_view.row_extent(), .col_count = n_hidden});
      device_matrix out(in_view.extent());
      std::vector<float> hidden_storage(hidden.size());
      const float_matrix_view hidden_view(hidden_storage, hidden.extent());
      std::vector<float> out_storage(out.size());
      const float_matrix_view out_view(out_storage, out.extent());

      REQUIRE(cuda::llm::linear(blas, hidden, in, fc_weight, fc_bias));
      REQUIRE(hidden.store(hidden_view));
      llm::gelu_new(hidden_view, hidden_view);
      REQUIRE(hidden.load(hidden_view));
      REQUIRE(cuda::llm::linear(blas, out, hidden, proj_weight, proj_bias));
      REQUIRE(out.store(out_view));

      check_close(out_view, expected, 1e-4F, 1e-4F);
    }
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
