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
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

#include "fixed_string.h"

// Registration-based runtime polymorphism ("proxy") system.
//
// Type-erased handles (`proxy`, `proxy_view`, `const_proxy_view`) over an
// interface definition (a facade), without inheritance, vtable pointers in the
// target type, or macros.
//
// Opting a class into a facade does not require modifying it, so you can
// retrofit a facade onto an existing type (even one in `std`), or have
// multiple facades over the same type.

// Some duplication currently exists in the facade definition, but it will
// collapse once reflection is available.
//
// A class's conformance to a facade is based on name, not shape, and
// registration is the act of conformance. This is in the same spirit as
// registered enums: registering a (facade, type) pair unlocks the facade's
// boilerplate impl, or carries a custom impl of its own.
//
// Facades compose through `extends<Base>` entries, and handles convert
// implicitly to handles of any facade theirs extends (Rust trait upcasting).
//
// See "proxy.md" for the design.
namespace corvid { inline namespace meta {
namespace prox {

#pragma region Method and key

// Method key.
//
// A tag carrying a method name as a compile-time string.
//
// `proxy_impl` bindings overload their `on` hook on it; it is how code
// canonically names a method, given that a library cannot mint a member with a
// caller-chosen name.
template<fixed_string Name>
struct method_key {
  static constexpr auto name_v = Name;
};

inline namespace literals {

// UDL for method keys: `"name"_method` is a `method_key<"name">`.
//
// This must be a literal operator template: the literal itself binds as the
// `fixed_string` template argument, which is what lets it reach NTTP
// position. The ordinary `(const char*, size_t)` form cannot work, because
// function parameters are runtime values, never constant expressions, even
// under `consteval`.
template<fixed_string Name>
consteval auto operator""_method() noexcept {
  return method_key<Name>{};
}

} // namespace literals

// Method descriptor: a name plus the erased signature.
//
// The signature is spelled like a member function's definition, using the same
// syntax that `std::function` takes. So, for example, it would look like
// `std::string(int)`. Or, for `const this`, it would be `std::string(int)
// const`.
//
// The signature fixes the erased ABI; a binding may return the declared result
// type or anything convertible to it.
//
// A `noexcept` qualifier is likewise honored. Conformance then requires the
// binding itself to be noexcept-invocable, and the erased call (`call` through
// a handle) is itself `noexcept`.
//
// A method derives from its `method_key`, so a method tag is usable anywhere
// its key is, including `on` overload selection and deduction of
// `method_key<K>` from a method argument. This also leaves the door open for
// bindings that overload on the full method (signature included) if per-name
// overload sets are ever supported.
namespace details {

// Shared base for the four `method` flavors (`const` crossed with
// `noexcept`).
//
// It carries the qualification flags and the result type, which are the only
// things that vary, and supplies the `method_key` base.
template<fixed_string Name, bool Const, bool Noexcept, typename R>
struct method_base: method_key<Name> {
  static constexpr bool const_v = Const;
  static constexpr bool noexcept_v = Noexcept;
  using result_t = R;
};

} // namespace details

template<fixed_string Name, typename Sig>
struct method;

template<fixed_string Name, typename R, typename... Args>
struct method<Name, R(Args...)>: details::method_base<Name, false, false, R> {
};

template<fixed_string Name, typename R, typename... Args>
struct method<Name, R(Args...) const>
    : details::method_base<Name, true, false, R> {};

template<fixed_string Name, typename R, typename... Args>
struct method<Name, R(Args...) noexcept>
    : details::method_base<Name, false, true, R> {};

template<fixed_string Name, typename R, typename... Args>
struct method<Name, R(Args...) const noexcept>
    : details::method_base<Name, true, true, R> {};

#pragma endregion
#pragma region Facade

// Facade base: an interface definition.
//
// Derive a named struct from `facade`, listing the methods. For example:
//
//    struct animal : facade<method<"speak", void() const>> {};
//
// The derived type, `animal` here, IS the facade: it is the `F` in
// `proxy_view<F>` and `proxy_impl<F, T>`, and the type registration keys on.
//
// Because identity is this named type rather than the method list, same-named
// methods in unrelated facades cannot cross-contaminate. This is also why we
// inherit from `facade` instead of just aliasing it.
//
// The facade body is where the optional member-call sugar is authored: a
// nested `api` type holding one deducing-this forwarder per method, which
// every handle of the facade inherits. For example:
//
//    struct animal : facade<method<"speak", void() const>> {
//      struct api {
//        void speak(this const auto& self) {
//          self.template call<"speak">();
//        }
//      };
//      template<typename T>
//      struct boilerplate : prox_impl {
//        static int on(method_key<"speak">, const T& t) {
//          return t.speak();
//        }
//      };
//    };
//
// With that in place, `handle.speak()` is sugar for `handle.call<"speak">()`,
// dispatching through the same table. See "proxy.md" for the design and its
// limits.
//
// The facade body is also the normal home of the boilerplate impl; see also
// `proxy_impl`.
template<typename... Methods>
struct facade {};

namespace details {

// Probe for the unique public `facade` base of `F`. Declared only; used in
// unevaluated contexts.
template<typename... Ms>
auto probe(const facade<Ms...>&) -> facade<Ms...>;

} // namespace details

// Concept for a type derived from a single `facade` base.
template<typename F>
concept Facade = requires(const F& f) { details::probe(f); };

// Facade composition entry: the facade extends `B` (Rust supertrait, ngcpp
// `add_facade`).
//
// Listed in the facade's method list alongside the methods, conventionally
// first. For example:
//
//    struct pet : facade<extends<animal>, method<"play", void()>> {};
//
// The derived facade's effective method list is the flattening of its bases'
// lists, in declaration order, followed by its own; handles of the derived
// facade dispatch inherited and own methods alike.
//
// Names must be unique across the flattened list. A facade cannot redeclare
// (or override) an inherited method, which is by design, because facades carry
// no implementations and there is nothing to override. Diamond composition is
// also rejected, but that is a limit of the current implementation rather than
// the model, with the dedup path recorded under Future in "proxy.md".
//
// Conformance is per facade, as with Rust supertraits: a type conforms to
// `pet` by binding `pet`'s own methods through `proxy_impl<pet, T>` and
// conforming to `animal` in the usual way. An inherited method's behavior is
// therefore defined once, by the base facade's impl, and an upcast handle
// cannot disagree with the derived one. A type relying on boilerplate impls
// registers once per facade in the chain, normally with a single chain hook
// (see `InChainOf`).
//
// Handles convert implicitly to handles of any facade theirs extends (Rust
// trait upcasting); see the view constructors.
template<Facade B>
struct extends {};

namespace details {

// Stand-in API base for facades that define no member-call sugar.
struct no_api {};

// Sugar base for handles of facade `F`.
//
// Yields the facade's nested `api` when it defines one, and the empty stand-in
// otherwise. The selection has to be lazy (a specialization rather than a
// `std::conditional_t`), because naming `F::api` when it does not exist is
// ill-formed.
template<typename F>
struct api_base {
  using type = no_api;
};

template<typename F>
requires(requires { typename F::api; })
struct api_base<F> {
  using type = F::api;
};

template<typename F>
using api_base_t = api_base<F>::type;

} // namespace details

#pragma endregion
#pragma region Registration and binding

// Convenience base for binding classes: a facade's `boilerplate`, or a
// registration-carried impl.
//
// Its sole member is a `method_key` alias, which lets a binding class spell
// its `on` parameters without qualification: base-class members participate
// in unqualified lookup, where the enclosing namespace's names do not.
// Inheriting it is optional, and a binding class that inherits a boilerplate
// already has it through that base. It also serves as documentation of what
// the class is for.
struct prox_impl {
  template<fixed_string Name>
  using method_key = prox::method_key<Name>;
};

// Binding point between a facade and a concrete type (Rust's `impl Trait for
// Type`).
//
// Intentionally undefined. A binding provides one static `on` overload per
// facade method, taking the method's `key`, the target (const-qualified per
// the method), and the method's arguments. Registration is the sole act of
// conformance: it either unlocks the facade's boilerplate or carries the
// pair's impl itself, and library partial specializations of this template
// install whichever applies.
//
// A facade author typically hosts a boilerplate impl inside the facade
// itself: a nested class template named `boilerplate`, generic over any
// registered `T` whose member names line up.
//
// A type whose names do not line up carries its own impl in the registration
// instead: `make_proxy_spec<F, T, Impl>()` names a binding class, which can
// live anywhere a type can: local to the hook itself (a fully self-contained
// registration, and the only self-contained option for a type you do not
// own), nested in the type it serves (which additionally grants access to
// the type's private members), or at namespace scope.
//
// A carried impl outranks the boilerplate, being the more specific
// declaration. The namespace-scope spelling of the boilerplate, a `proxy_impl`
// partial specialization gated on `ProxyRegistered`, remains equivalent and
// supported (to preserve carried-impl precedence, its gate should also
// require `!SpecCarriesImpl<F, T>`); it is the one user-authored
// specialization of this template in the supported surface.
//
// Unregistered per-type full specializations, the original wrong-names tier,
// are subsumed by carried impls and no longer supported.
//
// Because the nested boilerplate is an ordinary inheritable class, it also
// enables partial overrides: a type whose names line up except for one method
// registers a carried impl that inherits `F::boilerplate<T>`, re-exposes its
// `on` overloads with a using-declaration, and declares only the divergent
// binding.
//
// See "proxy.md" for the mechanics and "proxy_test.cpp" for
// examples (`sheriff`, `turncoat`).
//
// For example:

//    // Facade author's boilerplate, written once, serving any registered
//    // type whose member names line up. The `api` is omitted here for
//    // brevity. Inheriting `prox_impl` supplies the unqualified `method_key`
//    // spelling.
//    struct animal : facade<method<"speak", void() const>> {
//      template<typename T> struct boilerplate : prox_impl {
//        static void on(method_key<"speak">, const T& t) { t.speak(); }
//      };
//    };
//
//    // Conforming `dog`, whose names line up: pure registration.
//    consteval auto corvid_proxy_spec(animal*, dog*) {
//      return make_proxy_spec<animal, dog>();
//    }
//
//    // Conforming `cat`, whose names differ: the registration carries the
//    // impl, here local to the hook itself.
//    consteval auto corvid_proxy_spec(animal*, cat*) {
//      struct as_animal : prox_impl {
//        static void on(method_key<"speak">, const cat& c) { c.meow(); }
//      };
//      return make_proxy_spec<animal, cat, as_animal>();
//    }
template<typename F, typename T>
struct proxy_impl;

// Minimal registration spec.
//
// The value a registration hook returns. Distinct from `corvid_proxy_spec`,
// which is the hook itself, provided by the user.
//
// `Impl`, when not `void`, is a registration-carried impl: a binding class the
// registration names, which the library installs as the pair's `proxy_impl`
// (see `SpecCarriesImpl`). Because each hook's return type is deduced
// independently, richer spec types can be added later without touching
// existing registrations.
template<typename F, typename T, typename Impl = void>
struct proxy_spec {
  using impl_t = Impl;
};

namespace details {

// Forward declaration; defined under "API validation".
template<Facade F>
struct api_probe;

} // namespace details

// Forward declaration; defined under "API validation".
template<Facade F>
[[nodiscard]] consteval bool validate_api() noexcept;

// Whether registering a pair also validates the facade's `api` (see
// `validate_api`). A facade whose `api` deliberately deviates from the method
// list (say, a widening convenience signature), must register with
// `api_check::off`.
enum class api_check : std::uint8_t { off, on };

namespace details {

// Registration-time half of `validate_api`: run the check when the facade
// has an `api` and the registration has not opted out.
template<typename F, api_check Check>
consteval void maybe_validate_api() noexcept {
  if constexpr (Check == api_check::on && Facade<F> &&
                requires { typename F::api; })
  {
    constexpr bool has_boilerplate = requires {
      sizeof(proxy_impl<F, api_probe<F>>);
    };
    static_assert(has_boilerplate,
        "validating the api needs the facade's boilerplate impl visible at "
        "the registration; pass api_check::off to skip");
    if constexpr (has_boilerplate) (void)validate_api<F>();
  }
}

} // namespace details

// Make the registration spec for a (facade, type) pair.
//
// Call this from a `corvid_proxy_spec` overload, just as
// `make_sequence_enum_spec` is returned from `corvid_enum_spec`.
//
// The three-type overload carries an impl: `make_proxy_spec<F, T, Impl>()`
// names the binding class serving the pair, typically a local class in the
// hook itself or a class nested in `T`; see `proxy_impl` and
// `SpecCarriesImpl`. `api_check` is the trailing, defaulted parameter of both
// overloads.
//
// When the facade defines an `api`, registration is also the moment that the
// `api` is validated against the method list (see `validate_api`), because the
// boilerplate impl the registration exists to unlock must be visible here,
// along with the facade and its `api`.
//
// Correctness checking is opt-out rather than opt-in: pass `api_check::off` to
// skip. A registration hook that is itself a template defers the check to its
// own instantiation.
template<typename F, typename T, api_check Check = api_check::on>
[[nodiscard]] consteval proxy_spec<F, T> make_proxy_spec() noexcept {
  details::maybe_validate_api<F, Check>();
  return {};
}

template<typename F, typename T, typename Impl,
    api_check Check = api_check::on>
[[nodiscard]] consteval proxy_spec<F, T, Impl> make_proxy_spec() noexcept {
  details::maybe_validate_api<F, Check>();
  return {};
}

// Concept for a (facade, type) pair being registered.
//
// To register a pair, declare a `corvid_proxy_spec(F*, T*)` overload returning
// `make_proxy_spec<F, T>()`, in the namespace of either the facade or the
// type; it is found here by ADL.
//
// To register a type for a composed facade and its whole chain, one
// constrained template hook serves every level; see `InChainOf`.
//
// The library never defines this function: declaring an overload IS the act of
// registration. The single exception is the overload the library provides for
// its own API-validation probe; see `validate_api`.
//
// Registration is the sole act of conformance: it unlocks the facade's
// boilerplate, or carries the pair's impl itself.
template<typename F, typename T>
concept ProxyRegistered = requires {
  corvid_proxy_spec(static_cast<F*>(nullptr), static_cast<T*>(nullptr));
};

// Concept for a (facade, type) pair whose registration carries an impl.
//
// True when the pair is registered through the three-type
// `make_proxy_spec<F, T, Impl>()`, which names the binding class serving the
// pair.
//
// The library installs the carried impl as the pair's `proxy_impl` (below),
// outranking the facade's boilerplate: the carried impl is the more specific
// declaration, closer to the type.
//
// A namespace-scope boilerplate partial should include `!SpecCarriesImpl<F,
// T>` in its gate to preserve that precedence, since partial ordering would
// otherwise prefer it.
template<typename F, typename T>
concept SpecCarriesImpl =
    ProxyRegistered<F, T> &&
    !std::is_void_v<typename decltype(corvid_proxy_spec(
        static_cast<F*>(nullptr), static_cast<T*>(nullptr)))::impl_t>;

namespace details {

// Impl type carried by the pair's registration.
template<typename F, typename T>
using registered_impl_t = decltype(corvid_proxy_spec(static_cast<F*>(nullptr),
    static_cast<T*>(nullptr)))::impl_t;

} // namespace details

// Library-provided delegation to a registration-carried impl.
//
// The registration names the binding class, so the impl is explicitly
// registered like everything else, and the class itself can be local to the
// hook or nest inside the type it serves (a hook is implicitly inline, so a
// hook-local class is ODR-consistent across translation units).
template<Facade F, typename T>
requires SpecCarriesImpl<F, T>
struct proxy_impl<F, T>: details::registered_impl_t<F, T> {};

// Library-provided delegation to a facade-hosted boilerplate impl.
//
// When a facade defines a nested `boilerplate` class template and the
// (facade, type) pair is registered, this serves as the pair's `proxy_impl`,
// so the boilerplate lives next to the method list and `api` it mirrors and
// the facade needs no namespace-scope impl at all.
//
// A registration-carried impl outranks it (the exclusion below is what
// arbitrates), and a facade author's own namespace-scope partial outranks both
// by the ordinary specialization-ordering rules.
template<Facade F, typename T>
requires(ProxyRegistered<F, T> && !SpecCarriesImpl<F, T> &&
         requires { typename F::template boilerplate<T>; })
struct proxy_impl<F, T>: F::template boilerplate<T> {};

// Central access point for the registered spec, mirroring `enum_spec_v`.
//
// Each specialization deduces its own type, so richer spec types can be
// introduced later without touching existing registrations.
template<typename F, typename T>
requires ProxyRegistered<F, T>
constexpr inline auto proxy_spec_v =
    corvid_proxy_spec(static_cast<F*>(nullptr), static_cast<T*>(nullptr));

#pragma endregion
#pragma region Dispatch details

namespace details {

// Per-method dispatch machinery, shared by the four erased-signature flavors
// (`const` crossed with `noexcept`).
//
// Contains the thunk pointer type, the compile-time check that a `proxy_impl`
// binding exists, and the thunk itself, which is where the concrete type is
// seen for the last time before erasure.
//
// A const method sees the target as `const T&` and erases it as `const
// void*`. A noexcept method additionally requires the binding to be
// noexcept-invocable, and its thunk pointer type carries `noexcept` through
// the erased ABI.
template<fixed_string Name, bool Const, bool Noexcept, typename R,
    typename... Args>
struct method_traits_base {
  template<typename T>
  using target_t = std::conditional_t<Const, const T, T>;
  using erased_ptr_t = std::conditional_t<Const, const void*, void*>;
  using thunk_ptr_t = R (*)(erased_ptr_t, Args...) noexcept(Noexcept);

