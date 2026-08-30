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

// C++26 reflection layer of the proxy system.
//
// `reflected_impl<T>` is the boilerplate a facade author would write by hand,
// derived by enumerating `T`'s members: conformance for a type whose member
// names line up with the method list becomes the registration line alone,
// and a facade needs no `boilerplate` at all. It is the default impl of
// every registered pair whose facade has none, the tier `members<>` falls
// through to, and an ordinary base for a partial override; see `reflected_impl`
// and "Reflection (C++26)" in "proxy.md".
//
// Gated on `__cpp_impl_reflection`, so the header is empty (beyond the proxy
// system it includes) on a compiler without P2996. Each helper below states
// the gcc 16 limitation that shaped it, so a later compiler can reshape one
// helper at a time.
#include "proxy.h"

#if __cpp_impl_reflection >= 202506L

#include <meta>

#include <array>
#include <cstddef>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace corvid {
inline namespace meta {
namespace prox {

namespace details {

#pragma region Enumeration

// `info_pack` is a pack of reflections carried as a type.
template <std::meta::info... Ms> struct info_pack {};

// The non-static member functions of `cls` named `name`, as seen from `ctx`,
// or none when `cls` is not a class type.
//
// Searches the class's own members first and its bases only when it declares
// none by that name, which is the name hiding an ordinary member call
// `t.name(...)` applies. Two bases each contributing one leaves both in the
// set, so the call is ambiguous, as it would be in the language.
//
// `members_of` walks a class's direct members under an access context, each
// as an `info`, the compile-time handle to an entity. `has_identifier` guards
// `identifier_of`, which is ill-formed on unnamed members (constructors,
// operators). A member function template is a template, not a function, so
// `is_function` excludes it: a deducing-this member cannot be reflected into
// a binding.
consteval std::vector<std::meta::info>
named_functions(std::meta::info cls, std::string_view name,
                std::meta::access_context ctx) {
  std::vector<std::meta::info> out;
  if (!std::meta::is_class_type(cls))
    return out;

  for (const auto m : std::meta::members_of(cls, ctx))
    if (std::meta::is_function(m) && !std::meta::is_static_member(m) &&
        std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
      out.push_back(m);

  if (out.empty())
    for (const auto b : std::meta::bases_of(cls, ctx)) {
      const auto found =
          named_functions(std::meta::dealias(std::meta::type_of(b)), name, ctx);
      out.insert(out.end(), found.begin(), found.end());
    }

  return out;
}

// Lift a vector of reflections into an `info_pack` type.
//
// `substitute` instantiates a template with reflected arguments, which is the
// bridge from a value computation back into template-land. Each element is
// wrapped by `reflect_constant`, because a template argument is a value, and
// a bare `info` element would be read as the entity it reflects rather than
// as itself.
consteval std::meta::info as_pack(const std::vector<std::meta::info> &ms) {
  std::vector<std::meta::info> args;
  for (const auto m : ms)
    args.push_back(std::meta::reflect_constant(m));

  return std::meta::substitute(^^info_pack, args);
}

// The candidate set for `Key` on `T`, as seen from `Ctx`.
template <typename T, fixed_string Key, std::meta::access_context Ctx>
using reflected_candidates_t = [:as_pack(
                                     named_functions(^^T, Key.view(), Ctx)):];

#pragma endregion
#pragma region Ranking

// The synthetic overload standing in for candidate `M` at index `Ndx`.
//
// `type_of` on a member function is its function type, qualifiers included,
// and a type splice (`[:t:]`) spells that type, so `signature_traits`
// decomposes it as if it had been written. The probe is `rank_probe`, the
// same synthetic overload `resolve` ranks for a facade's own overload sets.
template <std::meta::info M, size_t Ndx>
using reflected_probe_t = rank_probe<
    Ndx, signature_traits<typename[:std::meta::type_of(M):]>::const_qualifier,
    typename signature_traits<typename[:std::meta::type_of(M):]>::args_t>;

// `reflected_ranker` ranks a candidate set for a call on `Self&` with `Args`.
//
// The compiler does the ranking, through a real call expression against a
// `rank_set` of the candidates' probes, exactly as `resolve` does for
// `call<>`: promotions, conversion ranks, and the object parameter's
// constness all weigh as they would in `t.name(args...)`. An empty set
// resolves nothing.
template <typename Cands> struct reflected_ranker {
  template <typename Self, typename... Args>
  static constexpr bool resolves_v = false;
};

template <std::meta::info... Ms>
  requires(sizeof...(Ms) > 0)
struct reflected_ranker<info_pack<Ms...>> {
  static constexpr auto count_v = sizeof...(Ms);
  static constexpr std::array<std::meta::info, count_v> members_v{Ms...};

  template <size_t... Ndxs>
  static auto make_set(std::index_sequence<Ndxs...>)
      -> rank_set<reflected_probe_t<Ms, Ndxs>...>;
  using set_t = decltype(make_set(std::make_index_sequence<count_v>{}));

  template <typename Self>
  using probe_t = std::conditional_t<std::is_const_v<Self>, const set_t, set_t>;

  template <typename Self, typename... Args>
  static constexpr bool resolves_v =
      requires(probe_t<Self> &s) { s(std::declval<Args>()...); };

  // The candidate the call resolves to, given that it resolves.
  template <typename Self, typename... Args>
    requires resolves_v<Self, Args...>
  static consteval std::meta::info pick() noexcept {
    return members_v[decltype(std::declval<probe_t<Self> &>()(
        std::declval<Args>()...))::value];
  }
};

#pragma endregion
#pragma region Binding

// `reflected_member` is the member pointer for reflection `M`.
//
// `&[:M:]` is the address of the spliced member, an ordinary pointer to
// member. Formed in a class template's static member so that no splice
// appears in a function signature (gcc 16 cannot mangle one there). Forming
// the pointer is not access-checked, so any member the enumeration could see
// can be bound.
template <std::meta::info M> struct reflected_member {
  static constexpr auto ptr_v = &[:M:];
};

// `reflected_binding` is whether `Key` on `T` binds for a call on `Self&`
// with `Args`, as seen from `Ctx`, and the member pointer it binds to.
//
// The primary is the unbound case. The partial applies when the candidates
// resolve, and binds when the resolved member is also invocable on `Self&`
// (which a member of an inaccessible base is not).
//
// A class template rather than a consteval function, so that `on` names only
// class template-ids in its signature: gcc 16 cannot mangle a consteval call
// carrying reflection values there.
template <typename T, std::meta::access_context Ctx, fixed_string Key,
          typename Self, typename... Args>
struct reflected_binding {
  static constexpr bool bound_v = false;
};

template <typename T, std::meta::access_context Ctx, fixed_string Key,
          typename Self, typename... Args>
  requires(reflected_ranker<reflected_candidates_t<T, Key, Ctx>>::
               template resolves_v<Self, Args...>)
struct reflected_binding<T, Ctx, Key, Self, Args...> {
  using ranker_t = reflected_ranker<reflected_candidates_t<T, Key, Ctx>>;
  static constexpr auto ptr_v =
      reflected_member<ranker_t::template pick<Self, Args...>()>::ptr_v;
  using ptr_t = decltype(ptr_v);
  static constexpr bool bound_v = std::is_invocable_v<ptr_t, Self &, Args...>;
};

// The resolved call of a bound key, as its result type and whether the
// invocation is nothrow.
//
// Separate from `reflected_binding` so that they are formed only for a key
// that binds.
template <typename T, std::meta::access_context Ctx, fixed_string Key,
          typename Self, typename... Args>
struct reflected_call {
  using binding_t = reflected_binding<T, Ctx, Key, Self, Args...>;
  using result_t =
      std::invoke_result_t<typename binding_t::ptr_t, Self &, Args...>;
  static constexpr bool nothrow_v =
      std::is_nothrow_invocable_v<typename binding_t::ptr_t, Self &, Args...>;
};

#pragma endregion

} // namespace details

#pragma region The reflected impl

// `reflected_impl` is the binding class reflection derives for target `T`:
// the boilerplate a facade author would write by hand, for a type whose
// member names line up with the method list.
//
// `on` for a key enumerates `T`'s non-static member functions with that name
// (own members first, then bases, with the name hiding of an ordinary member
// call), has the compiler rank them for the call as `call<>` ranks a facade's
// overloads, and invokes the winner through its member pointer with
// `std::invoke`. The target parameter is deduced, so constness flows
// through: a const target reaches only const members, and a const pair
// splits by handle constness. The result type and `noexcept` come from the
// declaration alone, so a conformance probe instantiates no body, and a
// `noexcept` member stays `noexcept` through `call<>`. A key with no viable
// member is unbound, which conformance reports as it would for any other
// binding class.
//
// `Ctx` is the access context the enumeration runs under. It defaults to the
// context where the template-id is written (`access_context::current()` is
// evaluated there, like `std::source_location::current()`), and a private
// member binds when that context can see it: `reflected_impl<T>` spelled in
// a hidden-friend hook of `T`, or in a class nested in `T`, binds `T`'s
// private members, while one spelled at namespace scope binds only the
// public ones. That is the rule `members<>` follows: a private member binds
// when the hook can name it.
//
// It is the default impl (see `default_impl`) of every registered pair whose
// facade has no `boilerplate`, so conformance for a type whose names line up
// is the registration line alone, and `members<>` routes its unlisted keys
// here. It is also an ordinary base: a partial-override binding class
// inherits it, re-exposes `on` with a using-declaration, and adds the one
// binding that needs a body, as it would over a hand-written boilerplate.
// On gcc 16 such a class is defined at namespace scope or nested in `T`, not
// local to the hook; see "gcc 16 notes" in "proxy.md".
//
// Never bound, because reflection does not see them as member functions:
// member function templates (a deducing-this member is one), static members,
// data members, and anything on a non-class target.
template <typename T,
          std::meta::access_context Ctx = std::meta::access_context::current()>
struct reflected_impl : proxy_impl_base {
  template <fixed_string Key, typename Self, typename... Args>
    requires(details::reflected_binding<T, Ctx, Key, Self, Args...>::bound_v)
  static constexpr auto on(method_key<Key>, Self &t, Args &&...args) noexcept(
      details::reflected_call<T, Ctx, Key, Self, Args...>::nothrow_v)
      -> details::reflected_call<T, Ctx, Key, Self, Args...>::result_t {
    return std::invoke(
        details::reflected_binding<T, Ctx, Key, Self, Args...>::ptr_v, t,
        std::forward<Args>(args)...);
  }
};

namespace details {

// The reflected impl as the default impl of a pair whose facade has no
// `boilerplate`.
//
// Spelled here, at namespace scope, so the enumeration runs under the
// library's access context and this route binds public members only. A
// private member binds through a hook that names `reflected_impl<T>` itself.
template <Facade F, typename T>
  requires(!requires { typename F::template boilerplate<T>; })
struct default_impl<F, T> {
  using type = reflected_impl<T>;
};

} // namespace details

#pragma endregion

} // namespace prox

#pragma region Exports

// Call-site vocabulary, exported to `corvid::meta`; see proxy_common.h.
using prox::reflected_impl;

#pragma endregion

} // namespace meta
} // namespace corvid

#endif
