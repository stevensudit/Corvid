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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "corvid/containers/utils/matrix_view.h"
#include "corvid/llm/gpt2_tokenizer.h"
#include "corvid/llm/safetensors.h"
#include "corvid/llm/token_id.h"
#include "corvid/proto/misc/json_parser.h"
#include "corvid/strings/conversion.h"
#include "test_files.h"

// The GPT-2 oracle for the forward-pass tests, CPU and device alike: where
// the dumps and fixtures are, how to view their tensors, the allclose
// comparison, the greedy continuation from the manifest, and the model's
// dimensions.

namespace corvid::tests::gpt2 {

using corvid::llm::gpt2_tokenizer;
using corvid::llm::safetensors_file;
using corvid::llm::token_id;

#pragma region Oracle

// The path of one gitignored oracle dump under tests/.local/llm/gpt2.
inline std::filesystem::path oracle_path(std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path() / ".local" / "llm" /
         "gpt2" / name;
}

// The path of one committed fixture under tests/data/llm/gpt2.
inline std::filesystem::path fixture_path(std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path() / "data" / "llm" /
         "gpt2" / name;
}

// The whole of the file at `path`, where an unreadable file fails the test.
inline std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

// The model weights, the bisect prompt's activations, and every prompt's IDs
// and logits, along with the stress prompt's IDs and the last row of its
// logits, as the oracle dumped them.
struct oracle_dumps {
  safetensors_file weights;
  safetensors_file activations;
  safetensors_file logits;

  // Load the files, skipping the test when the oracle has not run here.
  [[nodiscard]] static oracle_dumps load() {
    const auto model_path = oracle_path("model.safetensors");
    const auto activations_path = oracle_path("activations.safetensors");
    const auto logits_path = oracle_path("logits.safetensors");
    if (!std::filesystem::exists(model_path) ||
        !std::filesystem::exists(activations_path) ||
        !std::filesystem::exists(logits_path))
      SKIP("no oracle dumps under tests/.local/llm/gpt2; run the oracle");
    return {
        .weights = safetensors_file::load(tests::open_read_only(model_path)),
        .activations =
            safetensors_file::load(tests::open_read_only(activations_path)),
        .logits = safetensors_file::load(tests::open_read_only(logits_path)),
    };
  }
};

// The fp32 tensor `name` of `file`, which must be two-dimensional with `cols`
// columns, as a matrix view.
inline float_matrix_view
matrix_of(const safetensors_file& file, std::string_view name, size_t cols) {
  INFO(name);
  const auto view = file.find_matrix<float>(name, {.col_count = cols});
  REQUIRE(!view.empty());
  return view;
}

// The fp32 tensor `name` of `file`, which must be one-dimensional with `size`
// elements.
inline std::span<const float>
vector_of(const safetensors_file& file, std::string_view name, size_t size) {
  INFO(name);
  const auto span = file.find_vector<float>(name, size);
  REQUIRE(!span.empty());
  return span;
}

// The int32 tensor `name` of `file`, which must be one-dimensional, as token
// IDs.
inline std::vector<token_id>
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
  // The largest expected magnitude, the scale the absolute error is against.
  float max_magnitude{};
};

// Compare `actual` to `expected`, which must have the same extent.
inline closeness compare(float_matrix_view actual, float_matrix_view expected,
    float atol, float rtol) {
  REQUIRE(actual.extent() == expected.extent());
  closeness result;
  for (const auto row : actual.row_indexes()) {
    const auto actual_row = actual[row];
    const auto expected_row = expected[row];
    for (auto const col : actual.col_indexes()) {
      const auto magnitude = std::abs(expected_row[col]);
      const auto abs_error = std::abs(actual_row[col] - expected_row[col]);
      result.max_abs_error = std::max(result.max_abs_error, abs_error);
      result.max_magnitude = std::max(result.max_magnitude, magnitude);
      if (magnitude != 0.0F)
        result.max_rel_error =
            std::max(result.max_rel_error, abs_error / magnitude);
      if (abs_error > atol + (rtol * magnitude)) ++result.violations;
    }
  }
  return result;
}

// Check that `actual` is close to `expected`, reporting the largest errors.
inline void check_close(float_matrix_view actual, float_matrix_view expected,
    float atol, float rtol) {
  const auto result = compare(actual, expected, atol, rtol);
  INFO("max abs error "
       << result.max_abs_error << " against a largest " << result.max_magnitude
       << ", max rel error " << result.max_rel_error);
  CHECK(result.violations == 0);
}

// Check that the largest error of `actual` against `expected` is within
// `ratio` of the largest expected magnitude, for activations whose scale is
// set by a few large values.
inline void check_close_to_scale(float_matrix_view actual,
    float_matrix_view expected, float ratio) {
  const auto result = compare(actual, expected, 0.0F, 0.0F);
  INFO("max abs error " << result.max_abs_error << " against a largest "
                        << result.max_magnitude);
  CHECK(result.max_abs_error <= ratio * result.max_magnitude);
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

#pragma region Greedy

// The oracle's greedy continuation of one prompt, as the manifest records
// it: the prompt's index, the IDs appended to it, and their text.
struct greedy_continuation {
  size_t prompt{};
  std::vector<token_id> ids;
  std::string text;
};

// Read the greedy continuation from the manifest fixture.
inline greedy_continuation read_greedy_continuation() {
  const auto manifest_text = read_file(fixture_path("manifest.json"));
  json_value_view root;
  REQUIRE(parse_json(manifest_text, root));
  const auto spec = root.as_object().get_object("greedy");
  greedy_continuation result;
  const auto prompt = spec.get_number<size_t>("prompt");
  REQUIRE(prompt);
  result.prompt = *prompt;
  for (const auto item : spec.get_array("tokens")) {
    const auto id = item.as_number<uint32_t>();
    REQUIRE(id);
    result.ids.push_back(token_id{*id});
  }
  REQUIRE(spec.get_string("text", result.text));
  return result;
}

// Decode `ids` to text through the tokenizer, loaded from the merges fixture.
inline std::string decode_ids(std::span<const token_id> ids) {
  gpt2_tokenizer tok;
  const auto merges = read_file(fixture_path("merges.txt"));
  const auto merges_span =
      corvid::strings::conversion::as_byte_span<char8_t>(merges);
  REQUIRE(tok.load({merges_span.data(), merges_span.size()}));
  std::u8string bytes;
  REQUIRE(tok.decode(bytes, ids));
  return {bytes.begin(), bytes.end()};
}

#pragma endregion

} // namespace corvid::tests::gpt2
