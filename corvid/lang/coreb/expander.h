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
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../containers/core/opt_find.h"
#include "../../containers/core/scoped_value.h"
#include "../../containers/core/value_or_error.h"
#include "eval.h"
#include "runtime.h"
#include "value.h"

namespace corvid { inline namespace lang { namespace coreb {

using namespace std::literals;

// CoreB expander.
//
// Rewrites the forms the reader produces into the kernel forms the evaluator
// understands, before evaluation: templates become list-construction code,
// with their auto-gensym names freshened, and macro calls are replaced by
// their expansions. The evaluator never sees a template hole or a macro
// call.
//
//   runtime rt;
//   expander ex(rt);
//   auto code = ex.expand(*hall_reader::read_one(rt, "'(a $x)"));
//   if (code) code->print();  // "(list (quote a) x)"

#pragma region expand_error

// Description of a failed expansion.
using expand_error = error_value<struct ExpandTag>;

#pragma endregion
#pragma region expander

// Expander from read forms to evaluable kernel forms.
//
// Every quote in code context is a template. One with no holes is the literal
// it always was and passes through untouched, while one containing `(unquote
// e)`, `(unquote_splicing e)`, or `(%unquote e)` is rewritten into the `list`,
// `append`, and `quote` calls that build its value, with each hole's
// expression expanded in turn, since it is code. A `%` name in a built
// template is an auto-gensym: each becomes one fresh `gensym` symbol per
// template, so macro-introduced temporaries cannot capture use-site names.
// A quote nested inside a template is data, holes and gensym names and all;
// it becomes a template of its own only if the code it ends up in is itself
// expanded.
//
// A call whose head symbol is bound to a macro in the global scope is a
// macro call. The macro's body runs on the unevaluated argument forms, and
// its result is expanded in place of the call, so expansion repeats until
// only kernel forms and plain calls remain.
//
// The binding is consulted at expansion time, which is why the pass is per
// top-level form: one form's evaluation can define the macro the next form
// uses. A `lambda` or `macro` parameter shadows a macro of the same name for
// its body, and the parameter list itself is names, not code. A `define`
// inside a body is invisible here, so it cannot shadow a macro, because macros
// are global-scope constructs.
//
// Everything else is walked element-wise, so templates and macro calls
// anywhere inside a form are found.
//
// Expansion allocates through the runtime but never collects, holding a
// `gc_inhibitor` while macro bodies evaluate. The caller pins the result
// before evaluating it, as for any read form.
//
// Failure is reported by value as an `expand_error`. Nesting deeper than
// `max_depth` is rejected rather than risking stack exhaustion, which also
// bounds a macro whose expansion never stops producing macro calls.
class expander final {
public:
  template<typename T>
  using result = value_or_error<T, expand_error>;

  explicit expander(runtime& rt) : rt_{rt}, ev_{rt} {}

  expander(const expander&) = delete;
  expander& operator=(const expander&) = delete;

  // Expand `form`, returning the form to evaluate in its place.
  [[nodiscard]] result<value> expand(value form) {
    scoped_value guard(depth_, depth_ + 1);
    if (depth_ >= max_depth) return expand_error{"expansion too deep"};
    if (!form.is_cell()) return form;

    std::vector<value> elems;
    value tail;
    if (!form.append_elements(elems)) tail = last_tail(form);
    const auto head_sym = elems[0].maybe_symbol();

    // A well-formed quote is the one opaque form: a template or a literal.
    if (head_sym && *head_sym == rt_.sym_quote && elems.size() == 2 &&
        tail.is_nil())
    {
      if (!needs_build(elems[1])) return form;
      // Each template renames its own `%` names, so the map is scoped to
      // this build; a template inside a hole gets a fresh one.
      auto saved = std::exchange(gensyms_, {});
      auto built = build(elems[1]);
      gensyms_ = std::move(saved);
      return built;
    }

    // A lambda or macro form gets structural treatment: its parameter list
    // is names, not code.
    if (head_sym &&
        (*head_sym == rt_.sym_lambda || *head_sym == rt_.sym_macro) &&
        elems.size() >= 2)
      return expand_abstraction(std::move(elems), tail);

    // A proper call on a macro-bound, unshadowed head symbol is a macro
    // call.
    if (head_sym && tail.is_nil() && !is_shadowed(*head_sym))
      if (const auto bound = rt_.root_env().lookup(*head_sym))
        if (const auto mac = bound->maybe_macro())
          return expand_macro_call(*head_sym, *mac,
              std::span<const value>(elems).subspan(1));

    // Anything else is code all the way down, dotted tail included.
    return expand_elements(std::move(elems), tail);
  }

private:
#pragma region Templates

