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
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "../../containers/core/scoped_value.h"
#include "../source_scanner.h"
#include "monty_expression_parser.h"
#include "monty_lexer.h"
#include "runtime.h"
#include "token_classes.h"
#include "value.h"

namespace corvid { inline namespace lang { namespace coreb { namespace monty {

// Monty statement parser.
//
// Parses Monty statements from a `token_stream` and desugars them to the
// Hall forms the kernel evaluates, consuming exactly the statements'
// tokens. Expression positions inside statements are handed to the
// expression parser over the same stream.
//
// The desugar, by example:
//
//   x = 5            ->  (define x 5)
//   f(x)             ->  (f x)
//   fun inc(n):      ->  (define inc (lambda (n) (+ n 1)))
//     n + 1
//   if c:            ->  (if c (begin (f)) (begin (g)))
//     f()
//   else:
//     g()
//
// The grammar in EBNF ("{x}" repetition, "[x]" option, "|" alternation),
// over the lexer's NEWLINE/INDENT/DEDENT structure:
//
//   program ::= { statement }
//   statement ::= define | reserve | fun | if | return | expr NEWLINE
//   define  ::= word "=" expr NEWLINE
//   reserve ::= word ":=" expr NEWLINE
//   fun     ::= "fun" fname "(" [ word { "," word } ] ")" block
//   fname   ::= word | "(" operator ")"
//   if      ::= "if" expr block { "elif" expr block } [ "else" block ]
//   return  ::= "return" [ expr ] NEWLINE
//   block   ::= ":" NEWLINE INDENT statement { statement } DEDENT
//
// where fun/if/elif/else/return are contextual words, recognized in
// statement-leading position only when no `=` or `:=` follows (the definition
// reading wins, so keywords can be bound as variables; a statement otherwise
// led by the spelling reads as the keyword form, with grouping parens
// stripping the claim: `(fun + 1)` is an expression statement), a `fun` name
// is a word or an operator mention (`fun (-)(a, b):` rebinds subtraction), `=`
// desugars 1:1 to the kernel `define`, `:=` is rejected with a message
// reserving it for assignment, and blocks desugar to `(begin ...)` sequences
// except a `fun` body, which splats into the lambda's implicit sequence.
//
// `return` follows the restricted-return design: a final `return e` is just
// `e`, and an else-less `if` whose every arm ends in `return` takes the
// remainder of the body as its else branch, the guard clause idiom. Any deeper
// `return` is an error, as is one outside `fun`.

#pragma region statement_parser

// Parser from a Monty token stream to Hall statement forms.
//
// Nesting deeper than `max_depth` is rejected rather than risking stack
// exhaustion; the current depth seeds the expression parser at expression
// positions, which seeds the reader inside a Hall escape, so the stacked
// front ends share one budget. Failure is reported by value as a
// `source_error`.
class statement_parser final {
public:
  template<typename T>
  using result = source_scanner::result<T>;

  // Parse one statement from `toks`, desugared to a Hall value.
  //
  // Consumes exactly the statement's tokens, through its trailing newline
  // (or its block's dedent), leaving the rest of the stream.
  [[nodiscard]] static result<value> parse(runtime& rt, token_stream& toks) {
    builder b{rt, toks};
    auto s = b.parse_statement();
    if (!s) return s;

    auto body = b.desugar_body(std::span{&*s, 1}, false, false);
    if (!body) return body;

    return (*body)[0];
  }

  // Parse every statement in `toks`, in order, up to end of input.
  [[nodiscard]] static result<std::vector<value>>
  parse_all(runtime& rt, token_stream& toks) {
    builder b{rt, toks};
    std::vector<stmt> stmts;
    while (!toks.at(token_kind::eof)) {
      auto s = b.parse_statement();
      if (!s) return s;

      stmts.push_back(*std::move(s));
    }
    return b.desugar_body(stmts, false, false);
  }

private:
#pragma region statements

  // Parsed statements that the restricted-return rewrite must still see
  // structurally: everything else desugars to its Hall `value` at parse
  // time, but `return` and `if` wait for the enclosing body's rewrite.

  struct stmt;

  // A `return`; `expr` is nil for the bare spelling, and `tok` locates the
  // statement for rewrite errors.
  struct ret_stmt {
    value expr;
    token tok;
  };

  // One `if` or `elif` arm: its condition and block.
  struct if_arm {
    value cond;
    std::vector<stmt> body;
  };

  // An `if` statement: the if/elif arms, and the else block if present.
  struct if_stmt {
    std::vector<if_arm> arms;
    std::vector<stmt> else_body;
    bool has_else{};
  };

  struct stmt {
    std::variant<value, if_stmt, ret_stmt> node;
  };

#pragma endregion
#pragma region builder

  // Single-pass builder from the token stream to statement forms.
  struct builder {
    runtime& rt;
    token_stream& toks;
    size_t depth{};

