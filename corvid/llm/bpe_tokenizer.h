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
#include <array>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../containers/core/opt_find.h"
#include "../containers/core/transparent.h"
#include "../containers/utils/string_arena.h"
#include "../containers/utils/interval.h"
#include "../enums/sequence_enum.h"
#include "../meta/bool_enums.h"
#include "../strings/cases.h"
#include "../strings/conversion.h"
#include "../strings/token_parser.h"
#include "../strings/unicode.h"
#include "token_id.h"

// Byte-level Byte-Pair Encoding (BPE) over a loaded merge table, the
// tokenizer family that GPT-2 and Qwen2 belong to.
//
// `bpe_tokenizer<Rule>` is the whole tokenizer but for the rule that splits
// text into the chunks merges never cross, which `Rule::split` supplies (see
// "gpt2_tokenizer.h" and "qwen2_tokenizer.h"). It loads the merge table from
// the text of "merges.txt", plus any added tokens, and then encodes UTF-8 text
// to token IDs and decodes IDs back to bytes. The published tables store every
// byte in an escaped form that keeps it printable (see
// `implementation::escaped_bytes_lookup`), but `load` unescapes each piece
// once, and nothing else touches the escaped form.
//
// Loading:
//   gpt2_tokenizer tok;
//   if (!tok.load(merges_text)) ...
//
// Encoding and decoding:
//   std::vector<token_id> ids;
//   if (!tok.encode(ids, u8"Hello world")) ...
//   std::u8string bytes;
//   if (!tok.decode(bytes, ids)) ...