  // Parameter list normalized for exact-match probing: top-level cv and
  // references stripped from each parameter, so value-category spelling is
  // ignored but a merely-convertible type does not match.
  using norm_args_t = std::tuple<std::remove_cvref_t<Args>...>;

  template<typename F, typename T>
  static constexpr bool bound_v = requires(target_t<T>& t, Args... args) {
    {
      proxy_impl<F, T>::on(method_key<Name>{}, t, std::forward<Args>(args)...)
    } -> std::convertible_to<R>;
  } && (!Noexcept || requires(target_t<T>& t, Args... args) {
    {
      proxy_impl<F, T>::on(method_key<Name>{}, t, std::forward<Args>(args)...)
    } noexcept;
  });

  template<typename F, typename T>
  static consteval thunk_ptr_t make_thunk() noexcept {
    return [](erased_ptr_t target, Args... args) noexcept(Noexcept) -> R {
      return proxy_impl<F, T>::on(method_key<Name>{},
          *static_cast<target_t<T>*>(target), std::forward<Args>(args)...);
    };
  }
};

template<typename M>
struct method_traits;

template<fixed_string Name, typename R, typename... Args>
struct method_traits<method<Name, R(Args...)>>
    : method_traits_base<Name, false, false, R, Args...> {};

template<fixed_string Name, typename R, typename... Args>
struct method_traits<method<Name, R(Args...) const>>
    : method_traits_base<Name, true, false, R, Args...> {};

template<fixed_string Name, typename R, typename... Args>
struct method_traits<method<Name, R(Args...) noexcept>>
    : method_traits_base<Name, false, true, R, Args...> {};

template<fixed_string Name, typename R, typename... Args>
struct method_traits<method<Name, R(Args...) const noexcept>>
    : method_traits_base<Name, true, true, R, Args...> {};

// Inline (SBO) storage parameters for the owning `proxy`.
//
// A target is stored inline when it fits the buffer, is no more aligned than
// `std::max_align_t`, and is nothrow-move-constructible (a proxy move
// relocates an inline target, and proxy moves are unconditionally
// `noexcept`). Anything else lives in a unique-owned heap allocation.
inline constexpr std::size_t sbo_size = 2 * sizeof(void*);

template<typename T>
constexpr inline bool sbo_eligible_v =
    sizeof(T) <= sbo_size && alignof(T) <= alignof(std::max_align_t) &&
    std::is_nothrow_move_constructible_v<T>;

// Housekeeping thunks for the owning `proxy`, the analog of Rust's drop glue.
//
// `sbo_relocate` move-constructs `*from` into `to` and destroys the source.
// The heap path has none because a heap target moves by pointer steal.
template<typename T>
void sbo_destroy(void* target) noexcept {
  static_cast<T*>(target)->~T();
}

template<typename T>
void heap_destroy(void* target) noexcept {
  delete static_cast<T*>(target);
}

template<typename T>
void sbo_relocate(void* from, void* to) noexcept {
  auto* source = static_cast<T*>(from);
  ::new (to) T(std::move(*source));
  source->~T();
}

// Facade-wide dispatch machinery, specialized on the `facade` base to get at
// the entry pack: the flattened method list, name-to-slot lookup, the
// dispatch table type, the all-methods-bound check behind `Proxiable`, and
// the table builders.
template<typename FB>
struct vtable_builder;

// Facade-wide machinery for the derived facade type `F`.
template<Facade F>
using vtbuild_t = vtable_builder<decltype(probe(std::declval<const F&>()))>;

// Per-(facade, type) dispatch table instance. The handle stores a pointer to
// this; the table pointer lives in the handle, not in the target (a fat
// handle, like Rust's `&dyn`). A composed facade's table also embeds the
// address of each direct base's instance for the same target type, which is
// what makes upcasting a pointer read.
template<Facade F, typename T>
constexpr inline auto vtable_for = vtbuild_t<F>::template make_vtable<F, T>();

// Per-(facade, type) owning dispatch table instance, for `proxy`.
template<Facade F, typename T>
constexpr inline auto owning_vtable_for =
    vtbuild_t<F>::template make_owning_vtable<F, T>();

// Slot index of the first method in `Ms` named `K`, or `sizeof...(Ms)` (as
// an `npos` or `end` flag) when there is none.
template<fixed_string K, typename... Ms>
consteval std::size_t first_index_of() noexcept {
  constexpr std::array<bool, sizeof...(Ms)> matches{(Ms::name_v == K)...};
  for (std::size_t ndx = 0; ndx != matches.size(); ++ndx)
    if (matches[ndx]) return ndx;
  return sizeof...(Ms);
}

// Whether every method name in `Ms` is unique: each name's first occurrence
// must be its own slot. Duplicates arise from composition (an inherited name
// redeclared, or a diamond) as well as from a doubled declaration.
template<typename... Ms>
consteval bool unique_method_names() noexcept {
  std::size_t ndx = 0;
  return (... && (first_index_of<Ms::name_v, Ms...>() == ndx++));
}

// Traits over one entry of a facade's declaration list, which mixes `method`
// descriptors with `extends` composition entries.
//
// Each entry contributes methods to the flattened method list, base facades
// to the direct-base list, thunk slots and base-table pointers to the
// dispatch table, and a conformance term to `all_bound_v`.
//
// A `method` contributes itself, bound through `proxy_impl<F, T>`. An
// `extends<B>` contributes B's already-flattened methods, with the thunks
// copied from B's own table and conformance delegated to `B`, so an inherited
// method's binding always comes from the facade that declared it and an upcast
// handle cannot disagree with the derived one.
template<typename M>
struct entry_traits {
  using methods_t = std::tuple<M>;
  using bases_t = std::tuple<>;

