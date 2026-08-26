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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../bool_enums.h"
#include "../crossplatform.h"
#include "../fixed_string.h"
#include "invocable_common.h"
#include "invocable_policy.h"
#include "../traits.h"

// Registration-based runtime polymorphism ("proxy") system: the machinery
// under the handles.
//
// Type-erased handles (`proxy`, `proxy_view`, `const_proxy_view`,
// `shared_proxy`, `const_shared_proxy`, `weak_proxy`, `const_weak_proxy`)
// over an interface definition (a facade),
// without inheritance, vtable pointers in the target type, or macros.
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
// This header holds the facade, registration, and dispatch machinery the
// handles are built on. The handles live in the sibling headers, and
// `proxy.h` includes the whole family. See "proxy.md" for the design.

// Template parameter conventions, used consistently throughout the proxy
// headers:
//
// - `F`: the facade being dispatched, bound, or validated.
// - `B`: a base facade, one that `F` or `D` extends.
// - `D`: a derived facade, one that extends `F` or `B`.
// - `G`: a second facade, where two vary independently.
// - `T`: the concrete target type behind a handle.
// - `Handle`: a deduced handle parameter in the library's self-conformance
//     bindings, binding the const and mutable flavors with one overload.
// - `E`: one entry of a facade's list; `Es` is the whole pack (`name`,
//     `extends`, and `method` entries).
// - `Bs`: a facade's direct-base facades.
// - `M`: a `method` descriptor; `Ms` is a pack of them.
// - `S`: a flattened dispatch slot; `Ss` is a pack of them.
// - `Owner`: the facade that declared a slot's method.
// - `Born`: the facade a handle's target was constructed as, keying its
//      owning and view tables and the birth ancestries `try_downcast`
//      searches.
// - `Name`: a declared name, a facade's or a method's, as a `fixed_string`.
// - `OwnName`: the name of the facade being built.
// - `Key`: a `call` lookup key: a method name, optionally facade-qualified.
//      Distinct from `Name` because a key can be qualified; a name never is.
// - `Sig`: a method's erased signature.
// - `R`: a method's declared result type.
// - `Args`: a method's declared parameters, or the arguments a call site
//      passes; `CallArgs` where the two meet and need distinguishing.
// - `Impl`: a registration-carried binding class.
// - `Check`: an `api_check` value.
// - `Policy`: an owning proxy's storage policy, an `invocable_policy` value.
// - `P`: a second handle's storage policy, where two vary independently.

namespace corvid { inline namespace meta {
namespace prox {

// The shared invocable vocabulary (`invocable_policy`, `storage_mode`, and
// kin) is spelled unqualified throughout.
using namespace invocables;

#pragma region Method and key

// `method_key` is the tag carrying a method name as a compile-time string.
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
// position.
//
// The ordinary `(const char*, size_t)` form cannot work, because function
// parameters are runtime values, never constant expressions, even under
// `consteval`.
template<fixed_string Name>
consteval auto operator""_method() noexcept {
  return method_key<Name>{};
}

} // namespace literals

namespace details {

// The shared working parts live with `invocable_policy` and in
// "invocable_common.h". They are brought in whole so that unqualified
// `details::` calls below find them.
using namespace invocables::implementation;

// `proxy_storage_mode_of` is where a `proxy` under policy `p` keeps a `T`:
// `inlined` when the policy can store it inline, else `dynamic`.
//
// Never `direct`. The proxy contract resolves a target address at every turn
// (views lend it, impls take a `T&`, `extract` and `shared_proxy` adopt the
// allocation), so a target with no per-instance state (see
// `is_direct_eligible`) is stored like any other, and the policy's direct
// eligibility is not consulted.
//
// Nothing is lost: inline storage of such a target already costs nothing at
// runtime, and the one place it would save an allocation, `heap_only`,
// promises a stable address, which a target stored nowhere could not keep.
template<typename T>
consteval storage_mode proxy_storage_mode_of(invocable_policy p) noexcept {
  return can_store_inline<T>(p)
             ? storage_mode::inlined
             : storage_mode::dynamic;
}

// `name_is_unqualified` is whether `s` avoids the `"::"` separator, which
// qualified keys reserve for splitting the facade name from the method name.
[[nodiscard]] consteval bool name_is_unqualified(std::string_view s) noexcept {
  return !s.contains("::");
}

} // namespace details

// `method` is the descriptor with the name plus the erased signature.
//
// The signature is spelled like a member function's definition. So, for
// example, it would look like `std::string(int)`. Or, for `const this`, it
// would be `std::string(int) const`.
//
// The signature fixes the erased ABI; a binding may return the declared result
// type or anything convertible to it.
//
// A `noexcept` qualifier is likewise honored. Conformance then requires the
// binding itself to be noexcept-invocable, and the erased call (`call` through
// a handle) is itself `noexcept` whenever the argument conversions cannot
// throw; the conversions belong to the caller, exactly as with the `api`
// forwarders, so a throwing one propagates from either spelling.
//
// A method derives from its `method_key`, so a method tag is usable anywhere
// its key is, including `on` overload selection and deduction of
// `method_key<Key>` from a method argument. This also leaves the door open
// for bindings that overload on the full method (signature included);
// per-name overload sets themselves are supported by listing the name
// repeatedly, and the bindings overload on the trailing parameters.
//
// The signature may carry `const` and `noexcept`, but not a reference
// qualifier, because it serves every handle in the family, owning, viewing,
// and shared alike, and constness is the one qualifier they all share with
// the target.
//
// A handle's value category says nothing about the target's: a view is a
// copyable pair of pointers, and a shared handle has other owners, so an `&&`
// method would let either move out of an object it does not own, while `&`
// could only refuse a call on a temporary owner.
//
// This is `std::function_ref`'s rule (`const` and `noexcept` only), where
// `flexi_function` follows `std::move_only_function`, whose wrapper is the
// callable.
//
// A consuming operation belongs in the binding, which may take `T&` and move
// out of it, or in `proxy::extract`. The qualifiers are exposed as
// `const_qualifier` and `noexcept_specifier`, as the bools `is_const` and
// `is_noexcept`, and the const axis also as the `access_mode` `access`;
// `args_t` is the declared parameter list, for introspection (`codegen` walks
// it).
template<fixed_string Name, typename Sig>
struct method: method_key<Name> {
  static_assert(!Name.empty(), "method names may not be empty");
  static_assert(details::name_is_unqualified(Name.view()),
      "method names may not contain \"::\"; it is reserved for qualifying a "
      "key with the facade name");

  using traits = signature_traits<Sig>;
  static_assert(traits::ref_qualifier == ref_qual::none,
      "a method signature may be const and noexcept, but not ref-qualified");

