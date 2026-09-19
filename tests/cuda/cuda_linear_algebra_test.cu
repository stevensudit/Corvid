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
#include "catch2/catch_template_test_macros.hpp"

using namespace corvid;
using corvid::cuda::cublas_handle;
using corvid::cuda::cublas_operation;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Linear

TEMPLATE_TEST_CASE("Device linear on hand-computed rows",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  // Two rows of three features through a three-by-two weight: the first
  // output column sums features 0 and 2, the second sums features 1 and 2,
  // and each gets its bias.
  const std::vector<T> in_storage{T{1}, T{2}, T{3}, T{4}, T{5}, T{6}};
  const std::vector<T> weight_storage{T{1}, T{0}, T{0}, T{1}, T{1}, T{1}};
  constexpr std::array bias_storage{T{10}, T{20}};

  const cublas_handle blas;
  const cuda_matrix<T> in(
      matrix_view<const T>(in_storage, {.row_count = 2, .col_count = 3}));
  const cuda_matrix<T> weight(
      matrix_view<const T>(weight_storage, {.row_count = 3, .col_count = 2}));
  cuda_buffer<T> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  cuda_matrix<T> out({.row_count = 2, .col_count = 2});

  REQUIRE(cuda::linalg::linear_projection(blas, out, in, weight, bias));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.store(matrix_view<T>(out_storage, out.extent())));
  CHECK(out_storage == std::vector<T>{T{14}, T{25}, T{20}, T{31}});
}

#pragma endregion
#pragma region GEMM

TEMPLATE_TEST_CASE("Device gemm product, scale, and accumulation",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  // The linear case's two-by-three times three-by-two without its bias:
  // [[4, 5], [10, 11]].
  const std::vector<T> a_storage{T{1}, T{2}, T{3}, T{4}, T{5}, T{6}};
  const std::vector<T> b_storage{T{1}, T{0}, T{0}, T{1}, T{1}, T{1}};

  const cublas_handle blas;
  const cuda_matrix<T> a(
      matrix_view<const T>(a_storage, {.row_count = 2, .col_count = 3}));
  const cuda_matrix<T> b(
      matrix_view<const T>(b_storage, {.row_count = 3, .col_count = 2}));
  cuda_matrix<T> out({.row_count = 2, .col_count = 2});
  std::vector<T> out_storage(out.size());
  const matrix_view<T> out_view(out_storage, out.extent());

  // A plain product, reading nothing from `out`.
  REQUIRE(cuda::linalg::gemm(blas, out, a, b, nullptr, T{1}, T{0}));
  REQUIRE(out.store(out_view));
  CHECK(out_storage == std::vector<T>{T{4}, T{5}, T{10}, T{11}});

  // Twice the product, accumulated onto ones.
  const std::vector<T> ones_storage(out.size(), T{1});
  REQUIRE(out.load(matrix_view<const T>(ones_storage, out.extent())));
  REQUIRE(cuda::linalg::gemm(blas, out, a, b, nullptr, T{2}));
  REQUIRE(out.store(out_view));
  CHECK(out_storage == std::vector<T>{T{9}, T{11}, T{21}, T{23}});
}

TEMPLATE_TEST_CASE("Device gemm on a transposed operand",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  // `a` is stored three-by-two, so its transpose, [[1, 0, 1], [0, 1, 1]],
  // times the three-by-two `b` is [[6, 8], [8, 10]].
  const std::vector<T> a_storage{T{1}, T{0}, T{0}, T{1}, T{1}, T{1}};
  const std::vector<T> b_storage{T{1}, T{2}, T{3}, T{4}, T{5}, T{6}};

  const cublas_handle blas;
  const cuda_matrix<T> a(
      matrix_view<const T>(a_storage, {.row_count = 3, .col_count = 2}));
  const cuda_matrix<T> b(
      matrix_view<const T>(b_storage, {.row_count = 3, .col_count = 2}));
  cuda_matrix<T> out({.row_count = 2, .col_count = 2});

  REQUIRE(cuda::linalg::gemm(blas, out, a, b, nullptr, T{1}, T{0},
      cublas_operation::transpose));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.store(matrix_view<T>(out_storage, out.extent())));
  CHECK(out_storage == std::vector<T>{T{6}, T{8}, T{8}, T{10}});
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
