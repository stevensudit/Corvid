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
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "../../containers/core/enum_variant.h"
#include "../../containers/core/value_or_error.h"
#include "../../containers/core/opt_find.h"
#include "../../containers/core/optional_ptr.h"
#include "../../containers/core/transparent.h"
#include "../../enums/sequence_enum.h"
#include "../../meta/concepts.h"
#include "../../strings/cases.h"
#include "../../strings/builder.h"
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
// A `runtime_core` owns the symbol table and the heap the aggregate values
// and environments live in. It is the base half of the runtime: "runtime.h"
// derives the `runtime` embedders actually hold, which adds the kernel's
// pre-interned vocabulary. Nothing here needs that vocabulary, which is why
// the split exists.
//
//   runtime rt;
//   auto v = rt.cons(value{rt.intern("x")}, rt.cons(value{42}, value{}));
//   v.print();  // "(x 42)"

// Declared up front: a friend declaration alone does not make the name
// visible (MSVC injects it as an extension; conforming compilers do not).
class runtime_core;

// Maximum nesting depth for every recursive pass over values: reading,
// printing, Monty parsing and unparsing, and evaluation.
//
// Depth counts genuinely nested structure, not list length: every pass
// iterates chains through `tail` flat. The passes share the one budget, and
// front ends stacked on each other (Monty statements over expressions over
// Hall escapes) seed it forward rather than each starting fresh, so anything
// one pass accepts the others can process in full.
//
// The value is sized so each pass's guard fires before the real C++ stack
// runs out. This is the case even in unoptimized builds, whose frames are
// several times fatter, on the smallest common default stack (1MB on
// Windows).
inline constexpr size_t max_depth = 256;

#pragma region symbol

// Interned symbol.
//
// Each distinct spelling interned in a given runtime yields one unique
// symbol, so equality is pointer identity: comparing symbols never compares
// text.
class symbol final {
public:
  [[nodiscard]] const std::string& name() const noexcept { return *name_; }
  [[nodiscard]] bool operator==(const symbol&) const noexcept = default;

private:
  friend class runtime_core;

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
enum class kind : uint8_t {
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
// heap data. The owning runtime must outlive every `value` handed out from
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

