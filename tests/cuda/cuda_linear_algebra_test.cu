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
#include <ranges>
#include <vector>

#include "corvid/containers/utils/interval.h"
#include "corvid/cuda/bfloat16.cuh"
#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/linalg/linear_algebra.cuh"
#include "bfloat16_helpers.cuh"
#include "catch2_main.h"
#include "catch2/catch_template_test_macros.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"

// The whole test sits in a named namespace. A `using namespace corvid;` at
// global scope would make `cuda` (libcu++'s namespace against corvid::cuda)
// and `log` (corvid::infra::log against the C math function) ambiguous in the
// host code nvcc appends after the translation unit, and clang sees the same
// ambiguity wherever the test spells `cuda::`, which is why it is spelled
// `corvid::cuda::` throughout.
namespace corvid_tests {

using namespace corvid;
using Catch::Matchers::WithinAbs;
using corvid::cuda::bfloat16_t;
using corvid::cuda::cublas_handle;
using corvid::cuda::cublas_operation;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;
using corvid::cuda::cuda_matrix_lens;
using corvid::cuda::cuda_matrix_view;
using corvid::cuda::DeviceMatrixLike;
using corvid::cuda::DeviceMatrixViewable;
using corvid::cuda::GemmOutput;
using corvid::cuda::kernel_col_range;
using corvid::matrix_types::matrix_axis;
using corvid::tests::narrowed;
using corvid::tests::widened;

namespace {

// Each thread walks its columns of a `cols`-wide row and records how many it
// visited and their sum, so the test can pin which columns each thread owns.
__global__ void walk_columns(unsigned* count, unsigned* sum, size_t cols) {
  const auto thread = corvid::cuda::cuda_kernel::x_thread();
  const kernel_col_range columns{cols};
  auto visited = 0U;
  auto total = 0U;
  for (const auto c : columns) {
    ++visited;
    total += static_cast<unsigned>(c);
  }
  count[thread] = visited;
  sum[thread] = total;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region DeviceMatrixLike

TEST_CASE("DeviceMatrixLike admits only writable outputs",
    "[LinearAlgebraTest][cuda]") {
  // A matrix, a mutable view, and a const mutable view (shallow const) are
  // writable. A const matrix (deep const) and a view of const elements are
  // not.
  static_assert(DeviceMatrixLike<cuda_matrix<float>>);
  static_assert(DeviceMatrixLike<cuda_matrix<float>&>);
  static_assert(DeviceMatrixLike<cuda_matrix_lens<float>>);
  static_assert(DeviceMatrixLike<const cuda_matrix_lens<float>&>);
  static_assert(!DeviceMatrixLike<const cuda_matrix<float>>);
  static_assert(!DeviceMatrixLike<const cuda_matrix<float>&>);
  static_assert(!DeviceMatrixLike<cuda_matrix_view<float>>);
  static_assert(!DeviceMatrixLike<int>);
}

TEST_CASE("DeviceMatrixViewable admits anything that converts to a view",
    "[LinearAlgebraTest][cuda]") {
  static_assert(DeviceMatrixViewable<cuda_matrix<float>>);
  static_assert(DeviceMatrixViewable<const cuda_matrix<float>>);
  static_assert(DeviceMatrixViewable<cuda_matrix<float>&>);
  static_assert(DeviceMatrixViewable<cuda_matrix_lens<float>>);
  static_assert(DeviceMatrixViewable<cuda_matrix_view<float>>);
  static_assert(!DeviceMatrixViewable<int>);
  static_assert(std::same_as<
      corvid::cuda::device_value_t<cuda_matrix_view<float>>, float>);
}

TEST_CASE("GemmOutput admits a product's own type and float over bfloat16_t",
    "[LinearAlgebraTest][cuda]") {
  static_assert(GemmOutput<float, float>);
  static_assert(GemmOutput<double, double>);
  static_assert(GemmOutput<bfloat16_t, bfloat16_t>);
  static_assert(GemmOutput<bfloat16_t, float>);
  static_assert(!GemmOutput<float, bfloat16_t>);
  static_assert(!GemmOutput<float, double>);
  static_assert(!GemmOutput<double, float>);
  static_assert(!GemmOutput<int, int>);
}

#pragma endregion
#pragma region kernel_col_range

TEST_CASE("Kernel column range strides by the block width",
    "[LinearAlgebraTest][cuda]") {
  // Four threads over ten columns: thread 0 owns 0, 4, 8 and thread 3 owns
  // 3, 7. A zero-column row is walked by nobody.
  constexpr auto threads = 4U;
  cuda_buffer<unsigned> d_count{threads};
  cuda_buffer<unsigned> d_sum{threads};
  std::vector<unsigned> count(threads);
  std::vector<unsigned> sum(threads);

  walk_columns<<<1, threads>>>(d_count.get(), d_sum.get(), 10);
  REQUIRE(d_count.store(count));
  REQUIRE(d_sum.store(sum));
  CHECK(count == std::vector<unsigned>{3, 3, 2, 2});
  CHECK(sum == std::vector<unsigned>{12, 15, 8, 10});

  walk_columns<<<1, threads>>>(d_count.get(), d_sum.get(), 0);
  REQUIRE(d_count.store(count));
  CHECK(count == std::vector<unsigned>{0, 0, 0, 0});
}

#pragma endregion
#pragma region Launch geometry

TEST_CASE("Grid for an extent", "[LinearAlgebraTest][cuda]") {
  // Columns fill 256-thread blocks along x, and each row is a block along y.
  const cuda_matrix<float> m({.row_count = 14, .col_count = 768});
  const auto grid = corvid::cuda::linalg::grid_for(m);
  CHECK(grid.x == 3);
  CHECK(grid.y == 14);
  CHECK(grid.z == 1);

  // A view of it, and a bare extent, give the same grid, with a partial block
  // rounding up.
  const auto narrow = corvid::cuda::linalg::grid_for(
      m[{}, {.row_count = 2, .col_count = 257}]);
  CHECK(narrow.x == 2);
  CHECK(narrow.y == 2);
  const auto bare = corvid::cuda::linalg::grid_for(
      matrix_lens<float>::extent_t{.row_count = 1, .col_count = 1});
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
      matrix_view<T>(in_storage, {.row_count = 2, .col_count = 3}));
  const cuda_matrix<T> weight(
      matrix_view<T>(weight_storage, {.row_count = 3, .col_count = 2}));
  cuda_buffer<T> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  cuda_matrix<T> out({.row_count = 2, .col_count = 2});

  REQUIRE(
      corvid::cuda::linalg::linear_projection(blas, out, in, weight, bias));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.as_view().store(matrix_lens<T>(out_storage, out.extent())));
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
      matrix_view<T>(a_storage, {.row_count = 2, .col_count = 3}));
  const cuda_matrix<T> b(
      matrix_view<T>(b_storage, {.row_count = 3, .col_count = 2}));
  cuda_matrix<T> out({.row_count = 2, .col_count = 2});
  std::vector<T> out_storage(out.size());
  const matrix_lens<T> out_lens(out_storage, out.extent());

