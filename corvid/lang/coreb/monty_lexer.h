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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "../../enums/sequence_enum.h"
#include "../../strings/cases.h"
#include "../../strings/locating.h"
#include "../source_scanner.h"

namespace corvid { inline namespace lang { namespace coreb { namespace monty {

// Monty lexer.
//
// Monty is CoreB's Pythonesque surface syntax; the design rulings it
// implements are recorded in "coreb.md".
//
// This header tokenizes Monty source into words, operators, numbers, strings,
// punctuation, and the INDENT/DEDENT/NEWLINE structure that Python-style
// blocks require. The parser consumes this stream and desugars it to the
// s-expressions the kernel evaluates.
//
// The token grammar, by example:
//
// - Words lead with a letter or underscore and may end with a single '?',
//   which marks a predicate: `head`, `x2`, `_helper`, `nil?`.
// - Operators come from the closed table, longest match first, so `a<=b`
//   lexes as `a`, `<=`, `b`, and `x:=y` as `x`, `:=`, `y` (a colon is
//   punctuation only when no '=' follows). Blends do not exist: `comb-over`
//   is `comb` minus `over`.
// - Numbers take an optional fraction and exponent: `1`, `2.5`, `.5`, `1e3`,
//   `2E+4`. No sign; `-7` is the operator `-` applied to `7`.
// - Strings are double-quoted and single-line, with the kernel escape set:
//   `"say \"hi\"\n"`.
// - `#` starts a comment running to end of line.
//
// The same grammar in EBNF ("{x}" repetition, "[x]" option, "|"
// alternation):
//
//   token    ::= word | operator | number | string | punct
//   word     ::= ( letter | "_" ) { letter | digit | "_" } [ "?" ]
//   operator ::= "==" | "!=" | "<=" | ">=" | ":=" |
//                "+" | "-" | "*" | "/" | "<" | ">" | "="
//   number   ::= digit { digit } [ frac ] [ exp ] | frac [ exp ]
//   frac     ::= "." digit { digit }
//   exp      ::= ( "e" | "E" ) [ "+" | "-" ] digit { digit }
//   string   ::= '"' { plain | escape } '"'
//   punct    ::= "(" | ")" | "[" | "]" | "," | ":"
//   comment  ::= "#" { not-newline }
//
// where "plain" is any character except '"', '\', and newline, and "escape"
// is the kernel set from "conversion.h": \" \\ \n \t \r \u{hex}.
//
// Around the tokens, the lexer synthesizes line structure: NEWLINE ends each
// logical line, and INDENT/DEDENT bracket each two-space indentation change,
// so
//
//   def f(n):    ->  word:def word:f lparen word:n rparen colon newline
//     n + 1      ->  indent word:n op:+ number:1 newline dedent
//                ->  eof

#pragma region token

// Discriminator for the kinds of token the lexer produces.
enum class token_kind : std::uint8_t {
  word,
  op,
  number,
  string,
  lparen,
  rparen,
  lbracket,
  rbracket,
  comma,
  colon,
  newline,
  indent,
  dedent,
  eof
};
consteval auto corvid_enum_spec(token_kind*) {
  return corvid::enums::sequence::make_sequence_enum_spec<token_kind,
      "word,op,number,string,lparen,rparen,lbracket,rbracket,comma,colon,"
      "newline,indent,dedent,eof">();
}

// One lexed token.
//
// `text` is a view into the source the lexer was given, which must outlive
// the token; synthesized tokens (indent, dedent, eof) have empty text, and a
// string token's text keeps its quotes and escapes raw, for the parser to
// unescape. `pos` is the byte offset where the token starts.
struct token final {
  token_kind kind;
  std::string_view text;
  size_t pos{};

  // Build a failure at this token's position in `src`, the source it was
  // lexed from.
  [[nodiscard]] source_error error_at(std::string_view src,
      std::string message,
      error_cause cause = error_cause::invalid_input) const {
    assert(strings::locate_subview(src, text) == pos);
    return source_error::at(src, pos, std::move(message), cause);
  }
};

#pragma endregion
#pragma region token_stream

// Lexed tokens with a cursor into them, as well as the source text they point
// into (for error reporting).
//
// A token's `pos` and `text` are meaningful only against the whole source, so
// the two travel together. The lexer produces the stream, and parsers consume
// tokens through the cursor, each taking exactly the tokens its grammar covers
// and leaving the rest for its caller to judge. `src` must outlive the stream.
//
// The stream always ends with an `eof` token, which `peek` yields on any
// overshoot and `take` never moves past.
class token_stream final {
public:
  token_stream(std::string_view src, std::vector<token> tokens) noexcept
      : src_{src}, tokens_{std::move(tokens)} {
    assert(!tokens_.empty() && tokens_.back().kind == token_kind::eof);
  }