  template<typename F, typename T>
  static constexpr bool bound_v = method_traits<M>::template bound_v<F, T>;

  template<typename F, typename T>
  static consteval auto thunks() noexcept {
    return std::tuple{method_traits<M>::template make_thunk<F, T>()};
  }

  template<typename T>
  static consteval std::tuple<> base_vtables() noexcept {
    return {};
  }
};

template<Facade B>
struct entry_traits<extends<B>> {
  using methods_t = vtbuild_t<B>::flat_methods_t;
  using bases_t = std::tuple<B>;

  template<typename F, typename T>
  static constexpr bool bound_v = vtbuild_t<B>::template all_bound_v<B, T>;

  template<typename F, typename T>
  static consteval auto thunks() noexcept {
    return vtable_for<B, T>.thunks;
  }

  template<typename T>
  static consteval auto base_vtables() noexcept {
    return std::tuple{&vtable_for<B, T>};
  }
};

// Flattened method list and direct-base list of a facade's entry pack.
template<typename... Es>
using flat_methods_of_t = decltype(std::tuple_cat(
    std::declval<typename entry_traits<Es>::methods_t>()...));

template<typename... Es>
using bases_of_t = decltype(std::tuple_cat(
    std::declval<typename entry_traits<Es>::bases_t>()...));

// The flattened core of the builder, over the full method list `Ms` (bases'
// methods first, in declaration order, then own) and the direct-base facades
// `Bs`.
template<typename FlatMethods, typename Bases>
struct vtable_builder_impl;

template<typename... Ms, typename... Bs>
struct vtable_builder_impl<std::tuple<Ms...>, std::tuple<Bs...>> {
  static_assert(unique_method_names<Ms...>(),
      "method names must be unique across the facade and its extends bases");

