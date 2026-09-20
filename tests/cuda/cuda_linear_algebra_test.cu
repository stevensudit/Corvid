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
using corvid::cuda::cuda_matrix_view;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region DeviceMatrixLike

TEST_CASE("DeviceMatrixLike admits only writable outputs",
    "[LinearAlgebraTest][cuda]") {
  using corvid::cuda::DeviceMatrixLike;
  // A matrix, a mutable view, and a const mutable view (shallow const) are
  // writable. A const matrix (deep const) and a view of const elements are
  // not.
  static_assert(DeviceMatrixLike<cuda_matrix<float>>);
  static_assert(DeviceMatrixLike<cuda_matrix<float>&>);
  static_assert(DeviceMatrixLike<cuda_matrix_view<float>>);
  static_assert(DeviceMatrixLike<const cuda_matrix_view<float>&>);
  static_assert(!DeviceMatrixLike<const cuda_matrix<float>>);
  static_assert(!DeviceMatrixLike<const cuda_matrix<float>&>);
  static_assert(!DeviceMatrixLike<cuda_matrix_view<const float>>);
  static_assert(!DeviceMatrixLike<int>);
}

#pragma endregion
#pragma region Launch geometry

TEST_CASE("Grid for an extent", "[LinearAlgebraTest][cuda]") {
  // Columns fill 256-thread blocks along x, and each row is a block along y.
  const cuda_matrix<float> m({.row_count = 14, .col_count = 768});
  const auto grid = cuda::linalg::grid_for(m);
  CHECK(grid.x == 3);
  CHECK(grid.y == 14);
  CHECK(grid.z == 1);

  // A view of it, and a bare extent, give the same grid, with a partial block
  // rounding up.
  const auto narrow = cuda::linalg::grid_for(
      m.subview({}, {.row_count = 2, .col_count = 257}));
  CHECK(narrow.x == 2);
  CHECK(narrow.y == 2);
  const auto bare = cuda::linalg::grid_for(
      matrix_view<float>::extent_t{.row_count = 1, .col_count = 1});
  CHECK(bare.x == 1);
  CHECK(bare.y == 1);
}

#pragma endregion
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
  REQUIRE(out.as_view().store(matrix_view<T>(out_storage, out.extent())));
  CHECK(out_storage == std::vector<T>{T{14}, T{25}, T{20}, T{31}});
}

#pragma endregion
#pragma region GEMM

TEMPLATE_TEST_CASE("Device gemm product, scale, and addends",
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

  // The plain product reads nothing from `out`.
  REQUIRE(cuda::linalg::gemm(blas, out, a, b));
  REQUIRE(out.as_view().store(out_view));
  CHECK(out_storage == std::vector<T>{T{4}, T{5}, T{10}, T{11}});

  // Twice the product, accumulated onto ones.
  const std::vector<T> ones_storage(out.size(), T{1});
  REQUIRE(
      out.as_view().load(matrix_view<const T>(ones_storage, out.extent())));
  REQUIRE(cuda::linalg::gemm(blas, out, a, b, {.scale = 2}, out));
  REQUIRE(out.as_view().store(out_view));
  CHECK(out_storage == std::vector<T>{T{9}, T{11}, T{21}, T{23}});

  // The product plus a separate addend, which is left untouched.
  const std::vector<T> addend_storage{T{100}, T{200}, T{300}, T{400}};
  const cuda_matrix<T> addend(
      matrix_view<const T>(addend_storage, out.extent()));
  REQUIRE(cuda::linalg::gemm(blas, out, a, b, {}, addend));
  REQUIRE(out.as_view().store(out_view));
  CHECK(out_storage == std::vector<T>{T{104}, T{205}, T{310}, T{411}});
  REQUIRE(addend.as_view().store(out_view));
  CHECK(out_storage == addend_storage);

  // A zero addend scale drops the addend, copy and all.
  REQUIRE(cuda::linalg::gemm(blas, out, a, b, {.addend_scale = 0}, addend));
  REQUIRE(out.as_view().store(out_view));
  CHECK(out_storage == std::vector<T>{T{4}, T{5}, T{10}, T{11}});

  // The product plus twice a bias row.
  constexpr std::array bias_storage{T{10}, T{20}};
  cuda_buffer<T> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  REQUIRE(cuda::linalg::gemm(blas, out, a, b, bias, {.addend_scale = 2}));
  REQUIRE(out.as_view().store(out_view));
  CHECK(out_storage == std::vector<T>{T{24}, T{45}, T{30}, T{51}});
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

  REQUIRE(cuda::linalg::gemm(blas, out, a, b,
      {.op_a = cublas_operation::transpose}));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.as_view().store(matrix_view<T>(out_storage, out.extent())));
  CHECK(out_storage == std::vector<T>{T{6}, T{8}, T{8}, T{10}});
}