  [[nodiscard]] std::string_view src() const noexcept { return src_; }
  [[nodiscard]] std::span<const token> tokens() const noexcept {
    return tokens_;
  }

  // Token `ahead` positions past the current one.
  [[nodiscard]] const token& peek(size_t ahead = 0) const noexcept {
    return tokens_[std::min(ndx_ + ahead, tokens_.size() - 1)];
  }

  // Take the current token.
  const token& take() noexcept {
    const auto& t = peek();
    if (ndx_ + 1 < tokens_.size()) ++ndx_;
    return t;
  }

  [[nodiscard]] bool at(token_kind kind) const noexcept {
    return peek().kind == kind;
  }
  [[nodiscard]] bool at_op(std::string_view text) const noexcept {
    return at(token_kind::op) && peek().text == text;
  }
  [[nodiscard]] bool at_word(std::string_view text) const noexcept {
    return at(token_kind::word) && peek().text == text;
  }

  // Build a failure at the current token.
  [[nodiscard]] source_error fail(std::string message) const {
    return peek().error_at(src_, std::move(message));
  }

private:
  std::string_view src_;
  std::vector<token> tokens_;
  size_t ndx_{};
};

#pragma endregion
#pragma region lexer

// Lexer from Monty source text to a token stream.
//
// Tokenizes the whole source at once, ending the stream with `eof`. Blocks
// are two-space indentation: the lexer synthesizes one `indent` per new
// level and matching `dedent` tokens, with a `newline` ending each logical
// line. Indenting more than one level at a time is an error; a leading
// indent at the top of input lexes fine and is the parser's to reject.
//
// Raw tabs are an error outside string literals, blank and comment-only lines
// affect nothing, and inside unclosed brackets newlines and indentation are
// plain whitespace. Strings are single-line, sharing the kernel's escape
// grammar. Failure is reported by value as a `source_error`; an unterminated
// bracket reports `incomplete_input` (more lines could close it), while an
// unterminated string does not, strings being single-line.
class lexer final {
public:
  template<typename T>
  using result = source_scanner::result<T>;

  // Tokenize `src`, which must outlive the returned stream.
  [[nodiscard]] static result<token_stream> lex(std::string_view src) {
    auto r = scanner(src).scan();
    if (!r) return r;
    return token_stream{src, *std::move(r)};
  }

private:
#pragma region scanner

  // Single-pass scanner over the source text.
  struct scanner: source_scanner {
    explicit scanner(std::string_view src) noexcept : source_scanner{src} {}

    size_t depth{};
    bool at_line_start = true;
    bool line_has_content{};
    std::vector<std::pair<char, size_t>> brackets;
    std::vector<token> out;

    void skip_comment() {
      while (!at_end() && !at_newline()) take();
    }

    [[nodiscard]] static bool is_word_lead(char c) noexcept {
      return strings::is_alpha(c) || c == '_';
    }
    [[nodiscard]] static bool is_word_char(char c) noexcept {
      return strings::is_alpha(c) || strings::is_digit(c) || c == '_';
    }

    // Emit a token spanning from `start` to the current position.
    //
    // The `std::monostate` return is `result<void>` success, so an extract
    // method can end with `return push(...);`.
    std::monostate push(token_kind kind, size_t start) {
      out.push_back(token{kind, taken_from(start), start});
      switch (kind) {
      case token_kind::newline: line_has_content = false; break;
      case token_kind::indent:
      case token_kind::dedent:
      case token_kind::eof: break;
      default: line_has_content = true; break;
      }
      return {};
    }

    // Emit a single-character token at the current position.
    std::monostate push1(token_kind kind) {
      const auto start = cursor();
      take();
      return push(kind, start);
    }

    // Tokenize the whole source.
    [[nodiscard]] result<std::vector<token>> scan() {
      for (;;) {
        if (at_line_start && brackets.empty())
          if (auto r = extract_line_start(); !r) return r;

        if (at_end()) break;

        if (auto r = extract_token(); !r) return r;
      }

      if (!brackets.empty())
        return fail("unterminated bracket", brackets.back().second,
            error_cause::incomplete_input);

      if (line_has_content) push(token_kind::newline, cursor());

      for (; depth > 0; --depth) push(token_kind::dedent, cursor());

      push(token_kind::eof, cursor());
      return std::move(out);
    }

    // Dispatch on the current character: whitespace, a comment, or one
    // token.
    [[nodiscard]] result<void> extract_token() {
      const char c = peek();
      if (c == ' ') {
        take();
        return std::monostate{};
      }
      if (c == '\t') return fail("tab character");
      if (at_newline()) {
        extract_newline();
        return std::monostate{};
      }
      if (c == '\r') return fail("stray carriage return");
      if (c == '#') {
        skip_comment();
        return std::monostate{};
      }
      if (c == '"') return extract_string();
      if (strings::is_digit(c) || (c == '.' && strings::is_digit(peek(1))))
        return extract_number();
      if (is_word_lead(c)) return extract_word();
      return extract_punct(c);
    }

