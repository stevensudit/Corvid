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
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include "catch2_torch.h"
#include <catch2/catch_approx.hpp>
#include "corvid/containers/utils/interval.h"
#include "corvid/containers/utils/matrix_view.h"
#include "corvid/cuda/cuda_matrix.cuh"
#include "corvid/math/bfloat16.h"
#include "corvid/torch/cuda_tensor_adapter.cuh"
#include "corvid/torch/llm/llm_ops.h"
#include "corvid/torch/llm/qwen2_engine.h"
#include "corvid/torch/llm/qwen2_tensors.h"
#include "gpt2_oracle.h"
#include "qwen2_oracle.h"

namespace corvid_tests {

using namespace corvid;
using namespace corvid::tests::qwen2;
using namespace corvid::torch;
using namespace corvid::torch::llm;
using namespace corvid::cuda;
using namespace corvid::tests::gpt2;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// Whether `tensor` is a bf16 CUDA tensor of `sizes`.
bool is_device_bf16(const at::Tensor& tensor, at::IntArrayRef sizes) {
  return tensor.defined() && tensor.device().is_cuda() &&
         (tensor.scalar_type() == ScalarType::BFloat16) &&
         (tensor.sizes() == sizes);
}

// The oracle's logits, the model over its dumps, and its parameters uploaded
// once for the whole run, so that every case starts from the same bf16
// tensors.
struct shared_upload {
  safetensors_file logits;
  qwen2_model<bfloat16_t> model;
  qwen2_tensors tensors;

  shared_upload()
      : logits(load_logits()), model(load_model()),
        tensors(qwen2_tensors::upload(model)) {}
};

// The shared upload, skipping the test when the dumps are not here.
const shared_upload& uploaded() {
  static const shared_upload shared;
  return shared;
}

// fp32 tensors on the device, for the op tests against torch's own functions.
TensorOptions device_float() {
  return TensorOptions{}.dtype(ScalarType::Float).device(DeviceType::CUDA);
}

// The ratio of the bf16 engine's largest logit error to the largest expected
// logit, set above the largest measured over the five prompts, 2.0e-1 on
// prompt 0 (3.43 against 16.98) and 1.9e-1 on prompt 4 (3.98 against 21.43),
// with the residual stream in bf16 as the published model runs it. The
// reference is the same model run in bf16 by transformers, which loses 2.0e-1
// and 1.7e-1 on those prompts ("qwen2_bf16_reference.py").
constexpr auto bf16_logits_ratio = 2.5e-1F;

// The tolerance of the engine's fp32 logits against the oracle's, which are
// fp32 from the same bf16 parameters widened, so only the summation order
// differs.
constexpr auto fp32_logits_tolerance = 1e-3F;

// The same ratio with the residual stream in fp32, set above the largest
// measured, 5.8e-2 on prompt 0 (0.99 against 16.98) and 4.6e-2 on prompt 4
// (0.98 against 21.43).
constexpr auto fp32_residual_logits_ratio = 8e-2F;

// `tensors` widened to fp32, sharing nothing with it.
qwen2_tensors widened(const qwen2_tensors& tensors) {
  const auto wide = [](const Tensor& t) { return t.to(ScalarType::Float); };
  const auto tied = tensors.lm_head.is_same(tensors.embed_tokens);
  qwen2_tensors result{
      .embed_tokens = wide(tensors.embed_tokens),
      .norm_weight = wide(tensors.norm_weight),
  };
  result.blocks.reserve(tensors.blocks.size());
  for (const auto& block : tensors.blocks)
    result.blocks.push_back({
        .input_layernorm_weight = wide(block.input_layernorm_weight),
        .q_proj_weight = wide(block.q_proj_weight),
        .q_proj_bias = wide(block.q_proj_bias),
        .k_proj_weight = wide(block.k_proj_weight),
        .k_proj_bias = wide(block.k_proj_bias),
        .v_proj_weight = wide(block.v_proj_weight),
        .v_proj_bias = wide(block.v_proj_bias),
        .o_proj_weight = wide(block.o_proj_weight),
        .post_attention_layernorm_weight =
            wide(block.post_attention_layernorm_weight),
        .gate_proj_weight = wide(block.gate_proj_weight),
        .up_proj_weight = wide(block.up_proj_weight),
        .down_proj_weight = wide(block.down_proj_weight),
    });
  result.lm_head = tied ? result.embed_tokens : wide(tensors.lm_head);
  return result;
}

// Check `engine`'s logits against the oracle's on every prompt, with
// `check(actual, expected)` the comparison.
void check_forward(const qwen2_engine& engine,
    const safetensors_file& logits_file, auto check) {
  const auto vocab_size = engine.config().vocab_size;
  for (const auto n : iota(prompt_count)) {
    INFO("prompt_" << n);
    const auto prefix = std::format("prompt_{}", n);
    const auto ids = ids_of(logits_file, prefix + "/input_ids");
    const auto expected =
        matrix_of(logits_file, prefix + "/logits", vocab_size);
    REQUIRE(expected.row_extent() == ids.size());

    // The engine's logits are on the device in the parameters' type, so they
    // widen and come down before the comparison.
    const auto logits =
        engine.forward(ids).to(ScalarType::Float).to(DeviceType::CPU);
    const auto actual = as_host_view<float>(logits);
    REQUIRE(actual.extent() == expected.extent());
    check(actual, expected);
  }
}

// The comparison at `tolerance` both absolute and relative.
auto close_within(float tolerance) {
  return [tolerance](float_matrix_view actual, float_matrix_view expected) {
    check_close(actual, expected, tolerance, tolerance);
  };
}

// The comparison of the largest error against `ratio` of the largest
// expected magnitude.
auto close_to_scale(float ratio) {
  return [ratio](float_matrix_view actual, float_matrix_view expected) {
    check_close_to_scale(actual, expected, ratio);
  };
}

} // namespace

