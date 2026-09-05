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
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "corvid/cuda/llm/gpt2_tokenizer.h"
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

// The path of the committed GPT-2 fixture `name`, relative to this source.
std::filesystem::path fixture_path(std::string_view name) {
  return std::filesystem::path{__FILE__}.parent_path().parent_path() / "data" /
         "llm" / "gpt2" / name;
}

// The whole of fixture `name`; an unreadable file fails the test.
std::string read_fixture(std::string_view name) {
  std::ifstream in(fixture_path(name), std::ios::binary);
  REQUIRE(in);
  return std::string{std::istreambuf_iterator<char>{in}, {}};
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

// A tokenizer loaded from the fixture tables.
gpt2_tokenizer load_fixture_tokenizer() {
  gpt2_tokenizer tok;
  REQUIRE(tok.load(as_utf8(read_fixture("merges.txt"))));
  return tok;
}

// The IDs in `values`.
ids_t make_ids(std::initializer_list<uint32_t> values) {
  ids_t ids;
  for (const auto value : values) ids.push_back(token_id{value});
  return ids;
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
  REQUIRE(gpt2_tokenizer::split(chunks, text));
  chunks_t narrowed;
  for (const auto chunk : chunks) narrowed.emplace_back(narrow(chunk));
  return narrowed;
}

} // namespace

#pragma region Byte escaping

TEST_CASE("Bytes escape to printable code points", "[Gpt2TokenizerTest]") {
  // The printable Latin-1 bytes escape to themselves.
  CHECK(gpt2_tokenizer::escape_byte('!') == U'!');
  CHECK(gpt2_tokenizer::escape_byte('~') == U'~');
  CHECK(gpt2_tokenizer::escape_byte(0xA1) == U'\u00A1');
  CHECK(gpt2_tokenizer::escape_byte(0xAC) == U'\u00AC');
  CHECK(gpt2_tokenizer::escape_byte(0xAE) == U'\u00AE');
  CHECK(gpt2_tokenizer::escape_byte(0xFF) == U'\u00FF');

  // The other 68 take U+0100 up in byte order, so the space is U+0120 and
  // the soft hyphen is the last.
  CHECK(gpt2_tokenizer::escape_byte(0x00) == U'\u0100');
  CHECK(gpt2_tokenizer::escape_byte(0x20) == U'\u0120');
  CHECK(gpt2_tokenizer::escape_byte(0x7F) == U'\u0121');
  CHECK(gpt2_tokenizer::escape_byte(0x80) == U'\u0122');
  CHECK(gpt2_tokenizer::escape_byte(0xA0) == U'\u0142');
  CHECK(gpt2_tokenizer::escape_byte(0xAD) == U'\u0143');
  static_assert(gpt2_tokenizer::escape_byte(0x20) == U'\u0120');
}

TEST_CASE("Escaped bytes unescape", "[Gpt2TokenizerTest]") {
  for (auto ndx = 0; ndx < 256; ++ndx) {
    const auto byte = static_cast<uint8_t>(ndx);
    const auto escaped = gpt2_tokenizer::escape_byte(byte);
    uint8_t back{};
    REQUIRE(gpt2_tokenizer::unescape_byte(back, escaped));
    CHECK(back == byte);
  }

  // An escaped form that stands for no byte fails and leaves the byte alone.
  for (const auto escaped : {U' ', U'\u00A0', U'\u00AD', U'\u0144', U'\u65E5'})
  {
    uint8_t byte = 0xEE;
    CHECK_FALSE(gpt2_tokenizer::unescape_byte(byte, escaped));
    CHECK(byte == 0xEE);
  }
}

TEST_CASE("Escaped pieces unescape", "[Gpt2TokenizerTest]") {
  std::u8string bytes;
  REQUIRE(gpt2_tokenizer::unescape_piece(bytes, u8"\u0120t"));
  CHECK(narrow(bytes) == " t");

  // A multibyte character comes back as its UTF-8 bytes: e with an acute
  // accent, then a right single quotation mark.
  bytes.clear();
  REQUIRE(gpt2_tokenizer::unescape_piece(bytes, u8"\u00C3\u00A9"));
  CHECK(narrow(bytes) == "\xC3\xA9");
  bytes.clear();
  REQUIRE(gpt2_tokenizer::unescape_piece(bytes, u8"\u00E2\u0122\u013B"));
  CHECK(narrow(bytes) == "\xE2\x80\x99");

  // Appending to what is there, and leaving it alone on failure.
  bytes = u8"x";
  REQUIRE(gpt2_tokenizer::unescape_piece(bytes, u8"y"));
  CHECK(narrow(bytes) == "xy");
  CHECK_FALSE(gpt2_tokenizer::unescape_piece(bytes, u8"a b"));
  CHECK_FALSE(gpt2_tokenizer::unescape_piece(bytes, u8"a\xC3"));
  CHECK(narrow(bytes) == "xy");
}

#pragma endregion
#pragma region Splitting