  // The plain product reads nothing from `out`.
  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b));
  REQUIRE(out.as_view().store(out_lens));
  CHECK(out_storage == std::vector<T>{T{4}, T{5}, T{10}, T{11}});

  // Twice the product, accumulated onto ones.
  const std::vector<T> ones_storage(out.size(), T{1});
  REQUIRE(out.as_lens().load(matrix_view<T>(ones_storage, out.extent())));
  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b, {.scale = 2}, out));
  REQUIRE(out.as_view().store(out_lens));
  CHECK(out_storage == std::vector<T>{T{9}, T{11}, T{21}, T{23}});

  // The product plus a separate addend, which is left untouched.
  const std::vector<T> addend_storage{T{100}, T{200}, T{300}, T{400}};
  const cuda_matrix<T> addend(matrix_view<T>(addend_storage, out.extent()));
  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b, {}, addend));
  REQUIRE(out.as_view().store(out_lens));
  CHECK(out_storage == std::vector<T>{T{104}, T{205}, T{310}, T{411}});
  REQUIRE(addend.as_view().store(out_lens));
  CHECK(out_storage == addend_storage);

  // A zero addend scale drops the addend, copy and all.
  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b, {.addend_scale = 0},
      addend));
  REQUIRE(out.as_view().store(out_lens));
  CHECK(out_storage == std::vector<T>{T{4}, T{5}, T{10}, T{11}});

  // The product plus twice a bias row.
  constexpr std::array bias_storage{T{10}, T{20}};
  cuda_buffer<T> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  REQUIRE(
      corvid::cuda::linalg::gemm(blas, out, a, b, bias, {.addend_scale = 2}));
  REQUIRE(out.as_view().store(out_lens));
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
      matrix_view<T>(a_storage, {.row_count = 3, .col_count = 2}));
  const cuda_matrix<T> b(
      matrix_view<T>(b_storage, {.row_count = 3, .col_count = 2}));
  cuda_matrix<T> out({.row_count = 2, .col_count = 2});

  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b,
      {.op_a = cublas_operation::transpose}));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.as_view().store(matrix_lens<T>(out_storage, out.extent())));
  CHECK(out_storage == std::vector<T>{T{6}, T{8}, T{8}, T{10}});
}

