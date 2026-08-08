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
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../containers/core/value_or_error.h"
#include "../../containers/core/opt_find.h"
#include "../../containers/core/scoped_value.h"
#include "value.h"

namespace corvid { inline namespace lang { namespace coreb {

using namespace std::literals;

// CoreB evaluator.
//
// Evaluates the s-expressions the reader produces: lexical environments,
// closures, special forms, and proper tail calls, over the value model in
// "value.h".
//
//   runtime rt;
//   evaluator ev(rt);
//   auto v = ev.eval(*hall_reader::read_one(rt, "(+ 1 2)"));
//   if (v) v->print();  // "3"

#pragma region eval_error

// Description of a failed evaluation.
using eval_error = error_value<struct EvalTag>;

#pragma endregion
#pragma region evaluator

// Evaluator for the kernel s-expression language.
//
// Atoms evaluate to themselves; a symbol evaluates to its binding in the
// lexical environment; a list is a special form or, failing that, a function
// call.
//
// The special forms are `quote`, `if`, `define`, `lambda`, and `begin`;
// they are recognized by symbol identity in call position, are not values,
// and cannot be rebound.
//
// The global scope is the runtime's persistent root environment, so while an
// evaluator is transient, definitions made through one evaluator are visible
// to every later evaluator over the same runtime.
//
// The evaluation loop preserves proper tail calls: `if` branches, `begin`
// and lambda-body finales, and closure calls re-enter the loop instead of
// recursing in C++, so tail recursion (including mutual recursion) runs in
// constant stack. Only evaluation nested inside an expression recurses,
// guarded by `max_depth`.
//
// The outermost loop top is also the garbage-collection safe point (see
// `runtime::maybe_collect`), so values the embedder holds across `eval`
// calls must be pinned with a `gc_pin`.
//
// Failure is reported by value as an `eval_error`.
class evaluator final {
public:
  template<typename T>
  using result = value_or_error<T, eval_error>;

  // Maximum nested evaluation depth.
  //
  // Tail calls consume no depth because iteration written as tail recursion is
  // unbounded. What consumes depth is evaluation nested inside an
  // expression: arguments, an `if` condition, and every level of a non-tail
  // recursion such as a factorial that multiplies after the recursive call
  // returns. Each nested level is a C++ recursion, so the guard turns C++
  // stack exhaustion into an `eval_error`.
  //
  // The limit matches the reader's and is sized so the guard fires before the
  // real C++ stack runs out. This is the case even in unoptimized builds,
  // whose frames are several times fatter, on the smallest common default
  // stack (1MB on Windows).
  static constexpr size_t max_depth = 256;

  // Bind an evaluator to `rt`, adopting the runtime's root environment as
  // the global scope.
  //
  // Construction stocks the root with kernel primitives.
  explicit evaluator(runtime& rt)
      : rt_{rt}, global_{rt.root_env()}, quote_{rt.intern("quote")},
        if_{rt.intern("if")}, define_{rt.intern("define")},
        lambda_{rt.intern("lambda")}, begin_{rt.intern("begin")} {
    register_builtins();
  }

  evaluator(const evaluator&) = delete;
  evaluator& operator=(const evaluator&) = delete;

  // The global scope: the runtime's root environment, where top-level
  // `define` binds.
  [[nodiscard]] environment& global() noexcept { return global_; }

  // Evaluate `expr` in the global environment.
  [[nodiscard]] result<value> eval(value expr) { return eval(expr, global_); }

  // Evaluate `expr` in `env`.
  [[nodiscard]] result<value> eval(value expr, environment& env) {
    scoped_value guard(depth_, depth_ + 1);
    if (depth_ >= max_depth) return eval_error{"evaluation too deep"};

    // Each pass handles one expression. A step that hands back a tail
    // expression re-enters the loop instead of recursing in C++; that is the
    // entire tail-call mechanism.
    auto* cur = &env;
    for (;;) {
      // The outermost loop top is the collection safe point: the evaluator's
      // entire live set here is `expr` and the current environment.
      if (depth_ == 1) rt_.maybe_collect(expr, *cur);

      // Symbols are looked up in the environment chain, while atoms evaluate
      // to themselves. Note that a symbol that refers to a primitive or
      // closure is going to be found initially as the head of a cell, not
      // simply looked up here.
      if (const auto name = expr.maybe_symbol())
        return eval_symbol(*name, *cur);
      if (!expr.is_cell()) return expr;

      // A cell is a special form, like "(if p x)", or a call, like "(+ 1 2)".
      auto next = eval_cell(expr, cur);
      if (!next) return next;

      // If it was the final value, return it. If it was a tail expression,
      // loop to evaluate it, without recursing.
      if (const auto done = next->maybe_evaluated()) return *done;
      expr = next->as_tail_expr();
    }
  }

private:
#pragma region Evaluation

