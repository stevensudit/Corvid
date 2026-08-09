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

#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include "corvid/enums.h"
#include "corvid/lang/coreb/coreb.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::coreb;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// Lex `src` and render the token stream compactly: kind names in order,
// with payload-bearing kinds as "kind:text".
std::string lex_dump(std::string_view src) {
  CAPTURE(src);
  auto r = monty::lexer::lex(src);
  REQUIRE(r.has_value());
  std::string out;
  for (const auto& t : r->tokens()) {
    if (!out.empty()) out += ' ';
    out += std::format("{}", t.kind);
    switch (t.kind) {
    case monty::token_kind::word:
    case monty::token_kind::op:
    case monty::token_kind::number:
    case monty::token_kind::string:
    case monty::token_kind::hall:
      out += ':';
      out += t.text;
      break;
    default: break;
    }
  }
  return out;
}

// Lex `src` expecting failure, returning the error.
source_error lex_err(std::string_view src) {
  CAPTURE(src);
  auto r = monty::lexer::lex(src);
  REQUIRE_FALSE(r.has_value());
  return r.as_error();
}

// Parse a Monty expression and return the desugared s-expression's printed
// form, requiring the expression to span the whole source up to a trailing
// newline.
std::string parse_dump(runtime_environment& run_env, std::string_view src) {
  CAPTURE(src);
  auto lexed = monty::lexer::lex(src);
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto v = monty::expression_parser::parse(run_env, toks);
  REQUIRE(v.has_value());
  if (toks.at(monty::token_kind::newline)) toks.take();
  REQUIRE(toks.at(monty::token_kind::eof));
  return v->print();
}

// Parse a Monty expression expecting failure, returning the error.
source_error parse_err(runtime_environment& run_env, std::string_view src) {
  CAPTURE(src);
  auto lexed = monty::lexer::lex(src);
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto r = monty::expression_parser::parse(run_env, toks);
  REQUIRE_FALSE(r.has_value());
  return r.as_error();
}

// Parse a Monty program and render the desugared forms' printed forms,
// space-separated.
std::string stmt_dump(runtime_environment& run_env, std::string_view src) {
  CAPTURE(src);
  auto lexed = monty::lexer::lex(src);
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto forms = monty::statement_parser::parse_all(run_env, toks);
  REQUIRE(forms.has_value());
  std::string out;
  for (const auto& v : *forms) {
    if (!out.empty()) out += ' ';
    out += v.print();
  }
  return out;
}

// Parse a Monty program expecting failure, returning the error.
source_error stmt_err(runtime_environment& run_env, std::string_view src) {
  CAPTURE(src);
  auto lexed = monty::lexer::lex(src);
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto r = monty::statement_parser::parse_all(run_env, toks);
  REQUIRE_FALSE(r.has_value());
  return r.as_error();
}

} // namespace

#pragma region Monty lexer tokens

TEST_CASE("Monty lexer tokens", "[coreb]") {
  CHECK(lex_dump("") == "eof");
  CHECK(lex_dump("x") == "word:x newline eof");
  CHECK(lex_dump("x = 5") == "word:x op:= number:5 newline eof");
  CHECK(lex_dump("f(x, y)") ==
        "word:f lparen word:x comma word:y rparen newline eof");
  CHECK(lex_dump("xs = [1, 2]") ==
        "word:xs op:= lbracket number:1 comma number:2 rbracket newline eof");

  // Words lead with a letter or underscore; '?' is legal only finally.
  CHECK(lex_dump("_x nil? x2") == "word:_x word:nil? word:x2 newline eof");
  CHECK(lex_dump("nil?(x)") == "word:nil? lparen word:x rparen newline eof");

  // Operators come from the closed table, longest match first.
  CHECK(lex_dump("a<=b!=c>=d<e>f") ==
        "word:a op:<= word:b op:!= word:c op:>= word:d op:< word:e op:> "
        "word:f newline eof");

  // `==`, `:=`, and `=` are three distinct operators, and a colon is
  // punctuation only when no '=' follows.
  CHECK(lex_dump("a == b") == "word:a op:== word:b newline eof");
  CHECK(lex_dump("x := 5") == "word:x op::= number:5 newline eof");
  CHECK(lex_dump("a==b:=c=d") ==
        "word:a op:== word:b op::= word:c op:= word:d newline eof");

  // Unary minus is an operator, never part of the number literal.
  CHECK(lex_dump("-7") == "op:- number:7 newline eof");
  CHECK(lex_dump("1 2.5 .5 1e3 2E+4") ==
        "number:1 number:2.5 number:.5 number:1e3 number:2E+4 newline eof");

  // Strings keep their quotes and escapes raw for the parser to unescape,
  // and may contain a raw tab: the tab ban applies outside string literals.
  CHECK(
      lex_dump(R"(s = "a\nb")") == R"(word:s op:= string:"a\nb" newline eof)");
  CHECK(lex_dump("\"a\tb\"") == "string:\"a\tb\" newline eof");

  // `%(` opens a Hall escape, scanned raw to the matching paren; the
  // token's text is the Hall form, without the `%`. Parens inside Hall
  // strings and `;` comments do not count, and newlines inside are plain
  // text, not line structure.
  CHECK(lex_dump("x = %(+ 1 2)") == "word:x op:= hall:(+ 1 2) newline eof");
  CHECK(lex_dump("%(f (g 1))") == "hall:(f (g 1)) newline eof");
  CHECK(lex_dump(R"x(%(f ")"))x") == R"x(hall:(f ")") newline eof)x");
  CHECK(lex_dump("%(f ; )\n 1)") == "hall:(f ; )\n 1) newline eof");
  CHECK(lex_dump("%(f\n  1)") == "hall:(f\n  1) newline eof");
}