    // Build `(begin body...)`.
    [[nodiscard]] value begin_of(std::span<const value> body) const {
      std::vector<value> form{value{rt.sym_begin}};
      form.insert(form.end(), body.begin(), body.end());
      return rt.list_of(form);
    }

    // Take the newline ending a simple statement.
    [[nodiscard]] std::optional<source_error> take_line_end() {
      if (!toks.at(token_kind::newline))
        return toks.fail("expected end of line");

      toks.take();
      return std::nullopt;
    }

    // Parse one statement, dispatching on its leading tokens; the `=`/`:=`
    // lookahead wins over the keyword reading, keeping keywords contextual.
    [[nodiscard]] result<stmt> parse_statement() {
      if (depth >= max_depth) return toks.fail("nesting too deep");
      scoped_value guard(depth, depth + 1);
      if (toks.at(token_kind::indent)) return toks.fail("unexpected indent");
      if (toks.at(token_kind::eof)) return toks.fail("expected a statement");
      if (toks.at(token_kind::word) && toks.peek(1).kind == token_kind::op) {
        if (toks.peek(1).text == "=") return parse_define();
        if (toks.peek(1).text == ":=")
          return toks.fail("':=' assignment is not yet part of Monty");
      }
      if (toks.at_word("fun")) return parse_fun();
      if (toks.at_word("if")) return parse_if();
      if (toks.at_word("return")) return parse_return();
      auto v = expression_parser::parse(rt, toks, depth);
      if (!v) return v;

      if (auto e = take_line_end()) return std::move(*e);
      return stmt{*v};
    }

    // Reject a leading literal word in a binding position.
    [[nodiscard]] std::optional<source_error> reject_literal() const {
      const auto name = toks.peek().text;
      if (!is_literal_word(name)) return std::nullopt;
      return toks.fail("'" + std::string{name} + "' is a literal, not a name");
    }

    // Parse a definition: `name = expr` desugars 1:1 to `(define name
    // expr)`.
    //
    // The literal words read as values, so they name no binding.
    [[nodiscard]] result<stmt> parse_define() {
      if (auto e = reject_literal()) return std::move(*e);
      const auto name = toks.take().text;
      toks.take(); // '='
      auto v = expression_parser::parse(rt, toks, depth);
      if (!v) return v;

      if (auto e = take_line_end()) return std::move(*e);
      return stmt{
          rt.list_of({value{rt.sym_define}, value{rt.intern(name)}, *v})};
    }

    // Parse a function definition.
    //
    // This desugars to a `define` of a lambda. The block splats into the
    // lambda's implicit sequence, with the restricted-return rewrite applied.
    // The name is a word or an operator mention, so `fun (-)(a, b):` rebinds
    // subtraction; literal words name no binding.
    [[nodiscard]] result<stmt> parse_fun() {
      toks.take(); // 'fun'
      std::string_view name;
      if (toks.at(token_kind::word)) {
        if (auto e = reject_literal()) return std::move(*e);
        name = toks.take().text;
      } else if (toks.at(token_kind::lparen) &&
                 toks.peek(1).kind == token_kind::op &&
                 toks.peek(2).kind == token_kind::rparen)
      {
        toks.take(); // '('
        name = toks.take().text;
        toks.take(); // ')'
      } else
        return toks.fail("expected a function name");

      if (!toks.at(token_kind::lparen)) return toks.fail("expected '('");

      toks.take();
      std::vector<value> params;
      if (!toks.at(token_kind::rparen)) {
        for (;;) {
          if (!toks.at(token_kind::word))
            return toks.fail("expected a parameter name");

          if (auto e = reject_literal()) return std::move(*e);
          params.emplace_back(rt.intern(toks.take().text));
          if (!toks.at(token_kind::comma)) break;
          toks.take();
        }
      }
      if (!toks.at(token_kind::rparen)) return toks.fail("expected ')'");
      toks.take();
      auto block = parse_block();
      if (!block) return block;

      auto body = desugar_body(*block, true, true);
      if (!body) return body;

      std::vector<value> lambda{value{rt.sym_lambda}, rt.list_of(params)};
      lambda.insert(lambda.end(), body->begin(), body->end());
      return stmt{rt.list_of(
          {value{rt.sym_define}, value{rt.intern(name)}, rt.list_of(lambda)})};
    }

    // Parse an `if` statement: the if/elif arms and the optional else.
    [[nodiscard]] result<stmt> parse_if() {
      if_stmt f;
      toks.take(); // 'if'
      for (;;) {
        auto cond = expression_parser::parse(rt, toks, depth);
        if (!cond) return cond;

        auto body = parse_block();
        if (!body) return body;

        f.arms.push_back(if_arm{*cond, *std::move(body)});
        if (!toks.at_word("elif")) break;

        toks.take();
      }
      if (toks.at_word("else")) {
        toks.take();
        auto body = parse_block();
        if (!body) return body;

        f.else_body = *std::move(body);
        f.has_else = true;
      }
      return stmt{std::move(f)};
    }

