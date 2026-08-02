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
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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
// interned symbol, string, or cons cell.
//
// A `runtime` owns the symbol table and the heap the aggregate values live in.
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

#pragma endregion
#pragma region value

// Fwd.
struct cell;
struct heap_string;

// Discriminator for the alternatives a `value` can hold.
enum class kind : std::uint8_t {
  nil,
  boolean,
  integer,
  floating,
  symbol,
  string,
  cell
};
consteval auto corvid_enum_spec(kind*) {
  return corvid::enums::sequence::make_sequence_enum_spec<kind,
      "nil,boolean,integer,floating,symbol,string,cell">();
}

// CoreB value: nil, a boolean, an integer, a float, a symbol, or a handle to
// a heap-allocated string or cons cell.
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
      symbol, heap_string*, cell*>;

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

  // Whether this is something other than a cons cell.
  [[nodiscard]] bool is_atom() const noexcept { return !is_cell(); }

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
  // escaped, proper lists as "(a b c)", improper ones dotted. Printing cyclic
  // data (possible only after mutation) is unsupported and will not
  // terminate.
  bool append(std::string& out) const;

  // Return the printed s-expression form.
  [[nodiscard]] std::string print() const {
    std::string out;
    append(out);
    return out;
  }

#pragma endregion

private:
#pragma region Helpers

  static void do_append_float(std::string& out, double d);

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

#pragma endregion
#pragma region runtime

// The CoreB kernel runtime, containing the symbol table and the heap.
//
// All aggregate values (cons cells, strings) are allocated here, and every
// `value` handle is only valid while its runtime is alive.
//
// Storage is currently freed only on destruction; milestone 3 replaces that
// with mark-and-sweep collection, which is why ownership is centralized from
// the start.
class runtime final {
public:
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

private:
#pragma region Data members

  string_set syms_;
  std::vector<std::unique_ptr<cell>> cells_;
  std::vector<std::unique_ptr<heap_string>> strings_;

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
  }
  return true;
}

// Append a float so it re-reads as a float.
//
// A rendering with no exponent or decimal point (to_chars prints 1.0 as "1")
// gets ".0" appended. Infinities and NaN print as "inf"/"nan", which the
// reader does not accept as numbers; a round-trip for those is deferred until
// the language decides how to spell them.
inline void value::do_append_float(std::string& out, double d) {
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

}}} // namespace corvid::lang::coreb