#pragma endregion
#pragma region Monty lexer blocks

TEST_CASE("Monty lexer blocks", "[coreb]") {
  CHECK(lex_dump("if x:\n  f(x)\n  g\nh\n") ==
        "word:if word:x colon newline indent word:f lparen word:x rparen "
        "newline word:g newline dedent word:h newline eof");

  // Closing several levels at once emits one dedent per level.
  CHECK(lex_dump("a:\n  b:\n    c\nd") ==
        "word:a colon newline indent word:b colon newline indent word:c "
        "newline dedent dedent word:d newline eof");

  // EOF closes the last line and every open block.
  CHECK(lex_dump("a:\n  b") ==
        "word:a colon newline indent word:b newline dedent eof");

  // Blank and comment-only lines affect nothing, whatever their indent.
  CHECK(lex_dump("a\n\n  # note\nb") == "word:a newline word:b newline eof");
  CHECK(lex_dump("a  # trailing\nb") == "word:a newline word:b newline eof");

  // A leading indent lexes fine; rejecting it is the parser's job.
  CHECK(lex_dump("  x") == "indent word:x newline dedent eof");

  // CRLF is a newline.
  CHECK(lex_dump("a\r\nb") == "word:a newline word:b newline eof");
}

#pragma endregion
#pragma region Monty lexer continuation

TEST_CASE("Monty lexer continuation", "[coreb]") {
  // Inside unclosed brackets, newlines and indentation are plain
  // whitespace: this is the only line-continuation mechanism.
  CHECK(lex_dump("f(a,\n  b)") ==
        "word:f lparen word:a comma word:b rparen newline eof");
  CHECK(lex_dump("xs = [\n  1,\n  2,\n]") ==
        "word:xs op:= lbracket number:1 comma number:2 comma rbracket "
        "newline eof");
  CHECK(lex_dump("f(a, # arg\n  b)") ==
        "word:f lparen word:a comma word:b rparen newline eof");
}

#pragma endregion
#pragma region Monty lexer errors

TEST_CASE("Monty lexer errors", "[coreb]") {
  CHECK(lex_err("\tx").message == "tab character");
  CHECK(lex_err("a\n\tb").message == "tab character");
  CHECK(
      lex_err(" x").message == "indentation is not a multiple of two spaces");
  CHECK(
      lex_err("a:\n    b").message == "indentation jumps more than one level");
  CHECK(lex_err("a?b").message == "'?' can only end a name");
  CHECK(lex_err("a??").message == "'?' can only end a name");
  CHECK(lex_err("1abc").message == "malformed number");
  CHECK(lex_err("1.2.3").message == "malformed number");
  CHECK(lex_err("1e").message == "malformed number");
  CHECK(lex_err("!").message == "'!' appears only in '!='");
  CHECK(lex_err("$").message == "unexpected character");
  CHECK(lex_err("a\rb").message == "stray carriage return");
  CHECK(lex_err(")").message == "unmatched ')'");
  CHECK(lex_err("[)]").message == "unmatched ')'");

  // String errors never report `incomplete_input`: strings are single-line, so
  // more input cannot repair them.
  auto e = lex_err(R"("abc)");
  CHECK(e.message == "unterminated string");
  CHECK_FALSE(e.incomplete());
  CHECK(lex_err("\"a\nb\"").message == "unterminated string");
  CHECK(lex_err(R"("a\qb")").message == "invalid escape");

  // An unterminated bracket needs more input, and is reported at its opener.
  e = lex_err("f(a");
  CHECK(e.message == "unterminated bracket");
  CHECK(e.incomplete());
  CHECK(e.pos == 1);
  CHECK(e.line == 1);
  CHECK(e.col == 2);

  // An unterminated Hall escape needs more input, reported at its opener;
  // an unterminated Hall string swallows everything, leaving the escape
  // unclosed.
  e = lex_err("x = %(f (g 1)");
  CHECK(e.message == "unterminated Hall escape");
  CHECK(e.incomplete());
  CHECK(e.pos == 4);
  CHECK(lex_err(R"x(%(f "))x").message == "unterminated Hall escape");

  // `%` opens nothing but an escape.
  CHECK(lex_err("%x").message == "unexpected character");

  // Errors carry a 1-based line and column.
  e = lex_err("ok\n   x");
  CHECK(e.line == 2);
  CHECK(e.col == 4);
}