TEMPLATE_TEST_CASE("Device gemm over strided views",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  using lens_t = cuda_matrix_lens<T>;
  using row_ndx = lens_t::row_ndx;
  using col_ndx = lens_t::col_ndx;
  // The same product as above, with each operand the leading columns of a
  // wider matrix, and `out` the leading columns of one whose last column
  // must survive untouched. `x` marks filler.
  constexpr auto x = T{-1};
  const std::vector<T> a_storage{T{1}, T{2}, T{3}, x, T{4}, T{5}, T{6}, x};
  const std::vector<T> b_storage{T{1}, T{0}, x, T{0}, T{1}, x, T{1}, T{1}, x};
  const std::vector<T> out_start{x, x, T{9}, x, x, T{9}};

  const cublas_handle blas;
  const cuda_matrix<T> a_wide(
      matrix_view<T>(a_storage, {.row_count = 2, .col_count = 4}));
  const cuda_matrix<T> b_wide(
      matrix_view<T>(b_storage, {.row_count = 3, .col_count = 3}));
  cuda_matrix<T> out_wide(
      matrix_view<T>(out_start, {.row_count = 2, .col_count = 3}));
  const auto a =
      a_wide[{row_ndx{0}, col_ndx{0}}, {.row_count = 2, .col_count = 3}];
  const auto b =
      b_wide[{row_ndx{0}, col_ndx{0}}, {.row_count = 3, .col_count = 2}];
  const auto out =
      out_wide[{row_ndx{0}, col_ndx{0}}, {.row_count = 2, .col_count = 2}];
  CHECK(a.stride() == 4);
  CHECK(!a.is_packed());
  CHECK(out_wide.as_lens().is_packed());

  std::vector<T> wide_storage(out_wide.size());
  const matrix_lens<T> wide_lens(wide_storage, out_wide.extent());

  // The product lands in the window, and the column beside it keeps its 9s.
  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b));
  REQUIRE(out_wide.as_view().store(wide_lens));
  CHECK(wide_storage == std::vector<T>{T{4}, T{5}, T{9}, T{10}, T{11}, T{9}});

  // A bias row broadcast into the strided window.
  constexpr std::array bias_storage{T{10}, T{20}};
  cuda_buffer<T> bias(bias_storage.size());
  REQUIRE(bias.load(bias_storage));
  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b, bias));
  REQUIRE(out_wide.as_view().store(wide_lens));
  CHECK(
      wide_storage == std::vector<T>{T{14}, T{25}, T{9}, T{20}, T{31}, T{9}});

  // The window stores and loads on its own, through pitched copies.
  std::vector<T> window_storage(out.size());
  const matrix_lens<T> window_lens(window_storage, out.extent());
  REQUIRE(out.store(window_lens));
  CHECK(window_storage == std::vector<T>{T{14}, T{25}, T{20}, T{31}});
  const std::vector<T> ones_storage(out.size(), T{1});
  REQUIRE(out.load(matrix_view<T>(ones_storage, out.extent())));
  REQUIRE(out_wide.as_view().store(wide_lens));
  CHECK(wide_storage == std::vector<T>{T{1}, T{1}, T{9}, T{1}, T{1}, T{9}});

  // A strided addend, copied device to device: columns 1 and 2 of `a_wide`.
  const auto addend = a_wide[{row_ndx{0}, col_ndx{1}}, out.extent()];
  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b, {}, addend));
  REQUIRE(out_wide.as_view().store(wide_lens));
  CHECK(wide_storage == std::vector<T>{T{6}, T{8}, T{9}, T{15}, T{17}, T{9}});
}