  using result_t = traits::result_t;
  using args_t = traits::args_t;
  using function_t = traits::function_t;
  static constexpr const_qual const_qualifier = traits::const_qualifier;
  static constexpr noexcept_spec noexcept_specifier =
      traits::noexcept_specifier;
  static constexpr bool is_const = traits::is_const;
  static constexpr bool is_noexcept = traits::is_noexcept;
  static constexpr access_mode access = traits::access;
};

#pragma endregion
#pragma region Facade

// `facade` is the base that defines the interface.
//
// Derive a named struct from `facade`, listing the facade's `name` and its
// methods. For example:
//
//    struct animal
//        : facade<name<"animal">, //
//            method<"speak", void() const>> {};
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
//    struct animal
//        : facade<name<"animal">, //
//            method<"speak", void() const>> {
//      struct api {
//        void speak(this const auto& self) {
//          self.template call<"speak">();
//        }
//      };
//      template<typename T>
//      struct boilerplate : proxy_impl_base {
//        static void on(method_key<"speak">, const T& t) {
//          t.speak();
//        }
//      };
//    };
//
// With that in place, `handle.speak()` is sugar for `handle.call<"speak">()`,
// dispatching through the same table.
//
// The facade body is also the normal home of the boilerplate impl; see also
// `proxy_impl`.
//
// A facade is an interface descriptor, never a value: declare a handle over
// it (`proxy<F>`, `proxy_view<F>`, ...) rather than an instance of it. The
// deleted default constructor propagates to derived facades, so a stray
// `gunslinger g;` fails at the declaration instead of compiling into a
// useless empty object.
template<typename... Methods>
struct facade {
  facade() = delete;
};

namespace details {

// Probe for the unique public `facade` base of `F`.
//
// Declared only; used in unevaluated contexts.
template<typename... Ms>
auto probe(const facade<Ms...>&) -> facade<Ms...>;

} // namespace details

// Concept for a type derived from a single `facade` base.
template<typename F>
concept Facade = requires(const F& f) { details::probe(f); };

// `extends` is the facade composition entry declaring that the facade extends
// `B` (Rust supertrait, ngcpp `add_facade`).
//
// Listed in the facade's method list alongside the methods, conventionally
// after the `name`. For example:
//
//    struct pet : facade<name<"pet">,
//                     extends<animal>,
//                     method<"play", void()>> {};
//
// The derived facade's effective method list is the flattening of its bases'
// lists, in declaration order, followed by its own; handles of the derived
// facade dispatch inherited and own methods alike.
//
// Within one extends chain, a recurring method name forms an overload set,
// legal when every pair differs in arguments or constness, whether the
// declarations share a facade or span levels. A same-signature recurrence is
// rejected eagerly, because that would be redeclaration (or overriding), and
// facades carry no implementations, so there is nothing to override. The
// degenerate case, the identical entry listed twice, is rejected as a
// duplicate entry.
//
// Unrelated sibling bases MAY collide on a method name: distinct signatures
// form an overload set that unqualified calls resolve by argument, and a
// same-signature collision is a lazy call-site error, reachable through the
// facade-qualified key or an upcast handle.
//
// Diamond composition is supported: a shared ancestor reached through more
// than one path flattens to a single set of slots (dedup by facade identity),
// and since conformance is per facade there is only one binding to reach, no
// matter the path.
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

// `name` is the facade name entry, giving the facade a formal name, which
// qualifies its methods.
//
// Listed in the facade's entry list, conventionally first. For example:
//
//    struct animal
//        : facade<name<"animal">, //
//            method<"speak", void() const>> {};
//
// Every method then answers to its facade-qualified name ("animal::speak")
// as well as its plain one: a `call` key containing "::" matches the
// declaring facade's name plus the method name. The qualified spelling is
// what disambiguates sibling collisions, two `extends` bases declaring the
// same method name. Facade names must be unique within a composition, and
// would ideally be globally unique, though not by namespace-style
// qualification: `"::"` is reserved for splitting a qualified key, so
// neither facade nor method names may contain it (enforced here and in
// `method`).
template<fixed_string Name>
struct name {
  static_assert(!Name.empty(),
      "facade names may not be empty; an empty qualifier would match any "
      "facade");
  static_assert(details::name_is_unqualified(Name.view()),
      "facade names may not contain \"::\"; it is reserved for qualifying a "
      "key with the facade name");
};

namespace details {

// The stand-in API base for facades that define no member-call sugar.
struct no_api {};

// `api_base` is the sugar base for handles of facade `F`.
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

// `proxy_impl_base` is the convenience base for binding classes: a facade's
// `boilerplate`, or a registration-carried impl.
//
// Inheriting it is optional, and a binding class that inherits a boilerplate
// already has it through that base. It also serves as documentation of what
// the class is for.
struct proxy_impl_base {
  template<fixed_string Name>
  using method_key = prox::method_key<Name>;
};

// `proxy_impl` is the binding point between a facade and a concrete type
// (Rust's `impl Trait for Type`).
//
// The primary template is a bare forward declaration: it never receives a
// body, so instantiating it directly yields an incomplete type. Every usable
// binding is a specialization, and the specializations are gated on
// registration, the sole act of conformance: the library's partial
// specializations install the facade's boilerplate or the pair's
// registration-carried impl, and their constraints match registered pairs
// only. For an unregistered pair, no specialization applies, and the
// incomplete primary is what makes conformance fail.
//
// A binding provides a single static `on` overload per facade method, taking
// the method's `key`, the target (const-qualified per the method), and the
// method's arguments.
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
// the type's private members), or at namespace scope. Note that you can also
// forward-declare and friend it in the class, giving it private access without
// nesting.
//
// A carried impl outranks the boilerplate, being the more specific
// declaration. The namespace-scope spelling of the boilerplate, a `proxy_impl`
// partial specialization gated on `ProxyRegistered`, remains equivalent and
// supported (to preserve carried-impl precedence, its gate should also
// require `!SpecCarriesImpl<F, T>`); it is the one user-authored
// specialization of this template in the supported surface.
//
// Because the nested boilerplate is an ordinary inheritable class, it also
// enables partial overrides: a type whose names line up except for one method
// registers a carried impl that inherits `F::boilerplate<T>`, re-exposes its
// `on` overloads with a using-declaration, and declares only the divergent
// binding.
//
// See "proxy_test.cpp" for examples (`sheriff`, `turncoat`).
//
// For example:
//
//    // Facade author's boilerplate, written once, serving any registered
//    // type whose member names line up.
//    struct animal
//        : facade<name<"animal">, //
//            method<"speak", void() const>> {
//      // Note: API omitted for brevity.
//      template<typename T> struct boilerplate : proxy_impl_base {
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
//      struct as_animal : proxy_impl_base {
//        static void on(method_key<"speak">, const cat& c) { c.meow(); }
//      };
//      return make_proxy_spec<animal, cat, as_animal>();
//    }
template<typename F, typename T>
struct proxy_impl;

// `proxy_spec` is the minimal registration spec.
//
// The value a registration hook returns. Distinct from `corvid_proxy_spec`,
// which is the hook itself, provided by the user.
//
// `Impl`, when not `void`, is a registration-carried impl: a binding class the
// registration names, which the library installs as the pair's `proxy_impl`
// (see `SpecCarriesImpl`). Because each hook's return type is deduced
// independently, richer spec types can be added later without touching
// existing registrations.
//
// The spec re-exposes its pair as `facade_t` and `target_t`, which
// `ProxyRegistered` verifies against the queried pair: the facade exactly, the
// target as the queried type or a public base of it (the conversion that lets
// a base-class hook serve every derived type). A hook whose spec names an
// unrelated pair (a copy-paste slip) therefore fails registration instead of
// silently registering the pair it was found for.
template<typename F, typename T, typename Impl = void>
struct proxy_spec {
  using facade_t = F;
  using target_t = T;
  using impl_t = Impl;
};

namespace details {

// Fwd.
template<Facade F>
struct api_probe;

template<Facade F>
[[nodiscard]] consteval bool are_base_boilerplates_visible() noexcept;

} // namespace details

// Fwd.
template<Facade F>
[[nodiscard]] consteval bool validate_api() noexcept;

// Whether registering a pair also validates the facade's `api` (see
// `validate_api`).
//
// A facade whose `api` deliberately deviates from the method list (say, a
// widening convenience signature), must register with `api_check::off`.
enum class api_check : uint8_t { off, on };

namespace details {

// Registration-time half of `validate_api`.
//
// Run the check when the facade has an `api` and the registration has not
// opted out.
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
    // A composed facade's probe also runs through each base facade's
    // boilerplate, so their absence is diagnosed here rather than as a hard
    // error deep inside the table build.
    constexpr auto has_base_boilerplates = are_base_boilerplates_visible<F>();
    static_assert(has_base_boilerplates,
        "validating the api of a composed facade needs every base facade's "
        "boilerplate impl visible at the registration; pass api_check::off "
        "to skip");
    if constexpr (has_boilerplate && has_base_boilerplates)
      (void)validate_api<F>();
  }
}

} // namespace details

// `make_proxy_spec` to make the registration spec for a (facade, type) pair.
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

namespace details {

// The return type of the pair's registration hook, found by ADL exactly as
// `ProxyRegistered` finds it.
template<typename F, typename T>
using registered_spec_t = decltype(corvid_proxy_spec(static_cast<F*>(nullptr),
    static_cast<T*>(nullptr)));

} // namespace details

// `ProxyRegistered` is the concept for a (facade, type) pair being registered.
//
// To register a pair, declare a `corvid_proxy_spec(F*, T*)` overload returning
// `make_proxy_spec<F, T>()`, in the namespace of either the facade or the
// type; it is found here by ADL. One namespace or the other, never both:
// duplicate hooks make the ADL call ambiguous, and the pair then reads as
// unregistered here rather than as a diagnosed duplicate.
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
//
// The hook's returned spec must agree with the queried pair: the facade
// exactly, and the target as the queried type or a public base of it, which is
// the same derived-to-base conversion that lets a base type's hook register
// every type derived from it. A hook returning `make_proxy_spec` for an
// unrelated pair (a copy-paste slip) fails this concept rather than silently
// registering the pair it was found for.
template<typename F, typename T>
concept ProxyRegistered = requires {
  corvid_proxy_spec(static_cast<F*>(nullptr), static_cast<T*>(nullptr));
  requires std::same_as<typename details::registered_spec_t<F, T>::facade_t,
      F>;
  // The same-type term is spelled separately because `derived_from` holds
  // only between classes, and a target may be a non-class type.
  requires std::same_as<T,
               typename details::registered_spec_t<F, T>::target_t> ||
               std::derived_from<T,
                   typename details::registered_spec_t<F, T>::target_t>;
};

// `SpecCarriesImpl` is the concept for a (facade, type) pair whose
// registration carries an impl.
//
// True when the pair is registered through the three-type
// `make_proxy_spec<F, T, Impl>()`, which names the binding class serving the
// pair.
//
// The library installs the carried impl as the pair's `proxy_impl` (below),
// outranking the facade's boilerplate because the carried impl is the more
// specific declaration, closer to the type.
//
// A namespace-scope boilerplate partial should include `!SpecCarriesImpl<F,
// T>` in its gate to preserve that precedence, since partial ordering would
// otherwise prefer it.
template<typename F, typename T>
concept SpecCarriesImpl =
    ProxyRegistered<F, T> &&
    !std::is_void_v<typename details::registered_spec_t<F, T>::impl_t>;

namespace details {

// The impl type carried by the pair's registration.
template<typename F, typename T>
using registered_impl_t = registered_spec_t<F, T>::impl_t;

} // namespace details

// `proxy_impl` is the library-provided delegation to a registration-carried
// impl.
//
// The registration names the binding class, so the impl is explicitly
// registered like everything else, and the class itself can be local to the
// hook or nest inside the type it serves (a hook is implicitly inline, so a
// hook-local class is ODR-consistent across translation units).
template<Facade F, typename T>
requires SpecCarriesImpl<F, T>
struct proxy_impl<F, T>: details::registered_impl_t<F, T> {};

// `proxy_impl` is the library-provided delegation to a facade-hosted
// boilerplate impl.
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

// `proxy_spec_v` is the central access point for the registered spec,
// mirroring `enum_spec_v`.
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

// `method_traits` is the per-method dispatch machinery for method `M`, matched
// on its `function_t` to recover the result and parameter pack.
//
// Contains the thunk pointer type, the compile-time check that a `proxy_impl`
// binding exists, and the thunk itself, which is where the concrete type is
// seen for the last time before erasure.
//
// A const method sees the target as `const T&` and erases it as `const
// void*`. A noexcept method additionally requires the binding to be
// noexcept-invocable, and its thunk pointer type carries `noexcept` through
// the erased ABI.
template<typename M, typename FunctionT = M::function_t>
struct method_traits;

template<typename M, typename R, typename... Args>
struct method_traits<M, R(Args...)> {
  template<typename T>
  using target_t = conditional_const_t<M::access, T>;
  using erased_ptr_t = conditional_const_t<M::access, void>*;
  using thunk_ptr_t = R (*)(erased_ptr_t, Args...) noexcept(M::is_noexcept);

