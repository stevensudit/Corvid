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

#include <cassert>
#include <cstddef>
#include <vector>

#include "../../llm/gpt2.h"
#include "../../meta/containers.h"
#include "../cuda_buffer.cuh"
#include "../cuda_cublas.cuh"
#include "../cuda_matrix.cuh"
#include "llm_ops.cuh"

// The GPT-2 architecture on the device, in fp32.
//
// The parameter and activation bundles of one block, the block itself, and
// the forward pass from token IDs through every block to the final layer
// norm, composing the ops of "llm_ops.cuh" as "gpt2.h" composes the CPU ones.
// The parameters are uploaded once from their CPU views and owned here. The
// activations are caller-owned views, as on the CPU.
namespace corvid::cuda::llm {

using corvid::llm::layer_norm_eps;

#pragma region block

// The parameters of one block, uploaded from `corvid::llm::block_params`.
//
// The names and shapes are those of the CPU bundle.
struct block_params {
  cuda_buffer<float> ln_1_weight;
  cuda_buffer<float> ln_1_bias;
  cuda_matrix<float> attn_c_attn_weight;
  cuda_buffer<float> attn_c_attn_bias;
  cuda_matrix<float> attn_c_proj_weight;
  cuda_buffer<float> attn_c_proj_bias;
  cuda_buffer<float> ln_2_weight;
  cuda_buffer<float> ln_2_bias;
  cuda_matrix<float> mlp_c_fc_weight;
  cuda_buffer<float> mlp_c_fc_bias;
  cuda_matrix<float> mlp_c_proj_weight;
  cuda_buffer<float> mlp_c_proj_bias;

  // Allocate and upload every parameter of `host`, or throw.
  explicit block_params(const corvid::llm::block_params& host)
      : ln_1_weight(host.ln_1_weight), ln_1_bias(host.ln_1_bias),
        attn_c_attn_weight(host.attn_c_attn_weight),
        attn_c_attn_bias(host.attn_c_attn_bias),
        attn_c_proj_weight(host.attn_c_proj_weight),
        attn_c_proj_bias(host.attn_c_proj_bias), ln_2_weight(host.ln_2_weight),
        ln_2_bias(host.ln_2_bias), mlp_c_fc_weight(host.mlp_c_fc_weight),
        mlp_c_fc_bias(host.mlp_c_fc_bias),
        mlp_c_proj_weight(host.mlp_c_proj_weight),
        mlp_c_proj_bias(host.mlp_c_proj_bias) {}
};

// The intermediate activations of `block`, as caller-owned device views.
//
// The names, shapes, and sharing rules are those of the CPU
// `corvid::llm::block_activations`, except that `scores` is the device
// `attend`'s T x T scratch rather than one row.
//
// Note that the `const` on an instance is shallow.
struct block_activations {
  cuda_matrix_view<float> ln_1_out;
  cuda_matrix_view<float> qkv;
  cuda_matrix_view<float> heads_out;
  cuda_matrix_view<float> attn_out;
  cuda_matrix_view<float> ln_2_in;
  cuda_matrix_view<float> ln_2_out;
  cuda_matrix_view<float> hidden;
  cuda_matrix_view<float> mlp_out;
  cuda_matrix_view<float> scores;
};

// Run one block over the residual, reading `in` and writing `out`.
//
// The contract is that of the CPU `corvid::llm::block`, which also holds the
// step table. `out` and `in` must have the same extent and may be the same
// view. The activation views must have the extents `block_activations`
// states for that extent and may share storage only as it allows. Returns
// false when a launch is refused, leaving `out` and the activations
// unspecified.
[[nodiscard]] inline bool block(const cublas_handle& blas,
    cuda_matrix_view<float> out, cuda_matrix_view<const float> in,
    const block_params& params, const block_activations& acts,
    size_t head_count) {
  // Each of these four writes is followed by an add that reads the residual
  // it would have destroyed; the ops' same-or-disjoint checks allow all
  // four, and the ops catch every other overlap.
  assert(is_disjoint(in.as_span(), acts.ln_1_out.as_span()));
  assert(is_disjoint(in.as_span(), acts.attn_out.as_span()));
  assert(is_disjoint(acts.ln_2_in.as_span(), acts.ln_2_out.as_span()));
  assert(is_disjoint(acts.ln_2_in.as_span(), acts.mlp_out.as_span()));

  if (!layer_norm(acts.ln_1_out, in, params.ln_1_weight, params.ln_1_bias,
          layer_norm_eps))
    return false;
  if (!linear_projection(blas, acts.qkv, acts.ln_1_out,
          params.attn_c_attn_weight, params.attn_c_attn_bias))
    return false;
  if (!attend(blas, acts.heads_out, acts.qkv, head_count, acts.scores))
    return false;
  if (!linear_projection(blas, acts.attn_out, acts.heads_out,
          params.attn_c_proj_weight, params.attn_c_proj_bias))
    return false;
  if (!add(acts.ln_2_in, in, acts.attn_out)) return false;

  if (!layer_norm(acts.ln_2_out, acts.ln_2_in, params.ln_2_weight,
          params.ln_2_bias, layer_norm_eps))
    return false;
  if (!linear_projection(blas, acts.hidden, acts.ln_2_out,
          params.mlp_c_fc_weight, params.mlp_c_fc_bias))
    return false;
  if (!gelu_new(acts.hidden, acts.hidden)) return false;
  if (!linear_projection(blas, acts.mlp_out, acts.hidden,
          params.mlp_c_proj_weight, params.mlp_c_proj_bias))
    return false;
  return add(out, acts.ln_2_in, acts.mlp_out);
}

#pragma endregion
#pragma region forward

// The parameters of the whole model, uploaded from `corvid::llm::gpt2_params`.
//
// The names and shapes are those of the CPU bundle, with the blocks owned
// here in `h.N` order.
struct gpt2_params {
  cuda_matrix<float> wte;
  cuda_matrix<float> wpe;
  std::vector<block_params> blocks;
  cuda_buffer<float> ln_f_weight;
  cuda_buffer<float> ln_f_bias;

