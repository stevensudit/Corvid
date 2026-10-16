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
#include <filesystem>
#include <string_view>
#include <vector>

#include "catch2_torch.h"
#include "corvid/containers/utils/matrix_view.h"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/cuda_matrix.cuh"
#include "corvid/cuda/linalg/linear_algebra.cuh"
#include "corvid/llm/safetensors.h"
#include "corvid/torch/tensor_adapter.cuh"
#include "gpt2_oracle.h"

// The LibTorch build spike is one translation unit that links our CUDA stack
// and LibTorch together, views our safetensors reader's weights and our device
// buffers as tensors, runs a matmul through torch, and matches our own GEMM.
// It proves the build (clang-CUDA against the torch headers, the hand-wired
// link line, the shared libraries found at run time) and the runtime (torch's
// bundled cudart beside our statically linked one, in one process, sharing
// device memory both ways) before any model code depends on either. See the
// stage 6 build findings in corvid/llm/roadmap.md.
namespace corvid_tests {

using namespace corvid;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_matrix;
using corvid::llm::safetensors_file;
using corvid::matrix_types::matrix_extent;
using corvid::tests::gpt2::check_close;
using corvid::tests::gpt2::oracle_path;
using namespace corvid::torch;

namespace {

// The fp32 tensor `name` of `file`, which must be two-dimensional, as a
// matrix view.
float_matrix_view
matrix_of(const safetensors_file& file, std::string_view name) {
  INFO(name);
  const auto view = file.find_matrix<float>(name);
  REQUIRE(!view.empty());
  return view;
}

// The two operands and the product our GEMM computes from them.
//
// The operands are the positional embeddings [positions, width] against the
// first block's QKV projection [width, 3 * width], two real weight matrices
// whose inner extents agree.
struct gemm_case {
  safetensors_file weights;
  float_matrix_view a;
  float_matrix_view b;
  matrix_extent out_extent;
  std::vector<float> ours_storage;

  // Load the operands, skipping the test when the model is not here.
  [[nodiscard]] static gemm_case load() {
    const auto model_path = oracle_path("model.safetensors");
    if (!std::filesystem::exists(model_path))
      SKIP("no model under tests/.local/llm/gpt2; run the oracle");
    // The views point into the mapping, whose address the move preserves.
    gemm_case result{
        .weights = safetensors_file::load(tests::open_read_only(model_path))};
    result.a = matrix_of(result.weights, "wpe.weight");
    result.b = matrix_of(result.weights, "h.0.attn.c_attn.weight");
    REQUIRE(result.a.col_extent() == result.b.row_extent());
    result.out_extent = {.row_count = result.a.row_extent(),
        .col_count = result.b.col_extent()};
    return result;
  }

  // Our product, through cuBLAS, downloaded.
  [[nodiscard]] float_matrix_view ours(const cublas_handle& blas,
      const cuda_matrix<float>& d_a, const cuda_matrix<float>& d_b) {
    cuda_matrix<float> d_out(out_extent);
    REQUIRE(corvid::cuda::linalg::gemm(blas, d_out, d_a, d_b));
    ours_storage.resize(d_out.size());
    REQUIRE(
        d_out.as_view().store(float_matrix_lens(ours_storage, out_extent)));
    return {ours_storage, out_extent};
  }
};

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("torch matmul over our host memory matches our GEMM", "[torch]") {
  auto test = gemm_case::load();
  const cublas_handle blas;
  REQUIRE(blas);
  const auto ours =
      test.ours(blas, cuda_matrix<float>(test.a), cuda_matrix<float>(test.b));

  // The torch side views the mapped weights as tensors, moves them to the
  // device, multiplies, and brings the product back.
  REQUIRE(at::hasCUDA());
  const auto t_out =
      at::matmul(as_tensor(test.a).to(DeviceType::CUDA),
          as_tensor(test.b).to(DeviceType::CUDA))
          .to(DeviceType::CPU);
  const auto theirs = as_host_view<float>(t_out);
  REQUIRE(theirs.extent() == test.out_extent);

  // Both are fp32 products over the same operands, so only the summation
  // order differs.
  check_close(ours, theirs, 1e-4F, 1e-4F);
}

TEST_CASE("torch matmul over our device memory matches our GEMM", "[torch]") {
  auto test = gemm_case::load();
  const cublas_handle blas;
  REQUIRE(blas);
  const cuda_matrix<float> d_a(test.a);
  const cuda_matrix<float> d_b(test.b);
  const auto ours = test.ours(blas, d_a, d_b);

  // The torch side views our device buffers as tensors where they sit,
  // multiplies on the device, and hands the product back as one of our views
  // for our own download. Our buffers come from the statically linked cudart
  // and torch's from its bundled one, so this is the two runtimes sharing one
  // device address space.
  REQUIRE(at::hasCUDA());
  const auto t_out =
      at::matmul(as_tensor(d_a.as_view()), as_tensor(d_b.as_view()));
  const auto theirs_device = as_device_view<float>(t_out);
  REQUIRE(theirs_device.extent() == test.out_extent);
  std::vector<float> theirs_storage(theirs_device.size());
  REQUIRE(
      theirs_device.store(float_matrix_lens(theirs_storage, test.out_extent)));
  const float_matrix_view theirs(theirs_storage, test.out_extent);

  check_close(ours, theirs, 1e-4F, 1e-4F);
}

// NOLINTEND(readability-function-cognitive-complexity)

} // namespace corvid_tests
