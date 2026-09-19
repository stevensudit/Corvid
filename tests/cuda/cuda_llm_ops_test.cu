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
#include <vector>

#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/llm/llm_ops.cuh"
#include "catch2_main.h"
#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include "gpt2_oracle.h"

using namespace corvid;
using namespace corvid::tests::gpt2;
using Catch::Matchers::WithinAbs;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;

// NOLINTBEGIN(readability-function-cognitive-complexity)

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

  REQUIRE(cuda::llm::gelu_new(out.view(), in));

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
  REQUIRE(cuda::llm::gelu_new(same.view(), same));
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

      REQUIRE(cuda::linalg::linear_projection(blas, hidden.view(), in,
          fc_weight, fc_bias));
      REQUIRE(cuda::llm::gelu_new(hidden.view(), hidden));
      REQUIRE(cuda::linalg::linear_projection(blas, out.view(), hidden,
          proj_weight, proj_bias));
      REQUIRE(out.view().store(out_view));

      check_close(out_view, expected, 1e-4F, 1e-4F);
    }
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
