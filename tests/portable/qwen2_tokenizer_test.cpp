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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "corvid/llm/qwen2_tokenizer.h"
#include "corvid/proto/misc/json_parser.h"
#include "corvid/strings/conversion.h"
#include "corvid/strings/token_parser.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::llm;
using namespace corvid::strings::conversion;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

using ids_t = std::vector<token_id>;
using chunks_t = std::vector<std::string>;
using parser = strings::basic_token_parser<char8_t>;

// The path of the committed Qwen2.5 fixture `name`, relative to this source.
std::filesystem::path fixture_path(std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path().parent_path() / "data" /
         "llm" / "qwen2.5-3b-instruct" / name;
}

// The path of the gitignored Qwen2.5 table `name` the oracle fetched,
// relative to this source.
std::filesystem::path table_path(std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path().parent_path() /
         ".local" / "llm" / "qwen2.5-3b-instruct" / name;
}

// The whole of the file at `path`, failing the test when it cannot be read.
std::string read_whole(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

// The whole of fixture `name`, failing the test when it cannot be read.
std::string read_fixture(std::string_view name) {
  return read_whole(fixture_path(name));
}

// The whole of table `name`, skipping the test when the oracle has not
// fetched it here.
std::string read_table(std::string_view name) {
  const auto path = table_path(name);
  if (!std::filesystem::exists(path))
    SKIP(
        "no tokenizer tables under tests/.local/llm/qwen2.5-3b-instruct; "
        "run the oracle");
  return read_whole(path);
}

// The UTF-8 view of `bytes`.
std::u8string_view as_utf8(std::string_view bytes) {
  const auto span = as_byte_span<char8_t>(bytes);
  return {span.data(), span.size()};
}

// The narrow view of `text`, for legible failure messages.
std::string_view narrow(std::u8string_view text) {
  return as_string_view(std::span<const char8_t>(text.data(), text.size()));
}

// The IDs in a JSON array of numbers.
ids_t ids_of(json_array_view array) {
  ids_t ids;
  for (const auto item : array) {
    const auto value = item.as_number<uint32_t>();
    REQUIRE(value);
    ids.push_back(token_id{*value});
  }
  return ids;
}

// The chunks `split` produces for `text`, narrowed.
chunks_t chunks_of(std::u8string_view text) {
  std::vector<std::u8string_view> chunks;
  REQUIRE(qwen2_tokenizer::split(chunks, text));
  chunks_t narrowed;
  for (const auto chunk : chunks) narrowed.emplace_back(narrow(chunk));
  return narrowed;
}

// The added tokens' texts in ID order, from the committed fixture.
std::vector<std::u8string> added_tokens() {
  const auto text = read_fixture("special_tokens.json");
  json_value_view root;
  REQUIRE(parse_json(text, root));
  std::vector<std::u8string> tokens;
  for (const auto item : root.as_array()) {
    std::string content;
    REQUIRE(item.as_object().get_string("content", content));
    tokens.emplace_back(as_utf8(content));
  }
  REQUIRE(tokens.size() == 22);
  return tokens;
}

// A tokenizer loaded from the fetched tables and the added-token fixture.
qwen2_tokenizer load_fixture_tokenizer() {
  const auto merges = read_table("merges.txt");
  const auto added = added_tokens();
  std::vector<std::u8string_view> added_views(added.begin(), added.end());
  qwen2_tokenizer tok;
  REQUIRE(tok.load(as_utf8(merges), added_views));
  return tok;
}

// The IDs `text` encodes to.
ids_t encode(const qwen2_tokenizer& tok, std::u8string_view text) {
  ids_t ids;
  REQUIRE(tok.encode(ids, text));
  return ids;
}

} // namespace

#pragma region Splitting

TEST_CASE("Split digits one at a time", "[Qwen2TokenizerTest]") {
  CHECK(chunks_of(u8"7 42 1969") ==
        chunks_t{"7", " ", "4", "2", " ", "1", "9", "6", "9"});
  CHECK(chunks_of(u8"x1y2") == chunks_t{"x", "1", "y", "2"});
  CHECK(chunks_of(u8"3.14") == chunks_t{"3", ".", "1", "4"});
}

TEST_CASE("Split letters after one other character", "[Qwen2TokenizerTest]") {
  CHECK(chunks_of(u8"Hello world") == chunks_t{"Hello", " world"});
  CHECK(chunks_of(u8"(word") == chunks_t{"(word"});
  CHECK(chunks_of(u8"\tand") == chunks_t{"\tand"});
  CHECK(chunks_of(u8"((word") == chunks_t{"((", "word"});
  CHECK(chunks_of(u8"...ok") == chunks_t{"...", "ok"});
  CHECK(chunks_of(u8"\nword") == chunks_t{"\n", "word"});
  CHECK(chunks_of(u8"") == chunks_t{});
}

TEST_CASE("Split contractions in either case", "[Qwen2TokenizerTest]") {
  CHECK(chunks_of(u8"it's IT'S they'RE") ==
        chunks_t{"it", "'s", " IT", "'S", " they", "'RE"});
  CHECK(chunks_of(u8"'tis") == chunks_t{"'t", "is"});
  CHECK(chunks_of(u8"rock'n'roll") == chunks_t{"rock", "'n", "'roll"});
  CHECK(chunks_of(u8"'quoted'") == chunks_t{"'quoted", "'"});
}

