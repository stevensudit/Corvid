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
// A facade is written as the interface it describes, a struct of plain
// member function declarations, and everything else derives from it:
//
//    struct animal_api {
//      void speak() const;
//    };
//    struct animal: reflected_facade<animal, animal_api> {};
//
//    consteval auto corvid_proxy_spec(animal*, dog*) {
//      return make_proxy_spec<animal, dog>();
//    }
//
//    proxy_view<animal> p{my_dog};
//    p.speak();
//
// `reflected_facade` derives the facade's name and method list from the
// declarations, expanding to the `facade<...>` with the `name<>` and
// `method<>` list that would otherwise be written by hand.
//
// `reflected_impl<T>` replaces the `boilerplate<T>` that a facade author would
// write by hand to map facade members to the target's members. It enumerates
// `T`'s members, and serves as the default impl of every registered pair whose
// facade has none, the tier `members<>` falls through to, and an ordinary base
// for a partial override.
//
// The reflected sugar API gives every handle of a facade that lacks a
// hand-written `api` its member-call sugar, `p.speak()` for
// `p.call<"speak">()`. All of
// the C++23 spellings keep working alongside; see "Reflection (C++26)" in
// "proxy.md".
//
// Gated on `__cpp_impl_reflection`, so the header is empty (beyond the proxy
// system it includes) on a compiler without P2996. Each helper below states
// the gcc 16 limitation that shaped it, so a later compiler can reshape one
// helper at a time.
#include "owning_proxy.h"
#include "proxy_common.h"
#include "proxy_view.h"
#include "shared_proxy.h"

#if __cpp_impl_reflection >= 202506L