  // The parameter list normalized for exact-match probing.
  //
  // Top-level cv and references stripped from each parameter, so
  // value-category spelling is ignored, but a merely-convertible type does not
  // match.
  using norm_args_t = std::tuple<std::remove_cvref_t<Args>...>;

  // Whether `CallArgs` match the declared parameters exactly after
  // normalization, and whether they are merely viable through the ordinary
  // conversions a call performs.
  //
  // Exactness is the validation probe's strictness; viability is how
  // `resolve` tells an ambiguous call from an unmatched one when the ranking
  // probe rejects it.
  template<typename... CallArgs>
  static constexpr bool is_exact =
      std::same_as<std::tuple<std::remove_cvref_t<CallArgs>...>, norm_args_t>;
  template<typename... CallArgs>
  static constexpr bool is_viable =
      std::is_invocable_v<R (*)(Args...), CallArgs...>;

  // Whether the binding class `proxy_impl<F, T>` has an `on` overload for this
  // method, taking the target and the method's arguments.
  template<typename F, typename T>
  static constexpr bool is_bound = requires(target_t<T>& t, Args... args) {
    {
      proxy_impl<F, T>::on(method_key<M::name_v>{}, t,
          std::forward<Args>(args)...)
    } -> std::convertible_to<R>;
  } && (!M::is_noexcept || requires(target_t<T>& t, Args... args) {
    {
      proxy_impl<F, T>::on(method_key<M::name_v>{}, t,
          std::forward<Args>(args)...)
    } noexcept;
  });

  // Make the thunk for this method, which erases the target and forwards the
  // call to the binding class.
  template<typename F, typename T>
  static consteval thunk_ptr_t make_thunk() noexcept {
    return
        [](erased_ptr_t target, Args... args) noexcept(M::is_noexcept) -> R {
          return proxy_impl<F, T>::on(method_key<M::name_v>{},
              *static_cast<target_t<T>*>(target), std::forward<Args>(args)...);
        };
  }

  // Empty-call rules for `R`.
  using empty_traits = empty_call_traits<R>;

  // The behavior this method's empty thunk takes under the policy floor
  // `floor`, the mildest at or above it that the signature admits.
  static consteval on_empty empty_behavior(on_empty floor) noexcept {
    return empty_traits::resolve_floor(floor, M::noexcept_specifier);
  }

