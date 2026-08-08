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
std::string parse_dump(runtime& rt, std::string_view src) {
  CAPTURE(src);
  auto lexed = monty::lexer::lex(src);
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto v = monty::expression_parser::parse(rt, toks);
  REQUIRE(v.has_value());
  if (toks.at(monty::token_kind::newline)) toks.take();
  REQUIRE(toks.at(monty::token_kind::eof));
  return v->print();
}

// Parse a Monty expression expecting failure, returning the error.
source_error parse_err(runtime& rt, std::string_view src) {
  CAPTURE(src);
  auto lexed = monty::lexer::lex(src);
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto r = monty::expression_parser::parse(rt, toks);
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

  // Errors carry a 1-based line and column.
  e = lex_err("ok\n   x");
  CHECK(e.line == 2);
  CHECK(e.col == 4);
}

#pragma endregion
#pragma region Monty expression parser

TEST_CASE("Monty expression parser", "[coreb]") {
  runtime rt;

  // Primaries and literals.
  CHECK(parse_dump(rt, "x") == "x");
  CHECK(parse_dump(rt, "nil?") == "nil?");
  CHECK(parse_dump(rt, "42") == "42");
  CHECK(parse_dump(rt, "2.5") == "2.5");
  CHECK(parse_dump(rt, "nil") == "nil");
  CHECK(parse_dump(rt, "true") == "true");
  CHECK(parse_dump(rt, "false") == "false");
  CHECK(parse_dump(rt, R"("a\nb")") == R"("a\nb")");

  // Integer overflow falls back to double, matching the kernel rule.
  CHECK(parse_dump(rt, "99999999999999999999") == "1e+20");

  // Calls bind tightest and fold left.
  CHECK(parse_dump(rt, "f(x, y)") == "(f x y)");
  CHECK(parse_dump(rt, "f()") == "(f)");
  CHECK(parse_dump(rt, "f(a)(b)") == "((f a) b)");
  CHECK(parse_dump(rt, "f(g(x))") == "(f (g x))");

  // Arithmetic families fold left; parentheses group.
  CHECK(parse_dump(rt, "a + b - c") == "(- (+ a b) c)");
  CHECK(parse_dump(rt, "a * b / c") == "(/ (* a b) c)");
  CHECK(parse_dump(rt, "a + (b * c)") == "(+ a (* b c))");
  CHECK(parse_dump(rt, "(a + b) * c") == "(* (+ a b) c)");

  // Unary minus binds to the postfix chain it precedes.
  CHECK(parse_dump(rt, "-7") == "(- 7)");
  CHECK(parse_dump(rt, "a - -b") == "(- a (- b))");
  CHECK(parse_dump(rt, "-f(x) * y") == "(* (- (f x)) y)");

  // Comparison chains are same-operator and n-ary; arithmetic sits above.
  CHECK(parse_dump(rt, "a == b") == "(== a b)");
  CHECK(parse_dump(rt, "a != b") == "(!= a b)");
  CHECK(parse_dump(rt, "a < b < c") == "(< a b c)");
  CHECK(parse_dump(rt, "a + b < c * d") == "(< (+ a b) (* c d))");

  // The ternary desugars to the kernel `if` and chains rightward.
  CHECK(parse_dump(rt, "x if c else y") == "(if c x y)");
  CHECK(parse_dump(rt, "a if c else b if d else e") == "(if c a (if d b e))");

  // A parenthesized operator mentions it as a value.
  CHECK(parse_dump(rt, "map((-), xs)") == "(map - xs)");
  CHECK(parse_dump(rt, "(+)") == "+");

  // Bracket continuation carries an expression across lines.
  CHECK(parse_dump(rt, "f(a,\n  b)") == "(f a b)");
}

#pragma endregion
#pragma region Monty expression parser errors

TEST_CASE("Monty expression parser errors", "[coreb]") {
  runtime rt;

  // The sparse partial order: family mixing and mixed chains are errors.
  CHECK(parse_err(rt, "a + b * c").message ==
        "mixing '+'/'-' with '*'/'/' requires parentheses");
  CHECK(parse_err(rt, "a * b + c").message ==
        "mixing '+'/'-' with '*'/'/' requires parentheses");
  CHECK(parse_err(rt, "a < b <= c").message ==
        "comparison chains cannot mix operators");
  CHECK(parse_err(rt, "a != b != c").message == "'!=' does not chain");

  // `=` and `:=` are statements, rejected with dedicated messages.
  CHECK(parse_err(rt, "x = 5").message ==
        "'=' is a definition statement, not an expression");
  CHECK(parse_err(rt, "x := 5").message ==
        "':=' is reserved for assignment, a statement");
  CHECK(parse_err(rt, "f(x = 5)").message ==
        "'=' is a definition statement, not an expression");
  CHECK(parse_err(rt, "(=)").message == "'=' is a statement, not a value");

  // Reserved forms awaiting rulings.
  CHECK(parse_err(rt, "[1, 2]").message ==
        "list literals are not yet supported");
  CHECK(parse_err(rt, "xs[0]").message == "indexing is not yet part of Monty");

  // Structural errors.
  CHECK(parse_err(rt, "x if c").message ==
        "expected 'else' after ternary condition");
  CHECK(parse_err(rt, "()").message == "expected an expression");
  CHECK(parse_err(rt, "a +\nb").message == "expected an expression");

  // Parsing consumes exactly one expression; the leftover tokens stay in
  // the stream for the caller to judge.
  auto lexed = monty::lexer::lex("a b");
  REQUIRE(lexed.has_value());
  auto toks = *std::move(lexed);
  auto v = monty::expression_parser::parse(rt, toks);
  REQUIRE(v.has_value());
  CHECK(v->print() == "a");
  CHECK(toks.at_word("b"));

  // Nesting is depth-guarded rather than risking the C++ stack.
  const std::string deep =
      std::string(monty::expression_parser::max_depth + 1, '(') + "x" +
      std::string(monty::expression_parser::max_depth + 1, ')');
  CHECK(parse_err(rt, deep).message == "nesting too deep");
  CHECK(parse_err(rt,
            std::string(monty::expression_parser::max_depth + 1, '-') + "x")
            .message == "nesting too deep");

  // Errors carry the offending position.
  auto e = parse_err(rt, "(1 +\n*)");
  CHECK(e.message == "expected an expression");
  CHECK(e.line == 2);
  CHECK(e.col == 1);
}

#pragma endregion
#pragma region Monty expression parser evaluates

TEST_CASE("Monty expression parser evaluates", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  // End-to-end: Monty source through the desugar into the evaluator.
  auto parse_eval = [&](std::string_view src) {
    CAPTURE(src);
    auto lexed = monty::lexer::lex(src);
    REQUIRE(lexed.has_value());
    auto toks = *std::move(lexed);
    auto v = monty::expression_parser::parse(rt, toks);
    REQUIRE(v.has_value());
    // Evaluation may collect at safe points; the pending form is a root.
    std::vector<value> forms{*v};
    gc_pin pin(rt, forms);
    auto r = ev.eval(forms[0]);
    REQUIRE(r.has_value());
    return r->print();
  };

  CHECK(parse_eval("(1 + 2) * 3") == "9");
  CHECK(parse_eval("1 + 2 == 3") == "true");
  CHECK(parse_eval("1 < 2 < 3") == "true");
  CHECK(parse_eval("-2 * -3") == "6");
  CHECK(parse_eval("10 / 4") == "2.5");
  CHECK(parse_eval("1 if nil else 2") == "2");
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
