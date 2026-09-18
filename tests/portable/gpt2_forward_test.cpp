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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "corvid/cuda/llm/gpt2_forward.h"
#include "corvid/cuda/llm/safetensors.h"
#include "catch2_main.h"
#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include "test_files.h"

using namespace corvid;
using namespace corvid::llm;
using Catch::Matchers::WithinAbs;

using row_ndx = float_matrix_view::row_ndx;
using col_ndx = float_matrix_view::col_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

#pragma region Oracle

// The path of one gitignored oracle dump under tests/.local/llm/gpt2.
std::filesystem::path oracle_path(std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path().parent_path() /
         ".local" / "llm" / "gpt2" / name;
}

// The model weights, the bisect prompt's activations, and every prompt's IDs
// and logits, as the oracle dumped them.
struct oracle_dumps {
  safetensors_file weights;
  safetensors_file activations;
  safetensors_file logits;

  // Load the files, skipping the test when the oracle has not run here.
  void load() {
    const auto model_path = oracle_path("model.safetensors");
    const auto activations_path = oracle_path("activations.safetensors");
    const auto logits_path = oracle_path("logits.safetensors");
    if (!std::filesystem::exists(model_path) ||
        !std::filesystem::exists(activations_path) ||
        !std::filesystem::exists(logits_path))
      SKIP("no oracle dumps under tests/.local/llm/gpt2; run the oracle");
    REQUIRE(weights.load(tests::open_read_only(model_path)));
    REQUIRE(activations.load(tests::open_read_only(activations_path)));
    REQUIRE(logits.load(tests::open_read_only(logits_path)));
  }
};

// The fp32 tensor `name` of `file`, which must be two-dimensional with `cols`
// columns, as a matrix view.
const_float_matrix_view
matrix_of(const safetensors_file& file, std::string_view name, size_t cols) {
  INFO(name);
  const auto* entry = file.find(name);
  REQUIRE(entry);
  REQUIRE(entry->shape.size() == 2);
  REQUIRE(entry->shape[1] == cols);
  REQUIRE(entry->is<float>());
  return const_float_matrix_view(entry->as<float>(),
      {.row_count = entry->shape[0], .col_count = cols});
}

// The fp32 tensor `name` of `file`, which must be one-dimensional with `size`
// elements.
std::span<const float>
vector_of(const safetensors_file& file, std::string_view name, size_t size) {
  INFO(name);
  const auto* entry = file.find(name);
  REQUIRE(entry);
  REQUIRE(entry->shape == std::vector<size_t>{size});
  REQUIRE(entry->is<float>());
  return entry->as<float>();
}

// The int32 tensor `name` of `file`, which must be one-dimensional, as token
// IDs.
std::vector<token_id>
ids_of(const safetensors_file& file, std::string_view name) {
  INFO(name);
  const auto* entry = file.find(name);
  REQUIRE(entry);
  REQUIRE(entry->shape.size() == 1);
  REQUIRE(entry->is<int32_t>());
  std::vector<token_id> ids;
  for (const auto id : entry->as<int32_t>()) {
    REQUIRE(id >= 0);
    ids.push_back(token_id{static_cast<uint32_t>(id)});
  }
  return ids;
}

#pragma endregion
#pragma region Closeness

// The outcome of comparing two matrices elementwise under the allclose rule,
// `|actual - expected| <= atol + rtol * |expected|`.
struct closeness {
  size_t violations{};
  float max_abs_error{};
  // Over the elements whose expected value is not zero.
  float max_rel_error{};
};

// Compare `actual` to `expected`, which must have the same extent.
closeness compare(const_float_matrix_view actual,
    const_float_matrix_view expected, float atol, float rtol) {
  REQUIRE(actual.row_extent() == expected.row_extent());
  REQUIRE(actual.col_extent() == expected.col_extent());
  closeness result;
  for (const auto r : actual.row_interval()) {
    const auto actual_row = actual.row_as_span(r);
    const auto expected_row = expected.row_as_span(r);
    for (auto const col : actual.col_interval()) {
      const auto magnitude = std::abs(expected_row[col]);
      const auto abs_error = std::abs(actual_row[col] - expected_row[col]);
      result.max_abs_error = std::max(result.max_abs_error, abs_error);
      if (magnitude != 0.0F)
        result.max_rel_error =
            std::max(result.max_rel_error, abs_error / magnitude);
      if (abs_error > atol + (rtol * magnitude)) ++result.violations;
    }
  }
  return result;
}