#pragma endregion
#pragma region Monty expression parser

TEST_CASE("Monty expression parser", "[coreb]") {
  runtime_environment run_env;

  // Primaries and literals.
  CHECK(parse_dump(run_env, "x") == "x");
  CHECK(parse_dump(run_env, "nil?") == "nil?");
  CHECK(parse_dump(run_env, "42") == "42");
  CHECK(parse_dump(run_env, "2.5") == "2.5");
  CHECK(parse_dump(run_env, "nil") == "nil");
  CHECK(parse_dump(run_env, "true") == "true");
  CHECK(parse_dump(run_env, "false") == "false");
  CHECK(parse_dump(run_env, R"("a\nb")") == R"("a\nb")");

  // Integer overflow falls back to double, matching the kernel rule.
  CHECK(parse_dump(run_env, "99999999999999999999") == "1e+20");

  // Calls bind tightest and fold left.
  CHECK(parse_dump(run_env, "f(x, y)") == "(f x y)");
  CHECK(parse_dump(run_env, "f()") == "(f)");
  CHECK(parse_dump(run_env, "f(a)(b)") == "((f a) b)");
  CHECK(parse_dump(run_env, "f(g(x))") == "(f (g x))");

  // Arithmetic families fold left; parentheses group.
  CHECK(parse_dump(run_env, "a + b - c") == "(- (+ a b) c)");
  CHECK(parse_dump(run_env, "a * b / c") == "(/ (* a b) c)");
  CHECK(parse_dump(run_env, "a + (b * c)") == "(+ a (* b c))");
  CHECK(parse_dump(run_env, "(a + b) * c") == "(* (+ a b) c)");

  // A '-' touching a number token signs the literal, int64 min included;
  // touching anything else it negates, binding to the postfix chain it
  // precedes. Negation of a number is called by mention.
  CHECK(parse_dump(run_env, "-7") == "-7");
  CHECK(parse_dump(run_env, "-7.5") == "-7.5");
  CHECK(parse_dump(run_env, "-9223372036854775808") == "-9223372036854775808");
  CHECK(parse_dump(run_env, "--7") == "(- -7)");
  CHECK(parse_dump(run_env, "8 - -7") == "(- 8 -7)");
  CHECK(parse_dump(run_env, "(-)(7)") == "(- 7)");
  CHECK(parse_dump(run_env, "-%(head xs)") == "(- (head xs))");
  CHECK(parse_dump(run_env, "a - -b") == "(- a (- b))");
  CHECK(parse_dump(run_env, "-f(x) * y") == "(* (- (f x)) y)");

  // Comparison chains are same-operator and n-ary; arithmetic sits above.
  CHECK(parse_dump(run_env, "a == b") == "(== a b)");
  CHECK(parse_dump(run_env, "a != b") == "(!= a b)");
  CHECK(parse_dump(run_env, "a < b < c") == "(< a b c)");
  CHECK(parse_dump(run_env, "a + b < c * d") == "(< (+ a b) (* c d))");

  // The ternary desugars to the kernel `if` and chains rightward.
  CHECK(parse_dump(run_env, "x if c else y") == "(if c x y)");
  CHECK(parse_dump(run_env, "a if c else b if d else e") ==
        "(if c a (if d b e))");

  // A parenthesized operator mentions it as a value, `=`/`:=` included;
  // only their infix spellings are statement-bound.
  CHECK(parse_dump(run_env, "map((-), xs)") == "(map - xs)");
  CHECK(parse_dump(run_env, "(+)") == "+");
  CHECK(parse_dump(run_env, "(=)") == "=");
  CHECK(parse_dump(run_env, "(:=)") == ":=");

  // List literals desugar to the kernel `list` constructor, so elements are
  // evaluated expressions.
  CHECK(parse_dump(run_env, "[1, 2, 3]") == "(list 1 2 3)");
  CHECK(parse_dump(run_env, "[]") == "(list)");
  CHECK(parse_dump(run_env, "[1 + 2, [x]]") == "(list (+ 1 2) (list x))");

  // The `%(...)` Hall escape splices the read form in place: code, not
  // quotation, composing with postfix and operands like any primary.
  CHECK(parse_dump(run_env, "%(+ 1 2)") == "(+ 1 2)");
  CHECK(parse_dump(run_env, "1 + %(head xs)") == "(+ 1 (head xs))");
  CHECK(parse_dump(run_env, "%(lambda (n) (* n 2))(21)") ==
        "((lambda (n) (* n 2)) 21)");

  // Call-spelled `begin` is the sequencer expression: the call desugar
  // manufactures the kernel form, which special-form identity then claims.
  CHECK(parse_dump(run_env, "begin(f(), 3)") == "(begin (f) 3)");

  // Bracket continuation carries an expression across lines.
  CHECK(parse_dump(run_env, "f(a,\n  b)") == "(f a b)");
  CHECK(parse_dump(run_env, "[1,\n  2]") == "(list 1 2)");
}

