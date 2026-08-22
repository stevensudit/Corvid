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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../containers/core/scoped_value.h"
#include "../../strings/cases.h"
#include "runtime.h"
#include "token_classes.h"
#include "value.h"

namespace corvid { inline namespace lang { namespace coreb { namespace monty {

// Monty unparser, rendering Hall forms as canonical Monty source.
//
// Definitions become `x = e` and `fun f(a, b):` blocks, kernel `if` chains
// become if/elif/else ladders or the ternary by position, `(list ...)` becomes
// `[...]`, operator forms become infix with the minimal parentheses the sparse
// partial order demands, and two-space indentation throughout. Parsing the
// output desugars back to the same forms, canonical after one round trip.
//
// A form with no Monty spelling renders as the `%(...)` Hall escape, a Hall
// pass, which keeps unparsing total. This includes an anonymous `lambda`, or
// `define`/`quote` in expression position, a dotted pair, or any shape the
// desugars do not produce, such as a `define` of a non-symbol or a body-less
// `lambda`.
//
// Each emitter claims a form only when its whole shape checks out and
// otherwise falls through, so the escape preserves the program rather than
// validating it; whether the form means anything stays the kernel's business.
//
// The unparse, by example:
//
//   (define x 5)             ->  x = 5
//   (f x)                    ->  f(x)
//   (+ (- a b) c)            ->  a - b + c
//   (+ a (* b c))            ->  a + (b * c)
//   (< a b c)                ->  a < b < c
//   (if c x y)               ->  x if c else y
//   (list 1 (+ x 1))         ->  [1, x + 1]
//   (map - xs)               ->  map((-), xs)
//   (+ a b c)                ->  (+)(a, b, c)
//   (- 7)                    ->  (-)(7)
//   (define x (begin (f) 3))  ->  x = begin(f(), 3)
//   (define f (lambda (n)))  ->  f = %(lambda (n))
//
// and, with blocks:
//
//   (define inc (lambda (n) (+ n 1)))  ->  fun inc(n):
//                                            n + 1
//   (define - (lambda (a b) (- a b)))  ->  fun (-)(a, b):
//                                            a - b
//   (if c (begin (f)) (begin (g)))     ->  if c:
//                                            f()
//                                          else:
//                                            g()
//
// A statement-position `begin` splats into the enclosing sequence, which is
// identity-preserving under function-level scoping; an expression-position
// `begin` is the call-shaped sequencer, `begin(a, b)`. `return` never appears
// in output: a final `return e` desugared to `e` and guards to if/else, so
// unparsing spells them as the plain expression and else block they became.
//
// Symbols emit their interned spelling: a word as itself and an operator as
// its mention, `=` and `:=` included, both re-lexing under the shared token
// classes (see "token_classes.h"). The one exception is a `%` kernel symbol,
// which renders as-is and has no Monty spelling, kernel-generated forms
// being milestone 5's concern.

#pragma region unparser

// Unparser from Hall values to canonical Monty source.
//
// Nesting deeper than `max_depth` renders as an escape rather than risking
// stack exhaustion, subject to the printer's own depth guard.
class unparser final {
public:
  // Unparse one Hall form as a Monty statement, possibly a multi-line
  // block, without a trailing newline.
  [[nodiscard]] static std::string
  unparse(const runtime& rt, const value& form) {
    builder b(rt);
    b.emit_statement(form, 0);
    b.trim();
    return std::move(b.out);
  }

  // Unparse a program: each Hall form a top-level statement, newline
  // separated.
  [[nodiscard]] static std::string
  unparse_all(const runtime& rt, std::span<const value> forms) {
    builder b(rt);
    for (const auto& form : forms) b.emit_statement(form, 0);
    b.trim();
    return std::move(b.out);
  }

private:
#pragma region builder

  // The precedence band an emitted expression naturally occupies; a
  // position demanding a tighter band parenthesizes a looser expression.
  // `tight` covers primaries and postfix chains, which never need parens.
  enum class band : std::uint8_t { expr, chain, add, mul, unary, tight };