  // The final `tail` of an improper list: whatever ends the chain.
  // NOLINTNEXTLINE(bugprone-exception-escape): tail() is guarded by is_cell().
  [[nodiscard]] static value last_tail(value form) noexcept {
    while (form.is_cell()) form = form.tail();
    return form;
  }

  // Which template mark, if any, a form is: its head symbol against the
  // three, for a form of the right shape.
  enum class mark : std::uint8_t { none, unquote, splicing, literal };

  [[nodiscard]] mark mark_of(const value& v) const {
    const auto c = v.maybe_cell();
    if (!c) return mark::none;
    const auto head_ptr = c->head.maybe_symbol();
    if (!head_ptr) return mark::none;
    if (*head_ptr == rt_.sym_unquote) return mark::unquote;
    if (*head_ptr == rt_.sym_unquote_splicing) return mark::splicing;
    if (*head_ptr == rt_.sym_unquote_literal) return mark::literal;
    return mark::none;
  }

  // Whether `v` is a nested quote form, which a template treats as data.
  [[nodiscard]] bool is_quote(const value& v) const {
    const auto c = v.maybe_cell();
    if (!c) return false;
    const auto head_ptr = c->head.maybe_symbol();
    return head_ptr && *head_ptr == rt_.sym_quote;
  }

  // Whether template `t` needs building: it contains a hole or an
  // auto-gensym name, at any depth short of a nested quote.
  //
  // A hole in tail position, `(a . $x)`, reads as the list `(a unquote x)`,
  // so a mark form met as the remainder of a chain counts as one. This is
  // the classic quasiquote quirk, and the dotted spelling is the only way to
  // put a hole there.
  [[nodiscard]] bool needs_build(value t) const {
    if (const auto sym_ptr = t.maybe_symbol())
      return sym_ptr->name().starts_with('%');
    if (is_quote(t)) return false;
    for (; t.is_cell(); t = t.tail()) {
      if (mark_of(t) != mark::none) return true;
      if (needs_build(t.head())) return true;
    }
    return false;
  }

  // Quote `v` as a literal: `(quote v)`.
  [[nodiscard]] value quoted(value v) {
    return rt_.list_of({value{rt_.sym_quote}, v});
  }

  // The template-local fresh symbol for the auto-gensym name `s`.
  //
  // The first sighting in a template mints it; later sightings reuse it, so
  // every `%tmp` in one template is the same fresh name and no two templates
  // share one.
  [[nodiscard]] symbol freshen(symbol s) {
    if (const auto found = find_opt(gensyms_, s)) return *found;
    const auto fresh = rt_.gensym(s.name().substr(1));
    gensyms_.emplace(s, fresh);
    return fresh;
  }

  // The single operand of a mark form, or an error if its shape is off.
  [[nodiscard]] static result<value>
  mark_operand(const value& form, std::string_view name) {
    std::vector<value> elems;
    if (!form.append_elements(elems) || elems.size() != 2)
      return expand_error{std::string{name} + ": expects 1 argument"};
    return elems[1];
  }

  // Build the code that constructs template `t`.
  [[nodiscard]] result<value> build(value t) {
    scoped_value guard(depth_, depth_ + 1);
    if (depth_ >= max_depth) return expand_error{"expansion too deep"};

    // Atoms: a symbol must be quoted to survive evaluation, an auto-gensym
    // name freshened first; the rest evaluate to themselves.
    if (const auto sym_ptr = t.maybe_symbol()) {
      if (sym_ptr->name().starts_with('%'))
        return quoted(value{freshen(*sym_ptr)});
      return quoted(t);
    }
    if (!t.is_cell()) return t;

    switch (mark_of(t)) {
    case mark::unquote: {
      auto e = mark_operand(t, "unquote");
      if (!e) return e;
      return expand(*e);
    }
    case mark::literal: {
      // The escape: the data `(unquote <e>)`, e being a template itself.
      auto e = mark_operand(t, "%unquote");
      if (!e) return e;
      auto inner = build(*e);
      if (!inner) return inner;
      return rt_.list_of(
          {value{rt_.sym_list}, quoted(value{rt_.sym_unquote}), *inner});
    }
    case mark::splicing:
      return expand_error{"unquote_splicing: outside a list"};
    case mark::none: break;
    }

    // A nested quote is data; so is a subtree needing no work.
    if (is_quote(t) || !needs_build(t)) return quoted(t);

    return build_list(t);
  }

