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
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../containers/core/scoped_value.h"
#include "../../strings/conversion.h"
#include "../source_scanner.h"
#include "monty_lexer.h"
#include "value.h"

namespace corvid { inline namespace lang { namespace coreb { namespace monty {

// Monty expression parser.
//
// Parses one Monty expression from the lexer's token stream and desugars it
// to the Hall s-expression the kernel evaluates. This layer covers
// everything legal in expression position. Statements (definition,
// assignment, blocks) belong to the statement parser, still to come, and
// `=`/`:=` are rejected here with messages saying so.
//
// The desugar, by example:
//
//   f(x, y)        ->  (f x y)
//   a - b + c      ->  (+ (- a b) c)
//   -x * y         ->  (* (- x) y)
//   a < b < c      ->  (< a b c)
//   a + b < c * d  ->  (< (+ a b) (* c d))
//   x if c else y  ->  (if c x y)
//   map((-), xs)   ->  (map - xs)
//
// The grammar in EBNF ("{x}" repetition, "[x]" option, "|" alternation):
//
//   expr    ::= chain [ "if" chain "else" expr ]
//   chain   ::= arith [ cmp arith { cmp arith } ]
//   arith   ::= unary { ( "+" | "-" ) unary }
//             | unary { ( "*" | "/" ) unary }
//   unary   ::= "-" unary | postfix
//   postfix ::= primary { "(" [ expr { "," expr } ] ")" }
//   primary ::= number | string | word | "(" operator ")" | "(" expr ")"
//   cmp     ::= "==" | "!=" | "<" | "<=" | ">" | ">="
//
// where number/string/word are the lexer's tokens ("nil", "true", and
// "false" reading as literals rather than symbols), a chain's cmp
// occurrences must all be the same operator ("!=" may occur only once), and
// `(op)` mentions an operator as a value.
//
// The grammar enforces the sparse partial-order precedence ruling from
// "coreb.md" structurally: {+ -} and {* /} each fold left but mixing the
// families without parentheses is an error rather than a precedence
// decision, arithmetic sits above comparison, and comparison chains desugar
// to the kernel's chaining primitives.

#pragma region parser

// Parser from Monty expression source to a Hall value.
//
// Nesting deeper than `max_depth` is rejected rather than risking stack
// exhaustion. Failure is reported by value as a `source_error`; lexer
// failures pass through, so an unterminated bracket stays `incomplete`.
class parser final {
public:
  template<typename T>
  using result = source_scanner::result<T>;

  // Maximum expression-nesting depth accepted, matching
  // `hall_reader::max_depth`.
  static constexpr size_t max_depth = 256;

  // Parse exactly one expression from `src`, desugared to a Hall value.
  //
  // Anything but a trailing newline after the expression is an error.
  [[nodiscard]] static result<value>
  parse_expression(runtime& rt, std::string_view src) {
    auto toks = lexer::lex(src);
    if (!toks) return toks;

    builder b(rt, src, *toks);

    auto v = b.parse_expr();
    if (!v) return v;

    if (b.at(token_kind::newline)) b.take();
    if (!b.at(token_kind::eof))
      return b.fail("trailing content after expression");

    return v;
  }

private:
#pragma region builder

  // Single-pass builder from the token stream to a value.
  struct builder {
    builder(runtime& rt, std::string_view src,
        std::span<const token> toks) noexcept
        : rt{rt}, errors{src}, toks{toks} {}

    runtime& rt;
    source_scanner errors; // Used only for position errors.
    std::span<const token> toks;
    size_t ndx{};
    size_t depth{};

    // Token `ahead` positions past the current one; the trailing `eof`
    // token absorbs any overshoot.
    [[nodiscard]] const token& peek(size_t ahead = 0) const noexcept {
      return toks[std::min(ndx + ahead, toks.size() - 1)];
    }

