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

// The model weights and the bisect prompt's activations, as the oracle dumped
// them.
struct oracle_dumps {
  safetensors_file weights;
  safetensors_file activations;

  // Load both files, skipping the test when the oracle has not run here.
  void load() {
    const auto model_path = oracle_path("model.safetensors");
    const auto activations_path = oracle_path("activations.safetensors");
    if (!std::filesystem::exists(model_path) ||
        !std::filesystem::exists(activations_path))
      SKIP("no oracle dumps under tests/.local/llm/gpt2; run the oracle");
    REQUIRE(weights.load(tests::open_read_only(model_path)));
    REQUIRE(activations.load(tests::open_read_only(activations_path)));
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

// NOLINTEND(readability-function-cognitive-complexity)