// Check that `actual` is close to `expected`, reporting the largest errors.
void check_close(const_float_matrix_view actual,
    const_float_matrix_view expected, float atol, float rtol) {
  const auto result = compare(actual, expected, atol, rtol);
  INFO("max abs error "
       << result.max_abs_error << ", max rel error " << result.max_rel_error);
  CHECK(result.violations == 0);
}

#pragma endregion

constexpr auto n_layer = 12UZ;
constexpr auto n_embd = 768UZ;
// The MLP widens each token to four times the embedding before projecting it
// back down.
constexpr auto n_hidden = 4 * n_embd;
constexpr auto n_head = 12UZ;
// The `c_attn` projection yields queries, keys, and values side by side.
constexpr auto n_qkv = 3 * n_embd;
constexpr auto n_vocab = 50257UZ;
constexpr auto n_ctx = 1024UZ;
// The prompt whose activations the oracle dumped, by its index in the
// manifest.
constexpr auto bisect_prompt = 1UZ;

#pragma region Block

// The parameters of block `n`, as views over the oracle's weights.
block_params block_params_of(const oracle_dumps& oracle, size_t n) {
  const auto& w = oracle.weights;
  const auto h = std::format("h.{}", n);
  return {
      .ln_1_weight = vector_of(w, h + ".ln_1.weight", n_embd),
      .ln_1_bias = vector_of(w, h + ".ln_1.bias", n_embd),
      .attn_c_attn_weight = matrix_of(w, h + ".attn.c_attn.weight", n_qkv),
      .attn_c_attn_bias = vector_of(w, h + ".attn.c_attn.bias", n_qkv),
      .attn_c_proj_weight = matrix_of(w, h + ".attn.c_proj.weight", n_embd),
      .attn_c_proj_bias = vector_of(w, h + ".attn.c_proj.bias", n_embd),
      .ln_2_weight = vector_of(w, h + ".ln_2.weight", n_embd),
      .ln_2_bias = vector_of(w, h + ".ln_2.bias", n_embd),
      .mlp_c_fc_weight = matrix_of(w, h + ".mlp.c_fc.weight", n_hidden),
      .mlp_c_fc_bias = vector_of(w, h + ".mlp.c_fc.bias", n_hidden),
      .mlp_c_proj_weight = matrix_of(w, h + ".mlp.c_proj.weight", n_embd),
      .mlp_c_proj_bias = vector_of(w, h + ".mlp.c_proj.bias", n_embd),
  };
}

// Owned working storage for `block`, sized for `token_count` tokens of the
// model's widths.
struct owned_block_scratch {
  std::vector<float> normed;
  std::vector<float> qkv;
  std::vector<float> heads_out;
  std::vector<float> sublayer_out;
  std::vector<float> hidden;
  std::vector<float> scores;
  size_t token_count{};

  explicit owned_block_scratch(size_t token_count)
      : normed(token_count * n_embd), qkv(token_count * n_qkv),
        heads_out(token_count * n_embd), sublayer_out(token_count * n_embd),
        hidden(token_count * n_hidden), scores(token_count),
        token_count{token_count} {}

  // The views `block` takes.
  block_scratch views() {
    const auto rows = [&](std::vector<float>& storage, size_t cols) {
      return float_matrix_view(storage,
          {.row_count = token_count, .col_count = cols});
    };
    return {
        .normed = rows(normed, n_embd),
        .qkv = rows(qkv, n_qkv),
        .heads_out = rows(heads_out, n_embd),
        .sublayer_out = rows(sublayer_out, n_embd),
        .hidden = rows(hidden, n_hidden),
        .scores = scores,
    };
  }
};

#pragma endregion

} // namespace