TEST_CASE("Split words with their leading space", "[Gpt2TokenizerTest]") {
  CHECK(chunks_of(u8"Hello world") == chunks_t{"Hello", " world"});
  CHECK(chunks_of(u8" Hello") == chunks_t{" Hello"});
  CHECK(chunks_of(u8"In 1969, humans") ==
        chunks_t{"In", " 1969", ",", " humans"});
  CHECK(chunks_of(u8"fox.") == chunks_t{"fox", "."});
  CHECK(chunks_of(u8"a...b") == chunks_t{"a", "...", "b"});
  CHECK(chunks_of(u8"x = y;") == chunks_t{"x", " =", " y", ";"});
  CHECK(chunks_of(u8"") == chunks_t{});
}

TEST_CASE("Split contractions", "[Gpt2TokenizerTest]") {
  CHECK(chunks_of(u8"it's") == chunks_t{"it", "'s"});
  CHECK(chunks_of(u8"they're we've I'm you'll he'd don't") ==
        chunks_t{"they", "'re", " we", "'ve", " I", "'m", " you", "'ll", " he",
            "'d", " don", "'t"});

  // Only the lowercase suffixes count, and only right after the apostrophe.
  CHECK(chunks_of(u8"'S' 'tis") == chunks_t{"'", "S", "'", " '", "tis"});
  CHECK(chunks_of(u8"'quoted'") == chunks_t{"'", "quoted", "'"});
  CHECK(chunks_of(u8"rock'n'roll") == chunks_t{"rock", "'", "n", "'", "roll"});
}

TEST_CASE("Split whitespace runs", "[Gpt2TokenizerTest]") {
  // A run before a word gives its last space to the word.
  CHECK(chunks_of(u8"a   b") == chunks_t{"a", "  ", " b"});
  CHECK(chunks_of(u8"a  b") == chunks_t{"a", " ", " b"});

  // A run at the end keeps everything, and a single non-space whitespace
  // character before a word stands alone.
  CHECK(chunks_of(u8"a   ") == chunks_t{"a", "   "});
  CHECK(chunks_of(u8"a \nb") == chunks_t{"a", " ", "\n", "b"});
  CHECK(chunks_of(u8"a\n\nb") == chunks_t{"a", "\n", "\n", "b"});
  CHECK(chunks_of(u8"a\n\n\nb") == chunks_t{"a", "\n\n", "\n", "b"});
  CHECK(chunks_of(u8"\tword") == chunks_t{"\t", "word"});
  CHECK(chunks_of(u8"    if x:\n") == chunks_t{"   ", " if", " x", ":", "\n"});

  // Unicode whitespace is whitespace too, though only U+0020 leads a word.
  CHECK(chunks_of(u8"a\u00A0b") == chunks_t{"a", "\xC2\xA0", "b"});
  CHECK(chunks_of(u8"a\u3000 b") == chunks_t{"a", "\xE3\x80\x80", " b"});
}

TEST_CASE("Split Unicode letters, numbers, and symbols",
    "[Gpt2TokenizerTest]") {
  CHECK(chunks_of(u8"na\u00EFve caf\u00E9") ==
        chunks_t{"na\xC3\xAFve", " caf\xC3\xA9"});
  CHECK(chunks_of(u8"\u65E5\u672C\u8A9E") ==
        chunks_t{"\xE6\x97\xA5\xE6\x9C"
                 "\xAC\xE8\xAA\x9E"});
  CHECK(chunks_of(u8"\u0663\u0664") == chunks_t{"\xD9\xA3\xD9\xA4"});
  CHECK(chunks_of(u8"go \U0001F680!") == chunks_t{"go", " \xF0\x9F\x9A\x80!"});
}

TEST_CASE("Split rejects malformed UTF-8", "[Gpt2TokenizerTest]") {
  std::vector<std::u8string_view> chunks{u8"kept"};
  CHECK_FALSE(gpt2_tokenizer::split(chunks, u8"ab\xC3"));
  CHECK_FALSE(gpt2_tokenizer::split(chunks, u8"\x80"));
  CHECK_FALSE(gpt2_tokenizer::split(chunks, u8"a \xFFz"));
  CHECK(chunks.size() == 1);
}

#pragma endregion
#pragma region Loading

TEST_CASE("Load a small merge table", "[Gpt2TokenizerTest]") {
  gpt2_tokenizer tok;
  CHECK(tok.size() == 0);

  // The header is optional, a trailing carriage return is ignored, and a
  // merge may build on an earlier one.
  REQUIRE(tok.load(u8"#version: 0.2\n\u0120 t\n\u0120t h\r\n"));
  CHECK(tok.size() == 258);
  CHECK(narrow(tok.piece(token_id{256})) == " t");
  CHECK(narrow(tok.piece(token_id{257})) == " th");
  REQUIRE(tok.load(u8"\u0120 t"));
  CHECK(tok.size() == 257);

  // A failed load leaves the previous table in place.
  CHECK_FALSE(tok.load(u8"\u0120 \u0120t\n"));
  CHECK_FALSE(tok.load(u8"\u0120 t\n\u0120 t\n"));
  CHECK_FALSE(tok.load(u8"\u0120\n"));
  CHECK_FALSE(tok.load(u8"\u0120 t h\n"));
  CHECK_FALSE(tok.load(u8"\u0120 t\n\n"));
  CHECK_FALSE(tok.load(u8"a \xC3\n"));
  CHECK(tok.size() == 257);
  CHECK(narrow(tok.piece(token_id{256})) == " t");
}