  // Make the thunk that an empty handle dispatches this method to under the
  // policy floor `Floor`; see `empty_behavior`.
  template<on_empty Floor>
  static consteval thunk_ptr_t make_empty_thunk() noexcept {
    return [](erased_ptr_t, Args...) noexcept(M::is_noexcept) -> R {
      return empty_traits::template invoke<empty_behavior(Floor)>();
    };
  }
};

// Empty table's relocate slot, with nothing to move.
//
// A thunk rather than a null slot, so that an empty proxy's `target` resolves
// to its buffer, a valid address, rather than reading the heap pointer it
// never wrote.
inline void empty_relocate(void*, void*) noexcept {}

// Copy thunks, present in the owning table only for copy-constructible
// targets.
//
// Both return the copy's address. `inline_copy` copy-constructs `*from` into
// the buffer at `to` (and the return is that same address); `heap_copy`
// allocates the copy, ignoring `to`. Either can throw (the target's copy
// constructor, or the allocation), so unlike the other housekeeping thunks
// they are not `noexcept`; the caller publishes the result only on success.
template<typename T>
void* inline_copy(const void* from, void* to) {
  return ::new (to) T(*static_cast<const T*>(from));
}

template<typename T>
void* heap_copy(const void* from, void* /*to*/) {
  return new T(*static_cast<const T*>(from));
}

// `type_tag_v` is the type identity tag, such that the address of
// `type_tag_v<T>` identifies `T` uniquely across the program, without RTTI.
//
// The view table carries it, and the owning table embeds that, so a typed
// operation on an erased target can verify the type at runtime through
// any handle.
//
// Deliberately non-const: the address is the identity, and identical
// read-only data is fair game for linker identical-COMDAT folding (MSVC
// /OPT:ICF), which could merge the tags and with them the identities.
// Writable data is never folded.
template<typename T>
inline std::byte type_tag_v{};

// `facade_tag_v` is the facade identity tag, which is the facade analog of
// `type_tag_v`.
//
// Birth-ancestry entries carry it, so `try_downcast` can match a facade at
// runtime. Non-const for the same linker-folding reason.
template<Facade F>
inline std::byte facade_tag_v{};

// Facade-wide dispatch machinery, specialized on the `facade` base to get at
// the entry pack.
//
// Contains the flattened method list, name-to-slot lookup, the dispatch table
// type, the all-methods-bound check behind `Proxiable`, and the table
// builders.
template<typename FB>
struct vtable_builder;

// `vtbuild_t` is the facade-wide machinery for the derived facade type `F`.
template<Facade F>
using vtbuild_t = vtable_builder<decltype(probe(std::declval<const F&>()))>;

// `vtable_for` is the per-(facade, type, born facade) dispatch table instance.
//
// The handle stores a pointer to this; the table pointer lives in the handle,
// not in the target (a fat handle, like Rust's `&dyn`). A composed facade's
// table also embeds the address of each direct base's instance for the same
// target type and birth, which is what makes upcasting a pointer read.
//
// `Born` plays the same role as on the owning tables: it defaults to the
// facade itself, which is the birth of every directly built view, and stays
// out of the table's type; a view lent from an owning handle points at the
// born family its owner's table embeds, so the owner's birth carries over
// without the handle doing anything. The type is spelled explicitly for the
// same reason as `owning_vtable_for`: the born family's tables and its
// ancestry reference each other by address.
template<Facade F, typename T, Facade Born = F>
constexpr inline vtbuild_t<F>::vtable_t vtable_for =
    vtbuild_t<F>::template make_vtable<F, T, Born>();

// `owning_vtable_for` is the per-(facade, born facade, type, storage mode)
// owning dispatch table instance, for `proxy`.
//
// `StorageMode` is where the target lives, `storage_mode::inlined` or
// `storage_mode::dynamic`, decided by the constructing handle's policy as
// well as by `T`.
//
// `Born` is the facade the target was constructed as. Every pointer a table
// embeds (the direct-base tables, the other-mode sibling, the birth
// ancestry) stays within the same born family, so upcasts and mode changes
// land on born-keyed siblings automatically and `try_downcast` can recover
// the birth identity from the table alone, at no cost in the handle.
// Instances are many and tables are few, cold, and deduplicated, so the
// table is where the memory belongs. The table TYPE is still per facade;
// `Born` only selects which static object is pointed at.
//
// The type is spelled explicitly, not deduced: the two modes' tables
// reference each other by address, which is fine for initialization but
// would make `auto` deduction circular.
template<Facade F, Facade Born, typename T, storage_mode StorageMode>
constexpr inline vtbuild_t<F>::owning_vtable_t owning_vtable_for =
    vtbuild_t<F>::template make_owning_vtable<F, Born, T, StorageMode>();

// `empty_vtable_for` and `empty_owning_vtable_for` are the per-(facade, empty
// floor) tables for a handle holding no target.
//
// Each dispatch slot holds the method's empty thunk (see `make_empty_thunk`),
// so calling through an empty handle runs the policy's `on_empty` behavior
// with no branch on the call path. The housekeeping slots are null and there
// is no birth ancestry, so a downcast fails as it must. The base pointers
// lead to the same-floor empty tables of the base facades, so an empty handle
// upcasts and lends exactly as a full one does, carrying its floor along.
//
// `Floor` is the owner's `invocable_policy::empty`. A handle without a policy
// (the views, `shared_proxy`, `weak_proxy`) starts on the `raise` table.
template<Facade F, on_empty Floor>
constexpr inline vtbuild_t<F>::vtable_t empty_vtable_for =
    vtbuild_t<F>::template make_empty_vtable<Floor>();

template<Facade F, on_empty Floor>
constexpr inline vtbuild_t<F>::owning_vtable_t empty_owning_vtable_for =
    vtbuild_t<F>::template make_empty_owning_vtable<F, Floor>();

// An entry of a birth ancestry.
//
// Contains a facade's identity tag and that facade's table for the same birth
// and target: an owning table in an owning ancestry (which is also per
// storage mode), a view table in a view ancestry.
struct ancestor_entry {
  const void* tag{};
  const void* table{};
};

// The type-erased view of a birth ancestry, which is the facade the target was
// born as plus every facade it transitively extends.
//
// Every table points at its born family's ancestry of its own kind: owning
// tables at an owning ancestry, view tables at a view ancestry; the
// underlying tables are the statics below.
struct ancestry_t {
  const ancestor_entry* entries{};
  size_t count{};
};

// Return the table of the ancestry member whose tag is `tag`, or null when the
// facade is not in the ancestry.
//
// This is `try_downcast`'s runtime search; the tag match is what proves the
// cast sound, so the caller can cast the table back to the matched facade's
// type.
[[nodiscard]] constexpr const void*
find_ancestor(const ancestry_t& ancestry, const void* tag) noexcept {
  for (auto ndx = 0UZ; ndx != ancestry.count; ++ndx)
    if (ancestry.entries[ndx].tag == tag) return ancestry.entries[ndx].table;
  return nullptr;
}

// Return `D`'s view table from `vt`'s birth ancestry, or null when `vt` is an
// empty table (which has no ancestry) or the born family does not include `D`.
//
// The shared lookup behind every view-table `try_downcast`; `F` is the
// handle's own facade, spelling the source table type, so call sites pass
// both facades explicitly.
template<Facade D, Facade F>
[[nodiscard]] constexpr auto
find_downcast_table(const typename vtbuild_t<F>::vtable_t* vt) noexcept
    -> const vtbuild_t<D>::vtable_t* {
  if (!vt->ancestry) return nullptr;
  return static_cast<const vtbuild_t<D>::vtable_t*>(
      find_ancestor(*vt->ancestry, &facade_tag_v<D>));
}

// Build the ancestor table for a target born as (Born, T, StorageMode).
//
// Contains `Born` itself first, then every facade it extends, all keyed by the
// same birth. The tuple pointer parameter carries
// `vtbuild_t<Born>::ancestors_t` in deducible position.
template<Facade Born, typename T, storage_mode StorageMode, Facade... As>
consteval std::array<ancestor_entry, 1 + sizeof...(As)>
make_ancestor_table(std::tuple<As...>*) noexcept {
  return {{{&facade_tag_v<Born>,
               &owning_vtable_for<Born, Born, T, StorageMode>},
      {&facade_tag_v<As>, &owning_vtable_for<As, Born, T, StorageMode>}...}};
}

template<Facade Born, typename T, storage_mode StorageMode>
constexpr inline auto ancestor_table_for =
    make_ancestor_table<Born, T, StorageMode>(
        static_cast<vtbuild_t<Born>::ancestors_t*>(nullptr));

// The birth ancestry for a target born as (Born, T, StorageMode), the object
// every owning table of that born family points at.
//
// Each storage mode has its own ancestry, whose entries are that mode's
// tables; a mode-changing adoption switches the proxy to the table's
// other-mode sibling, which carries the other mode's ancestry, so the tables
// an ancestry hands out always match the target's current home.
template<Facade Born, typename T, storage_mode StorageMode>
constexpr inline ancestry_t ancestry_for{
    ancestor_table_for<Born, T, StorageMode>.data(),
    ancestor_table_for<Born, T, StorageMode>.size()};

// Build the view-table ancestor table for a target born as (Born, T),
// mirroring `make_ancestor_table`.
//
// A parallel family over the view tables, rather than entries into the
// owning tables: a view's downcast only ever needs dispatch, and routing it
// through owning tables would drag their destroy, relocate, and copy thunks
// into code that never owns anything.
template<Facade Born, typename T, Facade... As>
consteval std::array<ancestor_entry, 1 + sizeof...(As)>
make_view_ancestor_table(std::tuple<As...>*) noexcept {
  return {{{&facade_tag_v<Born>, &vtable_for<Born, T, Born>},
      {&facade_tag_v<As>, &vtable_for<As, T, Born>}...}};
}

template<Facade Born, typename T>
constexpr inline auto view_ancestor_table_for =
    make_view_ancestor_table<Born, T>(
        static_cast<vtbuild_t<Born>::ancestors_t*>(nullptr));

// The birth ancestry for a view over a target born as (Born, T), the object
// every view table of that born family points at.
//
// There is no storage-mode axis here; a view has no storage.
template<Facade Born, typename T>
constexpr inline ancestry_t view_ancestry_for{
    view_ancestor_table_for<Born, T>.data(),
    view_ancestor_table_for<Born, T>.size()};

// `slot` is the flattened dispatch slot, containing one facade method plus the
// facade that declared it.
//
// The owner is `void` while the declaring facade is the one being built,
// since a facade cannot name itself from inside its own list. An `extends`
// entry retags its base's own slots with the base's type as it flattens
// them, so in any facade's flattened list every inherited slot carries its
// declaring facade.
//
// The owner is what keeps per-facade conformance intact through flattening
// (an inherited method binds through `proxy_impl<Owner, T>`), and it is the
// identity that dedup uses to collapse a shared ancestor reached through
// more than one composition path (a diamond). The method's compile-time
// surface is re-exposed so slot packs can be probed the way method packs
// were.
template<typename Owner, typename M>
struct slot {
  using owner_t = Owner;
  using method_t = M;
  static constexpr auto name_v = M::name_v;
  static constexpr bool is_const = M::is_const;
  static constexpr bool is_noexcept = M::is_noexcept;
  using result_t = M::result_t;
};

// Retags base facade `B`'s own slots with `B` itself, leaving inherited slots
// untouched, as `extends<B>` flattens B's list into the derived facade's.
template<Facade B, typename S>
struct retagged {
  using type = S;
};
template<Facade B, typename M>
struct retagged<B, slot<void, M>> {
  using type = slot<B, M>;
};

// Retags all slots in a tuple with `B`.
template<Facade B, typename Slots>
struct retag_slots;
template<Facade B, typename... Ss>
struct retag_slots<B, std::tuple<Ss...>> {
  using type = std::tuple<typename retagged<B, Ss>::type...>;
};
template<Facade B, typename Slots>
using retag_slots_t = retag_slots<B, Slots>::type;

// The first position of type `S` in `Ss`, for dedup's first-occurrence test.
template<typename S, typename... Ss>
consteval size_t first_index_of_type() noexcept {
  constexpr std::array<bool, sizeof...(Ss)> matches{std::same_as<S, Ss>...};
  for (auto ndx = 0UZ; ndx != matches.size(); ++ndx)
    if (matches[ndx]) return ndx;
  return sizeof...(Ss);
}

// Remove duplicate slot types, keeping first occurrences in order.
//
// A slot type recurs when the same ancestor facade is reached through more
// than one composition path, so this is what collapses a diamond to a single
// set of slots (a literal duplicate entry would also collapse here, but
// `is_entry_listed_once` rejects it instead). Per-facade conformance already
// yields one `proxy_impl<Ancestor, T>` regardless of path (the effect of
// Rust's coherence rule); dedup makes the flattened table agree.
template<typename Slots>
struct dedup_slots;
template<typename... Ss>
struct dedup_slots<std::tuple<Ss...>> {
  template<size_t... Ndx>
  static auto keep(std::index_sequence<Ndx...>) -> decltype(std::tuple_cat(
      std::declval<std::conditional_t<first_index_of_type<Ss, Ss...>() == Ndx,
          std::tuple<Ss>, std::tuple<>>>()...));
  using type = decltype(keep(std::index_sequence_for<Ss...>{}));
};
template<typename Slots>
using dedup_slots_t = dedup_slots<Slots>::type;

// Thunk for one slot.
//
// An inherited method binds through its declaring facade, an own method binds
// through `F` itself, so per-facade conformance survives flattening verbatim
// and an upcast handle cannot disagree with the derived one.
template<typename F, typename T, typename S>
consteval auto slot_thunk() noexcept {
  using owner_t = std::conditional_t<std::is_void_v<typename S::owner_t>, F,
      typename S::owner_t>;
  return method_traits<typename S::method_t>::template make_thunk<owner_t,
      T>();
}

// Name that qualifies slot `S`, in a facade whose own name is `OwnName`, which
// is the declaring facade's name for an inherited slot, `OwnName` for an own
// one.
template<fixed_string OwnName, typename S>
consteval auto slot_owner_name() noexcept {
  if constexpr (std::is_void_v<typename S::owner_t>)
    return OwnName;
  else
    return vtbuild_t<typename S::owner_t>::name_v;
}

// Whether the declaring facades of two slots lie in one extends chain:
// identical, or one extends the other, with the facade being built (`void`)
// counting as extending every inherited owner.
template<typename S1, typename S2>
consteval bool have_same_chain_owners() noexcept {
  using o1_t = S1::owner_t;
  using o2_t = S2::owner_t;
  if constexpr (std::is_void_v<o1_t> || std::is_void_v<o2_t> ||
                std::same_as<o1_t, o2_t>)
    return true;
  else
    return vtbuild_t<o1_t>::template extends_facade<o2_t>() ||
           vtbuild_t<o2_t>::template extends_facade<o1_t>();
}

// Whether two same-name slots form a legal overload pair: distinct normalized
// argument lists, or the same arguments with different constness (the
// const-pair idiom, a mutable accessor and a read-only query sharing a name).
//
// The C++ member rules apply: the result type and `noexcept` do not
// overload, so a pair distinguished by nothing else is a collision.
template<typename S1, typename S2>
consteval bool is_legal_overload_pair() noexcept {
  using args1_t = method_traits<typename S1::method_t>::norm_args_t;
  using args2_t = method_traits<typename S2::method_t>::norm_args_t;
  return !std::same_as<args1_t, args2_t> || S1::is_const != S2::is_const;
}

// `no_chain_collision_against` and `owner_names_unique_against` are the two
// collision detonators, each pitting slot `S1` against the whole list.
// Post-dedup, a repeated slot type is only ever the self-pairing, so same-type
// pairs are skipped.
//
// Within one extends chain, a recurring method name is an overload set,
// legal when every pair differs in arguments or constness (see
// `is_legal_overload_pair`), whether the declarations share a facade or span
// levels: a base's `foo()` and a derived facade's `foo(int)` are different
// functions that happen to share a spelling, exactly as within one facade.
// A same-signature recurrence stays an error, since that is redeclaration (or
// overriding), and facades carry no implementations, so there is nothing to
// override. A literal duplicate entry never reaches this check: dedup
// collapses it away, and `is_entry_listed_once` rejects it instead.
//
// Unrelated sibling facades may collide on a method name freely, including
// on the full signature, since every facade is named and the qualified
// spelling is always available as the route to a collided slot that the
// arguments cannot single out; chain authors can see the base, so a chain's
// same-signature recurrence is rejected eagerly instead. Facade names must
// be unique within the composition, or qualified keys could themselves
// collide.
template<typename S1, typename... Ss>
consteval bool no_chain_collision_against() noexcept {
  return (
      (std::same_as<S1, Ss> || S1::name_v != Ss::name_v ||
          !have_same_chain_owners<S1, Ss>() ||
          is_legal_overload_pair<S1, Ss>()) &&
      ...);
}

template<fixed_string OwnName, typename S1, typename... Ss>
consteval bool owner_names_unique_against() noexcept {
  return (
      (std::same_as<typename S1::owner_t, typename Ss::owner_t> ||
          slot_owner_name<OwnName, S1>() != slot_owner_name<OwnName, Ss>()) &&
      ...);
}

// `entry_traits` are the traits over one entry of a facade's declaration list,
// which mixes `method` descriptors with `extends` composition entries.
//
// Each entry contributes slots to the flattened list, base facades to the
// direct-base list, base-table pointers to the dispatch table, and a
// conformance term to `is_all_bound`.
//
// A `method` contributes itself as an own slot. An `extends<B>` contributes
// B's already-flattened slots, retagged so B's own methods carry B, with
// conformance delegated to `B`.
template<typename M>
struct entry_traits {
  using slots_t = std::tuple<slot<void, M>>;
  using bases_t = std::tuple<>;
  using owning_bases_t = std::tuple<>;
  using chain_t = std::tuple<>;

