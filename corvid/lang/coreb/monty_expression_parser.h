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
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../containers/core/scoped_value.h"
#include "../../strings/conversion.h"
#include "../source_scanner.h"
#include "hall_reader.h"
#include "monty_lexer.h"
#include "symbols.h"
#include "token_classes.h"
#include "value.h"

namespace corvid { inline namespace lang { namespace coreb { namespace monty {

// Monty expression parser.
//
// Parses one Monty expression from a `token_stream` and desugars it to the
// Hall s-expression the kernel evaluates, consuming exactly the expression's
// tokens; what may follow is the caller's grammar to judge. This layer covers
// everything legal in expression position, which excludes `=` and `:=` because
// they head statements.
//
// The desugar, by example:
//
//   f(x, y)        ->  (f x y)
//   a - b + c      ->  (+ (- a b) c)
//   -x * y         ->  (* (- x) y)
//   -7             ->  -7
//   a < b < c      ->  (< a b c)
//   a + b < c * d  ->  (< (+ a b) (* c d))
//   x if c else y  ->  (if c x y)
//   map((-), xs)   ->  (map - xs)
//   [1, x + 1]     ->  (list 1 (+ x 1))
//   begin(f(), 3)  ->  (begin (f) 3)
//   %(lambda (n) n)  ->  (lambda (n) n)
//
// The grammar in EBNF ("{x}" repetition, "[x]" option, "|" alternation):
//
//   expr    ::= chain [ "if" chain "else" expr ]
//   chain   ::= arith [ cmp arith { cmp arith } ]
//   arith   ::= unary { ( "+" | "-" ) unary }
//             | unary { ( "*" | "/" ) unary }
//   unary   ::= "-" unary | postfix
//   postfix ::= primary { "(" [ expr { "," expr } ] ")" }
//   primary ::= number | string | word | list | hall
//             | "(" operator ")" | "(" expr ")"
//   list    ::= "[" [ expr { "," expr } ] "]"
//   cmp     ::= "==" | "!=" | "<" | "<=" | ">" | ">="
//
// where number/string/word/hall are the lexer's tokens ("nil", "true", and
// "false" reading as literals rather than symbols), a chain's cmp
// occurrences must all be the same operator ("!=" may occur only once),
// `(op)` mentions an operator as a value, and a unary `-` must touch its
// operand, a touching number token taking the sign as part of the literal,
// so `-7` is the literal and negating a number on purpose is spelled by
// mention: `(-)(7)`.
//
// A `hall` token is the `%(...)` escape, a Hall pass: its text is the Hall
// form itself, which is read, not evaluated, and splices in place. The
// sequencer `begin(a, b)` needs no rule of its own. This is because `begin` is
// an ordinary word here, and the call desugar manufactures the kernel form,
// which special-form identity then claims.
//
// The grammar enforces the sparse partial-order precedence structurally: {+ -}
// and {* /} each fold left but mixing the families without parentheses is an
// error rather than a precedence decision, arithmetic sits above comparison,
// and comparison chains desugar to the kernel's chaining primitives.

#pragma region expression_parser

// Parser from a Monty token stream to a Hall value.
//
// Nesting deeper than `max_depth` is rejected rather than risking stack
// exhaustion. Failure is reported by value as a `source_error`.
class expression_parser final {
public:
  template<typename T>
  using result = source_scanner::result<T>;

  // Parse one expression from `toks`, desugared to a Hall value.
  //
  // Consumes exactly the expression's tokens, leaving the rest of the
  // stream, so the caller decides what may legally follow. `depth` seeds
  // the nesting budget for a caller already that deep, so stacked front
  // ends share the one `max_depth` rather than each starting fresh.
  [[nodiscard]] static result<value>
  parse(runtime_environment& run_env, token_stream& toks, size_t depth = 0) {
    return builder{run_env, toks, depth}.parse_expr();
  }

private:
#pragma region builder

  // Single-pass builder from the token stream to a value.
  struct builder {
    runtime_environment& run_env;
    token_stream& toks;
    size_t depth{};
    runtime& rt = run_env.rt;
    const symbols& syms = run_env.syms;

    // The operator table entry at the cursor, or null.
    [[nodiscard]] const operator_symbol* at_operator() const noexcept {
      if (!toks.at(token_kind::op)) return nullptr;
      return find_operator(toks.peek().text);
    }
    [[nodiscard]] const operator_symbol* at_comparison() const noexcept {
      const auto* op = at_operator();
      return op && op->kind == operator_kind::comparison ? op : nullptr;
    }
    [[nodiscard]] const operator_symbol* at_arithmetic() const noexcept {
      const auto* op = at_operator();
      return op && is_arithmetic(op->kind) ? op : nullptr;
    }

