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

#include <limits>
#include <string>
#include <type_traits>

#include "corvid/lang/coreb/coreb.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::coreb;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// Read one expression and return its printed form, checking that the form is
// canonical: reading the printed text back and printing again must reproduce
// it exactly, because a single round trip reaches the fixed point.
std::string echo(runtime& rt, std::string_view src) {
  CAPTURE(src);
  auto v = reader::read_one(rt, src);
  REQUIRE(v.has_value());
  auto text = v->print();
  auto again = reader::read_one(rt, text);
  REQUIRE(again.has_value());
  CHECK(again->print() == text);
  return text;
}

// Read every form in `src` and evaluate them in order, returning the last
// value's printed form.
std::string run(runtime& rt, evaluator& ev, std::string_view src) {
  CAPTURE(src);
  auto forms = reader::read_all(rt, src);
  REQUIRE(forms.has_value());
  REQUIRE_FALSE(forms->empty());
  // Evaluation may collect at safe points; the pending forms are roots.
  gc_pin pin(rt, *forms);
  value last;
  for (const auto& form : *forms) {
    auto v = ev.eval(form);
    REQUIRE(v.has_value());
    last = *v;
  }
  return last.print();
}

// Read every form in `src` and evaluate until one fails, returning the error
// message. Evaluating them all successfully fails the test.
std::string run_err(runtime& rt, evaluator& ev, std::string_view src) {
  CAPTURE(src);
  auto forms = reader::read_all(rt, src);
  REQUIRE(forms.has_value());
  // Evaluation may collect at safe points; the pending forms are roots.
  gc_pin pin(rt, *forms);
  for (const auto& form : *forms) {
    auto v = ev.eval(form);
    if (!v) return v.as_error().reason;
  }
  FAIL("evaluation succeeded");
  return {};
}

} // namespace

#pragma region CoreB values

TEST_CASE("CoreB values", "[coreb]") {
  runtime rt;

  SECTION("nil") {
    value v;
    CHECK(v.is_nil());
    CHECK(v.is_atom());
    CHECK(v.type() == kind::nil);
    CHECK(v.print() == "nil");
  }
  SECTION("booleans") {
    value t = true;
    CHECK(t.is_bool());
    CHECK(t.as_bool());
    CHECK(t.print() == "true");
    CHECK(value{false}.print() == "false");
  }
  SECTION("integers") {
    value n = 42;
    CHECK(n.is_int());
    CHECK(n.as_int() == 42);
    CHECK(n.print() == "42");
    CHECK(value{-7}.print() == "-7");
  }
  SECTION("floats") {
    value d = 2.5;
    CHECK(d.is_float());
    CHECK(d.as_float() == 2.5);
    CHECK(d.print() == "2.5");
    // An integral-valued float still prints as a float.
    CHECK(value{1.0}.print() == "1.0");
    CHECK(value{-3.0}.print() == "-3.0");
  }
  SECTION("strings") {
    value s = rt.make_string("hello");
    CHECK(s.is_string());
    CHECK(s.as_string() == "hello");
    CHECK(s.print() == R"("hello")");
  }
  SECTION("cells and lists") {
    auto one = rt.cons(value{1}, value{});
    CHECK(one.is_cell());
    CHECK_FALSE(one.is_atom());
    CHECK(one.print() == "(1)");
    auto two = rt.cons(value{1}, rt.cons(value{2}, value{}));
    CHECK(two.print() == "(1 2)");
    CHECK(two.head().print() == "1");
    CHECK(two.tail().print() == "(2)");
    auto dotted = rt.cons(value{1}, value{2});
    CHECK(dotted.print() == "(1 . 2)");
    auto nested = rt.cons(two, rt.cons(value{3}, value{}));
    CHECK(nested.print() == "((1 2) 3)");
    // The structural debug form hides nothing: every cell prints fully
    // dotted, and the output is still valid reader syntax.
    CHECK(one.dump() == "(1 . nil)");
    CHECK(two.dump() == "(1 . (2 . nil))");
    CHECK(dotted.dump() == "(1 . 2)");
    CHECK(nested.dump() == "((1 . (2 . nil)) . (3 . nil))");
    // Atoms dump as they print.
    CHECK(value{7}.dump() == "7");
  }
  SECTION("printing depth") {
    // List length costs no printing depth: a long flat list prints, and
    // dumps, in full.
    value flat;
    for (size_t ndx = 0; ndx < 1000; ++ndx) flat = rt.cons(value{1}, flat);
    std::string out;
    CHECK(flat.append(out));
    out.clear();
    CHECK(flat.append_dump(out));

    // Nesting through heads past the cap renders as a display form instead
    // of overflowing the C++ stack, and the return reports the truncation.
    value deep;
    for (size_t ndx = 0; ndx < value::max_depth + 10; ++ndx)
      deep = rt.cons(deep, value{});
    out.clear();
    CHECK_FALSE(deep.append(out));
    CHECK(out == std::string(value::max_depth, '(') + "#<too deep>" +
                     std::string(value::max_depth, ')'));
    out.clear();
    CHECK_FALSE(deep.append_dump(out));
  }
  SECTION("maybe accessors") {
    // Test and access in one step: the result is empty for a kind mismatch
    // and offers optional semantics like `value_or`.
    value n = 42;
    CHECK(n.maybe_int().value_or() == 42);
    CHECK_FALSE(n.maybe_cell());
    CHECK_FALSE(n.maybe_float());
    CHECK(n.maybe_float().value_or(2.5) == 2.5);
    auto lst = rt.cons(value{1}, value{});
    CHECK(lst.maybe_cell());
    CHECK(lst.maybe_cell()->head.as_int() == 1);
    CHECK(value{rt.intern("x")}.maybe_symbol()->name() == "x");
  }
}