#pragma endregion
#pragma region Monty expression parser errors

TEST_CASE("Monty expression parser errors", "[coreb]") {
  runtime_environment run_env;

  // The sparse partial order: family mixing and mixed chains are errors.
  CHECK(parse_err(run_env, "a + b * c").message ==
        "mixing '+'/'-' with '*'/'/' requires parentheses");
  CHECK(parse_err(run_env, "a * b + c").message ==
        "mixing '+'/'-' with '*'/'/' requires parentheses");
  CHECK(parse_err(run_env, "a < b <= c").message ==
        "comparison chains cannot mix operators");
  CHECK(parse_err(run_env, "a != b != c").message == "'!=' does not chain");

  // Unary minus must touch its operand; an accidental space would
  // otherwise quietly turn a literal into a negation call.
  CHECK(
      parse_err(run_env, "- 7").message == "unary '-' must touch its operand");
  CHECK(parse_err(run_env, "a * - b").message ==
        "unary '-' must touch its operand");

  // `=` and `:=` are statements, rejected with dedicated messages.
  CHECK(parse_err(run_env, "x = 5").message ==
        "'=' is a definition statement, not an expression");
  CHECK(parse_err(run_env, "x := 5").message ==
        "':=' is reserved for assignment, a statement");
  CHECK(parse_err(run_env, "f(x = 5)").message ==
        "'=' is a definition statement, not an expression");

  // Indexing is reserved.
  CHECK(parse_err(run_env, "xs[0]").message ==
        "indexing is not yet part of Monty");

  // List-literal structural errors; trailing commas stay rejected, as in
  // calls.
  CHECK(parse_err(run_env, "[1 2]").message == "expected ']'");
  CHECK(parse_err(run_env, "[1, 2,]").message == "expected an expression");

  // Structural errors.
  CHECK(parse_err(run_env, "x if c").message ==
        "expected 'else' after ternary condition");
  CHECK(parse_err(run_env, "()").message == "expected an expression");
  CHECK(parse_err(run_env, "a +\nb").message == "expected an expression");

  // Parsing consumes exactly one expression; the leftover tokens stay in
  // the stream for the caller to judge.
  auto lexed = monty::lexer::lex("a b");
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto v = monty::expression_parser::parse(run_env, toks);
  REQUIRE(v.has_value());
  CHECK(v->print() == "a");
  CHECK(toks.at_word("b"));

  // Probe: the stream is move-only, so `std::move` on a const stream (as
  // `std::move(*lexed)` would be) is rejected rather than copying silently;
  // moving the result works.
  static_assert(!std::is_copy_constructible_v<monty::token_stream>);
  static_assert(!std::is_constructible_v<monty::token_stream,
      const monty::token_stream&&>);
  static_assert(
      std::is_constructible_v<monty::token_stream, monty::token_stream&&>);

  // Nesting is depth-guarded rather than risking the C++ stack.
  const std::string deep =
      std::string(max_depth + 1, '(') + "x" + std::string(max_depth + 1, ')');
  CHECK(parse_err(run_env, deep).message == "nesting too deep");
  CHECK(parse_err(run_env, std::string(max_depth + 1, '-') + "x").message ==
        "nesting too deep");

  // Nesting budgets compound across layers: neither 100 parens nor a
  // 100-deep Hall escape alone approaches the cap, but stacked they exceed
  // it.
  CHECK(parse_dump(run_env,
            std::string(100, '(') + "x" + std::string(100, ')')) == "x");
  const std::string stacked =
      std::string(100, '(') + "%(" + std::string(99, '(') + "x" +
      std::string(100, ')') + std::string(100, ')');
  CHECK(parse_err(run_env, stacked).message == "nesting too deep");

  // Errors carry the offending position.
  auto e = parse_err(run_env, "(1 +\n*)");
  CHECK(e.message == "expected an expression");
  CHECK(e.line == 2);
  CHECK(e.col == 1);

  // A Hall reader error inside an escape maps back to the enclosing
  // source, offset by the token's position: here, the misplaced dot.
  e = parse_err(run_env, "x + %(.)");
  CHECK(e.message == "misplaced '.'");
  CHECK(e.pos == 6);
  CHECK(e.col == 7);
}

#pragma endregion
#pragma region Monty expression parser evaluates

