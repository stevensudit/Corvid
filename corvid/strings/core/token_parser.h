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
#include <optional>

#include "../../meta/concepts.h"
#include "strings_shared.h"

namespace corvid::strings { inline namespace parsers {

#pragma region basic_token_parser

// Separator-based token parse.
template<CharType Char = char>
class basic_token_parser {
public:
#pragma region Member types

  using char_t = Char;
  using view_t = std::basic_string_view<char_t>;

#pragma endregion
#pragma region Construction
  explicit basic_token_parser(view_t separator) : separator_(separator) {}

#pragma endregion
#pragma region Accessors

  [[nodiscard]] auto separator() const noexcept { return separator_; }
  void separator(view_t separator) noexcept { separator_ = separator; }

#pragma endregion
#pragma region Delimited

  // Extract the next token delimited by `separator_`, returning it (without
  // the delimiter), and consuming it (and the delimiter) from `text`.
  //
  // As this is a delimiter, not a terminator, the last token is returned when
  // no more delimiters are found. An empty view is returned under two
  // conditions. First, when `text` contains just a delimiter; that delimiter
  // is consumed. Second, when `text` is empty.
  //
  // Similar to `extract_piece`.
  [[nodiscard]] static view_t next_delimited(view_t separator, view_t& text) {
    // If no input, nothing to parse.
    if (text.empty() || separator.empty()) return {};

    // If delimiter not found, consume the rest of the input.
    const auto pos = text.find(separator);
    if (pos == npos) {
      const auto token = text;
      text = {};
      return token;
    }

    // Delimiter found; return the token before it and update input to after
    // it.
    const auto token = text.substr(0, pos);
    text.remove_prefix(pos + separator.size());
    return token;
  }

  // Overload for single-character separator; skips empty-separator check and
  // uses the `char_t` overload of `find` for efficiency.
  [[nodiscard]] static view_t next_delimited(char_t separator, view_t& text) {
    // If no input, nothing to parse.
    if (text.empty()) return {};

    // If delimiter not found, consume the rest of the input.
    const auto pos = text.find(separator);
    if (pos == npos) {
      const auto token = text;
      text = {};
      return token;
    }

    // Delimiter found; return the token before it and update input to after
    // it.
    const auto token = text.substr(0, pos);
    text.remove_prefix(pos + 1);
    return token;
  }

  // See static method.
  [[nodiscard]] view_t next_delimited(view_t& text) const {
    return next_delimited(separator_, text);
  }

#pragma endregion
#pragma region Terminated

  // Extract the next token terminated by `separator_`, returning it (without
  // the terminator), and consuming it (and the terminator) from `text`.
  //
  // As this is a terminator, not a delimiter, the terminator is required; if
  // not found, `nullopt` is returned. In contrast, if `text` contains just a
  // terminator, an empty view is returned, and the terminator is removed from
  // `text`.
  [[nodiscard]] static std::optional<view_t>
  next_terminated(view_t separator, view_t& text) {
    // If no input, nothing to parse.
    if (text.empty() || separator.empty()) return {};

    // If terminator not found, fail.
    const auto pos = text.find(separator);
    if (pos == npos) return std::nullopt;

    // Terminator found; return the token before it and update input to after
    // it.
    const auto token = text.substr(0, pos);
    text.remove_prefix(pos + separator.size());
    return token;
  }

  // Overload for single-character separator; skips empty-separator check and
  // uses the `char_t` overload of `find` for efficiency.
  [[nodiscard]] static std::optional<view_t>
  next_terminated(char_t separator, view_t& text) {
    // If no input, nothing to parse.
    if (text.empty()) return {};

    // If terminator not found, fail.
    const auto pos = text.find(separator);
    if (pos == npos) return std::nullopt;

    // Terminator found; return the token before it and update input to after
    // it.
    const auto token = text.substr(0, pos);
    text.remove_prefix(pos + 1);
    return token;
  }

  // See static method.
  [[nodiscard]] std::optional<view_t> next_terminated(view_t& text) const {
    return next_terminated(separator_, text);
  }

#pragma endregion
#pragma region Data members

private:
  view_t separator_;

#pragma endregion
};

// The default token parser, over `char`.
using token_parser = basic_token_parser<char>;

#pragma endregion
}} // namespace corvid::strings::parsers
