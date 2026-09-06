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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../containers/core/opt_find.h"
#include "../../containers/core/transparent.h"
#include "../../enums/sequence_enum.h"
#include "../../strings/conversion.h"
#include "../../strings/token_parser.h"
#include "../../strings/unicode.h"

// GPT-2's byte-level BPE tokenizer.
//
// `gpt2_tokenizer` loads the merge table from the text of "merges.txt". This
// allows it to encode UTF-8 text to token IDs and decode IDs back to bytes.
// The published tables store every byte in an escaped form that keeps it
// printable (see `details::escaped_bytes`). `load` unescapes each piece once,
// and nothing else touches the escaped form.
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

#pragma region token_id

// Index of a piece in a tokenizer's vocabulary.
enum class token_id : uint32_t {};
consteval auto corvid_enum_spec(token_id*) {
  return corvid::enums::sequence::make_sequence_enum_spec<token_id, "">();
}

#pragma endregion
#pragma region details

namespace details {

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
  for (auto byte = 0; byte < 256; ++byte)
    table[escaped_bytes_lookup[byte]] = static_cast<uint8_t>(byte);
  return table;
}
inline constexpr auto unescaped_bytes_lookup = make_unescaped_bytes_lookup();

// The count of bytes that are their own escaped form.
[[nodiscard]] consteval uint32_t count_printable_latin1() {
  uint32_t count{};
  for (auto byte = 0; byte < 256; ++byte)
    if (is_printable_latin1(static_cast<uint8_t>(byte))) ++count;
  return count;
}

// Generate a lookup table from each byte to the `token_id` of its single-byte
// piece; the inverse of `id_bytes_lookup`.
//
// These single-byte pieces are the individual bytes that remain after merging
// is complete, ensuring that all inputs can be represented. They are  assigned
// `token_id`s in ascending order of their escaped forms.
[[nodiscard]] consteval std::array<token_id, 256> make_byte_ids_lookup() {
  std::array<token_id, 256> ids{};
  uint32_t next_printable{};
  auto next_other = count_printable_latin1();
  // Assigns a `token_id` to each byte, with printable Latin-1 bytes first and
  // the rest following.
  for (auto byte = 0; byte < 256; ++byte) {
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
  for (auto byte = 0; byte < 256; ++byte)
    bytes[*byte_ids_lookup[byte]] = static_cast<uint8_t>(byte);
  return bytes;
}
inline constexpr auto id_bytes_lookup = make_id_bytes_lookup();

} // namespace details

#pragma endregion
#pragma region gpt2_tokenizer

// GPT-2's byte-level BPE tokenizer over a loaded merge table.
//
// IDs below 256 are assigned to the single-byte pieces, in ascending order of
// their escaped forms. The merge at rank `r` (its line in "merges.txt",
// counting from 0) produces the ID `256 + r`. The end-of-text ID (50256 for
// GPT-2) is not a piece, so `encode` never produces it and `decode` rejects
// it.
class gpt2_tokenizer {
public:
#pragma region Byte escaping

  // The escaped form of `byte`.
  [[nodiscard]] static constexpr char32_t escape_byte(uint8_t byte) noexcept {
    return details::escaped_bytes_lookup[byte];
  }

  // Recover the byte behind `escaped`.
  //
  // On success, returns true and sets `byte`. On failure, returns false,
  // leaving `byte` untouched.
  [[nodiscard]] static constexpr bool
  unescape_byte(uint8_t& byte, char32_t escaped) noexcept {
    if (escaped > details::max_escaped_byte) return false;
    const auto candidate = details::unescaped_bytes_lookup[escaped];
    // A 0 result could be due to 0x0100 as input, or it could mean the input
    // was invalid; round-tripping settles it.
    if (!candidate && escaped != details::escaped_bytes_lookup[0])
      return false;
    byte = candidate;
    return true;
  }