TEST_CASE("Row reductions", "[Gpt2ForwardTest]") {
  constexpr std::array values{1.0F, 2.0F, 3.0F, 4.0F};
  static_assert(sum(values) == 10.0F);
  static_assert(mean(values) == 2.5F);
  static_assert(squared_deviation_sum(values, 2.5F) == 5.0F);
  static_assert(variance(values, 2.5F) == 1.25F);
  constexpr std::array flat{5.0F, 5.0F};
  static_assert(variance(flat, 5.0F) == 0.0F);
  static_assert(sum(std::span<const float>{}) == 0.0F);
  CHECK(std::isnan(mean(std::span<const float>{})));
  CHECK(std::isnan(variance(std::span<const float>{}, 0.0F)));
  CHECK_THAT(inverse_std_dev(values, 2.5F, 0.0F),
      WithinAbs(1.0 / std::sqrt(1.25), 1e-6));
  CHECK(std::isinf(inverse_std_dev(flat, 5.0F, 0.0F)));
  CHECK_THAT(inverse_std_dev(flat, 5.0F, 0.25F), WithinAbs(2.0, 1e-6));
}

TEST_CASE("Elementwise steps", "[Gpt2ForwardTest]") {
  static_assert(standardize(4.0F, 2.5F, 2.0F) == 3.0F);
  static_assert(standardize(2.5F, 2.5F, 2.0F) == 0.0F);
  static_assert(scale_shift(3.0F, 2.0F, 0.5F) == 6.5F);
}

TEST_CASE("Add scaled", "[Gpt2ForwardTest]") {
  std::array acc{1.0F, 2.0F, 3.0F};
  constexpr std::array values{10.0F, 20.0F, 30.0F};
  add_scaled(acc, 0.5F, values);
  CHECK(acc == std::array{6.0F, 12.0F, 18.0F});
}

TEST_CASE("Linear on hand-computed rows", "[Gpt2ForwardTest]") {
  // Two rows of three features through a three-by-two weight: the first
  // output column sums features 0 and 2, the second sums features 1 and 2,
  // and each gets its bias.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
  const std::vector<float> weight_storage{1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
  constexpr std::array bias{10.0F, 20.0F};
  const const_float_matrix_view in(in_storage,
      {.row_count = 2, .col_count = 3});
  const const_float_matrix_view weight(weight_storage,
      {.row_count = 3, .col_count = 2});

  // The output lands in the middle two columns of a wider buffer, the way a
  // projection fills one block of columns and leaves the rest alone.
  std::vector<float> out_storage(2UZ * 4, -1.0F);
  const auto out =
      float_matrix_view(out_storage, {.row_count = 2, .col_count = 4})
          .subview({row_ndx{0}, col_ndx{1}}, {.row_count = 2, .col_count = 2});

  linear(out, in, weight, bias);

  CHECK(out_storage ==
        std::vector<float>{-1.0F, 14.0F, 25.0F, -1.0F, -1.0F, 20.0F, 31.0F,
            -1.0F});
}

TEST_CASE("Layer norm on hand-computed rows", "[Gpt2ForwardTest]") {
  // Row 0 has mean 2.5 and biased variance 1.25. Row 1 is constant, so it
  // normalizes to zero and the output is the bias alone.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 5.0F, 5.0F,
      5.0F};
  std::vector<float> out_storage(in_storage.size());
  const const_float_matrix_view in(in_storage,
      {.row_count = 2, .col_count = 4});
  const float_matrix_view out(out_storage, {.row_count = 2, .col_count = 4});
  constexpr std::array weight{1.0F, 2.0F, 1.0F, 2.0F};
  constexpr std::array bias{0.0F, 0.0F, 0.5F, 0.5F};

  layer_norm(out, in, weight, bias);

  constexpr auto tolerance = 1e-5;
  const auto inv_std = 1.0F / std::sqrt(1.25F + layer_norm_eps);
  const auto first = out.row_as_span(row_ndx{0});
  CHECK_THAT(first[col_ndx{0}], WithinAbs(-1.5F * inv_std, tolerance));
  CHECK_THAT(first[col_ndx{1}], WithinAbs(-0.5F * inv_std * 2.0F, tolerance));
  CHECK_THAT(first[col_ndx{2}], WithinAbs((0.5F * inv_std) + 0.5F, tolerance));
  CHECK_THAT(first[col_ndx{3}],
      WithinAbs((1.5F * inv_std * 2.0F) + 0.5F, tolerance));
  const auto second = out.row_as_span(row_ndx{1});
  CHECK_THAT(second[col_ndx{0}], WithinAbs(0.0, tolerance));
  CHECK_THAT(second[col_ndx{1}], WithinAbs(0.0, tolerance));
  CHECK_THAT(second[col_ndx{2}], WithinAbs(0.5, tolerance));
  CHECK_THAT(second[col_ndx{3}], WithinAbs(0.5, tolerance));
}

