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
#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

#include "catch2_torch.h"
#include "corvid/containers/utils/matrix_view.h"
#include "corvid/cuda/cuda_matrix.cuh"
#include "corvid/math/bfloat16.h"
#include "corvid/torch/qwen2_tensors.cuh"
#include "corvid/torch/tensor_adapter.cuh"
#include "qwen2_oracle.h"

namespace corvid_tests {

using namespace corvid;
using namespace corvid::tests::qwen2;
using namespace corvid::torch;
using corvid::cuda::cuda_matrix_view;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// Whether `tensor` is a bf16 CUDA tensor of `sizes`.
bool is_device_bf16(const at::Tensor& tensor, at::IntArrayRef sizes) {
  return tensor.defined() && tensor.device().is_cuda() &&
         (tensor.scalar_type() == at::kBFloat16) && (tensor.sizes() == sizes);
}

} // namespace

#pragma region Upload

TEST_CASE("Qwen2 parameters upload as bf16 CUDA tensors", "[torch][oracle]") {
  const auto model = load_model();
  const auto tensors = qwen2_tensors::upload(model);
  const auto& config = model.config;
  const auto width = static_cast<int64_t>(config.hidden_size);
  const auto kv_width = static_cast<int64_t>(config.kv_width());
  const auto hidden_width = static_cast<int64_t>(config.intermediate_size);
  const auto vocab_size = static_cast<int64_t>(config.vocab_size);

  CHECK(is_device_bf16(tensors.embed_tokens, {vocab_size, width}));
  CHECK(is_device_bf16(tensors.norm_weight, {width}));
  CHECK(tensors.lm_head.is_same(tensors.embed_tokens));
  REQUIRE(tensors.blocks.size() == config.num_hidden_layers);
  for (const auto& [n, block] : std::views::enumerate(tensors.blocks)) {
    INFO("block_" << n);
    CHECK(is_device_bf16(block.input_layernorm_weight, {width}));
    CHECK(is_device_bf16(block.q_proj_weight, {width, width}));
    CHECK(is_device_bf16(block.q_proj_bias, {width}));
    CHECK(is_device_bf16(block.k_proj_weight, {kv_width, width}));
    CHECK(is_device_bf16(block.k_proj_bias, {kv_width}));
    CHECK(is_device_bf16(block.v_proj_weight, {kv_width, width}));
    CHECK(is_device_bf16(block.v_proj_bias, {kv_width}));
    CHECK(is_device_bf16(block.o_proj_weight, {width, width}));
    CHECK(is_device_bf16(block.post_attention_layernorm_weight, {width}));
    CHECK(is_device_bf16(block.gate_proj_weight, {hidden_width, width}));
    CHECK(is_device_bf16(block.up_proj_weight, {hidden_width, width}));
    CHECK(is_device_bf16(block.down_proj_weight, {width, hidden_width}));
  }

  // A tensor holds the bytes its view in the file holds, checked by bringing
  // one back down through the adapter.
  const auto host = model.blocks[0].k_proj_weight;
  const auto device =
      as_device_view<bfloat16_t>(tensors.blocks[0].k_proj_weight);
  REQUIRE(device.extent() == host.extent());
  std::vector<bfloat16_t> storage(device.size());
  REQUIRE(device.store(matrix_lens<bfloat16_t>(storage, device.extent())));
  CHECK(std::ranges::equal(std::as_bytes(std::span{storage}),
      std::as_bytes(host.as_span())));
}

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