    // Consume a newline: a statement separator outside brackets, plain
    // whitespace inside them.
    void extract_newline() {
      if (brackets.empty()) {
        const auto start = cursor();
        take_newline();
        push(token_kind::newline, start);
        at_line_start = true;
      } else
        take_newline();
    }

    // Lex punctuation, tracking bracket nesting, or fall through to the
    // operator table.
    [[nodiscard]] result<void> extract_punct(char c) {
      if (c == '(' || c == '[') {
        brackets.emplace_back(c, cursor());
        return push1(c == '(' ? token_kind::lparen : token_kind::lbracket);
      }
      if (c == ')' || c == ']') {
        const char open = (c == ')') ? '(' : '[';
        if (brackets.empty() || brackets.back().first != open)
          return fail(std::string("unmatched '") + c + "'");
        brackets.pop_back();
        return push1(c == ')' ? token_kind::rparen : token_kind::rbracket);
      }
      if (c == ',') return push1(token_kind::comma);
      // A ':' is punctuation only when no '=' follows; ':=' is an operator.
      if (c == ':' && peek(1) != '=') return push1(token_kind::colon);

      return extract_operator();
    }

    // Handle the start of a logical line: skip blank and comment-only lines,
    // then check the indentation and emit indent or dedent tokens.
    [[nodiscard]] result<void> extract_line_start() {
      for (;;) {
        size_t count = 0;
        while (peek() == ' ') {
          take();
          ++count;
        }
        if (peek() == '\t') return fail("tab character");
        if (at_end()) return std::monostate{};
        if (at_newline()) {
          take_newline();
          continue;
        }
        if (peek() == '#') {
          skip_comment();
          if (!at_end()) take_newline();
          continue;
        }
        if (count % 2 != 0)
          return fail("indentation is not a multiple of two spaces");

        const auto units = count / 2;
        if (units > depth + 1)
          return fail("indentation jumps more than one level");

        if (units == depth + 1) {
          push(token_kind::indent, cursor());
          ++depth;
        } else {
          for (; depth > units; --depth) push(token_kind::dedent, cursor());
        }
        at_line_start = false;
        return std::monostate{};
      }
    }

    // Lex a quoted single-line string, validating its escapes; the token
    // text keeps the quotes and escapes raw for the parser to unescape.
    [[nodiscard]] result<void> extract_string() {
      const auto start = cursor();
      take('"');
      for (;;) {
        if (at_end() || at_newline())
          return fail("unterminated string", start);

        const char c = peek();
        if (c == '"') {
          take('"');
          return push(token_kind::string, start);
        }
        if (c == '\\') {
          char ch{};
          if (!take_escaped(ch)) return fail("invalid escape");
          continue;
        }
        take();
      }
    }

    // Lex a number: digits, optional fraction, optional exponent. The
    // parser converts, applying the kernel's int64-overflow-to-double rule.
    [[nodiscard]] result<void> extract_number() {
      const auto start = cursor();
      while (strings::is_digit(peek())) take();
      if (peek() == '.' && strings::is_digit(peek(1))) {
        take();
        while (strings::is_digit(peek())) take();
      }
      if (peek() == 'e' || peek() == 'E') {
        take();
        if (peek() == '+' || peek() == '-') take();
        if (!strings::is_digit(peek())) return fail("malformed number", start);
        while (strings::is_digit(peek())) take();
      }
      if (is_word_char(peek()) || peek() == '.')
        return fail("malformed number", start);
      return push(token_kind::number, start);
    }

    // Lex a word symbol: a leading letter or underscore, word characters,
    // and an optional final '?', which must in fact be final.
    [[nodiscard]] result<void> extract_word() {
      const auto start = cursor();
      take();
      while (is_word_char(peek())) take();
      if (peek() == '?') take();
      if (is_word_char(peek()) || peek() == '?')
        return fail("'?' can only end a name");
      return push(token_kind::word, start);
    }

    // Lex an operator symbol from the closed operator table, longest match
    // first.
    [[nodiscard]] result<void> extract_operator() {
      static constexpr std::string_view two_char_ops[]{
          "==", "!=", "<=", ">=", ":="};
      const auto start = cursor();
      for (const auto op : two_char_ops)
        if (at_text(op)) {
          take(op);
          return push(token_kind::op, start);
        }
      constexpr std::string_view one_char_ops = "+-*/<>=";
      if (one_char_ops.contains(peek())) return push1(token_kind::op);
      if (peek() == '!') return fail("'!' appears only in '!='");
      return fail("unexpected character");
    }
  };

#pragma endregion
};

#pragma endregion

}}}} // namespace corvid::lang::coreb::monty
