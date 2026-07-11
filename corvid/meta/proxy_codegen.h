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
#include <algorithm>
#include <array>
#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "naming.h"
#include "proxy.h"

// Source generation for facade authors.
//
// `prox::codegen<F>(os)` writes the canonical `api` and `boilerplate` for a
// facade, ready to paste into its body.
//
// A separate header, so `proxy.h` itself stays free of streams and RTTI-based
// type naming.
namespace corvid { inline namespace meta { namespace prox {

namespace details {

// `codegen_starts`: facade-wide parameter numbering, one sequence spanning
// every slot of the flattened list; entry `ndx` is slot ndx's first number.
//
// One shared sequence is what keeps a parameter's generated name identical
// between the `api` forwarder and the `boilerplate` binding, so renaming it
// after pasting is a single search-and-replace that hits both. A slot the
// generated `api` does not spell (one an inherited base `api` covers) still
// consumes numbers, which is harmless; the names are arbitrary by design.
template<typename... Ss>
consteval std::array<std::size_t, sizeof...(Ss)>
codegen_starts(std::tuple<Ss...>*) noexcept {
  std::array<std::size_t, sizeof...(Ss)> starts{};
  std::size_t next = 1;
  std::size_t ndx = 0;
  ((starts[ndx++] = next,
       next += std::tuple_size_v<typename Ss::method_t::args_t>),
      ...);
  return starts;
}

// `declared_in`: whether the method `M`, declared by facade `Owner`, appears
// in facade `P`'s flattened list, meaning an inherited `P::api` already
// covers it.
//
// The tuple pointer parameter carries `P`'s flattened slots in deducible
// position.
template<typename Owner, typename M, Facade P, typename... Ps>
consteval bool declared_in(std::tuple<Ps...>*) noexcept {
  return (
      (std::same_as<Owner,
           std::conditional_t<std::is_void_v<typename Ps::owner_t>, P,
               typename Ps::owner_t>> &&
          std::same_as<M, typename Ps::method_t>) ||
      ...);
}

// `name_declared_in`: whether any slot of the flattened list answers to
// `name`.
template<typename... Ps>
consteval bool
name_declared_in(std::string_view name, std::tuple<Ps...>*) noexcept {
  return ((Ps::name_v.view() == name) || ...);
}

// `has_const_twin`: whether the facade also declares a const method with
// slot `S`'s name and arguments, making `S` the mutable member of a const
// pair.
//
// Its generated forwarder then carries the trailing requires-clause (see the
// `api` caveats under "Member-call sugar").
template<typename S, typename... Ss>
consteval bool has_const_twin(std::tuple<Ss...>*) noexcept {
  return (
      (!std::same_as<S, Ss> && Ss::const_v && !S::const_v &&
          Ss::name_v.view() == S::name_v.view() &&
          std::same_as<
              typename method_traits<typename S::method_t>::norm_args_t,
              typename method_traits<typename Ss::method_t>::norm_args_t>) ||
      ...);
}

// `heaviest_base`: index of the direct base with the largest flattened
// method list (ties to the first), the base whose `api` the generated one
// inherits.
//
// This is the single-path diamond shape the `api` documentation recommends.
template<Facade F>
consteval std::size_t heaviest_base() noexcept {
  return []<std::size_t... Ndxs>(std::index_sequence<Ndxs...>) {
    constexpr std::array<std::size_t, sizeof...(Ndxs)> sizes{
        std::tuple_size_v<typename vtbuild_t<
            typename vtbuild_t<F>::template base_t<Ndxs>>::flat_slots_t>...};
    std::size_t best = 0;
    for (std::size_t ndx = 1; ndx != sizes.size(); ++ndx)
      if (sizes[ndx] > sizes[best]) best = ndx;
    return best;
  }(std::make_index_sequence<vtbuild_t<F>::base_count_v>{});
}

// `codegen_path_t`: the base facade whose `api` the generated one inherits,
// or `void` for a facade with no bases.
//
// A heaviest base that defines no `api` (supported, not recommended) also
// yields `void`: there is nothing to inherit, so the generated `api` spells
// every flattened forwarder itself.
template<Facade F>
consteval auto do_codegen_path() noexcept {
  if constexpr (vtbuild_t<F>::base_count_v == 0)
    return std::type_identity<void>{};
  else {
    using heavy_t = vtbuild_t<F>::template base_t<heaviest_base<F>()>;
    if constexpr (requires { typename heavy_t::api; })
      return std::type_identity<heavy_t>{};
    else
      return std::type_identity<void>{};
  }
}
template<Facade F>
using codegen_path_t = decltype(do_codegen_path<F>())::type;

// `api_emits`: whether the generated `api` spells a forwarder for slot `S`:
// every own method, plus every inherited method the path base `P` does not
// cover.
template<typename S, typename P>
consteval bool api_emits() noexcept {
  if constexpr (std::is_void_v<typename S::owner_t> || std::is_void_v<P>)
    return true;
  else
    return !declared_in<typename S::owner_t, typename S::method_t, P>(
        static_cast<vtbuild_t<P>::flat_slots_t*>(nullptr));
}

// `emit_params`: emit `, T arg_N` for each declared parameter, numbering
// from `next`.
template<typename... Args>
void emit_params(std::ostream& os, std::size_t next, std::tuple<Args...>*) {
  ((os << ", " << friendly_type_name<Args>() << " arg_" << next++), ...);
}

// `emit_args`: emit `arg_N, arg_N+1, ...`, numbering from `next`.
template<typename... Args>
void emit_args(std::ostream& os, std::size_t next, std::tuple<Args...>*) {
  for (std::size_t ndx = 0; ndx != sizeof...(Args); ++ndx)
    os << (ndx ? ", " : "") << "arg_" << next + ndx;
}

// `emit_api_slot`: one `api` forwarder for slot `S` of facade `F`.
template<Facade F, typename S>
void emit_api_slot(std::ostream& os, std::size_t next) {
  using result_t = S::result_t;
  constexpr auto* args = static_cast<S::method_t::args_t*>(nullptr);
  const auto name = S::name_v.view();
  const auto result = friendly_type_name<result_t>();
  os << "    " << result << " " << name << "(this "
     << (S::const_v ? "const auto&" : "auto&&") << " self";
  emit_params(os, next, args);
  os << ")";
  if constexpr (S::noexcept_v) os << " noexcept";
  constexpr bool twin =
      has_const_twin<S>(static_cast<vtbuild_t<F>::flat_slots_t*>(nullptr));
  if constexpr (twin) {
    os << "\n    requires(requires {\n      { self.template call<\"" << name
       << "\">(";
    emit_args(os, next, args);
    os << ") } -> std::same_as<" << result << ">;\n    })\n    {\n";
  } else {
    os << " {\n";
  }
  os << "      ";
  if constexpr (!std::is_void_v<result_t>) os << "return ";
  os << "self.template call<\"" << name << "\">(";
  emit_args(os, next, args);
  os << ");\n    }\n";
}

// `emit_boilerplate_slot`: one `boilerplate` binding for slot `S`.
template<typename S>
void emit_boilerplate_slot(std::ostream& os, std::size_t next) {
  using result_t = S::result_t;
  constexpr auto* args = static_cast<S::method_t::args_t*>(nullptr);
  const auto name = S::name_v.view();
  os << "    static " << friendly_type_name<result_t>() << " on(method_key<\""
     << name << "\">, " << (S::const_v ? "const T& t" : "T& t");
  emit_params(os, next, args);
  os << ")";
  if constexpr (S::noexcept_v) os << " noexcept";
  os << " {\n      ";
  if constexpr (!std::is_void_v<result_t>) os << "return ";
  os << "t." << name << "(";
  emit_args(os, next, args);
  os << ");\n    }\n";
}

} // namespace details

// `codegen`: write the canonical `api` and `boilerplate` for facade `F` to
// `os`, ready to paste into the facade body.
//
// The generated `api` inherits the heaviest direct base's `api` (the
// single-path diamond shape), spells forwarders for the facade's own
// methods and for any inherited methods that path does not cover, adds the
// using-declarations that merge names those forwarders would otherwise
// hide, marks noexcept forwarders, and carries the const-pair
// requires-clause. When the heaviest base defines no `api` (supported, not
// recommended), nothing is inherited and every flattened method gets its
// own forwarder. The `boilerplate` covers the facade's own methods. This
// is the closest thing to reflection available today; when C++26 reflection
// lands, it deletes the paste step.
//
// Parameter names are generated as one `arg_N` sequence spanning the whole
// facade, so each name is unique across the output and renaming one after
// pasting is a single search-and-replace hitting the `api` and the
// `boilerplate` together.
//
// Base `api` spellings use the demangled C++ type name of the base facade
// (via `friendly_type_name`), not its formal `name<>` entry, which need not
// match the type name. Type spellings are the demangler's, best-effort
// normalized; touch up after pasting where they fall short.
template<Facade F>
void codegen(std::ostream& os) {
  using slots_t = details::vtbuild_t<F>::flat_slots_t;
  using path_t = details::codegen_path_t<F>;
  constexpr auto* slots = static_cast<slots_t*>(nullptr);
  constexpr auto starts = details::codegen_starts(slots);
  constexpr auto count = std::tuple_size_v<slots_t>;
  const auto walk = [&]<typename Fn>(Fn&& fn) {
    [&]<std::size_t... Ndxs>(std::index_sequence<Ndxs...>) {
      (fn.template operator()<std::tuple_element_t<Ndxs, slots_t>>(
           starts[Ndxs]),
          ...);
    }(std::make_index_sequence<count>{});
  };

  os << "  struct api";
  std::string path_name;
  if constexpr (!std::is_void_v<path_t>) {
    path_name = friendly_type_name<path_t>();
    os << ": " << path_name << "::api";
  }
  os << " {\n";
  if constexpr (!std::is_void_v<path_t>) {
    // A forwarder hides every inherited overload of its name until a
    // using-declaration merges them back in.
    std::vector<std::string_view> merged;
    walk([&]<typename S>(std::size_t) {
      constexpr auto* path_slots =
          static_cast<details::vtbuild_t<path_t>::flat_slots_t*>(nullptr);
      if constexpr (details::api_emits<S, path_t>() &&
                    details::name_declared_in(S::name_v.view(), path_slots))
      {
        const auto name = S::name_v.view();
        if (std::find(merged.begin(), merged.end(), name) == merged.end())
          merged.push_back(name);
      }
    });
    for (const auto name : merged)
      os << "    using " << path_name << "::api::" << name << ";\n";
  }
  walk([&]<typename S>(std::size_t next) {
    if constexpr (details::api_emits<S, path_t>())
      details::emit_api_slot<F, S>(os, next);
  });
  os << "  };\n";

  os << "  template<typename T>\n  struct boilerplate: proxy_impl_base {\n";
  walk([&]<typename S>(std::size_t next) {
    if constexpr (std::is_void_v<typename S::owner_t>)
      details::emit_boilerplate_slot<S>(os, next);
  });
  os << "  };\n";
}

}}} // namespace corvid::meta::prox