TEMPLATE_TEST_CASE("Device batched gemm over both axes",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  const cublas_handle blas;

  // Two instances. Instance 0 is column 0 of `a` and of `b`, instance 1 is
  // column 1, and each product is a column times a transposed column:
  // [1, 3]^T * [5, 7] and [2, 4]^T * [6, 8], stacked as the rows of `outer`.
  const std::vector<T> a_storage{T{1}, T{2}, T{3}, T{4}};
  const std::vector<T> b_storage{T{5}, T{6}, T{7}, T{8}};
  const cuda_matrix<T> a(
      matrix_view<T>(a_storage, {.row_count = 2, .col_count = 2}));
  const cuda_matrix<T> b(
      matrix_view<T>(b_storage, {.row_count = 2, .col_count = 2}));
  cuda_matrix<T> outer({.row_count = 4, .col_count = 2});
  REQUIRE(corvid::cuda::linalg::gemm_batched(blas, outer, a, b,
      {.count = 2,
          .a = matrix_axis::cols,
          .b = matrix_axis::cols,
          .out = matrix_axis::rows},
      {.op_b = cublas_operation::transpose}));

  std::vector<T> outer_storage(outer.size());
  REQUIRE(
      outer.as_view().store(matrix_lens<T>(outer_storage, outer.extent())));
  CHECK(outer_storage ==
        std::vector<T>{T{5}, T{7}, T{15}, T{21}, T{12}, T{16}, T{24}, T{32}});

  // Each stacked square times its column of `a`, [[5, 7], [15, 21]] * [1, 3]^T
  // and [[12, 16], [24, 32]] * [2, 4]^T, writing interleaved columns of `out`.
  cuda_matrix<T> out({.row_count = 2, .col_count = 2});
  REQUIRE(corvid::cuda::linalg::gemm_batched(blas, out, outer, a,
      {.count = 2,
          .a = matrix_axis::rows,
          .b = matrix_axis::cols,
          .out = matrix_axis::cols}));

  std::vector<T> out_storage(out.size());
  REQUIRE(out.as_view().store(matrix_lens<T>(out_storage, out.extent())));
  CHECK(out_storage == std::vector<T>{T{26}, T{88}, T{78}, T{176}});
}

