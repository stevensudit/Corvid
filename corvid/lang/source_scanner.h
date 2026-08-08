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
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "../containers/core/value_or_error.h"
#include "../strings/conversion.h"

namespace corvid { inline namespace lang {

// Shared source-text machinery for language front ends.
//
// A lexer or reader walks a source string and reports positioned failures,
// so the walking and the reporting live here. A byte offset is the single
// source of truth for location: it is what `string_view` slicing needs while
// scanning, and the human-facing line and column are derived from it at the
// error boundary, the one place they are worth computing.

#pragma region source_error

// Description of a failure at a position in source text.
//
// `pos` is the byte offset where the offending construct starts; `line` and
// `col` locate the same spot as a 1-based line number and byte column.
//
// `incomplete` marks errors that more input could repair, such as an
// unterminated list or bracket; a REPL uses it to keep reading instead of
// reporting.
struct source_error final {
  std::string message;
  size_t pos{};
  size_t line{};
  size_t col{};
  bool incomplete{};
};

#pragma endregion
#pragma region source_scanner

// Base for single-pass scanners over source text.
//
// Holds the text and the cursor, the primitive character operations, and
// the `source_error` builders. Everything language-shaped stays in the
// derived scanner: what a token is, what nesting means, and when to fail.
//
// Derived code peeks and takes; it treats the cursor as an opaque bookmark
// (to save for an error message or a token span), never as a number to do
// arithmetic on. Any offset math belongs in a helper here.
class source_scanner {
public:
  // Success-or-`source_error` result of a scan.
  template<typename T>
  using result = value_or_error<T, source_error>;

  explicit source_scanner(std::string_view src) noexcept : src_{src} {}

  [[nodiscard]] size_t cursor() const noexcept { return pos_; }

  // Whether the cursor, advanced `ahead` positions, is at or past the end.
  [[nodiscard]] bool at_end(size_t ahead = 0) const noexcept {
    return pos_ + ahead >= src_.size();
  }

  // Character `ahead` positions past the current one, or '\0' at or past
  // the end.
  [[nodiscard]] char peek(size_t ahead = 0) const noexcept {
    return (pos_ + ahead < src_.size()) ? src_[pos_ + ahead] : '\0';
  }

  // Whether `s` is next at the cursor.
  [[nodiscard]] bool at_text(std::string_view s) const noexcept {
    return src_.substr(pos_).starts_with(s);
  }

  // Whether the cursor is at a newline, as "\n" or "\r\n".
  [[nodiscard]] bool at_newline() const noexcept {
    return peek() == '\n' || (peek() == '\r' && peek(1) == '\n');
  }

  // Take the current character, so long as we're not at the end.
  void take() {
    assert(!at_end());
    ++pos_;
  }

  // Take the current character, asserting that it is `c`.
  void take([[maybe_unused]] char c) {
    assert(peek() == c);
    take();
  }

  // Take `s`, asserting that it is next.
  void take(std::string_view s) {
    assert(at_text(s));
    pos_ += s.size();
  }

  // Take a newline, either "\n" or "\r\n".
  void take_newline() {
    if (peek() == '\r') take();
    take('\n');
  }

  // Take the escape sequence at the cursor into `ch`, which receives the
  // character it denotes.
  //
  // On a malformed escape, returns false and the cursor does not move.
  [[nodiscard]] bool take_escaped(char& ch) {
    auto rest = src_.substr(pos_);
    if (!strings::parse_escaped(rest, ch)) return false;
    pos_ = src_.size() - rest.size();
    return true;
  }

  // Text from `start` up to the cursor.
  [[nodiscard]] std::string_view taken_from(size_t start) const noexcept {
    return src_.substr(start, pos_ - start);
  }

  // Build a failure at the current position, or at `pos`.
  [[nodiscard]] source_error fail(std::string message) const {
    return make_source_error(pos_, std::move(message), false);
  }
  [[nodiscard]] source_error fail_at(size_t pos, std::string message) const {
    return make_source_error(pos, std::move(message), false);
  }

  // Build a failure that more input could repair (see
  // `source_error::incomplete`).
  [[nodiscard]] source_error fail_incomplete(std::string message) const {
    return make_source_error(pos_, std::move(message), true);
  }
  [[nodiscard]] source_error
  fail_incomplete_at(size_t pos, std::string message) const {
    return make_source_error(pos, std::move(message), true);
  }

  [[nodiscard]] source_error
  make_source_error(size_t pos, std::string message, bool incomplete) const {
    // Compute the 1-based line and column from the byte offset.
    size_t line = 1;
    size_t bol = 0;
    for (size_t ndx = 0; ndx < pos; ++ndx)
      if (src_[ndx] == '\n') {
        ++line;
        bol = ndx + 1;
      }
    return source_error{std::move(message), pos, line, pos - bol + 1,
        incomplete};
  }

  std::string_view src_;
  size_t pos_{};
};

#pragma endregion

}} // namespace corvid::lang
