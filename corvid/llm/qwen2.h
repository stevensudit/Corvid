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
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "../containers/utils/interval.h"
#include "../containers/utils/matrix_view.h"
#include "../enums/enum_conversion.h"
#include "../proto/misc/json_parser.h"
#include "parameter_element.h"
#include "safetensors.h"
#include "token_id.h"

// A Qwen2 model as its publisher ships it, which is the dimensions from
// "config.json" and the parameters as views over the sharded safetensors
// files.
//
// `qwen2_config::parse` reads the configuration, and `qwen2_model::load`
// takes the shards and views every parameter the configuration names:
//
//   const auto config = qwen2_config::parse(config_json);
//   const auto model =
//       qwen2_model<bfloat16_t>::load(config, std::move(shards));
//   model.blocks[0].q_proj_weight  // [C, C], a view into its shard
//
// Nothing here computes. Running the model is the torch band's job, which
// uploads these views to the device.

namespace corvid::llm {

#pragma region qwen2_config

// The dimensions and constants of a Qwen2 model, from its "config.json".
//
// The members keep the file's key names. For Qwen2.5-3B-Instruct: C = 2048
// features per token, F = 11008 inside the MLP, L = 36 blocks, H = 16 query
// heads sharing 2 key-value heads of D = 128 features each, V = 151936
// vocabulary entries (padded past the 151665 the tokenizer uses), and a
// context of 32768 positions.
struct qwen2_config {
  size_t hidden_size{};             // C
  size_t intermediate_size{};       // F
  size_t num_hidden_layers{};       // L
  size_t num_attention_heads{};     // H
  size_t num_key_value_heads{};     // the heads the keys and values have
  size_t vocab_size{};              // V
  size_t max_position_embeddings{}; // the context
  double rms_norm_eps{};
  double rope_theta{};
  bool tie_word_embeddings{};
  token_id bos_token_id{};
  token_id eos_token_id{};

#pragma region Dimensions

  // The features per head, D.
  [[nodiscard]] size_t head_dim() const noexcept {
    return hidden_size / num_attention_heads;
  }

  // The features of one token's keys, or values, over every key-value head.
  [[nodiscard]] size_t kv_width() const noexcept {
    return num_key_value_heads * head_dim();
  }

#pragma endregion
#pragma region Parsing

  // Parse the text of "config.json", or throw when it is not a JSON object or
  // a key is missing or not of its type.
  [[nodiscard]] static qwen2_config parse(std::string_view json) {
    json_value_view root;
    if (!parse_json(json, root) || !root.is_object())
      throw std::runtime_error{"Qwen2 config: not a JSON object"};
    const auto fields = root.as_object();
    return {
        .hidden_size = number<size_t>(fields, "hidden_size"),
        .intermediate_size = number<size_t>(fields, "intermediate_size"),
        .num_hidden_layers = number<size_t>(fields, "num_hidden_layers"),
        .num_attention_heads = number<size_t>(fields, "num_attention_heads"),
        .num_key_value_heads = number<size_t>(fields, "num_key_value_heads"),
        .vocab_size = number<size_t>(fields, "vocab_size"),
        .max_position_embeddings =
            number<size_t>(fields, "max_position_embeddings"),
        .rms_norm_eps = number<double>(fields, "rms_norm_eps"),
        .rope_theta = number<double>(fields, "rope_theta"),
        .tie_word_embeddings = flag(fields, "tie_word_embeddings"),
        .bos_token_id = token_id{number<uint32_t>(fields, "bos_token_id")},
        .eos_token_id = token_id{number<uint32_t>(fields, "eos_token_id")},
    };
  }

#pragma endregion
#pragma region Helpers
private:
  // The number `key` of `fields`, as `T`, or throw.
  template<typename T>
  [[nodiscard]] static T
  number(json_object_view fields, std::string_view key) {
    const auto value = fields.get_number<T>(key);
    if (!value) raise_missing(key, "number");
    return *value;
  }