TEST_CASE("Encode and decode with a small merge table",
    "[Gpt2TokenizerTest]") {
  gpt2_tokenizer tok;
  REQUIRE(tok.load(u8"\u0120 t\n\u0120t h\nt h\nth e\n"));

  // Within a chunk, the lowest rank wins each round: " the" merges as " t"
  // (rank 0) before "th" (rank 2) could form, then " th" (rank 1).
  ids_t ids;
  REQUIRE(tok.encode(ids, u8"the the"));
  CHECK(ids == make_ids({259, 257, 68}));

  // Merges never cross chunk boundaries, and unmerged bytes come out as
  // their byte IDs.
  ids.clear();
  REQUIRE(tok.encode(ids, u8"t he"));
  CHECK(ids == make_ids({83, 220, 71, 68}));

  std::u8string bytes;
  REQUIRE(tok.decode(bytes, make_ids({259, 257, 68})));
  CHECK(narrow(bytes) == "the the");

  // An unknown ID fails and leaves the output alone.
  CHECK_FALSE(tok.decode(bytes, make_ids({260})));
  CHECK(narrow(bytes) == "the the");
}

TEST_CASE("Encode rejects malformed UTF-8", "[Gpt2TokenizerTest]") {
  gpt2_tokenizer tok;
  auto ids = make_ids({7});
  CHECK_FALSE(tok.encode(ids, u8"ab\xC3"));
  CHECK(ids == make_ids({7}));
}

#pragma endregion
#pragma region Fixtures

TEST_CASE("Vocabulary matches vocab.json", "[Gpt2TokenizerTest]") {
  const auto tok = load_fixture_tokenizer();
  const auto vocab_text = read_fixture("vocab.json");
  json_value_view root;
  REQUIRE(parse_json(vocab_text, root));
  const auto vocab = root.as_object();
  REQUIRE(vocab);

  // Every entry but the end-of-text marker is a piece at the ID the file
  // gives it, which pins both the byte numbering and ID = 256 + rank.
  size_t entries = 0;
  std::string key;
  std::u8string bytes;
  for (const auto [key_view, value] : vocab) {
    ++entries;
    REQUIRE(key_view.decode_string(key));
    const auto id_value = value.as_number<uint32_t>();
    REQUIRE(id_value);
    const token_id id{*id_value};
    if (key == "<|endoftext|>") {
      CHECK(*id == 50256);
      continue;
    }
    bytes.clear();
    REQUIRE(gpt2_tokenizer::unescape_piece(bytes, as_utf8(key)));
    REQUIRE(*id < tok.size());
    CHECK(narrow(tok.piece(id)) == narrow(bytes));
  }
  CHECK(entries == 50257);
  CHECK(tok.size() == 50256);

  // Spot checks of the byte numbering.
  CHECK(narrow(tok.piece(token_id{0})) == "!");
  CHECK(narrow(tok.piece(token_id{187})) == "\xFF");
  CHECK(narrow(tok.piece(token_id{188})) == "\0"sv);
  CHECK(narrow(tok.piece(token_id{220})) == " ");
  CHECK(narrow(tok.piece(token_id{255})) == "\xAD");
}

TEST_CASE("Corpus encodes and decodes as the reference did",
    "[Gpt2TokenizerTest]") {
  const auto tok = load_fixture_tokenizer();
  const auto corpus_text = read_fixture("corpus.txt");
  const auto corpus = as_utf8(corpus_text);
  const auto tokens_text = read_fixture("corpus_tokens.json");
  json_value_view root;
  REQUIRE(parse_json(tokens_text, root));
  const auto tokens = root.as_object();
  const auto full = ids_of(tokens.find("full").as_array());
  REQUIRE(full.size() == 1375);

  // The whole file at once.
  ids_t ids;
  REQUIRE(tok.encode(ids, corpus));
  CHECK(ids == full);

  // Each line on its own, without its newline.
  const auto lines = tokens.find("lines").as_array();
  auto rest = corpus;
  size_t line_ndx = 0;
  for (const auto line_tokens : lines) {
    const auto line = parser::next_delimited(u8'\n', rest);
    ids.clear();
    REQUIRE(tok.encode(ids, line));
    INFO("line " << line_ndx << ": " << narrow(line));
    CHECK(ids == ids_of(line_tokens.as_array()));
    ++line_ndx;
  }
  CHECK(line_ndx == 85);
  CHECK(rest.empty());

  // Decoding the reference IDs reproduces the file byte for byte.
  std::u8string bytes;
  REQUIRE(tok.decode(bytes, full));
  CHECK(bytes == corpus);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