#include <meta>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace corvid { inline namespace meta {
namespace prox {

namespace details {

#pragma region Enumeration

// `info_pack` is a pack of reflections carried as a type.
template<std::meta::info... Ms>
struct info_pack {};

// `named_functions` finds the non-static member functions of `cls` named
// `name`, as seen from `ctx`, or none when `cls` is not a class type.
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
consteval std::vector<std::meta::info> named_functions(std::meta::info cls,
    std::string_view name, std::meta::access_context ctx) {
  std::vector<std::meta::info> out;
  if (!std::meta::is_class_type(cls)) return out;

  for (const auto m : std::meta::members_of(cls, ctx))
    if (std::meta::is_function(m) && !std::meta::is_static_member(m) &&
        std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
      out.push_back(m);

  if (out.empty())
    for (const auto b : std::meta::bases_of(cls, ctx)) {
      const auto found = named_functions(
          std::meta::dealias(std::meta::type_of(b)), name, ctx);
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
consteval std::meta::info as_pack(const std::vector<std::meta::info>& ms) {
  std::vector<std::meta::info> args;
  for (const auto m : ms) args.push_back(std::meta::reflect_constant(m));
  return std::meta::substitute(^^info_pack, args);
}

// The candidate set for `Key` on `T`, as seen from `Ctx`.
template<typename T, fixed_string Key, std::meta::access_context Ctx>
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
template<std::meta::info M, size_t Ndx>
using reflected_probe_t = rank_probe<Ndx,
    signature_traits<typename[:std::meta::type_of(M):]>::const_qualifier,
    typename signature_traits<typename[:std::meta::type_of(M):]>::args_t>;

// Rank a candidate set for a call on `Self&` with `Args`.
//
// The compiler does the ranking, through a real call expression against a
// `rank_set` of the candidates' probes, exactly as `resolve` does for
// `call<>`: promotions, conversion ranks, and the object parameter's
// constness all weigh as they would in `t.name(args...)`. An empty set
// resolves nothing.
template<typename Cands>
struct reflected_ranker {
  template<typename Self, typename... Args>
  static constexpr bool resolves_v = false;
};

template<std::meta::info... Ms>
requires(sizeof...(Ms) > 0)
struct reflected_ranker<info_pack<Ms...>> {
  static constexpr auto count_v = sizeof...(Ms);
  static constexpr std::array<std::meta::info, count_v> members_v{Ms...};

  template<size_t... Ndxs>
  static auto make_set(std::index_sequence<Ndxs...>)
      -> rank_set<reflected_probe_t<Ms, Ndxs>...>;
  using set_t = decltype(make_set(std::make_index_sequence<count_v>{}));

  template<typename Self>
  using probe_t =
      std::conditional_t<std::is_const_v<Self>, const set_t, set_t>;

  template<typename Self, typename... Args>
  static constexpr bool resolves_v = requires(probe_t<Self>& s) {
    s(std::declval<Args>()...);
  };

  // The candidate the call resolves to, given that it resolves.
  template<typename Self, typename... Args>
  requires resolves_v<Self, Args...>
  static consteval std::meta::info pick() noexcept {
    return members_v[decltype(std::declval<probe_t<Self>&>()(
        std::declval<Args>()...))::value];
  }
};

#pragma endregion
#pragma region Binding

// The member pointer for reflection `M`.
//
// `&[:M:]` is the address of the spliced member, an ordinary pointer to
// member. Formed in a class template's static member so that no splice
// appears in a function signature (gcc 16 cannot mangle one there). Forming
// the pointer is not access-checked, so any member the enumeration could see
// can be bound.
template<std::meta::info M>
struct reflected_member {
  static constexpr auto ptr_v = &[:M:];
};

// The `reflected_binding` determines whether `Key` on `T` binds for a call on
// `Self&` with `Args`, as seen from `Ctx`, and the member pointer it binds to.
//
// The primary is the unbound case. The partial applies when the candidates
// resolve, and binds when the resolved member is also invocable on `Self&`
// (which a member of an inaccessible base is not).
//
// A class template rather than a consteval function, so that `on` names only
// class template-ids in its signature: gcc 16 cannot mangle a consteval call
// carrying reflection values there.
template<typename T, std::meta::access_context Ctx, fixed_string Key,
    typename Self, typename... Args>
struct reflected_binding {
  static constexpr bool bound_v = false;
};

template<typename T, std::meta::access_context Ctx, fixed_string Key,
    typename Self, typename... Args>
requires(reflected_ranker<
    reflected_candidates_t<T, Key, Ctx>>::template resolves_v<Self, Args...>)
struct reflected_binding<T, Ctx, Key, Self, Args...> {
  using ranker_t = reflected_ranker<reflected_candidates_t<T, Key, Ctx>>;
  static constexpr auto ptr_v =
      reflected_member<ranker_t::template pick<Self, Args...>()>::ptr_v;
  using ptr_t = decltype(ptr_v);
  static constexpr bool bound_v = std::is_invocable_v<ptr_t, Self&, Args...>;
};

// The resolved call of a bound key, as its result type and whether the
// invocation is nothrow.
//
// Separate from `reflected_binding` so that they are formed only for a key
// that binds.
template<typename T, std::meta::access_context Ctx, fixed_string Key,
    typename Self, typename... Args>
struct reflected_call {
  using binding_t = reflected_binding<T, Ctx, Key, Self, Args...>;
  using result_t =
      std::invoke_result_t<typename binding_t::ptr_t, Self&, Args...>;
  static constexpr bool nothrow_v =
      std::is_nothrow_invocable_v<typename binding_t::ptr_t, Self&, Args...>;
};

#pragma endregion
#pragma region Interface-first facades

// The key of reflected member `M`, as a `fixed_string` sized by its
// identifier.
//
// A variable template rather than a consteval function, so that each key is
// its own constant evaluation: gcc 16 rejects a class NTTP built from a
// pointer inside a consteval loop.
template<std::meta::info M>
constexpr inline size_t key_len_v = std::meta::identifier_of(M).size();

template<std::meta::info M>
constexpr inline auto key_v = fixed_string{std::meta::identifier_of(M).data(),
    std::integral_constant<size_t, key_len_v<M>>{}};

// The function type of reflected member `M`, qualifiers included.
//
// An alias, because gcc 16 rejects a type splice directly under a pack
// expansion.
template<std::meta::info M>
using signature_t = [:std::meta::type_of(M):];

// The methods interface `api` declares, as an `info_pack` in declaration
// order: its public, non-static, named, non-special member functions.
//
// `is_special_member_function` drops constructors and the like, which may
// carry an identifier; operators and conversion functions have none. A
// member function template is not a function, so a deducing-this member is
// not a method.
consteval std::meta::info method_pack(std::meta::info api) {
  std::vector<std::meta::info> ms;
  for (const auto m :
      std::meta::members_of(api, std::meta::access_context::unprivileged()))
    if (std::meta::is_function(m) && !std::meta::is_static_member(m) &&
        std::meta::has_identifier(m) &&
        !std::meta::is_special_member_function(m))
      ms.push_back(std::meta::reflect_constant(m));

  return std::meta::substitute(^^info_pack, ms);
}

// Whether the entries `Es` include a `name` entry.
template<typename... Es>
constexpr inline bool has_name_entry_v = (entry_name<Es>::is_name || ...);

// `facade_maker` forms the `facade<...>` of facade type `F` from the method
// pack of its interface, followed by the extra entries `Es`.
//
// The name is `F`'s own identifier unless `Es` carries a `name` entry. The
// consteval enumeration only collected reflections; every key and signature
// is formed here, at the type level, by pack expansion (gcc 16 rejects
// `substitute` inside a loop over reflected members).
template<typename F, typename Methods, typename... Es>
struct facade_maker;

template<typename F, std::meta::info... Ms, typename... Es>
struct facade_maker<F, info_pack<Ms...>, Es...> {
  using type = std::conditional_t<has_name_entry_v<Es...>,
      facade<method<key_v<Ms>, signature_t<Ms>>..., Es...>,
      facade<name<key_v<^^F>>, method<key_v<Ms>, signature_t<Ms>>..., Es...>>;
};

#pragma endregion

} // namespace details

#pragma region Interface-first facades

// `reflected_facade` is the facade base for `F` spelled by the declarations
// of interface `Api`, creating an ordinary `facade<...>` that the rest of the
// machinery takes as it would a hand-written one.
//
// The `Api` interface holds plain member function declarations, which are
// never defined and never called. They are just the specification. The
// declaration grammar carries everything a `method<>` entry does: `void f()
// const` is a const method, `noexcept` is honored, and two declarations of one
// name are an overload set. Static members, constructors, operators, data
// members, and templates are not methods, and a deducing-this forwarder is a
// template, so it cannot serve as a spec.
//
// For example:
//
//    struct animal_api {
//      void speak() const;
//    };
//    struct animal: reflected_facade<animal, animal_api> {};
//
// is equivalent to `facade<name<"animal">, method<"speak", void() const>>`.
//
// The interface need not be written for the purpose: any class serves, its
// public member functions being the spec, so `reflected_facade<F, lawman>` is
// a facade over `lawman`'s whole public interface, bodies ignored.
//
// The name is `F`'s own identifier. A `name<>` entry among `Es` overrides it,
// for a facade whose identifier does not serve (two facades sharing one across
// namespaces, which must not collide in a composition).
//
// `extends<>` entries are listed in `Es` as they would be in a `facade<...>`.
template<typename F, typename Api, typename... Es>
using reflected_facade = details::facade_maker<F,
    typename[:details::method_pack(^^Api):], Es...>::type;

#pragma endregion
#pragma region The reflected impl

// `reflected_impl` is the binding class reflection derives for target `T`:
// the boilerplate a facade author would have had to write by hand, for a type
// whose member names line up with the method list.
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
// The following are never bound, because reflection does not see them as
// member functions: member function templates (a deducing-this member is one),
// static members, data members, and anything on a non-class target.
template<typename T,
    std::meta::access_context Ctx = std::meta::access_context::current()>
struct reflected_impl: proxy_impl_base {
  template<fixed_string Key, typename Self, typename... Args>
  requires(details::reflected_binding<T, Ctx, Key, Self, Args...>::bound_v)
  static constexpr auto on(method_key<Key>, Self& t, Args&&... args) noexcept(
      details::reflected_call<T, Ctx, Key, Self, Args...>::nothrow_v)
      -> details::reflected_call<T, Ctx, Key, Self, Args...>::result_t {
    return std::invoke(
        details::reflected_binding<T, Ctx, Key, Self, Args...>::ptr_v, t,
        std::forward<Args>(args)...);
  }
};

namespace details {

// Whether `T` is the `validate_api` probe.
template<typename T>
constexpr inline bool is_api_probe_v = false;
template<Facade F>
constexpr inline bool is_api_probe_v<api_probe<F>> = true;

// The reflected impl as the default impl of a pair whose facade has no
// `boilerplate`.
//
// Spelled here, at namespace scope, so the enumeration runs under the
// library's access context and this route binds public members only. A
// private member binds through a hook that names `reflected_impl<T>` itself.
//
// Does not work with a `validate_api` probe, since `api` forwarders are
// templates that reflection does not see as members. A facade with a
// hand-written `api` and no `boilerplate` therefore registers with
// `api_check::off`, which the registration's own diagnostic asks for.
template<Facade F, typename T>
requires(!requires {
  typename F::template boilerplate<T>;
} && !is_api_probe_v<T>)
struct default_impl<F, T> {
  using type = reflected_impl<T>;
};

} // namespace details

#pragma endregion
#pragma region The reflected Sugar API

namespace details {

// Fwd.
template<Facade F, typename H>
struct reflected_api;

// Byte offset of the sugar API subobject within `cls`, searching its public
// bases recursively, or -1 when `cls` has none.
//
// The handles inherit the sugar API at different depths (directly for `proxy`,
// through `view_base` or `shared_base` for the rest), so the offset
// accumulates along the chain. `^^alias` reflects the alias, so both sides
// are `dealias`ed before comparing.
consteval std::ptrdiff_t
api_offset_of(std::meta::info cls, std::meta::info api) {
  for (const auto b :
      std::meta::bases_of(cls, std::meta::access_context::unprivileged()))
  {
    const auto base = std::meta::dealias(std::meta::type_of(b));
    if (base == api) return std::meta::offset_of(b).bytes;

    const auto nested = api_offset_of(base, api);
    if (nested >= 0) return std::meta::offset_of(b).bytes + nested;
  }
  return -1;
}

// `reflected_sugar` is one member of the reflected sugar API: an empty
// callable standing in for the method `Key` on handle `H`, so that
// `h.key(args...)` is `h.key`, a data member, followed by `(args...)`, its
// call operator.
//
// The call operator forwards to `H::call<Key>`, deep const included: a
// const handle reaches only the const operator, which forwards to the const
// `call`, and both operators are constrained on the forward compiling, so
// overloads, const pairs, and `noexcept` resolve inside `call<>`.
//
// The handle is recovered from the member's own address, by the offset of the
// member within the sugar API and of the sugar API within the handle, both
// computed from reflection when the operator is first instantiated (the handle
// is complete by then, where it is not when the sugar API is defined).
template<Facade F, typename H, fixed_string Key>
struct reflected_sugar {
  template<typename... Args>
  constexpr auto operator()(Args&&... args) noexcept(noexcept(
      std::declval<H&>().template call<Key>(std::forward<Args>(args)...)))
      -> decltype(std::declval<H&>().template call<Key>(
          std::forward<Args>(args)...)) {
    return owner().template call<Key>(std::forward<Args>(args)...);
  }

  template<typename... Args>
  constexpr auto operator()(Args&&... args) const noexcept(
      noexcept(std::declval<const H&>().template call<Key>(
          std::forward<Args>(args)...)))
      -> decltype(std::declval<const H&>().template call<Key>(
          std::forward<Args>(args)...)) {
    return owner().template call<Key>(std::forward<Args>(args)...);
  }

private:
  // Byte offset of this member from the start of `H`.
  static consteval std::ptrdiff_t offset() {
    using api_t = reflected_api<F, H>::type;
    std::ptrdiff_t member = -1;
    for (const auto m : std::meta::nonstatic_data_members_of(^^api_t,
             std::meta::access_context::unprivileged()))
      if (std::meta::identifier_of(m) == Key.view())
        member = std::meta::offset_of(m).bytes;
    const auto base = api_offset_of(^^H, std::meta::dealias(^^api_t));
    if (member < 0) throw "reflected api member not found";
    if (base < 0) throw "reflected api base not found";
    return member + base;
  }

  H& owner() noexcept {
    return *reinterpret_cast<H*>(reinterpret_cast<char*>(this) - offset());
  }
  const H& owner() const noexcept {
    return *reinterpret_cast<const H*>(
        reinterpret_cast<const char*>(this) - offset());
  }
};

// One `data_member_spec` per distinct method name in the flattened slot
// list `Ss` (an overload set or a const pair is one name, so one member).
//
// `substitute` forms `reflected_sugar<F, H, Key>` from reflections of its
// arguments; the key is `reflect_constant` of the slot's `fixed_string`, a
// structural type, so it rides through as an NTTP. `data_member_spec` then
// describes the member: its type, its name, and `no_unique_address`.
template<Facade F, typename H, typename... Ss>
consteval std::vector<std::meta::info> sugar_specs(std::tuple<Ss...>*) {
  std::vector<std::string_view> seen;
  std::vector<std::meta::info> specs;
  const auto add = [&]<typename S>() {
    const auto name = S::name_v.view();
    if (std::ranges::find(seen, name) != seen.end()) return;
    seen.push_back(name);
    const auto type = std::meta::substitute(^^reflected_sugar,
        {^^F, ^^H, std::meta::reflect_constant(S::name_v)});
    // Built by `push_back`: under `-fsanitize=undefined`, gcc 16 rejects a
    // consteval `std::string{name}` as non-constant.
    std::string member_name;
    for (const auto c : name) member_name.push_back(c);
    std::meta::data_member_options opts;
    opts.name = std::move(member_name);
    opts.no_unique_address = true;
    specs.push_back(std::meta::data_member_spec(type, opts));
  };
  (add.template operator()<Ss>(), ...);
  return specs;
}

// `reflected_api` is the sugar API for facade `F` on handle `H`, as its nested
// `type`: an empty class of empty `reflected_sugar` members, one per method
// name, defined when `type` is first named.
//
// Every member is empty and of a distinct type, so they all share the API's
// address, the API is an empty base, and the handle keeps its size.
//
// `define_aggregate` runs only inside a `consteval {}` block, and the block
// must sit in a scope enclosing the class it defines (gcc 16), so the API is
// the nested class of a class template whose body holds the block, which runs
// at instantiation.
template<Facade F, typename H>
struct reflected_api {
  struct type;
  consteval {
    using slots_t = vtbuild_t<F>::flat_slots_t;
    std::meta::define_aggregate(^^type,
        sugar_specs<F, H>(static_cast<slots_t*>(nullptr)));
  }
};

// The reflected sugar API as the sugar base of every handle of a facade that
// defines no `api` of its own.
template<Facade F, typename H>
requires(!requires { typename F::api; })
struct api_base<F, H> {
  using type = reflected_api<F, H>::type;
};

} // namespace details

#pragma endregion

} // namespace prox

#pragma region Exports

// Call-site vocabulary, exported to `corvid::meta`; see proxy_common.h.
using prox::reflected_facade;
using prox::reflected_impl;

#pragma endregion

}} // namespace corvid::meta

#endif
