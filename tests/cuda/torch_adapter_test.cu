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
#include <ranges>
#include <span>
#include <vector>

#include "catch2_torch.h"
#include "corvid/containers/utils/interval.h"
#include "corvid/containers/utils/matrix_view.h"
#include "corvid/cuda/cuda_matrix.cuh"
#include "corvid/math/bfloat16.h"
#include "corvid/torch/tensor_adapter.cuh"

namespace corvid_tests {

using namespace corvid;
using namespace corvid::matrix_types;
using namespace corvid::torch;
using corvid::cuda::cuda_matrix;

namespace {

constexpr matrix_extent extent{.row_count = 3, .col_count = 4};

// The values 0 through 11, as a 3 by 4 matrix.
std::vector<float> counting() {
  std::vector<float> values(extent.size());
  for (const auto ndx : iota(values.size()))
    values[ndx] = static_cast<float>(ndx);
  return values;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Host lens and tensor share memory", "[torch]") {
  auto values = counting();
  const float_matrix_lens lens(values, extent);

  const auto tensor = as_tensor(lens);
  REQUIRE(tensor.dim() == 2);
  CHECK(tensor.size(0) == 3);
  CHECK(tensor.size(1) == 4);
  CHECK(tensor.stride(0) == 4);
  CHECK(tensor.stride(1) == 1);
  CHECK(tensor.scalar_type() == at::kFloat);
  CHECK(tensor.is_cpu());
  CHECK(tensor[1][2].item<float>() == 6.0F);

  // A write through the tensor lands in the vector.
  tensor.add_(1.0F);
  CHECK(values[6] == 7.0F);

  // And the tensor converts back to the same lens.
  const auto back = as_host_lens<float>(tensor);
  REQUIRE(!back.empty());
  CHECK(back.as_span().data() == values.data());
  CHECK(back.extent() == extent);
  CHECK(back.stride() == 4);
  const auto back_view = as_host_view<float>(tensor);
  CHECK(back_view.as_span().data() == values.data());
}

TEST_CASE("Host span and one-dimensional tensor share memory", "[torch]") {
  auto values = counting();
  const auto tensor = as_tensor(std::span{values});
  REQUIRE(tensor.dim() == 1);
  CHECK(tensor.size(0) == 12);
  CHECK(tensor.scalar_type() == at::kFloat);
  CHECK(tensor.is_cpu());
  CHECK(tensor[6].item<float>() == 6.0F);

  // A write through the tensor lands in the vector, and a span of const
  // elements converts the same way.
  tensor.add_(1.0F);
  CHECK(values[6] == 7.0F);
  const auto from_const = as_tensor(std::span<const float>{values});
  CHECK(from_const[6].item<float>() == 7.0F);

  // An empty span is an empty tensor, not a null one.
  const auto empty = as_tensor(std::span<float>{});
  CHECK(empty.defined());
  CHECK(empty.numel() == 0);
}

TEST_CASE("A strided slice converts with its stride", "[torch]") {
  auto values = counting();
  const float_matrix_lens lens(values, extent);
  // Columns 1 and 2 of every row.
  const auto slice = lens[{.row = row_ndx{}, .col = col_ndx{1}},
      {.row_count = 3, .col_count = 2}];

  const auto tensor = as_tensor(slice);
  CHECK(tensor.size(0) == 3);
  CHECK(tensor.size(1) == 2);
  CHECK(tensor.stride(0) == 4);
  CHECK(tensor.stride(1) == 1);
  CHECK(tensor[2][0].item<float>() == 9.0F);

  const auto back = as_host_view<float>(tensor);
  REQUIRE(!back.empty());
  CHECK(back.stride() == 4);
  CHECK(back.extent() == slice.extent());
  CHECK(back.as_span().data() == slice.as_span().data());
}

TEST_CASE("A tensor that is not a matrix of T converts to empty", "[torch]") {
  auto values = counting();
  const auto tensor = as_tensor(float_matrix_lens(values, extent));

  CHECK(as_host_view<double>(tensor).empty());
  CHECK(as_host_view<float>(tensor.t()).empty());
  CHECK(as_host_view<float>(tensor.reshape({12})).empty());
  CHECK(as_device_view<float>(tensor).empty());
  CHECK(as_host_view<float>(at::Tensor{}).empty());

  // A single column may carry any inner stride, since no second element is
  // reached through it, and a single row's outer stride is likewise moot.
  const auto column = tensor.as_strided({3, 1}, {4, 7});
  const auto column_view = as_host_view<float>(column);
  REQUIRE(!column_view.empty());
  CHECK(column_view.extent() == matrix_extent{.row_count = 3, .col_count = 1});
  CHECK(column_view.stride() == 4);
  CHECK(column_view[{.row = row_ndx{2}, .col = col_ndx{}}] == 8.0F);
  const auto row = tensor.as_strided({1, 4}, {99, 1});
  const auto row_view = as_host_view<float>(row);
  REQUIRE(!row_view.empty());
  CHECK(row_view.extent() == matrix_extent{.row_count = 1, .col_count = 4});
  CHECK(row_view.stride() == 4);
}

TEST_CASE("Device lens and CUDA tensor share memory", "[torch]") {
  REQUIRE(at::hasCUDA());
  auto values = counting();
  cuda_matrix<float> device(float_matrix_view(values, extent));

  const auto tensor = as_tensor(device.as_lens());
  REQUIRE(tensor.is_cuda());
  CHECK(tensor.size(0) == 3);
  CHECK(tensor.size(1) == 4);
  CHECK(tensor.stride(0) == 4);

  // A write through the tensor lands in our buffer.
  tensor.add_(1.0F);
  REQUIRE(device.as_view().store(float_matrix_lens(values, extent)));
  CHECK(values[6] == 7.0F);

  const auto back = as_device_lens<float>(tensor);
  REQUIRE(!back.empty());
  CHECK(back.get() == device.get());
  CHECK(back.extent() == extent);
  CHECK(as_host_view<float>(tensor).empty());
}

TEST_CASE("A tensor torch allocates on the device converts to a view",
    "[torch]") {
  REQUIRE(at::hasCUDA());
  const auto tensor = at::ones({2, 3},
      at::TensorOptions{}.dtype(at::kFloat).device(at::kCUDA));

  const auto view = as_device_view<float>(tensor);
  REQUIRE(!view.empty());
  std::vector<float> values(view.size());
  REQUIRE(view.store(float_matrix_lens(values, view.extent())));
  CHECK(std::ranges::all_of(values, [](float v) { return v == 1.0F; }));
}

TEST_CASE("bfloat16 elements map to the bfloat16 scalar type", "[torch]") {
  std::vector<bfloat16_t> values{bfloat16_t{1.5F}, bfloat16_t{-2.0F}};
  const matrix_lens<bfloat16_t> lens(values, {.row_count = 1, .col_count = 2});

  const auto tensor = as_tensor(lens);
  CHECK(tensor.scalar_type() == scalar_type_v<bfloat16_t>);
  CHECK(tensor[0][0].item<float>() == 1.5F);
  CHECK(tensor[0][1].item<float>() == -2.0F);
  CHECK(!as_host_view<bfloat16_t>(tensor).empty());
}

// NOLINTEND(readability-function-cognitive-complexity)

} // namespace corvid_tests
