# CoreB

CoreB is a yet another Lisp dialect, this time with a surface syntax closer to Python or Pascal than to parentheses, in the spirit of Rhombus and Dylan and infected by decades of C++. It is bootstrapped by a small C++ kernel and otherwise written in itself. The name is a pun on "corbie", Scots for crow. There is no CoreA.

It exists because the only way to avoid being destroyed by [Greenspun's tenth rule](https://en.wikipedia.org/wiki/Greenspun%27s_tenth_rule) is to embrace it. Also, it seemed like a good idea at the time. More fundamentally, this is an exercise in education. And on that note, here's a tip: if someone tells you that Lisp is pronounced "lithp", they're messing with you.

This document is the design record and status tracker for the project. It is written for a reader who is not assumed to know Lisp internals or compiler construction; terms are explained as they are introduced, and there is a glossary at the end.


## Goals and non-goals

Goals:

- Learning. The point is to build a language from scratch and understand every part of it.
- Teaching. The code is self-explanatory to the level where reading it teaches the language..
- A Lisp-1 with full macro power, reachable through a syntax that does not repel people who dislike parentheses.
- Self-hosting: the C++ kernel stays minimal, and as much of the language as possible is written in CoreB itself.

Non-goals:

- Commercial use, popularity, or performance. No optimization work beyond what correctness requires.
- Parser generators or compiler-compilers (ANTLR, yacc, and kin). Every parser here is written by hand; that is what "from scratch" means.

One deferred goal: eventually transpile CoreB into something a real compiler can chew on. That future tool is reserved the name CoreC and is not part of the current effort beyond the occasional forward reference.

## The central design decision

Lisp's macro power comes from homoiconicity: code is represented as ordinary data (nested lists), so a macro is just a function that receives code as lists and returns new code as lists. A non-parenthesized surface syntax threatens this, because it raises the question of what data structure macros operate on.

CoreB resolves this with a two-layer design:

1. The surface syntax is a skin. A hand-written lexer and parser turn Pythonish source text into classic s-expressions. Everything downstream of the parser (the evaluator, the macro expander, the standard library, the eventual self-hosted compiler) works on s-expressions, exactly as in a traditional Lisp.
2. Sugar on top. An unparser renders s-expressions back into surface syntax. Macro-expanded code, error messages, and REPL output can therefore be shown in CoreB notation rather than parentheses. The parenthesized form remains available as an internal and debugging format, much like assembly listings from a C++ compiler. The unparser emits canonical formatting at first. Full round-tripping is a deferred goal, not a non-goal: code written in the REPL must eventually be savable to a file, which means comments (and blank lines, treatable as a degenerate comment) become part of the s-expression representation as trivia that is never executed. The mechanism is deliberately unchosen until it matters; candidates are trivia annotations that the evaluator and macro expander transparently skip, or a fidelity mode in the reader. The known tax: if comments are list elements, every list-walking transform must know to step over them.

This buys full Lisp macro power at the cost of one interesting parser, and it makes self-hosting tractable because the language underneath is a conventional Lisp.

A corollary worth stating: because the surface syntax is a skin, nothing stops CoreB from carrying several parser/unparser pairs over the same s-expression core. Syntax dialects are pluggable; the Pythonesque one is merely first.

## Locked decisions

- **Lisp-1.** Functions and variables share a single namespace. `f(x)` and `x` resolve `f` and `x` the same way. (Common Lisp is a Lisp-2, with separate namespaces; Scheme, Clojure, and CoreB are Lisp-1s.) Corollary: functions are ordinary first-class values. A `lambda` yields a value that is stored in a variable, passed, and returned like any other; a call form just evaluates the variable in call position and applies the result.
- **Macro hygiene: the Clojure-style middle.** A Lisp-1 with fully unhygienic macros invites variable capture, where a name introduced by a macro accidentally collides with a name at the use site. Scheme solves this with full hygiene, which is a research project in itself. CoreB follows Clojure instead: quasiquote support with auto-gensym, so macro authors get fresh, collision-proof names with minimal ceremony. See the glossary for `gensym`.
- **Garbage collection: simple mark-and-sweep in the kernel.** Reference counting via `shared_ptr` was rejected because Lisp programs create reference cycles casually, and refcounting leaks them. A stop-the-world mark-and-sweep collector is simple, correct, and in the spirit of the project. Long-term, this argues for an eventual transpile target that brings its own real GC.
- **Proper tail calls from day one.** A tail call is a call in final position, where the caller has nothing left to do afterward. The evaluator reuses the caller's frame for such calls, so tail recursion (including mutual recursion) runs in constant stack. This is built into the evaluator loop from the start because it is easy to design in and miserable to retrofit. Iteration constructs such as `for-each` are provided, but they are macros that expand into tail-recursive code: you write iteration, it is recursion underneath.
- **Kernel/library split.** The C++ kernel contains only what cannot be written in CoreB: the reader and printer, the value representation, the garbage collector, `eval`/`apply`, and a small set of primitive functions. Everything else, including the macro expander once it can be expressed, is written in CoreB.

## Architecture

```
source text (.coreb)
      |
      v
lexer + parser (C++, hand-written; Pratt parsing for infix)
      |
      v
s-expressions  <--------> unparser (renders back to surface syntax)
      |
      v
macro expander (CoreB, eventually; kernel-assisted at first)
      |
      v
evaluator (C++: environments, closures, proper tail calls)
      |
      v
primitives (C++) + standard library (CoreB)
```

The kernel lives under "corvid/lang/coreb/" as header-only C++23, tested with Catch2, following normal repo conventions. The lexer and reader build on the existing Corvid string machinery (locators and parsers in "corvid/strings/", character classification and conversion in "cases.h" and "conversion.h") rather than reinventing it; the kernel doubles as a dogfooding exercise for the library. The existing "ast_pred.h" one directory up is an unrelated predicate-AST exercise and stays untouched.

## Leanings (recorded, not locked)

Directions the project intends to grow, noted early so later milestones can leave room for them:

- **Optional type annotations, Dylan-flavored.** Dylan lets parameters and bindings carry type constraints while remaining a dynamic Lisp underneath. CoreB leans the same way: annotations are optional, checked at runtime to start, and become fuel for CoreC later. Not deeply Lispy, deliberately so.
- **OOP as generic functions with multiple dispatch.** In the CLOS/Dylan lineage, methods are not owned by classes; a generic function holds a family of methods, each specialized on the types of its arguments, and a call dispatches to the most specific match. This fits a Lisp-1 naturally (a generic function is just a value in a variable) and matches the instinct of "macros that apply to a constrained type list" better than message-passing OOP would.

## Surface syntax sketch (NOT final)

Nothing in this section is decided. It exists to make the two-layer design concrete and to seed the milestone 4 discussion. A strawman:

```
def greet(name):
  if empty?(name):
    "hello, world"
  else:
    concat("hello, ", name)
```

which the parser would desugar to the s-expression

```
(define greet
  (lambda (name)
    (if (empty? name)
        "hello, world"
        (concat "hello, " name))))
```

Block structure is decided: Python-style indentation-as-blocks, ruled 2026-08-02. The user's reasoning: C's wart is that it is not a full-blocking language (`if (p) s;` where `s` merely may be compound), and the conventional brace indentation is a cosmetic cover for that. Dylan-style `define method X`/`end method` full blocking was considered and set aside as too ceremonious; if a delimited dialect ever happens, it arrives as a second pluggable skin, not a redesign. The lexer will synthesize INDENT/DEDENT tokens, as Python's does.

Open syntax questions, deferred to milestone 4: operator set and precedence, how quotation and quasiquotation look in surface form, and line-continuation rules for expressions that span lines.

## Roadmap

Milestones are ordered semantics-first: the pretty syntax arrives at milestone 4, not 1, so that every parser decision is tested against a working evaluator instead of guessed at.

1. **Value model, s-expr reader and printer.** [DONE] Tagged value type, interned symbols, cons cells, a classic parenthesized reader, and a printer. The parens here are scaffolding, not the product: macros and `quote` traffic in s-expressions regardless, and this gives a working input format for testing the evaluator long before the surface parser exists. Landed as "value.h" (`value` over the library's `enum_variant`, a slim pointer-identity `symbol`, and a `runtime` owning the heap and symbol table, with heap objects constructible only by the runtime) and "reader.h" (recursive-descent, `std::expected` errors carrying position/line/column, grammar documented as EBNF), tested by "tests/portable/coreb_test.cpp". Kernel rulings made here: the halves of a cons cell are named `head` and `tail`, retiring car and cdr along with the IBM 704 registers they abbreviated; nil unifies with the empty list, printing as "nil" while the reader accepts "()" too; `nil`/`true`/`false` are reader literals, not symbols; integers are int64 with literal overflow falling back to double; the string escape set is `\"` `\\` `\n` `\t` `\r` plus `\u{hex}` denoting a byte by value, implemented by the shared escaping utilities in "corvid/strings/conversion.h" (one grammar for the printer, the reader, and the library's debug escaping, so non-printables round-trip); quote sugar `'x` reads as `(quote x)`.
2. **Evaluator.** [NOT STARTED] Environments, closures, special forms (`quote`, `if`, `define`, `lambda`, and the minimum around them), with the tail-call-preserving evaluation loop built in from the start. Includes a minimal REPL driver (a `notest_` executable, per repo convention), since interactive poking is half the point.
3. **Garbage collector.** [NOT STARTED] Mark-and-sweep. Development can run leaky until this lands.
4. **Surface syntax.** [NOT STARTED] Hand-written lexer, Pratt parser for infix precedence, desugaring to s-expressions, and the unparser going the other way.
5. **Macro expander.** [NOT STARTED] Quasiquote with Clojure-style auto-gensym.
6. **Standard library in CoreB.** [NOT STARTED] Including `for-each` and other iteration forms as macros over tail recursion. Self-hosting starts being real here.
7. **CoreC.** [RESERVED] A transpiler emitting C or C++. Not designed. One known constraint: C does not guarantee tail calls, so CoreC will need a technique such as a trampoline.

## Glossary

- **s-expression:** nested lists written with parentheses, e.g. `(if (empty? name) "hello" name)`. The universal data format of Lisp; both code and data take this shape.
- **M-expression:** McCarthy's originally intended surface syntax for Lisp, writing `f[x; y]` for what the s-expression spells `(f x y)`. It was never implemented; programmers used s-expressions directly and the sugar died. Wolfram Language's `f[x, y]` is the one thriving descendant.
- **homoiconicity:** the property that a language's code is represented in the language's own basic data structures, so programs can manipulate programs.
- **Lisp-1 / Lisp-2:** whether functions and variables share one namespace (Lisp-1) or live in two (Lisp-2).
- **special form:** a construct the evaluator handles directly with its own evaluation rule, such as `if` (which must not evaluate both branches) or `quote` (which must not evaluate its argument at all). Everything that is not a special form or macro is an ordinary function call.
- **closure:** a function value bundled with the environment it was created in, so it can refer to variables from its birthplace even when called elsewhere.
- **tail call:** a call in final position; the caller has nothing left to do after it. Proper tail-call support reuses the caller's frame, so loops written as recursion run in constant stack.
- **quasiquote:** a template mechanism for building code: quote a skeleton, then mark the holes to be filled in with computed values. The workhorse of macro writing.
- **interning:** canonicalizing symbols so that every occurrence of the same spelling is the same object, making symbol comparison a pointer comparison.
- **gensym:** "generated symbol", a fresh name guaranteed not to collide with any other, achieved by never entering it into the intern table. Interning maps same spelling to same symbol; gensym manufactures a symbol no spelling can ever reach. Auto-gensym generates these inside quasiquote so macro-introduced variables cannot capture user variables.
- **generic function / multiple dispatch:** a function that holds several method implementations and picks one per call based on the runtime types of the arguments (all of them, not just a privileged `this`).
- **Pratt parser:** a compact hand-written parsing technique (also called top-down operator precedence) that handles infix operators and precedence elegantly without a parser generator.
- **mark-and-sweep:** a garbage collection strategy: start from the live roots, mark everything reachable, then sweep up (free) everything unmarked. Handles cycles, which reference counting cannot.
- **trampoline:** a loop that repeatedly invokes a function which returns either a final result or the next function to call. A standard trick for getting tail-call behavior on targets (like C) that do not guarantee it.