#pragma region Upload

TEST_CASE("Qwen2 parameters upload as bf16 CUDA tensors", "[torch][oracle]") {
  const auto& [logits, model, tensors] = uploaded();
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
#pragma region Ops

TEST_CASE("attend matches torch's scaled dot product attention", "[torch]") {
  REQUIRE(at::hasCUDA());
  at::manual_seed(0);
  // Four query heads share each of two key-value heads.
  const auto q = at::randn({8, 7, 16}, device_float());
  const auto k = at::randn({2, 7, 16}, device_float());
  const auto v = at::randn({2, 7, 16}, device_float());

  const auto ours = attend(q, k, v);
  // Torch's takes a batch dimension in front and repeats the key-value heads
  // itself when told the heads are grouped.
  const auto theirs =
      at::scaled_dot_product_attention(q.unsqueeze(0), k.unsqueeze(0),
          v.unsqueeze(0), {}, 0.0, /*is_causal=*/true, std::nullopt,
          /*enable_gqa=*/true)
          .squeeze(0);
  REQUIRE(ours.sizes() == theirs.sizes());
  CHECK(at::allclose(ours, theirs, 1e-5, 1e-5));
}

TEST_CASE("apply_rope turns by position and keeps relative dot products",
    "[torch]") {
  REQUIRE(at::hasCUDA());
  at::manual_seed(0);
  constexpr auto head_width = 16;
  constexpr auto position_count = 12;
  const auto tables =
      rope_tables::create(position_count, head_width, 10000.0, device_float());
  REQUIRE(tables.cos.sizes() == at::IntArrayRef({position_count, head_width}));

  // One query and one key, each copied to every position, so row `t` of the
  // result is that vector turned as if it were at position `t`.
  const auto query = at::randn({1, 1, head_width}, device_float());
  const auto key = at::randn({1, 1, head_width}, device_float());
  const auto turned_query =
      apply_rope(query.expand({1, position_count, head_width}), tables);
  const auto turned_key =
      apply_rope(key.expand({1, position_count, head_width}), tables);

  // Position 0 turns by nothing.
  CHECK(at::allclose(turned_query[0][0], query[0][0], 1e-6, 1e-6));

  // A query at position 2 against a key at position 5 scores the same as the
  // pair moved to positions 6 and 9, since only the distance of 3 matters.
  const auto score = [&](int64_t query_at, int64_t key_at) {
    return at::dot(turned_query[0][query_at], turned_key[0][key_at])
        .item<float>();
  };
  CHECK(score(2, 5) == Catch::Approx(score(6, 9)).margin(1e-4));
  CHECK(score(5, 2) == Catch::Approx(score(9, 6)).margin(1e-4));
  // The direction matters, so swapping the two is a different score unless
  // the vectors happen to coincide.
  CHECK(score(2, 5) != Catch::Approx(score(5, 2)).margin(1e-4));
}

#pragma endregion
#pragma region Forward

TEST_CASE("Qwen2 forward in fp32 matches the oracle's logits on every prompt",
    "[torch][oracle]") {
  const auto& [logits, model, tensors] = uploaded();
  const qwen2_engine engine(model.config, widened(tensors));
  check_forward(engine, logits, close_within(fp32_logits_tolerance));
}

TEST_CASE("Qwen2 forward in bf16 matches the oracle's logits on every prompt",
    "[torch][oracle]") {
  const auto& [logits, model, tensors] = uploaded();
  const qwen2_engine engine(model.config, tensors);
  CHECK(engine.residual_type() == ScalarType::BFloat16);
  check_forward(engine, logits, close_to_scale(bf16_logits_ratio));
}

TEST_CASE(
    "Qwen2 forward in bf16 with an fp32 residual matches the oracle's "
    "logits on every prompt",
    "[torch][oracle]") {
  const auto& [logits, model, tensors] = uploaded();
  const qwen2_engine engine(model.config, tensors, ScalarType::Float);
  CHECK(engine.parameter_type() == ScalarType::BFloat16);
  check_forward(engine, logits, close_to_scale(fp32_residual_logits_ratio));
}

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
