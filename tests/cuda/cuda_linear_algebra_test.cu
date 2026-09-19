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
#include <vector>

#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/linalg/linear_algebra.cuh"
#include "catch2_main.h"

using namespace corvid;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Linear

TEST_CASE("Device linear on hand-computed rows", "[LinearAlgebraTest][cuda]") {
  // Two rows of three features through a three-by-two weight: the first
  // output column sums features 0 and 2, the second sums features 1 and 2,
  // and each gets its bias.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
  const std::vector<float> weight_storage{1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
  constexpr std::array bias_storage{10.0F, 20.0F};

  const cublas_handle blas;
  const cuda_matrix in(
      const_float_matrix_view(in_storage, {.row_count = 2, .col_count = 3}));
  const cuda_matrix weight(const_float_matrix_view(weight_storage,
      {.row_count = 3, .col_count = 2}));
  cuda_buffer<float> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  cuda_matrix out({.row_count = 2, .col_count = 2});

  REQUIRE(cuda::linalg::linear_projection(blas, out, in, weight, bias));

  std::vector<float> out_storage(out.size());
  REQUIRE(out.store(float_matrix_view(out_storage, out.extent())));
  CHECK(out_storage == std::vector<float>{14.0F, 25.0F, 20.0F, 31.0F});
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
