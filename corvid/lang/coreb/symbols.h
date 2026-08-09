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

// The kernel vocabulary and the bundle that carries it.
//
// A `symbols` pre-interns every name the kernel and the front ends
// reference by identity: the special forms and the builtin primitives. A
// `runtime_environment` owns a `runtime` together with its `symbols`, and
// is what every language component runs against:
//
//   runtime_environment run_env;
//   evaluator ev(run_env);
//   auto v = ev.eval(*hall_reader::read_one(run_env, "(+ 1 2)"));

#pragma region symbols

// The kernel vocabulary, pre-interned.
//
// Construction interns every member into `rt`, so this class is the one
// place that knows the spellings; what a name resolves to is not its
// concern. Members whose spelling collides with a C++ keyword carry the
// `keyword_` prefix, and operator spellings are named for the operation,
// with `nil_p` following the Lisp convention of `p` for a predicate.
struct symbols final {
  explicit symbols(runtime& rt)
      : quote{rt.intern("quote")}, keyword_if{rt.intern("if")},
        define{rt.intern("define")}, lambda{rt.intern("lambda")},
        begin{rt.intern("begin")}, plus{rt.intern("+")}, minus{rt.intern("-")},
        times{rt.intern("*")}, divide{rt.intern("/")}, eq{rt.intern("==")},
        ne{rt.intern("!=")}, lt{rt.intern("<")}, le{rt.intern("<=")},
        gt{rt.intern(">")}, ge{rt.intern(">=")}, cons{rt.intern("cons")},
        list{rt.intern("list")}, head{rt.intern("head")},
        tail{rt.intern("tail")}, nil_p{rt.intern("nil?")} {}

  // The special forms.
  symbol quote, keyword_if, define, lambda, begin;

  // The builtin primitives.
  symbol plus, minus, times, divide;
  symbol eq, ne, lt, le, gt, ge;
  symbol cons, list, head, tail, nil_p;
};

#pragma endregion
#pragma region runtime_environment

// The bundle a language component runs against: a runtime and its
// pre-interned vocabulary.
//
// Owns the runtime, so replacing the bundle replaces the whole world. The
// reader, parsers, unparser, and evaluator all take one by reference; it
// must outlive them, exactly as the runtime it owns must.
struct runtime_environment final {
  runtime_environment() : syms(rt) {}

  runtime rt;
  symbols syms;
};

#pragma endregion

}}} // namespace corvid::lang::coreb
