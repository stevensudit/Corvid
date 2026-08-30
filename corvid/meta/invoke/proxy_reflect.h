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
// `p.call<"speak">()`. All of the C++23 spellings keep working alongside; see
// "Reflection (C++26)" in "proxy.md".
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
#include <cstdint>
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

// How the explicit object parameter of a function admits an lvalue.
//
// `implicit` is a function with no explicit object parameter. Of the rest,
// `as_const` takes a const lvalue of its class (`this const C&`, or `this C`
// by value), `as_mutable` takes only a mutable one (`this C&`), and `unbound`
// takes no lvalue at all (`this C&&`).
enum class object_param : uint8_t { implicit, as_mutable, as_const, unbound };

// Classify the object parameter of function `fn`.
//
// `parameters_of` lists a function's parameters, the explicit object
// parameter first when it has one. Convertibility from an lvalue of the
// parameter's own class decides the lvalue it takes. The relation of that
// class to the target is checked where the pointer binds.
consteval object_param object_param_of(std::meta::info fn) {
  const auto ps = std::meta::parameters_of(fn);
  if (ps.empty() || !std::meta::is_explicit_object_parameter(ps[0]))
    return object_param::implicit;

  const auto p = std::meta::dealias(std::meta::type_of(ps[0]));
  const auto c = std::meta::remove_cvref(p);
  if (!std::meta::is_class_type(c)) return object_param::unbound;
  if (std::meta::is_convertible_type(
          std::meta::add_lvalue_reference(std::meta::add_const(c)), p))
    return object_param::as_const;
  if (std::meta::is_convertible_type(std::meta::add_lvalue_reference(c), p))
    return object_param::as_mutable;
  return object_param::unbound;
}

// `deduce_object` determines the function that reflection `m` stands for in a
// call on an lvalue of type `self`.
//
// It returns `m` itself for a function. For a member function template, it
// returns the specialization that deduction would choose for that object. And
// it returns null reflection for a template that does not deduce for it.
//
// A member function template has no signature until it is specialized, and
// `substitute` specializes it on explicit template arguments where a call
// would deduce them. Deduction is reconstructed in two steps.
//
// First the template is specialized on the class of `self` itself, which
// reveals the declared shape of the object parameter (`auto&&` shows as `C&&`,
// `auto&` as `C&`, `auto` as `C`), then on the argument deduction derives for
// an lvalue of `self` from that shape: `self&` for a forwarding reference,
// `self` for an lvalue reference, and the class type by value.
//
// A constraint on the object parameter takes part through `can_substitute`, as
// it would in deduction. A template with further template parameters, which a
// call would deduce from its arguments, does not specialize on the object type
// alone, so it is not a candidate.
//
// The shape is read on the class of `self` rather than on the template's own
// class because gcc 16.2 instantiates the body of a specialization as
// `substitute` forms it, and a body need only be well-formed for the class
// it is called on (an `api` forwarder's `self.call<...>()` exists on the
// handle, not on the `api`). The one specialization this forms beyond the
// deduced one is the rvalue-object one of a forwarding reference.
consteval std::meta::info
deduce_object(std::meta::info m, std::meta::info self) {
  if (std::meta::is_function(m)) return m;
  if (!std::meta::is_function_template(m)) return {};

  const auto cls = std::meta::remove_cvref(self);
  if (!std::meta::can_substitute(m, {cls})) return {};
  const auto ps = std::meta::parameters_of(std::meta::substitute(m, {cls}));
  if (ps.empty() || !std::meta::is_explicit_object_parameter(ps[0])) return {};

  const auto shape = std::meta::dealias(std::meta::type_of(ps[0]));
  std::meta::info arg;
  if (std::meta::is_rvalue_reference_type(shape)) {
    // `const auto&&` is not a forwarding reference and binds no lvalue.
    if (std::meta::is_const_type(std::meta::remove_reference(shape)))
      return {};
    arg = std::meta::add_lvalue_reference(self);
  } else if (std::meta::is_lvalue_reference_type(shape)) {
    arg = self;
  } else {
    arg = std::meta::remove_cvref(self);
  }

  if (!std::meta::can_substitute(m, {arg})) return {};
  return std::meta::substitute(m, {arg});
}