namespace corvid::llm {

using namespace strings::unicode;
using strings::unicode::classifier::code_point_class;

#pragma region implementation

namespace implementation {

// Whether `byte` is its own escaped form.
//
// These are the printable Latin-1 bytes (not UTF-8). They're ASCII from '!' to
// '~', and the Latin-1 Supplement, without the no-break space and the soft
// hyphen.
[[nodiscard]] constexpr bool is_printable_latin1(uint8_t byte) noexcept {
  return (byte >= 0x21 && byte <= 0x7E) || (byte >= 0xA1 && byte <= 0xAC) ||
         byte >= 0xAE;
}

// Generate a lookup table for the escaped form, providing the `char32_t` code
// point for a byte.
//
// This is the encoding used in "merges.txt" and "vocab.json" to ensure that
// all characters are printable. Therefore, the printable Latin-1 bytes are
// unescaped (mapping to themselves) while the other 68 bytes are assigned the
// code points from U+0100 up, in order.
[[nodiscard]] consteval std::array<char32_t, 256> make_escaped_bytes_lookup() {
  std::array<char32_t, 256> table{};
  auto next = U'\u0100';
  for (char32_t byte = 0; byte < 256; ++byte)
    table[byte] =
        is_printable_latin1(static_cast<uint8_t>(byte)) ? byte : next++;
  return table;
}
inline constexpr auto escaped_bytes_lookup = make_escaped_bytes_lookup();
inline constexpr auto max_escaped_byte =
    std::ranges::max(escaped_bytes_lookup);

// Generate a lookup table for the byte behind each `char32_t` escaped form, up
// to `max_escaped_byte`; the inverse of `escaped_bytes_lookup`.
//
// An impossible escaped form, which is a non-printable value that should have
// been mapped to the U+0100 range, contains a 0. Only the escaped form of byte
// 0, which is U+0100, legitimately unescapes to 0, which is how
// `unescape_byte` tells the two apart.
[[nodiscard]] consteval auto make_unescaped_bytes_lookup() {
  std::array<uint8_t, max_escaped_byte + 1> table{};
  for (const auto byte : iota(256))
    table[escaped_bytes_lookup[byte]] = static_cast<uint8_t>(byte);
  return table;
}
inline constexpr auto unescaped_bytes_lookup = make_unescaped_bytes_lookup();

// The count of bytes that are their own escaped form.
[[nodiscard]] consteval uint32_t count_printable_latin1() {
  uint32_t count{};
  for (const auto byte : iota(256))
    if (is_printable_latin1(static_cast<uint8_t>(byte))) ++count;
  return count;
}

// Generate a lookup table from each byte to the `token_id` of its single-byte
// piece; the inverse of `id_bytes_lookup`.
//
// These single-byte pieces are the individual bytes that remain after merging
// is complete, ensuring that all inputs can be represented. They are assigned
// `token_id`s in ascending order of their escaped forms.
[[nodiscard]] consteval std::array<token_id, 256> make_byte_ids_lookup() {
  std::array<token_id, 256> ids{};
  uint32_t next_printable{};
  auto next_other = count_printable_latin1();
  // Assigns a `token_id` to each byte, with printable Latin-1 bytes first and
  // the rest following.
  for (const auto byte : iota(256)) {
    ids[byte] = token_id{
        is_printable_latin1(static_cast<uint8_t>(byte))
            ? next_printable++
            : next_other++};
  }
  return ids;
}
inline constexpr auto byte_ids_lookup = make_byte_ids_lookup();

// Generate the inverse lookup table, from each `token_id` below 256 to its
// byte.
[[nodiscard]] consteval std::array<uint8_t, 256> make_id_bytes_lookup() {
  std::array<uint8_t, 256> bytes{};
  for (const auto byte : iota(256))
    bytes[*byte_ids_lookup[byte]] = static_cast<uint8_t>(byte);
  return bytes;
}
inline constexpr auto id_bytes_lookup = make_id_bytes_lookup();

#pragma endregion
#pragma region Scanning

// The length of the contraction suffix (s, t, re, ve, m, ll, d) at the front
// of `rest`, or 0.
//
// When `sensitivity` is `insensitive`, uppercase letters count too.
[[nodiscard]] constexpr size_t contraction_suffix_length(
    std::u8string_view rest, case_sensitivity sensitivity) noexcept {
  if (rest.empty()) return 0;
  const auto is_folded = (sensitivity == case_sensitivity::insensitive);
  const auto letter = [&](size_t ndx) {
    return is_folded ? strings::as_lower(rest[ndx]) : rest[ndx];
  };
  const auto first = letter(0);
  if (rest.size() >= 2) {
    const auto second = letter(1);
    if ((first == u8'r' && second == u8'e') ||
        (first == u8'v' && second == u8'e') ||
        (first == u8'l' && second == u8'l'))
      return 2;
  }
  return (first == u8's' || first == u8't' || first == u8'm' || first == u8'd')
             ? 1
             : 0;
}

// Whether `rest` starts with a contraction suffix, setting `len` to its
// length.
//
// When it does not, `len` is left untouched.
[[nodiscard]] constexpr bool has_contraction_suffix(size_t& len,
    std::u8string_view rest, case_sensitivity sensitivity) noexcept {
  const auto found = contraction_suffix_length(rest, sensitivity);
  if (!found) return false;
  len = found;
  return true;
}

// Consume the run of code points of class `cls` at the front of `rest`,
// returning how many there were.
constexpr size_t
skip_run(std::u8string_view& rest, code_point_class cls) noexcept {
  size_t count{};
  for (auto after = rest;; after = rest) {
    char32_t cp{};
    if (!utf::extract(cp, after) || classifier::classify(cp) != cls) break;
    rest = after;
    ++count;
  }
  return count;
}

// Consume the rest of the whitespace chunk that starts at `chunk`, whose
// first code point, already consumed from `rest`, must be whitespace.
// Returns how many code points the chunk has.
//
// A run followed by anything else gives up its last code point to lead the
// next chunk, unless that is the run's only one.
constexpr size_t skip_white_space_chunk(std::u8string_view& rest,
    std::u8string_view chunk) noexcept {
  auto last = chunk;
  size_t count = 1;
  for (auto after = rest;; after = rest) {
    char32_t cp{};
    if (!utf::extract(cp, after) || !classifier::is_white_space(cp)) break;
    last = rest;
    rest = after;
    ++count;
  }
  if (!rest.empty() && count > 1) {
    rest = last;
    --count;
  }
  return count;
}

} // namespace implementation

#pragma endregion
#pragma region bpe_tokenizer

// A byte-level BPE tokenizer over a loaded merge table, splitting text by
// `Rule`.
//
// IDs below 256 are assigned to the single-byte pieces, in ascending order of
// their escaped forms. The merge at rank `r` (its line in "merges.txt",
// counting from 0) produces the ID `256 + r`, and the added tokens follow in
// the order they were loaded. An added token is matched in the text as it is,
// ahead of the split rule, and `decode` gives its text back.
template<typename Rule>
class bpe_tokenizer {
public:
#pragma region Byte escaping

  // The escaped form of `byte`.
  [[nodiscard]] static constexpr char32_t escape_byte(uint8_t byte) noexcept {
    return implementation::escaped_bytes_lookup[byte];
  }