TEST_CASE("Device gemm over bfloat16_t accumulates in float",
    "[LinearAlgebraTest][cuda]") {
  // A two-by-three times three-by-two product whose values and partial sums
  // are exact in float, so the bfloat16 result is the float product rounded
  // once, at the store. Three of the four products need more than eight
  // significant bits, so the rounding is visible.
  const std::vector<float> a_values{1.5F, 2.25F, 3.125F, 4.0625F, 5.5F, 6.75F};
  const std::vector<float> b_values{1.25F, 0.5F, 0.75F, 1.125F, 1.0F, 2.5F};
  constexpr matrix_types::matrix_extent a_extent{.row_count = 2,
      .col_count = 3};
  constexpr matrix_types::matrix_extent b_extent{.row_count = 3,
      .col_count = 2};
  constexpr matrix_types::matrix_extent out_extent{.row_count = 2,
      .col_count = 2};

  const cublas_handle blas;
  const cuda_matrix<float> a(matrix_view<float>(a_values, a_extent));
  const cuda_matrix<float> b(matrix_view<float>(b_values, b_extent));
  cuda_matrix<float> out(out_extent);
  REQUIRE(corvid::cuda::linalg::gemm(blas, out, a, b));
  std::vector<float> out_values(out.size());
  REQUIRE(out.as_view().store(matrix_lens<float>(out_values, out_extent)));
  CHECK(out_values ==
        std::vector<float>{6.6875F, 11.09375F, 15.953125F, 25.09375F});

  const auto a_narrowed = narrowed(a_values);
  const auto b_narrowed = narrowed(b_values);
  const cuda_matrix<bfloat16_t> a_bf16(
      matrix_view<bfloat16_t>(a_narrowed, a_extent));
  const cuda_matrix<bfloat16_t> b_bf16(
      matrix_view<bfloat16_t>(b_narrowed, b_extent));
  cuda_matrix<bfloat16_t> out_bf16(out_extent);
  REQUIRE(corvid::cuda::linalg::gemm(blas, out_bf16, a_bf16, b_bf16));
  std::vector<bfloat16_t> out_narrowed(out_bf16.size());
  REQUIRE(out_bf16.as_view().store(
      matrix_lens<bfloat16_t>(out_narrowed, out_extent)));
  CHECK(out_narrowed == narrowed(out_values));

  // Scaled and accumulated onto the float product, still rounding only at
  // the store: 2 * product + product, in float, then narrowed.
  REQUIRE(out_bf16.as_lens().load(
      matrix_view<bfloat16_t>(out_narrowed, out_extent)));
  REQUIRE(corvid::cuda::linalg::gemm(blas, out_bf16, a_bf16, b_bf16,
      {.scale = 2}, out_bf16));
  REQUIRE(out_bf16.as_view().store(
      matrix_lens<bfloat16_t>(out_narrowed, out_extent)));
  std::vector<float> tripled(out_values.size());
  for (const auto [value, sum] : std::views::zip(out_values, tripled))
    sum = 3 * value;
  CHECK(out_narrowed == narrowed(tripled));
}

TEST_CASE("Device batched gemm over bfloat16_t", "[LinearAlgebraTest][cuda]") {
  // The float and double case's first product, whose values are all exact in
  // eight significant bits.
  const cublas_handle blas;
  const auto a_narrowed = narrowed({1.0F, 2.0F, 3.0F, 4.0F});
  const auto b_narrowed = narrowed({5.0F, 6.0F, 7.0F, 8.0F});
  const cuda_matrix<bfloat16_t> a(
      matrix_view<bfloat16_t>(a_narrowed, {.row_count = 2, .col_count = 2}));
  const cuda_matrix<bfloat16_t> b(
      matrix_view<bfloat16_t>(b_narrowed, {.row_count = 2, .col_count = 2}));
  cuda_matrix<bfloat16_t> outer({.row_count = 4, .col_count = 2});
  REQUIRE(corvid::cuda::linalg::gemm_batched(blas, outer, a, b,
      {.count = 2,
          .a = matrix_axis::cols,
          .b = matrix_axis::cols,
          .out = matrix_axis::rows},
      {.op_b = cublas_operation::transpose}));

  std::vector<bfloat16_t> outer_narrowed(outer.size());
  REQUIRE(outer.as_view().store(
      matrix_lens<bfloat16_t>(outer_narrowed, outer.extent())));
  CHECK(outer_narrowed ==
        narrowed({5.0F, 7.0F, 15.0F, 21.0F, 12.0F, 16.0F, 24.0F, 32.0F}));
}

#pragma endregion
#pragma region add