TEMPLATE_TEST_CASE("Device gemm over strided views",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  using view_t = cuda_matrix_view<T>;
  using row_ndx = view_t::row_ndx;
  using col_ndx = view_t::col_ndx;
  // The same product as above, with each operand the leading columns of a
  // wider matrix, and `out` the leading columns of one whose last column
  // must survive untouched. `x` marks filler.
  constexpr auto x = T{-1};
  const std::vector<T> a_storage{T{1}, T{2}, T{3}, x, T{4}, T{5}, T{6}, x};
  const std::vector<T> b_storage{T{1}, T{0}, x, T{0}, T{1}, x, T{1}, T{1}, x};
  const std::vector<T> out_start{x, x, T{9}, x, x, T{9}};

  const cublas_handle blas;
  const cuda_matrix<T> a_wide(
      matrix_view<const T>(a_storage, {.row_count = 2, .col_count = 4}));
  const cuda_matrix<T> b_wide(
      matrix_view<const T>(b_storage, {.row_count = 3, .col_count = 3}));
  cuda_matrix<T> out_wide(
      matrix_view<const T>(out_start, {.row_count = 2, .col_count = 3}));
  const auto a = a_wide.subview({row_ndx{0}, col_ndx{0}},
      {.row_count = 2, .col_count = 3});
  const auto b = b_wide.subview({row_ndx{0}, col_ndx{0}},
      {.row_count = 3, .col_count = 2});
  const auto out = out_wide.subview({row_ndx{0}, col_ndx{0}},
      {.row_count = 2, .col_count = 2});
  CHECK(a.stride() == 4);
  CHECK(!a.is_packed());
  CHECK(out_wide.as_view().is_packed());

  std::vector<T> wide_storage(out_wide.size());
  const matrix_view<T> wide_view(wide_storage, out_wide.extent());

  // The product lands in the window, and the column beside it keeps its 9s.
  REQUIRE(cuda::linalg::gemm(blas, out, a, b));
  REQUIRE(out_wide.as_view().store(wide_view));
  CHECK(wide_storage == std::vector<T>{T{4}, T{5}, T{9}, T{10}, T{11}, T{9}});

  // A bias row broadcast into the strided window.
  constexpr std::array bias_storage{T{10}, T{20}};
  cuda_buffer<T> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  REQUIRE(cuda::linalg::gemm(blas, out, a, b, bias));
  REQUIRE(out_wide.as_view().store(wide_view));
  CHECK(
      wide_storage == std::vector<T>{T{14}, T{25}, T{9}, T{20}, T{31}, T{9}});

  // The window stores and loads on its own, through pitched copies.
  std::vector<T> window_storage(out.size());
  const matrix_view<T> window_view(window_storage, out.extent());
  REQUIRE(out.store(window_view));
  CHECK(window_storage == std::vector<T>{T{14}, T{25}, T{20}, T{31}});
  const std::vector<T> ones_storage(out.size(), T{1});
  REQUIRE(out.load(matrix_view<const T>(ones_storage, out.extent())));
  REQUIRE(out_wide.as_view().store(wide_view));
  CHECK(wide_storage == std::vector<T>{T{1}, T{1}, T{9}, T{1}, T{1}, T{9}});

  // A strided addend, copied device to device: columns 1 and 2 of `a_wide`.
  const auto addend = a_wide.subview({row_ndx{0}, col_ndx{1}}, out.extent());
  REQUIRE(cuda::linalg::gemm(blas, out, a, b, {}, addend));
  REQUIRE(out_wide.as_view().store(wide_view));
  CHECK(wide_storage == std::vector<T>{T{6}, T{8}, T{9}, T{15}, T{17}, T{9}});
}

#pragma endregion
#pragma region add

