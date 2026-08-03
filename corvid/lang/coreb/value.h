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
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "../../containers/core/enum_variant.h"
#include "../../containers/core/opt_find.h"
#include "../../containers/core/transparent.h"
#include "../../enums/sequence_enum.h"
#include "../../meta/concepts.h"
#include "../../strings/cases.h"
#include "../../strings/conversion.h"

namespace corvid { inline namespace lang { namespace coreb {

// CoreB kernel value model.
//
// CoreB is a Lisp-1 whose code and data share one representation: the
// s-expression.
//
// This header defines that representation for the C++ kernel, which consists
// of:
//
// A small copyable `value` holds one of: nil, boolean, integer, float,
// interned symbol, string, cons cell, closure, or primitive function.
//
// A lexical `environment` maps symbols to values, chained to its enclosing
// scope.
//
// A `runtime` owns the symbol table and the heap the aggregate values and
// environments live in.
//
//   runtime rt;
//   auto v = rt.cons(value{rt.intern("x")}, rt.cons(value{42}, value{}));
//   v.print();  // "(x 42)"

#pragma region symbol

// Interned symbol.
//
// Each distinct spelling interned in a given `runtime` yields one unique
// symbol, so equality is pointer identity: comparing symbols never compares
// text.
class symbol final {
public:
  [[nodiscard]] const std::string& name() const noexcept { return *name_; }
  [[nodiscard]] bool operator==(const symbol&) const noexcept = default;

private:
  friend class runtime;

  explicit symbol(const std::string& name) noexcept : name_{&name} {}

  const std::string* name_;
};

// Hash a symbol by identity.
//
// Interning makes the spelling's address the symbol's identity, so the hash
// is the pointer's, never the text's.
struct symbol_hash {
  [[nodiscard]] size_t operator()(const symbol& s) const noexcept {
    return std::hash<const std::string*>{}(&s.name());
  }
};

#pragma endregion
#pragma region value

// Fwd.
struct cell;
struct heap_string;
struct closure;
struct primitive;
class environment;

// Discriminator for the alternatives a `value` can hold.
enum class kind : std::uint8_t {
  nil,
  boolean,
  integer,
  floating,
  symbol,
  string,
  cell,
  closure,
  primitive
};
consteval auto corvid_enum_spec(kind*) {
  return corvid::enums::sequence::make_sequence_enum_spec<kind,
      "nil,boolean,integer,floating,symbol,string,cell,closure,primitive">();
}

// CoreB value: nil, a boolean, an integer, a float, a symbol, or a handle to
// a heap-allocated string, cons cell, or function (closure or primitive).
//
// A `value` is small and cheap. Copying is always shallow and never touches
// heap data. The owning `runtime` must outlive every `value` handed out from
// it.
//
// Nil doubles as the empty list, in the classic Lisp tradition: a proper list
// chains cons cells through `tail` and terminates at nil. The standalone
// printed form is "nil"; the reader accepts both "nil" and "()".
//
// The `as_*` accessors have narrow contracts: the value must hold the
// requested alternative.
class value final {
  using variant_t = enum_variant<kind, std::monostate, bool, int64_t, double,
      symbol, heap_string*, cell*, closure*, primitive*>;

public:
#pragma region Construction

  // A default-constructed value is nil.
  constexpr value() noexcept = default;

  // Implicit construction from each alternative.
  value(bool b) noexcept : v_{b} {}
  value(Integer auto n) noexcept : v_{static_cast<int64_t>(n)} {}
  value(double d) noexcept : v_{d} {}
  value(symbol s) noexcept : v_{s} {}
  value(heap_string& s) noexcept : v_{&s} {}
  value(cell& c) noexcept : v_{&c} {}
  value(closure& c) noexcept : v_{&c} {}
  value(primitive& p) noexcept : v_{&p} {}

  // Strings are made through `runtime::make_string` and symbols through
  // `runtime::intern`.
  value(const char*) = delete;

#pragma endregion
#pragma region Classification

  [[nodiscard]] kind type() const noexcept { return v_.index(); }