#pragma endregion
#pragma region CoreB symbols

TEST_CASE("CoreB symbols", "[coreb]") {
  runtime rt;
  auto foo = rt.intern("foo");
  auto foo2 = rt.intern("foo");
  auto bar = rt.intern("bar");

  // Interning is canonicalizing: same spelling, same symbol.
  CHECK(foo == foo2);
  CHECK(foo != bar);
  CHECK(foo.name() == "foo");

  value v = foo;
  CHECK(v.is_symbol());
  CHECK(v.as_symbol() == foo);
  CHECK(v.print() == "foo");
}

#pragma endregion
#pragma region CoreB reader atoms

TEST_CASE("CoreB reader atoms", "[coreb]") {
  runtime rt;
  auto read = [&rt](std::string_view src) {
    auto v = reader::read_one(rt, src);
    REQUIRE(v.has_value());
    return *v;
  };

  CHECK(read("42").as_int() == 42);
  CHECK(read("-7").as_int() == -7);
  CHECK(read("+7").as_int() == 7);
  CHECK(read("2.5").as_float() == 2.5);
  CHECK(read("-0.5").as_float() == -0.5);
  CHECK(read(".5").as_float() == 0.5);
  CHECK(read("1e3").as_float() == 1000.0);
  CHECK(read("nil").is_nil());
  CHECK(read("true").as_bool());
  CHECK_FALSE(read("false").as_bool());
  CHECK(read("foo").is_symbol());
  CHECK(read("empty?").print() == "empty?");
  // Kernel-reserved symbols read like any other; only `define` polices them.
  CHECK(read("%comment").is_symbol());
  CHECK(read("%comment").print() == "%comment");
  // Signs alone are symbols, not numbers.
  CHECK(read("+").is_symbol());
  CHECK(read("-").is_symbol());
  // Only the lone '.' is punctuation; multi-dot tokens are ordinary symbols.
  CHECK(read("...").is_symbol());

  constexpr auto int_max = std::numeric_limits<int64_t>::max();
  CHECK(read("9223372036854775807").as_int() == int_max);
  // An integer too large for int64 falls back to floating point. The float
  // approximation it lands on is canonical, so `echo` verifies it survives
  // another round trip unchanged.
  CHECK(read("92233720368547758080").is_float());
  echo(rt, "92233720368547758080");
}

#pragma endregion
#pragma region CoreB reader lists

TEST_CASE("CoreB reader lists", "[coreb]") {
  runtime rt;

  CHECK(echo(rt, "(1 2 3)") == "(1 2 3)");
  CHECK(echo(rt, "( a ( b c )  d )") == "(a (b c) d)");
  CHECK(echo(rt, "()") == "nil");
  CHECK(echo(rt, "(a . b)") == "(a . b)");
  CHECK(echo(rt, "(a b . c)") == "(a b . c)");
  // The dotted spelling of a proper list collapses to the plain form.
  CHECK(echo(rt, "(a . (b . nil))") == "(a b)");
  CHECK(echo(rt, "(a . (b . c))") == "(a b . c)");
}