  // One emitted expression: its text and the band it occupies.
  struct emitted {
    std::string text;
    band b;
  };

  // Single-pass builder from Hall forms to Monty text.
  struct builder {
    explicit builder(const runtime& rt) noexcept : rt{rt} {}

    const runtime& rt;
    std::string out;
    size_t depth{};

    // Drop the final statement's newline.
    void trim() {
      if (out.ends_with('\n')) out.pop_back();
    }

    // Whether a statement line would begin with a contextual keyword,
    // needing grouping parens to strip the keyword claim.
    [[nodiscard]] static bool leads_with_keyword(std::string_view text) {
      for (const auto kw : contextual_keywords) {
        if (!text.starts_with(kw)) continue;
        if (text.size() == kw.size()) return true;
        const char c = text[kw.size()];
        if (!is_word_char(c) && c != '?') return true;
      }
      return false;
    }

#pragma endregion
#pragma region expressions

    // Render `form` as the `%(...)` Hall escape. Only cells reach this:
    // every atom has a direct spelling.
    [[nodiscard]] static emitted escape(const value& form) {
      return {"%" + form.print(), band::tight};
    }

    [[nodiscard]] static std::string parenthesize(emitted e, bool needed) {
      if (!needed) return std::move(e.text);
      return "(" + std::move(e.text) + ")";
    }

    // Emission for each operand position, parenthesizing when the operand's
    // band is looser than the position admits.
    [[nodiscard]] std::string as_expr(const value& v) { return emit(v).text; }
    [[nodiscard]] std::string as_chain_operand(const value& v) {
      auto e = emit(v);
      const auto parens = e.b == band::expr;
      return parenthesize(std::move(e), parens);
    }
    [[nodiscard]] std::string as_cmp_operand(const value& v) {
      auto e = emit(v);
      const auto parens = e.b == band::expr || e.b == band::chain;
      return parenthesize(std::move(e), parens);
    }
    [[nodiscard]] std::string
    as_arith_operand(const value& v, band family, bool leftmost) {
      auto e = emit(v);
      const auto arith = e.b == band::add || e.b == band::mul;
      const auto parens =
          e.b == band::expr || e.b == band::chain ||
          (arith && (e.b != family || !leftmost));
      return parenthesize(std::move(e), parens);
    }
    [[nodiscard]] std::string as_unary_operand(const value& v) {
      auto e = emit(v);
      const auto parens = e.b != band::unary && e.b != band::tight;
      return parenthesize(std::move(e), parens);
    }
    [[nodiscard]] std::string as_callee(const value& v) {
      auto e = emit(v);
      const auto parens = e.b != band::tight;
      return parenthesize(std::move(e), parens);
    }

    // Emit a comma-separated argument or element sequence.
    [[nodiscard]] std::string join_exprs(std::span<const value> elems) {
      std::string text;
      for (const auto& elem : elems) {
        if (!text.empty()) text += ", ";
        text += as_expr(elem);
      }
      return text;
    }

    // Emit one expression in its natural spelling, unparenthesized.
    [[nodiscard]] emitted emit(const value& v) {
      if (const auto sym = v.maybe_symbol()) {
        const auto& name = (*sym).name();
        // Every operator spells as its mention, `=` and `:=` included; a
        // `%` kernel symbol renders as-is, having no Monty spelling.
        if (is_operator_symbol(name)) return {"(" + name + ")", band::tight};
        return {name, band::tight};
      }
      if (!v.is_cell()) {
        auto text = v.print();
        // A negative number's sign occupies the unary band, so positions
        // demanding a tight operand parenthesize it.
        const auto b = text.starts_with('-') ? band::unary : band::tight;
        return {std::move(text), b};
      }
      if (depth >= max_depth) return escape(v);
      scoped_value guard(depth, depth + 1);
      std::vector<value> elems;
      if (!v.append_elements(elems)) return escape(v);
      if (const auto head = elems[0].maybe_symbol()) {
        if (*head == rt.sym_if && elems.size() == 4)
          return {
              as_chain_operand(elems[2]) + " if " +
                  as_chain_operand(elems[1]) + " else " + as_expr(elems[3]),
              band::expr};
        if (*head == rt.sym_list)
          return {"[" + join_exprs(std::span{elems}.subspan(1)) + "]",
              band::tight};
        // A `begin` head deliberately falls through to the call emission:
        // the call spelling is the ruled sequencer.
        if (*head == rt.sym_quote || *head == rt.sym_lambda ||
            *head == rt.sym_define || *head == rt.sym_if)
          return escape(v);
        if (auto e = emit_operator((*head).name(), elems))
          return *std::move(e);
      }
      return {as_callee(elems[0]) + "(" +
                  join_exprs(std::span{elems}.subspan(1)) + ")",
          band::tight};
    }