  [[nodiscard]] bool is_nil() const noexcept { return type() == kind::nil; }
  [[nodiscard]] bool is_bool() const noexcept {
    return type() == kind::boolean;
  }
  [[nodiscard]] bool is_int() const noexcept {
    return type() == kind::integer;
  }
  [[nodiscard]] bool is_float() const noexcept {
    return type() == kind::floating;
  }
  [[nodiscard]] bool is_symbol() const noexcept {
    return type() == kind::symbol;
  }
  [[nodiscard]] bool is_string() const noexcept {
    return type() == kind::string;
  }
  [[nodiscard]] bool is_cell() const noexcept { return type() == kind::cell; }
  [[nodiscard]] bool is_closure() const noexcept {
    return type() == kind::closure;
  }
  [[nodiscard]] bool is_primitive() const noexcept {
    return type() == kind::primitive;
  }

  // Whether this is something other than a cons cell.
  [[nodiscard]] bool is_atom() const noexcept { return !is_cell(); }

  // Clojure-style truthiness: nil and false are falsy; everything else,
  // including zero and the empty string, is truthy.
  //
  // The `is_bool` guard makes `as_bool`'s throw path dead.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  [[nodiscard]] bool is_truthy() const noexcept {
    return !is_nil() && (!is_bool() || as_bool());
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool as_bool() const {
    assert(is_bool());
    return v_.get<kind::boolean>();
  }
  [[nodiscard]] int64_t as_int() const {
    assert(is_int());
    return v_.get<kind::integer>();
  }
  [[nodiscard]] double as_float() const {
    assert(is_float());
    return v_.get<kind::floating>();
  }
  [[nodiscard]] symbol as_symbol() const {
    assert(is_symbol());
    return v_.get<kind::symbol>();
  }
  [[nodiscard]] const std::string& as_string() const;
  [[nodiscard]] cell& as_cell() const {
    assert(is_cell());
    return *v_.get<kind::cell>();
  }
  [[nodiscard]] closure& as_closure() const {
    assert(is_closure());
    return *v_.get<kind::closure>();
  }
  [[nodiscard]] primitive& as_primitive() const {
    assert(is_primitive());
    return *v_.get<kind::primitive>();
  }

  // The halves of a cons cell.
  //
  // Precondition: `is_cell`.
  [[nodiscard]] value head() const;
  [[nodiscard]] value tail() const;

#pragma endregion
#pragma region Printing

  // Append the printed s-expression form to `out`.
  //
  // The output is what the reader accepts: symbols bare, strings quoted and
  // escaped, proper lists as "(a b c)", improper ones dotted. Function values
  // are the exception: they have no readable form and print as the display
  // forms "#<lambda>" and "#<primitive name>". Printing cyclic data (possible
  // only after mutation) is unsupported and will not terminate.
  bool append(std::string& out) const;

  // Return the printed s-expression form.
  [[nodiscard]] std::string print() const {
    std::string out;
    append(out);
    return out;
  }

  // Append the structural debug form to `out`.
  //
  // Where `append` abbreviates chains of cells into list notation, this shows
  // the raw pair structure in fully dotted form: every cell prints as "(head .
  // tail)", so "(a b)" dumps as "(a . (b . nil))". Atoms print as in `append`.
  //
  // The output remains valid reader syntax, but re-reading it is subject to
  // `reader::max_depth`: the dotted form spends one nesting level per list
  // element where the abbreviated form spends none, so a long proper list
  // dumps fine yet will not read back.
  bool append_dump(std::string& out) const;

  // Return the structural debug form.
  [[nodiscard]] std::string dump() const {
    std::string out;
    append_dump(out);
    return out;
  }

#pragma endregion

private:
#pragma region Helpers

  // Append a float so it re-reads as a float.
  //
  // A rendering with no exponent or decimal point (`to_chars` prints 1.0 as
  // "1") gets ".0" appended. Infinities and NaN print as "inf"/"nan", which
  // the reader does not accept as numbers; a round-trip for those is deferred
  // until the language decides how to spell them.
  static void do_append_float(std::string& out, double d) {
    const auto start = out.size();
    strings::append_num(out, d);
    const auto text = std::string_view{out}.substr(start);
    auto integral_form = true;
    for (const char c : text)
      if (!strings::is_digit(c) && c != '-') {
        integral_form = false;
        break;
      }
    if (integral_form) out += ".0";
  }

#pragma endregion
#pragma region Data members