TEMPLATE_TEST_CASE("Device add on hand-computed rows",
    "[LinearAlgebraTest][cuda]", float, double, bfloat16_t) {
  using T = TestType;
  using lens_t = cuda_matrix_lens<T>;
  using row_ndx = lens_t::row_ndx;
  using col_ndx = lens_t::col_ndx;
  using extent_t = lens_t::extent_t;
  const std::vector<T> a_storage{T{1}, T{2}, T{3}, T{4}};
  const std::vector<T> b_storage{T{10}, T{20}, T{30}, T{40}};
  const matrix_view<T> a_view(a_storage, {.row_count = 2, .col_count = 2});
  const matrix_view<T> b_view(b_storage, {.row_count = 2, .col_count = 2});
  const std::vector<T> expected{T{11}, T{22}, T{33}, T{44}};
  cuda_matrix<T> a(a_view);
  cuda_matrix<T> b(b_view);
  std::vector<T> storage(a.size());
  const matrix_lens<T> out_lens(storage, a.extent());

  SECTION("into a separate matrix") {
    cuda_matrix<T> out(a.extent());
    REQUIRE(corvid::cuda::linalg::add(out, a, b));
    REQUIRE(out.as_view().store(out_lens));
    CHECK(storage == expected);
  }

  SECTION("in place on the left") {
    REQUIRE(corvid::cuda::linalg::add(a, a, b));
    REQUIRE(a.as_view().store(out_lens));
    CHECK(storage == expected);
  }

  SECTION("in place on the right") {
    REQUIRE(corvid::cuda::linalg::add(b, a, b));
    REQUIRE(b.as_view().store(out_lens));
    CHECK(storage == expected);
  }

  SECTION("over strided windows") {
    // The operands are the leading two columns of three-wide matrices, and
    // `out` the leading columns of one whose last column must survive
    // untouched. `x` marks filler.
    const auto x = T{-1};
    const std::vector<T> a_wide_storage{T{1}, T{2}, x, T{3}, T{4}, x};
    const std::vector<T> b_wide_storage{T{10}, T{20}, x, T{30}, T{40}, x};
    const std::vector<T> out_start{x, x, T{9}, x, x, T{9}};
    const cuda_matrix<T> a_wide(
        matrix_view<T>(a_wide_storage, {.row_count = 2, .col_count = 3}));
    const cuda_matrix<T> b_wide(
        matrix_view<T>(b_wide_storage, {.row_count = 2, .col_count = 3}));
    cuda_matrix<T> out_wide(
        matrix_view<T>(out_start, {.row_count = 2, .col_count = 3}));
    const extent_t window{.row_count = 2, .col_count = 2};
    const auto out = out_wide[{row_ndx{0}, col_ndx{0}}, window];
    CHECK(!out.is_packed());

    REQUIRE(corvid::cuda::linalg::add(out,
        a_wide[{row_ndx{0}, col_ndx{0}}, window],
        b_wide[{row_ndx{0}, col_ndx{0}}, window]));

    std::vector<T> wide_storage(out_wide.size());
    REQUIRE(out_wide.as_view().store(
        matrix_lens<T>(wide_storage, out_wide.extent())));
    const std::vector<T> wide_expected{T{11}, T{22}, T{9}, T{33}, T{44}, T{9}};
    CHECK(wide_storage == wide_expected);
  }
}

#pragma endregion
#pragma region subtract

TEMPLATE_TEST_CASE("Device subtract on hand-computed rows",
    "[LinearAlgebraTest][cuda]", float, double, bfloat16_t) {
  using T = TestType;
  const std::vector<T> a_storage{T{11}, T{22}, T{33}, T{44}};
  const std::vector<T> b_storage{T{1}, T{2}, T{3}, T{4}};
  const matrix_view<T> a_view(a_storage, {.row_count = 2, .col_count = 2});
  const matrix_view<T> b_view(b_storage, {.row_count = 2, .col_count = 2});
  const std::vector<T> expected{T{10}, T{20}, T{30}, T{40}};
  cuda_matrix<T> a(a_view);
  cuda_matrix<T> b(b_view);
  std::vector<T> storage(a.size());
  const matrix_lens<T> out_lens(storage, a.extent());

  SECTION("into a separate matrix") {
    cuda_matrix<T> out(a.extent());
    REQUIRE(corvid::cuda::linalg::subtract(out, a, b));
    REQUIRE(out.as_view().store(out_lens));
    CHECK(storage == expected);
  }

  SECTION("in place on the left") {
    REQUIRE(corvid::cuda::linalg::subtract(a, a, b));
    REQUIRE(a.as_view().store(out_lens));
    CHECK(storage == expected);
  }

  SECTION("in place on the right") {
    REQUIRE(corvid::cuda::linalg::subtract(b, a, b));
    REQUIRE(b.as_view().store(out_lens));
    CHECK(storage == expected);
  }
}