#pragma endregion
#pragma region CoreB reader strings

TEST_CASE("CoreB reader strings", "[coreb]") {
  runtime rt;
  auto read = [&rt](std::string_view src) {
    auto v = reader::read_one(rt, src);
    REQUIRE(v.has_value());
    return *v;
  };

  CHECK(read(R"("hello")").as_string() == "hello");
  CHECK(read(R"("a\nb")").as_string() == "a\nb");
  CHECK(read(R"("a\\b")").as_string() == R"(a\b)");
  CHECK(read(R"("say \"hi\"")").as_string() == R"(say "hi")");
  // Escapes survive a print/read round trip.
  CHECK(read(R"("say \"hi\"")").print() == R"("say \"hi\"")");
  CHECK(read(R"("tab\there")").print() == R"("tab\there")");

  // Hex escapes denote a byte by value.
  CHECK(read(R"("a\u{1f}b")").as_string() ==
        "a\x1f"
        "b");
  // Non-printables print as hex escapes, and round-trip.
  value ctl = rt.make_string("\x01\x7f");
  CHECK(ctl.print() == R"("\u{1}\u{7f}")");
  CHECK(read(ctl.print()).as_string() == "\x01\x7f");
}

#pragma endregion
#pragma region CoreB reader sugar and trivia

TEST_CASE("CoreB reader sugar and trivia", "[coreb]") {
  runtime rt;

  CHECK(echo(rt, "'x") == "(quote x)");
  CHECK(echo(rt, "'(1 2)") == "(quote (1 2))");
  CHECK(echo(rt, "; leading comment\n 42 ; trailing comment") == "42");

  auto all = reader::read_all(rt, "1 2 (3 4) ; done");
  REQUIRE(all.has_value());
  REQUIRE(all->size() == 3);
  CHECK((*all)[0].print() == "1");
  CHECK((*all)[2].print() == "(3 4)");

  auto none = reader::read_all(rt, " ; nothing here\n");
  REQUIRE(none.has_value());
  CHECK(none->empty());
}

#pragma endregion
#pragma region CoreB reader errors

TEST_CASE("CoreB reader errors", "[coreb]") {
  runtime rt;
  auto err = [&rt](std::string_view src) {
    auto v = reader::read_one(rt, src);
    REQUIRE_FALSE(v.has_value());
    return v.as_error();
  };

  CHECK(err("").message == "unexpected end of input");
  CHECK(err(")").message == "unmatched ')'");
  CHECK(err("(1 2").message == "unterminated list");
  // The unterminated list is reported at its opening paren.
  CHECK(err("  (1 2").pos == 2);
  CHECK(err("  (1 2").col == 3);
  // Errors carry a 1-based line and column.
  const auto nested = err("(a\n(b");
  CHECK(nested.message == "unterminated list");
  CHECK(nested.pos == 3);
  CHECK(nested.line == 2);
  CHECK(nested.col == 1);
  CHECK(err(R"("abc)").message == "unterminated string");
  CHECK(err(R"("a\qb")").message == "invalid escape");
  CHECK(err(R"("a\u{}b")").message == "invalid escape");
  CHECK(err(R"("a\u{1f")").message == "invalid escape");
  CHECK(err(R"("a\u{100}")").message == "invalid escape");
  CHECK(err("1abc").message == "malformed number");
  CHECK(err("1 2").message == "trailing content after expression");
  CHECK(err("(. 1)").message == "misplaced '.'");
  CHECK(err("(1 . )").message == "expected expression after '.'");
  CHECK(err("(1 . 2 3)").message == "expected ')' after dotted tail");
  // A lone '.' is dotted-tail punctuation, not an atom, even at top level.
  CHECK(err(".").message == "misplaced '.'");
  CHECK(err("'.").message == "misplaced '.'");

  // The incomplete flag marks the errors more input could repair, which is
  // how a REPL decides to keep reading rather than report.
  CHECK(err("(1 2").incomplete);
  CHECK(err(R"("abc)").incomplete);
  CHECK(err("").incomplete);
  CHECK(err("'").incomplete);
  CHECK_FALSE(err(")").incomplete);
  CHECK_FALSE(err("1abc").incomplete);

  const std::string deep(reader::max_depth + 1, '(');
  CHECK(err(deep).message == "nesting too deep");
  // A chain of quotes nests one level per quote.
  const std::string quotes(reader::max_depth + 1, '\'');
  CHECK(err(quotes + "x").message == "nesting too deep");
  // Depth counts nesting, not length: a long flat list is fine.
  std::string wide = "(";
  for (auto ndx = 0; ndx < 1000; ++ndx) wide += "x ";
  wide += ")";
  CHECK(reader::read_one(rt, wide).has_value());
}

#pragma endregion
#pragma region CoreB eval atoms and quote

TEST_CASE("CoreB eval atoms and quote", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  // Atoms evaluate to themselves.
  CHECK(run(rt, ev, "42") == "42");
  CHECK(run(rt, ev, "2.5") == "2.5");
  CHECK(run(rt, ev, "true") == "true");
  CHECK(run(rt, ev, "nil") == "nil");
  CHECK(run(rt, ev, R"("hello")") == R"("hello")");

  // Quote yields its argument unevaluated.
  CHECK(run(rt, ev, "'x") == "x");
  CHECK(run(rt, ev, "''x") == "(quote x)");
  CHECK(run(rt, ev, "(quote (1 2))") == "(1 2)");
  CHECK(run_err(rt, ev, "(quote)") == "quote: expects 1 argument");
  CHECK(run_err(rt, ev, "(quote a b)") == "quote: expects 1 argument");

  // A symbol evaluates to its binding; function values print as display
  // forms, a closure's showing its code but not its captured environment.
  CHECK(run(rt, ev, "+") == "#<primitive +>");
  CHECK(run(rt, ev, "(lambda (x) x)") == "#<lambda (x) x>");
  CHECK(run(rt, ev, "(lambda () 1 2)") == "#<lambda () 1 2>");
  CHECK(run_err(rt, ev, "y") == "unbound symbol: y");
}

#pragma endregion
#pragma region CoreB eval if

TEST_CASE("CoreB eval if", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  CHECK(run(rt, ev, "(if true 1 2)") == "1");
  CHECK(run(rt, ev, "(if false 1 2)") == "2");
  // Clojure-style truthiness: nil and false are falsy; everything else is
  // truthy, zero and the empty string included.
  CHECK(run(rt, ev, "(if nil 'y 'n)") == "n");
  CHECK(run(rt, ev, "(if '() 'y 'n)") == "n");
  CHECK(run(rt, ev, "(if 0 'y 'n)") == "y");
  CHECK(run(rt, ev, R"((if "" 'y 'n))") == "y");
  // One-armed if yields nil when the condition is falsy.
  CHECK(run(rt, ev, "(if false 1)") == "nil");
  // Only the chosen branch is evaluated: the head of a non-cell would error.
  CHECK(run(rt, ev, "(if true 1 (head 2))") == "1");
  CHECK(run_err(rt, ev, "(if true)") == "if: expects 2 or 3 arguments");
}

#pragma endregion
#pragma region CoreB eval define

TEST_CASE("CoreB eval define", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  // Define binds in the current scope, yields the defined name, and allows
  // rebinding.
  CHECK(run(rt, ev, "(define x 5)") == "x");
  CHECK(run(rt, ev, "x") == "5");
  CHECK(run(rt, ev, "(define x 6) x") == "6");

  // Primitives are ordinary bindings, so a Lisp-1 can pass them around.
  CHECK(run(rt, ev, "(define plus +) (plus 1 2)") == "3");

  // Definition polices reserved names.
  CHECK(run_err(rt, ev, "(define %x 1)") ==
        "'%' names are reserved for the kernel: %x");
  CHECK(run_err(rt, ev, "(define if 1)") == "cannot rebind special form: if");
  CHECK(run_err(rt, ev, "(define 5 1)") == "define: expects a symbol, got: 5");
  CHECK(run_err(rt, ev, "(define y)") == "define: expects a name and a value");
}

#pragma endregion
#pragma region CoreB eval lambda and closures

TEST_CASE("CoreB eval lambda and closures", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  CHECK(run(rt, ev, "((lambda (x) x) 42)") == "42");
  CHECK(run(rt, ev, "(define add (lambda (a b) (+ a b))) (add 2 3)") == "5");
  CHECK(run(rt, ev, "add") == "#<lambda (a b) (+ a b)>");

  // The classic closure test: the inner lambda captures its birthplace's
  // `n`, which outlives the call that created it.
  CHECK(run(rt, ev,
            "(define make-adder (lambda (n) (lambda (x) (+ x n))))"
            "(define add3 (make-adder 3))"
            "(add3 4)") == "7");
  // Scoping is lexical, not dynamic: a global `n` does not leak into the
  // closure, whose captured `n` still shadows it.
  CHECK(run(rt, ev, "(define n 100) (add3 4)") == "7");

  CHECK(run_err(rt, ev, "((lambda (a b) a) 1)") ==
        "lambda: expects 2 arguments, got 1");
  CHECK(
      run_err(rt, ev, "(lambda (a a) a)") == "lambda: duplicate parameter: a");
  CHECK(run_err(rt, ev, "(lambda (a 5) a)") ==
        "lambda: parameter is not a symbol: 5");
  CHECK(run_err(rt, ev, "(lambda (x))") ==
        "lambda: expects a parameter list and a body");
  // The variadic spellings are reserved until they are implemented.
  CHECK(run_err(rt, ev, "(lambda (a . rest) a)") ==
        "lambda: variadic parameters are not yet supported");
  CHECK(run_err(rt, ev, "(lambda args args)") ==
        "lambda: variadic parameters are not yet supported");
}

#pragma endregion
#pragma region CoreB eval begin and sequencing

TEST_CASE("CoreB eval begin and sequencing", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  CHECK(run(rt, ev, "(begin 1 2 3)") == "3");
  CHECK(run(rt, ev, "(begin)") == "nil");
  // A lambda body is an implicit sequence; the last expression is its value.
  CHECK(run(rt, ev, "((lambda () 1 2))") == "2");
  // Define works inside a body, binding in the call's scope, not the global
  // one.
  CHECK(run(rt, ev, "((lambda () (define local 9) (+ local 1)))") == "10");
  CHECK(run_err(rt, ev, "local") == "unbound symbol: local");
}

#pragma endregion
#pragma region CoreB eval tail calls

TEST_CASE("CoreB eval tail calls", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  // A tail-recursive loop runs in constant C++ stack: 100000 iterations
  // would overflow any real stack if each call recursed.
  CHECK(run(rt, ev,
            "(define loop (lambda (n) (if (= n 0) 'done (loop (- n 1)))))"
            "(loop 100000)") == "done");

  // Mutual tail recursion too. Note that `even?` calls `odd?` before it is
  // defined; the symbol is looked up at call time, not definition time.
  CHECK(run(rt, ev,
            "(define even? (lambda (n) (if (= n 0) true (odd? (- n 1)))))"
            "(define odd? (lambda (n) (if (= n 0) false (even? (- n 1)))))"
            "(even? 100001)") == "false");

  // `begin`'s finale is a tail position too: routed through it, the loop
  // still runs in constant stack.
  CHECK(run(rt, ev,
            "(define loop2 (lambda (n)"
            "  (begin 0 (if (= n 0) 'done (loop2 (- n 1))))))"
            "(loop2 100000)") == "done");

  // Non-tail recursion is the contrast: the multiply happens after the
  // recursive call returns, so each level consumes real depth and the guard
  // catches runaways.
  CHECK(run(rt, ev,
            "(define fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))"
            "(fact 20)") == "2432902008176640000");
  CHECK(run_err(rt, ev, "(fact 2000)") == "evaluation too deep");
}

#pragma endregion
#pragma region CoreB eval arithmetic

TEST_CASE("CoreB eval arithmetic", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  CHECK(run(rt, ev, "(+ 1 2)") == "3");
  CHECK(run(rt, ev, "(+)") == "0");
  CHECK(run(rt, ev, "(*)") == "1");
  CHECK(run(rt, ev, "(* 2 3 4)") == "24");
  CHECK(run(rt, ev, "(- 10 1 2)") == "7");
  CHECK(run(rt, ev, "(- 5)") == "-5");
  // A float operand switches the fold to floating point.
  CHECK(run(rt, ev, "(+ 1 2.5)") == "3.5");
  CHECK(run(rt, ev, "(* 2 0.5)") == "1.0");
  // Integer overflow falls back to floating point, matching what the reader
  // does with oversized integer literals.
  CHECK(run(rt, ev, "(* 9223372036854775807 2)") == "1.8446744073709552e+19");
  CHECK(run(rt, ev, "(- -9223372036854775808)") == "9.223372036854776e+18");

  CHECK(run_err(rt, ev, "(+ 1 'a)") == "+: expects numbers, got: a");
  CHECK(run_err(rt, ev, "(-)") == "-: expects at least 1 argument");

  // Division stays exact when it divides evenly and otherwise falls to
  // double; there is no ratio type.
  CHECK(run(rt, ev, "(/ 6 3)") == "2");
  CHECK(run(rt, ev, "(/ 7 2)") == "3.5");
  CHECK(run(rt, ev, "(/ 2)") == "0.5");
  CHECK(run(rt, ev, "(/ 24 2 2)") == "6");
  CHECK(run(rt, ev, "(/ -9223372036854775808 -1)") == "9.223372036854776e+18");
  // An exact zero divisor is an error; a float zero divisor follows IEEE.
  CHECK(run_err(rt, ev, "(/ 5 0)") == "/: division by zero");
  CHECK(run(rt, ev, "(/ 1 0.0)") == "inf");
}

#pragma endregion
#pragma region CoreB eval comparisons

TEST_CASE("CoreB eval comparisons", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  // Comparisons chain across adjacent pairs, so (< a b c) is a < b and
  // b < c.
  CHECK(run(rt, ev, "(< 1 2 3)") == "true");
  CHECK(run(rt, ev, "(< 1 3 2)") == "false");
  CHECK(run(rt, ev, "(<= 1 1 2)") == "true");
  CHECK(run(rt, ev, "(> 3 2 1)") == "true");
  CHECK(run(rt, ev, "(>= 2 2 1)") == "true");
  CHECK(run(rt, ev, "(= 1 1 1)") == "true");
  // A mixed pair compares numerically across the int/float divide.
  CHECK(run(rt, ev, "(= 1 1.0)") == "true");
  // `!=` takes exactly 2: chained adjacent inequality would be a trap.
  CHECK(run(rt, ev, "(!= 1 2)") == "true");
  CHECK(run(rt, ev, "(!= 1 1)") == "false");
  CHECK(run_err(rt, ev, "(!= 1 2 3)") == "!=: expects 2 arguments");

  CHECK(run_err(rt, ev, "(< 1)") == "<: expects at least 2 arguments");
  // Comparisons are numeric only; symbol and string equality is a later,
  // separate primitive.
  CHECK(run_err(rt, ev, "(= 'a 'a)") == "=: expects numbers, got: a");
}

#pragma endregion
#pragma region CoreB eval lists

TEST_CASE("CoreB eval lists", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  CHECK(run(rt, ev, "(cons 1 2)") == "(1 . 2)");
  CHECK(run(rt, ev, "(cons 1 nil)") == "(1)");
  CHECK(run(rt, ev, "(head '(1 2))") == "1");
  CHECK(run(rt, ev, "(tail '(1 2 3))") == "(2 3)");
  CHECK(run(rt, ev, "(nil? nil)") == "true");
  CHECK(run(rt, ev, "(nil? '())") == "true");
  CHECK(run(rt, ev, "(nil? 0)") == "false");
  CHECK(run(rt, ev, "(nil? '(1))") == "false");

  CHECK(run_err(rt, ev, "(head 5)") == "head: expects a cell, got: 5");
  CHECK(run_err(rt, ev, "(tail 5)") == "tail: expects a cell, got: 5");
}

#pragma endregion
#pragma region CoreB eval persistent runtime

TEST_CASE("CoreB eval persistent runtime", "[coreb]") {
  runtime rt;
  {
    evaluator ev(rt);
    CHECK(run(rt, ev,
              "(define x 5)"
              "(define double (lambda (n) (* n 2)))") == "double");
    // Builtins are ordinary bindings, so even rebinding one sticks.
    CHECK(run(rt, ev, "(define + 42)") == "+");
  }
  // The global scope is the runtime's root environment, so a later evaluator
  // over the same runtime sees everything the first one defined; stocking
  // never overwrites an existing binding, which is why the rebound `+`
  // survives.
  evaluator again(rt);
  CHECK(run(rt, again, "x") == "5");
  CHECK(run(rt, again, "(double 4)") == "8");
  CHECK(run(rt, again, "+") == "42");
}

#pragma endregion
#pragma region CoreB eval errors

TEST_CASE("CoreB eval errors", "[coreb]") {
  runtime rt;
  evaluator ev(rt);

  CHECK(run_err(rt, ev, "(5 1)") == "not callable: 5");
  CHECK(run_err(rt, ev, R"(("no" 1))") == R"(not callable: "no")");
  // A form must be a proper list; a dotted call is malformed.
  CHECK(run_err(rt, ev, "(+ 1 . 2)") == "improper form: (+ 1 . 2)");
  // Errors propagate out of nested evaluation.
  CHECK(run_err(rt, ev, "(+ 1 (head 2))") == "head: expects a cell, got: 2");
}

#pragma endregion
#pragma region CoreB garbage collection

TEST_CASE("CoreB gc", "[coreb]") {
  runtime rt;
  // A fresh runtime owns exactly one heap object: the root environment.
  const auto baseline = rt.live_objects();
  CHECK(baseline == 1);

  SECTION("unreachable garbage is collected") {
    CHECK(rt.cons(value{1}, rt.cons(value{2}, value{})).is_cell());
    CHECK(rt.live_objects() == baseline + 2);
    rt.collect();
    CHECK(rt.live_objects() == baseline);
  }
  SECTION("a pin roots the variable, not a copy") {
    // A temporary cannot be pinned: it would dangle by the semicolon.
    static_assert(!std::is_constructible_v<gc_pin, runtime&, value>);
    value v = rt.cons(value{1}, value{});
    gc_pin pin(rt, v);
    rt.collect();
    CHECK(v.print() == "(1)");
    // Rebinding the pinned variable roots the new value on the next
    // collection, and the old one becomes garbage.
    v = rt.cons(value{2}, value{});
    rt.collect();
    CHECK(v.print() == "(2)");
    CHECK(rt.live_objects() == baseline + 1);
  }
  SECTION("root environment bindings survive") {
    rt.root_env().bind(rt.intern("keep"), rt.cons(value{1}, value{}));
    CHECK(rt.cons(value{9}, value{}).is_cell()); // Abandoned garbage.
    rt.collect();
    CHECK(rt.live_objects() == baseline + 1);
    CHECK(rt.root_env().lookup(rt.intern("keep"))->print() == "(1)");
  }
  SECTION("cycles are collected once unreachable") {
    evaluator ev(rt);
    // A self-recursive definition is a reference cycle without any mutation
    // primitive: the closure captures the very scope that binds it. While
    // reachable, it survives collection.
    CHECK(run(rt, ev, "(define f (lambda (n) (f n)))") == "f");
    rt.collect();
    CHECK(run(rt, ev, "(nil? f)") == "false");
    const auto with_f = rt.live_objects();
    // Unreachable, the whole cycle goes, which reference counting could
    // never do.
    CHECK(run(rt, ev, "(define f nil)") == "f");
    rt.collect();
    CHECK(rt.live_objects() < with_f);
  }
  SECTION("marking iterates over deep structure") {
    value deep;
    gc_pin pin(rt, deep);
    for (size_t ndx = 0; ndx < 100'000; ++ndx) deep = rt.cons(deep, value{});
    rt.collect();
    CHECK(rt.live_objects() == baseline + 100'000);
  }
  SECTION("the evaluator collects at its safe point") {
    evaluator ev(rt);
    const auto before = rt.live_objects();
    // The tail loop allocates one call frame per iteration; without the
    // safe point, all 100000 environments would still be here afterward.
    // Collections along the way keep the heap near the trigger threshold.
    CHECK(run(rt, ev,
              "(define loop (lambda (n) (if (= n 0) 'done (loop (- n 1)))))"
              "(loop 100000)") == "done");
    CHECK(rt.live_objects() < before + (2 * runtime::gc_threshold));
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