  // Strings are made through `runtime_core::make_string` and symbols through
  // `runtime_core::intern`.
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
  [[nodiscard]] bool is_truthy() const noexcept {
    const auto b = v_.get_if<kind::boolean>();
    return !is_nil() && (!b || *b);
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

  // Test and access in one step.
  //
  // Each `maybe_*` returns an `optional_ptr` to the held alternative, empty
  // when this value holds a different kind, so the result drives a branch
  // directly and offers `std::optional` semantics such as `value_or`.
  [[nodiscard]] optional_ptr<const symbol*> maybe_symbol() const noexcept {
    return v_.get_if<kind::symbol>();
  }
  [[nodiscard]] optional_ptr<const int64_t*> maybe_int() const noexcept {
    return v_.get_if<kind::integer>();
  }
  [[nodiscard]] optional_ptr<const double*> maybe_float() const noexcept {
    return v_.get_if<kind::floating>();
  }
  [[nodiscard]] optional_ptr<cell*> maybe_cell() const noexcept {
    const auto pp = v_.get_if<kind::cell>();
    return pp ? *pp : nullptr;
  }
  [[nodiscard]] optional_ptr<closure*> maybe_closure() const noexcept {
    const auto pp = v_.get_if<kind::closure>();
    return pp ? *pp : nullptr;
  }
  [[nodiscard]] optional_ptr<primitive*> maybe_primitive() const noexcept {
    const auto pp = v_.get_if<kind::primitive>();
    return pp ? *pp : nullptr;
  }

  // The halves of a cons cell.
  //
  // Precondition: `is_cell`.
  [[nodiscard]] value head() const;
  [[nodiscard]] value tail() const;

  // Append a proper list's elements to `out`, returning whether this value
  // is a proper list.
  //
  // Walks the `tail` chain appending each `head`, so nil appends nothing and
  // is proper. Returns false when the walk ends at anything but nil (an
  // improper list, or an atom), with the elements up to that point already
  // appended.
  [[nodiscard]] bool append_elements(std::vector<value>& out) const {
    auto list = *this;
    for (; list.is_cell(); list = list.tail()) out.push_back(list.head());
    return list.is_nil();
  }

#pragma endregion
#pragma region Printing

  // Append the printed s-expression form to `out`.
  //
  // The output is what the reader accepts: symbols bare, strings quoted and
  // escaped, proper lists as "(a b c)", improper ones dotted. Function values
  // are the exception: they have no readable form and print as display forms,
  // "#<primitive name>" and "#<lambda (params) body...>", the latter showing
  // the closure's code but not its captured environment.
  //
  // Nesting deeper than `max_depth` is the other exception: the subtree
  // renders as the display form "#<too deep>" instead of overflowing the C++
  // stack. Only nesting spends depth here: a cell's head, and each form of a
  // closure's body. Returns false if any subtree was truncated this way, true
  // otherwise. Printing cyclic data (possible only after mutation) is
  // unsupported; a cycle through `tail` will not terminate.
  bool append(std::string& out, size_t depth = 0) const;

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
  // tail)", so "(a b)" dumps as "(a . (b . nil))". Atoms print as in `append`,
  // and depth truncation works as in `append` too.
  //
  // Display forms and truncation aside, the output is valid reader syntax,
  // but re-reading it is subject to `max_depth`: the dotted form
  // spends one nesting level per list element where the abbreviated form
  // spends none, so a long proper list dumps fine yet will not read back.
  bool append_dump(std::string& out, size_t depth = 0) const;

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

  // The runtime traces heap pointers through the variant when collecting.
  friend class runtime_core;

  variant_t v_;

#pragma endregion
};

#pragma endregion
#pragma region Heap objects

// Base for the runtime's collectible heap objects.
//
// Every heap object shares the same identity semantics: constructed in place
// by the runtime, never copied, and freed only by collection or the
// runtime's destruction. The base pins those down and carries the
// collector's mark epoch. It is deliberately not polymorphic: tracing stays
// centralized in the runtime, so the base is data, not behavior.
class gc_object {
public:
  gc_object(const gc_object&) = delete;
  gc_object& operator=(const gc_object&) = delete;

protected:
  gc_object() = default;
  ~gc_object() = default;

private:
  friend class runtime_core;

  // Collector mark epoch.
  size_t marked_gen_{};
};

// Cons cell: the two-value node from which all CoreB structure is constructed.
//
// If you like the IBM 704, you can call this a `pair` of `car` and `cdr`. But,
// also, if you like the IBM 704, then something is wrong with you. Get help.
//
// A proper list chains through `tail` and terminates at nil. In other words,
// the `tail` always contains either `nil` or another `cell`. Any other `tail`
// makes the sequence improper, so it's printed with a dot: "(a b . c)".
//
// Constructed only by the runtime, which owns every cell.
struct cell final: gc_object {
private:
  enum class allow : bool { ctor };
  friend class runtime_core;

public:
  cell(allow, value head, value tail) noexcept : head{head}, tail{tail} {}

  // Append the printed list form to `out`: proper lists as "(a b c)",
  // improper tails dotted. Returns false if a subtree was truncated for
  // depth (see `max_depth`).
  bool append(std::string& out, size_t depth = 0) const {
    if (depth >= max_depth) {
      out += "#<too deep>";
      return false;
    }
    out += '(';
    auto ok = true;
    for (const auto* cur = this;;) {
      ok = cur->head.append(out, depth + 1) && ok;
      const auto& rest = cur->tail;
      if (rest.is_nil()) break;
      if (!rest.is_cell()) {
        out += " . ";
        ok = rest.append(out, depth) && ok;
        break;
      }
      out += ' ';
      cur = &rest.as_cell();
    }
    out += ')';
    return ok;
  }

