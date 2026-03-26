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
#include "strings_shared.h"

namespace corvid::strings { inline namespace parsers {

// Separator-based token parse.
class token_parser {
public:
  explicit token_parser(std::string_view separator) : separator_(separator) {}

  auto separator() const noexcept { return separator_; }
  void separator(std::string_view separator) noexcept {
    separator_ = separator;
  }

  // Extract the next token delimited by `separator_`, returning it (without
  // the delimiter), and consuming it (and the delimiter) from `text. As this
  // is a delimiter, not a terminator, the last token is returned when no more
  // delimiters are found. An empty `std::string_view` is returned under two
  // conditions. First, when `text` contains just a delimiter; that delimiter
  // is consumed. Second, when `text` is empty.
  [[nodiscard]] std::string_view next_delimited(std::string_view& text) const {
    // If no input, nothing to parse.
    if (text.empty()) return {};

    // If delimiter not found, consume the rest of the input.
    const auto pos = text.find(separator_);
    if (pos == npos) {
      const auto token = text;
      text = {};
      return token;
    }

    // Delimiter found; return the token before it and update input to after
    // it.
    const auto token = text.substr(0, pos);
    text.remove_prefix(pos + separator_.size());
    return token;
  }

  // Extract the next token terminated by `separator_`, returning it (without
  // the terminator), and consuming it (and the terminator) from `text`. As
  // this is a terminator, not a delimiter, the terminator is required; if not
  // found, `nullopt` is returned. In contrast, if `text` contains just a
  // terminator, an empty `std::string_view` is returned, and the terminator is
  // removed from `text`.
  [[nodiscard]] std::optional<std::string_view> next_terminated(
      std::string_view& text) const {
    // If no input, nothing to parse.
    if (text.empty()) return {};

    // If terminator not found, fail.
    const auto pos = text.find(separator_);
    if (pos == npos) return std::nullopt;

    // Terminator found; return the token before it and update input to after
    // it.
    const auto token = text.substr(0, pos);
    text.remove_prefix(pos + separator_.size());
    return token;
  }

private:
  std::string_view separator_;
};
}} // namespace corvid::strings::parsers