// `named_functions` finds the non-static member functions of `cls` named
// `name` that a call on an lvalue of type `self` can reach, as seen from
// `ctx`, or none when `cls` is not a class type.
//
// A deducing-this member counts through the specialization `deduce_object`
// derives for `self`, and an explicit-object member through itself. Searches
// the class's own members first and its bases only when it declares no
// function or function template by that name, which is the name hiding that
// an ordinary member call `t.name(...)` applies, with two documented gaps.
//
// The first is that a using-declaration is not a member to `members_of`, so a
// base overload it un-hides is not a candidate beside the class's own. The
// second is that a non-function member by that name does not hide the base's
// functions here, though it does in the language. Two bases each contributing
// one leaves both in the set, so the call is ambiguous, as it would be in the
// language.
//
// `members_of` walks a class's direct members under an access context, each
// as an `info`, the compile-time handle to an entity. `has_identifier` guards
// `identifier_of`, which is ill-formed on unnamed members (constructors,
// operators). A member function template is a template to it, not a
// function, and one that turns out to have no deducing object parameter (a
// static member template, or an ordinary function template) still hides.
consteval std::vector<std::meta::info> named_functions(std::meta::info cls,
    std::string_view name, std::meta::access_context ctx,
    std::meta::info self) {
  std::vector<std::meta::info> out;
  if (!std::meta::is_class_type(cls)) return out;

  bool declared = false;
  for (const auto m : std::meta::members_of(cls, ctx)) {
    if (!std::meta::has_identifier(m) || std::meta::identifier_of(m) != name)
      continue;
    const bool function =
        std::meta::is_function(m) && !std::meta::is_static_member(m);
    if (!function && !std::meta::is_function_template(m)) continue;
    declared = true;
    if (const auto f = deduce_object(m, self); f != std::meta::info{})
      out.push_back(f);
  }

  if (!declared)
    for (const auto b : std::meta::bases_of(cls, ctx)) {
      const auto found = named_functions(
          std::meta::dealias(std::meta::type_of(b)), name, ctx, self);
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

// The candidate set for `Key` on `T` in a call on `Self&`, as seen from
// `Ctx`.
template<typename T, fixed_string Key, std::meta::access_context Ctx,
    typename Self>
using reflected_candidates_t = [:as_pack(named_functions(^^T, Key.view(), Ctx,
                                     ^^Self)):];

// The function type of reflected function `M`, qualifiers included.
//
// `type_of` on a member function is its function type, and a type splice
// (`[:t:]`) spells that type. An alias, because gcc 16 rejects a type splice
// directly under a pack expansion.
template<std::meta::info M>
using signature_t = [:std::meta::type_of(M):];

// `object_signature` is the method signature of function type `Sig`, whose
// first parameter is an explicit object parameter admitting `Obj`.
//
// The object parameter leaves the parameter list, and the lvalue it admits
// becomes the const qualifier, so the signature reads as the plain
// declaration of the same method. A function admitting no lvalue keeps its
// result and takes `rank_poison`, which no ranking call satisfies.
template<typename Sig, object_param Obj>
struct object_signature {
  using type = Sig;
};

template<typename R, typename P, typename... Args>
struct object_signature<R(P, Args...), object_param::as_mutable> {
  using type = R(Args...);
};

template<typename R, typename P, typename... Args>
struct object_signature<R(P, Args...) noexcept, object_param::as_mutable> {
  using type = R(Args...) noexcept;
};

template<typename R, typename P, typename... Args>
struct object_signature<R(P, Args...), object_param::as_const> {
  using type = R(Args...) const;
};

template<typename R, typename P, typename... Args>
struct object_signature<R(P, Args...) noexcept, object_param::as_const> {
  using type = R(Args...) const noexcept;
};

template<typename Sig>
struct object_signature<Sig, object_param::unbound> {
  using result_t = signature_traits<Sig>::result_t;
  using type = result_t(rank_poison);
};

// The method signature reflected function `M` declares, which is its
// function type with an explicit object parameter folded into the
// qualifiers, so that `signature_traits` decomposes an explicit-object
// member as if it had been declared the plain way.
template<std::meta::info M>
using method_signature_t =
    object_signature<signature_t<M>, object_param_of(M)>::type;

#pragma endregion
#pragma region Ranking

// The synthetic overload standing in for candidate `M` at index `Ndx`.
//
// The probe is `rank_probe`, the same synthetic overload `resolve` ranks for
// a facade's own overload sets, over the candidate's method signature.
template<std::meta::info M, size_t Ndx>
using reflected_probe_t =
    rank_probe<Ndx, signature_traits<method_signature_t<M>>::const_qualifier,
        typename signature_traits<method_signature_t<M>>::args_t>;

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

// The pointer for reflection `M`, where `M` is a member of a class template.
//
// `&[:M:]` is the address of the spliced member, an ordinary pointer to
// member, or a plain function pointer for an explicit-object member, whose
// object is its first argument either way under `std::invoke`. Formed in a
// class template's static member so that no splice appears in a function
// signature (gcc 16 cannot mangle one there). Forming the pointer is not
// access-checked, so any member the enumeration could see can be bound.
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
// It is a class template rather than a consteval function, so that `on` names
// only class template-ids in its signature. Needed because gcc 16 cannot
// mangle a consteval call carrying reflection values there.
template<typename T, std::meta::access_context Ctx, fixed_string Key,
    typename Self, typename... Args>
struct reflected_binding {
  static constexpr bool bound_v = false;
};

template<typename T, std::meta::access_context Ctx, fixed_string Key,
    typename Self, typename... Args>
requires(reflected_ranker<reflected_candidates_t<T, Key, Ctx,
        Self>>::template resolves_v<Self, Args...>)
struct reflected_binding<T, Ctx, Key, Self, Args...> {
  using ranker_t = reflected_ranker<reflected_candidates_t<T, Key, Ctx, Self>>;
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

// The name of reflected member `M`, which for the specialization of a
// deducing-this member is its template's, since a specialization has no
// identifier of its own.
consteval std::string_view name_of(std::meta::info m) {
  return std::meta::identifier_of(
      std::meta::has_identifier(m) ? m : std::meta::template_of(m));
}

// The key of reflected member `M`, as a `fixed_string` sized by its name.
//
// This is a variable template rather than a consteval function, so that each
// key is its own constant evaluation. Needed because gcc 16 rejects a class
// NTTP built from a pointer inside a consteval loop.
template<std::meta::info M>
constexpr inline size_t key_len_v = name_of(M).size();

template<std::meta::info M>
constexpr inline auto key_v = fixed_string{name_of(M).data(),
    std::integral_constant<size_t, key_len_v<M>>{}};

// The methods interface `api` declares, as an `info_pack` in declaration
// order: its public, non-static, named, non-special member functions, and
// the deducing-this members that deduce for an lvalue of the interface.
//
// A deducing-this member is tried on a const interface object first, so
// that one taking a const object (a forwarding, by-value, or `const auto&`
// object parameter) declares a const method, and one constrained to a
// mutable object declares a mutable one. An explicit-object member admitting
// no lvalue declares nothing.
//
// `is_special_member_function` drops constructors and the like, which may
// carry an identifier; operators and conversion functions have none.
consteval std::meta::info method_pack(std::meta::info api) {
  std::vector<std::meta::info> ms;
  for (const auto m :
      std::meta::members_of(api, std::meta::access_context::unprivileged()))
  {
    if (!std::meta::has_identifier(m)) continue;
    const bool function =
        std::meta::is_function(m) && !std::meta::is_static_member(m) &&
        !std::meta::is_special_member_function(m);
    const bool function_template =
        std::meta::is_function_template(m) &&
        !std::meta::is_constructor_template(m);
    if (!function && !function_template) continue;

    auto f = deduce_object(m, std::meta::add_const(api));
    if (f == std::meta::info{}) f = deduce_object(m, api);
    if (f != std::meta::info{} && object_param_of(f) != object_param::unbound)
      ms.push_back(std::meta::reflect_constant(f));
  }

  return std::meta::substitute(^^info_pack, ms);
}

// Whether the entries `Es` include a `name` entry.
template<typename... Es>
constexpr inline bool has_name_entry_v = (entry_name<Es>::is_name || ...);

// `facade_maker` forms the `facade<...>` of facade type `F` from the method
// pack of its interface, followed by the extra entries `Es`.
//
// The name is `F`'s own identifier unless `Es` carries a `name` entry. The
// partials split on that, so the identifier is reflected only where it is
// used: a class template specialization has none (`has_identifier` is false,
// and `identifier_of` is not a constant expression), so it must carry a
// `name` entry. The consteval enumeration only collected reflections; every
// key and signature is formed here, at the type level, by pack expansion (gcc
// 16 rejects `substitute` inside a loop over reflected members).
template<typename F, typename Methods, typename... Es>
struct facade_maker;

template<typename F, std::meta::info... Ms, typename... Es>
requires has_name_entry_v<Es...>
struct facade_maker<F, info_pack<Ms...>, Es...> {
  using type = facade<method<key_v<Ms>, method_signature_t<Ms>>..., Es...>;
};

template<typename F, std::meta::info... Ms, typename... Es>
requires(!has_name_entry_v<Es...> && std::meta::has_identifier(^^F))
struct facade_maker<F, info_pack<Ms...>, Es...> {
  using type = facade<name<key_v<^^F>>,
      method<key_v<Ms>, method_signature_t<Ms>>..., Es...>;
};

// The diagnostic partial, for no `name` entry and no identifier to stand in
// for one.
//
// The placeholder name keeps the error to the one assertion, in place of the
// cascade that reflecting the missing identifier would add.
template<typename F, std::meta::info... Ms, typename... Es>
requires(!has_name_entry_v<Es...> && !std::meta::has_identifier(^^F))
struct facade_maker<F, info_pack<Ms...>, Es...> {
  static_assert(false,
      "this facade has no identifier to serve as its name (a class template "
      "specialization has none); add a name<> entry");
  using type = facade<name<"unnamed">,
      method<key_v<Ms>, method_signature_t<Ms>>..., Es...>;
};

#pragma endregion

} // namespace details

#pragma region Interface-first facades

// `reflected_facade` is the facade base for `F` spelled by the declarations
// of interface `Api`, creating an ordinary `facade<...>` that the rest of the
// machinery takes as it would a hand-written one.
//
// The `Api` interface holds plain member function declarations, which are
// never defined and never called. They are just the specification.
//
// The declaration grammar carries everything a `method<>` entry does: `void
// f() const` is a const method, `noexcept` is honored, and two declarations of
// one name are an overload set.
//
// A deducing-this member declares the method its object parameter admits:
// const when it takes a const interface object (a forwarding, by-value, or
// `const auto&` object parameter), mutable when it takes only a mutable one,
// and nothing when it takes no lvalue.
//
// Static members, constructors, operators, data members, and other templates
// are not methods.
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
// The interface need not be written for the purpose: any class serves. Its
// public member functions become the spec, so `reflected_facade<F, lawman>` is
// a facade over `lawman`'s whole public interface, bodies ignored.
//
// The name is `F`'s own identifier. A `name<>` entry among `Es` overrides it,
// for a facade whose identifier does not serve (two facades sharing one across
// namespaces, which must not collide in a composition), and is required for a
// facade that has none: a class template specialization is unnamed to
// reflection, and leaving the entry out is a `static_assert` saying so.
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
// overloads, and invokes the winner through its pointer with `std::invoke`.
//
// A deducing-this member is a candidate as the specialization the call would
// deduce for the target, and an explicit-object member as itself, each with
// the constness its object parameter admits. The target parameter is
// deduced, so constness flows through: a const target reaches only const
// members, and a const pair splits by handle constness.
//
// The result type and `noexcept` come from the declaration alone, so a
// conformance probe instantiates no body (a deducing-this member's aside,
// which gcc 16.2 compiles as its specialization forms), and a `noexcept`
// member stays `noexcept` through `call<>`. A key with no viable member is
// unbound, which conformance reports as it would for any other binding class.
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
// member functions: member function templates other than deducing-this ones,
// static members, data members, and anything on a non-class target. Nor is a
// base overload that a using-declaration un-hides beside the class's own
// (`using base::fire;` next to a `fire` of the class), since `members_of`
// does not report the using-declaration; such a key takes a `members<>`
// binding or an override.
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

// The reflected impl as the default impl of a pair whose facade has no
// `boilerplate`.
//
// Spelled here, at namespace scope, so the enumeration runs under the
// library's access context and this route binds public members only. A
// private member binds through a hook that names `reflected_impl<T>` itself.
//
// The `validate_api` probe is a target like any other: its `api` forwarders
// are deducing-this members, which bind by name, so a hand-written `api`
// over a reflected boilerplate validates at registration as one over a
// hand-written boilerplate does.
template<Facade F, typename T>
requires(!requires { typename F::template boilerplate<T>; })
struct default_impl<F, T> {
  using type = reflected_impl<T>;
};

} // namespace details

#pragma endregion
#pragma region The reflected sugar API

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