  // The Boolean `key` of `fields`, or throw.
  [[nodiscard]] static bool
  flag(json_object_view fields, std::string_view key) {
    const auto value = fields.get_bool(key);
    if (!value) raise_missing(key, "Boolean");
    return *value;
  }

  // Throw for the key `key`, which is missing or not a `kind`.
  [[noreturn]] static void
  raise_missing(std::string_view key, std::string_view kind) {
    throw std::runtime_error{
        std::format("Qwen2 config: key {} is missing or not a {}", key, kind)};
  }

#pragma endregion
};

#pragma endregion
#pragma region qwen2_block_params

// The parameters of one block, as views over a weights file whose
// parameters are `T`.
//
// The names follow the tensor names under `model.layers.N`, without their
// `self_attn` and `mlp` prefixes. Every projection is a `torch.nn.Linear`,
// so its weight is [out, in] and applies as `x @ W^T`. For Qwen2.5-3B, with
// C = 2048, F = 11008, and K = 256 (the keys' width over its two heads):
//
//   input_layernorm_weight           [C]
//   q_proj_weight, q_proj_bias       [C, C], [C]
//   k_proj_weight, k_proj_bias       [K, C], [K]
//   v_proj_weight, v_proj_bias       [K, C], [K]
//   o_proj_weight                    [C, C]
//   post_attention_layernorm_weight  [C]
//   gate_proj_weight                 [F, C]
//   up_proj_weight                   [F, C]
//   down_proj_weight                 [C, F]
template<ParameterElement T>
struct qwen2_block_params {
  using element_t = T;
  using matrix_t = matrix_view<element_t>;
  using vector_t = enum_span<const element_t, matrix_types::col_ndx>;

  vector_t input_layernorm_weight;
  matrix_t q_proj_weight;
  vector_t q_proj_bias;
  matrix_t k_proj_weight;
  vector_t k_proj_bias;
  matrix_t v_proj_weight;
  vector_t v_proj_bias;
  matrix_t o_proj_weight;
  vector_t post_attention_layernorm_weight;
  matrix_t gate_proj_weight;
  matrix_t up_proj_weight;
  matrix_t down_proj_weight;
};

#pragma endregion
#pragma region qwen2_model

// A Qwen2 model, as views over the sharded weights files it owns, whose
// parameters are `T`.
//
// For Qwen2.5-3B, with V = 151936, C = 2048, and L = 36:
//
//   embed_tokens  [V, C]  the token embedding
//   blocks        [L]     one `block_params` per `model.layers.N`, in order
//   norm_weight   [C]     the final RMS norm
//   lm_head       [V, C]  the head, which is `embed_tokens` itself when the
//                         configuration ties them
//
// Every tensor of the files must be `T`, so a bf16 file is viewed as it is,
// never widened. Every member is const, so a model is built by `load` and
// never changes, and the views live as long as it does.
template<ParameterElement T>
struct qwen2_model {
#pragma region Types

  using element_t = T;
  using block_params = qwen2_block_params<element_t>;
  using matrix_t = block_params::matrix_t;
  using vector_t = block_params::vector_t;
  using matrix_extent = matrix_types::matrix_extent;

#pragma endregion
#pragma region Data members

  const qwen2_config config;
  const std::vector<safetensors_file> shards;
  const matrix_t embed_tokens;
  const std::vector<block_params> blocks;
  const vector_t norm_weight;
  const matrix_t lm_head;

#pragma endregion
#pragma region Loading

