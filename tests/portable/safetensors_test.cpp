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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "corvid/cuda/llm/gpt2_tokenizer.h"
#include "corvid/cuda/llm/safetensors.h"
#include "corvid/filesys/os_file.h"
#include "corvid/proto/misc/json_parser.h"
#include "corvid/strings/conversion.h"
#include "catch2_main.h"
#include "test_files.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::llm;
using namespace corvid::strings::conversion;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

using image_t = std::vector<std::byte>;

// The native bytes of `values`, which is little-endian on every target here.
template<typename T>
image_t bytes_of(std::initializer_list<T> values) {
  image_t bytes(values.size() * sizeof(T));
  std::memcpy(bytes.data(), std::data(values), bytes.size());
  return bytes;
}

// A file image: the header length, `header`, then `payload`.
//
// Unless `aligned` is false, the header is padded with spaces so the payload
// starts on an 8-byte boundary, as the reference writer pads it.
image_t make_image(std::string_view header, const image_t& payload = {},
    bool aligned = true) {
  std::string padded{header};
  if (aligned) padded.resize((padded.size() + 7) / 8 * 8, ' ');
  image_t image(8 + padded.size() + payload.size());
  const auto header_size = static_cast<uint64_t>(padded.size());
  std::memcpy(image.data(), &header_size, 8);
  std::memcpy(image.data() + 8, padded.data(), padded.size());
  std::ranges::copy(payload, image.data() + 8 + padded.size());
  return image;
}

// A directory of the GPT-2 artifacts, relative to this source: the committed
// fixtures under "data", or the gitignored oracle dumps under ".local".
std::filesystem::path gpt2_path(std::string_view root, std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path().parent_path() / root /
         "llm" / "gpt2" / name;
}

// The whole of the file at `path`; an unreadable file fails the test.
std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

// A small file: six floats in a 2x3, two ints, and one metadata string.
constexpr auto small_header =
    R"({"a":{"dtype":"F32","shape":[2,3],)"
    R"("data_offsets":[0,24]},)"
    R"("b":{"dtype":"I32","shape":[2],)"
    R"("data_offsets":[24,32]},)"
    R"("__metadata__":{"format":"pt"}})"sv;

image_t small_payload() {
  auto payload = bytes_of<float>({1, 2, 3, 4, 5, 6});
  const auto ints = bytes_of<int32_t>({7, 8});
  payload.insert(payload.end(), ints.begin(), ints.end());
  return payload;
}

} // namespace

#pragma region Format

TEST_CASE("Parse a small file", "[SafetensorsTest]") {
  const auto image = make_image(small_header, small_payload());
  safetensors_file file;
  REQUIRE(file.parse(image));
  CHECK(file.size() == 2);
  CHECK(file.tensors().size() == 2);
  CHECK(file.tensors()[0].name == "a");
  CHECK(file.tensors()[1].name == "b");

  const auto* a = file.find("a");
  REQUIRE(a);
  CHECK(a->dtype == tensor_dtype::f32);
  CHECK(a->shape == std::vector<size_t>{2, 3});
  CHECK(a->count() == 6);
  CHECK(a->bytes.size() == 24);
  REQUIRE(a->is<float>());
  CHECK_FALSE(a->is<double>());
  CHECK_FALSE(a->is<int32_t>());
  const auto values = a->as<float>();
  CHECK(std::vector<float>(values.begin(), values.end()) ==
        std::vector<float>{1, 2, 3, 4, 5, 6});

  const auto* b = file.find("b");
  REQUIRE(b);
  CHECK(b->dtype == tensor_dtype::i32);
  REQUIRE(b->is<int32_t>());
  CHECK(b->as<int32_t>()[1] == 8);

  CHECK_FALSE(file.find("c"));
  REQUIRE(file.metadata().contains("format"));
  CHECK(file.metadata().find("format")->second == "pt");
}

TEST_CASE("Header padding, scalars, and empty tensors", "[SafetensorsTest]") {
  // A scalar has an empty shape, and a tensor may have no bytes at all. The
  // padding is `make_image`'s, spaces after the JSON.
  const auto header =
      R"({"s":{"dtype":"F64","shape":[],"data_offsets":[0,8]},)"
      R"("e":{"dtype":"U8","shape":[0],"data_offsets":[8,8]}})"sv;
  safetensors_file file;
  REQUIRE(file.parse(make_image(header, bytes_of<double>({2.5}))));
  const auto* s = file.find("s");
  REQUIRE(s);
  CHECK(s->shape.empty());
  CHECK(s->count() == 1);
  REQUIRE(s->is<double>());
  CHECK(s->as<double>()[0] == 2.5);
  const auto* e = file.find("e");
  REQUIRE(e);
  CHECK(e->count() == 0);
  CHECK(e->bytes.empty());
  CHECK(file.metadata().empty());
}