  template<typename F, typename T>
  static constexpr bool is_bound = method_traits<M>::template is_bound<F, T>;
};

template<Facade B>
struct entry_traits<extends<B>> {
  using slots_t = retag_slots_t<B, typename vtbuild_t<B>::flat_slots_t>;
  using bases_t = std::tuple<B>;
  using owning_bases_t =
      std::tuple<const typename vtbuild_t<B>::owning_vtable_t*>;
  using chain_t = decltype(std::tuple_cat(std::declval<std::tuple<B>>(),
      std::declval<typename vtbuild_t<B>::ancestors_t>()));

  template<typename F, typename T>
  static constexpr bool is_bound = vtbuild_t<B>::template is_all_bound<B, T>;
};

template<fixed_string Name>
struct entry_traits<name<Name>> {
  using slots_t = std::tuple<>;
  using bases_t = std::tuple<>;
  using owning_bases_t = std::tuple<>;
  using chain_t = std::tuple<>;

  template<typename F, typename T>
  static constexpr auto is_bound = true;
};

// Facade name carried by one entry of a facade's list, empty for every other
// entry kind.
template<typename E>
struct entry_name {
  static constexpr fixed_string name_v = "";
  static constexpr bool is_name{};
};
template<fixed_string Name>
struct entry_name<name<Name>> {
  static constexpr auto name_v = Name;
  static constexpr auto is_name = true;
};

// Whether entry `E` appears exactly once in the facade's declaration list.
//
// A literal duplicate entry, the identical `method` or `extends` spelled
// twice, is a copy-paste slip with no meaning: dedup would silently collapse
// the duplicate's slots, so the entry list is checked before flattening can
// hide it. A diamond is different, two DISTINCT entries whose chains share an
// ancestor, and stays legal.
template<typename E, typename... Es>
consteval bool is_entry_listed_once() noexcept {
  return (0 + ... + std::same_as<E, Es>) == 1;
}

// Name of a facade with entries `Es`.
//
// The single `name` entry's string, or empty. Concatenation acts as selection,
// because at most one entry contributes a nonempty string.
template<typename... Es>
constexpr inline auto facade_name_of_v =
    (fixed_string{""} + ... + entry_name<Es>::name_v);

// Flattened, deduped slot list and direct-base list of a facade's entry pack.
template<typename... Es>
using flat_slots_of_t = dedup_slots_t<decltype(std::tuple_cat(
    std::declval<typename entry_traits<Es>::slots_t>()...))>;

// Flattened direct-base list of a facade's entry pack.
template<typename... Es>
using bases_of_t = decltype(std::tuple_cat(
    std::declval<typename entry_traits<Es>::bases_t>()...));

// Whether each declared parameter in the `Params` tuple is
// nothrow-constructible from the corresponding call argument.
//
// This is the argument-conversion half of a dispatch's `noexcept`. The
// conversions run in the caller's position, outside the thunk, so when one
// can throw the erased call must not be `noexcept`, letting the exception
// propagate exactly as it does from an `api` forwarder's parameter
// initialization.
template<typename Params, typename... CallArgs>
constexpr inline bool is_nothrow_args_v = false;

template<typename... Ps, typename... CallArgs>
requires(sizeof...(Ps) == sizeof...(CallArgs))
constexpr inline bool is_nothrow_args_v<std::tuple<Ps...>, CallArgs...> =
    (std::is_nothrow_constructible_v<Ps, CallArgs> && ...);

// Parameter type nothing converts to, making a probe overload permanently
// non-viable.
//
// It stands in for the slots outside a key's candidate set, so a `rank_set`
// can span the whole slot list positionally.
struct rank_poison {
  rank_poison() = delete;
};

// Synthetic overload standing in for the candidate slot at index `Ndx` during
// overload ranking.
//
// The call operator's parameters are the slot's declared parameter list and
// its constness mirrors the method's, so the implicit object parameter
// participates exactly as it would in a real member overload set. The result
// type carries the slot index out of a resolved call expression. Probes are
// only ever named in unevaluated contexts, so the operators need no
// definitions.
template<size_t Ndx, const_qual Const, typename ArgsTuple>
struct rank_probe;

template<size_t Ndx, typename... Args>
struct rank_probe<Ndx, const_qual::none, std::tuple<Args...>> {
  std::integral_constant<size_t, Ndx> operator()(Args...);
};

template<size_t Ndx, typename... Args>
struct rank_probe<Ndx, const_qual::present, std::tuple<Args...>> {
  std::integral_constant<size_t, Ndx> operator()(Args...) const;
};

// Synthetic overload set `resolve` hands to the compiler.
//
// A real call expression against it runs genuine C++ overload resolution
// over the candidates, promotions, conversion ranks, and object-parameter
// weighing included, which is what keeps `call<>` and the `api` forwarders
// in exact agreement. A same-signature candidate pair merges legally and
// stays ambiguous at the call, matching the sibling-collision semantics.
template<typename... Probes>
struct rank_set: Probes... {
  using Probes::operator()...;
};

// Strict enforcement's per-method detonator, requiring method `M` to take
// the policy floor `Floor` exactly on an empty handle.
//
// One instantiation per slot, so every method that cannot is reported, each
// with the method named in its instantiation note.
template<typename M, on_empty Floor>
struct empty_fit_check {
  static constexpr bool value =
      (method_traits<M>::empty_behavior(Floor) == Floor);
  static_assert(value,
      "policy_enforcement::strict: this method cannot take the policy's "
      "on_empty behavior exactly on an empty proxy (a noexcept method cannot "
      "raise, and silent needs a value-initializable result); see the "
      "instantiation note for the method, and choose a behavior every method "
      "admits, or lenient enforcement");
};

// `vtable_builder_impl` is the flattened core of the builder, over the full
// slot list `Ss` (bases' methods first, in declaration order, then own,
// deduped), the direct-base facades `Bs`, and the facade's own name `OwnName`.
template<typename FlatSlots, typename Bases, fixed_string OwnName>
struct vtable_builder_impl;

template<typename... Ss, typename... Bs, fixed_string OwnName>
struct vtable_builder_impl<std::tuple<Ss...>, std::tuple<Bs...>, OwnName> {
  static_assert((no_chain_collision_against<Ss, Ss...>() && ...),
      "a method name may recur within one extends chain only as overloads "
      "differing in arguments or constness");
  static_assert((owner_names_unique_against<OwnName, Ss, Ss...>() && ...),
      "facade names must be unique within a composition");

  using flat_slots_t = std::tuple<Ss...>;
  static constexpr auto name_v = OwnName;
  static constexpr auto count_v = sizeof...(Ss);
  static constexpr auto base_count_v = sizeof...(Bs);

  // Flag results of `resolve` outside the valid index range, when no slot
  // answers to the key, or more than one does and the arguments do not single
  // one out.
  static constexpr auto none_v = count_v;
  static constexpr auto ambiguous_v = count_v + 1;

  // Direct-base facade at index `Ndx`.
  template<size_t Ndx>
  using base_t = std::tuple_element_t<Ndx, std::tuple<Bs...>>;

  // Flattened slot at index `Ndx`.
  template<size_t Ndx>
  using slot_t = std::tuple_element_t<Ndx, std::tuple<Ss...>>;

  // Thunk pointer type for each slot, in a tuple.
  using thunks_t = std::tuple<
      typename method_traits<typename Ss::method_t>::thunk_ptr_t...>;

  // Build the thunk tuple for target `T` of facade `F`, containing one thunk
  // per slot, each bound through the slot's declaring facade.
  template<typename F, typename T>
  static consteval thunks_t make_thunks() noexcept {
    return {slot_thunk<F, T, Ss>()...};
  }

  // Build the thunk tuple of an empty handle under the policy floor `Floor`,
  // one empty thunk per slot.
  template<on_empty Floor>
  static consteval thunks_t make_empty_thunks() noexcept {
    return {method_traits<typename Ss::method_t>::template make_empty_thunk<
        Floor>()...};
  }

  // Whether every slot takes `Policy.empty` exactly on an empty handle, which
  // strict enforcement requires and lenient waives.
  //
  // Under strict enforcement the answer is reached through one
  // `empty_fit_check` per slot, so that each offending method is reported by
  // name.
  template<invocable_policy Policy>
  static consteval bool empty_fits_policy() noexcept {
    if constexpr (Policy.enforcement == policy_enforcement::lenient)
      return true;
    else
      return (
          empty_fit_check<typename Ss::method_t, Policy.empty>::value && ...);
  }

  // `vtable_t` is the dispatch table, with one thunk slot per flattened
  // method, plus the address of each direct base's table for the same target
  // type.
  //
  // Dispatch always uses the flattened thunks, so an inherited call costs the
  // same single indexed load as an own one; the base pointers exist for
  // upcasting to follow (Rust's embedded supertrait vtables).
  //
  // `ancestry` names the born family's view ancestry (see
  // `view_ancestry_for`), which is what `try_downcast` on the views and on
  // `shared_proxy` searches. `type_tag` identifies the target type itself
  // (see `type_tag_v`), letting a typed operation on the erased target verify
  // its `T` at runtime, as `extract` and `try_share` do. Those are the
  // table's only slots beyond dispatch, so views still carry no lifetime
  // machinery.
  struct vtable_t {
    thunks_t thunks;
    const ancestry_t* ancestry{};
    const void* type_tag{};
    std::tuple<const typename vtbuild_t<Bs>::vtable_t*...> bases;
  };

