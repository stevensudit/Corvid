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
#include <filesystem>
#include <span>
#include <vector>

#include <ATen/ATen.h>
// c10's logging shim defines a glog-style CHECK, which Catch2's must replace.
#undef CHECK

#include "corvid/containers/utils/matrix_view.h"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/cuda_matrix.cuh"
#include "corvid/cuda/linalg/linear_algebra.cuh"
#include "corvid/llm/safetensors.h"
#include "catch2_main.h"
#include "gpt2_oracle.h"

// The LibTorch build spike: one translation unit that links our CUDA stack
// and LibTorch together, borrows a tensor from our safetensors reader, runs a
// matmul on the device through torch, and matches our own GEMM. It proves the
// build (clang-CUDA against the torch headers, the hand-wired link line, the
// shared libraries found at run time) and the runtime (torch's bundled cudart
// beside our statically linked one, in one process) before any model code
// depends on either. See the stage 6 build findings in corvid/llm/roadmap.md.
namespace corvid_tests {

using namespace corvid;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_matrix;
using corvid::llm::safetensors_file;
using corvid::matrix_types::matrix_extent;
using corvid::tests::gpt2::check_close;
using corvid::tests::gpt2::oracle_path;

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

// `host` borrowed as a torch tensor of the same shape, without a copy.
//
// `from_blob` takes a mutable pointer whatever the caller does with it; the
// tensor is only ever read here, so the cast removes a qualifier the API
// never honors in the first place.
at::Tensor borrow(float_matrix_view host) {
  const auto extent = host.extent();
  return at::from_blob(const_cast<float*>(host.as_span().data()),
      {static_cast<int64_t>(extent.row_count),
          static_cast<int64_t>(extent.col_count)},
      at::kFloat);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("torch device matmul matches our GEMM", "[torch]") {
  const auto model_path = oracle_path("model.safetensors");
  if (!std::filesystem::exists(model_path))
    SKIP("no model under tests/.local/llm/gpt2; run the oracle");
  const auto weights =
      safetensors_file::load(tests::open_read_only(model_path));
  // Two real weight matrices whose inner extents agree: the positional
  // embeddings [positions, width] against the first block's QKV projection
  // [width, 3 * width].
  const auto a = matrix_of(weights, "wpe.weight");
  const auto b = matrix_of(weights, "h.0.attn.c_attn.weight");
  REQUIRE(a.col_extent() == b.row_extent());
  const matrix_extent out_extent{.row_count = a.row_extent(),
      .col_count = b.col_extent()};

  // Ours: upload, GEMM through cuBLAS, download.
  const cublas_handle blas;
  REQUIRE(blas);
  const cuda_matrix<float> d_a(a);
  const cuda_matrix<float> d_b(b);
  cuda_matrix<float> d_out(out_extent);
  REQUIRE(corvid::cuda::linalg::gemm(blas, d_out, d_a, d_b));
  std::vector<float> ours_storage(d_out.size());
  REQUIRE(d_out.as_view().store(float_matrix_lens(ours_storage, out_extent)));
  const float_matrix_view ours(ours_storage, out_extent);

  // Torch: borrow the mapped weights, move them to the device, multiply, and
  // bring the product back.
  REQUIRE(at::hasCUDA());
  const auto t_out =
      at::matmul(borrow(a).to(at::kCUDA), borrow(b).to(at::kCUDA))
          .to(at::kCPU)
          .contiguous();
  REQUIRE(t_out.dim() == 2);
  REQUIRE(t_out.size(0) == static_cast<int64_t>(out_extent.row_count));
  REQUIRE(t_out.size(1) == static_cast<int64_t>(out_extent.col_count));
  const float_matrix_view theirs(
      std::span<const float>(t_out.const_data_ptr<float>(), d_out.size()),
      out_extent);

  // Both are fp32 products over the same operands; only the summation order
  // differs.
  check_close(ours, theirs, 1e-4F, 1e-4F);
}

// NOLINTEND(readability-function-cognitive-complexity)

} // namespace corvid_tests
