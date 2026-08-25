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
#include "value.h"

namespace corvid { inline namespace lang { namespace coreb {

// The CoreB kernel runtime.
//
// A `runtime` is a `runtime_core`, which owns the symbol table and the heap
// (see "value.h"), plus the kernel's vocabulary: every special-form and
// builtin spelling, interned once at construction and reachable by name.
//
// The split keeps the core ignorant of what any symbol means while sparing
// every caller a second object to carry:
//
//   runtime rt;
//   auto v = rt.cons(value{rt.sym_quote}, rt.cons(value{42}, value{}));
//   v.print();  // "(quote 42)"

#pragma region runtime

// The kernel runtime: the heap and symbol table, plus the pre-interned
// kernel vocabulary.
//
// Each `sym_` member is one kernel spelling, interned by the constructor.
// The prefix is what lets the vocabulary sit directly on the runtime: it
// keeps `sym_cons` clear of the `cons` allocator, and spellings that are C++
// keywords or punctuation get an ordinary name (`sym_if`, `sym_plus`), with
// `sym_nil_p` following the Lisp convention of a trailing p for a predicate.
//
// Naming a symbol here does not bind it: what these resolve to, if anything,
// is the evaluator's business.
class runtime final: public runtime_core {
public:
  runtime()
      : sym_quote{intern("quote")}, sym_unquote{intern("unquote")},
        sym_unquote_splicing{intern("unquote_splicing")},
        sym_unquote_literal{intern("%unquote")}, sym_if{intern("if")},
        sym_define{intern("define")}, sym_lambda{intern("lambda")},
        sym_begin{intern("begin")}, sym_plus{intern("+")},
        sym_minus{intern("-")}, sym_times{intern("*")},
        sym_divide{intern("/")}, sym_eq{intern("==")}, sym_ne{intern("!=")},
        sym_lt{intern("<")}, sym_le{intern("<=")}, sym_gt{intern(">")},
        sym_ge{intern(">=")}, sym_cons{intern("cons")},
        sym_list{intern("list")}, sym_head{intern("head")},
        sym_tail{intern("tail")}, sym_nil_p{intern("nil?")},
        sym_append{intern("append")}, sym_gensym{intern("gensym")} {}

  // The special forms, and the template marks the reader spells inside a
  // quote (special to the expander rather than the evaluator).
  symbol sym_quote, sym_unquote, sym_unquote_splicing, sym_unquote_literal;
  symbol sym_if, sym_define, sym_lambda, sym_begin;

  // The builtin primitives.
  symbol sym_plus, sym_minus, sym_times, sym_divide;
  symbol sym_eq, sym_ne, sym_lt, sym_le, sym_gt, sym_ge;
  symbol sym_cons, sym_list, sym_head, sym_tail, sym_nil_p;
  symbol sym_append, sym_gensym;
};

#pragma endregion

}}} // namespace corvid::lang::coreb