  // The outcome of dispatching one form: either a finished evaluation or a
  // tail expression, which is the next expression to evaluate in tail
  // position.
  class step final {
  public:
    // Build a step over the fully-evaluated `v`.
    [[nodiscard]] static step make_evaluated(value v) noexcept {
      return step{v, false};
    }

    // Build a step over `expr`, to be evaluated next in tail position.
    [[nodiscard]] static step make_tail_expr(value expr) noexcept {
      return step{expr, true};
    }

    // Access the held state, asserting it.
    [[nodiscard]] value as_evaluated() const {
      assert(!tail_);
      return v_;
    }
    [[nodiscard]] value as_tail_expr() const {
      assert(tail_);
      return v_;
    }

    // The evaluated result, or empty if this step is a tail expression.
    [[nodiscard]] optional_ptr<const value*> maybe_evaluated() const noexcept {
      return tail_ ? nullptr : &v_;
    }

    // The tail expression, or empty if this step is evaluated.
    [[nodiscard]] optional_ptr<const value*> maybe_tail_expr() const noexcept {
      return tail_ ? &v_ : nullptr;
    }

  private:
    step(value v, bool tail) noexcept : v_{v}, tail_{tail} {}

    value v_;
    bool tail_;
  };

  // Wrap a finished evaluation as a step.
  //
  // Maps a `value` `result` to a `step` result.
  [[nodiscard]] static result<step> finish_step(result<value> r) {
    if (!r) return std::move(r);
    return step::make_evaluated(*r);
  }

  // Evaluate a symbol by looking up its binding.
  [[nodiscard]] static result<value>
  eval_symbol(symbol name, const environment& env) {
    if (const auto found = env.lookup(name)) return *found;
    return eval_error{"unbound symbol: " + name.name()};
  }

  // Evaluate a cell, `(op . args)`: a special form or an ordinary call.
  //
  // `env` is rebound when a closure call installs its frame.
  [[nodiscard]] result<step> eval_cell(value expr, environment*& env) {
    // Flatten the cell list into a vector of arguments.
    auto args = std::vector<value>{};
    if (!append_elements(args, expr.tail()))
      return eval_error{"improper form: " + expr.print()};

    // If it's a special symbol, dispatch to its evaluation rule; otherwise,
    // it's a call.
    const auto op = expr.head();
    if (const auto form = op.maybe_symbol(); form && is_special(*form))
      return eval_special(*form, args, *env);

    return eval_call(op, std::move(args), env);
  }

  // Evaluate a special form, each with its own evaluation rule.
  [[nodiscard]] result<step>
  eval_special(symbol form, std::span<const value> args, environment& env) {
    if (form == quote_) {
      if (args.size() != 1) return eval_error{"quote: expects 1 argument"};
      // Ironically, it's not actually evaluated, which is the whole point.
      return step::make_evaluated(args[0]);
    }
    if (form == if_) {
      if (args.size() < 2 || args.size() > 3)
        return eval_error{"if: expects 2 or 3 arguments"};
      auto cond = eval(args[0], env);
      if (!cond) return cond;
      if (cond->is_truthy()) return step::make_tail_expr(args[1]);
      if (args.size() == 3) return step::make_tail_expr(args[2]);
      return step::make_evaluated(value{});
    }
    if (form == define_) return finish_step(eval_define(args, env));
    if (form == lambda_) return finish_step(eval_lambda(args, env));
    assert(form == begin_);
    if (args.empty()) return step::make_evaluated(value{});
    auto last = eval_leading(args, env);
    if (!last) return last;
    return step::make_tail_expr(*last);
  }

