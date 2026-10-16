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
#pragma once
#include <cstdint>
#include <format>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <ATen/ATen.h>
#include <c10/util/Exception.h>

#include "../../llm/qwen2/qwen2.h"
#include "llm_ops.h"
#include "qwen2_tensors.h"

// The Qwen2 inference engine over tensors on the device.
//
// `qwen2_engine` runs the parameters `qwen2_tensors::upload` placed on the
// device, in their type, at three levels. `apply_attention` and `apply_mlp`
// run one block's two sublayers, `apply_block` runs a block over a residual,
// and `forward` embeds the token IDs and runs every block, the final norm, and
// the head over them. The ops it composes live in "llm_ops.h", and every
// level returns the tensor it computed, so any of them can be inspected.
//
//   const auto engine = qwen2_engine::upload(model);
//   const auto logits = engine.forward(ids); // [T, V]
//
// The residual stream runs in the parameters' type, as the published model
// does, unless the engine is built with a `residual_type` of its own. Keeping
// it in fp32 while the parameters are bf16 spends the rounding on the
// sublayers' inputs and outputs alone, so the residual carries less error
// across the blocks.
//
//   const auto engine = qwen2_engine::upload(model, ScalarType::Float);
//
// Every error torch raises on the way stays inside this band. Each public
// method rethrows it as `std::runtime_error`.
namespace corvid::torch::llm {

#pragma region qwen2_engine

// The Qwen2 inference engine over a `qwen2_tensors` it owns.
//
// Tokenizing text is `qwen2_tokenizer`'s job, so this takes token IDs alone.
class qwen2_engine {
public:
#pragma region Construction

  // Over `tensors`, which `config` describes, with the rotary tables built
  // for every position `config` allows, in the parameters' type, and the
  // residual stream kept in `residual_type`, which is the parameters' own
  // when `Undefined`.
  qwen2_engine(const qwen2_config& config, qwen2_tensors tensors,
      ScalarType residual_type = ScalarType::Undefined)
      : config_{config}, tensors_{std::move(tensors)},
        residual_type_{
            (residual_type == ScalarType::Undefined)
                ? tensors_.embed_tokens.scalar_type()
                : residual_type},
        rope_{translate_errors([&] {
          return rope_tables::create(
              static_cast<int64_t>(config.max_position_embeddings),
              static_cast<int64_t>(config.head_dim()), config.rope_theta,
              tensors_.embed_tokens.options());
        })} {}

  // Upload `model` to the device and build the engine over it, with the
  // residual stream kept in `residual_type`, or throw when the device
  // refuses, which is out of memory in practice.
  template<ParameterElement T>
  [[nodiscard]] static qwen2_engine upload(const qwen2_model<T>& model,
      ScalarType residual_type = ScalarType::Undefined) {
    return {model.config, qwen2_tensors::upload(model), residual_type};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] const qwen2_config& config() const noexcept { return config_; }
  [[nodiscard]] const qwen2_tensors& tensors() const noexcept {
    return tensors_;
  }
  [[nodiscard]] const rope_tables& rope() const noexcept { return rope_; }

  // The type of the parameters, and so of every sublayer's input and output.
  [[nodiscard]] ScalarType parameter_type() const noexcept {
    return tensors_.embed_tokens.scalar_type();
  }

  // The type the residual stream runs in.
  [[nodiscard]] ScalarType residual_type() const noexcept {
    return residual_type_;
  }

#pragma endregion
#pragma region Running

  // `forward` runs `ids` through the whole model, returning every token's
  // logits.
  //
  // With T tokens and V vocabulary entries:
  //
  //   ids     [T]     the prompt, no more than the positions `config` allows
  //   result  [T, V]  row `t` scores every vocabulary entry as the token after
  //                   `ids[t]`, in the parameters' type
  [[nodiscard]] Tensor forward(token_ids_view ids) const {
    return translate_errors([&] {
      auto residual =
          embed_tokens(tensors_.embed_tokens, ids).to(residual_type_);
      for (const auto& block : tensors_.blocks)
        residual = apply_block(residual, block);
      return at::linear(normalized(residual, tensors_.norm_weight),
          tensors_.lm_head);
    });
  }

  // `apply_block` runs `block` over `residual`, returning the residual after
  // its two sublayers each added their correction.
  //
  // With T tokens of width C:
  //
  //   residual  [T, C]  in the residual type
  //   result    [T, C]  in the residual type
  [[nodiscard]] Tensor
  apply_block(const Tensor& residual, const qwen2_block_tensors& block) const {
    return translate_errors([&] {
      const auto after_attention =
          residual +
          apply_attention(normalized(residual, block.input_layernorm_weight),
              block);
      return after_attention +
             apply_mlp(normalized(after_attention,
                           block.post_attention_layernorm_weight),
                 block);
    });
  }

  // `apply_attention` runs `block`'s attention sublayer over `in`, which is
  // the normalized residual, returning the correction to add to it.
  //
  // With T tokens of width C:
  //
  //   in      [T, C]
  //   result  [T, C]
  //
  // The queries, keys, and values are projections of `in` with a bias, the
  // queries and keys turned by position, and the heads' outputs are merged
  // and projected back to the width without one.
  [[nodiscard]] Tensor
  apply_attention(const Tensor& in, const qwen2_block_tensors& block) const {
    return translate_errors([&] {
      const auto head_count =
          static_cast<int64_t>(config_.num_attention_heads);
      const auto kv_head_count =
          static_cast<int64_t>(config_.num_key_value_heads);
      const auto q = apply_rope(
          split_heads(at::linear(in, block.q_proj_weight, block.q_proj_bias),
              head_count),
          rope_);
      const auto k = apply_rope(
          split_heads(at::linear(in, block.k_proj_weight, block.k_proj_bias),
              kv_head_count),
          rope_);
      const auto v = split_heads(
          at::linear(in, block.v_proj_weight, block.v_proj_bias),
          kv_head_count);
      return at::linear(merge_heads(attend(q, k, v)), block.o_proj_weight);
    });
  }

  // `apply_mlp` runs `block`'s MLP sublayer over `in`, which is the
  // normalized residual, returning the correction to add to it.
  //
  // With T tokens of width C and MLP width F:
  //
  //   in      [T, C]
  //   result  [T, C]
  //
  // `in` widens to F through the gate and up projections, `swiglu` combines
  // them, and the down projection narrows the result back to C.
  [[nodiscard]] static Tensor
  apply_mlp(const Tensor& in, const qwen2_block_tensors& block) {
    return translate_errors([&] {
      const auto hidden = swiglu(at::linear(in, block.gate_proj_weight),
          at::linear(in, block.up_proj_weight));
      return at::linear(hidden, block.down_proj_weight);
    });
  }

#pragma endregion
#pragma region Helpers
private:
  // `residual` normalized by `weight`, as a sublayer's input in the
  // parameters' type.
  [[nodiscard]] Tensor
  normalized(const Tensor& residual, const Tensor& weight) const {
    return rms_norm(residual, weight, config_.rms_norm_eps)
        .to(parameter_type());
  }

  // Run `f`, rethrowing torch's error as `std::runtime_error`.
  template<typename F>
  static std::invoke_result_t<F> translate_errors(F&& f) {
    try {
      return std::forward<F>(f)();
    }
    catch (const c10::Error& e) {
      throw std::runtime_error{
          std::format("Qwen2 engine: {}", e.what_without_backtrace())};
    }
  }

#pragma endregion
#pragma region Data members

  qwen2_config config_;
  qwen2_tensors tensors_;
  ScalarType residual_type_;
  rope_tables rope_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::torch::llm