  // Allocate and upload every parameter of `host`, or throw.
  explicit gpt2_params(const corvid::llm::gpt2_params& host)
      : wte(host.wte), wpe(host.wpe), ln_f_weight(host.ln_f_weight),
        ln_f_bias(host.ln_f_bias) {
    blocks.reserve(host.blocks.size());
    for (const auto& host_block : host.blocks) blocks.emplace_back(host_block);
  }
};

// Run the model over `ids`, writing the final layer norm's output to `out`.
//
// The contract is that of the CPU `corvid::llm::forward`, which also holds
// the step table. `out` doubles as the residual, so it must have one row per
// ID and the model's width, and there must be no more IDs than `wpe` has
// rows. `acts` is reused by every block, so it holds the last block's
// activations on return; its `ln_2_in` may be `out`, the in-place form, but
// no other buffer may overlap `out`. Returns false when a launch is refused,
// leaving `out` and the activations unspecified.
[[nodiscard]] inline bool forward(const cublas_handle& blas,
    cuda_matrix_view<float> out, const cuda_buffer<token_id>& ids,
    const gpt2_params& params, const block_activations& acts,
    size_t head_count) {
  using row_ndx = cuda_matrix_view<float>::row_ndx;
  using col_ndx = cuda_matrix_view<float>::col_ndx;

  assert(ids.size() <= params.wpe.row_extent());

  if (!embed_tokens(out, ids, params.wte)) return false;
  if (!add(out, out,
          params.wpe.subview({row_ndx{0}, col_ndx{0}},
              {.row_count = ids.size(),
                  .col_count = params.wpe.col_extent()})))
    return false;
  for (const auto& block_params : params.blocks)
    if (!block(blas, out, out, block_params, acts, head_count)) return false;
  return layer_norm(out, out, params.ln_f_weight, params.ln_f_bias,
      layer_norm_eps);
}

#pragma endregion

} // namespace corvid::cuda::llm