TEST_CASE("Row ops match layer norm step by step", "[Gpt2ForwardTest]") {
  // Standardize into a fresh row, then scale and shift it in place, and
  // expect the same output as the fused op.
  const std::vector<float> in_storage{1.0F, 2.0F, 3.0F, 4.0F};
  std::vector<float> out_storage(in_storage.size());
  std::vector<float> expected(in_storage.size());
  const const_float_matrix_view in(in_storage,
      {.row_count = 1, .col_count = 4});
  const float_matrix_view out(out_storage, {.row_count = 1, .col_count = 4});
  const float_matrix_view fused(expected, {.row_count = 1, .col_count = 4});
  constexpr std::array weight{1.0F, 2.0F, 1.0F, 2.0F};
  constexpr std::array bias{0.0F, 0.0F, 0.5F, 0.5F};

  const auto out_row = out.row_as_span(row_ndx{0});
  standardize_row(out_row, in.row_as_span(row_ndx{0}), layer_norm_eps);

  constexpr auto tolerance = 1e-5;
  const auto inv_std = 1.0F / std::sqrt(1.25F + layer_norm_eps);
  CHECK_THAT(out_row[col_ndx{0}], WithinAbs(-1.5F * inv_std, tolerance));
  CHECK_THAT(out_row[col_ndx{3}], WithinAbs(1.5F * inv_std, tolerance));

  scale_shift_row(out_row, out_row, weight, bias);
  layer_norm(fused, in, weight, bias);

  CHECK(out_storage == expected);
}

TEST_CASE("Layer norm in place", "[Gpt2ForwardTest]") {
  std::vector<float> storage{1.0F, 2.0F, 3.0F, 4.0F};
  std::vector<float> expected(storage.size());
  const float_matrix_view m(storage, {.row_count = 1, .col_count = 4});
  const float_matrix_view out(expected, {.row_count = 1, .col_count = 4});
  constexpr std::array weight{1.0F, 2.0F, 1.0F, 2.0F};
  constexpr std::array bias{0.0F, 0.0F, 0.5F, 0.5F};

  layer_norm(out, m, weight, bias);
  layer_norm(m, m, weight, bias);

  CHECK(storage == expected);
}

TEST_CASE("Layer norm honors the stride of both views", "[Gpt2ForwardTest]") {
  // The row lives in the first two columns of a three-column buffer, and
  // writes land in the last two columns of another, leaving the rest alone.
  const std::vector<float> in_storage{2.0F, 4.0F, -1.0F};
  std::vector<float> out_storage{-1.0F, -1.0F, -1.0F};
  const auto in =
      const_float_matrix_view(in_storage, {.row_count = 1, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{0}}, {.row_count = 1, .col_count = 2});
  const auto out =
      float_matrix_view(out_storage, {.row_count = 1, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{1}});
  constexpr std::array weight{1.0F, 1.0F};
  constexpr std::array bias{0.0F, 0.0F};

  layer_norm(out, in, weight, bias);

  constexpr auto tolerance = 1e-5;
  CHECK(out_storage[0] == -1.0F);
  CHECK_THAT(out_storage[1], WithinAbs(-1.0, tolerance));
  CHECK_THAT(out_storage[2], WithinAbs(1.0, tolerance));
}