  variant_t v_;

#pragma endregion
};

#pragma endregion
#pragma region Heap objects

// Cons cell: the two-value node from which all CoreB structure is constructed.
//
// If you like the IBM 704, you can call this a `pair` of `car` and `cdr`. But,
// also, if you like the IBM 704, then something is wrong with you. Get help.
//
// A proper list chains through `tail` and terminates at nil. In other words,
// the `tail` always contains either `nil` or another `cell`. Any other `tail`
// makes the sequence improper, so it's printed with a dot: "(a b . c)".
//
// Constructed only by the `runtime`, which owns every cell.
struct cell final {
private:
  enum class allow : bool { ctor };
  friend class runtime;

public:
  cell(allow, value head, value tail) noexcept : head{head}, tail{tail} {}

  cell(const cell&) = delete;
  cell& operator=(const cell&) = delete;

  // Append the printed list form to `out`: proper lists as "(a b c)",
  // improper tails dotted.
  bool append(std::string& out) const {
    out += '(';
    for (const auto* cur = this;;) {
      cur->head.append(out);
      const auto& rest = cur->tail;
      if (rest.is_nil()) break;
      if (!rest.is_cell()) {
        out += " . ";
        rest.append(out);
        break;
      }
      out += ' ';
      cur = &rest.as_cell();
    }
    out += ')';
    return true;
  }

  // Append the structural debug form to `out`: "(head . tail)", recursing
  // into both halves, with no list abbreviation.
  bool append_dump(std::string& out) const {
    out += '(';
    head.append_dump(out);
    out += " . ";
    tail.append_dump(out);
    out += ')';
    return true;
  }

  value head;
  value tail;
};

// Heap-allocated string payload.
//
// Constructed only by the `runtime`, which owns it.
struct heap_string final {
private:
  enum class allow : bool { ctor };
  friend class runtime;

public:
  heap_string(allow, std::string str) noexcept : str{std::move(str)} {}

  heap_string(const heap_string&) = delete;
  heap_string& operator=(const heap_string&) = delete;

  // Append the quoted form to `out`, escaping exactly the set the reader
  // unescapes.
  bool append(std::string& out) const {
    out.reserve(out.size() + 2 + str.size());
    out += '"';
    strings::append_escaped(out, str);
    out += '"';
    return true;
  }

  std::string str;
};

// Closure: the function value a `lambda` evaluates to.
//
// Bundles the parameter names, the body (a sequence of expressions evaluated
// in order, the last in tail position), and the environment captured where
// the `lambda` was evaluated.
//
// That captured environment is what makes it a closure: the body sees the
// variables of its birthplace even when called from somewhere else entirely.
//
// Constructed only by the `runtime`, which owns it.
struct closure final {
private:
  enum class allow : bool { ctor };
  friend class runtime;

public:
  closure(allow, std::vector<symbol> params, std::vector<value> body,
      environment& env) noexcept
      : params{std::move(params)}, body{std::move(body)}, env{&env} {}

  closure(const closure&) = delete;
  closure& operator=(const closure&) = delete;

  std::vector<symbol> params;
  std::vector<value> body;
  environment* env;
};

// Primitive: a kernel C++ function exposed as a CoreB function value.
//
// The function receives the evaluated arguments and returns a value or an
// error message, which the evaluator prefixes with the primitive's name.
//
// Constructed only by the `runtime`, which owns it.
struct primitive final {
private:
  enum class allow : bool { ctor };
  friend class runtime;

public:
  using fn_t = std::expected<value, std::string> (*)(runtime&,
      std::span<const value>);

  primitive(allow, symbol name, fn_t fn) noexcept : name{name}, fn{fn} {}

  primitive(const primitive&) = delete;
  primitive& operator=(const primitive&) = delete;

  // Append the display form, "#<primitive name>". Primitives have no
  // readable form.
  bool append(std::string& out) const {
    out += "#<primitive ";
    out += name.name();
    out += '>';
    return true;
  }

  symbol name;
  fn_t fn;
};

#pragma endregion
#pragma region environment

// Lexical environment: one scope's bindings from symbols to values, chained
// to the enclosing scope.
//
// Lookup searches this scope first, then the chain outward; the global scope
// ends the chain with a null parent. Environments are runtime-owned heap
// objects rather than stack frames because a closure captures its defining
// environment, which must then outlive the call that created it.
//
// Constructed only by the `runtime`, which owns it.
class environment final {
private:
  enum class allow : bool { ctor };
  friend class runtime;

public:
  environment(allow, environment* parent) : parent_{parent} {}

  environment(const environment&) = delete;
  environment& operator=(const environment&) = delete;

  // Look up `name`, searching enclosing scopes outward.
  [[nodiscard]] std::optional<value> lookup(symbol name) const noexcept {
    for (const auto* env = this; env; env = env->parent_)
      if (const auto found = find_opt(env->vars_, name)) return *found;
    return std::nullopt;
  }