  // Evaluate an ordinary call: the operator, then the arguments, left to
  // right.
  //
  // A primitive is invoked here; a closure call binds its frame, rebinds
  // `env`, and hands back its body's finale as the tail expression.
  [[nodiscard]] result<step>
  eval_call(value op, std::vector<value> args, environment*& env) {
    // Look up the operator symbol to get the value associated with it, which
    // may be a primitive or a closure. Note that this is the `op` on its own,
    // not as part of a cell: we have gone deeper.
    auto callee = eval(op, *env);
    if (!callee) return callee;

    // Eval each arg, replacing it with its value.
    for (auto& arg : args) {
      auto r = eval(arg, *env);
      if (!r) return r;
      arg = *r;
    }
    if (const auto prim = callee->maybe_primitive())
      return finish_step(apply_primitive(*prim, args));

    const auto fun = callee->maybe_closure();
    if (!fun) return eval_error{"not callable: " + callee->print()};

    auto frame = bind_frame(*fun, args);
    if (!frame) return frame;

    auto last = eval_leading(fun->body, **frame);
    if (!last) return last;

    env = *frame;
    return step::make_tail_expr(*last);
  }

  // Append a proper list's elements to `out`, returning false if the list is
  // improper.
  [[nodiscard]] static bool
  append_elements(std::vector<value>& out, value list) {
    for (; list.is_cell(); list = list.tail()) out.push_back(list.head());
    return list.is_nil();
  }

  // Evaluate every expression but the last, returning the last unevaluated
  // so the caller can treat it as a tail position.
  [[nodiscard]] result<value>
  eval_leading(std::span<const value> exprs, environment& env) {
    assert(!exprs.empty());
    for (size_t ndx = 0; ndx + 1 < exprs.size(); ++ndx)
      if (auto r = eval(exprs[ndx], env); !r) return r;
    return exprs.back();
  }

  // Evaluate a `(define name expr)` form: bind `name` in the current scope
  // to the evaluated expression, and yield the name, which the REPL then
  // echoes as confirmation.
  //
  // Definition is where reserved names are policed: `%` names belong to the
  // kernel, and special-form names cannot be rebound.
  [[nodiscard]] result<value>
  eval_define(std::span<const value> args, environment& env) {
    if (args.size() != 2)
      return eval_error{"define: expects a name and a value"};
    const auto name = args[0].maybe_symbol();
    if (!name)
      return eval_error{"define: expects a symbol, got: " + args[0].print()};
    if (auto objection = check_bindable(*name))
      return eval_error{std::move(*objection)};
    auto init = eval(args[1], env);
    if (!init) return init;
    env.bind(*name, *init);
    return args[0];
  }

  // Evaluate a `(lambda (params...) body...)` form into a closure capturing
  // `env`.
  [[nodiscard]] result<value>
  eval_lambda(std::span<const value> args, environment& env) {
    if (args.size() < 2)
      return eval_error{"lambda: expects a parameter list and a body"};
    std::vector<symbol> params;
    if (auto objection = parse_params(args[0], params))
      return eval_error{std::move(*objection)};
    return rt_.make_closure(std::move(params), {args.begin() + 1, args.end()},
        env);
  }

  // Parse a parameter list into symbols, returning the objection if it is
  // not a proper list of unique, bindable symbols.
  //
  // A dotted tail or a bare symbol in place of the list is the classic
  // spelling for variadic parameters, which are planned but not yet
  // supported; the dedicated message reserves the spelling.
  [[nodiscard]] std::optional<std::string>
  parse_params(value list, std::vector<symbol>& out) const {
    for (; list.is_cell(); list = list.tail()) {
      const auto param = list.head();
      const auto name = param.maybe_symbol();
      if (!name) return "lambda: parameter is not a symbol: " + param.print();
      if (auto objection = check_bindable(*name)) return objection;
      if (find_opt(out, *name))
        return "lambda: duplicate parameter: " + name->name();
      out.push_back(*name);
    }
    if (list.is_nil()) return std::nullopt;
    if (list.is_symbol())
      return "lambda: variadic parameters are not yet supported";
    return "lambda: malformed parameter list";
  }

  // Whether `name` may be bound, returning the objection if not.
  [[nodiscard]] std::optional<std::string> check_bindable(symbol name) const {
    if (name.name().starts_with('%'))
      return "'%' names are reserved for the kernel: " + name.name();
    if (is_special(name)) return "cannot rebind special form: " + name.name();
    return std::nullopt;
  }

