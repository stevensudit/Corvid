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
#include <format>
#include <span>
#include <stdexcept>
#include <vector>

#include <ATen/ATen.h>
#include <c10/util/Exception.h>

#include "../llm/qwen2.h"
#include "tensor_adapter.cuh"

// A Qwen2 model's parameters as tensors on the device.
//
// `qwen2_tensors::upload` copies every parameter of a `qwen2_model` to the
// device as it is, so a bf16 model becomes bf16 tensors, named as the model
// names them:
//
//   const auto tensors = qwen2_tensors::upload(model);
//   at::matmul(hidden, tensors.blocks[0].q_proj_weight.t())
//
// Every tensor owns its device storage, which lives as long as any handle
// to it does, so copying a struct of tensors shares the storage rather than
// the device memory, as `Tensor` itself does.
//
// Every error torch raises on the way stays inside this band. `upload`
// rethrows it as `std::runtime_error`.

namespace corvid::torch {

#pragma region qwen2_block_tensors

// One block's parameters on the device, named as `qwen2_block_params` names
// them, each owning its storage.
struct qwen2_block_tensors {
  Tensor input_layernorm_weight;
  Tensor q_proj_weight;
  Tensor q_proj_bias;
  Tensor k_proj_weight;
  Tensor k_proj_bias;
  Tensor v_proj_weight;
  Tensor v_proj_bias;
  Tensor o_proj_weight;
  Tensor post_attention_layernorm_weight;
  Tensor gate_proj_weight;
  Tensor up_proj_weight;
  Tensor down_proj_weight;
};

#pragma endregion
#pragma region qwen2_tensors

// A Qwen2 model's parameters on the device, named as `qwen2_model` names
// them, each owning its storage.
//
// `lm_head` is the very same tensor as `embed_tokens` when the configuration
// ties them, so the embedding is on the device once.
struct qwen2_tensors {
  Tensor embed_tokens;
  std::vector<qwen2_block_tensors> blocks;
  Tensor norm_weight;
  Tensor lm_head;

#pragma region Uploading

  // Copy every parameter of `model` to the device, or throw when the device
  // refuses, which is out of memory in practice.
  template<llm::ParameterElement T>
  [[nodiscard]] static qwen2_tensors upload(const llm::qwen2_model<T>& model) {
    try {
      qwen2_tensors tensors{
          .embed_tokens = to_device(model.embed_tokens),
          .norm_weight = to_device(model.norm_weight),
      };
      tensors.blocks.reserve(model.blocks.size());
      for (const auto& block : model.blocks)
        tensors.blocks.push_back(upload_block(block));
      tensors.lm_head =
          model.config.tie_word_embeddings
              ? tensors.embed_tokens
              : to_device(model.lm_head);
      return tensors;
    }
    catch (const c10::Error& e) {
      throw std::runtime_error{
          std::format("Qwen2 upload: {}", e.what_without_backtrace())};
    }
  }

#pragma endregion
#pragma region Helpers
private:
  // One block's parameters, copied to the device.
  template<llm::ParameterElement T>
  [[nodiscard]] static qwen2_block_tensors
  upload_block(const llm::qwen2_block_params<T>& block) {
    return {
        .input_layernorm_weight = to_device(block.input_layernorm_weight),
        .q_proj_weight = to_device(block.q_proj_weight),
        .q_proj_bias = to_device(block.q_proj_bias),
        .k_proj_weight = to_device(block.k_proj_weight),
        .k_proj_bias = to_device(block.k_proj_bias),
        .v_proj_weight = to_device(block.v_proj_weight),
        .v_proj_bias = to_device(block.v_proj_bias),
        .o_proj_weight = to_device(block.o_proj_weight),
        .post_attention_layernorm_weight =
            to_device(block.post_attention_layernorm_weight),
        .gate_proj_weight = to_device(block.gate_proj_weight),
        .up_proj_weight = to_device(block.up_proj_weight),
        .down_proj_weight = to_device(block.down_proj_weight),
    };
  }

  // `host` copied to the device as a contiguous tensor of the same shape.
  template<typename T>
  [[nodiscard]] static Tensor to_device(matrix_view<T> host) {
    return as_tensor(host).to(DeviceType::CUDA);
  }

  template<typename T>
  [[nodiscard]] static Tensor
  to_device(enum_span<const T, matrix_types::col_ndx> host) {
    return as_tensor(std::span<const T>(host.data(), host.size()))
        .to(DeviceType::CUDA);
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::torch