  // Whether this facade transitively extends facade `B`.
  //
  // Strict: the search covers the bases and their bases, never this facade
  // itself, so a self-match is false.
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
  static consteval size_t base_route() noexcept {
    constexpr std::array<bool, sizeof...(Bs)> matches{(
        std::same_as<B, Bs> ||
        vtbuild_t<Bs>::template extends_facade<B>())...};
    for (auto ndx = 0UZ; ndx != matches.size(); ++ndx)
      if (matches[ndx]) return ndx;
    return base_count_v;
  }

  // Name that qualifies slot `S`.
  template<typename S>
  static consteval auto owner_name() noexcept {
    return slot_owner_name<OwnName, S>();
  }

  // Whether slot `S` answers to `Key`.
  //
  // An unqualified key matches the method name alone; a qualified key
  // ("facade::method") also requires the qualifying facade's name.
  template<typename S, fixed_string Key>
  // NOLINTNEXTLINE(bugprone-exception-escape): consteval, compile-time only
  static consteval bool slot_matches() noexcept {
    constexpr std::string_view k = Key.view();
    constexpr auto pos = k.find("::");
    constexpr auto qual =
        (pos == k.npos) ? std::string_view{} : k.substr(0, pos);
    constexpr auto base = (pos == k.npos) ? k : k.substr(pos + 2);
    if (S::name_v.view() != base) return false;
    if (qual.empty()) return true;
    constexpr auto owner = owner_name<S>();
    return owner.view() == qual;
  }

  // Per-slot flags over the whole list, marking the slots that answer to `Key`
  // (const-qualified only, when dispatching through a const handle).
  template<fixed_string Key, access_mode Access>
  static consteval std::array<bool, count_v> candidates() noexcept {
    return {(slot_matches<Ss, Key>() &&
             ((Access == access_mode::as_mutable) || Ss::is_const))...};
  }

  // Per-slot flags over the whole list, marking the slots whose declared
  // parameters match `CallArgs` exactly (after normalization) or are viable
  // through the ordinary conversions a call performs.
  template<typename... CallArgs>
  static consteval std::array<bool, count_v> exact_flags() noexcept {
    return {method_traits<typename Ss::method_t>::template is_exact<
        CallArgs...>...};
  }

  template<typename... CallArgs>
  static consteval std::array<bool, count_v> viable_flags() noexcept {
    return {method_traits<typename Ss::method_t>::template is_viable<
        CallArgs...>...};
  }

  // Narrow flag set `a` by flag set `b`, elementwise.
  static consteval std::array<bool, count_v> both(std::array<bool, count_v> a,
      const std::array<bool, count_v>& b) noexcept {
    for (auto ndx = 0UZ; ndx != count_v; ++ndx) a[ndx] = a[ndx] && b[ndx];
    return a;
  }

  // Count of set flags, plus the last set index (`none_v` when none are).
  static consteval std::pair<size_t, size_t> tally(
      const std::array<bool, count_v>& flags) noexcept {
    size_t cnt{};
    size_t at{none_v};
    for (auto ndx = 0UZ; ndx != count_v; ++ndx)
      if (flags[ndx]) {
        ++cnt;
        at = ndx;
      }
    return {cnt, at};
  }

  // Per-slot flags marking the non-const methods, the tiebreak preference for
  // `resolve_exact` through a mutable strict call.
  static consteval std::array<bool, count_v> nonconst_flags() noexcept {
    return {!Ss::is_const...};
  }

  // Tally `flags`, breaking a tie in favor of a unique non-const candidate.
  //
  // This is C++ overload resolution's object-parameter preference, applied
  // as a tiebreak for `resolve_exact`, whose exact-match filter cannot see
  // the object parameter: through a mutable call (`Access` is `as_mutable`), a
  // const pair resolves to its non-const member, as a call on a non-const
  // object would. `resolve` needs no such tiebreak, because the compiler
  // weighs the object parameter itself during ranking.
  template<access_mode Access>
  static consteval std::pair<size_t, size_t>
  tally_preferring_nonconst(const std::array<bool, count_v>& flags) noexcept {
    const auto whole = tally(flags);
    if constexpr (Access == access_mode::as_mutable) {
      if (whole.first > 1) {
        const auto preferred = tally(both(flags, nonconst_flags()));
        if (preferred.first == 1) return preferred;
      }
    }
    return whole;
  }

  // Build the `rank_set` type for `Key`'s candidates.
  //
  // The set spans the whole slot list positionally; a non-candidate slot
  // contributes a `rank_poison` overload no call can select, so the winning
  // index needs no translation.
  template<fixed_string Key, access_mode Access, size_t... Ndx>
  static consteval auto make_rank_set(std::index_sequence<Ndx...>) noexcept {
    return std::type_identity<
        rank_set<std::conditional_t<candidates<Key, Access>()[Ndx],
            rank_probe<Ndx, slot_t<Ndx>::method_t::const_qualifier,
                typename slot_t<Ndx>::method_t::args_t>,
            rank_probe<Ndx, const_qual::none, std::tuple<rank_poison>>>...>>{};
  }

  // `resolve` to resolve the `Key`, called with `CallArgs`, to a slot index,
  // or to `none_v` or `ambiguous_v`.
  //
  // An unqualified key with a single candidate resolves to it
  // unconditionally, so a call with unsuitable arguments still fails
  // directly at the thunk; a qualified key narrows the candidates to one
  // facade's before the same rules run. An `Access` of `as_const` restricts
  // the candidates to const-qualified methods, for dispatch through const
  // handles.
  //
  // A key over an overload set (per-name overloads within a facade, or a
  // sibling collision) is ranked by the compiler rather than by a
  // reimplementation of its rules: the candidates become a synthetic
  // overload set (`make_rank_set`) and a real call expression against
  // `CallArgs` picks the winner, whose result type carries the slot index.
  // Dispatch therefore agrees with the `api` forwarders everywhere,
  // promotions, conversion ranks, and the object-parameter weighing
  // included. An ill-formed ranking call is classified by per-slot
  // viability: some viable candidate means the call is ambiguous, none
  // means nothing matched.
  template<fixed_string Key, access_mode Access, typename... CallArgs>
  static consteval size_t resolve() noexcept {
    constexpr auto cand = candidates<Key, Access>();
    const auto [cnt, at] = tally(cand);
    if (cnt < 2) return cnt ? at : none_v;
    using set_t = decltype(make_rank_set<Key, Access>(
        std::make_index_sequence<count_v>{}))::type;
    using probe_t = conditional_const_t<Access, set_t>;
    // cl C4244: narrowing here is the ranked overload's own argument
    // conversion, chosen by design.
    PRAGMA_DIAG(push)
    PRAGMA_MSVC_IGNORED(4244)
    if constexpr (requires(probe_t& p) { p(std::declval<CallArgs>()...); })
      return decltype(std::declval<probe_t&>()(
          std::declval<CallArgs>()...))::value;
    else
      return tally(both(cand, viable_flags<CallArgs...>())).first
                 ? ambiguous_v
                 : none_v;
    PRAGMA_DIAG(pop)
  }

  // `resolve_exact` to resolve `Key` to the unique candidate whose declared
  // parameters match `CallArgs` exactly, with a constness tiebreak standing
  // in for the object-parameter weighing that `resolve` gets from the
  // compiler.
  //
  // This is the validation probe's strictness: a merely-viable signature does
  // not count. The tiebreak is what lets the probe's mutable strict call
  // single out the non-const member of a const pair.
  template<fixed_string Key, access_mode Access, typename... CallArgs>
  static consteval size_t resolve_exact() noexcept {
    const auto [cnt, at] = tally_preferring_nonconst<Access>(
        both(candidates<Key, Access>(), exact_flags<CallArgs...>()));
    if (cnt == 1) return at;
    return cnt ? ambiguous_v : none_v;
  }

  // Whether any method answering to `Key` is const-qualified.
  //
  // The gate on dispatching `Key` through a const handle. False for an unknown
  // key, since rejecting one is `resolve`'s job.
  template<fixed_string Key>
  static consteval bool is_const() noexcept {
    return ((slot_matches<Ss, Key>() && Ss::is_const) || ...);
  }

  // Whether the call `Key` resolves to, with `CallArgs`, dispatches a noexcept
  // method through nothrow argument conversions.
  //
  // The conversion term keeps the two spellings in agreement. An `api`
  // forwarder converts its arguments in the caller, outside its own
  // `noexcept`, so a throwing conversion propagates; the erased call
  // converts at the thunk invocation, inside the dispatcher, so the
  // dispatcher may be `noexcept` only when those conversions cannot throw.
  //
  // False when the call does not resolve.
  template<fixed_string Key, access_mode Access, typename... CallArgs>
  static consteval bool is_noexcept() noexcept {
    constexpr std::array<bool, count_v> flags{Ss::is_noexcept...};
    constexpr auto ndx = resolve<Key, Access, CallArgs...>();
    if constexpr (ndx < count_v)
      return flags[ndx] &&
             is_nothrow_args_v<typename slot_t<ndx>::method_t::args_t,
                 CallArgs...>;
    else
      return false;
  }

  // Declared result type of the exact-match candidate for `Key`, or `void`
  // when there is none.
  //
  // The permissive fallback keeps return-type substitution in the `api_probe`
  // from hard-erroring before its constraint can reject the key.
  template<fixed_string Key, access_mode Access, typename... CallArgs>
  static consteval auto do_result_of() noexcept {
    constexpr auto ndx = resolve_exact<Key, Access, CallArgs...>();
    if constexpr (ndx < count_v) {
      using s_t = std::tuple_element_t<ndx, std::tuple<Ss...>>;
      return std::type_identity<typename s_t::result_t>{};
    } else {
      return std::type_identity<void>{};
    }
  }

  // Declared result type of the exact-match candidate for `Key`, or `void`
  // when there is none.
  template<fixed_string Key, access_mode Access, typename... CallArgs>
  using result_of_t = decltype(do_result_of<Key, Access, CallArgs...>())::type;