    // Emit an operator form whose arity has an infix or unary spelling per
    // the shared operator table; other shapes fall through to the
    // mention-call, `(+)(a, b, c)`. That includes negation of a number,
    // `(-)(7)`, whose unary spelling `-7` would re-read as a signed
    // literal, and the statement spellings, which have no infix at all.
    [[nodiscard]] std::optional<emitted>
    emit_operator(std::string_view name, std::span<const value> elems) {
      const auto* op = find_operator(name);
      if (!op) return std::nullopt;
      if (name == "-" && elems.size() == 2 && !elems[1].is_int() &&
          !elems[1].is_float())
        return emitted{"-" + as_unary_operand(elems[1]), band::unary};
      if (is_arithmetic(op->kind) && elems.size() == 3) {
        const auto family =
            op->kind == operator_kind::additive ? band::add : band::mul;
        auto text = as_arith_operand(elems[1], family, true);
        text += " ";
        text += name;
        text += " ";
        text += as_arith_operand(elems[2], family, false);
        return emitted{std::move(text), family};
      }
      if (op->kind == operator_kind::comparison &&
          (op->chains ? elems.size() >= 3 : elems.size() == 3))
      {
        std::string text;
        for (const auto& elem : elems.subspan(1)) {
          if (!text.empty()) {
            text += " ";
            text += name;
            text += " ";
          }
          text += as_cmp_operand(elem);
        }
        return emitted{std::move(text), band::chain};
      }
      return std::nullopt;
    }

#pragma endregion
#pragma region statements

    // Append one line at `indent` levels of two-space indentation.
    void emit_line(size_t indent, std::string_view text) {
      out.append(indent * 2, ' ');
      out += text;
      out += '\n';
    }

    // Emit one form as a statement: a definition, a `fun` block, an
    // if/elif/else ladder, a splatted `begin` sequence, or an expression
    // statement, whose line takes grouping parens when a contextual keyword
    // would otherwise claim it.
    void emit_statement(const value& v, size_t indent) {
      if (v.is_cell() && depth < max_depth && emit_compound(v, indent)) return;

      auto text = as_expr(v);
      if (leads_with_keyword(text)) text = "(" + std::move(text) + ")";
      emit_line(indent, text);
    }

    // Emit a statement-shaped compound form: a `define`, a splatted
    // `begin`, or an if/elif/else ladder; false leaves the form to the
    // expression statement.
    [[nodiscard]] bool emit_compound(const value& v, size_t indent) {
      scoped_value guard(depth, depth + 1);
      std::vector<value> elems;
      if (!v.append_elements(elems)) return false;
      const auto head = elems[0].maybe_symbol();
      if (!head) return false;
      if (*head == rt.sym_define && elems.size() == 3) {
        if (emit_fun(elems[1], elems[2], indent)) return true;
        const auto name = elems[1].maybe_symbol();
        if (!name || !is_bindable_word((*name).name())) return false;
        emit_line(indent, (*name).name() + " = " + as_expr(elems[2]));
        return true;
      }
      if (*head == rt.sym_begin && elems.size() >= 2) {
        for (const auto& elem : std::span{elems}.subspan(1))
          emit_statement(elem, indent);
        return true;
      }
      return *head == rt.sym_if && emit_if(elems, indent);
    }