  // Recover the bytes behind `piece`, which is escaped, appending them to
  // `bytes`.
  //
  // On failure (malformed UTF-8, or an escaped form that stands for no
  // byte), returns false, leaving `bytes` untouched.
  [[nodiscard]] static constexpr bool
  unescape_piece(std::u8string& bytes, std::u8string_view piece) {
    strings::truncate_guard guard(bytes);
    bytes.reserve(bytes.size() + piece.size());
    for (char32_t cp{}; utf::extract(cp, piece);) {
      uint8_t byte{};
      if (!unescape_byte(byte, cp)) return false;
      bytes.push_back(static_cast<char8_t>(byte));
    }
    if (!piece.empty()) return false;
    return guard.release();
  }

#pragma endregion
#pragma region Splitting

  // Split `text` into the chunks that GPT-2's rule produces, appending views
  // into `text` to `chunks`.
  //
  // The rule is this regex, with its alternatives tried in order:
  //   's|'t|'re|'ve|'m|'ll|'d
  //   | ?\p{L}+| ?\p{N}+| ?[^\s\p{L}\p{N}]+
  //   |\s+(?!\S)|\s+
  //
  // On failure (malformed UTF-8), returns false, leaving `chunks` untouched.
  [[nodiscard]] static bool
  split(std::vector<std::u8string_view>& chunks, std::u8string_view text) {
    strings::truncate_guard guard(chunks);
    // Text runs about four to six bytes per chunk, so this guess is within
    // the factor of two that costs at most one more growth step.
    chunks.reserve(chunks.size() + (text.size() / 4));
    auto rest = text;
    while (!rest.empty()) {
      // The chunk runs from here to wherever `rest` is left.
      const auto chunk = rest;
      char32_t cp{};
      if (!utf::extract(cp, rest)) return false;
      if (cp == U'\'') {
        // A contraction, else the apostrophe starts a punctuation run.
        if (const auto len = contraction_suffix_length(rest))
          rest.remove_prefix(len);
        else
          skip_run(rest, code_point_class::other);
      } else if (cp == U' ') {
        // A single space leads the run after it, whatever its class, unless
        // that run is more whitespace, in which case the space is part of
        // it.
        auto after_next = rest;
        char32_t next{};
        const auto cls =
            utf::extract(next, after_next)
                ? classifier::classify(next)
                : code_point_class::white_space;
        if (cls == code_point_class::white_space) {
          skip_white_space_chunk(rest, chunk);
        } else {
          rest = after_next;
          skip_run(rest, cls);
        }
      } else if (const auto cls = classifier::classify(cp);
          cls == code_point_class::white_space)
      {
        skip_white_space_chunk(rest, chunk);
      } else {
        skip_run(rest, cls);
      }
      chunks.push_back(chunk.substr(0, chunk.size() - rest.size()));
    }
    return guard.release();
  }

#pragma endregion
#pragma region Loading