  using flat_methods_t = std::tuple<Ms...>;
  static constexpr std::size_t count_v = sizeof...(Ms);
  static constexpr std::size_t base_count_v = sizeof...(Bs);

  // Direct-base facade at index `Ndx`.
  template<std::size_t Ndx>
  using base_t = std::tuple_element_t<Ndx, std::tuple<Bs...>>;

  using thunks_t = std::tuple<typename method_traits<Ms>::thunk_ptr_t...>;

  // Dispatch table: one thunk slot per flattened method, plus the address of
  // each direct base's table for the same target type.
  //
  // Dispatch always uses the flattened thunks, so an inherited call costs the
  // same single indexed load as an own one; the base pointers exist for
  // upcasting to follow (Rust's embedded supertrait vtables).
  struct vtable_t {
    thunks_t thunks;
    std::tuple<const typename vtbuild_t<Bs>::vtable_t*...> bases;
  };

  // Whether this facade transitively extends facade `B`. Strict: the search
  // covers the bases and their bases, never this facade itself, so a
  // self-match is false.
  template<typename B>
  static consteval bool extends_facade() noexcept {
    return (
        (std::same_as<B, Bs> || vtbuild_t<Bs>::template extends_facade<B>()) ||
        ...);
  }

  // Index of the first direct base that is `B` or extends it, or
  // `base_count_v` when there is none. This is the route `upcast_vtable`
  // descends.
  template<typename B>
  static consteval std::size_t base_route() noexcept {
    constexpr std::array<bool, sizeof...(Bs)> matches{(
        std::same_as<B, Bs> ||
        vtbuild_t<Bs>::template extends_facade<B>())...};
    for (std::size_t ndx = 0; ndx != matches.size(); ++ndx)
      if (matches[ndx]) return ndx;
    return base_count_v;
  }

  // Slot index of the method named `K`, or `count_v` (as an `npos` or `end`
  // flag) when there is none.
  template<fixed_string K>
  static consteval std::size_t index_of() noexcept {
    return first_index_of<K, Ms...>();
  }

  // Qualification of the method named `K`. Both are `false` when no method
  // has that name, since rejecting an unknown name is `index_of`'s job.
  template<fixed_string K>
  static consteval bool is_const() noexcept {
    constexpr std::array<bool, sizeof...(Ms)> flags{Ms::const_v...};
    constexpr auto ndx = index_of<K>();
    return ndx != count_v && flags[ndx];
  }

  template<fixed_string K>
  static consteval bool is_noexcept() noexcept {
    constexpr std::array<bool, sizeof...(Ms)> flags{Ms::noexcept_v...};
    constexpr auto ndx = index_of<K>();
    return ndx != count_v && flags[ndx];
  }

  // Declared result type of the method named `K`, or `void` when no method has
  // that name. The permissive fallback keeps return-type substitution in the
  // `api_probe` from hard-erroring before its constraint can reject the
  // unknown name.
  template<fixed_string K>
  static consteval auto do_result_of() noexcept {
    constexpr auto ndx = index_of<K>();
    if constexpr (ndx != count_v) {
      using m_t = std::tuple_element_t<ndx, std::tuple<Ms...>>;
      return std::type_identity<typename m_t::result_t>{};
    } else {
      return std::type_identity<void>{};
    }
  }

  template<fixed_string K>
  using result_of_t = decltype(do_result_of<K>())::type;

