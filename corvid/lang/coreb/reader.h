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
#include <cstdint>
#include <expected>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../strings/cases.h"
#include "../../strings/conversion.h"
#include "value.h"

namespace corvid { inline namespace lang { namespace coreb {

// CoreB s-expression reader.
//
// Parses parenthesized source text into values. This is the kernel's internal
// bootstrap format, not the CoreB surface syntax (which arrives in milestone
// 4 as a separate parser producing the same values).
//
//   runtime rt;
//   auto v = reader::read_one(rt, "(a 1 2.5)");
//   if (v) v->print();  // "(a 1 2.5)"

#pragma region read_error

// Description of a failed read.
//
// `pos` is the byte offset where the offending construct starts; `line` and
// `col` locate the same spot as a 1-based line number and byte column.
struct read_error final {
  std::string message;
  size_t pos{};
  size_t line{};
  size_t col{};
};

#pragma endregion
#pragma region reader

// Reader from s-expression source text to values.
//
// The grammar:
// - Lists are parenthesized, whitespace-separated, and may end with a dotted
//   tail: "(a b . c)". "()" reads as nil.
// - "nil", "true", and "false" are literals, not symbols.
// - A token starting with a digit, or with a sign or '.' followed by a digit,
//   is a number: first tried as a 64-bit integer, then as a double, so an
//   integer too large for int64 falls back to floating point.
// - (quote x) may be written 'x.
// - Strings are double-quoted; the escapes are \" \\ \n \t \r, plus \u{hex}
//   denoting a byte by value.
// - Any other token is a symbol, interned in the runtime. Symbols starting
//   with '%' are reserved for kernel-generated forms (see "coreb.md"); the
//   reader accepts them, and definition is policed by the evaluator.
// - ';' starts a comment running to end of line. Comments and whitespace are
//   dropped entirely for now; representing them for round-tripping is a
//   deferred goal (see "coreb.md").
//
// The same grammar in EBNF ("{x}" repetition, "[x]" option, "|" alternation):
//
//   unit    ::= trivia { expr trivia }
//   expr    ::= list | string | quote | atom
//   list    ::= "(" trivia { expr trivia } [ "." trivia expr trivia ] ")"
//   quote   ::= "'" trivia expr
//   string  ::= '"' { plain | escape } '"'
//   escape  ::= "\" ( '"' | "\" | "n" | "t" | "r" | "u{" hex { hex } "}" )
//   atom    ::= char { char }
//   trivia  ::= { space | comment }
//   comment ::= ";" { not-newline }
//
// where "plain" is any character but '"' or '\', "char" is any character that
// is not a delimiter (whitespace, parentheses, '"', ';', '\''), a dotted tail
// requires at least one preceding element, and the "." must stand alone as a
// token. `read_one` accepts a single trivia-surrounded `expr`; `read_all`
// accepts `unit`.
//
// Failure is reported by value as a `read_error`; nesting deeper than
// `max_depth` is rejected rather than risking stack exhaustion.
class reader final {
public:
  template<typename T>
  using result = std::expected<T, read_error>;

  // Maximum expression-nesting depth accepted.
  //
  // Depth counts nested expressions (sublists and quotes), not list length: a
  // flat thousand-element table is depth 1, so only genuinely nested
  // structure approaches the limit, which exists to keep recursion from
  // exhausting the C++ stack.
  static constexpr size_t max_depth = 256;

  // Read exactly one expression from `src`.
  //
  // Anything but trailing trivia after the expression is an error.
  [[nodiscard]] static result<value>
  read_one(runtime& rt, std::string_view src) {
    parser p{rt, src};
    p.skip_trivia();
    auto v = p.parse_value();
    if (!v) return v;
    p.skip_trivia();
    if (!p.at_end()) return p.fail("trailing content after expression");
    return v;
  }

  // Read every expression in `src`, in order.
  //
  // All-trivia input yields an empty vector.
  [[nodiscard]] static result<std::vector<value>>
  read_all(runtime& rt, std::string_view src) {
    std::vector<value> values;
    parser p{rt, src};
    for (p.skip_trivia(); !p.at_end(); p.skip_trivia()) {
      auto v = p.parse_value();
      if (!v) return std::unexpected{v.error()};
      values.push_back(*v);
    }
    return values;
  }

private:
#pragma region parser

  // Single-pass recursive-descent parser over the source text.
  struct parser {
    runtime& rt;
    std::string_view src;
    size_t pos{};
    size_t depth{};

    [[nodiscard]] bool at_end() const noexcept { return pos == src.size(); }

    // Character `ahead` positions past the current one, or '\0' at or past
    // the end.
    [[nodiscard]] char peek(size_t ahead = 0) const noexcept {
      return (pos + ahead < src.size()) ? src[pos + ahead] : '\0';
    }

    // Advance past the current character, so long as we're not at the end.
    void consume() {
      assert(!at_end());
      ++pos;
    }

    // Advance past the current character, asserting that it is `c`.
    void consume([[maybe_unused]] char c) {
      assert(peek() == c);
      consume();
    }

    // Whether `c` ends a token.
    //
    // NUL is a delimiter, matching what `peek` returns for end of input.
    [[nodiscard]] static constexpr bool is_delimiter(char c) noexcept {
      return c == '\0' || strings::is_space(c) || c == '(' || c == ')' ||
             c == '"' || c == ';' || c == '\'';
    }