TEST_CASE("Monty expression parser evaluates", "[coreb]") {
  runtime_environment run_env;
  evaluator ev(run_env);

  // End-to-end: Monty source through the desugar into the evaluator.
  auto parse_eval = [&](std::string_view src) {
    CAPTURE(src);
    auto lexed = monty::lexer::lex(src);
    REQUIRE(lexed.has_value());
    auto toks = *std::move(lexed);
    auto v = monty::expression_parser::parse(run_env, toks);
    REQUIRE(v.has_value());
    // Evaluation may collect at safe points; the pending form is a root.
    std::vector<value> forms{*v};
    gc_pin pin(run_env.rt, forms);
    auto r = ev.eval(forms[0]);
    REQUIRE(r.has_value());
    return r->print();
  };

  CHECK(parse_eval("(1 + 2) * 3") == "9");
  CHECK(parse_eval("1 + 2 == 3") == "true");
  CHECK(parse_eval("1 < 2 < 3") == "true");
  CHECK(parse_eval("-2 * -3") == "6");
  CHECK(parse_eval("(-)(7)") == "-7");
  CHECK(parse_eval("10 / 4") == "2.5");
  CHECK(parse_eval("1 if nil else 2") == "2");
  CHECK(parse_eval("[1, 2 + 3]") == "(1 5)");
  CHECK(parse_eval("head([1, 2])") == "1");
  CHECK(parse_eval("begin(1 + 1, 2 + 2)") == "4");
}

#pragma endregion
#pragma region Monty statement parser desugar

TEST_CASE("Monty statement parser desugar", "[coreb]") {
  runtime_environment run_env;

  // Simple statements: definitions and expression statements.
  CHECK(stmt_dump(run_env, "x = 5") == "(define x 5)");
  CHECK(stmt_dump(run_env, "f(1)") == "(f 1)");
  CHECK(stmt_dump(run_env, "x = 5\ny = x + 1") ==
        "(define x 5) (define y (+ x 1))");

  // Keywords are contextual, not reserved: a leading word followed by `=`
  // is a definition even when the word is a statement keyword. `if`
  // parses the same way; rebinding it is policed at kernel `define`.
  CHECK(stmt_dump(run_env, "fun = 5") == "(define fun 5)");
  CHECK(stmt_dump(run_env, "return = 5") == "(define return 5)");
  CHECK(stmt_dump(run_env, "elif = 5") == "(define elif 5)");
  CHECK(stmt_dump(run_env, "if = 5") == "(define if 5)");
  // Grouping parens strip the keyword claim, keeping Monty total: an
  // expression statement can lead with a keyword-named variable this way.
  CHECK(stmt_dump(run_env, "(fun + 1)") == "(+ fun 1)");

  // `fun` is a define of a lambda; the block splats into the implicit
  // sequence.
  CHECK(stmt_dump(run_env, "fun inc(n):\n  n + 1") ==
        "(define inc (lambda (n) (+ n 1)))");
  // A zero-parameter lambda's parameter list is nil, which prints as
  // "nil": nil unifies with the empty list.
  CHECK(stmt_dump(run_env, "fun f():\n  1\n  2") ==
        "(define f (lambda nil 1 2))");
  CHECK(stmt_dump(run_env, "fun add(a, b):\n  a + b") ==
        "(define add (lambda (a b) (+ a b)))");
  // A `fun` name may be an operator mention, rebinding the operator;
  // `=`/`:=` mention like any other.
  CHECK(stmt_dump(run_env, "fun (-)(a, b):\n  a + b") ==
        "(define - (lambda (a b) (+ a b)))");
  CHECK(stmt_dump(run_env, "fun (=)(a, b):\n  a") ==
        "(define = (lambda (a b) a))");

  // A flat run of guard clauses folds iteratively rather than one C++
  // recursion per clause.
  {
    std::string src = "fun f(n):\n";
    for (size_t ndx = 0; ndx < 2000; ++ndx)
      src += "  if n == 0:\n    return 1\n";
    src += "  2\n";
    auto guards = monty::lexer::lex(src);
    REQUIRE(guards.has_value());
    auto guard_toks = *std::move(guards);
    CHECK(monty::statement_parser::parse_all(run_env, guard_toks).has_value());
  }

  // `if`/`elif`/`else` chains rightward over begin blocks; else-less is
  // the kernel's two-argument `if`.
  CHECK(stmt_dump(run_env, "if a:\n  f()") == "(if a (begin (f)))");
  CHECK(stmt_dump(run_env, "if a:\n  f()\nelse:\n  g()") ==
        "(if a (begin (f)) (begin (g)))");
  CHECK(stmt_dump(run_env, "if a:\n  1\nelif b:\n  2\nelse:\n  3") ==
        "(if a (begin 1) (if b (begin 2) (begin 3)))");

  // A final `return e` is just `e`; the bare spelling returns nil.
  CHECK(stmt_dump(run_env, "fun f(n):\n  return n") ==
        "(define f (lambda (n) n))");
  CHECK(stmt_dump(run_env, "fun f():\n  return") ==
        "(define f (lambda nil nil))");

  // The guard-clause rewrite: an else-less `if` ending in `return` takes
  // the remainder of the body as its else branch.
  CHECK(stmt_dump(run_env, "fun f(n):\n  if n == 0:\n    return 1\n  n * 2") ==
        "(define f (lambda (n) (if (== n 0) (begin 1) (begin (* n 2)))))");

  // A final `if` with `else` may return from both arms.
  CHECK(stmt_dump(run_env,
            "fun f(n):\n  if n:\n    return 1\n  else:\n    return 2") ==
        "(define f (lambda (n) (if n (begin 1) (begin 2))))");

  // The single-statement entry consumes exactly one statement, leaving
  // the rest in the stream.
  auto lexed = monty::lexer::lex("x = 1\ny = 2");
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto v = monty::statement_parser::parse(run_env, toks);
  REQUIRE(v.has_value());
  CHECK(v->print() == "(define x 1)");
  CHECK(toks.at_word("y"));
}