  // Bind `name` in this scope, replacing any existing binding here but
  // leaving enclosing scopes untouched.
  void bind(symbol name, value val) { vars_.insert_or_assign(name, val); }

private:
#pragma region Data members

  environment* parent_;
  std::unordered_map<symbol, value, symbol_hash> vars_;

#pragma endregion
};

#pragma endregion
#pragma region runtime

// The CoreB kernel runtime, containing the symbol table and the heap.
//
// All aggregate values (cons cells, strings, closures, primitives) and all
// environments are allocated here, and every `value` handle is only valid
// while its runtime is alive.
//
// Storage is currently freed only on destruction; milestone 3 replaces that
// with mark-and-sweep collection, which is why ownership is centralized from
// the start.
class runtime final {
public:
  // A fresh runtime owns one environment from the start: the empty root
  // scope.
  runtime() {
    envs_.push_back(
        std::make_unique<environment>(environment::allow::ctor, nullptr));
  }

  // Intern `name`, returning its unique symbol.
  //
  // Repeated calls with the same spelling return the same symbol.
  [[nodiscard]] symbol intern(std::string_view name) {
    if (const auto found = find_opt(syms_, name)) return symbol{*found};
    return symbol{*syms_.emplace(name).first};
  }

  // Construct a cell.
  [[nodiscard]] value cons(value head, value tail) {
    cells_.push_back(std::make_unique<cell>(cell::allow::ctor, head, tail));
    return value{*cells_.back()};
  }

  // Allocate a string.
  [[nodiscard]] heap_string& make_string(std::string str) {
    strings_.push_back(std::make_unique<heap_string>(heap_string::allow::ctor,
        std::move(str)));
    return *strings_.back();
  }

  // Construct a closure over `env`.
  [[nodiscard]] value make_closure(std::vector<symbol> params,
      std::vector<value> body, environment& env) {
    closures_.push_back(std::make_unique<closure>(closure::allow::ctor,
        std::move(params), std::move(body), env));
    return value{*closures_.back()};
  }

  // Expose a C++ function as a function value named `name`.
  [[nodiscard]] value make_primitive(symbol name, primitive::fn_t fn) {
    primitives_.push_back(
        std::make_unique<primitive>(primitive::allow::ctor, name, fn));
    return value{*primitives_.back()};
  }

  // Construct an environment scoped inside `parent`.
  [[nodiscard]] environment& make_env(environment& parent) {
    envs_.push_back(
        std::make_unique<environment>(environment::allow::ctor, &parent));
    return *envs_.back();
  }

  // The root environment, the ancestor of every other scope.
  //
  // This is the global scope: bindings made in it belong to the runtime and
  // outlive any one evaluator, which is what makes the runtime reusable
  // across evaluations.
  [[nodiscard]] environment& root_env() noexcept { return *envs_.front(); }

private:
#pragma region Data members

  string_set syms_;
  std::vector<std::unique_ptr<cell>> cells_;
  std::vector<std::unique_ptr<heap_string>> strings_;
  std::vector<std::unique_ptr<closure>> closures_;
  std::vector<std::unique_ptr<primitive>> primitives_;
  std::vector<std::unique_ptr<environment>> envs_;

#pragma endregion
};

#pragma endregion
#pragma region Printing definitions

inline const std::string& value::as_string() const {
  assert(is_string());
  return v_.get<kind::string>()->str;
}

inline value value::head() const { return as_cell().head; }
inline value value::tail() const { return as_cell().tail; }

inline bool value::append(std::string& out) const {
  switch (type()) {
  case kind::nil: out += "nil"; break;
  case kind::boolean: out += (as_bool() ? "true" : "false"); break;
  case kind::integer: strings::append_num(out, as_int()); break;
  case kind::floating: do_append_float(out, as_float()); break;
  case kind::symbol: out += as_symbol().name(); break;
  case kind::string: v_.get<kind::string>()->append(out); break;
  case kind::cell: as_cell().append(out); break;
  case kind::closure: out += "#<lambda>"; break;
  case kind::primitive: as_primitive().append(out); break;
  }
  return true;
}

inline bool value::append_dump(std::string& out) const {
  if (is_cell()) return as_cell().append_dump(out);
  return append(out);
}

#pragma endregion

}}} // namespace corvid::lang::coreb