  // Append the structural debug form to `out`: "(head . tail)", recursing
  // into each head while iterating the tail chain, with no list
  // abbreviation. Returns false if a subtree was truncated for depth (see
  // `max_depth`).
  bool append_dump(std::string& out, size_t depth = 0) const {
    if (depth >= max_depth) {
      out += "#<too deep>";
      return false;
    }
    auto ok = true;
    size_t opens = 0;
    for (const auto* cur = this;;) {
      out += '(';
      ++opens;
      ok = cur->head.append_dump(out, depth + 1) && ok;
      out += " . ";
      const auto& rest = cur->tail;
      if (!rest.is_cell()) {
        ok = rest.append_dump(out, depth) && ok;
        break;
      }
      cur = &rest.as_cell();
    }
    out.append(opens, ')');
    return ok;
  }

  value head;
  value tail;
};

// Heap-allocated string payload.
//
// Constructed only by the runtime, which owns it.
struct heap_string final: gc_object {
private:
  enum class allow : bool { ctor };
  friend class runtime_core;

public:
  heap_string(allow, std::string str) noexcept : str{std::move(str)} {}

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
// Constructed only by the runtime, which owns it.
struct closure final: gc_object {
private:
  enum class allow : bool { ctor };
  friend class runtime_core;

public:
  closure(allow, std::vector<symbol> params, std::vector<value> body,
      environment& env) noexcept
      : params{std::move(params)}, body{std::move(body)}, env{&env} {}

  // Append the display form, "#<lambda (params) body...>".
  //
  // Closures have no readable form: the parameters and body are shown, but the
  // captured environment has no printed spelling. Returns false if a subtree
  // was truncated for depth (see `max_depth`).
  bool append(std::string& out, size_t depth = 0) const;

  std::vector<symbol> params;
  std::vector<value> body;
  environment* env;
};

// Primitive: a kernel C++ function exposed as a CoreB function value.
//
// The function receives the evaluated arguments and returns a value or an
// error message, which the evaluator prefixes with the primitive's name.
//
// Constructed only by the runtime, which owns it.
struct primitive final: gc_object {
private:
  enum class allow : bool { ctor };
  friend class runtime_core;

public:
  using fn_t = value_or_error<value, std::string> (*)(runtime_core&,
      std::span<const value>);

  primitive(allow, symbol name, fn_t fn) noexcept : name{name}, fn{fn} {}