#pragma endregion
#pragma region Monty statement parser errors

TEST_CASE("Monty statement parser errors", "[coreb]") {
  runtime_environment run_env;

  // Restricted return: inside `fun` only, and only where the rewrite can
  // express it.
  CHECK(
      stmt_err(run_env, "return 1").message == "'return' outside a function");
  CHECK(stmt_err(run_env, "fun f():\n  return 1\n  2").message ==
        "'return' must end its function or a guard clause");
  CHECK(
      stmt_err(run_env, "fun f():\n  if a:\n    return 1\n  else:\n    2\n  3")
          .message == "'return' must end its function or a guard clause");

  // `:=` stays reserved pending mutation.
  CHECK(stmt_err(run_env, "x := 5").message ==
        "':=' assignment is not yet part of Monty");

  // Structural errors.
  CHECK(stmt_err(run_env, "  x").message == "unexpected indent");
  CHECK(stmt_err(run_env, "if a\n  f()").message == "expected ':'");
  CHECK(
      stmt_err(run_env, "if a: f()").message == "expected an indented block");
  CHECK(stmt_err(run_env, "fun f(5):\n  1").message ==
        "expected a parameter name");
  // A statement led by a keyword spelling reads as the keyword form; a
  // keyword-named variable is read from non-leading expression positions,
  // or from the lead behind grouping parens.
  CHECK(stmt_err(run_env, "fun + 1").message == "expected a function name");
  // Literal words name no binding: not in definitions, fun names, or
  // parameters.
  CHECK(stmt_err(run_env, "nil = 5").message ==
        "'nil' is a literal, not a name");
  CHECK(stmt_err(run_env, "fun true():\n  1").message ==
        "'true' is a literal, not a name");
  CHECK(stmt_err(run_env, "fun f(false):\n  1").message ==
        "'false' is a literal, not a name");
  CHECK(stmt_err(run_env, "fun f():\n  x = ").message ==
        "expected an expression");
  CHECK(stmt_err(run_env, "x, y").message == "expected end of line");

  // The statement parser's block depth seeds the expression parser: ten
  // block levels plus an expression fine on its own exceed the shared
  // budget.
  std::string stacked;
  for (size_t ndx = 0; ndx < 10; ++ndx)
    stacked += std::string(2 * ndx, ' ') + "if c:\n";
  stacked += std::string(20, ' ') + std::string(125, '(') + "x" +
             std::string(125, ')') + "\n";
  CHECK(stmt_err(run_env, stacked).message == "nesting too deep");
}

#pragma endregion
#pragma region Monty statement parser evaluates

TEST_CASE("Monty statement parser evaluates", "[coreb]") {
  runtime_environment run_env;
  evaluator ev(run_env);

  // End-to-end: a Monty program through the desugar into the evaluator,
  // yielding the last form's value.
  auto program_eval = [&](std::string_view src) {
    CAPTURE(src);
    auto lexed = monty::lexer::lex(src);
    REQUIRE(lexed.has_value());
    auto toks = *std::move(lexed);
    auto parsed = monty::statement_parser::parse_all(run_env, toks);
    REQUIRE(parsed.has_value());
    auto forms = *std::move(parsed);
    // Evaluation may collect at safe points; the pending forms are roots.
    gc_pin pin(run_env.rt, forms);
    std::string out;
    for (const auto& form : forms) {
      auto r = ev.eval(form);
      REQUIRE(r.has_value());
      out = r->print();
    }
    return out;
  };

  CHECK(program_eval("x = 5\nx + 1") == "6");
  CHECK(program_eval("fun = 5\n(fun + 1)") == "6");

  // The Hall escape end to end: an anonymous lambda, inexpressible in
  // Monty today, embeds and evaluates.
  CHECK(program_eval("double = %(lambda (n) (* n 2))\ndouble(21)") == "42");
  CHECK(program_eval("fun double(n):\n  n * 2\ndouble(21)") == "42");
  CHECK(program_eval("if 1 < 2:\n  \"yes\"\nelse:\n  \"no\"") == "\"yes\"");

  // The guard-clause rewrite, end to end.
  CHECK(
      program_eval(
          "fun fact(n):\n"
          "  if n == 0:\n"
          "    return 1\n"
          "  n * fact(n - 1)\n"
          "fact(10)") == "3628800");

  // An operator-mention `fun` rebinds the operator for later infix use.
  // Last in this test case: `-` stays rebound in the shared runtime.
  CHECK(program_eval("fun (-)(a, b):\n  a + b\n10 - 4") == "14");
}

