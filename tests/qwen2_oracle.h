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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "corvid/llm/qwen2/qwen2.h"
#include "corvid/llm/safetensors.h"
#include "corvid/math/bfloat16.h"
#include "test_files.h"

// The Qwen2.5-3B-Instruct oracle for the model tests.
//
// It knows where the dumps and fixtures are, and loads the configuration and
// the model from the dumps.
namespace corvid::tests::qwen2 {

using corvid::llm::qwen2_config;
using corvid::llm::qwen2_model;
using corvid::llm::safetensors_file;

#pragma region Oracle

// The path of one gitignored oracle dump under
// tests/.local/llm/qwen2.5-3b-instruct.
inline std::filesystem::path oracle_path(std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path() / ".local" / "llm" /
         "qwen2.5-3b-instruct" / name;
}

// The path of one committed fixture under tests/data/llm/qwen2.5-3b-instruct.
inline std::filesystem::path fixture_path(std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path() / "data" / "llm" /
         "qwen2.5-3b-instruct" / name;
}

// The whole of the file at `path`, where an unreadable file fails the test.
inline std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

// The configuration, parsed from the "config.json" the oracle fetched,
// skipping the test when it is not here.
inline qwen2_config load_config() {
  const auto path = oracle_path("config.json");
  if (!std::filesystem::exists(path))
    SKIP(
        "no config under tests/.local/llm/qwen2.5-3b-instruct; run the "
        "oracle");
  return qwen2_config::parse(read_file(path));
}

// The weights shards the oracle fetched, in name order, skipping the test
// when there are none.
inline std::vector<safetensors_file> load_shards() {
  std::vector<std::filesystem::path> paths;
  const auto dir = oracle_path("");
  if (std::filesystem::exists(dir))
    for (const auto& entry : std::filesystem::directory_iterator{dir}) {
      const auto name = entry.path().filename().string();
      if (name.starts_with("model-") && name.ends_with(".safetensors"))
        paths.push_back(entry.path());
    }
  if (paths.empty())
    SKIP(
        "no weights under tests/.local/llm/qwen2.5-3b-instruct; run the "
        "oracle");
  std::ranges::sort(paths);
  std::vector<safetensors_file> shards;
  shards.reserve(paths.size());
  for (const auto& path : paths)
    shards.push_back(safetensors_file::load(tests::open_read_only(path)));
  return shards;
}

// The model over the dumps, skipping the test when they are not here.
inline qwen2_model<bfloat16_t> load_model() {
  return qwen2_model<bfloat16_t>::load(load_config(), load_shards());
}

#pragma endregion

} // namespace corvid::tests::qwen2