  // Recover the byte behind `escaped`.
  //
  // On success, returns true and sets `byte`. On failure, returns false,
  // leaving `byte` untouched.
  [[nodiscard]] static constexpr bool
  unescape_byte(uint8_t& byte, char32_t escaped) noexcept {
    if (escaped > implementation::max_escaped_byte) return false;
    const auto candidate = implementation::unescaped_bytes_lookup[escaped];
    // A 0 result could be due to 0x0100 as input, or it could mean the input
    // was invalid; round-tripping settles it.
    if (!candidate && escaped != implementation::escaped_bytes_lookup[0])
      return false;
    byte = candidate;
    return true;
  }

  // Recover the unescaped text for `escaped`, appending it to `text`.
  //
  // On failure (malformed UTF-8, or an escaped form that stands for no
  // byte), returns false, leaving `text` untouched.
  [[nodiscard]] static constexpr bool
  unescape_piece(std::u8string& text, std::u8string_view escaped) {
    strings::truncate_guard guard(text);
    text.reserve(text.size() + escaped.size());
    for (char32_t cp{}; utf::extract(cp, escaped);) {
      uint8_t byte{};
      if (!unescape_byte(byte, cp)) return false;
      text.push_back(static_cast<char8_t>(byte));
    }
    if (!escaped.empty()) return false;
    return guard.release();
  }

#pragma endregion
#pragma region Splitting

  // Split `text` into the chunks that the rule produces, appending views into
  // `text` to `chunks`.
  //
  // On failure (malformed UTF-8), returns false, leaving `chunks` untouched.
  [[nodiscard]] static bool
  split(std::vector<std::u8string_view>& chunks, std::u8string_view text) {
    return Rule::split(chunks, text);
  }

#pragma endregion
#pragma region Loading

  // Load the merge table from `merges_text` (the contents of "merges.txt")
  // and then `added_tokens`, each of which takes the next ID in turn.
  //
  // The text is a header line starting with '#', if any, then one merge per
  // line as "left right" in rank order. Each side is an escaped piece and must
  // already be in the vocabulary. A trailing '\r' on a line is ignored. An
  // added token is its own text, unescaped. On failure (a malformed line, an
  // unknown side, a duplicate result, or an empty or duplicate added token),
  // returns false, leaving the tokenizer as it was.
  [[nodiscard]] bool load(std::u8string_view merges_text,
      std::span<const std::u8string_view> added_tokens = {}) {
    // The vocabulary under construction: the arena that becomes `tokens_`,
    // the pair table that becomes `merges_`, and a text-to-ID map that only
    // loading needs.
    //
    // Each line is one merge, and a merged piece is never longer than its
    // escaped line, so the sizes below are upper bounds.
    const auto line_count =
        static_cast<size_t>(std::ranges::count(merges_text, u8'\n')) + 1;
    const auto token_count = 256 + line_count + added_tokens.size();
    u8string_arena<token_id> tokens;
    std::vector<uint64_t> pair_keys;
    std::vector<token_id> result_ids;
    u8string_unordered_map<token_id> ids_by_text;
    tokens.reserve(token_count, 256 + merges_text.size());
    pair_keys.reserve(line_count);
    result_ids.reserve(line_count);
    ids_by_text.reserve(token_count);

    // Append `token` as the piece with the next ID. Fails on a duplicate
    // piece.
    const auto add_piece = [&](token_id& id, std::u8string_view token) {
      id = tokens.append(token);
      return ids_by_text.emplace(token, id).second;
    };

    // The first 256 pieces are the single bytes, in ID order.
    for (const auto ndx : iota(256)) {
      token_id id{};
      const auto byte =
          static_cast<char8_t>(implementation::id_bytes_lookup[ndx]);
      (void)add_piece(id, std::u8string_view(&byte, 1));
    }

    // Skip the comment line if it exists.
    if (merges_text.starts_with(u8'#'))
      (void)parser::next_delimited(u8'\n', merges_text);

    // Each merge line names two existing pieces. Their concatenation is the
    // next piece, and the pair of their IDs maps to its ID. A blank line is
    // skipped.
    std::u8string merged_token;
    while (!merges_text.empty()) {
      auto line = parser::next_delimited(u8'\n', merges_text);
      if (line.ends_with(u8'\r')) line.remove_suffix(1);
      if (line.empty()) continue;
      const auto left_escaped = parser::next_delimited(u8' ', line);
      const auto right_escaped = line;
      if (left_escaped.empty() || right_escaped.empty() ||
          right_escaped.contains(u8' '))
        return false;

      // Unescape both sides into one buffer, which is then the merged piece.
      merged_token.clear();
      if (!unescape_piece(merged_token, left_escaped)) return false;
      const auto left_len = merged_token.size();
      if (!unescape_piece(merged_token, right_escaped)) return false;
      const std::u8string_view left_bytes{merged_token.data(), left_len};
      const auto right_bytes =
          std::u8string_view{merged_token}.substr(left_len);
      const auto left_id = find_opt(ids_by_text, left_bytes);
      const auto right_id = find_opt(ids_by_text, right_bytes);
      if (!left_id || !right_id) return false;

      token_id result_id{};
      if (!add_piece(result_id, merged_token)) return false;
      pair_keys.push_back(pack(*left_id, *right_id));
      result_ids.push_back(result_id);
    }

    // The added tokens take the IDs after the merges, in order. Encoding
    // tries them longest first, so that one that extends another wins.
    std::vector<added_token> added;
    added.reserve(added_tokens.size());
    for (const auto token : added_tokens) {
      token_id id{};
      if (token.empty() || !add_piece(id, token)) return false;
      added.push_back({.token = std::u8string{token}, .id = id});
    }
    // Sort the added tokens by length in descending order, so that longer
    // tokens are matched first during encoding. Note that this is not strictly
    // necessary for Qwen2, as it has no overlapping added tokens.
    std::ranges::stable_sort(added, std::ranges::greater{},
        [](const added_token& token) { return token.token.size(); });

    // The longest prefix every added token shares, which is where a search
    // for one can start.
    std::u8string_view prefix;
    if (!added.empty()) prefix = added.front().token;
    for (const auto& entry : added) {
      const auto shared = static_cast<size_t>(
          std::ranges::mismatch(prefix, entry.token).in1 - prefix.begin());
      prefix = prefix.substr(0, shared);
    }

    merges_ = merges_t(std::move(pair_keys), std::move(result_ids));
    tokens_ = std::move(tokens);
    added_prefix_ = std::u8string{prefix};
    added_tokens_ = std::move(added);
    return true;
  }

#pragma endregion
#pragma region Encoding