TEST_CASE("Rejects malformed files", "[SafetensorsTest]") {
  safetensors_file file;
  REQUIRE(file.parse(make_image(small_header, small_payload())));

  // Every rejection leaves the previous contents in place.
  const auto rejects = [&](std::string_view header, const image_t& payload) {
    const auto image = make_image(header, payload);
    const auto is_rejected = !file.parse(image);
    CHECK(file.size() == 2);
    return is_rejected;
  };
  const auto one_float = bytes_of<float>({1});
  constexpr auto one_float_header =
      R"({"a":{"dtype":"F32","shape":[1],"data_offsets":[0,4]}})"sv;
  if (safetensors_file probe; true)
    CHECK(probe.parse(make_image(one_float_header, one_float)));

  // Too short for a length, a length past the end, a huge length.
  CHECK_FALSE(file.parse(image_t(7)));
  image_t past_end(8);
  past_end[0] = std::byte{9};
  CHECK_FALSE(file.parse(past_end));
  auto huge = make_image("{}", {});
  huge[3] = std::byte{0x10};
  CHECK_FALSE(file.parse(huge));
  CHECK(file.size() == 2);

  // Not JSON, not an object, a tensor that is not an object.
  CHECK(rejects("nope", {}));
  CHECK(rejects("[]", {}));
  CHECK(rejects(R"({"a":7})", {}));

  // Unknown dtype, bad shapes, bad offsets.
  CHECK(rejects(R"({"a":{"dtype":"F128","shape":[1],"data_offsets":[0,4]}})",
      one_float));
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":1,"data_offsets":[0,4]}})",
      one_float));
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[-1],"data_offsets":[0,4]}})",
      one_float));
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[1],"data_offsets":[0]}})",
      one_float));
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[1],"data_offsets":[0,4,4]}})",
      one_float));
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[1],"data_offsets":[4,0]}})",
      one_float));
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[1],"data_offsets":[0,8]}})",
      one_float));
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[2],"data_offsets":[0,4]}})",
      one_float));

  // A duplicate name, a gap, an overlap, and bytes no tensor claims.
  const auto two_floats = bytes_of<float>({1, 2});
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[1],"data_offsets":[0,4]},)"
                R"("a":{"dtype":"F32","shape":[1],"data_offsets":[4,8]}})",
      two_floats));
  const auto three_floats = bytes_of<float>({1, 2, 3});
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[1],"data_offsets":[0,4]},)"
                R"("b":{"dtype":"F32","shape":[1],"data_offsets":[8,12]}})",
      three_floats));
  CHECK(rejects(R"({"a":{"dtype":"F32","shape":[2],"data_offsets":[0,8]},)"
                R"("b":{"dtype":"F32","shape":[2],"data_offsets":[4,12]}})",
      three_floats));
  CHECK(rejects(one_float_header, two_floats));
  CHECK(rejects(R"({"__metadata__":{"format":7}})", {}));
}

TEST_CASE("Typed views require alignment", "[SafetensorsTest]") {
  // The header's length puts the buffer at an offset that is not a multiple
  // of four. The bytes are still there, but not as floats.
  const auto header =
      R"({"a":{"dtype":"F32","shape":[1],"data_offsets":[0,4]}} )"sv;
  REQUIRE((8 + header.size()) % 4 != 0);
  safetensors_file file;
  REQUIRE(file.parse(make_image(header, bytes_of<float>({1}), false)));
  const auto* a = file.find("a");
  REQUIRE(a);
  CHECK(a->bytes.size() == 4);
  CHECK_FALSE(a->is<float>());
  CHECK_FALSE(a->is<int8_t>());
  CHECK(a->dtype == tensor_dtype::f32);
}

TEST_CASE("Load maps a file", "[SafetensorsTest]") {
  const auto image = make_image(small_header, small_payload());
  const tests::temp_file tf{as_string_view(std::span{image})};
  safetensors_file file;
  REQUIRE(file.load(tf.file));
  CHECK(file.size() == 2);
  const auto* a = file.find("a");
  REQUIRE(a);
  REQUIRE(a->is<float>());
  CHECK(a->as<float>()[5] == 6);

  // A closed file fails and leaves the previous contents in place.
  CHECK_FALSE(file.load(os_file{}));
  CHECK(file.size() == 2);
  CHECK(file.find("a")->as<float>()[0] == 1);
}