    // Take the current token; the trailing `eof` token is never passed.
    const token& take() noexcept {
      const auto& t = peek();
      if (ndx + 1 < toks.size()) ++ndx;
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
    [[nodiscard]] bool at_cmp() const noexcept {
      constexpr std::string_view cmp_ops[]{"==", "!=", "<", "<=", ">", ">="};
      return at(token_kind::op) && std::ranges::contains(cmp_ops, peek().text);
    }
    [[nodiscard]] bool at_arith() const noexcept {
      return at_op("+") || at_op("-") || at_op("*") || at_op("/");
    }

    // Build a failure at the current token.
    [[nodiscard]] source_error fail(std::string message) const {
      return errors.make_source_error(peek().pos, std::move(message), false);
    }

    // Build the list `(elems...)` as nested cons cells.
    [[nodiscard]] value list_of(std::span<const value> elems) const {
      value list;
      for (const auto& elem : std::views::reverse(elems))
        list = rt.cons(elem, list);
      return list;
    }
    [[nodiscard]] value list_of(std::initializer_list<value> elems) const {
      return list_of(std::span<const value>{elems.begin(), elems.size()});
    }

    // Parse one expression: a comparison chain, optionally wrapped by the
    // ternary `x if c else y`, which desugars to the kernel `if`. The
    // else-branch parses as another expression, so ternaries chain
    // rightward as in Python.
    [[nodiscard]] result<value> parse_expr() {
      if (depth >= max_depth) return fail("nesting too deep");
      scoped_value guard(depth, depth + 1);
      auto v = parse_chain();
      if (!v) return v;
      if (at_word("if")) {
        take();
        auto c = parse_chain();
        if (!c) return c;
        if (!at_word("else"))
          return fail("expected 'else' after ternary condition");
        take();
        auto e = parse_expr();
        if (!e) return e;
        v = list_of({value{rt.intern("if")}, *c, *v, *e});
      }
      if (at_op("="))
        return fail("'=' is a definition statement, not an expression");
      if (at_op(":="))
        return fail("':=' is reserved for assignment, a statement");
      return v;
    }

    // Parse a comparison chain over arithmetic operands. A chain uses one
    // comparison operator throughout and desugars to the kernel's chaining
    // primitive, so `a < b < c` becomes `(< a b c)`; mixed chains await
    // `and`, and `!=` refuses to chain at all.
    [[nodiscard]] result<value> parse_chain() {
      auto l = parse_arith();
      if (!l) return l;
      if (!at_cmp()) return l;
      const auto op = take().text;
      std::vector<value> elems{value{rt.intern(op)}, *l};
      for (;;) {
        auto r = parse_arith();
        if (!r) return r;
        elems.push_back(*r);
        if (!at_cmp()) break;
        if (op == "!=") return fail("'!=' does not chain");
        if (peek().text != op)
          return fail("comparison chains cannot mix operators");
        take();
      }
      return list_of(elems);
    }

    // Parse an arithmetic fold. {+ -} and {* /} are families: each folds
    // left into binary forms, and mixing the families without parentheses
    // is an error, per the sparse partial-order precedence ruling.
    [[nodiscard]] result<value> parse_arith() {
      auto l = parse_unary();
      if (!l) return l;
      const bool additive = at_op("+") || at_op("-");
      while (at_arith()) {
        if ((at_op("+") || at_op("-")) != additive)
          return fail("mixing '+'/'-' with '*'/'/' requires parentheses");
        const auto op = take().text;
        auto r = parse_unary();
        if (!r) return r;
        l = list_of({value{rt.intern(op)}, *l, *r});
      }
      return l;
    }

    // Parse a unary minus, which binds to the postfix chain it precedes
    // and desugars to the kernel's one-argument negation.
    [[nodiscard]] result<value> parse_unary() {
      if (depth >= max_depth) return fail("nesting too deep");
      scoped_value guard(depth, depth + 1);
      if (at_op("-")) {
        take();
        auto v = parse_unary();
        if (!v) return v;
        return list_of({value{rt.intern("-")}, *v});
      }
      return parse_postfix();
    }

    // Parse a primary and its postfix chain: calls bind tightest and fold
    // left, so `f(a)(b)` is `((f a) b)`.
    [[nodiscard]] result<value> parse_postfix() {
      auto v = parse_primary();
      if (!v) return v;
      for (;;) {
        if (at(token_kind::lbracket))
          return fail("indexing is not yet part of Monty");
        if (!at(token_kind::lparen)) break;
        take();
        std::vector<value> form{*v};
        if (!at(token_kind::rparen)) {
          for (;;) {
            auto arg = parse_expr();
            if (!arg) return arg;
            form.push_back(*arg);
            if (!at(token_kind::comma)) break;
            take();
          }
        }
        if (!at(token_kind::rparen)) return fail("expected ')'");
        take();
        v = list_of(form);
      }
      return v;
    }

    // Parse a primary: a literal, a word, a parenthesized expression, or a
    // parenthesized operator mention such as `(-)`.
    [[nodiscard]] result<value> parse_primary() {
      switch (peek().kind) {
      case token_kind::number: return parse_number();
      case token_kind::string: return parse_string();
      case token_kind::word: return parse_word();
      case token_kind::lparen: return parse_group();
      case token_kind::lbracket:
        return fail("list literals are not yet supported");
      default: return fail("expected an expression");
      }
    }

    // Parse a number token, applying the kernel's int64-overflow-to-double
    // rule.
    [[nodiscard]] result<value> parse_number() {
      const auto text = peek().text;
      if (const auto n = strings::parse_num<int64_t>(text)) {
        take();
        return value{*n};
      }
      if (const auto d = strings::parse_num<double>(text)) {
        take();
        return value{*d};
      }
      return fail("malformed number");
    }

    // Parse a string token, unescaping the raw quoted text the lexer
    // validated.
    [[nodiscard]] result<value> parse_string() {
      auto rest = peek().text;
      std::string out;
      if (!strings::parse_escaped_quoted(rest, out) || !rest.empty())
        return fail("invalid escape");
      take();
      return value{rt.make_string(std::move(out))};
    }

    // Parse a word: the literals nil/true/false, or an interned symbol.
    [[nodiscard]] result<value> parse_word() {
      const auto text = take().text;
      if (text == "nil") return value{};
      if (text == "true") return value{true};
      if (text == "false") return value{false};
      return value{rt.intern(text)};
    }

    // Parse a parenthesized group: an operator mention such as `(-)`, or a
    // full expression.
    [[nodiscard]] result<value> parse_group() {
      take(); // '('
      if (at(token_kind::op) && peek(1).kind == token_kind::rparen) {
        const auto op = peek().text;
        if (op == "=" || op == ":=")
          return fail("'" + std::string{op} + "' is a statement, not a value");
        take();
        take();
        return value{rt.intern(op)};
      }
      auto v = parse_expr();
      if (!v) return v;
      if (!at(token_kind::rparen)) return fail("expected ')'");
      take();
      return v;
    }
  };

#pragma endregion
};

#pragma endregion

}}}} // namespace corvid::lang::coreb::monty
