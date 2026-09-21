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
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "../containers/utils/matrix_view.h"
#include "safetensors.h"
#include "token_id.h"

namespace corvid::llm {

#pragma region gpt2_model

// A GPT-2 model, as views over the weights file it owns.
//
// For GPT-2, with V = 50257 vocabulary entries, a context of 1024, C = 768,
// and L = 12 blocks:
//
//   wte                     [V, C]     the token embedding, reused by the head
//   wpe                     [1024, C]  the position embedding
//   blocks                  [L]        one `block_params` per `h.N`, in order
//   ln_f_weight, ln_f_bias  [C]        the final layer norm
//
// Every member is const, so a model is built by `load` and never changes,
// and the views live as long as it does.
//
//   const auto model = gpt2_model::load(safetensors_file::load(os_file));
struct gpt2_model {
#pragma region Constants

  // The epsilon GPT-2 adds to the variance inside every layer norm.
  static constexpr float layer_norm_eps = 1e-5F;

  // The ID of `<|endoftext|>`, the last vocabulary entry, which ends a
  // generation.
  static constexpr token_id end_of_text{50256};

#pragma endregion
#pragma region block_params

  // The parameters of one block, as views over the weight file.
  //
  // The names follow the tensor names under `h.N`, with the sublayer
  // prefixed so the two `c_proj` projections can be told apart. For GPT-2,
  // with C = 768 and F = 3072:
  //
  //   ln_1_weight, ln_1_bias                [C]
  //   attn_c_attn_weight, attn_c_attn_bias  [C, 3C], [3C]
  //   attn_c_proj_weight, attn_c_proj_bias  [C, C], [C]
  //   ln_2_weight, ln_2_bias                [C]
  //   mlp_c_fc_weight, mlp_c_fc_bias        [C, F], [F]
  //   mlp_c_proj_weight, mlp_c_proj_bias    [F, C], [C]
  struct block_params {
    const_float_row_span ln_1_weight;
    const_float_row_span ln_1_bias;
    const_float_matrix_view attn_c_attn_weight;
    const_float_row_span attn_c_attn_bias;
    const_float_matrix_view attn_c_proj_weight;
    const_float_row_span attn_c_proj_bias;
    const_float_row_span ln_2_weight;
    const_float_row_span ln_2_bias;
    const_float_matrix_view mlp_c_fc_weight;
    const_float_row_span mlp_c_fc_bias;
    const_float_matrix_view mlp_c_proj_weight;
    const_float_row_span mlp_c_proj_bias;
  };

#pragma endregion
#pragma region Data members

  const safetensors_file weights;
  const const_float_matrix_view wte;
  const const_float_matrix_view wpe;
  const std::vector<block_params> blocks;
  const const_float_row_span ln_f_weight;
  const const_float_row_span ln_f_bias;
  const size_t head_count; // Heads per block, H.

#pragma endregion
#pragma region Dimensions

  // The features per token, C.
  [[nodiscard]] size_t width() const noexcept { return wte.col_extent(); }

  // The features per token inside the MLP, F.
  [[nodiscard]] size_t hidden_width() const noexcept {
    return blocks.front().mlp_c_fc_weight.col_extent();
  }

  // The vocabulary entries, V.
  [[nodiscard]] size_t vocab_size() const noexcept { return wte.row_extent(); }

  // The most tokens one pass can take.
  [[nodiscard]] size_t context_length() const noexcept {
    return wpe.row_extent();
  }

#pragma endregion
#pragma region Loading