  // Whether `Args` match the declared parameters of exactly one candidate for
  // `Key`, after normalization, rather than by convertibility.
  //
  // False for an unknown key. This is the `api_probe`'s constraint.
  template<fixed_string Key, access_mode Access, typename... Args>
  static consteval bool has_exact_args() noexcept {
    return resolve_exact<Key, Access, Args...>() < count_v;
  }
};

// Facade-wide dispatch machinery, specialized on the `facade` base to get at
// the entry pack.
template<typename... Es>
struct vtable_builder<facade<Es...>>
    : vtable_builder_impl<flat_slots_of_t<Es...>, bases_of_t<Es...>,
          facade_name_of_v<Es...>> {
  static_assert((0 + ... + entry_name<Es>::is_name) == 1,
      "every facade must carry exactly one name entry");
  static_assert((is_entry_listed_once<Es, Es...>() && ...),
      "a facade may not list the identical method or extends entry twice");

  using impl_t = vtable_builder_impl<flat_slots_of_t<Es...>, bases_of_t<Es...>,
      facade_name_of_v<Es...>>;
  using vtable_t = impl_t::vtable_t;

  // `is_all_bound` is whether every method of the facade, inherited and own,
  // has a usable binding for `T`.
  //
  // Own methods bind through `proxy_impl<F, T>`; inherited ones recurse into
  // their declaring facade, so conforming to a composed facade requires
  // conforming to each of its bases.
  //
  // This is binding-existence only, vacuously true when the facade contributes
  // no methods; the per-pair opt-in that keeps vacuity from conforming lives
  // in `Proxiable`.
  template<typename F, typename T>
  static constexpr bool is_all_bound =
      (entry_traits<Es>::template is_bound<F, T> && ...);

  // Creates view-table pointers of the direct bases, for the same born family
  // and target, built over the direct-base pack (carried by the tuple pointer
  // parameter, as in `make_ancestor_table`).
  //
  // A member with a spelled-out return type for the same reason as
  // `make_owning_bases`: the view ancestry's back-references reach a
  // sibling's table build mid-instantiation in a diamond, which the shared
  // per-entry hook this replaced could not survive.
  template<typename T, typename Born, typename... Bs2>
  static consteval std::tuple<const typename vtbuild_t<Bs2>::vtable_t*...>
  make_view_bases(std::tuple<Bs2...>*) noexcept {
    return {&vtable_for<Bs2, T, Born>...};
  }

  // Build the dispatch table for target `T` of facade `F`, born as facade
  // `Born`.
  template<typename F, typename T, typename Born>
  static consteval vtable_t make_vtable() noexcept {
    return {impl_t::template make_thunks<F, T>(), &view_ancestry_for<Born, T>,
        &type_tag_v<T>,
        make_view_bases<T, Born>(static_cast<bases_of_t<Es...>*>(nullptr))};
  }

  // Direct-base owning-table pointers, mirroring `vtable_t::bases`.
  using owning_bases_t = decltype(std::tuple_cat(
      std::declval<typename entry_traits<Es>::owning_bases_t>()...));

  // Every facade this one transitively extends, deduped (a diamond's shared
  // ancestor appears once), not including this facade itself.
  //
  // The birth ancestry `try_downcast` searches is built over it.
  using ancestors_t = dedup_slots_t<decltype(std::tuple_cat(
      std::declval<typename entry_traits<Es>::chain_t>()...))>;

  // `owning_vtable_t` is the owning dispatch table.
  //
  // Carries housekeeping slots alongside the facade methods, plus the address
  // of each direct base's owning table for the same birth, target type, and
  // storage mode, which is what makes owning upcasts a pointer read (mirroring
  // `vtable_t::bases`).
  //
  // A null `relocate` marks a heap-stored target, which moves by pointer
  // steal rather than relocation. A null `copy` marks a target that is not
  // copy-constructible. `size` and `align` describe the target (its type
  // identity is `vt.type_tag`), so an adopting proxy can check the fit of an
  // erased arrival against its own buffer.
  //
  // The mode-changing pairs point across to the table's other-mode sibling:
  // an inline-mode table carries `to_heap` (see `box`) plus `heap_table`,
  // which is how a `heap_only` proxy adopts an erased inline arrival, and a
  // heap-mode table carries `to_inline` (see `unbox`) plus `inline_table`
  // (null when the target is not nothrow-move-constructible and so can never
  // live inline), the un-boxing inverse for an `inline_only` proxy.
  //
  // `ancestry` names the born family's birth ancestry for this mode (see
  // `ancestry_for`): the facade the target was constructed as plus every
  // facade it extends, which is what `try_downcast` searches.
  struct owning_vtable_t {
    vtable_t vt;
    void (*destroy)(void*) noexcept {};
    void (*relocate)(void*, void*) noexcept {};
    void* (*copy)(const void*, void*){};
    void* (*to_heap)(void*){};
    void (*to_inline)(void*, void*) noexcept {};
    const owning_vtable_t* heap_table{};
    const owning_vtable_t* inline_table{};
    size_t size{};
    size_t align{};
    const ancestry_t* ancestry{};
    owning_bases_t bases;
  };

  // `make_owning_bases` to build owning-table pointers of the direct bases,
  // for the same born family, target, and mode, built over the direct-base
  // pack (carried by the tuple pointer parameter, as in
  // `make_ancestor_table`).
  //
  // Deliberately a member rather than an `entry_traits` hook, and with a
  // spelled-out return type: an entry's helper is shared by every facade
  // listing that entry, and in a diamond the ancestry's back-references
  // reach a sibling's table build while the shared helper is still
  // mid-instantiation, which a deduced return type cannot survive. A member
  // is unique per (facade, born, type, mode) and so is only ever entered
  // once.
  template<typename Born, typename T, storage_mode StorageMode,
      typename... Bs2>
  static consteval owning_bases_t
  make_owning_bases(std::tuple<Bs2...>*) noexcept {
    return {&owning_vtable_for<Bs2, Born, T, StorageMode>...};
  }

  // `make_owning_vtable` to build the owning table for target `T` born as
  // facade `Born`, stored `inlined` or `dynamic` per `StorageMode`.
  //
  // The mode is decided at proxy construction from the handle's policy, so it
  // is part of the table's identity rather than a property of the type alone;
  // the birth is decided there too, as the facade under construction.
  //
  // The embedded `vt` is a copy of the standalone view-table instance rather
  // than a second `make_vtable` call: both spellings would share one function
  // specialization, and the view ancestry's back-references re-enter it while
  // it is mid-instantiation from here, the same diamond hazard as
  // `make_owning_bases`. Reading the completed variable keeps `make_vtable`
  // entered from exactly one place, its own `vtable_for`.
  template<typename F, typename Born, typename T, storage_mode StorageMode>
  static consteval owning_vtable_t make_owning_vtable() noexcept {
    owning_vtable_t ovt{vtable_for<F, T, Born>, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, sizeof(T), alignof(T),
        &ancestry_for<Born, T, StorageMode>,
        make_owning_bases<Born, T, StorageMode>(
            static_cast<bases_of_t<Es...>*>(nullptr))};
    static_assert(StorageMode != storage_mode::direct,
        "a proxy table is inlined or dynamic; see proxy_storage_mode_of");
    // The housekeeping slots erase the shared primitives (invocable_common.h)
    // here, the last place the target type is seen, as `make_thunk` does for
    // dispatch.
    if constexpr (StorageMode == storage_mode::inlined) {
      ovt.destroy = [](void* target) noexcept {
        destroy_inline(static_cast<T*>(target));
      };
      ovt.relocate = [](void* from, void* to) noexcept {
        relocate_inline(static_cast<T*>(from), to);
      };
      ovt.to_heap = [](void* from) -> void* {
        return box(static_cast<T*>(from));
      };
      ovt.heap_table = &owning_vtable_for<F, Born, T, storage_mode::dynamic>;
      if constexpr (std::is_copy_constructible_v<T>)
        ovt.copy = &inline_copy<T>;
    } else {
      ovt.destroy = [](void* target) noexcept {
        destroy_heap(static_cast<T*>(target));
      };
      if constexpr (std::is_nothrow_move_constructible_v<T>) {
        ovt.to_inline = [](void* from, void* to) noexcept {
          unbox(static_cast<T*>(from), to);
        };
        ovt.inline_table =
            &owning_vtable_for<F, Born, T, storage_mode::inlined>;
      }
      if constexpr (std::is_copy_constructible_v<T>) ovt.copy = &heap_copy<T>;
    }
    return ovt;
  }

  // `make_empty_view_bases` and `make_empty_owning_bases` create the
  // base-table pointers of an empty table, leading to the direct bases' empty
  // tables of the same floor, built over the direct-base pack like
  // `make_view_bases`.
  template<on_empty Floor, typename... Bs2>
  static consteval std::tuple<const typename vtbuild_t<Bs2>::vtable_t*...>
  make_empty_view_bases(std::tuple<Bs2...>*) noexcept {
    return {&empty_vtable_for<Bs2, Floor>...};
  }

  template<on_empty Floor, typename... Bs2>
  static consteval owning_bases_t
  make_empty_owning_bases(std::tuple<Bs2...>*) noexcept {
    return {&empty_owning_vtable_for<Bs2, Floor>...};
  }

  // Build the dispatch table of an empty handle under the policy floor.
  template<on_empty Floor>
  static consteval vtable_t make_empty_vtable() noexcept {
    return {impl_t::template make_empty_thunks<Floor>(), nullptr, nullptr,
        make_empty_view_bases<Floor>(
            static_cast<bases_of_t<Es...>*>(nullptr))};
  }

  // Build the owning table of an empty `proxy` of facade `F` under the policy
  // floor `Floor`; see `empty_owning_vtable_for`.
  //
  // The embedded `vt` is a copy of the standalone instance, as in
  // `make_owning_vtable`. `relocate` is the one non-null housekeeping slot;
  // see `empty_relocate`.
  template<typename F, on_empty Floor>
  static consteval owning_vtable_t make_empty_owning_vtable() noexcept {
    return {.vt = empty_vtable_for<F, Floor>,
        .relocate = &empty_relocate,
        .bases = make_empty_owning_bases<Floor>(
            static_cast<bases_of_t<Es...>*>(nullptr))};
  }
};

// Facade-to-table alias templates, which the families `upcast_table` walks
// over.
template<Facade F>
using view_table_t = vtbuild_t<F>::vtable_t;
template<Facade F>
using owning_table_t = vtbuild_t<F>::owning_vtable_t;

// Upcast to narrow a table pointer from facade `D` to `B`, where `B` is `D`
// itself or a facade it extends, by following the embedded direct-base table
// pointers.
//
// The shared walk behind `upcast_vtable` and `upcast_owning_vtable`, which
// differ only in the table family `TableFor` selects. The route is resolved
// at compile time; the runtime cost is one dependent load per composition
// level crossed.
template<template<Facade> class TableFor, Facade B, Facade D>
[[nodiscard]] constexpr const TableFor<B>*
upcast_table(const TableFor<D>* vt) noexcept {
  if constexpr (std::same_as<B, D>) {
    return vt;
  } else {
    constexpr auto ndx = vtbuild_t<D>::template base_route<B>();
    static_assert(ndx != vtbuild_t<D>::base_count_v,
        "the source facade does not extend the target facade");
    return upcast_table<TableFor, B,
        typename vtbuild_t<D>::template base_t<ndx>>(std::get<ndx>(vt->bases));
  }
}

// `upcast_vtable` and `upcast_owning_vtable` are the `upcast_table`s over the
// dispatch tables and the owning tables.
template<Facade B, Facade D>
[[nodiscard]] constexpr auto
upcast_vtable(const typename vtbuild_t<D>::vtable_t* vt) noexcept
    -> const vtbuild_t<B>::vtable_t* {
  return upcast_table<view_table_t, B, D>(vt);
}

template<Facade B, Facade D>
[[nodiscard]] constexpr auto
upcast_owning_vtable(const typename vtbuild_t<D>::owning_vtable_t* vt) noexcept
    -> const vtbuild_t<B>::owning_vtable_t* {
  return upcast_table<owning_table_t, B, D>(vt);
}

// Shared body of every handle's `call`.
//
// Resolves `Key` against facade `F`'s slot list, surfaces the user-facing
// errors, and invokes the thunk on the erased target. `Access` is the
// calling handle's access mode.
template<Facade F, access_mode Access, fixed_string Key, typename ErasedPtr,
    typename... Args>
constexpr decltype(auto)
dispatch(const typename vtbuild_t<F>::thunks_t& tks, ErasedPtr target,
    Args&&... args) noexcept(vtbuild_t<F>::template is_noexcept<Key, Access,
    Args...>()) {
  constexpr auto ndx = vtbuild_t<F>::template resolve<Key, Access, Args...>();
  static_assert(ndx != vtbuild_t<F>::none_v, "no matching signature");
  static_assert(ndx != vtbuild_t<F>::ambiguous_v,
      "ambiguous method call; qualify the key with the facade name, or match "
      "one overload's arguments exactly");
  // cl C4244: narrowing here is the ranked overload's own argument
  // conversion, chosen by design.
  PRAGMA_DIAG(push)
  PRAGMA_MSVC_IGNORED(4244)
  return std::get<ndx>(tks)(target, std::forward<Args>(args)...);
  PRAGMA_DIAG(pop)
}

} // namespace details