#pragma endregion
#pragma region GPT-2

TEST_CASE("GPT-2 weights", "[SafetensorsTest]") {
  const auto model_path = gpt2_path(".local", "model.safetensors");
  if (!std::filesystem::exists(model_path))
    SKIP("no " << model_path.string() << "; run the oracle to create it");

  safetensors_file weights;
  REQUIRE(weights.load(tests::open_read_only(model_path)));
  CHECK(weights.size() == 160);
  REQUIRE(weights.metadata().contains("format"));
  CHECK(weights.metadata().find("format")->second == "pt");

  // Every tensor is fp32 and aligned for it, which the oracle's rewrite of
  // the hub's file guarantees.
  for (const auto& entry : weights.tensors()) {
    INFO(entry.name);
    CHECK(entry.dtype == tensor_dtype::f32);
    CHECK(entry.is<float>());
  }

  // The shapes the manifest records: embeddings, and the Conv1D projections
  // with the input dimension first.
  const auto shape_of = [&](std::string_view name) {
    const auto* entry = weights.find(name);
    REQUIRE(entry);
    return entry->shape;
  };
  CHECK(shape_of("wte.weight") == std::vector<size_t>{50257, 768});
  CHECK(shape_of("wpe.weight") == std::vector<size_t>{1024, 768});
  CHECK(shape_of("ln_f.weight") == std::vector<size_t>{768});
  CHECK(shape_of("h.0.attn.c_attn.weight") == std::vector<size_t>{768, 2304});
  CHECK(shape_of("h.11.mlp.c_proj.weight") == std::vector<size_t>{3072, 768});
  CHECK_FALSE(weights.find("h.12.ln_1.weight"));
}

TEST_CASE("GPT-2 embeddings match the oracle", "[SafetensorsTest]") {
  const auto model_path = gpt2_path(".local", "model.safetensors");
  const auto activations_path = gpt2_path(".local", "activations.safetensors");
  if (!std::filesystem::exists(model_path) ||
      !std::filesystem::exists(activations_path))
    SKIP("no oracle dumps under tests/.local/llm/gpt2");

  // The bisect prompt's tokens, from the manifest and the tokenizer.
  const auto manifest_text = read_file(gpt2_path("data", "manifest.json"));
  json_value_view root;
  REQUIRE(parse_json(manifest_text, root));
  const auto manifest = root.as_object();
  const auto bisect = manifest.get_number<size_t>("bisect_prompt");
  REQUIRE(bisect);
  std::string prompt;
  size_t ndx = 0;
  for (const auto item : manifest.get_array("prompts")) {
    if (ndx++ != *bisect) continue;
    REQUIRE(item.as_object().get_string("text", prompt));
  }
  REQUIRE_FALSE(prompt.empty());
  gpt2_tokenizer tok;
  const auto merges = read_file(gpt2_path("data", "merges.txt"));
  const auto merges_span = as_byte_span<char8_t>(merges);
  REQUIRE(tok.load({merges_span.data(), merges_span.size()}));
  std::vector<token_id> ids;
  const auto prompt_span = as_byte_span<char8_t>(prompt);
  REQUIRE(tok.encode(ids, {prompt_span.data(), prompt_span.size()}));
  REQUIRE(ids.size() == 14);

  // The embedding output is the token embedding plus the position embedding.
  safetensors_file weights;
  REQUIRE(weights.load(tests::open_read_only(model_path)));
  safetensors_file activations;
  REQUIRE(activations.load(tests::open_read_only(activations_path)));
  const auto* wte = weights.find("wte.weight");
  const auto* wpe = weights.find("wpe.weight");
  const auto* embed = activations.find("embed/out");
  REQUIRE((wte && wpe && embed));
  REQUIRE(embed->shape == std::vector<size_t>{ids.size(), 768});
  REQUIRE((wte->is<float>() && wpe->is<float>() && embed->is<float>()));
  const auto wte_values = wte->as<float>();
  const auto wpe_values = wpe->as<float>();
  const auto embed_values = embed->as<float>();
  constexpr auto width = 768UZ;
  auto max_error = 0.0F;
  for (auto pos = 0UZ; pos < ids.size(); ++pos) {
    for (auto col = 0UZ; col < width; ++col) {
      const auto expected =
          wte_values[(*ids[pos] * width) + col] +
          wpe_values[(pos * width) + col];
      const auto actual = embed_values[(pos * width) + col];
      max_error = std::max(max_error, std::abs(actual - expected));
    }
  }
  CHECK(max_error < 1e-6F);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