  // Encode `text`, appending its IDs to `ids`.
  //
  // An added token found in the text is one ID. The text between added
  // tokens is split into chunks by the rule (see `split`). Each chunk starts
  // out as the IDs of its bytes' single-byte pieces. Then adjacent pairs are
  // merged, each merge replacing two IDs with the ID of the piece they form,
  // until the chunk has no pair left that is in the merge table. Merges never
  // cross a chunk boundary.
  //
  // On failure (malformed UTF-8), returns false, leaving `ids` untouched.
  [[nodiscard]] bool
  encode(std::vector<token_id>& ids, std::u8string_view text) const {
    strings::truncate_guard guard(ids);
    std::vector<std::u8string_view> chunks;
    auto rest = text;
    while (!rest.empty()) {
      // The plain text runs up to the next added token, if there is one.
      const auto [plain_len, found] = find_added_token(rest);
      if (!encode_plain(ids, chunks, rest.substr(0, plain_len))) return false;
      rest.remove_prefix(plain_len);
      if (found) {
        ids.push_back(found->id);
        rest.remove_prefix(found->token.size());
      }
    }
    return guard.release();
  }

#pragma endregion
#pragma region Decoding

  // Decode `ids`, appending the text to `out`.
  //
  // On failure (an ID with no piece), returns false, leaving `out` untouched.
  [[nodiscard]] bool
  decode(std::u8string& out, std::span<const token_id> ids) const {
    strings::truncate_guard guard(out);
    for (const auto id : ids) {
      if (*id >= size()) return false;
      out += text(id);
    }
    return guard.release();
  }

#pragma endregion
#pragma region Vocabulary

  // The number of pieces, which is one past the largest ID `decode` accepts.
  [[nodiscard]] size_t size() const noexcept { return tokens_.size(); }

  // The text of piece `id`, which must be below `size`.
  [[nodiscard]] std::u8string_view text(token_id id) const noexcept {
    return tokens_[id];
  }

#pragma endregion
#pragma region Helpers
private:
  // An added token's text and ID.
  struct added_token {
    std::u8string token;
    token_id id;
  };

  // The added token at the front of `rest`, the longest when several fit, or
  // null.
  [[nodiscard]] const added_token* added_token_at(
      std::u8string_view rest) const noexcept {
    for (const auto& added : added_tokens_)
      if (rest.starts_with(added.token)) return &added;
    return nullptr;
  }