    // Parse one expression: a comparison chain, optionally wrapped by the
    // ternary `x if c else y`, which desugars to the kernel `if`. The
    // else-branch parses as another expression, so ternaries chain
    // rightward as in Python.
    [[nodiscard]] result<value> parse_expr() {
      if (depth >= max_depth) return toks.fail("nesting too deep");
      scoped_value guard(depth, depth + 1);
      auto v = parse_chain();
      if (!v) return v;

      if (toks.at_word("if")) {
        toks.take();
        auto c = parse_chain();
        if (!c) return c;

        if (!toks.at_word("else"))
          return toks.fail("expected 'else' after ternary condition");

        toks.take();
        auto e = parse_expr();
        if (!e) return e;

        v = rt.list_of({value{syms.keyword_if}, *c, *v, *e});
      }
      if (toks.at_op("="))
        return toks.fail("'=' is a definition statement, not an expression");

      if (toks.at_op(":="))
        return toks.fail("':=' is reserved for assignment, a statement");

      return v;
    }

    // Parse a comparison chain over arithmetic operands. A chain uses one
    // comparison operator throughout and desugars to the kernel's chaining
    // primitive, so `a < b < c` becomes `(< a b c)`; mixed chains are an
    // error, and `!=` refuses to chain at all.
    [[nodiscard]] result<value> parse_chain() {
      auto l = parse_arith();
      if (!l) return l;
      const auto* cmp = at_comparison();
      if (!cmp) return l;
      toks.take();
      std::vector<value> elems{value{rt.intern(cmp->spelling)}, *l};
      for (;;) {
        auto r = parse_arith();
        if (!r) return r;

        elems.push_back(*r);
        const auto* next = at_comparison();
        if (!next) break;

        if (!cmp->chains)
          return toks.fail(
              "'" + std::string{cmp->spelling} + "' does not chain");

        if (next != cmp)
          return toks.fail("comparison chains cannot mix operators");

        toks.take();
      }
      return rt.list_of(elems);
    }

    // Parse an arithmetic fold. {+ -} and {* /} are families: each folds
    // left into binary forms, and mixing the families without parentheses
    // is an error, per the sparse partial-order precedence design.
    [[nodiscard]] result<value> parse_arith() {
      auto l = parse_unary();
      if (!l) return l;
      const auto* op = at_arithmetic();
      if (!op) return l;
      const auto family = op->kind;
      for (; op; op = at_arithmetic()) {
        if (op->kind != family)
          return toks.fail("mixing '+'/'-' with '*'/'/' requires parentheses");

        toks.take();
        auto r = parse_unary();
        if (!r) return r;

        l = rt.list_of({value{rt.intern(op->spelling)}, *l, *r});
      }
      return l;
    }

    // Parse a unary minus, which must touch its operand.
    //
    // Touching a number token, the sign is part of the literal, as in Hall, so
    // `-7` is the literal; touching anything else, it binds to the postfix
    // chain it precedes and desugars to the kernel's one-argument negation.
    // Negating a number on purpose is spelled by mention: `(-)(7)`.
    [[nodiscard]] result<value> parse_unary() {
      if (depth >= max_depth) return toks.fail("nesting too deep");
      scoped_value guard(depth, depth + 1);
      if (toks.at_op("-")) {
        const auto minus = toks.peek();
        const auto& next = toks.peek(1);
        // A hall token's `pos` names its '(', one past the '%' badge.
        const auto start =
            next.kind == token_kind::hall ? next.pos - 1 : next.pos;
        if (start != minus.pos + 1)
          return toks.fail("unary '-' must touch its operand");

        if (next.kind == token_kind::number) {
          // The tokens touch, so the signed text is one source span.
          const auto text = toks.src().substr(minus.pos, next.text.size() + 1);
          const auto v = to_number(text);
          if (!v) return toks.fail("malformed number");
          toks.take(); // '-'
          toks.take(); // number
          return *v;
        }
        toks.take();
        auto v = parse_unary();
        if (!v) return v;

        return rt.list_of({value{syms.minus}, *v});
      }
      return parse_postfix();
    }