  // Whether `name` names a special form.
  [[nodiscard]] bool is_special(symbol name) const noexcept {
    return name == quote_ || name == if_ || name == define_ ||
           name == lambda_ || name == begin_;
  }

  // Bind a call frame for `fun` over `args`, scoped inside the closure's
  // captured environment.
  [[nodiscard]] result<environment*>
  bind_frame(const closure& fun, std::span<const value> args) {
    if (args.size() != fun.params.size())
      return eval_error{
          "lambda: expects " + std::to_string(fun.params.size()) +
          " arguments, got " + std::to_string(args.size())};
    auto& frame = rt_.make_env(*fun.env);
    for (size_t ndx = 0; ndx < args.size(); ++ndx)
      frame.bind(fun.params[ndx], args[ndx]);
    return &frame;
  }

  // Invoke `prim` on evaluated `args`, prefixing any error with its name.
  [[nodiscard]] result<value>
  apply_primitive(const primitive& prim, std::span<const value> args) {
    auto r = prim.fn(rt_, args);
    if (!r) return eval_error{prim.name.name() + ": " + r.as_error()};
    return *r;
  }

#pragma endregion
#pragma region Builtins

  using prim_result = value_or_error<value, std::string>;

  // Build a primitive failure over a non-numeric operand.
  [[nodiscard]] static std::string not_numeric(value v) {
    return "expects numbers, got: " + v.print();
  }