#pragma endregion
#pragma region Proxiable

// The concept for when facade `D` (transitively) extends facade `B` through
// `extends` composition entries.
//
// Strict: false when `D` is `B` itself. `ExtendsOrIs` is the reflexive
// form.
template<typename D, typename B>
concept Extends =
    Facade<D> && Facade<B> &&
    details::vtbuild_t<D>::template extends_facade<B>();

// `InChainOf` is the concept for when facade `B` is `D` itself or a facade `D`
// (transitively) extends.
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

// `ExtendsOrIs` is the concept for when facade `D` is facade `B` itself or
// (transitively) extends it. It is `Extends` made reflexive, in `Extends`'s
// argument order.
//
// This is the constraint on a conversion's source facade, where `InChainOf`
// is the same relation from the hook's side.
template<typename D, typename B>
concept ExtendsOrIs = InChainOf<B, D>;

// Fwd.
template<Facade F>
class proxy_view;
template<Facade F>
class const_proxy_view;
template<Facade F, invocable_policy Policy = invocable_policy{}>
class proxy;
template<Facade F>
class shared_proxy;
template<Facade F>
class const_shared_proxy;
template<Facade F>
class weak_proxy;
template<Facade F>
class const_weak_proxy;

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
template<Facade F, invocable_policy P>
struct handle_facade<proxy<F, P>> {
  using type = F;
};
template<Facade F>
struct handle_facade<shared_proxy<F>> {
  using type = F;
};
template<Facade F>
struct handle_facade<const_shared_proxy<F>> {
  using type = F;
};

// Whether `T` is a proxy handle whose facade is `F` or extends it, the set
// served by the dedicated viewing and upcasting constructors.
//
// The generic erased-target constructors exclude it, so re-pointing at such a
// handle's target always wins over wrapping the handle itself as a target.
template<typename T, typename F>
consteval bool is_handle_for() noexcept {
  using G = handle_facade<T>::type;
  if constexpr (std::is_void_v<G>)
    return false;
  else
    return ExtendsOrIs<G, F>;
}

// The `Key` qualified by facade `F`'s name.
//
// The library's self-conformance bindings forward through this spelling: `Key`
// there is always one of `F`'s own method names, so the qualified key stays
// unambiguous on a derived handle even when the derived facade's flattened
// list collides on `Key`.
template<Facade F, fixed_string Key>
consteval auto qualified_key() noexcept {
  return vtbuild_t<F>::name_v + fixed_string{"::"} + Key;
}

} // namespace details

// `Proxiable` is the concept for when `T` can back facade `F`.
//
// Satisfied when a usable `proxy_impl<F, T>` binding exists for every method
// of `F`, and the pair itself has opted in: it is registered, or `T` is a
// proxy handle of `F` or of a facade extending it (the library's
// self-conformance bindings).
//
// The explicit opt-in term is what keeps a facade with no own methods, a
// name-only marker or a pure aggregation level, from being backed by every
// type in the program: binding-existence alone is vacuously true there.
//
// This is the gate on proxy construction and doubles as the trait bound for
// static-dispatch templates.
template<typename T, typename F>
concept Proxiable =
    Facade<F> && details::vtbuild_t<F>::template is_all_bound<F, T> &&
    (ProxyRegistered<F, T> || details::is_handle_for<T, F>());

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

// Result type of the probe's strict `call`.
//
// This is `strict_result` for object types, `void` for `void`, and references
// passed through unchanged.
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
// `has_exact_args`), and the result converts only to exactly the declared
// result type. Never constructed or executed; it exists to be type-checked.
template<Facade F>
struct api_probe: api_base_t<F> {
  template<fixed_string Key, typename... Args>
  requires(vtbuild_t<F>::template has_exact_args<Key, access_mode::as_mutable,
      Args...>())
  strict_return_t<typename vtbuild_t<F>::template result_of_t<Key,
      access_mode::as_mutable, Args...>>
  call(Args&&...) {
    std::terminate();
  }

  template<fixed_string Key, typename... Args>
  requires(vtbuild_t<F>::template has_exact_args<Key, access_mode::as_const,
      Args...>())
  // NOLINTNEXTLINE(modernize-use-nodiscard): mirrors `call`, never executed.
  strict_return_t<typename vtbuild_t<F>::template result_of_t<Key,
      access_mode::as_const, Args...>> call(Args&&...) const {
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
requires ExtendsOrIs<G, F>
consteval auto corvid_proxy_spec(F*, api_probe<G>*) noexcept {
  return make_proxy_spec<F, api_probe<G>, api_check::off>();
}

} // namespace details

// `validate_api` is to validate a facade's `api` against its method list,
// spelling neither the names nor the signatures again.
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
// reference-to-value decay of a declared result, a body dispatching the
// wrong key with an identical signature, and a missing overload forwarder
// whose probe call a same-name sibling forwarder absorbs by conversion,
// landing exactly on the sibling's slot.
//
// Limitation: a composed facade whose flattened list collides on a
// same-signature method name cannot validate, because the boilerplates drive
// the probe by natural name, which is exactly the spelling the collision
// makes ambiguous. Such a facade registers with `api_check::off`; its base
// levels still validate normally.
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

namespace details {

// Whether every facade in `F`'s extends chain has a boilerplate impl visible
// to drive `F`'s api probe.
//
// This is the registration-time guard for the base half of the requirement
// stated above: `validate_api` runs the probe through each base's
// boilerplate, so a missing one must be caught before the table build turns
// it into an incomplete-type hard error. The tuple pointer parameter carries
// `vtbuild_t<F>::ancestors_t` in deducible position.
template<Facade F, Facade... Bs>
consteval bool do_are_base_boilerplates_visible(std::tuple<Bs...>*) noexcept {
  return (... && requires { sizeof(proxy_impl<Bs, api_probe<F>>); });
}

template<Facade F>
[[nodiscard]] consteval bool are_base_boilerplates_visible() noexcept {
  return do_are_base_boilerplates_visible<F>(
      static_cast<vtbuild_t<F>::ancestors_t*>(nullptr));
}

} // namespace details

#pragma endregion

} // namespace prox

#pragma region Exports

// Call-site vocabulary, exported to `corvid::meta`.
//
// Consuming an erased handle is ordinary type usage, as in
// `do_stuff(proxy_view<foo_like>)`, so these names belong in the wider
// namespace.
//
// The authoring vocabulary generally stays inside `prox`, since those names
// are too generic to export, and facade and impl authors are already working
// in that domain.
using prox::Proxiable;
using prox::proxy_impl_base;

#pragma endregion

}} // namespace corvid::meta