  // Whether `Args` match the declared parameters of the method named `K`
  // exactly after normalization, rather than by convertibility. False for an
  // unknown name. This is the `api_probe`'s strictness.
  template<fixed_string K, typename... Args>
  static consteval bool exact_args() noexcept {
    constexpr auto ndx = index_of<K>();
    if constexpr (ndx != count_v) {
      using m_t = std::tuple_element_t<ndx, std::tuple<Ms...>>;
      return std::same_as<std::tuple<std::remove_cvref_t<Args>...>,
          typename method_traits<m_t>::norm_args_t>;
    } else {
      return false;
    }
  }
};

template<typename... Es>
struct vtable_builder<facade<Es...>>
    : vtable_builder_impl<flat_methods_of_t<Es...>, bases_of_t<Es...>> {
  using impl_t =
      vtable_builder_impl<flat_methods_of_t<Es...>, bases_of_t<Es...>>;
  using vtable_t = impl_t::vtable_t;

  // Whether every method of the facade, inherited and own, has a usable
  // binding for `T`. Own methods bind through `proxy_impl<F, T>`; inherited
  // ones recurse into their declaring facade, so conforming to a composed
  // facade requires conforming to each of its bases.
  template<typename F, typename T>
  static constexpr bool all_bound_v =
      (entry_traits<Es>::template bound_v<F, T> && ...);

  template<typename F, typename T>
  static consteval vtable_t make_vtable() noexcept {
    return {std::tuple_cat(entry_traits<Es>::template thunks<F, T>()...),
        std::tuple_cat(entry_traits<Es>::template base_vtables<T>()...)};
  }

  // Owning dispatch table, carrying housekeeping slots alongside the facade
  // methods. A null `relocate` marks a heap-stored target, which moves by
  // pointer steal rather than relocation.
  struct owning_vtable_t {
    vtable_t vt;
    void (*destroy)(void*) noexcept;
    void (*relocate)(void*, void*) noexcept;
  };

  template<typename F, typename T>
  static consteval owning_vtable_t make_owning_vtable() noexcept {
    if constexpr (sbo_eligible_v<T>)
      return {make_vtable<F, T>(), &sbo_destroy<T>, &sbo_relocate<T>};
    else
      return {make_vtable<F, T>(), &heap_destroy<T>, nullptr};
  }
};

// Narrow a dispatch-table pointer from facade `D` to `B`, where `B` is `D`
// itself or a facade it extends, by following the embedded direct-base table
// pointers. The route is resolved at compile time; the runtime cost is one
// dependent load per composition level crossed.
template<Facade B, Facade D>
[[nodiscard]] constexpr auto
upcast_vtable(const typename vtbuild_t<D>::vtable_t* vt) noexcept
    -> const vtbuild_t<B>::vtable_t* {
  if constexpr (std::same_as<B, D>) {
    return vt;
  } else {
    constexpr auto ndx = vtbuild_t<D>::template base_route<B>();
    static_assert(ndx != vtbuild_t<D>::base_count_v,
        "the source facade does not extend the target facade");
    return upcast_vtable<B, typename vtbuild_t<D>::template base_t<ndx>>(
        std::get<ndx>(vt->bases));
  }
}

} // namespace details

#pragma endregion
#pragma region Proxiable

// Concept: `T` can back facade `F`.
//
// Satisfied when a usable `proxy_impl<F, T>` binding exists for every method
// of `F`.
//
// An explicit specialization satisfies it directly; a facade author's
// boilerplate satisfies it exactly when the pair is registered.
//
// This is the gate on proxy construction and doubles as the trait bound for
// static-dispatch templates.
template<typename T, typename F>
concept Proxiable =
    Facade<F> && details::vtbuild_t<F>::template all_bound_v<F, T>;

// Concept: facade `D` (transitively) extends facade `B` through `extends`
// composition entries.
//
// Strict: false when `D` is `B` itself. Constraints that mean "B or anything
// extending it" pair this with `std::same_as`.
template<typename D, typename B>
concept Extends =
    Facade<D> && Facade<B> &&
    details::vtbuild_t<D>::template extends_facade<B>();

// Concept: facade `B` is `D` itself or a facade `D` (transitively) extends.
//
// This is `Extends` made reflexive and argument-flipped, so it can constrain
// a registration hook's facade parameter. A single template hook
//
//    template<prox::InChainOf<pet> F>
//    consteval auto corvid_proxy_spec(F*, dog*) {
//      return prox::make_proxy_spec<F, dog>();
//    }
//
// registers `dog` for `pet` and every facade it extends in one declaration,
// the idiomatic spelling for conforming a type to a whole composition chain.
// The bindings stay per facade either way; the chain hook only collapses the
// opt-in ceremony.
template<typename B, typename D>
concept InChainOf = Facade<B> && (std::same_as<B, D> || Extends<D, B>);

// Forward declarations of the three erased-handle flavors, for the trait
// below and the cross-handle constructors.
template<Facade F>
class proxy_view;
template<Facade F>
class const_proxy_view;
template<Facade F>
class proxy;

namespace details {

// Facade of a proxy handle type, or `void` for any other type.
template<typename T>
struct handle_facade {
  using type = void;
};
template<Facade F>
struct handle_facade<proxy_view<F>> {
  using type = F;
};
template<Facade F>
struct handle_facade<const_proxy_view<F>> {
  using type = F;
};
template<Facade F>
struct handle_facade<proxy<F>> {
  using type = F;
};

// Whether `T` is a proxy handle whose facade is `F` or extends it: the set
// the dedicated viewing and upcasting constructors serve.
//
// The generic erased-target constructors exclude it, so re-pointing at such a
// handle's target always wins over wrapping the handle itself as a target.
template<typename T, typename F>
consteval bool is_handle_for() noexcept {
  using G = handle_facade<T>::type;
  if constexpr (std::is_void_v<G>)
    return false;
  else
    return std::same_as<G, F> || Extends<G, F>;
}

} // namespace details

#pragma endregion
#pragma region API validation

namespace details {

// Exact-conversion carrier for the `api_probe`'s strict `call`.
//
// The conversion operator is a template constrained to exactly `R`. Deduction
// runs against the target type, which in a forwarder's `return` statement is
// the forwarder's declared result type, so a declared type that is merely
// convertible to `R` leaves no viable conversion and fails to compile. Probe
// machinery is only ever type-checked, never executed.
template<typename R>
struct strict_result {
  template<typename U>
  requires std::same_as<U, R>
  operator U() {
    std::terminate();
  }
};

// Result type of the probe's strict `call`: `strict_result` for object types,
// `void` for `void`, and references passed through unchanged.
//
// A conversion operator cannot distinguish binding a reference from copying
// out of one, so reference results cannot be exactness-checked; a forwarder
// that decays a declared reference result to a value goes undetected here.
template<typename R>
using strict_return_t = std::conditional_t<std::is_void_v<R>, void,
    std::conditional_t<std::is_reference_v<R>, R, strict_result<R>>>;

// Synthetic dispatch target for `validate_api`.
//
// It inherits the facade's `api` and exposes a deliberately strict `call`.
// Argument types must match the method's declared parameters exactly (per
// `exact_args`), and the result converts only to exactly the declared result
// type. Never constructed or executed; it exists to be type-checked.
template<Facade F>
struct api_probe: api_base_t<F> {
  template<fixed_string K, typename... Args>
  requires(vtbuild_t<F>::template exact_args<K, Args...>())
  strict_return_t<typename vtbuild_t<F>::template result_of_t<K>>
  call(Args&&...) {
    std::terminate();
  }