TEST_CASE("GELU on hand-computed values", "[Gpt2ForwardTest]") {
  // Reference values from torch's gelu with approximate="tanh". Zero maps to
  // zero, the far tails pass through or vanish, and the negative side dips
  // below zero before it does.
  constexpr auto tolerance = 1e-6;
  CHECK(gelu_new(0.0F) == 0.0F);
  CHECK_THAT(gelu_new(1.0F), WithinAbs(0.841192, tolerance));
  CHECK_THAT(gelu_new(-1.0F), WithinAbs(-0.158808, tolerance));
  CHECK_THAT(gelu_new(2.0F), WithinAbs(1.954598, tolerance));
  CHECK_THAT(gelu_new(-2.0F), WithinAbs(-0.045402, tolerance));
  CHECK_THAT(gelu_new(10.0F), WithinAbs(10.0, tolerance));
  CHECK_THAT(gelu_new(-10.0F), WithinAbs(0.0, tolerance));
}

TEST_CASE("GELU honors the stride of both views", "[Gpt2ForwardTest]") {
  // Two rows of two features inside three-column buffers: the input's rows
  // occupy the first two columns and the output's the last two, so the
  // untouched column of each output row proves the stride is honored.
  const std::vector<float> in_storage{1.0F, -1.0F, 99.0F, 2.0F, 0.0F, 99.0F};
  std::vector<float> out_storage(in_storage.size(), -1.0F);
  const auto in =
      const_float_matrix_view(in_storage, {.row_count = 2, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{0}}, {.row_count = 2, .col_count = 2});
  const auto out =
      float_matrix_view(out_storage, {.row_count = 2, .col_count = 3})
          .subview({row_ndx{0}, col_ndx{1}});

  gelu_new(out, in);

  constexpr auto tolerance = 1e-6;
  CHECK(out_storage[0] == -1.0F);
  CHECK_THAT(out_storage[1], WithinAbs(0.841192, tolerance));
  CHECK_THAT(out_storage[2], WithinAbs(-0.158808, tolerance));
  CHECK(out_storage[3] == -1.0F);
  CHECK_THAT(out_storage[4], WithinAbs(1.954598, tolerance));
  CHECK(out_storage[5] == 0.0F);
}

TEST_CASE("GELU in place", "[Gpt2ForwardTest]") {
  std::vector<float> storage{1.0F, -1.0F, 2.0F, 0.5F};
  std::vector<float> expected(storage.size());
  const float_matrix_view m(storage, {.row_count = 2, .col_count = 2});
  const float_matrix_view out(expected, {.row_count = 2, .col_count = 2});

  gelu_new(out, m);
  gelu_new(m, m);

  CHECK(storage == expected);
}

TEST_CASE("Dot product", "[Gpt2ForwardTest]") {
  constexpr std::array a{1.0F, 2.0F, 3.0F};
  constexpr std::array b{4.0F, 5.0F, 6.0F};
  static_assert(dot(a, b) == 32.0F);
  static_assert(
      dot(std::span<const float>{}, std::span<const float>{}) == 0.0F);
}

TEST_CASE("Softmax row", "[Gpt2ForwardTest]") {
  constexpr auto tolerance = 1e-5;

  // Scores 0 and 0.707 differ by 0.707, so the second gets twice the weight:
  // exp(0.707) is 2.028 times exp(0).
  std::array scores{0.0F, 0.707F};
  softmax_row(scores, scores);
  CHECK_THAT(scores[0], WithinAbs(0.330262, tolerance));
  CHECK_THAT(scores[1], WithinAbs(0.669738, tolerance));

  // Equal scores share equally, even when exp of the raw score would
  // overflow a float.
  std::array huge{1000.0F, 1000.0F, 1000.0F, 1000.0F};
  std::array weights{0.0F, 0.0F, 0.0F, 0.0F};
  softmax_row(weights, huge);
  CHECK(weights == std::array{0.25F, 0.25F, 0.25F, 0.25F});

  // A single score gets all the weight.
  std::array one{-3.0F};
  softmax_row(one, one);
  CHECK(one[0] == 1.0F);
}

