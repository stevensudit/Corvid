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
#include <cstddef>
#include <string_view>
#include <vector>

#include "../strings/conversion.h"
#include "../strings/unicode.h"
#include "bpe_tokenizer.h"

// Qwen2's byte-level BPE tokenizer, which is `bpe_tokenizer` under Qwen2's
// split rule.
//
// The chat and end-of-text markers (`<|im_start|>`, `<|im_end|>`,
// `<|endoftext|>`, and the rest of the model's added tokens) are not in the
// merge table. They are loaded as added tokens, in the order the model lists
// them, so that they take their published IDs.
//
// The published tokenizer normalizes text to NFC before splitting. This one
// does not, so text that is not already NFC will encode incorrtectly. The user
// should therefore ensure that all input is NFC-normalized.
//
// Loading:
//   qwen2_tokenizer tok;
//   if (!tok.load(merges_text, added_tokens)) ...
//
// Encoding and decoding:
//   std::vector<token_id> ids;
//   if (!tok.encode(ids, u8"<|im_start|>user\nHello<|im_end|>")) ...
//   std::u8string bytes;
//   if (!tok.decode(bytes, ids)) ...

namespace corvid::llm {

#pragma region qwen2_split_rule

// Qwen2's rule for splitting text into the chunks that merges never cross.
struct qwen2_split_rule {
  // Split `text` into the chunks that Qwen2's rule produces, appending views
  // into `text` to `chunks`.
  //
  // The rule is this regex, with its alternatives tried in order:
  //   (?i:'s|'t|'re|'ve|'m|'ll|'d)
  //   |[^\r\n\p{L}\p{N}]?\p{L}+|\p{N}| ?[^\s\p{L}\p{N}]+[\r\n]*
  //   |\s*[\r\n]+|\s+(?!\S)|\s+
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
      const auto cls = classifier::classify(cp);
      // The code point after this one decides between the alternatives that
      // take an optional first character and the ones that do not.
      auto after_next = rest;
      char32_t next{};
      const auto has_next = utf::extract(next, after_next);
      const auto next_cls =
          has_next ? classifier::classify(next)
                   : code_point_class::white_space;
      size_t len{};
      if (cp == U'\'' &&
          implementation::has_contraction_suffix(len, rest,
              case_sensitivity::insensitive))
      {
        // Alternatives 1 to 7, the contractions, in either case.
        rest.remove_prefix(len);
      } else if (cls == code_point_class::letter) {
        // Alternative 8 without its optional first character.
        implementation::skip_run(rest, cls);
      } else if (cls == code_point_class::number) {
        // Alternative 9, one digit on its own.
      } else if (!is_newline(cp) && has_next &&
                 next_cls == code_point_class::letter)
      {
        // Alternative 8 with its first character, which may be anything but
        // a newline, a letter, or a digit.
        rest = after_next;
        implementation::skip_run(rest, next_cls);
      } else if (cls == code_point_class::other) {
        // Alternative 10 without its optional space, a run of symbols and
        // punctuation, then any newlines.
        implementation::skip_run(rest, cls);
        skip_newlines(rest);
      } else if (cp == U' ' && has_next && next_cls == code_point_class::other)
      {
        // Alternative 10 with its space.
        rest = after_next;
        implementation::skip_run(rest, next_cls);
        skip_newlines(rest);
      } else {
        // Alternatives 11 to 13, a whitespace run. One that holds a newline
        // ends at its last newline. Otherwise the lookahead rule applies.
        skip_white_space_chunk(rest, chunk);
      }
      chunks.push_back(chunk.substr(0, chunk.size() - rest.size()));
    }
    return guard.release();
  }

private:
  // Whether `cp` is a carriage return or a line feed.
  [[nodiscard]] static constexpr bool is_newline(char32_t cp) noexcept {
    return (cp == U'\r') || (cp == U'\n');
  }

  // Whether the code unit `ch` is a carriage return or a line feed, which
  // are single code units in UTF-8.
  [[nodiscard]] static constexpr bool is_newline(char8_t ch) noexcept {
    return (ch == u8'\r') || (ch == u8'\n');
  }

  // Consume the newlines at the front of `rest`.
  static constexpr void skip_newlines(std::u8string_view& rest) noexcept {
    while (!rest.empty() && is_newline(rest.front())) rest.remove_prefix(1);
  }

  // Consume the rest of the whitespace chunk that starts at `chunk`, whose
  // first code point, already consumed from `rest`, must be whitespace.
  //
  // A run with a newline in it ends after its last newline, leaving any
  // whitespace past that for the next chunk. A run without one gives up its
  // last code point to lead the next chunk, unless that is the run's only one.
  static constexpr void skip_white_space_chunk(std::u8string_view& rest,
      std::u8string_view chunk) noexcept {
    const auto first = chunk.front();
    // Where the run stands after its last newline so far, if any.
    std::u8string_view after_newline;
    auto has_newline = is_newline(first);
    if (has_newline) after_newline = rest;
    auto last = chunk;
    size_t count = 1;
    for (auto after = rest;; after = rest) {
      char32_t cp{};
      if (!utf::extract(cp, after) || !classifier::is_white_space(cp)) break;
      last = rest;
      rest = after;
      ++count;
      if (is_newline(cp)) {
        has_newline = true;
        after_newline = rest;
      }
    }
    if (has_newline) {
      rest = after_newline;
    } else if (!rest.empty() && count > 1) {
      rest = last;
    }
  }
};

#pragma endregion
#pragma region qwen2_tokenizer

using qwen2_tokenizer = bpe_tokenizer<qwen2_split_rule>;

#pragma endregion

} // namespace corvid::llm