#pragma endregion
#pragma region softmax

TEMPLATE_TEST_CASE("Device softmax on hand-computed rows",
    "[LinearAlgebraTest][cuda]", float, double) {
  using T = TestType;
  using lens_t = cuda_matrix_lens<T>;
  using row_ndx = lens_t::row_ndx;
  using col_ndx = lens_t::col_ndx;
  using extent_t = lens_t::extent_t;
  constexpr auto tolerance = 1e-5;

  // Row 0 is the CPU test's pair, where the second score gets twice the
  // weight. Row 1 has equal scores whose raw exponential would overflow a
  // float. Row 2 is a gap of 8, so the smaller weight is exp(-8) over
  // 1 + exp(-8).
  const std::vector<T> in_storage{T{0}, static_cast<T>(0.707), T{1000},
      T{1000}, T{-3}, T{5}};
  const matrix_view<T> in_view(in_storage, {.row_count = 3, .col_count = 2});
  const std::vector<T> expected{static_cast<T>(0.330262),
      static_cast<T>(0.669738), T{0.5}, T{0.5}, static_cast<T>(0.000335350),
      static_cast<T>(0.999664650)};
  const cuda_matrix<T> in(in_view);

  const auto check_rows = [&](const std::vector<T>& storage) {
    for (const auto i : iota(expected.size())) {
      CAPTURE(i);
      CHECK_THAT(storage[i], WithinAbs(expected[i], tolerance));
    }
  };

  SECTION("into a separate matrix") {
    cuda_matrix<T> out(in.extent());
    REQUIRE(corvid::cuda::linalg::softmax(out, in));
    std::vector<T> storage(out.size());
    REQUIRE(out.as_view().store(matrix_lens<T>(storage, out.extent())));
    check_rows(storage);
  }

  SECTION("in place") {
    cuda_matrix<T> same(in_view);
    REQUIRE(corvid::cuda::linalg::softmax(same, same));
    std::vector<T> storage(same.size());
    REQUIRE(same.as_view().store(matrix_lens<T>(storage, same.extent())));
    check_rows(storage);
  }

  SECTION("over strided windows") {
    // The rows are the leading two columns of a three-wide matrix whose last
    // column must survive untouched. `x` marks filler.
    constexpr auto x = T{-1};
    const std::vector<T> wide_storage{T{0}, static_cast<T>(0.707), x, T{1000},
        T{1000}, x, T{-3}, T{5}, x};
    cuda_matrix<T> wide(
        matrix_view<T>(wide_storage, {.row_count = 3, .col_count = 3}));
    const extent_t window{.row_count = 3, .col_count = 2};
    const auto out = wide[{row_ndx{0}, col_ndx{0}}, window];
    CHECK(!out.is_packed());
    REQUIRE(corvid::cuda::linalg::softmax(out, out));

    std::vector<T> storage(wide.size());
    REQUIRE(wide.as_view().store(matrix_lens<T>(storage, wide.extent())));
    for (const auto r : iota(3)) {
      CAPTURE(r);
      CHECK_THAT(storage[(r * 3)], WithinAbs(expected[r * 2], tolerance));
      CHECK_THAT(storage[(r * 3) + 1],
          WithinAbs(expected[(r * 2) + 1], tolerance));
      CHECK(storage[(r * 3) + 2] == x);
    }
  }
}