  // Load the merge table from `merges_text`, the contents of "merges.txt".
  //
  // The text is a header line starting with '#', if any, then one merge per
  // line as "left right" in rank order. Each side is an escaped piece and must
  // already be in the vocabulary. A trailing '\r' on a line is ignored. On
  // failure (a malformed line, an unknown side, a duplicate result, or a
  // vocabulary too large for the pair key), returns false, leaving the
  // tokenizer as it was.
  [[nodiscard]] bool load(std::u8string_view merges_text) {
    using parser = strings::basic_token_parser<char8_t>;
    using ids_by_bytes_t = std::unordered_map<std::u8string, token_id,
        basic_transparent_hash_equal<char8_t>,
        basic_transparent_hash_equal<char8_t>>;

    // The vocabulary under construction: the piece arena that becomes
    // `pieces_` and `piece_starts_` (see their comment), the pair table that
    // becomes `merges_`, and a bytes-to-ID map that only loading needs. Each
    // line is one merge, and a merged piece is never longer than its escaped
    // line, so the sizes below are upper bounds.
    const auto line_count =
        static_cast<size_t>(std::ranges::count(merges_text, u8'\n')) + 1;
    std::u8string pieces;
    std::vector<size_t> piece_starts;
    std::vector<uint32_t> pair_keys;
    std::vector<token_id> result_ids;
    ids_by_bytes_t ids_by_bytes;
    pieces.reserve(256 + merges_text.size());
    piece_starts.reserve(256 + line_count + 1);
    pair_keys.reserve(line_count);
    result_ids.reserve(line_count);
    ids_by_bytes.reserve(256 + line_count);

    // Append `bytes` as the piece with the next ID, which is the count of
    // pieces so far. Fails on a duplicate piece or an ID too large for the
    // pair key.
    const auto add_piece = [&](token_id& id, std::u8string_view bytes) {
      id = token_id{static_cast<uint32_t>(piece_starts.size())};
      if (*id > max_packed_id) return false;
      if (!ids_by_bytes.emplace(bytes, id).second) return false;
      piece_starts.push_back(pieces.size());
      pieces += bytes;
      return true;
    };

    // The first 256 pieces are the single bytes, in ID order.
    for (auto ndx = 0U; ndx < 256; ++ndx) {
      token_id id{};
      const auto byte = static_cast<char8_t>(details::id_bytes_lookup[ndx]);
      (void)add_piece(id, std::u8string_view(&byte, 1));
    }

    // Skip the comment line if it exists.
    if (merges_text.starts_with(u8'#'))
      (void)parser::next_delimited(u8'\n', merges_text);

    // Each merge line names two existing pieces. Their concatenation is the
    // next piece, and the pair of their IDs maps to its ID.
    std::u8string merged_bytes;
    while (!merges_text.empty()) {
      auto line = parser::next_delimited(u8'\n', merges_text);
      if (line.ends_with(u8'\r')) line.remove_suffix(1);
      const auto left_escaped = parser::next_delimited(u8' ', line);
      const auto right_escaped = line;
      if (left_escaped.empty() || right_escaped.empty() ||
          right_escaped.contains(u8' '))
        return false;

      // Unescape both sides into one buffer, which is then the merged piece.
      merged_bytes.clear();
      if (!unescape_piece(merged_bytes, left_escaped)) return false;
      const auto left_len = merged_bytes.size();
      if (!unescape_piece(merged_bytes, right_escaped)) return false;
      const std::u8string_view left_bytes{merged_bytes.data(), left_len};
      const auto right_bytes =
          std::u8string_view{merged_bytes}.substr(left_len);
      const auto left_id = find_opt(ids_by_bytes, left_bytes);
      const auto right_id = find_opt(ids_by_bytes, right_bytes);
      if (!left_id || !right_id) return false;

      token_id result_id{};
      if (!add_piece(result_id, merged_bytes)) return false;
      pair_keys.push_back(pack(*left_id, *right_id));
      result_ids.push_back(result_id);
    }
    piece_starts.push_back(pieces.size());

    merges_ = merges_t(std::move(pair_keys), std::move(result_ids));
    pieces_ = std::move(pieces);
    piece_starts_ = std::move(piece_starts);
    return true;
  }

#pragma endregion
#pragma region Encoding

  // Encode `text`, appending its IDs to `ids`.
  //
  // The text is split into chunks by GPT-2's rule (see `split`). Each chunk
  // starts out as the IDs of its bytes' single-byte pieces. Then adjacent
  // pairs are merged, each merge replacing two IDs with the ID of the piece
  // they form, until the chunk has no pair left that is in the merge table.
  // Merges never cross a chunk boundary.
  //
  // On failure (malformed UTF-8), returns false, leaving `ids` untouched.
  [[nodiscard]] bool
  encode(std::vector<token_id>& ids, std::u8string_view text) const {
    std::vector<std::u8string_view> chunks;
    if (!split(chunks, text)) return false;
    for (const auto chunk : chunks) {
      // The chunk's bytes go in as single-byte IDs and merge in place, so
      // the output vector is the only work buffer.
      const auto start = ids.size();
      for (const auto byte : chunk)
        ids.push_back(details::byte_ids_lookup[static_cast<uint8_t>(byte)]);
      auto word = std::span{ids}.subspan(start);
      merge(word);
      ids.resize(start + word.size());
    }
    return true;
  }

#pragma endregion
#pragma region Decoding