    // Parse a `return` statement, kept structural for the enclosing body's
    // restricted-return rewrite; the bare spelling returns nil.
    [[nodiscard]] result<stmt> parse_return() {
      auto tok = toks.peek();
      toks.take(); // 'return'
      value expr;
      if (!toks.at(token_kind::newline)) {
        auto v = expression_parser::parse(rt, toks, depth);
        if (!v) return v;
        expr = *v;
      }
      if (auto e = take_line_end()) return std::move(*e);
      return stmt{ret_stmt{expr, tok}};
    }

    // Parse a block: a colon, then statements one indent level deeper.
    [[nodiscard]] result<std::vector<stmt>> parse_block() {
      if (!toks.at(token_kind::colon)) return toks.fail("expected ':'");
      toks.take();
      if (!toks.at(token_kind::newline))
        return toks.fail("expected an indented block");
      toks.take();
      if (!toks.at(token_kind::indent))
        return toks.fail("expected an indented block");
      toks.take();
      std::vector<stmt> body;
      while (!toks.at(token_kind::dedent)) {
        auto s = parse_statement();
        if (!s) return s;
        body.push_back(*std::move(s));
      }
      toks.take(); // dedent
      return body;
    }

    // Whether every arm of an else-less `if` ends in a `return`, making it
    // a guard clause.
    [[nodiscard]] static bool all_arms_return(const if_stmt& f) {
      for (const auto& arm : f.arms)
        if (arm.body.empty() ||
            !std::holds_alternative<ret_stmt>(arm.body.back().node))
          return false;
      return true;
    }

    // Check a `return`'s placement: inside `fun` only, and only where the
    // rewrite can express it.
    [[nodiscard]] std::optional<source_error>
    check_return(const ret_stmt& r, bool in_fun, bool expressible) const {
      if (!in_fun)
        return r.tok.error_at(toks.src(), "'return' outside a function");
      if (!expressible)
        return r.tok.error_at(toks.src(),
            "'return' must end its function or a guard clause");
      return std::nullopt;
    }

    // Desugar a parsed body to Hall forms, applying the restricted-return
    // rewrite.
    //
    // `in_fun` marks a body somewhere inside a function; `tail` marks one
    // whose final form is the enclosing function's result, which is where
    // `return` is expressible: as the final statement, or through the
    // guard-clause rewrite, where an else-less `if` whose every arm ends
    // in `return` takes the remainder of the body as its else branch.
    //
    // The walk runs back to front so each guard clause absorbs the
    // already-desugared remainder as its else branch iteratively; a flat
    // run of guard clauses costs no C++ stack, which the parse-nesting
    // depth guard could not otherwise bound.
    [[nodiscard]] result<std::vector<value>>
    desugar_body(std::span<const stmt> stmts, bool in_fun, bool tail) {
      // Reversed while building: `rest.back()` is the body's next form.
      std::vector<value> rest;
      for (size_t ndx = stmts.size(); ndx-- > 0;) {
        const auto& s = stmts[ndx];
        const auto last = rest.empty();
        if (const auto* v = std::get_if<value>(&s.node)) {
          rest.push_back(*v);
          continue;
        }
        if (const auto* r = std::get_if<ret_stmt>(&s.node)) {
          if (auto e = check_return(*r, in_fun, tail && last))
            return std::move(*e);
          rest.push_back(r->expr);
          continue;
        }
        const auto& f = std::get<if_stmt>(s.node);
        if (tail && !last && !f.has_else && all_arms_return(f)) {
          const std::vector<value> remainder{rest.rbegin(), rest.rend()};
          auto guard = desugar_if(f, in_fun, true, begin_of(remainder));
          if (!guard) return guard;
          rest.assign(1, *guard);
          continue;
        }
        auto form = desugar_if(f, in_fun, tail && last, std::nullopt);
        if (!form) return form;
        rest.push_back(*form);
      }
      return std::vector<value>{rest.rbegin(), rest.rend()};
    }

    // Desugar an `if` statement to the kernel `if`, elifs chaining
    // rightward; `forced_else` is the guard-clause rewrite's else branch.
    [[nodiscard]] result<value> desugar_if(const if_stmt& f, bool in_fun,
        bool tail, std::optional<value> forced_else) {
      std::optional<value> chain = forced_else;
      if (f.has_else) {
        auto body = desugar_body(f.else_body, in_fun, tail);
        if (!body) return body;

        chain = begin_of(*body);
      }
      for (const auto& arm : std::views::reverse(f.arms)) {
        auto body = desugar_body(arm.body, in_fun, tail);
        if (!body) return body;

        const value then = begin_of(*body);
        const value if_sym{rt.sym_if};
        chain = chain ? rt.list_of({if_sym, arm.cond, then, *chain})
                      : rt.list_of({if_sym, arm.cond, then});
      }
      return *chain;
    }
  };

#pragma endregion
};

#pragma endregion

}}}} // namespace corvid::lang::coreb::monty