  // Append the display form, "#<primitive name>". Primitives have no
  // readable form.
  bool append(std::string& out) const {
    strings::shared_builder{out} << "#<primitive " << name.name() << '>';
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
// Constructed only by the runtime, which owns it.
class environment final: public gc_object {
private:
  enum class allow : bool { ctor };
  friend class runtime_core;

public:
  environment(allow, environment* parent) : parent_{parent} {}

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
#pragma region runtime_core

// Fwd.
class gc_pin;

// The symbol table and the heap: the half of the runtime that knows nothing
// of what any particular symbol means.
//
// All aggregate values (cons cells, strings, closures, primitives) and all
// environments are allocated here, and every `value` handle is only valid
// while its runtime is alive.
//
// Storage is reclaimed by a stop-the-world mark-and-sweep collector.
// `collect` frees everything not reachable from the roots: the root
// environment, every live `gc_pin`, and, for the safe-point form
// `maybe_collect`, whatever the evaluator hands in. A `value` held in C++
// across a possible collection must be pinned, or it dangles. Interned
// symbol spellings are not collected; they leak deliberately until gensym
// makes that worth solving.
//
// This is a base, not a whole runtime: "runtime.h" derives `runtime`, which
// adds the kernel's pre-interned vocabulary, and that is what embedders
// construct. Construction and destruction are therefore protected.
class runtime_core {
public:
  // Intern `name`, returning its unique symbol.
  //
  // Repeated calls with the same spelling return the same symbol. The
  // spelling is not validated here: the front ends enforce the token
  // classes before interning, so only symbols they produce are guaranteed
  // to re-read from their printed form.
  [[nodiscard]] symbol intern(std::string_view name) {
    if (const auto found = find_opt(syms_, name)) return symbol{*found};
    return symbol{*syms_.emplace(name).first};
  }

  // Intern a fresh symbol spelled `%base_N`, distinct from every symbol so
  // far.
  //
  // The `%` prefix marks it kernel-generated (see "coreb.md"), so no source
  // definition can bind the name, and N is drawn from a counter, skipping any
  // spelling the table already holds, so a `%` name read from source cannot
  // collide either. `base` must be a word spelling; the callers enforce that.
  [[nodiscard]] symbol gensym(std::string_view base) {
    std::string name;
    do {
      name.clear();
      strings::shared_builder{name} << '%' << base << '_';
      strings::append_num(name, ++gensyms_);
    } while (syms_.contains(name));
    const auto sym = symbol{*syms_.emplace(std::move(name)).first};
    gensym_names_.insert(&sym.name());
    return sym;
  }

  // Whether `s` was minted by `gensym`.
  //
  // This is what exempts a fresh name from the `%`-reservation policing at
  // binding time: a macro's gensym-made temporary is bindable, while a
  // hand-spelled `%` name stays the kernel's.
  [[nodiscard]] bool is_gensym(symbol s) const noexcept {
    return gensym_names_.contains(&s.name());
  }

  // Construct a cell.
  [[nodiscard]] value cons(value head, value tail) {
    ++allocs_;
    cells_.push_back(std::make_unique<cell>(cell::allow::ctor, head, tail));
    return value{*cells_.back()};
  }

  // Construct the proper list `(elems...)` as a chain of cells.
  //
  // An explicit `tail` seeds the chain in place of nil, yielding the dotted
  // list `(elems... . tail)`.
  [[nodiscard]] value list_of(std::span<const value> elems, value tail = {}) {
    auto list = tail;
    for (const auto& elem : std::views::reverse(elems))
      list = cons(elem, list);
    return list;
  }
  [[nodiscard]] value list_of(std::initializer_list<value> elems) {
    return list_of(std::span<const value>(elems.begin(), elems.size()));
  }

  // Allocate a string.
  [[nodiscard]] heap_string& make_string(std::string str) {
    ++allocs_;
    strings_.push_back(std::make_unique<heap_string>(heap_string::allow::ctor,
        std::move(str)));
    return *strings_.back();
  }

  // Construct a closure over `env`.
  [[nodiscard]] value make_closure(std::vector<symbol> params,
      std::vector<value> body, environment& env) {
    ++allocs_;
    closures_.push_back(std::make_unique<closure>(closure::allow::ctor,
        std::move(params), std::move(body), env));
    return value{*closures_.back()};
  }

  // Expose a C++ function as a function value named `name`.
  [[nodiscard]] value make_primitive(symbol name, primitive::fn_t fn) {
    ++allocs_;
    primitives_.push_back(
        std::make_unique<primitive>(primitive::allow::ctor, name, fn));
    return value{*primitives_.back()};
  }

  // Construct an environment scoped inside `parent`.
  [[nodiscard]] environment& make_env(environment& parent) {
    ++allocs_;
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

#pragma region Collection

  // Allocations between safe-point collections.
  //
  // Crossing this makes the next `maybe_collect` collect. Collection cost is
  // proportional to live data, so the value only trades peak heap size
  // against collection frequency; it is deliberately untuned.
  static constexpr size_t gc_threshold = 10'000;

  // Free every heap object not reachable from a root: the root environment
  // and every live `gc_pin`.
  //
  // A `value` held in C++ dangles after this unless what it references is
  // still reachable, whether through an environment or a `gc_pin`.
  void collect() { do_collect(value{}, nullptr); }

  // The safe-point form of `collect`: collect only if allocation since the
  // last collection has crossed `gc_threshold`, tracing `live` and `env` as
  // extra roots.
  //
  // The evaluator calls this at its outermost trampoline loop top, where its
  // entire live set is the expression about to be evaluated and the current
  // environment.
  void maybe_collect(value live, environment& env) {
    if (allocs_ < gc_threshold) return;
    do_collect(live, &env);
  }

  // The number of heap objects (cells, strings, closures, primitives, and
  // environments) currently owned.
  [[nodiscard]] size_t live_objects() const noexcept {
    return cells_.size() + strings_.size() + closures_.size() +
           primitives_.size() + envs_.size();
  }

#pragma endregion

protected:
  // A fresh runtime owns one environment from the start: the empty root
  // scope.
  runtime_core() {
    envs_.push_back(
        std::make_unique<environment>(environment::allow::ctor, nullptr));
  }

  // Non-polymorphic base: destruction runs through the derived type, and
  // there is nothing to delete a `runtime_core*` with.
  ~runtime_core() = default;

private:
#pragma region Collection helpers

  friend class gc_pin;

  void do_pin(const gc_pin& pin) { pins_.push_back(&pin); }
  void do_unpin(const gc_pin& pin) noexcept { std::erase(pins_, &pin); }

  // Mark `obj` for the current epoch, returning whether it was newly
  // reached.
  [[nodiscard]] bool do_mark(gc_object& obj) const noexcept {
    if (obj.marked_gen_ == gen_) return false;
    obj.marked_gen_ = gen_;
    return true;
  }

  void do_collect(value live, environment* extra_env);

#pragma endregion
#pragma region Data members

  string_set syms_;
  std::vector<std::unique_ptr<cell>> cells_;
  std::vector<std::unique_ptr<heap_string>> strings_;
  std::vector<std::unique_ptr<closure>> closures_;
  std::vector<std::unique_ptr<primitive>> primitives_;
  std::vector<std::unique_ptr<environment>> envs_;
  std::vector<const gc_pin*> pins_;
  std::unordered_set<const std::string*> gensym_names_;
  size_t gen_{};
  size_t allocs_{};
  size_t gensyms_{};

#pragma endregion
};

#pragma endregion
#pragma region gc_pin

// Root registration for values held in C++ across a possible collection.
//
// A pin registers the caller's own storage, not a copy: the pinned variable
// or span is traced as a root on every collection while the pin lives, so
// rebinding through the variable needs no re-pin. The storage must outlive
// the pin, and the pin must not outlive the runtime.
//
// There is no race between allocating and pinning, because allocation never
// collects: collection runs only inside `collect` and `maybe_collect`, and
// the evaluator calls the latter only at its safe point. A value is at risk
// only when held across one of those, which is exactly when a pin is needed.
//
//   value v = rt.cons(value{1}, value{});
//   gc_pin pin(rt, v); // No collection can run between these lines.
//   rt.collect(); // `v` survives; unpinned garbage does not.
class gc_pin final {
public:
  gc_pin(runtime_core& rt, const value& v) : rt_{rt}, vals_(&v, 1) {
    rt_.do_pin(*this);
  }

  // A temporary would dangle by the end of the statement, long before any
  // collection could trace it. A span's underlying storage cannot be
  // checked the same way, so there the contract stays on the caller.
  gc_pin(runtime_core&, value&&) = delete;

  gc_pin(runtime_core& rt, std::span<const value> vals)
      : rt_{rt}, vals_{vals} {
    rt_.do_pin(*this);
  }
  ~gc_pin() { rt_.do_unpin(*this); }

  gc_pin(const gc_pin&) = delete;
  gc_pin& operator=(const gc_pin&) = delete;

private:
  friend class runtime_core;

  runtime_core& rt_;
  std::span<const value> vals_;
};

#pragma endregion
#pragma region Printing definitions

inline const std::string& value::as_string() const {
  assert(is_string());
  return v_.get<kind::string>()->str;
}

inline value value::head() const { return as_cell().head; }
inline value value::tail() const { return as_cell().tail; }

inline bool value::append(std::string& out, size_t depth) const {
  switch (type()) {
  case kind::nil: out += "nil"; break;
  case kind::boolean: out += (as_bool() ? "true" : "false"); break;
  case kind::integer: strings::append_num(out, as_int()); break;
  case kind::floating: do_append_float(out, as_float()); break;
  case kind::symbol: out += as_symbol().name(); break;
  case kind::string: v_.get<kind::string>()->append(out); break;
  case kind::cell: return as_cell().append(out, depth);
  case kind::closure: return as_closure().append(out, depth);
  case kind::primitive: as_primitive().append(out); break;
  }
  return true;
}

inline bool closure::append(std::string& out, size_t depth) const {
  if (depth >= max_depth) {
    out += "#<too deep>";
    return false;
  }
  out += "#<lambda (";
  for (size_t ndx = 0; ndx < params.size(); ++ndx) {
    if (ndx) out += ' ';
    out += params[ndx].name();
  }
  out += ')';
  auto ok = true;
  for (const auto& form : body) {
    out += ' ';
    ok = form.append(out, depth + 1) && ok;
  }
  out += '>';
  return ok;
}

inline bool value::append_dump(std::string& out, size_t depth) const {
  if (is_cell()) return as_cell().append_dump(out, depth);
  return append(out, depth);
}

#pragma endregion
#pragma region Collection definitions

// Mark and sweep collection of the heap, tracing outward from the roots.
inline void runtime_core::do_collect(value live, environment* extra_env) {
  // Mark: trace outward from the roots with explicit worklists.
  //
  // Recursion here would be fatal: structure nests arbitrarily deep, and the
  // object graph is cyclic (a self-recursive definition's closure captures the
  // scope that binds it). The epoch check is also what breaks the cycles.
  ++gen_;
  allocs_ = 0;

  // Accumulate the root values and environments into worklists.
  std::vector<value> vals{live};
  std::vector<environment*> envs{&root_env()};
  if (extra_env) envs.push_back(extra_env);
  for (const auto* pin : pins_)
    vals.insert(vals.end(), pin->vals_.begin(), pin->vals_.end());

  // Keep marking until the worklists are empty.
  while (!vals.empty() || !envs.empty()) {
    // Find the next env that hasn't already been marked, mark it, and push its
    // parent and all its values onto the worklists.
    if (!envs.empty()) {
      auto* env = envs.back();
      envs.pop_back();
      if (!do_mark(*env)) continue;
      if (env->parent_) envs.push_back(env->parent_);
      for (const auto& binding : env->vars_) vals.push_back(binding.second);
      continue;
    }
    // Find the next val that hasn't already been marked, mark it, and push its
    // children onto the worklists.
    const auto v = vals.back();
    vals.pop_back();
    switch (v.type()) {
    case kind::nil:
    case kind::boolean:
    case kind::integer:
    case kind::floating:
    case kind::symbol: break;
    case kind::string: v.v_.get<kind::string>()->marked_gen_ = gen_; break;
    case kind::cell: {
      auto& c = v.as_cell();
      if (!do_mark(c)) break;
      vals.push_back(c.head);
      vals.push_back(c.tail);
      break;
    }
    case kind::closure: {
      auto& c = v.as_closure();
      if (!do_mark(c)) break;
      vals.insert(vals.end(), c.body.begin(), c.body.end());
      envs.push_back(c.env);
      break;
    }
    case kind::primitive: v.as_primitive().marked_gen_ = gen_; break;
    }
  }

  // Sweep: free everything the trace did not reach.
  const auto dead = [this](const auto& p) { return p->marked_gen_ != gen_; };
  std::erase_if(cells_, dead);
  std::erase_if(strings_, dead);
  std::erase_if(closures_, dead);
  std::erase_if(primitives_, dead);
  std::erase_if(envs_, dead);
}

#pragma endregion

}}} // namespace corvid::lang::coreb