  template<fixed_string K, typename... Args>
  requires(vtbuild_t<F>::template is_const<K>() &&
           vtbuild_t<F>::template exact_args<K, Args...>())
  // NOLINTNEXTLINE(modernize-use-nodiscard): mirrors `call`, never executed.
  strict_return_t<typename vtbuild_t<F>::template result_of_t<K>>
  call(Args&&...) const {
    std::terminate();
  }
};

// Probe registration, covering every facade.
//
// This is the only overload of `corvid_proxy_spec` the library itself
// provides; it is what admits the probe to a facade author's
// registration-gated boilerplate impl.
//
// The probe of a facade also registers for every facade its facade extends,
// because validating a composed facade drives each base's boilerplate at the
// derived probe. It must opt out of validation: a validating registration
// would recurse into itself through the boilerplate-visibility check, whose
// `ProxyRegistered` probe deduces this hook's return type.
template<Facade F, Facade G>
requires(std::same_as<F, G> || Extends<G, F>)
consteval auto corvid_proxy_spec(F*, api_probe<G>*) noexcept {
  return make_proxy_spec<F, api_probe<G>, api_check::off>();
}

} // namespace details

// Validate a facade's `api` against its method list, spelling neither the
// names nor the signatures again.
//
// This runs automatically from `make_proxy_spec` at every registration of an
// `api`-bearing facade, unless the registration opts out with
// `api_check::off`.
//
// The standalone spelling, `static_assert(validate_api<my_facade>());`,
// remains for a facade author to assert at the definition site, before any
// registration exists.
//
// It works by dispatching the facade author's boilerplate impl at a probe
// target that inherits the `api`, chaining thunk -> boilerplate `on` -> `api`
// forwarder -> strict probe `call`. The chain is anchored to the facade's
// exact declared types at both ends, so convertibility drift anywhere in the
// middle, in the `api` or in the boilerplate itself, fails to compile with the
// error pointing at the drifting line.
//
// Caught: a missing or misspelled forwarder, wrong arity, wrong const flavor
// of `self`, a parameter or declared result type that is merely convertible to
// the facade's (including the silently-truncating kind), and a forwarder body
// dispatching a key with a different signature. Not caught: a missing
// `noexcept` on a forwarder, by-value versus by-reference parameter spellings,
// reference-to-value decay of a declared result, and a body dispatching the
// wrong key with an identical signature.
//
// Failures are hard compile errors rather than a `false` return, and the
// facade must have a boilerplate impl (a facade-hosted nested `boilerplate`,
// or a namespace-scope `proxy_impl` partial gated on `ProxyRegistered`) for
// the chain to exist.
//
// For a facade with `extends` bases, the chain also runs through each base's
// boilerplate, so the inherited forwarders (typically brought in by deriving
// the `api` from the base facade's) are validated against the flattened
// method list as well.
template<Facade F>
[[nodiscard]] consteval bool validate_api() noexcept {
  const auto vt = details::vtable_for<F, details::api_probe<F>>;
  (void)vt;
  return true;
}

#pragma endregion
#pragma region Views

namespace details {

// Storage and const-method dispatch shared by the two view flavors, which
// differ only in the constness of the erased target pointer.
//
// The `call` here serves const-qualified methods, the only dispatch a const
// handle allows. The `proxy_view` class layers the unrestricted non-const
// overload on top.
//
// This is also where the views pick up the facade's `api` base, keeping each
// view a single-inheritance chain.
template<Facade F, bool Const>
class view_base: public api_base_t<F> {
public:
  using facade_t = F;

  // Call the const-qualified facade method named `K`, forwarding `args`
  // through the erased signature.
  //
  // The call is `noexcept` when the method is. It is not `[[nodiscard]]`,
  // because discardability belongs to the facade method rather than the
  // dispatcher (the `std::invoke` precedent).
  template<fixed_string K, typename... Args>
  requires(vtbuild_t<F>::template is_const<K>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  constexpr decltype(auto) call(Args&&... args) const
      noexcept(vtbuild_t<F>::template is_noexcept<K>()) {
    constexpr auto ndx = vtbuild_t<F>::template index_of<K>();
    static_assert(ndx != vtbuild_t<F>::count_v, "no matching signature");
    return std::get<ndx>(
        vtable_->thunks)(target_, std::forward<Args>(args)...);
  }

  // An empty view (default-constructed) holds no target. It is testable and
  // rebindable by assignment, but calling through it is undefined behavior,
  // exactly as with an empty proxy.
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtable_;
  }

protected:
  using vtable_t = vtbuild_t<F>::vtable_t;
  using target_ptr_t = std::conditional_t<Const, const void*, void*>;

  constexpr view_base() noexcept = default;
  constexpr view_base(target_ptr_t target, const vtable_t* vtable) noexcept
      : target_{target}, vtable_{vtable} {}

  target_ptr_t target_{};
  const vtable_t* vtable_{};
};

} // namespace details

// `proxy_view` is a non-owning erased handle over any `Proxiable` target:
// Rust's `&mut dyn Trait`, ngcpp's `proxy_view`.
//
// Two pointers: the target and the per-(facade, type) dispatch table. The
// target must outlive the view.
//
// A view also converts implicitly from any handle of a facade that extends
// `F` (Rust trait upcasting), and from an lvalue owning proxy, re-pointing at
// the handle's target rather than wrapping the handle. An upcast view is
// indistinguishable from one built directly over the target.
//
// Deep-const as an instance, so only const-qualified facade methods dispatch
// through a `const proxy_view`. Because views are freely copyable, that is a
// guardrail rather than a guarantee. Copying a `const proxy_view` yields a
// mutable view onto the same target, much as a `T* const` pointer copies to
// a plain `T*`. Code that enforces read-only access must use
// `const_proxy_view`, where constness is part of the type and survives
// copying.
//
// A default-constructed view is empty, like a default-constructed proxy:
// testable via `operator bool` and rebindable by assignment, but calling
// through it is undefined behavior.
//
// When the facade defines a nested `api`, the view inherits it, so the
// member-call sugar forwarders dispatch alongside `call`.
template<Facade F>
class proxy_view: public details::view_base<F, false> {
  using base = details::view_base<F, false>;
  using vtbuild_t = details::vtbuild_t<F>;

public:
  // An empty view holds no target; see the class comment.
  proxy_view() = default;

  // Converting constructor from an lvalue target.
  //
  // Intentionally implicit, like `string_view` from `string`. Rvalues do not
  // bind, so construction from a temporary is rejected at compile time. Const
  // targets take a `const_proxy_view`. Handles of `F`, or of a facade
  // extending it, take the dedicated constructors below instead of being
  // wrapped as targets.
  template<typename T>
  requires(Proxiable<T, F> && !std::is_const_v<T> &&
           !details::is_handle_for<T, F>())
  constexpr explicit(false) proxy_view(T& target) noexcept
      : base{std::addressof(target), &details::vtable_for<F, T>} {}

  // Upcasting constructor from a view over a facade that extends `F` (Rust
  // trait upcasting).
  //
  // Intentionally implicit, like derived-to-base pointer conversion: the
  // target carries over unchanged and the dispatch table narrows to `F`'s by
  // following the embedded base-table pointers, so the upcast view dispatches
  // exactly what a directly-built `F` view of the target would.
  template<Facade D>
  requires Extends<D, F>
  constexpr explicit(false) proxy_view(const proxy_view<D>& view) noexcept
      : base{view.target_, details::upcast_vtable<F, D>(view.vtable_)} {}