#pragma endregion
#pragma region Monty unparser

namespace {

// Read Hall source and unparse its forms as Monty.
std::string unparse_hall(runtime_environment& run_env, std::string_view src) {
  CAPTURE(src);
  auto forms = hall_reader::read_all(run_env, src);
  REQUIRE(forms.has_value());
  return monty::unparser::unparse_all(run_env, *forms);
}

// Check the round trip: reading Hall, unparsing to Monty, and parsing that
// back reaches the same forms.
void check_roundtrip(runtime_environment& run_env, std::string_view src) {
  CAPTURE(src);
  auto forms = hall_reader::read_all(run_env, src);
  REQUIRE(forms.has_value());
  const auto monty_src = monty::unparser::unparse_all(run_env, *forms);
  CAPTURE(monty_src);
  auto lexed = monty::lexer::lex(monty_src);
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto back = monty::statement_parser::parse_all(run_env, toks);
  REQUIRE(back.has_value());
  REQUIRE(back->size() == forms->size());
  for (size_t ndx = 0; ndx < forms->size(); ++ndx)
    CHECK((*back)[ndx].print() == (*forms)[ndx].print());
}

} // namespace

TEST_CASE("Monty unparser", "[coreb]") {
  runtime_environment run_env;
  auto up = [&](std::string_view src) { return unparse_hall(run_env, src); };

  // Simple statements.
  CHECK(up("(define x 5)") == "x = 5");
  CHECK(up("(f x y)") == "f(x, y)");
  CHECK(up("(f)") == "f()");
  CHECK(up("((f a) b)") == "f(a)(b)");
  CHECK(up("(define x 5) (f x)") == "x = 5\nf(x)");

  // fun blocks; a zero-parameter list unparses from nil.
  CHECK(up("(define inc (lambda (n) (+ n 1)))") == "fun inc(n):\n  n + 1");
  CHECK(up("(define f (lambda nil 1 2))") == "fun f():\n  1\n  2");
  CHECK(up("(define f (lambda nil nil))") == "fun f():\n  nil");
  // An operator name spells as its mention; a non-lambda operator define
  // still escapes, since `=` takes a word.
  CHECK(up("(define - (lambda (a b) (- a b)))") == "fun (-)(a, b):\n  a - b");
  CHECK(up("(define - 5)") == "%(define - 5)");

  // if ladders when every arm is a block; the ternary otherwise.
  CHECK(up("(if a (begin 1))") == "if a:\n  1");
  CHECK(up("(if a (begin 1) (begin 2))") == "if a:\n  1\nelse:\n  2");
  CHECK(up("(if a (begin 1) (if b (begin 2) (begin 3)))") ==
        "if a:\n  1\nelif b:\n  2\nelse:\n  3");
  CHECK(up("(if c 1 2)") == "1 if c else 2");
  CHECK(up("(define x (if c 1 2))") == "x = 1 if c else 2");

  // A statement-position begin splats into the sequence; an
  // expression-position begin is the call-shaped sequencer. The same
  // if-form takes the block spelling at statement position and the
  // ternary with sequencer arms in expression position.
  CHECK(up("(begin (f) (g))") == "f()\ng()");
  CHECK(up("(define x (begin (+ 1 1) (+ 2 2)))") == "x = begin(1 + 1, 2 + 2)");
  CHECK(up("(begin)") == "begin()");
  CHECK(up("(if pred (begin (foo) (goo)) (begin (hoo) (ioo)))") ==
        "if pred:\n  foo()\n  goo()\nelse:\n  hoo()\n  ioo()");
  CHECK(up("(define zzz (if pred (begin (foo) (goo)) (begin (hoo) (ioo))))") ==
        "zzz = begin(foo(), goo()) if pred else begin(hoo(), ioo())");

  // Infix with minimal parens per the partial order.
  CHECK(up("(+ (- a b) c)") == "a - b + c");
  CHECK(up("(- a (- b c))") == "a - (b - c)");
  CHECK(up("(+ a (* b c))") == "a + (b * c)");
  CHECK(up("(* (+ a b) c)") == "(a + b) * c");
  // Negation of a number spells as the mention-call: `-7` is the literal.
  CHECK(up("(- 7)") == "(-)(7)");
  CHECK(up("(- -7)") == "(-)(-7)");
  CHECK(up("(- (- 7))") == "-(-)(7)");
  CHECK(up("(- (- x))") == "--x");
  CHECK(up("(- (+ a b))") == "-(a + b)");
  CHECK(up("(* (- x) y)") == "-x * y");
  CHECK(up("(< a b c)") == "a < b < c");
  CHECK(up("(!= a b)") == "a != b");
  CHECK(up("(< (+ a b) (* c d))") == "a + b < c * d");
  CHECK(up("(== (< a b) c)") == "(a < b) == c");

  // A negative literal round-trips as itself: the touching sign is part
  // of the literal.
  CHECK(up("(+ x -7)") == "x + -7");

  // Lists and operator mentions; arities without an infix spelling call
  // the mention.
  CHECK(up("(list 1 2)") == "[1, 2]");
  CHECK(up("(list)") == "[]");
  CHECK(up("(map - xs)") == "map((-), xs)");
  CHECK(up("(+ a b c)") == "(+)(a, b, c)");
  // Bare operator symbols spell as their mention, `=`/`:=` included, in
  // operand, head, and fun-name positions alike.
  CHECK(up("(f =)") == "f((=))");
  CHECK(up("(= a b)") == "(=)(a, b)");
  CHECK(up("(define := (lambda (a b) a))") == "fun (:=)(a, b):\n  a");
  // A `%` kernel symbol renders as-is: it has no Monty spelling today, so
  // this output does not re-lex.
  CHECK(up("(f %foo)") == "f(%foo)");
  // A define of a literal word arises only from embedder-built forms (the
  // reader makes the literal); it has no Monty spelling and escapes.
  auto& rt = run_env.rt;
  const auto define_nil = rt.cons(value{rt.intern("define")},
      rt.cons(value{rt.intern("nil")}, rt.cons(value{5}, value{})));
  CHECK(monty::unparser::unparse(run_env, define_nil) == "%(define nil 5)");

  // Escapes: shapes with no Monty spelling.
  CHECK(up("(define f (lambda (n)))") == "f = %(lambda (n))");
  CHECK(up("(quote x)") == "%(quote x)");
  CHECK(up("(define 5 6)") == "%(define 5 6)");
  CHECK(up("((lambda (n) n) 5)") == "%(lambda (n) n)(5)");
  CHECK(up("(f (quote x))") == "f(%(quote x))");
  CHECK(up("(a . b)") == "%(a . b)");

  // A keyword-led expression statement takes grouping parens; a keyword
  // definition does not need them.
  CHECK(up("(fun 1)") == "(fun(1))");
  CHECK(up("(define fun 5)") == "fun = 5");
}

