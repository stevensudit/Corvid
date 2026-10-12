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

// GPT-2's byte-level BPE tokenizer, which is `bpe_tokenizer` under GPT-2's
// split rule.
//
// The end-of-text ID (50256 for GPT-2) is not a piece unless it is loaded as
// an added token, so by default `encode` never produces it and `decode`
// rejects it.
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

#pragma region gpt2_split_rule

// GPT-2's rule for splitting text into the chunks that merges never cross.
struct gpt2_split_rule {
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
        // Alternatives 1 to 7, the contractions. Otherwise the apostrophe
        // starts a punctuation run, alternative 10.
        if (const auto len = implementation::contraction_suffix_length(rest,
                case_sensitivity::sensitive))
          rest.remove_prefix(len);
        else
          implementation::skip_run(rest, code_point_class::other);
      } else if (cp == U' ') {
        // Alternatives 8 to 10 with their optional leading space. The space
        // leads the run after it, whatever its class, unless that run is
        // more whitespace, in which case the space is part of it and
        // alternatives 11 and 12 apply.
        auto after_next = rest;
        char32_t next{};
        const auto cls =
            utf::extract(next, after_next)
                ? classifier::classify(next)
                : code_point_class::white_space;
        if (cls == code_point_class::white_space) {
          implementation::skip_white_space_chunk(rest, chunk);
        } else {
          rest = after_next;
          implementation::skip_run(rest, cls);
        }
      } else if (const auto cls = classifier::classify(cp);
          cls == code_point_class::white_space)
      {
        // Alternatives 11 and 12, a whitespace run with the lookahead.
        implementation::skip_white_space_chunk(rest, chunk);
      } else {
        // Alternatives 8 to 10 without the leading space, a run of one class.
        implementation::skip_run(rest, cls);
      }
      chunks.push_back(chunk.substr(0, chunk.size() - rest.size()));
    }
    return guard.release();
  }
};

#pragma endregion
#pragma region gpt2_tokenizer

using gpt2_tokenizer = bpe_tokenizer<gpt2_split_rule>;

#pragma endregion

} // namespace corvid::llm