  // Checked int64 addition, subtraction, and multiplication; empty on
  // overflow. Implemented over uint64, whose wraparound is well-defined.
  [[nodiscard]] static std::optional<int64_t>
  checked_add(int64_t a, int64_t b) noexcept {
    const auto sum = static_cast<int64_t>(
        static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
    if ((a < 0) == (b < 0) && (sum < 0) != (a < 0)) return std::nullopt;
    return sum;
  }
  [[nodiscard]] static std::optional<int64_t>
  checked_sub(int64_t a, int64_t b) noexcept {
    const auto diff = static_cast<int64_t>(
        static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
    if ((a < 0) != (b < 0) && (diff < 0) != (a < 0)) return std::nullopt;
    return diff;
  }
  [[nodiscard]] static std::optional<int64_t>
  checked_mul(int64_t a, int64_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    // With -1 handled up front, the division in the final check cannot
    // itself overflow.
    constexpr auto min = std::numeric_limits<int64_t>::min();
    if (a == -1) {
      if (b == min) return std::nullopt;
      return -b;
    }
    if (b == -1) {
      if (a == min) return std::nullopt;
      return -a;
    }
    const auto prod = static_cast<int64_t>(
        static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
    if (prod / a != b) return std::nullopt;
    return prod;
  }

  // A kernel number: an exact `int64_t` or a `double`.
  //
  // Arithmetic stays exact while it can and switches to floating point when a
  // float operand appears or integer math overflows. This is the same fallback
  // the reader applies to oversized integer literals.
  //
  // Which representation a number holds is decided by which constructor built
  // it, and only the members see the representation.
  class number final {
  public:
    // Default is exact zero. An integral value is exact, widened to `int64_t`;
    // a floating value is inexact, widened to `double`.
    constexpr number() noexcept = default;
    template<std::integral N>
    constexpr number(N n) noexcept : i_{n} {}
    template<std::floating_point F>
    constexpr number(F d) noexcept : d_{d}, exact_{false} {}

    // The number `v` holds, or empty if `v` is not numeric.
    [[nodiscard]] static std::optional<number> from(value v) noexcept {
      if (const auto n = v.maybe_int()) return number{*n};
      if (const auto d = v.maybe_float()) return number{*d};
      return std::nullopt;
    }

    [[nodiscard]] value as_value() const noexcept {
      return exact_ ? value{i_} : value{d_};
    }
    [[nodiscard]] double as_double() const noexcept {
      return exact_ ? static_cast<double>(i_) : d_;
    }

    // Arithmetic in the numeric tower.
    [[nodiscard]] number add(number b) const noexcept {
      if (exact_ && b.exact_)
        if (const auto n = checked_add(i_, b.i_)) return number{*n};
      return number{as_double() + b.as_double()};
    }
    [[nodiscard]] number sub(number b) const noexcept {
      if (exact_ && b.exact_)
        if (const auto n = checked_sub(i_, b.i_)) return number{*n};
      return number{as_double() - b.as_double()};
    }
    [[nodiscard]] number mul(number b) const noexcept {
      if (exact_ && b.exact_)
        if (const auto n = checked_mul(i_, b.i_)) return number{*n};
      return number{as_double() * b.as_double()};
    }

    // Divide in the numeric tower; empty on an exact zero divisor.
    //
    // An exact quotient stays exact only when the division is even; a
    // remainder falls to double, there being no ratio type. A float zero
    // divisor is not an error: IEEE yields an infinity or NaN.
    [[nodiscard]] std::optional<number> div(number b) const noexcept {
      if (b.exact_ && b.i_ == 0) return std::nullopt;
      if (exact_ && b.exact_) {
        // min / -1 overflows (and min % -1 is UB); checked negation covers
        // both.
        if (b.i_ == -1) {
          if (const auto n = checked_sub(0, i_)) return number{*n};
        } else if (i_ % b.i_ == 0) {
          return number{i_ / b.i_};
        }
      }
      return number{as_double() / b.as_double()};
    }

    // Compare, exactly when both sides are exact.
    //
    // A mixed pair promotes the int to double, which is approximate past 2^53.
    // NaN compares unordered, so every comparison against it is false except
    // `!=`.
    [[nodiscard]] std::partial_ordering compare(number b) const noexcept {
      if (exact_ && b.exact_) return i_ <=> b.i_;
      return as_double() <=> b.as_double();
    }

  private:
    int64_t i_{};
    double d_{};
    bool exact_ = true;
  };

  // Compare two numeric values.
  [[nodiscard]] static value_or_error<std::partial_ordering, std::string>
  compare_nums(value a, value b) {
    const auto na = number::from(a);
    if (!na) return not_numeric(a);
    const auto nb = number::from(b);
    if (!nb) return not_numeric(b);
    return na->compare(*nb);
  }

  // Chain a comparison across adjacent argument pairs: `(< a b c)` is true
  // when a < b and b < c.
  template<typename Pred>
  static prim_result chain_compare(std::span<const value> args, Pred keep) {
    if (args.size() < 2) return "expects at least 2 arguments"s;
    for (size_t ndx = 0; ndx + 1 < args.size(); ++ndx) {
      auto ord = compare_nums(args[ndx], args[ndx + 1]);
      if (!ord) return ord;
      if (!keep(*ord)) return value{false};
    }
    return value{true};
  }

  // The `+` builtin: n-ary addition; no arguments yield 0.
  static prim_result prim_add(runtime&, std::span<const value> args) {
    auto acc = number{};
    for (const auto& arg : args) {
      const auto n = number::from(arg);
      if (!n) return not_numeric(arg);
      acc = acc.add(*n);
    }
    return acc.as_value();
  }

  // The `-` builtin: subtraction folding left; one argument negates.
  static prim_result prim_sub(runtime&, std::span<const value> args) {
    if (args.empty()) return "expects at least 1 argument"s;
    const auto first = number::from(args[0]);
    if (!first) return not_numeric(args[0]);
    if (args.size() == 1) return number{}.sub(*first).as_value();
    auto acc = *first;
    for (const auto& arg : args.subspan(1)) {
      const auto n = number::from(arg);
      if (!n) return not_numeric(arg);
      acc = acc.sub(*n);
    }
    return acc.as_value();
  }

  // The `*` builtin: n-ary multiplication; no arguments yield 1.
  static prim_result prim_mul(runtime&, std::span<const value> args) {
    auto acc = number{1};
    for (const auto& arg : args) {
      const auto n = number::from(arg);
      if (!n) return not_numeric(arg);
      acc = acc.mul(*n);
    }
    return acc.as_value();
  }

  // The `/` builtin: division folding left; one argument divides 1 by it.
  static prim_result prim_div(runtime&, std::span<const value> args) {
    if (args.empty()) return "expects at least 1 argument"s;
    const auto first = number::from(args[0]);
    if (!first) return not_numeric(args[0]);
    auto acc = *first;
    if (args.size() == 1) {
      const auto r = number{1}.div(acc);
      if (!r) return "division by zero"s;
      return r->as_value();
    }
    for (const auto& arg : args.subspan(1)) {
      const auto n = number::from(arg);
      if (!n) return not_numeric(arg);
      const auto r = acc.div(*n);
      if (!r) return "division by zero"s;
      acc = *r;
    }
    return acc.as_value();
  }

  // The numeric comparison builtins, chained across adjacent pairs.
  static prim_result prim_eq(runtime&, std::span<const value> args) {
    return chain_compare(args, [](std::partial_ordering ord) {
      return std::is_eq(ord);
    });
  }
  static prim_result prim_lt(runtime&, std::span<const value> args) {
    return chain_compare(args, [](std::partial_ordering ord) {
      return std::is_lt(ord);
    });
  }
  static prim_result prim_le(runtime&, std::span<const value> args) {
    return chain_compare(args, [](std::partial_ordering ord) {
      return std::is_lteq(ord);
    });
  }
  static prim_result prim_gt(runtime&, std::span<const value> args) {
    return chain_compare(args, [](std::partial_ordering ord) {
      return std::is_gt(ord);
    });
  }
  static prim_result prim_ge(runtime&, std::span<const value> args) {
    return chain_compare(args, [](std::partial_ordering ord) {
      return std::is_gteq(ord);
    });
  }

  // The `!=` builtin. Unlike the other comparisons it takes exactly 2
  // arguments, because chaining adjacent inequality is a trap: `(!= 1 2 1)`
  // would be true.
  static prim_result prim_ne(runtime&, std::span<const value> args) {
    if (args.size() != 2) return "expects 2 arguments"s;
    auto ord = compare_nums(args[0], args[1]);
    if (!ord) return ord;
    return value{!std::is_eq(*ord)};
  }

  // The `cons` builtin: construct a cell.
  static prim_result prim_cons(runtime& rt, std::span<const value> args) {
    if (args.size() != 2) return "expects 2 arguments"s;
    return rt.cons(args[0], args[1]);
  }

  // The `list` builtin: construct a list of the arguments, so `(list)` is
  // nil.
  static prim_result prim_list(runtime& rt, std::span<const value> args) {
    value list;
    for (const auto& arg : std::views::reverse(args))
      list = rt.cons(arg, list);
    return list;
  }

  // The `head` and `tail` builtins: the halves of a cell.
  static prim_result prim_head(runtime&, std::span<const value> args) {
    if (args.size() != 1) return "expects 1 argument"s;
    const auto c = args[0].maybe_cell();
    if (!c) return "expects a cell, got: " + args[0].print();
    return c->head;
  }
  static prim_result prim_tail(runtime&, std::span<const value> args) {
    if (args.size() != 1) return "expects 1 argument"s;
    const auto c = args[0].maybe_cell();
    if (!c) return "expects a cell, got: " + args[0].print();
    return c->tail;
  }

  // The `nil?` builtin: whether the argument is nil, and so also whether it
  // is the empty list.
  static prim_result prim_nil(runtime&, std::span<const value> args) {
    if (args.size() != 1) return "expects 1 argument"s;
    return value{args[0].is_nil()};
  }

  // Bind the kernel's primitive functions into the global environment,
  // skipping any name already bound.
  void register_builtins() {
    if (global_.lookup(rt_.intern("+"))) return;

    register_builtin("+", prim_add);
    register_builtin("-", prim_sub);
    register_builtin("*", prim_mul);
    register_builtin("/", prim_div);
    register_builtin("==", prim_eq);
    register_builtin("!=", prim_ne);
    register_builtin("<", prim_lt);
    register_builtin("<=", prim_le);
    register_builtin(">", prim_gt);
    register_builtin(">=", prim_ge);
    register_builtin("cons", prim_cons);
    register_builtin("list", prim_list);
    register_builtin("head", prim_head);
    register_builtin("tail", prim_tail);
    register_builtin("nil?", prim_nil);
  }

  void register_builtin(std::string_view name, primitive::fn_t fn) {
    const auto s = rt_.intern(name);
    if (global_.lookup(s)) return;
    global_.bind(s, rt_.make_primitive(s, fn));
  }

#pragma endregion
#pragma region Data members

  runtime& rt_;
  environment& global_;
  symbol quote_;
  symbol if_;
  symbol define_;
  symbol lambda_;
  symbol begin_;
  size_t depth_{};

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::lang::coreb