  // Decode `ids`, appending their bytes to `out`.
  //
  // On failure (an ID with no piece), returns false, leaving `out`
  // untouched.
  [[nodiscard]] bool
  decode(std::u8string& out, std::span<const token_id> ids) const {
    strings::truncate_guard guard(out);
    for (const auto id : ids) {
      if (*id >= size()) return false;
      out += piece(id);
    }
    return guard.release();
  }

#pragma endregion
#pragma region Vocabulary

  // The number of pieces, which is one past the largest ID `decode` accepts.
  [[nodiscard]] size_t size() const noexcept {
    return piece_starts_.empty() ? 0 : piece_starts_.size() - 1;
  }

  // The bytes of piece `id`, which must be below `size` (asserted).
  [[nodiscard]] std::u8string_view piece(token_id id) const {
    assert(*id < size());
    return std::u8string_view{pieces_}.substr(piece_starts_[*id],
        piece_starts_[*id + 1] - piece_starts_[*id]);
  }

#pragma endregion
#pragma region Helpers
private:
  // The length of the contraction suffix at the front of `rest`, or 0.
  [[nodiscard]] static constexpr size_t contraction_suffix_length(
      std::u8string_view rest) noexcept {
    if (rest.empty()) return 0;
    if (rest.starts_with(u8"re") || rest.starts_with(u8"ve") ||
        rest.starts_with(u8"ll"))
      return 2;
    const auto ch = rest.front();
    return (ch == u8's' || ch == u8't' || ch == u8'm' || ch == u8'd') ? 1 : 0;
  }

  // Consume the run of code points of class `cls` at the front of `rest`,
  // returning how many there were.
  static constexpr size_t
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
  static constexpr size_t skip_white_space_chunk(std::u8string_view& rest,
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

  // The pair key: two 16-bit IDs in one 32-bit word.
  //
  // These are the left and right token IDs that are merged together to form a
  // new token (whose ID is not stored here).
  [[nodiscard]] static constexpr uint32_t
  pack(token_id left, token_id right) noexcept {
    return (*left << 16) | *right;
  }
  static constexpr uint32_t max_packed_id = 0xFFFF;

  // Merge `word` in place, shrinking it to what remains.
  //
  // Each round merges every occurrence of the lowest-ranked adjacent pair,
  // left to right, until no adjacent pair is in the table. Returns the
  // number of rounds.
  size_t merge(std::span<token_id>& word) const noexcept {
    size_t rounds{};
    auto len = word.size();
    while (len > 1) {
      // Earlier merges produced lower IDs, so the lowest result ID among
      // the pairs present is the lowest rank.
      uint32_t best_key{};
      optional_ptr<const token_id*> best;
      for (size_t ndx = 0; ndx + 1 < len; ++ndx) {
        const auto key = pack(word[ndx], word[ndx + 1]);
        const auto found = find_opt(merges_, key);
        if (found && (!best || *found < *best)) {
          best = found;
          best_key = key;
        }
      }
      if (!best) break;

      size_t out = 0;
      for (size_t ndx = 0; ndx < len; ++ndx, ++out) {
        if (ndx + 1 < len && pack(word[ndx], word[ndx + 1]) == best_key) {
          word[out] = *best;
          ++ndx;
        } else {
          word[out] = word[ndx];
        }
      }
      len = out;
      ++rounds;
    }
    word = word.first(len);
    return rounds;
  }

#pragma endregion
#pragma region Data members

  using merges_t = std::flat_map<uint32_t, token_id>;

  // The merge table, keyed on the packed pair and valued by the result ID.
  merges_t merges_;

  // `pieces_` holds every piece concatenated in ID order, and `piece_starts_`
  // holds where each piece starts plus one past the last, so piece `id` is
  // the bytes from `piece_starts_[id]` to `piece_starts_[id + 1]`.
  std::u8string pieces_;
  std::vector<size_t> piece_starts_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::llm