TEST_CASE("Device softmax over a row wider than a block",
    "[LinearAlgebraTest][cuda]") {
  // With 1000 columns and 256 threads, each thread folds four columns before
  // the block reductions. The scores ramp by 0.01, so neighboring weights
  // differ by a factor of exp(0.01), and the row sums to 1.
  constexpr auto cols = 1000UZ;
  std::vector<float> in_storage(cols);
  for (const auto [c, score] : std::views::enumerate(in_storage))
    score = static_cast<float>(c) * 0.01F;
  const cuda_matrix<float> in(
      float_matrix_view(in_storage, {.row_count = 1, .col_count = cols}));
  cuda_matrix<float> out(in.extent());
  REQUIRE(corvid::cuda::linalg::softmax(out, in));

  std::vector<float> weights(cols);
  REQUIRE(out.as_view().store(float_matrix_lens(weights, out.extent())));
  auto total = 0.0;
  for (const auto weight : weights) total += weight;
  CHECK_THAT(total, WithinAbs(1.0, 1e-5));
  const auto ratio = std::exp(0.01);
  for (const auto c : {0UZ, 255UZ, 256UZ, 998UZ}) {
    CAPTURE(c);
    CHECK_THAT(weights[c + 1] / weights[c], WithinAbs(ratio, 1e-4));
  }
}

TEST_CASE("Device softmax over bfloat16_t is the float softmax narrowed",
    "[LinearAlgebraTest][cuda]") {
  // The hand-computed rows, narrowed first so that both kernels widen to
  // the same floats. The bf16 result is then the float result rounded once,
  // at the store.
  const auto in_narrowed =
      narrowed({0.0F, 0.707F, 1000.0F, 1000.0F, -3.0F, 5.0F});
  constexpr matrix_types::matrix_extent extent{.row_count = 3, .col_count = 2};

  const auto in_widened = widened(in_narrowed);
  const cuda_matrix<float> in(matrix_view<float>(in_widened, extent));
  cuda_matrix<float> out(extent);
  REQUIRE(corvid::cuda::linalg::softmax(out, in));
  std::vector<float> out_values(out.size());
  REQUIRE(out.as_view().store(matrix_lens<float>(out_values, extent)));

  const cuda_matrix<bfloat16_t> in_bf16(
      matrix_view<bfloat16_t>(in_narrowed, extent));
  cuda_matrix<bfloat16_t> out_bf16(extent);
  REQUIRE(corvid::cuda::linalg::softmax(out_bf16, in_bf16));
  std::vector<bfloat16_t> out_narrowed(out_bf16.size());
  REQUIRE(
      out_bf16.as_view().store(matrix_lens<bfloat16_t>(out_narrowed, extent)));
  CHECK(out_narrowed == narrowed(out_values));
}

#pragma endregion
#pragma region convert

TEST_CASE("Device convert narrows and widens between element types",
    "[LinearAlgebraTest][cuda]") {
  // Values that need more than eight significant bits narrow to bfloat16 as
  // the host narrows them, and widen back to what they narrowed to. A float
  // widens to double exactly.
  const std::vector<float> values{1.0F, 1.00390625F, 3.14159F, -0.1F, 65504.0F,
      1e-3F};
  constexpr matrix_types::matrix_extent extent{.row_count = 2, .col_count = 3};
  const cuda_matrix<float> in(matrix_view<float>(values, extent));

  cuda_matrix<bfloat16_t> as_bf16(extent);
  REQUIRE(corvid::cuda::linalg::convert(as_bf16, in));
  std::vector<bfloat16_t> bf16_values(as_bf16.size());
  REQUIRE(
      as_bf16.as_view().store(matrix_lens<bfloat16_t>(bf16_values, extent)));
  CHECK(bf16_values == narrowed(values));

  cuda_matrix<float> round_trip(extent);
  REQUIRE(corvid::cuda::linalg::convert(round_trip, as_bf16));
  std::vector<float> round_trip_values(round_trip.size());
  REQUIRE(round_trip.as_view().store(
      matrix_lens<float>(round_trip_values, extent)));
  CHECK(round_trip_values == widened(bf16_values));

  cuda_matrix<double> as_double(extent);
  REQUIRE(corvid::cuda::linalg::convert(as_double, in));
  std::vector<double> double_values(as_double.size());
  REQUIRE(
      as_double.as_view().store(matrix_lens<double>(double_values, extent)));
  CHECK(double_values == std::vector<double>(values.begin(), values.end()));
}

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