  // Viewing constructor from an owning proxy of `F`, or of a facade that
  // extends it.
  //
  // Intentionally implicit, and lvalue-only, so a view cannot be left dangling
  // by a temporary proxy. The proxy must be non-empty and must outlive the
  // view. A const proxy takes a `const_proxy_view` instead, preserving deep
  // const.
  template<Facade D>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) proxy_view(proxy<D>& p) noexcept
      : base{p.target(), details::upcast_vtable<F, D>(&p.vtable_->vt)} {}

  // Call the facade method named `K`, forwarding `args` through the erased
  // signature. The call is `noexcept` when the method is.
  //
  // This overload dispatches every method. The inherited const overload,
  // re-exposed by the using-declaration, is constrained to const-qualified
  // methods, mirroring the owning proxy's deep const.
  template<fixed_string K, typename... Args>
  constexpr decltype(auto)
  call(Args&&... args) noexcept(vtbuild_t::template is_noexcept<K>()) {
    constexpr auto ndx = vtbuild_t::template index_of<K>();
    static_assert(ndx != vtbuild_t::count_v, "no matching signature");
    return std::get<ndx>(
        this->vtable_->thunks)(this->target_, std::forward<Args>(args)...);
  }

  using base::call;

private:
  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
};

// Library-provided binding so that a view satisfies its own facade and every
// facade that facade extends (as in Rust, where `dyn Trait` implements
// `Trait` and meets its supertrait bounds).
//
// Calls forward through the wrapped view, with conditional `noexcept` so the
// invariant also holds for facades with noexcept methods. The const overload
// serves const-qualified methods, matching the view's instance-level deep
// const. This makes facade-constrained generic code accept concrete and
// erased arguments interchangeably, including derived-facade handles under a
// base-facade bound, and allows views of views.
template<Facade F, Facade D>
requires(std::same_as<D, F> || Extends<D, F>)
struct proxy_impl<F, proxy_view<D>> {
  template<fixed_string K, typename... Args>
  static constexpr decltype(auto)
  on(method_key<K>, proxy_view<D>& view, Args&&... args) noexcept(
      noexcept(view.template call<K>(std::forward<Args>(args)...))) {
    return view.template call<K>(std::forward<Args>(args)...);
  }
  template<fixed_string K, typename... Args>
  static constexpr decltype(auto)
  on(method_key<K>, const proxy_view<D>& view, Args&&... args) noexcept(
      noexcept(view.template call<K>(std::forward<Args>(args)...))) {
    return view.template call<K>(std::forward<Args>(args)...);
  }
};

// Non-owning read-only erased handle: Rust's `&dyn Trait`, the
// `const_iterator` to `proxy_view`'s `iterator`.
//
// Constness is part of the type, so unlike a `const proxy_view` it survives
// copying. A const view only ever copies or converts to another const view.
// It binds const and mutable targets alike, and dispatches only the
// const-qualified facade methods, sharing the mutable view's per-(facade,
// type) dispatch table (the non-const slots are simply unreachable). The
// target must outlive the view.
//
// A default-constructed view is empty: testable via `operator bool` and
// rebindable by assignment, but calling through it is undefined behavior.
//
// When the facade defines a nested `api`, the view inherits it. Only the const
// forwarders are callable; a mutable forwarder fails inside its `call` if
// used.
template<Facade F>
class const_proxy_view: public details::view_base<F, true> {
  using base = details::view_base<F, true>;

public:
  // An empty view holds no target; see the class comment.
  const_proxy_view() = default;

  // Converting constructor from an lvalue target, const or not.
  //
  // Intentionally implicit. Rvalues do not bind. Handles of `F`, or of a
  // facade extending it, take the dedicated constructors below instead of
  // being wrapped as targets.
  template<typename T>
  requires(Proxiable<std::remove_const_t<T>, F> &&
           !details::is_handle_for<std::remove_const_t<T>, F>())
  constexpr explicit(false) const_proxy_view(T& target) noexcept
      : base{std::addressof(target),
            &details::vtable_for<F, std::remove_const_t<T>>} {}

  // Converting constructor from the mutable view. Dropping mutability is
  // implicit and safe, like `T*` to `const T*`, and there is no path back.
  constexpr explicit(false)
      const_proxy_view(const proxy_view<F>& view) noexcept
      : base{view.target_, view.vtable_} {}

  // Upcasting constructor from a const view over a facade that extends `F`
  // (Rust trait upcasting).
  //
  // Intentionally implicit; see the mutable view's upcasting constructor.
  template<Facade D>
  requires Extends<D, F>
  constexpr explicit(false)
      const_proxy_view(const const_proxy_view<D>& view) noexcept
      : base{view.target_, details::upcast_vtable<F, D>(view.vtable_)} {}

  // Upcasting constructor from the mutable view of a facade that extends
  // `F`, dropping mutability and upcasting in one implicit step.
  template<Facade D>
  requires Extends<D, F>
  constexpr explicit(false)
      const_proxy_view(const proxy_view<D>& view) noexcept
      : base{view.target_, details::upcast_vtable<F, D>(view.vtable_)} {}

  // Viewing constructor from an owning proxy of `F`, or of a facade that
  // extends it.
  //
  // Intentionally implicit, and lvalue-only. The proxy must be non-empty and
  // must outlive the view. Mutable and const proxies alike yield the const
  // view; there is no path back to mutability.
  template<Facade D>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) const_proxy_view(const proxy<D>& p) noexcept
      : base{p.target(), details::upcast_vtable<F, D>(&p.vtable_->vt)} {}

  // No need for a `using` because`call` is inherited. The base's const-method
  // dispatch is the entire interface, since the mutable methods do not exist
  // on this view.

private:
  template<Facade G>
  friend class const_proxy_view;
};

// Library-provided binding so that a const view satisfies its own facade,
// and the facades that facade extends, where that is possible.
//
// It dispatches only const methods, so the invariant holds exactly for
// all-const facades.
//
// The `on` is itself constrained to const methods so that a mixed facade
// fails conformance cleanly at overload resolution, rather than erroring
// during return type deduction of a forwarder whose `call` cannot compile.
template<Facade F, Facade D>
requires(std::same_as<D, F> || Extends<D, F>)
struct proxy_impl<F, const_proxy_view<D>> {
  template<fixed_string K, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<K>())
  static constexpr decltype(auto)
  on(method_key<K>, const const_proxy_view<D>& view, Args&&... args) noexcept(
      noexcept(view.template call<K>(std::forward<Args>(args)...))) {
    return view.template call<K>(std::forward<Args>(args)...);
  }
};

// Make a view over `target`: a convenience for spelling the facade at the call
// site.
//
// When `target` is already a view of `F` it is copied rather than wrapped, so
// generic facade-constrained code can erase its argument without stacking
// indirections. A handle of a facade extending `F`, or an owning proxy,
// likewise re-points at its target (upcasting as needed) through the
// dedicated view constructors.
template<Facade F, typename T>
requires Proxiable<T, F>
[[nodiscard]] constexpr proxy_view<F> make_proxy_view(T& target) noexcept {
  if constexpr (std::same_as<std::remove_cv_t<T>, proxy_view<F>>)
    return target;
  else
    return proxy_view<F>{target};
}

#pragma endregion
#pragma region proxy