#pragma endregion
#pragma region Monty unparser round trip

TEST_CASE("Monty unparser round trip", "[coreb]") {
  runtime_environment run_env;

  // Hall -> Monty -> Hall reaches the same forms.
  constexpr std::string_view sources[]{
      "(define x 5)",
      "(define inc (lambda (n) (+ n 1)))",
      "(define - (lambda (a b) (- a b)))",
      "(define f (lambda nil (if (== n 0) (begin 1) (begin (* n 2)))))",
      "(if a (begin 1) (if b (begin 2) (begin 3)))",
      "(if c 1 2)",
      "(+ (- a b) c)",
      "(- a (- b c))",
      "(* (+ a b) (/ c d))",
      "(< a b c)",
      "(list 1 (+ x 1) (list))",
      "(map - xs)",
      "(+ a b c)",
      "(define f (lambda (n)))",
      "(quote (a b))",
      "((lambda (n) (* n 2)) 21)",
      "(f (g x) (h))",
      "(fun 1)",
      "(define fun 5)",
      "(- 7)",
      "(- (- 7))",
      "(+ x -7)",
      "(define x (begin (f) (g)))",
      "(define zzz (if pred (begin (foo) (goo)) (begin (hoo) (ioo))))",
      "(f =)",
      "(= a b)",
      "(define := (lambda (a b) a))",
  };
  for (const auto src : sources) check_roundtrip(run_env, src);

  // Monty -> Hall -> Monty is textually the fixed point for canonical
  // source.
  constexpr std::string_view canonical[]{
      "x = 5",
      "fun inc(n):\n  n + 1",
      "fun (-)(a, b):\n  a - b",
      "if a:\n  f()\nelse:\n  g()",
      "x + -7",
      "(-)(7)",
      "(-)(-7)",
      "x = begin(f(), g())",
      "zzz = begin(foo(), goo()) if pred else begin(hoo(), ioo())",
      "f((=))",
      "a - (b - c)",
      "[1, x + 1]",
      "map((-), xs)",
      "(+)(a, b, c)",
      "f = %(lambda (n))",
      "1 if c else 2",
      "(fun(1))",
      "fun = 5\n(fun + 1)",
  };
  for (const auto src : canonical) {
    CAPTURE(src);
    auto lexed = monty::lexer::lex(src);
    REQUIRE(lexed.has_value());
    auto toks = *std::move(lexed);
    auto forms = monty::statement_parser::parse_all(run_env, toks);
    REQUIRE(forms.has_value());
    CHECK(monty::unparser::unparse_all(run_env, *forms) == src);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