TEST_CASE("Attention on three tokens of width two", "[Gpt2ForwardTest]") {
  // The napkin example: three tokens with features [1, 0], [0, 1], [1, 1].
  // The `c_attn` weight copies the features into the query and key columns
  // and swaps them into the value columns, with no bias, so q and k equal
  // the input and v is the input with its columns swapped.
  const std::vector<float> in_storage{1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
  const std::vector<float> c_attn_storage{1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F,
      0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F};
  constexpr std::array no_bias{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
  const const_float_matrix_view in(in_storage,
      {.row_count = 3, .col_count = 2});
  const const_float_matrix_view c_attn(c_attn_storage,
      {.row_count = 2, .col_count = 6});

  std::vector<float> qkv_storage(3UZ * 6);
  const float_matrix_view qkv(qkv_storage, {.row_count = 3, .col_count = 6});
  linear(qkv, in, c_attn, no_bias);
  CHECK(qkv_storage ==
        std::vector<float>{1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F,
            0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F});

  std::vector<float> out_storage(3UZ * 2);
  const float_matrix_view out(out_storage, {.row_count = 3, .col_count = 2});
  std::array<float, 3> scores{};
  constexpr auto tolerance = 1e-5;

  SECTION("one head of width two") {
    // Token 0 sees only itself, so its output is its own value, [0, 1].
    // Token 1 scores itself 0.707 and token 0 zero, for weights 0.670 and
    // 0.330. Token 2 scores itself 1.414 and the others 0.707, for weights
    // 0.503, 0.248, and 0.248.
    attention(out, qkv, 1, scores);
    CHECK_THAT(out_storage[0], WithinAbs(0.0, tolerance));
    CHECK_THAT(out_storage[1], WithinAbs(1.0, tolerance));
    CHECK_THAT(out_storage[2], WithinAbs(0.669762, tolerance));
    CHECK_THAT(out_storage[3], WithinAbs(0.330238, tolerance));
    CHECK_THAT(out_storage[4], WithinAbs(0.751745, tolerance));
    CHECK_THAT(out_storage[5], WithinAbs(0.751745, tolerance));
  }

  SECTION("two heads of width one") {
    // Each head sees one column of q, k, and v. For token 1, head 0 has a
    // zero query, so both scores are zero and it takes half of each value:
    // (0 + 1) / 2. Head 1 scores itself 1 and token 0 zero, for weights
    // 0.731 and 0.269 on values 0 and 1.
    attention(out, qkv, 2, scores);
    CHECK_THAT(out_storage[0], WithinAbs(0.0, tolerance));
    CHECK_THAT(out_storage[1], WithinAbs(1.0, tolerance));
    CHECK_THAT(out_storage[2], WithinAbs(0.5, tolerance));
    CHECK_THAT(out_storage[3], WithinAbs(0.268941, tolerance));
    CHECK_THAT(out_storage[4], WithinAbs(0.577681, tolerance));
    CHECK_THAT(out_storage[5], WithinAbs(0.577681, tolerance));
  }
}

TEST_CASE("Add on hand-computed rows", "[Gpt2ForwardTest]") {
  std::vector<float> a_storage{1.0F, 2.0F, 3.0F, 4.0F};
  std::vector<float> b_storage{10.0F, 20.0F, 30.0F, 40.0F};
  const float_matrix_view a(a_storage, {.row_count = 2, .col_count = 2});
  const float_matrix_view b(b_storage, {.row_count = 2, .col_count = 2});
  const std::vector<float> expected{11.0F, 22.0F, 33.0F, 44.0F};

  SECTION("into a separate matrix") {
    std::vector<float> storage(a.size());
    const float_matrix_view out(storage, a.extent());
    add(out, a, b);
    CHECK(storage == expected);
  }

  SECTION("in place on the left") {
    add(a, a, b);
    CHECK(a_storage == expected);
  }

  SECTION("in place on the right") {
    add(b, a, b);
    CHECK(b_storage == expected);
  }
}

TEST_CASE("Embed on hand-computed rows", "[Gpt2ForwardTest]") {
  // Three tokens in the vocabulary, two positions, width two.
  const std::vector<float> wte_storage{1.0F, 2.0F, 10.0F, 20.0F, 100.0F,
      200.0F};
  const std::vector<float> wpe_storage{0.5F, 0.25F, 0.125F, 0.0625F};
  const const_float_matrix_view wte(wte_storage,
      {.row_count = 3, .col_count = 2});
  const const_float_matrix_view wpe(wpe_storage,
      {.row_count = 2, .col_count = 2});
  const std::vector<token_id> ids{token_id{2}, token_id{0}};

  std::vector<float> storage(ids.size() * 2);
  const float_matrix_view out(storage, {.row_count = 2, .col_count = 2});
  embed(out, ids, wte, wpe);

  CHECK(storage == std::vector<float>{100.5F, 200.25F, 1.125F, 2.0625F});
}

TEST_CASE("Layer norm matches the oracle", "[Gpt2ForwardTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Every layer norm in the model, fed its own dumped input, so no error in
  // an earlier op can reach it.
  struct site {
    std::string dump;
    std::string param;
  };
  std::vector<site> sites;
  for (auto n = 0UZ; n < n_layer; ++n)
    for (const auto* ln : {"ln_1", "ln_2"})
      sites.push_back(
          {std::format("block_{}/{}", n, ln), std::format("h.{}.{}", n, ln)});
  sites.push_back({"ln_f", "ln_f"});

  for (const auto& [dump, param] : sites) {
    DYNAMIC_SECTION(dump) {
      const auto in = matrix_of(oracle.activations, dump + "/in", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/out", n_embd);
      const auto weight = vector_of(oracle.weights, param + ".weight", n_embd);
      const auto bias = vector_of(oracle.weights, param + ".bias", n_embd);
      REQUIRE(in.row_extent() == 14);

      std::vector<float> storage(in.size());
      const float_matrix_view out(storage, in.extent());
      layer_norm(out, in, weight, bias);

      check_close(out, expected, 1e-5F, 1e-5F);
    }
  }
}

TEST_CASE("MLP path matches the oracle", "[Gpt2ForwardTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Every block's MLP, fed its own dumped `ln_2/out` and compared against its
  // dumped `mlp/out`, which is the first point after `c_fc`, `gelu_new`, and
  // `c_proj` that the oracle captures. This gates `linear` as well, since no
  // dump sits between the two projections.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto param = std::format("h.{}.mlp", n);
      const auto in =
          matrix_of(oracle.activations, dump + "/ln_2/out", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/mlp/out", n_embd);
      const auto fc_weight =
          matrix_of(oracle.weights, param + ".c_fc.weight", n_hidden);
      const auto fc_bias =
          vector_of(oracle.weights, param + ".c_fc.bias", n_hidden);
      const auto proj_weight =
          matrix_of(oracle.weights, param + ".c_proj.weight", n_embd);
      const auto proj_bias =
          vector_of(oracle.weights, param + ".c_proj.bias", n_embd);
      REQUIRE(in.row_extent() == 14);

      std::vector<float> hidden_storage(in.row_extent() * n_hidden);
      const float_matrix_view hidden(hidden_storage,
          {.row_count = in.row_extent(), .col_count = n_hidden});
      std::vector<float> out_storage(in.size());
      const float_matrix_view out(out_storage, in.extent());

      linear(hidden, in, fc_weight, fc_bias);
      gelu_new(hidden, hidden);
      linear(out, hidden, proj_weight, proj_bias);

      check_close(out, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Attention path matches the oracle", "[Gpt2ForwardTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Every block's attention, fed its own dumped `ln_1/out` and compared
  // against its dumped `attn/out`, which is the first point after `c_attn`,
  // the heads, and `c_proj` that the oracle captures.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto dump = std::format("block_{}", n);
      const auto param = std::format("h.{}.attn", n);
      const auto in =
          matrix_of(oracle.activations, dump + "/ln_1/out", n_embd);
      const auto expected =
          matrix_of(oracle.activations, dump + "/attn/out", n_embd);
      const auto attn_weight =
          matrix_of(oracle.weights, param + ".c_attn.weight", n_qkv);
      const auto attn_bias =
          vector_of(oracle.weights, param + ".c_attn.bias", n_qkv);
      const auto proj_weight =
          matrix_of(oracle.weights, param + ".c_proj.weight", n_embd);
      const auto proj_bias =
          vector_of(oracle.weights, param + ".c_proj.bias", n_embd);
      const auto token_count = in.row_extent();
      REQUIRE(token_count == 14);

      std::vector<float> qkv_storage(token_count * n_qkv);
      const float_matrix_view qkv(qkv_storage,
          {.row_count = token_count, .col_count = n_qkv});
      std::vector<float> heads_storage(in.size());
      const float_matrix_view heads_out(heads_storage, in.extent());
      std::vector<float> scores(token_count);
      std::vector<float> out_storage(in.size());
      const float_matrix_view out(out_storage, in.extent());

      linear(qkv, in, attn_weight, attn_bias);
      attention(heads_out, qkv, n_head, scores);
      linear(out, heads_out, proj_weight, proj_bias);

      check_close(out, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Residual adds match the oracle", "[Gpt2ForwardTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Both adds of every block, each fed its own dumped operands. Adding two
  // fp32 values is the same IEEE operation here and in the oracle, so the
  // match is exact, with no tolerance at all.
  struct site {
    std::string residual;
    std::string correction;
    std::string sum;
  };
  std::vector<site> sites;
  for (auto n = 0UZ; n < n_layer; ++n) {
    const auto block = std::format("block_{}", n);
    const auto after_mlp =
        (n + 1 < n_layer)
            ? std::format("block_{}/ln_1/in", n + 1)
            : std::string{"ln_f/in"};
    sites.push_back(
        {block + "/ln_1/in", block + "/attn/out", block + "/ln_2/in"});
    sites.push_back({block + "/ln_2/in", block + "/mlp/out", after_mlp});
  }

  for (const auto& [residual, correction, sum] : sites) {
    DYNAMIC_SECTION(sum) {
      const auto a = matrix_of(oracle.activations, residual, n_embd);
      const auto b = matrix_of(oracle.activations, correction, n_embd);
      const auto expected = matrix_of(oracle.activations, sum, n_embd);
      REQUIRE(a.row_extent() == 14);

      std::vector<float> storage(a.size());
      const float_matrix_view out(storage, a.extent());
      add(out, a, b);

      check_close(out, expected, 0.0F, 0.0F);
    }
  }
}

TEST_CASE("Embed matches the oracle", "[Gpt2ForwardTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // The bisect prompt's IDs come from the logits dump, the only place the
  // oracle wrote them. As with the residual add, the sum of two fp32 rows is
  // exact.
  const auto ids =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  REQUIRE(ids.size() == 14);
  const auto wte = matrix_of(oracle.weights, "wte.weight", n_embd);
  const auto wpe = matrix_of(oracle.weights, "wpe.weight", n_embd);
  REQUIRE(wte.row_extent() == n_vocab);
  REQUIRE(wpe.row_extent() == n_ctx);
  const auto expected = matrix_of(oracle.activations, "embed/out", n_embd);

  std::vector<float> storage(ids.size() * n_embd);
  const float_matrix_view out(storage, expected.extent());
  embed(out, ids, wte, wpe);

  check_close(out, expected, 0.0F, 0.0F);
}

TEST_CASE("Block matches the oracle", "[Gpt2ForwardTest][oracle]") {
  oracle_dumps oracle;
  oracle.load();

  // Every block, fed its own dumped residual and compared against the
  // residual that leaves it, which is the next block's `ln_1/in` or, for the
  // last block, `ln_f/in`. The gate is the attention and MLP paths' 1e-4,
  // since both run inside.
  for (auto n = 0UZ; n < n_layer; ++n) {
    DYNAMIC_SECTION("block_" << n) {
      const auto in = matrix_of(oracle.activations,
          std::format("block_{}/ln_1/in", n), n_embd);
      const auto exit =
          (n + 1 < n_layer)
              ? std::format("block_{}/ln_1/in", n + 1)
              : std::string{"ln_f/in"};
      const auto expected = matrix_of(oracle.activations, exit, n_embd);
      const auto params = block_params_of(oracle, n);
      const auto token_count = in.row_extent();
      REQUIRE(token_count == 14);

      std::vector<float> residual_storage(in.as_span().begin(),
          in.as_span().end());
      const float_matrix_view residual(residual_storage, in.extent());
      owned_block_scratch scratch(token_count);
      block(residual, params, scratch.views(), n_head);

      check_close(residual, expected, 1e-4F, 1e-4F);
    }
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