TEST_CASE("Split punctuation runs with their newlines",
    "[Qwen2TokenizerTest]") {
  CHECK(
      chunks_of(u8"Hello!!!\n\nnext") == chunks_t{"Hello", "!!!\n\n", "next"});
  CHECK(chunks_of(u8" $19.99") == chunks_t{" $", "1", "9", ".", "9", "9"});
  CHECK(chunks_of(u8"x = y;") == chunks_t{"x", " =", " y", ";"});
}

TEST_CASE("Split whitespace runs", "[Qwen2TokenizerTest]") {
  // A run with a newline ends after its last newline.
  CHECK(chunks_of(u8"  \nfoo") == chunks_t{"  \n", "foo"});
  CHECK(chunks_of(u8"\n  foo") == chunks_t{"\n", " ", " foo"});
  CHECK(chunks_of(u8"a \n\n b") == chunks_t{"a", " \n\n", " b"});
  // Without one, the run gives its last code point to the next chunk.
  CHECK(chunks_of(u8"a  b") == chunks_t{"a", " ", " b"});
  CHECK(chunks_of(u8"a   ") == chunks_t{"a", "   "});
  CHECK(chunks_of(u8"a 1") == chunks_t{"a", " ", "1"});
}

TEST_CASE("Split rejects malformed UTF-8", "[Qwen2TokenizerTest]") {
  std::vector<std::u8string_view> chunks;
  CHECK_FALSE(qwen2_tokenizer::split(chunks, u8"\xff"sv));
  CHECK(chunks.empty());
}

#pragma endregion
#pragma region Fixtures

TEST_CASE("Added tokens take their published IDs", "[Qwen2TokenizerTest]") {
  const auto tok = load_fixture_tokenizer();
  CHECK(tok.size() == 151665);
  CHECK(tok.text(token_id{151643}) == u8"<|endoftext|>");
  CHECK(tok.text(token_id{151644}) == u8"<|im_start|>");
  CHECK(tok.text(token_id{151645}) == u8"<|im_end|>");

  const auto ids = encode(tok, u8"<|im_start|>user\nhi<|im_end|>");
  REQUIRE(ids.size() >= 4);
  CHECK(ids.front() == token_id{151644});
  CHECK(ids.back() == token_id{151645});
  std::u8string bytes;
  REQUIRE(tok.decode(bytes, ids));
  CHECK(bytes == u8"<|im_start|>user\nhi<|im_end|>");
}

TEST_CASE("Corpus encodes and decodes as the reference did",
    "[Qwen2TokenizerTest]") {
  const auto tok = load_fixture_tokenizer();
  const auto corpus_text = read_fixture("corpus.txt");
  const auto corpus = as_utf8(corpus_text);
  const auto tokens_text = read_fixture("corpus_tokens.json");
  json_value_view root;
  REQUIRE(parse_json(tokens_text, root));
  const auto tokens = root.as_object();
  const auto full = ids_of(tokens.find("full").as_array());
  REQUIRE(full.size() == 377);

  // The whole file at once.
  CHECK(encode(tok, corpus) == full);

  // Each line on its own, without its newline.
  const auto lines = tokens.find("lines").as_array();
  auto rest = corpus;
  size_t line_ndx = 0;
  for (const auto line_tokens : lines) {
    const auto line = parser::next_delimited(u8'\n', rest);
    INFO("line " << line_ndx << ": " << narrow(line));
    CHECK(encode(tok, line) == ids_of(line_tokens.as_array()));
    ++line_ndx;
  }
  CHECK(line_ndx == 26);
  CHECK(rest.empty());

  // Decoding the reference IDs reproduces the file byte for byte.
  std::u8string bytes;
  REQUIRE(tok.decode(bytes, full));
  CHECK(bytes == corpus);
}

TEST_CASE("Prompts and the chat sample encode as the reference did",
    "[Qwen2TokenizerTest]") {
  const auto tok = load_fixture_tokenizer();
  const auto manifest_text = read_fixture("manifest.json");
  json_value_view root;
  REQUIRE(parse_json(manifest_text, root));
  const auto manifest = root.as_object();

  size_t prompt_ndx = 0;
  for (const auto item : manifest.find("prompts").as_array()) {
    const auto prompt = item.as_object();
    std::string text;
    REQUIRE(prompt.get_string("text", text));
    INFO("prompt " << prompt_ndx << ": " << text);
    CHECK(encode(tok, as_utf8(text)) == ids_of(prompt.find("ids").as_array()));
    ++prompt_ndx;
  }
  CHECK(prompt_ndx == 5);

  // The rendered chat, with its added tokens in the text.
  const auto chat = manifest.find("chat").as_object();
  std::string rendered;
  REQUIRE(chat.get_string("rendered", rendered));
  CHECK(rendered.starts_with("<|im_start|>system\n"));
  CHECK(encode(tok, as_utf8(rendered)) == ids_of(chat.find("ids").as_array()));
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