    // Parse a primary and its postfix chain: calls bind tightest and fold
    // left, so `f(a)(b)` is `((f a) b)`.
    [[nodiscard]] result<value> parse_postfix() {
      auto v = parse_primary();
      if (!v) return v;
      for (;;) {
        if (toks.at(token_kind::lbracket))
          return toks.fail("indexing is not yet part of Monty");

        if (!toks.at(token_kind::lparen)) break;

        toks.take();
        std::vector<value> form{*v};
        if (!toks.at(token_kind::rparen))
          for (;;) {
            auto arg = parse_expr();
            if (!arg) return arg;

            form.push_back(*arg);
            if (!toks.at(token_kind::comma)) break;

            toks.take();
          }

        if (!toks.at(token_kind::rparen)) return toks.fail("expected ')'");

        toks.take();
        v = rt.list_of(form);
      }
      return v;
    }

    // Parse a primary: a literal, a word, a parenthesized expression, or a
    // parenthesized operator mention such as `(-)`.
    [[nodiscard]] result<value> parse_primary() {
      switch (toks.peek().kind) {
      case token_kind::number: return parse_number();
      case token_kind::string: return parse_string();
      case token_kind::word: return parse_word();
      case token_kind::hall: return parse_hall();
      case token_kind::lparen: return parse_group();
      case token_kind::lbracket: return parse_list();
      default: return toks.fail("expected an expression");
      }
    }

    // Convert a number's text, applying the kernel's int64-overflow-to-double
    // rule.
    [[nodiscard]] static std::optional<value> to_number(
        std::string_view text) {
      if (const auto n = strings::parse_num<int64_t>(text)) return value{*n};
      if (const auto d = strings::parse_num<double>(text)) return value{*d};
      return std::nullopt;
    }

    // Parse a number token.
    [[nodiscard]] result<value> parse_number() {
      if (const auto v = to_number(toks.peek().text)) {
        toks.take();
        return *v;
      }
      return toks.fail("malformed number");
    }

    // Parse a string token, unescaping the raw quoted text the lexer
    // validated.
    [[nodiscard]] result<value> parse_string() {
      auto rest = toks.peek().text;
      std::string out;
      if (!strings::parse_escaped_quoted(rest, out) || !rest.empty())
        return toks.fail("invalid escape");
      toks.take();
      return value{rt.make_string(std::move(out))};
    }

    // Parse a word: the literals nil/true/false, or an interned symbol.
    [[nodiscard]] result<value> parse_word() {
      const auto text = toks.take().text;
      if (text == "nil") return value{};
      if (text == "true") return value{true};
      if (text == "false") return value{false};
      return value{rt.intern(text)};
    }

    // Parse a Hall escape token, the reader sharing this parser's nesting
    // budget.
    [[nodiscard]] result<value> parse_hall() {
      const auto tok = toks.take();
      auto v = hall_reader::read_one(run_env, tok.text, depth);
      // Patch the error location.
      if (!v) {
        auto e = std::move(v).as_error();
        return source_error::at(toks.src(), tok.pos + e.pos,
            std::move(e.message), e.cause);
      }
      return v;
    }

    // Parse a list literal.
    //
    // This desugars to the kernel `list` constructor, so elements are
    // evaluated: `[1, x]` is `(list 1 x)`, and `[]` is `(list)`, yielding nil.
    [[nodiscard]] result<value> parse_list() {
      toks.take(); // '['
      std::vector<value> form{value{syms.list}};
      if (!toks.at(token_kind::rbracket)) {
        for (;;) {
          auto elem = parse_expr();
          if (!elem) return elem;
          form.push_back(*elem);
          if (!toks.at(token_kind::comma)) break;
          toks.take();
        }
      }
      if (!toks.at(token_kind::rbracket)) return toks.fail("expected ']'");
      toks.take();
      return rt.list_of(form);
    }

    // Parse a parenthesized group: an operator mention such as `(-)`, or a
    // full expression. Every operator mentions, `=` and `:=` included:
    // only their infix spellings are statement-bound.
    [[nodiscard]] result<value> parse_group() {
      toks.take(); // '('
      if (toks.at(token_kind::op) && toks.peek(1).kind == token_kind::rparen) {
        const auto op = toks.take().text;
        toks.take();
        return value{rt.intern(op)};
      }
      auto v = parse_expr();
      if (!v) return v;

      if (!toks.at(token_kind::rparen)) return toks.fail("expected ')'");

      toks.take();
      return v;
    }
  };

#pragma endregion
};

#pragma endregion

}}}} // namespace corvid::lang::coreb::monty