  // Take `shards` and view every parameter `config` names, searching the
  // shards in order, or throw when a tensor is missing, misshaped, or not `T`.
  [[nodiscard]] static qwen2_model
  load(const qwen2_config& config, std::vector<safetensors_file>&& shards) {
    const matrix_extent embedding_extent{.row_count = config.vocab_size,
        .col_count = config.hidden_size};
    const auto embed_tokens =
        lookup_matrix(shards, "model.embed_tokens.weight", embedding_extent);
    std::vector<block_params> blocks;
    blocks.reserve(config.num_hidden_layers);
    for (const auto n : iota(config.num_hidden_layers))
      blocks.push_back(lookup_block(shards, n, config));
    const auto norm_weight =
        lookup_vector(shards, "model.norm.weight", config.hidden_size);
    const auto lm_head =
        config.tie_word_embeddings
            ? embed_tokens
            : lookup_matrix(shards, "lm_head.weight", embedding_extent);
    // The views point into the mappings, whose addresses the move preserves.
    return {
        .config = config,
        .shards = std::move(shards),
        .embed_tokens = embed_tokens,
        .blocks = std::move(blocks),
        .norm_weight = norm_weight,
        .lm_head = lm_head,
    };
  }

#pragma endregion
#pragma region Helpers
private:
  // The parameters of block `n`, or throw.
  [[nodiscard]] static block_params
  lookup_block(std::span<const safetensors_file> shards, size_t n,
      const qwen2_config& config) {
    const auto width = config.hidden_size;
    const auto kv_width = config.kv_width();
    const auto hidden_width = config.intermediate_size;
    const auto name = [n](std::string_view suffix) {
      return std::format("model.layers.{}.{}", n, suffix);
    };
    const auto matrix = [&](std::string_view suffix, matrix_extent extent) {
      return lookup_matrix(shards, name(suffix), extent);
    };
    const auto vector = [&](std::string_view suffix, size_t size) {
      return lookup_vector(shards, name(suffix), size);
    };
    const matrix_extent square{.row_count = width, .col_count = width};
    const matrix_extent kv{.row_count = kv_width, .col_count = width};
    const matrix_extent up{.row_count = hidden_width, .col_count = width};
    const matrix_extent down{.row_count = width, .col_count = hidden_width};
    return {
        .input_layernorm_weight = vector("input_layernorm.weight", width),
        .q_proj_weight = matrix("self_attn.q_proj.weight", square),
        .q_proj_bias = vector("self_attn.q_proj.bias", width),
        .k_proj_weight = matrix("self_attn.k_proj.weight", kv),
        .k_proj_bias = vector("self_attn.k_proj.bias", kv_width),
        .v_proj_weight = matrix("self_attn.v_proj.weight", kv),
        .v_proj_bias = vector("self_attn.v_proj.bias", kv_width),
        .o_proj_weight = matrix("self_attn.o_proj.weight", square),
        .post_attention_layernorm_weight =
            vector("post_attention_layernorm.weight", width),
        .gate_proj_weight = matrix("mlp.gate_proj.weight", up),
        .up_proj_weight = matrix("mlp.up_proj.weight", up),
        .down_proj_weight = matrix("mlp.down_proj.weight", down),
    };
  }

  // The matrix `name` of the first shard that holds it, whose shape must
  // match `expected` in each count that is not `dynamic_extent`, or throw.
  [[nodiscard]] static matrix_t
  lookup_matrix(std::span<const safetensors_file> shards,
      std::string_view name, matrix_extent expected = {}) {
    for (const auto& shard : shards) {
      const auto view = shard.find_matrix<element_t>(name, expected);
      if (!view.empty()) return view;
    }
    raise_missing(name);
  }

  // The vector `name` of the first shard that holds it, `size` long, or throw.
  [[nodiscard]] static vector_t
  lookup_vector(std::span<const safetensors_file> shards,
      std::string_view name, size_t size) {
    for (const auto& shard : shards) {
      const auto span = shard.find_vector<element_t>(name, size);
      if (!span.empty()) return span;
    }
    raise_missing(name);
  }

  // Throw for the tensor `name`, which is missing, misshaped, or not `T`.
  [[noreturn]] static void raise_missing(std::string_view name) {
    throw std::runtime_error{std::format(
        "Qwen2 weights: tensor {} is missing, misshaped, or not {}", name,
        enum_as_string(dtype_of<element_t>()))};
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::llm