  // Build the code that constructs list template `t`.
  //
  // Runs of plain elements become `(list ...)` segments, each splice is its
  // own segment, and `append` joins the segments, onto the built tail when
  // the template is dotted.
  [[nodiscard]] result<value> build_list(value t) {
    std::vector<value> segments;
    std::vector<value> run{value{rt_.sym_list}};
    const auto flush = [&] {
      if (run.size() == 1) return;
      segments.push_back(rt_.list_of(run));
      run.resize(1);
    };
    for (; t.is_cell(); t = t.tail()) {
      // A mark form as the remainder is a hole in tail position; `build`
      // handles it, rejecting a splice there.
      if (mark_of(t) != mark::none) break;
      const auto& elem = t.head();
      if (mark_of(elem) == mark::splicing) {
        auto e = mark_operand(elem, "unquote_splicing");
        if (!e) return e;
        auto code = expand(*e);
        if (!code) return code;
        flush();
        segments.push_back(*code);
        continue;
      }
      auto code = build(elem);
      if (!code) return code;
      run.push_back(*code);
    }
    flush();
    if (segments.size() == 1 && t.is_nil()) return segments[0];
    if (!t.is_nil()) {
      auto tail = build(t);
      if (!tail) return tail;
      segments.push_back(*tail);
    }
    segments.insert(segments.begin(), value{rt_.sym_append});
    return rt_.list_of(segments);
  }

#pragma endregion
#pragma region Macro calls

  // Expand `elems` and `tail` element-wise from `from` on, rebuilding the
  // form.
  [[nodiscard]] result<value>
  expand_elements(std::vector<value> elems, value tail, size_t from = 0) {
    for (auto& elem : std::span{elems}.subspan(from)) {
      auto r = expand(elem);
      if (!r) return r;
      elem = *r;
    }
    if (!tail.is_nil()) {
      auto r = expand(tail);
      if (!r) return r;
      tail = *r;
    }
    return rt_.list_of(elems, tail);
  }

  // Expand a lambda or macro form, walking the body only, its parameter
  // names shadowing any same-named macros there.
  //
  // The parameter list passes through untouched; whether it is well formed
  // is the evaluator's business.
  [[nodiscard]] result<value>
  expand_abstraction(std::vector<value> elems, value tail) {
    const auto shadow_mark = shadowed_.size();
    for (auto params = elems[1]; params.is_cell(); params = params.tail())
      if (const auto name = params.head().maybe_symbol())
        shadowed_.push_back(*name);
    auto expanded = expand_elements(std::move(elems), tail, 2);
    shadowed_.erase(shadowed_.begin() + static_cast<ptrdiff_t>(shadow_mark),
        shadowed_.end());
    return expanded;
  }

  // Whether an enclosing lambda or macro parameter shadows `name`.
  [[nodiscard]] bool is_shadowed(symbol name) const noexcept {
    return find_opt(shadowed_, name).has_value();
  }

  // Expand the macro call `(name args...)`, invoking the macro on the
  // unevaluated argument forms and expanding its result in place of the
  // call.
  //
  // The body runs in a frame binding the parameters to the argument forms,
  // scoped inside the macro's captured environment. Safe-point collection is
  // inhibited for the duration, since the expansion in flight is not pinned.
  [[nodiscard]] result<value> expand_macro_call(symbol name,
      const closure& mac, std::span<const value> args) {
    if (args.size() != mac.params.size())
      return expand_error{
          name.name() + ": expects " + std::to_string(mac.params.size()) +
          " arguments, got " + std::to_string(args.size())};
    auto& frame = rt_.make_env(*mac.env);
    for (size_t ndx = 0; ndx < args.size(); ++ndx)
      frame.bind(mac.params[ndx], args[ndx]);
    gc_inhibitor guard(rt_);
    value out;
    for (const auto& form : mac.body) {
      auto r = ev_.eval(form, frame);
      if (!r) return expand_error{name.name() + ": " + r.as_error().reason};
      out = *r;
    }
    return expand(out);
  }

#pragma endregion

  runtime& rt_;
  evaluator ev_;
  size_t depth_{};
  std::unordered_map<symbol, symbol, symbol_hash> gensyms_;
  std::vector<symbol> shadowed_;
};

#pragma endregion

}}} // namespace corvid::lang::coreb