TEMPLATE_TEST_CASE("Device add on hand-computed rows",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  using view_t = cuda_matrix_view<T>;
  using row_ndx = view_t::row_ndx;
  using col_ndx = view_t::col_ndx;
  using extent_t = view_t::extent_t;
  const std::vector<T> a_storage{T{1}, T{2}, T{3}, T{4}};
  const std::vector<T> b_storage{T{10}, T{20}, T{30}, T{40}};
  const matrix_view<const T> a_view(a_storage,
      {.row_count = 2, .col_count = 2});
  const matrix_view<const T> b_view(b_storage,
      {.row_count = 2, .col_count = 2});
  const std::vector<T> expected{T{11}, T{22}, T{33}, T{44}};
  cuda_matrix<T> a(a_view);
  cuda_matrix<T> b(b_view);
  std::vector<T> storage(a.size());
  const matrix_view<T> out_view(storage, a.extent());

  SECTION("into a separate matrix") {
    cuda_matrix<T> out(a.extent());
    REQUIRE(cuda::linalg::add(out, a, b));
    REQUIRE(out.as_view().store(out_view));
    CHECK(storage == expected);
  }

  SECTION("in place on the left") {
    REQUIRE(cuda::linalg::add(a, a, b));
    REQUIRE(a.as_view().store(out_view));
    CHECK(storage == expected);
  }

  SECTION("in place on the right") {
    REQUIRE(cuda::linalg::add(b, a, b));
    REQUIRE(b.as_view().store(out_view));
    CHECK(storage == expected);
  }

  SECTION("over strided windows") {
    // The operands are the leading two columns of three-wide matrices, and
    // `out` the leading columns of one whose last column must survive
    // untouched. `x` marks filler.
    constexpr auto x = T{-1};
    const std::vector<T> a_wide_storage{T{1}, T{2}, x, T{3}, T{4}, x};
    const std::vector<T> b_wide_storage{T{10}, T{20}, x, T{30}, T{40}, x};
    const std::vector<T> out_start{x, x, T{9}, x, x, T{9}};
    const cuda_matrix<T> a_wide(matrix_view<const T>(a_wide_storage,
        {.row_count = 2, .col_count = 3}));
    const cuda_matrix<T> b_wide(matrix_view<const T>(b_wide_storage,
        {.row_count = 2, .col_count = 3}));
    cuda_matrix<T> out_wide(
        matrix_view<const T>(out_start, {.row_count = 2, .col_count = 3}));
    const extent_t window{.row_count = 2, .col_count = 2};
    const auto out = out_wide.subview({row_ndx{0}, col_ndx{0}}, window);
    CHECK(!out.is_packed());

    REQUIRE(cuda::linalg::add(out,
        a_wide.subview({row_ndx{0}, col_ndx{0}}, window),
        b_wide.subview({row_ndx{0}, col_ndx{0}}, window)));

    std::vector<T> wide_storage(out_wide.size());
    REQUIRE(out_wide.as_view().store(
        matrix_view<T>(wide_storage, out_wide.extent())));
    const std::vector<T> wide_expected{T{11}, T{22}, T{9}, T{33}, T{44}, T{9}};
    CHECK(wide_storage == wide_expected);
  }
}

#pragma endregion
#pragma region subtract

TEMPLATE_TEST_CASE("Device subtract on hand-computed rows",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  const std::vector<T> a_storage{T{11}, T{22}, T{33}, T{44}};
  const std::vector<T> b_storage{T{1}, T{2}, T{3}, T{4}};
  const matrix_view<const T> a_view(a_storage,
      {.row_count = 2, .col_count = 2});
  const matrix_view<const T> b_view(b_storage,
      {.row_count = 2, .col_count = 2});
  const std::vector<T> expected{T{10}, T{20}, T{30}, T{40}};
  cuda_matrix<T> a(a_view);
  cuda_matrix<T> b(b_view);
  std::vector<T> storage(a.size());
  const matrix_view<T> out_view(storage, a.extent());

  SECTION("into a separate matrix") {
    cuda_matrix<T> out(a.extent());
    REQUIRE(cuda::linalg::subtract(out, a, b));
    REQUIRE(out.as_view().store(out_view));
    CHECK(storage == expected);
  }

  SECTION("in place on the left") {
    REQUIRE(cuda::linalg::subtract(a, a, b));
    REQUIRE(a.as_view().store(out_view));
    CHECK(storage == expected);
  }

  SECTION("in place on the right") {
    REQUIRE(cuda::linalg::subtract(b, a, b));
    REQUIRE(b.as_view().store(out_view));
    CHECK(storage == expected);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