  // Take `file` and view every parameter, or throw when a tensor is missing
  // or misshaped or the block count is not one of GPT-2's four sizes.
  [[nodiscard]] static gpt2_model load(safetensors_file&& file) {
    const auto wte = lookup_matrix(file, "wte.weight");
    const auto width = wte.col_extent();
    const auto wpe = lookup_matrix(file, "wpe.weight", {.col_count = width});
    std::vector<block_params> blocks;
    for (auto n = 0UZ; file.find(std::format("h.{}.ln_1.weight", n)); ++n)
      blocks.push_back(lookup_block(file, n, width));
    const auto head_count = head_count_of(blocks.size());
    const auto ln_f_weight = lookup_vector(file, "ln_f.weight", width);
    const auto ln_f_bias = lookup_vector(file, "ln_f.bias", width);
    // The views point into the mapping, whose address the move preserves.
    return {
        .weights = std::move(file),
        .wte = wte,
        .wpe = wpe,
        .blocks = std::move(blocks),
        .ln_f_weight = ln_f_weight,
        .ln_f_bias = ln_f_bias,
        .head_count = head_count,
    };
  }

#pragma endregion
#pragma region Helpers
private:
  // The fp32 matrix `name` of `file`, whose shape must match `expected` in
  // each count that is not `dynamic_extent`, or throw.
  [[nodiscard]] static const_float_matrix_view
  lookup_matrix(const safetensors_file& file, std::string_view name,
      matrix_types::matrix_extent expected = {}) {
    const auto view = file.find_matrix<float>(name, expected);
    if (view.empty())
      throw std::runtime_error{std::format(
          "GPT-2 weights: tensor {} is missing or misshaped", name)};
    return view;
  }

  // The fp32 vector `name` of `file`, `size` long, or throw.
  [[nodiscard]] static const_float_row_span lookup_vector(
      const safetensors_file& file, std::string_view name, size_t size) {
    const auto span = file.find_vector<float>(name, size);
    if (span.empty())
      throw std::runtime_error{std::format(
          "GPT-2 weights: tensor {} is missing or misshaped", name)};
    return span;
  }

  // The parameters of block `n` of `file`, for tokens `width` wide, or throw.
  [[nodiscard]] static block_params
  lookup_block(const safetensors_file& file, size_t n, size_t width) {
    const auto h = std::format("h.{}", n);
    const auto mlp_c_fc_weight =
        lookup_matrix(file, h + ".mlp.c_fc.weight", {.row_count = width});
    const auto hidden_width = mlp_c_fc_weight.col_extent();
    return {
        .ln_1_weight = lookup_vector(file, h + ".ln_1.weight", width),
        .ln_1_bias = lookup_vector(file, h + ".ln_1.bias", width),
        .attn_c_attn_weight = lookup_matrix(file, h + ".attn.c_attn.weight",
            {.row_count = width, .col_count = 3 * width}),
        .attn_c_attn_bias =
            lookup_vector(file, h + ".attn.c_attn.bias", 3 * width),
        .attn_c_proj_weight = lookup_matrix(file, h + ".attn.c_proj.weight",
            {.row_count = width, .col_count = width}),
        .attn_c_proj_bias =
            lookup_vector(file, h + ".attn.c_proj.bias", width),
        .ln_2_weight = lookup_vector(file, h + ".ln_2.weight", width),
        .ln_2_bias = lookup_vector(file, h + ".ln_2.bias", width),
        .mlp_c_fc_weight = mlp_c_fc_weight,
        .mlp_c_fc_bias =
            lookup_vector(file, h + ".mlp.c_fc.bias", hidden_width),
        .mlp_c_proj_weight = lookup_matrix(file, h + ".mlp.c_proj.weight",
            {.row_count = hidden_width, .col_count = width}),
        .mlp_c_proj_bias = lookup_vector(file, h + ".mlp.c_proj.bias", width),
    };
  }

  // The head count of the GPT-2 size with `block_count` blocks, or throw.
  //
  // The tensor shapes give every other dimension, but not how the width
  // divides into heads, so the block count names the size.
  [[nodiscard]] static size_t head_count_of(size_t block_count) {
    switch (block_count) {
    case 12: return 12; // 124M
    case 24: return 16; // 355M
    case 36: return 20; // 774M
    case 48: return 25; // 1558M
    default:
      throw std::runtime_error{std::format(
          "GPT-2 weights: {} blocks is not a GPT-2 size", block_count)};
    }
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::llm