  // Find the first added token in `rest`, returning the length of the plain
  // text before it and the token, which is null when there is none.
  //
  // Every added token starts with `added_prefix_`, so only the offsets where
  // it occurs are tried, unless it is empty, in which case every offset is.
  [[nodiscard]] std::pair<size_t, const added_token*> find_added_token(
      std::u8string_view rest) const noexcept {
    if (added_tokens_.empty()) return {rest.size(), nullptr};
    for (size_t at = 0; at < rest.size(); ++at) {
      if (!added_prefix_.empty()) {
        auto probe = rest.substr(at);
        at += parser::next_delimited(added_prefix_, probe).size();
        if (at == rest.size()) break;
      }
      if (const auto* found = added_token_at(rest.substr(at)))
        return {at, found};
    }
    return {rest.size(), nullptr};
  }

  // Encode `text`, which holds no added token, appending its IDs to `ids`
  // and using `chunks` as scratch.
  [[nodiscard]] bool encode_plain(std::vector<token_id>& ids,
      std::vector<std::u8string_view>& chunks, std::u8string_view text) const {
    chunks.clear();
    if (!split(chunks, text)) return false;
    for (const auto chunk : chunks) {
      // The chunk's bytes go in as single-byte IDs and merge in place, so
      // the output vector is the only work buffer.
      const auto start = ids.size();
      for (const auto byte : chunk)
        ids.push_back(
            implementation::byte_ids_lookup[static_cast<uint8_t>(byte)]);
      auto word = std::span{ids}.subspan(start);
      merge(word);
      ids.resize(start + word.size());
    }
    return true;
  }

  // The pair key packs two 32-bit IDs into one 64-bit word.
  //
  // These are the left and right token IDs that are merged together to form a
  // new token (whose ID is stored as the value of the collection keyed by
  // this).
  [[nodiscard]] static constexpr uint64_t
  pack(token_id left, token_id right) noexcept {
    return (static_cast<uint64_t>(*left) << 32) | *right;
  }

  // Merge `word` in place, shrinking it to what remains.
  //
  // Each round merges every occurrence of the lowest-ranked adjacent pair,
  // left to right, until no adjacent pair is in the table. Returns the
  // number of rounds.
  size_t merge(std::span<token_id>& word) const noexcept {
    size_t rounds{};
    uint64_t key{};
    token_id result{};
    while (word.size() > 1 && find_best_merge(key, result, word)) {
      apply_merge(word, key, result);
      ++rounds;
    }
    return rounds;
  }

  // Find the lowest-ranked adjacent pair in `word` that is in the merge table.
  //
  // On success, returns true and sets `key` to the pair and `result` to
  // the ID it merges into. On failure (no adjacent pair is in the table),
  // returns false, leaving both untouched.
  [[nodiscard]] bool find_best_merge(uint64_t& key, token_id& result,
      std::span<const token_id> word) const noexcept {
    // Earlier merges produced lower IDs, so the lowest result ID among the
    // pairs present is the lowest rank.
    const token_id* best{};
    uint64_t best_key{};
    for (const auto [left, right] : word | std::views::pairwise) {
      const auto pair_key = pack(left, right);
      const auto found = find_opt(merges_, pair_key).get();
      if (found && (!best || *found < *best)) {
        best = found;
        best_key = pair_key;
      }
    }
    if (!best) return false;
    key = best_key;
    result = *best;
    return true;
  }

  // Replace every occurrence of the pair `key` in `word` with `result`,
  // left to right, shrinking `word` to what remains. Returns the number of
  // replacements.
  static size_t apply_merge(std::span<token_id>& word, uint64_t key,
      token_id result) noexcept {
    size_t replacements{};
    size_t out = 0;
    for (size_t ndx = 0; ndx < word.size(); ++ndx, ++out) {
      if (ndx + 1 < word.size() && pack(word[ndx], word[ndx + 1]) == key) {
        word[out] = result;
        ++ndx;
        ++replacements;
      } else {
        word[out] = word[ndx];
      }
    }
    word = word.first(out);
    return replacements;
  }

#pragma endregion
#pragma region Data members

  using merges_t = std::flat_map<uint64_t, token_id>;
  using parser = strings::u8token_parser;

  // The merge table, keyed on the packed pair and valued by the result ID.
  merges_t merges_;

  // The vocabulary, holding every token's text at its ID.
  u8string_arena<token_id> tokens_;

  // The added tokens, longest first.
  std::vector<added_token> added_tokens_;

  // The longest prefix every added token shares, where a search for one can
  // start. Empty when they share none.
  std::u8string added_prefix_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::llm