    // Skip whitespace and ';' line comments.
    void skip_trivia() {
      for (;;) {
        if (strings::is_space(peek())) {
          consume();
          continue;
        }
        if (peek() == ';') {
          while (peek() && peek() != '\n') consume();
          continue;
        }
        break;
      }
    }

    // Build a failure at the current position, or at `at`.
    [[nodiscard]] std::unexpected<read_error> fail(std::string message) const {
      return fail_at(pos, std::move(message));
    }
    [[nodiscard]] std::unexpected<read_error>
    fail_at(size_t at, std::string message) const {
      size_t line = 1;
      size_t bol = 0;
      for (size_t ndx = 0; ndx < at; ++ndx)
        if (src[ndx] == '\n') {
          ++line;
          bol = ndx + 1;
        }
      return std::unexpected{
          read_error{std::move(message), at, line, at - bol + 1}};
    }

    // Parse one expression starting at the current position, guarding
    // recursion depth.
    [[nodiscard]] result<value> parse_value() {
      if (depth >= max_depth) return fail("nesting too deep");
      ++depth;
      auto r = do_parse_value();
      --depth;
      return r;
    }

    [[nodiscard]] result<value> do_parse_value() {
      if (at_end()) return fail("unexpected end of input");
      switch (peek()) {
      case '(': return parse_list();
      case ')': return fail("unmatched ')'");
      case '"': return parse_string();
      case '\'': return parse_quote();
      default: return parse_atom();
      }
    }

    // Parse a ' quotation, which is syntactic sugar for `(quote expr)`.
    [[nodiscard]] result<value> parse_quote() {
      consume('\'');
      skip_trivia();
      auto quoted = parse_value();
      if (!quoted) return quoted;
      return rt.cons(value{rt.intern("quote")}, rt.cons(*quoted, value{}));
    }

    // Whether the upcoming token is the lone '.' of a dotted tail.
    [[nodiscard]] bool at_dot() const noexcept {
      return peek() == '.' && is_delimiter(peek(1));
    }

    // Parse a parenthesized list.
    [[nodiscard]] result<value> parse_list() {
      const auto open_pos = pos;
      consume('(');
      std::vector<value> elems;
      value tail;
      for (;;) {
        skip_trivia();
        if (at_end()) return fail_at(open_pos, "unterminated list");
        if (peek() == ')') {
          consume(')');
          break;
        }
        if (at_dot()) {
          if (elems.empty()) return fail("misplaced '.'");
          auto t = parse_dotted_tail(open_pos);
          if (!t) return t;
          tail = *t;
          break;
        }
        auto v = parse_value();
        if (!v) return v;
        elems.push_back(*v);
      }
      value list = tail;
      for (const auto& elem : std::views::reverse(elems))
        list = rt.cons(elem, list);
      return list;
    }

    // Parse a dotted tail, ". expr )", with the '.' already detected but not
    // consumed.
    //
    // Returns the tail expression.
    [[nodiscard]] result<value> parse_dotted_tail(size_t open_pos) {
      consume('.');
      skip_trivia();
      if (at_end()) return fail_at(open_pos, "unterminated list");
      if (peek() == ')') return fail("expected expression after '.'");
      auto t = parse_value();
      if (!t) return t;
      skip_trivia();
      if (at_end()) return fail_at(open_pos, "unterminated list");
      if (peek() != ')') return fail("expected ')' after dotted tail");
      consume(')');
      return t;
    }

    // Parse a quoted string literal.
    [[nodiscard]] result<value> parse_string() {
      const auto open_pos = pos;
      consume('"');
      std::string out;
      while (!at_end()) {
        const char c = peek();
        if (c == '"') {
          consume('"');
          return value{rt.make_string(std::move(out))};
        }
        if (c == '\\') {
          if (pos + 1 == src.size()) break; // dangling '\' at end of input
          auto rest = src.substr(pos);
          char ch{};
          if (!strings::parse_escaped(rest, ch)) return fail("invalid escape");
          pos = src.size() - rest.size();
          out += ch;
          continue;
        }
        out += c;
        consume();
      }
      return fail_at(open_pos, "unterminated string");
    }

    // Whether `t` is claimed by the number grammar.
    //
    // What the grammar claims must then parse, so "1abc" is an error rather
    // than a symbol.
    [[nodiscard]] static bool looks_numeric(std::string_view t) noexcept {
      size_t ndx = 0;
      if (t[ndx] == '+' || t[ndx] == '-') ++ndx;
      if (ndx == t.size()) return false;
      if (strings::is_digit(t[ndx])) return true;
      return t[ndx] == '.' && ndx + 1 < t.size() &&
             strings::is_digit(t[ndx + 1]);
    }

    // Parse a literal, number, or symbol token.
    [[nodiscard]] result<value> parse_atom() {
      const auto start = pos;
      while (!is_delimiter(peek())) consume();
      const auto token = src.substr(start, pos - start);
      if (token.empty()) return fail("unexpected character");
      if (token == "nil") return value{};
      if (token == "true") return value{true};
      if (token == "false") return value{false};
      if (looks_numeric(token)) {
        // `from_chars` (under `parse_num`) rejects a leading '+', so strip
        // it.
        const auto digits = (token.front() == '+') ? token.substr(1) : token;
        if (const auto n = strings::parse_num<int64_t>(digits))
          return value{*n};
        if (const auto d = strings::parse_num<double>(digits))
          return value{*d};
        return fail_at(start, "malformed number");
      }
      return value{rt.intern(token)};
    }
  };

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::lang::coreb
