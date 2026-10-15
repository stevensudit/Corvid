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
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "corvid/llm/qwen2.h"
#include "corvid/proto/misc/json_parser.h"
#include "catch2_main.h"
#include "qwen2_oracle.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::tests::qwen2;
using corvid::llm::token_id;
using matrix_types::row_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// The number `key` of the manifest's `config` object, which must be there.
template<typename T>
T manifest_number(json_object_view config, std::string_view key) {
  const auto value = config.get_number<T>(key);
  REQUIRE(value);
  return *value;
}

} // namespace

#pragma region Config

TEST_CASE("Config matches the manifest", "[Qwen2ModelTest][oracle]") {
  const auto config = load_config();

  // The committed manifest records the same keys from the same file, as the
  // oracle read them.
  const auto manifest_text = read_file(fixture_path("manifest.json"));
  json_value_view root;
  REQUIRE(parse_json(manifest_text, root));
  const auto manifest = root.as_object().get_object("config");
  REQUIRE(manifest);
  CHECK(
      config.hidden_size == manifest_number<size_t>(manifest, "hidden_size"));
  CHECK(config.intermediate_size ==
        manifest_number<size_t>(manifest, "intermediate_size"));
  CHECK(config.num_hidden_layers ==
        manifest_number<size_t>(manifest, "num_hidden_layers"));
  CHECK(config.num_attention_heads ==
        manifest_number<size_t>(manifest, "num_attention_heads"));
  CHECK(config.num_key_value_heads ==
        manifest_number<size_t>(manifest, "num_key_value_heads"));
  CHECK(config.vocab_size == manifest_number<size_t>(manifest, "vocab_size"));
  CHECK(config.max_position_embeddings ==
        manifest_number<size_t>(manifest, "max_position_embeddings"));
  CHECK(config.rms_norm_eps ==
        manifest_number<double>(manifest, "rms_norm_eps"));
  CHECK(config.rope_theta == manifest_number<double>(manifest, "rope_theta"));
  CHECK(*config.bos_token_id ==
        manifest_number<uint32_t>(manifest, "bos_token_id"));
  CHECK(*config.eos_token_id ==
        manifest_number<uint32_t>(manifest, "eos_token_id"));
  const auto tied = manifest.get_bool("tie_word_embeddings");
  REQUIRE(tied);
  CHECK(config.tie_word_embeddings == *tied);

  // The derived dimensions of Qwen2.5-3B.
  CHECK(config.head_dim() == 128);
  CHECK(config.kv_width() == 256);
}

TEST_CASE("Config rejects a missing or mistyped key", "[Qwen2ModelTest]") {
  // Whole, then with one key gone, then with one key of the wrong type.
  constexpr auto whole =
      R"({"hidden_size": 8, "intermediate_size": 16, "num_hidden_layers": 2,
          "num_attention_heads": 2, "num_key_value_heads": 1,
          "vocab_size": 32, "max_position_embeddings": 64,
          "rms_norm_eps": 1e-6, "rope_theta": 10000.0,
          "tie_word_embeddings": true, "bos_token_id": 0,
          "eos_token_id": 1})"sv;
  const auto config = llm::qwen2_config::parse(whole);
  CHECK(config.hidden_size == 8);
  CHECK(config.head_dim() == 4);
  CHECK(config.kv_width() == 4);
  CHECK(config.tie_word_embeddings);
  CHECK(config.eos_token_id == token_id{1});

  CHECK_THROWS_AS(llm::qwen2_config::parse(R"({"hidden_size": 8})"),
      std::runtime_error);
  std::string mistyped{whole};
  mistyped.replace(mistyped.find("true"), 4, "\"yes\"");
  CHECK_THROWS_AS(llm::qwen2_config::parse(mistyped), std::runtime_error);
  CHECK_THROWS_AS(llm::qwen2_config::parse("[]"), std::runtime_error);
}

#pragma endregion
#pragma region Model

TEST_CASE("Model views every parameter of the shards",
    "[Qwen2ModelTest][oracle]") {
  const auto model = load_model();
  const auto& config = model.config;
  const auto width = config.hidden_size;
  const auto kv_width = config.kv_width();
  const auto hidden_width = config.intermediate_size;

  CHECK(model.shards.size() == 2);
  CHECK(model.blocks.size() == config.num_hidden_layers);
  CHECK(model.embed_tokens.row_extent() == config.vocab_size);
  CHECK(model.embed_tokens.col_extent() == width);
  CHECK(model.norm_weight.size() == width);

  // The head is the embedding itself, since the configuration ties them.
  REQUIRE(config.tie_word_embeddings);
  CHECK(model.lm_head.as_span().data() == model.embed_tokens.as_span().data());

  // Every block has the shapes its block comment states, with the keys and
  // values narrower than the queries.
  for (const auto& [n, block] : std::views::enumerate(model.blocks)) {
    INFO("block_" << n);
    CHECK(block.input_layernorm_weight.size() == width);
    CHECK(block.q_proj_weight.row_extent() == width);
    CHECK(block.q_proj_weight.col_extent() == width);
    CHECK(block.q_proj_bias.size() == width);
    CHECK(block.k_proj_weight.row_extent() == kv_width);
    CHECK(block.k_proj_weight.col_extent() == width);
    CHECK(block.k_proj_bias.size() == kv_width);
    CHECK(block.v_proj_weight.row_extent() == kv_width);
    CHECK(block.v_proj_weight.col_extent() == width);
    CHECK(block.v_proj_bias.size() == kv_width);
    CHECK(block.o_proj_weight.row_extent() == width);
    CHECK(block.o_proj_weight.col_extent() == width);
    CHECK(block.post_attention_layernorm_weight.size() == width);
    CHECK(block.gate_proj_weight.row_extent() == hidden_width);
    CHECK(block.gate_proj_weight.col_extent() == width);
    CHECK(block.up_proj_weight.row_extent() == hidden_width);
    CHECK(block.up_proj_weight.col_extent() == width);
    CHECK(block.down_proj_weight.row_extent() == width);
    CHECK(block.down_proj_weight.col_extent() == hidden_width);
  }

  // A view points into its shard. The embedding's first row starts where
  // the file's bytes for the tensor do.
  const safetensors_file::tensor* entry{};
  for (const auto& shard : model.shards)
    if (!entry) entry = shard.find("model.embed_tokens.weight");
  REQUIRE(entry);
  CHECK(static_cast<const void*>(model.embed_tokens[row_ndx{0}].data()) ==
        static_cast<const void*>(entry->bytes.data()));
}

TEST_CASE("Model rejects shards without its tensors", "[Qwen2ModelTest]") {
  // A valid file with no tensors, which is the 8-byte header length, then
  // `{}`.
  constexpr auto image = "\x02\x00\x00\x00\x00\x00\x00\x00{}"sv;
  std::vector<safetensors_file> shards;
  shards.push_back(safetensors_file::parse(std::as_bytes(std::span{image})));
  const auto config = llm::qwen2_config::parse(
      R"({"hidden_size": 8, "intermediate_size": 16, "num_hidden_layers": 1,
          "num_attention_heads": 2, "num_key_value_heads": 1,
          "vocab_size": 32, "max_position_embeddings": 64,
          "rms_norm_eps": 1e-6, "rope_theta": 10000.0,
          "tie_word_embeddings": true, "bos_token_id": 0,
          "eos_token_id": 1})");
  CHECK_THROWS_AS(qwen2_model<bfloat16_t>::load(config, std::move(shards)),
      std::runtime_error);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
