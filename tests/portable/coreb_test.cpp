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
    return v.error();
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

// NOLINTEND(readability-function-cognitive-complexity)