    // Emit `(define name (lambda params body...))` as a `fun` block when
    // the whole shape checks out: a name that is a bindable word or an
    // operator (spelled as its mention), params nil or all bindable words,
    // and a nonempty body.
    [[nodiscard]] bool
    emit_fun(const value& name_v, const value& lambda_v, size_t indent) {
      const auto name = name_v.maybe_symbol();
      if (!name) return false;
      const auto& fname = (*name).name();
      const auto word = is_bindable_word(fname);
      if (!word && !is_operator_symbol(fname)) return false;
      std::vector<value> lam;
      if (!lambda_v.is_cell() || !lambda_v.append_elements(lam)) return false;
      if (lam.size() < 3) return false;
      if (const auto head = lam[0].maybe_symbol();
          !head || *head != rt.sym_lambda)
        return false;
      std::vector<value> params;
      if (!lam[1].is_nil() &&
          (!lam[1].is_cell() || !lam[1].append_elements(params)))
        return false;
      std::string line = word ? "fun " + fname + "(" : "fun (" + fname + ")(";
      for (const auto& param : params) {
        const auto sym = param.maybe_symbol();
        if (!sym || !is_bindable_word((*sym).name())) return false;
        if (line.back() != '(') line += ", ";
        line += (*sym).name();
      }
      line += "):";
      emit_line(indent, line);
      for (const auto& form : std::span{lam}.subspan(2))
        emit_statement(form, indent + 1);
      return true;
    }

    // Emit a kernel `if` chain as an if/elif/else ladder when every arm is
    // a block: the then operand a nonempty `begin`, and the else operand a
    // nonempty `begin`, another such `if`, or absent. A non-block `if` is
    // an expression statement instead, spelled as the ternary.
    [[nodiscard]] bool emit_if(std::span<const value> elems, size_t indent) {
      // Validate the whole ladder before emitting any of it.
      struct arm {
        value cond;
        std::vector<value> body;
      };
      std::vector<arm> arms;
      std::vector<value> else_body;
      std::vector<value> cur{elems.begin(), elems.end()};
      for (;;) {
        if (cur.size() != 3 && cur.size() != 4) return false;
        auto body = block_of(cur[2]);
        if (!body) return false;
        arms.push_back(arm{cur[1], *std::move(body)});
        if (cur.size() == 3) break;
        const auto else_arm = cur[3];
        if (auto block = block_of(else_arm)) {
          else_body = *std::move(block);
          break;
        }
        std::vector<value> nested;
        if (!else_arm.is_cell() || !else_arm.append_elements(nested))
          return false;
        const auto head = nested[0].maybe_symbol();
        if (!head || *head != rt.sym_if) return false;
        cur = std::move(nested);
      }
      for (size_t ndx = 0; ndx < arms.size(); ++ndx) {
        const auto lead = (ndx == 0) ? "if " : "elif ";
        emit_line(indent, lead + as_expr(arms[ndx].cond) + ":");
        for (const auto& form : arms[ndx].body)
          emit_statement(form, indent + 1);
      }
      if (!else_body.empty()) {
        emit_line(indent, "else:");
        for (const auto& form : else_body) emit_statement(form, indent + 1);
      }
      return true;
    }

    // An arm's statements when `v` is a nonempty `(begin ...)` block.
    [[nodiscard]] std::optional<std::vector<value>> block_of(
        const value& v) const {
      std::vector<value> elems;
      if (!v.is_cell() || !v.append_elements(elems)) return std::nullopt;
      if (const auto head = elems[0].maybe_symbol();
          !head || *head != rt.sym_begin || elems.size() < 2)
        return std::nullopt;
      elems.erase(elems.begin());
      return elems;
    }
  };

#pragma endregion
};

#pragma endregion

}}}} // namespace corvid::lang::coreb::monty