// Owning erased handle over any `Proxiable` target: Rust's `Box<dyn Trait>`,
// ngcpp's `proxy`.
//
// Move-only. Small targets (at most `sbo_size` bytes, at most
// `std::max_align_t` alignment, nothrow-move-constructible) are stored
// inline. Anything else lives in a unique-owned heap allocation. The owning
// dispatch table carries destroy and relocate slots alongside the facade
// methods, so destruction and moves work without knowing the target type.
//
// The proxy is deep-const, so only const-qualified facade methods dispatch
// through a const proxy. Being move-only, it cannot be copied out of that
// constness the way a view can.
//
// A default-constructed or moved-from proxy is empty. It is destructible,
// assignable, and testable via `operator bool`, but calling through it is
// undefined behavior.
//
// A non-empty lvalue proxy converts implicitly to `proxy_view` (mutable
// proxies only) and `const_proxy_view`, of its own facade or of any facade it
// extends; the view re-points at the stored target, and the proxy must
// outlive it. See the view constructors.
//
// When the facade defines a nested `api`, the proxy inherits it, so the
// member-call sugar forwarders dispatch alongside `call`.
template<Facade F>
class proxy: public details::api_base_t<F> {
  using vtbuild_t = details::vtbuild_t<F>;
  using owning_vtable_t = vtbuild_t::owning_vtable_t;

public:
  using facade_t = F;

  // Inline storage capacity in bytes. See the class comment for the other
  // inline-eligibility conditions.
  static constexpr std::size_t sbo_size = details::sbo_size;

  // An empty proxy holds no target.
  proxy() = default;

  proxy(const proxy&) = delete;
  proxy& operator=(const proxy&) = delete;

  // Construct an owning proxy holding a `T` built in place from `args`.
  // Usually spelled through `make_proxy`.
  template<typename T, typename... Args>
  requires(Proxiable<T, F> && std::constructible_from<T, Args...>)
  explicit proxy(std::in_place_type_t<T>, Args&&... args)
      : vtable_{&details::owning_vtable_for<F, T>} {
    if constexpr (details::sbo_eligible_v<T>)
      ::new (static_cast<void*>(storage_.buf)) T(std::forward<Args>(args)...);
    else
      storage_.ptr = new T(std::forward<Args>(args)...);
  }

  // Move construction and assignment leave the source empty. Inline targets
  // relocate through the table's move slot, while heap targets move by
  // pointer steal.
  proxy(proxy&& other) noexcept { do_adopt(other); }

  proxy& operator=(proxy&& other) noexcept {
    if (this != &other) {
      do_reset();
      do_adopt(other);
    }
    return *this;
  }

  ~proxy() { do_reset(); }

  // Call the facade method named `K`, forwarding `args` through the erased
  // signature.
  //
  // The call is `noexcept` when the method is. The const overload is
  // constrained to const-qualified methods, enforcing deep const at overload
  // resolution so the rejection is visible to `requires` probes as well. It
  // is not `[[nodiscard]]`, because discardability belongs to the facade
  // method rather than the dispatcher (the `std::invoke` precedent).
  template<fixed_string K, typename... Args>
  decltype(auto)
  call(Args&&... args) noexcept(vtbuild_t::template is_noexcept<K>()) {
    constexpr auto ndx = vtbuild_t::template index_of<K>();
    static_assert(ndx != vtbuild_t::count_v, "no matching signature");
    return std::get<ndx>(
        vtable_->vt.thunks)(target(), std::forward<Args>(args)...);
  }

  template<fixed_string K, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<K>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  decltype(auto) call(Args&&... args) const
      noexcept(vtbuild_t::template is_noexcept<K>()) {
    constexpr auto ndx = vtbuild_t::template index_of<K>();
    static_assert(ndx != vtbuild_t::count_v, "no matching signature");
    return std::get<ndx>(
        vtable_->vt.thunks)(target(), std::forward<Args>(args)...);
  }

  // An empty proxy (default-constructed or moved-from) holds no target.
  [[nodiscard]] explicit operator bool() const noexcept { return vtable_; }

private:
  union storage_t {
    alignas(std::max_align_t) std::byte buf[sbo_size];
    void* ptr;
  };

  // Target address, inline or heap. Meaningless when empty.
  [[nodiscard]] void* target() noexcept {
    assert(vtable_);
    return vtable_->relocate ? static_cast<void*>(storage_.buf) : storage_.ptr;
  }
  [[nodiscard]] const void* target() const noexcept {
    assert(vtable_);
    return vtable_->relocate
               ? static_cast<const void*>(storage_.buf)
               : storage_.ptr;
  }

  // Destroy the target, if any, leaving the proxy empty.
  void do_reset() noexcept {
    if (!vtable_) return;
    vtable_->destroy(target());
    vtable_ = nullptr;
  }

  // Take over `other`'s target, leaving `other` empty. Assumes `*this` holds
  // no target (freshly constructed or just reset). Note that we don't need to
  // clear `buf` or `ptr` on `other.storage_` because `other.vtable_` defines
  // whether it's empty.
  void do_adopt(proxy& other) noexcept {
    vtable_ = std::exchange(other.vtable_, nullptr);
    if (!vtable_) return;
    if (vtable_->relocate)
      vtable_->relocate(other.storage_.buf, storage_.buf);
    else
      storage_.ptr = other.storage_.ptr;
  }

  storage_t storage_;
  const owning_vtable_t* vtable_{};

  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
};

// Library-provided binding so that an owning proxy satisfies its own facade
// and every facade that facade extends, like the view.
//
// Calls forward through the proxy, with conditional `noexcept`. The const
// overload serves const-qualified methods, matching the proxy's deep const,
// and the non-const overload serves the rest.
template<Facade F, Facade D>
requires(std::same_as<D, F> || Extends<D, F>)
struct proxy_impl<F, proxy<D>> {
  template<fixed_string K, typename... Args>
  static decltype(auto)
  on(method_key<K>, proxy<D>& p, Args&&... args) noexcept(
      noexcept(p.template call<K>(std::forward<Args>(args)...))) {
    return p.template call<K>(std::forward<Args>(args)...);
  }
  template<fixed_string K, typename... Args>
  static decltype(auto)
  on(method_key<K>, const proxy<D>& p, Args&&... args) noexcept(
      noexcept(p.template call<K>(std::forward<Args>(args)...))) {
    return p.template call<K>(std::forward<Args>(args)...);
  }
};

// Make an owning proxy of facade `F` holding a `T` constructed in place from
// `args`.
//
// To move an existing object in, pass it as the constructor argument:
// `make_proxy<F, T>(std::move(obj))`.
template<Facade F, typename T, typename... Args>
requires Proxiable<T, F>
[[nodiscard]] proxy<F> make_proxy(Args&&... args) {
  return proxy<F>{std::in_place_type<T>, std::forward<Args>(args)...};
}

#pragma endregion

} // namespace prox

#pragma region Exports

// Call-site vocabulary, exported to `corvid::meta`.
//
// Consuming an erased handle is ordinary type usage, as in
// `do_stuff(proxy_view<foo_like>)`, so these names belong in the wider
// namespace. The authoring vocabulary (`facade`, `method`, `proxy_impl`, and
// the registration machinery) stays inside `prox`, since those names are too
// generic to export and facade and impl authors are already working in that
// domain.
using prox::const_proxy_view;
using prox::make_proxy;
using prox::make_proxy_view;
using prox::proxy;
using prox::Proxiable;
using prox::proxy_view;
using prox::prox_impl;

#pragma endregion

}} // namespace corvid::meta
