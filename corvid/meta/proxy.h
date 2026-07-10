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
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
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
//
// Template parameter conventions, used consistently throughout this header:
//
// - `F`: the facade being dispatched, bound, or validated.
// - `B`: a base facade, one that `F` or `D` extends.
// - `D`: a derived facade, one that extends `F` or `B`.
// - `G`: a second facade, where two vary independently.
// - `T`: the concrete target type behind a handle.
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
// - `Policy`: an owning proxy's storage policy, a `proxy_policy` value; `P`
//      where a second handle's policy varies independently.
namespace corvid { inline namespace meta {
namespace prox {

#pragma region Method and key

// `method_key`: a tag carrying a method name as a compile-time string.
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

// `method_base`: shared base for the four `method` flavors (`const` crossed
// with `noexcept`).
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

// `method`: Method descriptor, a name plus the erased signature.
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
// `method_key<Key>` from a method argument. This also leaves the door open for
// bindings that overload on the full method (signature included) if per-name
// overload sets are ever supported.
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

// `facade`: facade base, an interface definition.
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
//        static int on(method_key<"speak">, const T& t) {
//          return t.speak();
//        }
//      };
//    };
//
// With that in place, `handle.speak()` is sugar for `handle.call<"speak">()`,
// dispatching through the same table.
//
// The facade body is also the normal home of the boilerplate impl; see also
// `proxy_impl`.
template<typename... Methods>
struct facade {};

namespace details {

// `probe`: Probe for the unique public `facade` base of `F`.
// Declared only; used in unevaluated contexts.
template<typename... Ms>
auto probe(const facade<Ms...>&) -> facade<Ms...>;

} // namespace details

// `Facade`: concept for a type derived from a single `facade` base.
template<typename F>
concept Facade = requires(const F& f) { details::probe(f); };

// `extends`: facade composition entry declaring that the facade extends `B`
// (Rust supertrait, ngcpp `add_facade`).
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
// A method name may not recur within one extends chain: a facade cannot
// declare a name twice or redeclare (or override) an inherited one, which is
// by design, because facades carry no implementations and there is nothing to
// override.
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

// `name`: facade name entry, giving the facade a formal name, which qualifies
// its methods.
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
// would ideally be globally unique.
template<fixed_string Name>
struct name {};

namespace details {

// `no_api`: stand-in API base for facades that define no member-call sugar.
struct no_api {};

// `api_base`: sugar base for handles of facade `F`.
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
#pragma region Proxy policy

// `proxy_alloc`: allocation strategy for the owning `proxy`.
//
// `sbo_or_heap` stores a target inline when it is eligible and efficient (see
// `proxy_policy`), and on the heap otherwise.
//
// `sbo_only` forbids the heap path: constructing a proxy over an ineligible
// target is a compile error, and a heap target arriving through a
// converting move is un-boxed into the buffer, throwing `std::length_error`
// if it does not fit (the one runtime failure, since an erased target's size
// cannot be checked statically).
//
// `heap_only` forbids the inline path, so every target has a stable heap
// address, and the proxy carries no inline buffer at all; an inline target
// arriving through a converting move is re-boxed onto the heap.
enum class proxy_alloc : std::uint8_t {
  sbo_only = 1 << 0,
  heap_only = 1 << 1,
  sbo_or_heap = sbo_only | heap_only
};

// `proxy_policy`: per-handle storage policy for the owning `proxy`, used as
// its second template parameter.
//
// The default reproduces the baseline proxy: a two-pointer inline buffer at
// `std::max_align_t` alignment, falling back to the heap. A facade whose
// typical targets are a little too big for the default buffer can be handled
// with a larger `sbo_size`.
//
// A target is stored inline when it fits `sbo_size` and `sbo_align` and is
// nothrow-move-constructible (a proxy move relocates an inline target, and
// proxy moves are unconditionally `noexcept`). The exception is when the
// source is on the heap and the target allows heap storage, in which case only
// the pointer is moved.
//
// Policies are checked at proxy construction, not at registration.
// Registration is per (facade, type) and knows nothing about any particular
// handle's storage. One facade can serve proxies of different policies, and
// views, simultaneously.
struct proxy_policy {
  std::size_t sbo_size{2 * sizeof(void*)};
  std::size_t sbo_align{alignof(std::max_align_t)};
  proxy_alloc alloc{proxy_alloc::sbo_or_heap};

  friend constexpr bool
  operator==(const proxy_policy&, const proxy_policy&) = default;
};

namespace details {

// `sbo_fits`: whether `T` is eligible for policy `P`'s inline buffer.
template<typename T>
consteval bool sbo_fits(proxy_policy p) noexcept {
  return sizeof(T) <= p.sbo_size && alignof(T) <= p.sbo_align &&
         std::is_nothrow_move_constructible_v<T>;
}

// `can_store_inline`: whether policy `P` can store `T` inline.
//
// An `sbo_only` policy over an ineligible target is rejected separately, with
// its own diagnostic.
template<typename T>
consteval bool can_store_inline(proxy_policy p) noexcept {
  return p.alloc != proxy_alloc::heap_only && sbo_fits<T>(p);
}

// `inline_fit_guaranteed`: whether every inline target the source policy
// admits is guaranteed to fit the destination's buffer, letting adoption skip
// the runtime fit check (and, with it, every mode-changing path for inline
// arrivals).
consteval bool
inline_fit_guaranteed(proxy_policy to, proxy_policy from) noexcept {
  return to.alloc != proxy_alloc::heap_only && to.sbo_size >= from.sbo_size &&
         to.sbo_align >= from.sbo_align;
}

// `adopt_may_throw`: whether adopting from policy `from` into policy `to` can
// throw.
//
// Could be an inline arrival that might not stay inline (a re-boxing
// allocation, or nowhere at all to put it under `sbo_only`), or a heap arrival
// that must un-box into an `sbo_only` buffer it might not fit.
consteval bool adopt_may_throw(proxy_policy to, proxy_policy from) noexcept {
  const bool from_sbo = from.alloc != proxy_alloc::heap_only;
  const bool from_heap = from.alloc != proxy_alloc::sbo_only;
  return (from_sbo && !inline_fit_guaranteed(to, from)) ||
         (from_heap && to.alloc == proxy_alloc::sbo_only);
}

} // namespace details

#pragma endregion
#pragma region Registration and binding

// `proxy_impl_base` is a convenience base for binding classes: a facade's
// `boilerplate`, or a registration-carried impl.
//
// Inheriting it is optional, and a binding class that inherits a boilerplate
// already has it through that base. It also serves as documentation of what
// the class is for.
struct proxy_impl_base {
  template<fixed_string Name>
  using method_key = prox::method_key<Name>;
};

// `proxy_impl` is a binding point between a facade and a concrete type (Rust's
// `impl Trait for Type`).
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

// `proxy_spec`: minimal registration spec.
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

// `api_check`: whether registering a pair also validates the facade's `api`
// (see `validate_api`).
//
// A facade whose `api` deliberately deviates from the method list (say, a
// widening convenience signature), must register with `api_check::off`.
enum class api_check : std::uint8_t { off, on };

namespace details {

// `maybe_validate_api`: registration-time half of `validate_api`.
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
    if constexpr (has_boilerplate) (void)validate_api<F>();
  }
}

} // namespace details

// `make_proxy_spec`: make the registration spec for a (facade, type) pair.
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

// `ProxyRegistered`: concept for a (facade, type) pair being registered.
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

// `SpecCarriesImpl`: concept for a (facade, type) pair whose registration
// carries an impl.
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
    !std::is_void_v<typename decltype(corvid_proxy_spec(
        static_cast<F*>(nullptr), static_cast<T*>(nullptr)))::impl_t>;

namespace details {

// `registered_impl_t`: impl type carried by the pair's registration.
template<typename F, typename T>
using registered_impl_t = decltype(corvid_proxy_spec(static_cast<F*>(nullptr),
    static_cast<T*>(nullptr)))::impl_t;

} // namespace details

// `proxy_impl`: library-provided delegation to a registration-carried impl.
//
// The registration names the binding class, so the impl is explicitly
// registered like everything else, and the class itself can be local to the
// hook or nest inside the type it serves (a hook is implicitly inline, so a
// hook-local class is ODR-consistent across translation units).
template<Facade F, typename T>
requires SpecCarriesImpl<F, T>
struct proxy_impl<F, T>: details::registered_impl_t<F, T> {};

// `proxy_impl`: library-provided delegation to a facade-hosted boilerplate
// impl.
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

// `proxy_spec_v`: central access point for the registered spec, mirroring
// `enum_spec_v`.
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

// `method_traits_base`: per-method dispatch machinery, shared by the four
// erased-signature flavors (`const` crossed with `noexcept`).
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

  // `norm_args_t`: parameter list normalized for exact-match probing.
  //
  // Top-level cv and references stripped from each parameter, so
  // value-category spelling is ignored, but a merely-convertible type does not
  // match.
  using norm_args_t = std::tuple<std::remove_cvref_t<Args>...>;

  // `exact_v`, `viable_v`: whether `CallArgs` match the declared parameters
  // exactly after normalization, and whether they are merely viable through
  // the ordinary conversions a call performs.
  //
  // Exactness is the validation probe's strictness; viability is overload-set
  // resolution's fallback.
  template<typename... CallArgs>
  static constexpr bool exact_v =
      std::same_as<std::tuple<std::remove_cvref_t<CallArgs>...>, norm_args_t>;
  template<typename... CallArgs>
  static constexpr bool viable_v =
      std::is_invocable_v<R (*)(Args...), CallArgs...>;

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

// `sbo_destroy`, `heap_destroy`, `sbo_relocate`: housekeeping thunks for the
// owning `proxy`, the analog of Rust's drop glue.
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

// `sbo_copy`, `heap_copy`: copy thunks, present in the owning table only for
// copy-constructible targets.
//
// Both return the copy's address. `sbo_copy` copy-constructs `*from` into
// the buffer at `to` (and the return is that same address); `heap_copy`
// allocates the copy, ignoring `to`. Either can throw (the target's copy
// constructor, or the allocation), so unlike the other housekeeping thunks
// they are not `noexcept`; the caller publishes the result only on success.
template<typename T>
void* sbo_copy(const void* from, void* to) {
  return ::new (to) T(*static_cast<const T*>(from));
}

template<typename T>
void* heap_copy(const void* from, void* /*to*/) {
  return new T(*static_cast<const T*>(from));
}

// `sbo_to_heap`, `heap_to_sbo`: mode-changing thunks, for adopting a target
// into a proxy whose policy demands the other storage mode.
//
// `sbo_to_heap` re-boxes: it moves an inline target into a fresh heap
// allocation and destroys the inline source. Inline eligibility guarantees
// a nothrow move, so only the allocation can throw, and it throws before
// the source is touched.
//
// `heap_to_sbo` un-boxes: it moves a heap target into the buffer at `to` and
// frees the allocation; nothing can throw, but the caller must first verify
// the fit through the table's `size` and `align`.
template<typename T>
void* sbo_to_heap(void* from) {
  auto* source = static_cast<T*>(from);
  auto* target = new T(std::move(*source));
  source->~T();
  return target;
}

template<typename T>
void heap_to_sbo(void* from, void* to) noexcept {
  auto* source = static_cast<T*>(from);
  ::new (to) T(std::move(*source));
  delete source;
}

// `type_tag_v`: type identity tag: the address of `type_tag_v<T>` identifies
// `T` uniquely across the program, without RTTI.
//
// The owning table carries it, so typed operations on an erased target can
// verify the type at runtime.
template<typename T>
constexpr inline std::byte type_tag_v{};

// `facade_tag_v`: facade identity tag, the facade analog of `type_tag_v`.
//
// Birth-ancestry entries carry it, so `try_downcast` can match a facade at
// runtime.
template<Facade F>
constexpr inline std::byte facade_tag_v{};

// `vtable_builder`: facade-wide dispatch machinery, specialized on the
// `facade` base to get at the entry pack.
//
// Contains the flattened method list, name-to-slot lookup, the dispatch table
// type, the all-methods-bound check behind `Proxiable`, and the table
// builders.
template<typename FB>
struct vtable_builder;

// `vtbuild_t`: facade-wide machinery for the derived facade type `F`.
template<Facade F>
using vtbuild_t = vtable_builder<decltype(probe(std::declval<const F&>()))>;

// `vtable_for`: per-(facade, type, born facade) dispatch table instance.
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

// `owning_vtable_for`: per-(facade, born facade, type, storage mode) owning
// dispatch table instance, for `proxy`.
//
// `Sbo` marks a table whose target is stored inline; whether it is depends on
// the constructing handle's policy as well as on `T`.
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
template<Facade F, Facade Born, typename T, bool Sbo>
constexpr inline vtbuild_t<F>::owning_vtable_t owning_vtable_for =
    vtbuild_t<F>::template make_owning_vtable<F, Born, T, Sbo>();

// `ancestor_entry`: one entry of a birth ancestry.
//
// Contains a facade's identity tag and that facade's table for the same birth
// and target: an owning table in an owning ancestry (which is also per
// storage mode), a view table in a view ancestry.
struct ancestor_entry {
  const void* tag;
  const void* table;
};

// `ancestry_t`: type-erased view of a birth ancestry, the facade the target
// was born as plus every facade it transitively extends.
//
// Every table points at its born family's ancestry of its own kind: owning
// tables at an owning ancestry, view tables at a view ancestry; the
// underlying tables are the statics below.
struct ancestry_t {
  const ancestor_entry* entries;
  std::size_t count;
};

// `find_ancestor`: the table of the ancestry member whose tag is `tag`, or
// null when the facade is not in the ancestry.
//
// This is `try_downcast`'s runtime search; the tag match is what proves the
// cast sound, so the caller can cast the table back to the matched facade's
// type.
[[nodiscard]] constexpr const void*
find_ancestor(const ancestry_t& ancestry, const void* tag) noexcept {
  for (std::size_t ndx = 0; ndx != ancestry.count; ++ndx)
    if (ancestry.entries[ndx].tag == tag) return ancestry.entries[ndx].table;
  return nullptr;
}

// `make_ancestor_table`: build the ancestor table for a target born as (Born,
// T, Sbo).
//
// Contains `Born` itself first, then every facade it extends, all keyed by the
// same birth. The tuple pointer parameter carries
// `vtbuild_t<Born>::ancestors_t` in deducible position.
template<Facade Born, typename T, bool Sbo, Facade... As>
consteval std::array<ancestor_entry, 1 + sizeof...(As)>
make_ancestor_table(std::tuple<As...>*) noexcept {
  return {{{&facade_tag_v<Born>, &owning_vtable_for<Born, Born, T, Sbo>},
      {&facade_tag_v<As>, &owning_vtable_for<As, Born, T, Sbo>}...}};
}

template<Facade Born, typename T, bool Sbo>
constexpr inline auto ancestor_table_for = make_ancestor_table<Born, T, Sbo>(
    static_cast<vtbuild_t<Born>::ancestors_t*>(nullptr));

// `ancestry_for`: the birth ancestry for a target born as (Born, T, Sbo), the
// object every owning table of that born family points at.
//
// Each storage mode has its own ancestry, whose entries are that mode's
// tables; a mode-changing adoption switches the proxy to the table's
// other-mode sibling, which carries the other mode's ancestry, so the tables
// an ancestry hands out always match the target's current home.
template<Facade Born, typename T, bool Sbo>
constexpr inline ancestry_t ancestry_for{
    ancestor_table_for<Born, T, Sbo>.data(),
    ancestor_table_for<Born, T, Sbo>.size()};

// `make_view_ancestor_table`: build the view-table ancestor table for a
// target born as (Born, T), mirroring `make_ancestor_table`.
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

// `view_ancestry_for`: the birth ancestry for a view over a target born as
// (Born, T), the object every view table of that born family points at.
//
// There is no storage-mode axis here; a view has no storage.
template<Facade Born, typename T>
constexpr inline ancestry_t view_ancestry_for{
    view_ancestor_table_for<Born, T>.data(),
    view_ancestor_table_for<Born, T>.size()};

// `slot`: flattened dispatch slot, containing one facade method plus the
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
  static constexpr bool const_v = M::const_v;
  static constexpr bool noexcept_v = M::noexcept_v;
  using result_t = M::result_t;
};

// `retagged`: retag base facade `B`'s own slots with `B` itself, leaving
// inherited slots untouched, as `extends<B>` flattens B's list into the
// derived facade's.
template<Facade B, typename S>
struct retagged {
  using type = S;
};
template<Facade B, typename M>
struct retagged<B, slot<void, M>> {
  using type = slot<B, M>;
};

// `retag_slots`: retag all slots in a tuple with `B`.
template<Facade B, typename Slots>
struct retag_slots;
template<Facade B, typename... Ss>
struct retag_slots<B, std::tuple<Ss...>> {
  using type = std::tuple<typename retagged<B, Ss>::type...>;
};
template<Facade B, typename Slots>
using retag_slots_t = retag_slots<B, Slots>::type;

// `first_index_of_type`: first position of type `S` in `Ss`, for dedup's
// first-occurrence test.
template<typename S, typename... Ss>
consteval std::size_t first_index_of_type() noexcept {
  constexpr std::array<bool, sizeof...(Ss)> matches{std::same_as<S, Ss>...};
  for (std::size_t ndx = 0; ndx != matches.size(); ++ndx)
    if (matches[ndx]) return ndx;
  return sizeof...(Ss);
}

// `dedup_slots`: remove duplicate slot types, keeping first occurrences in
// order.
//
// A slot type recurs exactly when the same ancestor facade is reached
// through more than one composition path, so this is what collapses a
// diamond to a single set of slots. Per-facade conformance already yields
// one `proxy_impl<Ancestor, T>` regardless of path (the effect of Rust's
// coherence rule); dedup makes the flattened table agree.
template<typename Slots>
struct dedup_slots;
template<typename... Ss>
struct dedup_slots<std::tuple<Ss...>> {
  template<std::size_t... Ndx>
  static auto keep(std::index_sequence<Ndx...>) -> decltype(std::tuple_cat(
      std::declval<std::conditional_t<first_index_of_type<Ss, Ss...>() == Ndx,
          std::tuple<Ss>, std::tuple<>>>()...));
  using type = decltype(keep(std::index_sequence_for<Ss...>{}));
};
template<typename Slots>
using dedup_slots_t = dedup_slots<Slots>::type;

// `slot_thunk`: thunk for one slot.
//
// An inherited method binds through its declaring facade, an own method
// through `F` itself, so per-facade conformance survives flattening verbatim
// and an upcast handle cannot disagree with the derived one.
template<typename F, typename T, typename S>
consteval auto slot_thunk() noexcept {
  using owner_t = std::conditional_t<std::is_void_v<typename S::owner_t>, F,
      typename S::owner_t>;
  return method_traits<typename S::method_t>::template make_thunk<owner_t,
      T>();
}

// `slot_owner_name`: name that qualifies slot `S`, in a facade whose own name
// is `OwnName`, which is the declaring facade's name for an inherited slot,
// `OwnName` for an own one.
template<fixed_string OwnName, typename S>
consteval auto slot_owner_name() noexcept {
  if constexpr (std::is_void_v<typename S::owner_t>)
    return OwnName;
  else
    return vtbuild_t<typename S::owner_t>::name_v;
}

// `same_chain_owners`: whether the declaring facades of two slots lie in one
// extends chain: identical, or one extends the other, with the facade being
// built (`void`) counting as extending every inherited owner.
template<typename S1, typename S2>
consteval bool same_chain_owners() noexcept {
  using o1_t = S1::owner_t;
  using o2_t = S2::owner_t;
  if constexpr (std::is_void_v<o1_t> || std::is_void_v<o2_t> ||
                std::same_as<o1_t, o2_t>)
    return true;
  else
    return vtbuild_t<o1_t>::template extends_facade<o2_t>() ||
           vtbuild_t<o2_t>::template extends_facade<o1_t>();
}

// `no_chain_collision_against`, `owner_names_unique_against`: the two
// collision detonators, each pitting slot `S1` against the whole list.
// Post-dedup, a repeated slot type is only ever the self-pairing, so same-type
// pairs are skipped.
//
// A method name may not recur within one extends chain (a doubled declaration,
// or a derived facade redeclaring an inherited name, which is rejected by
// design: facades carry no implementations, so there is nothing to override).
//
// Unrelated sibling facades may collide on a method name freely, since every
// facade is named and the qualified spelling is always available as the route
// to a collided slot that the arguments cannot single out. Facade names must
// be unique within the composition, or qualified keys could themselves
// collide.
template<typename S1, typename... Ss>
consteval bool no_chain_collision_against() noexcept {
  return ((std::same_as<S1, Ss> || S1::name_v != Ss::name_v ||
              !same_chain_owners<S1, Ss>()) &&
          ...);
}

template<fixed_string OwnName, typename S1, typename... Ss>
consteval bool owner_names_unique_against() noexcept {
  return (
      (std::same_as<typename S1::owner_t, typename Ss::owner_t> ||
          slot_owner_name<OwnName, S1>() != slot_owner_name<OwnName, Ss>()) &&
      ...);
}

// `entry_traits`: traits over one entry of a facade's declaration list, which
// mixes `method` descriptors with `extends` composition entries.
//
// Each entry contributes slots to the flattened list, base facades to the
// direct-base list, base-table pointers to the dispatch table, and a
// conformance term to `all_bound_v`.
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
  static constexpr bool bound_v = method_traits<M>::template bound_v<F, T>;
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
  static constexpr bool bound_v = vtbuild_t<B>::template all_bound_v<B, T>;
};

template<fixed_string Name>
struct entry_traits<name<Name>> {
  using slots_t = std::tuple<>;
  using bases_t = std::tuple<>;
  using owning_bases_t = std::tuple<>;
  using chain_t = std::tuple<>;

  template<typename F, typename T>
  static constexpr bool bound_v = true;
};

// `entry_name`: facade name carried by one entry of a facade's list, empty for
// every other entry kind.
template<typename E>
struct entry_name {
  static constexpr fixed_string name_v{""};
  static constexpr bool is_name_v = false;
};
template<fixed_string Name>
struct entry_name<name<Name>> {
  static constexpr auto name_v = Name;
  static constexpr bool is_name_v = true;
};

// `facade_name_of_v`: name of a facade with entries `Es`.
//
// The single `name` entry's string, or empty. Concatenation acts as selection,
// because at most one entry contributes a nonempty string.
template<typename... Es>
constexpr inline auto facade_name_of_v =
    (fixed_string{""} + ... + entry_name<Es>::name_v);

// `flat_slots_of_t`: flattened, deduped slot list and direct-base list of a
// facade's entry pack.
template<typename... Es>
using flat_slots_of_t = dedup_slots_t<decltype(std::tuple_cat(
    std::declval<typename entry_traits<Es>::slots_t>()...))>;

// `bases_of_t`: flattened direct-base list of a facade's entry pack.
template<typename... Es>
using bases_of_t = decltype(std::tuple_cat(
    std::declval<typename entry_traits<Es>::bases_t>()...));

// `vtable_builder_impl`: the flattened core of the builder, over the full slot
// list `Ss` (bases' methods first, in declaration order, then own, deduped),
// the direct-base facades `Bs`, and the facade's own name `OwnName`.
template<typename FlatSlots, typename Bases, fixed_string OwnName>
struct vtable_builder_impl;

template<typename... Ss, typename... Bs, fixed_string OwnName>
struct vtable_builder_impl<std::tuple<Ss...>, std::tuple<Bs...>, OwnName> {
  static_assert((no_chain_collision_against<Ss, Ss...>() && ...),
      "a facade cannot declare a method name twice or redeclare an inherited "
      "one");
  static_assert((owner_names_unique_against<OwnName, Ss, Ss...>() && ...),
      "facade names must be unique within a composition");

  using flat_slots_t = std::tuple<Ss...>;
  static constexpr auto name_v = OwnName;
  static constexpr std::size_t count_v = sizeof...(Ss);
  static constexpr std::size_t base_count_v = sizeof...(Bs);

  // `none_v`, `ambiguous_v`: flag results of `resolve`, outside the valid
  // index range: no slot answers to the key, or more than one does and the
  // arguments do not single one out.
  static constexpr std::size_t none_v = count_v;
  static constexpr std::size_t ambiguous_v = count_v + 1;

  // `base_t`: direct-base facade at index `Ndx`.
  template<std::size_t Ndx>
  using base_t = std::tuple_element_t<Ndx, std::tuple<Bs...>>;

  using thunks_t = std::tuple<
      typename method_traits<typename Ss::method_t>::thunk_ptr_t...>;

  // `make_thunks`: build the thunk tuple for target `T` of facade `F`,
  // containing one thunk per slot, each bound through the slot's declaring
  // facade.
  template<typename F, typename T>
  static consteval thunks_t make_thunks() noexcept {
    return {slot_thunk<F, T, Ss>()...};
  }

  // `vtable_t`: dispatch table, with one thunk slot per flattened method, plus
  // the address of each direct base's table for the same target type.
  //
  // Dispatch always uses the flattened thunks, so an inherited call costs the
  // same single indexed load as an own one; the base pointers exist for
  // upcasting to follow (Rust's embedded supertrait vtables).
  //
  // `ancestry` names the born family's view ancestry (see
  // `view_ancestry_for`), which is what `try_downcast` on the views and on
  // `shared_proxy` searches; it is the table's only slot beyond dispatch, so
  // views still carry no lifetime machinery.
  struct vtable_t {
    thunks_t thunks;
    const ancestry_t* ancestry;
    std::tuple<const typename vtbuild_t<Bs>::vtable_t*...> bases;
  };

  // `extends_facade`: whether this facade transitively extends facade `B`.
  // Strict: the search covers the bases and their bases, never this facade
  // itself, so a self-match is false.
  template<typename B>
  static consteval bool extends_facade() noexcept {
    return (
        (std::same_as<B, Bs> || vtbuild_t<Bs>::template extends_facade<B>()) ||
        ...);
  }

  // `base_route`: index of the first direct base that is `B` or extends it, or
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

  // `owner_name`: name that qualifies slot `S`.
  //
  // The declaring facade's name for an inherited slot, this facade's own name
  // otherwise.
  template<typename S>
  static consteval auto owner_name() noexcept {
    if constexpr (std::is_void_v<typename S::owner_t>)
      return OwnName;
    else
      return vtbuild_t<typename S::owner_t>::name_v;
  }

  // `slot_matches`: whether slot `S` answers to `Key`.
  //
  // An unqualified key matches the method name alone; a qualified key
  // ("facade::method") also requires the qualifying facade's name.
  template<typename S, fixed_string Key>
  static consteval bool slot_matches() noexcept {
    constexpr std::string_view k = Key.view();
    constexpr auto pos = k.find("::");
    constexpr auto qual =
        pos == std::string_view::npos ? std::string_view{} : k.substr(0, pos);
    constexpr auto base =
        pos == std::string_view::npos ? k : k.substr(pos + 2);
    if (S::name_v.view() != base) return false;
    if (qual.empty()) return true;
    constexpr auto owner = owner_name<S>();
    return owner.view() == qual;
  }

  // `candidates`: per-slot flags over the whole list, marking the slots that
  // answer to `Key` (const-qualified only, when dispatching through a const
  // handle).
  template<fixed_string Key, bool ConstOnly>
  static consteval std::array<bool, count_v> candidates() noexcept {
    return {(slot_matches<Ss, Key>() && (!ConstOnly || Ss::const_v))...};
  }

  // `exact_flags`, `viable_flags`: per-slot flags over the whole list, marking
  // the slots whose declared parameters match `CallArgs` exactly (after
  // normalization) or are viable through the ordinary conversions a call
  // performs.
  template<typename... CallArgs>
  static consteval std::array<bool, count_v> exact_flags() noexcept {
    return {method_traits<typename Ss::method_t>::template exact_v<
        CallArgs...>...};
  }

  template<typename... CallArgs>
  static consteval std::array<bool, count_v> viable_flags() noexcept {
    return {method_traits<typename Ss::method_t>::template viable_v<
        CallArgs...>...};
  }

  // `both`: narrow flag set `a` by flag set `b`, elementwise.
  static consteval std::array<bool, count_v> both(std::array<bool, count_v> a,
      const std::array<bool, count_v>& b) noexcept {
    for (std::size_t ndx = 0; ndx != count_v; ++ndx) a[ndx] = a[ndx] && b[ndx];
    return a;
  }

  // `tally`: count of set flags, plus the last set index (`none_v` when none
  // are).
  static consteval std::pair<std::size_t, std::size_t> tally(
      const std::array<bool, count_v>& flags) noexcept {
    std::size_t cnt{};
    std::size_t at{none_v};
    for (std::size_t ndx = 0; ndx != count_v; ++ndx)
      if (flags[ndx]) {
        ++cnt;
        at = ndx;
      }
    return {cnt, at};
  }

  // `resolve`: resolve `Key`, called with `CallArgs`, to a slot index, or to
  // `none_v` or `ambiguous_v`.
  //
  // A qualified key names its slot outright. An unqualified key with a
  // single candidate resolves to it unconditionally, so a call with
  // unsuitable arguments still fails directly at the thunk. An unqualified
  // key over a sibling overload set resolves the way a C++ call would: a
  // unique exact signature match wins, else a unique viable candidate, and
  // anything else is ambiguous and needs the qualified spelling. `ConstOnly`
  // restricts the candidates to const-qualified methods, for dispatch
  // through const handles.
  template<fixed_string Key, bool ConstOnly, typename... CallArgs>
  static consteval std::size_t resolve() noexcept {
    constexpr auto cand = candidates<Key, ConstOnly>();
    const auto [cnt, at] = tally(cand);
    if (cnt < 2) return cnt ? at : none_v;
    const auto [exact_cnt, exact_at] =
        tally(both(cand, exact_flags<CallArgs...>()));
    if (exact_cnt == 1) return exact_at;
    if (exact_cnt > 1) return ambiguous_v;
    const auto [viable_cnt, viable_at] =
        tally(both(cand, viable_flags<CallArgs...>()));
    if (viable_cnt == 1) return viable_at;
    return viable_cnt ? ambiguous_v : none_v;
  }

  // `resolve_exact`: resolve `Key` to the unique candidate whose declared
  // parameters match `CallArgs` exactly.
  //
  // This is the validation probe's strictness: a merely-viable signature does
  // not count.
  template<fixed_string Key, bool ConstOnly, typename... CallArgs>
  static consteval std::size_t resolve_exact() noexcept {
    const auto [cnt, at] =
        tally(both(candidates<Key, ConstOnly>(), exact_flags<CallArgs...>()));
    if (cnt == 1) return at;
    return cnt ? ambiguous_v : none_v;
  }

  // `is_const`: whether any method answering to `Key` is const-qualified.
  //
  // The gate on dispatching `Key` through a const handle. False for an unknown
  // key, since rejecting one is `resolve`'s job.
  template<fixed_string Key>
  static consteval bool is_const() noexcept {
    return ((slot_matches<Ss, Key>() && Ss::const_v) || ...);
  }

  // `is_noexcept`: whether the call `Key` resolves to, with `CallArgs`,
  // dispatches a noexcept method.
  //
  // False when the call does not resolve.
  template<fixed_string Key, bool ConstOnly, typename... CallArgs>
  static consteval bool is_noexcept() noexcept {
    constexpr std::array<bool, count_v> flags{Ss::noexcept_v...};
    constexpr auto ndx = resolve<Key, ConstOnly, CallArgs...>();
    return ndx < count_v && flags[ndx];
  }

  // `do_result_of`: declared result type of the exact-match candidate for
  // `Key`, or `void` when there is none.
  //
  // The permissive fallback keeps return-type substitution in the `api_probe`
  // from hard-erroring before its constraint can reject the key.
  template<fixed_string Key, bool ConstOnly, typename... CallArgs>
  static consteval auto do_result_of() noexcept {
    constexpr auto ndx = resolve_exact<Key, ConstOnly, CallArgs...>();
    if constexpr (ndx < count_v) {
      using s_t = std::tuple_element_t<ndx, std::tuple<Ss...>>;
      return std::type_identity<typename s_t::result_t>{};
    } else {
      return std::type_identity<void>{};
    }
  }

  // `result_of_t`: declared result type of the exact-match candidate for
  // `Key`, or `void` when there is none.
  template<fixed_string Key, bool ConstOnly, typename... CallArgs>
  using result_of_t =
      decltype(do_result_of<Key, ConstOnly, CallArgs...>())::type;

  // `exact_args`: whether `Args` match the declared parameters of exactly one
  // candidate for `Key`, after normalization, rather than by convertibility.
  //
  // False for an unknown key. This is the `api_probe`'s constraint.
  template<fixed_string Key, bool ConstOnly, typename... Args>
  static consteval bool exact_args() noexcept {
    return resolve_exact<Key, ConstOnly, Args...>() < count_v;
  }
};

// `vtable_builder`: facade-wide dispatch machinery, specialized on the
// `facade` base to get at the entry pack.
template<typename... Es>
struct vtable_builder<facade<Es...>>
    : vtable_builder_impl<flat_slots_of_t<Es...>, bases_of_t<Es...>,
          facade_name_of_v<Es...>> {
  static_assert((0 + ... + entry_name<Es>::is_name_v) == 1,
      "every facade must carry exactly one name entry");

  using impl_t = vtable_builder_impl<flat_slots_of_t<Es...>, bases_of_t<Es...>,
      facade_name_of_v<Es...>>;
  using vtable_t = impl_t::vtable_t;

  // `all_bound_v`: whether every method of the facade, inherited and own, has
  // a usable binding for `T`.
  //
  // Own methods bind through `proxy_impl<F, T>`; inherited ones recurse into
  // their declaring facade, so conforming to a composed facade requires
  // conforming to each of its bases.
  template<typename F, typename T>
  static constexpr bool all_bound_v =
      (entry_traits<Es>::template bound_v<F, T> && ...);

  // `make_view_bases`: view-table pointers of the direct bases, for the same
  // born family and target, built over the direct-base pack (carried by the
  // tuple pointer parameter, as in `make_ancestor_table`).
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

  // `make_vtable`: build the dispatch table for target `T` of facade `F`, born
  // as facade `Born`.
  template<typename F, typename T, typename Born>
  static consteval vtable_t make_vtable() noexcept {
    return {impl_t::template make_thunks<F, T>(), &view_ancestry_for<Born, T>,
        make_view_bases<T, Born>(static_cast<bases_of_t<Es...>*>(nullptr))};
  }

  // `owning_bases_t`: direct-base owning-table pointers, mirroring
  // `vtable_t::bases`.
  using owning_bases_t = decltype(std::tuple_cat(
      std::declval<typename entry_traits<Es>::owning_bases_t>()...));

  // `ancestors_t`: every facade this one transitively extends, deduped (a
  // diamond's shared ancestor appears once), not including this facade itself.
  //
  // The birth ancestry `try_downcast` searches is built over it.
  using ancestors_t = dedup_slots_t<decltype(std::tuple_cat(
      std::declval<typename entry_traits<Es>::chain_t>()...))>;

  // `owning_vtable_t`: owning dispatch table.
  //
  // Carries housekeeping slots alongside the facade methods, plus the address
  // of each direct base's owning table for the same birth, target type, and
  // storage mode, which is what makes owning upcasts a pointer read (mirroring
  // `vtable_t::bases`).
  //
  // A null `relocate` marks a heap-stored target, which moves by pointer
  // steal rather than relocation. A null `copy` marks a target that is not
  // copy-constructible. `type_tag` identifies the target type itself (see
  // `type_tag_v`), letting typed operations on the erased target verify
  // their `T` at runtime; `size` and `align` describe it, so an adopting
  // proxy can check the fit of an erased arrival against its own buffer.
  //
  // The mode-changing pairs point across to the table's other-mode sibling:
  // an inline-mode table carries `to_heap` (see `sbo_to_heap`) plus
  // `heap_table`, which is how a `heap_only` proxy adopts an erased inline
  // arrival, and a heap-mode table carries `to_sbo` plus `sbo_table` (null
  // when the target is not nothrow-move-constructible and so can never live
  // inline), the un-boxing inverse for an `sbo_only` proxy.
  //
  // `ancestry` names the born family's birth ancestry for this mode (see
  // `ancestry_for`): the facade the target was constructed as plus every
  // facade it extends, which is what `try_downcast` searches.
  struct owning_vtable_t {
    vtable_t vt;
    void (*destroy)(void*) noexcept;
    void (*relocate)(void*, void*) noexcept;
    void* (*copy)(const void*, void*);
    void* (*to_heap)(void*);
    void (*to_sbo)(void*, void*) noexcept;
    const void* type_tag;
    const owning_vtable_t* heap_table;
    const owning_vtable_t* sbo_table;
    std::size_t size;
    std::size_t align;
    const ancestry_t* ancestry;
    owning_bases_t bases;
  };

  // `make_owning_bases`: owning-table pointers of the direct bases, for the
  // same born family, target, and mode, built over the direct-base pack
  // (carried by the tuple pointer parameter, as in `make_ancestor_table`).
  //
  // Deliberately a member rather than an `entry_traits` hook, and with a
  // spelled-out return type: an entry's helper is shared by every facade
  // listing that entry, and in a diamond the ancestry's back-references
  // reach a sibling's table build while the shared helper is still
  // mid-instantiation, which a deduced return type cannot survive. A member
  // is unique per (facade, born, type, mode) and so is only ever entered
  // once.
  template<typename Born, typename T, bool Sbo, typename... Bs2>
  static consteval owning_bases_t
  make_owning_bases(std::tuple<Bs2...>*) noexcept {
    return {&owning_vtable_for<Bs2, Born, T, Sbo>...};
  }

  // `make_owning_vtable`: build the owning table for target `T` born as facade
  // `Born`, stored inline (`Sbo`) or on the heap.
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
  template<typename F, typename Born, typename T, bool Sbo>
  static consteval owning_vtable_t make_owning_vtable() noexcept {
    owning_vtable_t ovt{vtable_for<F, T, Born>, nullptr, nullptr, nullptr,
        nullptr, nullptr, &type_tag_v<T>, nullptr, nullptr, sizeof(T),
        alignof(T), &ancestry_for<Born, T, Sbo>,
        make_owning_bases<Born, T, Sbo>(
            static_cast<bases_of_t<Es...>*>(nullptr))};
    if constexpr (Sbo) {
      ovt.destroy = &sbo_destroy<T>;
      ovt.relocate = &sbo_relocate<T>;
      ovt.to_heap = &sbo_to_heap<T>;
      ovt.heap_table = &owning_vtable_for<F, Born, T, false>;
      if constexpr (std::is_copy_constructible_v<T>) ovt.copy = &sbo_copy<T>;
    } else {
      ovt.destroy = &heap_destroy<T>;
      if constexpr (std::is_nothrow_move_constructible_v<T>) {
        ovt.to_sbo = &heap_to_sbo<T>;
        ovt.sbo_table = &owning_vtable_for<F, Born, T, true>;
      }
      if constexpr (std::is_copy_constructible_v<T>) ovt.copy = &heap_copy<T>;
    }
    return ovt;
  }
};

// `upcast_vtable`: narrow a dispatch-table pointer from facade `D` to `B`,
// where `B` is `D` itself or a facade it extends, by following the embedded
// direct-base table pointers.
//
// The route is resolved at compile time; the runtime cost is one dependent
// load per composition level crossed.
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

// `upcast_owning_vtable`: narrow an owning-table pointer from facade `D` to
// `B`, where `B` is `D` itself or a facade it extends.
//
// The owning counterpart of `upcast_vtable`, following the embedded
// direct-base owning tables along the same compile-time-resolved route.
template<Facade B, Facade D>
[[nodiscard]] constexpr auto
upcast_owning_vtable(const typename vtbuild_t<D>::owning_vtable_t* vt) noexcept
    -> const vtbuild_t<B>::owning_vtable_t* {
  if constexpr (std::same_as<B, D>) {
    return vt;
  } else {
    constexpr auto ndx = vtbuild_t<D>::template base_route<B>();
    static_assert(ndx != vtbuild_t<D>::base_count_v,
        "the source facade does not extend the target facade");
    return upcast_owning_vtable<B,
        typename vtbuild_t<D>::template base_t<ndx>>(std::get<ndx>(vt->bases));
  }
}

// `dispatch`: shared body of every handle's `call`.
//
// Resolves `Key` against facade `F`'s slot list, surfaces the user-facing
// errors, and invokes the thunk on the erased target. `ConstOnly` marks
// dispatch through a const handle.
template<Facade F, bool ConstOnly, fixed_string Key, typename ErasedPtr,
    typename... Args>
constexpr decltype(auto)
dispatch(const typename vtbuild_t<F>::thunks_t& tks, ErasedPtr target,
    Args&&... args) noexcept(vtbuild_t<F>::template is_noexcept<Key, ConstOnly,
    Args...>()) {
  constexpr auto ndx =
      vtbuild_t<F>::template resolve<Key, ConstOnly, Args...>();
  static_assert(ndx != vtbuild_t<F>::none_v, "no matching signature");
  static_assert(ndx != vtbuild_t<F>::ambiguous_v,
      "ambiguous method name; qualify the key with the facade name");
  return std::get<ndx>(tks)(target, std::forward<Args>(args)...);
}

} // namespace details

#pragma endregion
#pragma region Proxiable

// `Proxiable`: concept for when `T` can back facade `F`.
//
// Satisfied when a usable `proxy_impl<F, T>` binding exists for every method
// of `F`.
//
// Both binding routes, the facade's boilerplate and the registration-carried
// impl, are registration-gated, so the concept is satisfied exactly when the
// pair is registered with a usable binding.
//
// This is the gate on proxy construction and doubles as the trait bound for
// static-dispatch templates.
template<typename T, typename F>
concept Proxiable =
    Facade<F> && details::vtbuild_t<F>::template all_bound_v<F, T>;

// `Extends`: concept for when facade `D` (transitively) extends facade `B`
// through `extends` composition entries.
//
// Strict: false when `D` is `B` itself. Constraints that mean "B or anything
// extending it" pair this with `std::same_as`.
template<typename D, typename B>
concept Extends =
    Facade<D> && Facade<B> &&
    details::vtbuild_t<D>::template extends_facade<B>();

// `InChainOf`: concept for when facade `B` is `D` itself or a facade `D`
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

// `proxy_view`, `const_proxy_view`, `proxy`, `shared_proxy`, `weak_proxy`:
// forward declarations of the erased-handle flavors, for the trait below and
// the cross-handle constructors.
//
// The owning proxy's storage policy defaults here, on the first declaration.
template<Facade F>
class proxy_view;
template<Facade F>
class const_proxy_view;
template<Facade F, proxy_policy Policy = proxy_policy{}>
class proxy;
template<Facade F>
class shared_proxy;
template<Facade F>
class weak_proxy;

namespace details {

// `handle_facade`: facade of a proxy handle type, or `void` for any other
// type.
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
template<Facade F, proxy_policy P>
struct handle_facade<proxy<F, P>> {
  using type = F;
};
template<Facade F>
struct handle_facade<shared_proxy<F>> {
  using type = F;
};

// `is_handle_for`: whether `T` is a proxy handle whose facade is `F` or
// extends it, the set served by the dedicated viewing and upcasting
// constructors.
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

// `qualified_key`: `Key` qualified by facade `F`'s name.
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

#pragma endregion
#pragma region API validation

namespace details {

// `strict_result`: exact-conversion carrier for the `api_probe`'s strict
// `call`.
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

// `strict_return_t`: result type of the probe's strict `call`.
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

// `api_probe`: synthetic dispatch target for `validate_api`.
//
// It inherits the facade's `api` and exposes a deliberately strict `call`.
// Argument types must match the method's declared parameters exactly (per
// `exact_args`), and the result converts only to exactly the declared result
// type. Never constructed or executed; it exists to be type-checked.
template<Facade F>
struct api_probe: api_base_t<F> {
  template<fixed_string Key, typename... Args>
  requires(vtbuild_t<F>::template exact_args<Key, false, Args...>())
  strict_return_t<
      typename vtbuild_t<F>::template result_of_t<Key, false, Args...>>
  call(Args&&...) {
    std::terminate();
  }

  template<fixed_string Key, typename... Args>
  requires(vtbuild_t<F>::template exact_args<Key, true, Args...>())
  // NOLINTNEXTLINE(modernize-use-nodiscard): mirrors `call`, never executed.
  strict_return_t<
      typename vtbuild_t<F>::template result_of_t<Key, true, Args...>>
  call(Args&&...) const {
    std::terminate();
  }
};

// `corvid_proxy_spec`: probe registration, covering every facade.
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

// `validate_api`: validate a facade's `api` against its method list, spelling
// neither the names nor the signatures again.
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

#pragma endregion
#pragma region Views

namespace details {

// `view_base`: storage and const-method dispatch shared by the two view
// flavors, which differ only in the constness of the erased target pointer.
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

  // `call`: call the const-qualified facade method named `Key`, forwarding
  // `args` through the erased signature.
  //
  // The call is `noexcept` when the method is. It is not `[[nodiscard]]`,
  // because discardability belongs to the facade method rather than the
  // dispatcher (the `std::invoke` precedent).
  template<fixed_string Key, typename... Args>
  requires(vtbuild_t<F>::template is_const<Key>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  constexpr decltype(auto) call(Args&&... args) const
      noexcept(vtbuild_t<F>::template is_noexcept<Key, true, Args...>()) {
    return dispatch<F, true, Key>(vtable_->thunks, target_,
        std::forward<Args>(args)...);
  }

  // `operator bool`: an empty view (default-constructed) holds no target.
  //
  // It is testable and rebindable by assignment, but calling through it is
  // undefined behavior, exactly as with an empty proxy.
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
// Two pointers: the target and the per-(facade, type, born facade) dispatch
// table. The target must outlive the view.
//
// A view also converts implicitly from any handle of a facade that extends
// `F` (Rust trait upcasting), and from an lvalue owning `proxy`, re-pointing
// at the handle's target rather than wrapping the handle. An upcast view is
// indistinguishable from one built directly over the target.
//
// Deep-const as an instance, so only const-qualified facade methods dispatch
// through a `const proxy_view`. Because views are freely copyable, that is a
// guardrail rather than a guarantee since copying a `const proxy_view`
// yields a mutable view onto the same target, much as a `T* const` pointer
// copies to a plain `T*`. Code that enforces read-only access must use
// `const_proxy_view`, where constness is part of the type and survives
// copying.
//
// A default-constructed view is empty, like a default-constructed `proxy`:
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
  // `proxy_view`: an empty view holds no target; see the class comment.
  proxy_view() = default;

  // `proxy_view`: converting constructor from an lvalue target.
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

  // `proxy_view`: upcasting constructor from a view over a facade that extends
  // `F` (Rust trait upcasting).
  //
  // Intentionally implicit, like derived-to-base pointer conversion: the
  // target carries over unchanged and the dispatch table narrows to `F`'s by
  // following the embedded base-table pointers, so the upcast view dispatches
  // exactly what a directly-built `F` view of the target would.
  template<Facade D>
  requires Extends<D, F>
  constexpr explicit(false) proxy_view(const proxy_view<D>& view) noexcept
      : base{view.target_, details::upcast_vtable<F, D>(view.vtable_)} {}

  // `proxy_view`: viewing constructor from an owning `proxy` of `F`, or of a
  // facade that extends it.
  //
  // Intentionally implicit, and lvalue-only, so a view cannot be left dangling
  // by a temporary `proxy`. The `proxy` must be non-empty and must outlive the
  // view. A const `proxy` takes a `const_proxy_view` instead, preserving deep
  // const.
  template<Facade D, proxy_policy P>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) proxy_view(proxy<D, P>& p) noexcept
      : base{p.target(), details::upcast_vtable<F, D>(&p.vtable_->vt)} {}

  // `proxy_view`: viewing constructor from a `shared_proxy` of `F`, or of a
  // facade that extends it, under the same rules as viewing an owning `proxy`;
  // the target must outlive the view, meaning at least one shared owner must.
  template<Facade D>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) proxy_view(shared_proxy<D>& p) noexcept
      : base{p.target(), details::upcast_vtable<F, D>(p.vtable_)} {}

  // `call`: call the facade method named `Key`, forwarding `args` through the
  // erased signature.
  //
  // The call is `noexcept` when the method is.
  //
  // This overload dispatches every method. The inherited const overload,
  // re-exposed by the using-declaration, is constrained to const-qualified
  // methods, mirroring the owning `proxy`'s deep const.
  template<fixed_string Key, typename... Args>
  constexpr decltype(auto) call(Args&&... args) noexcept(
      vtbuild_t::template is_noexcept<Key, false, Args...>()) {
    return details::dispatch<F, false, Key>(this->vtable_->thunks,
        this->target_, std::forward<Args>(args)...);
  }

  using base::call;

  // `try_downcast`: attempt a view of `D`, a facade extending `F`, over a
  // target that may have been upcast away from it.
  //
  // The view table remembers the facade the target was born as: the view's
  // own facade for a view built directly over a target, and the owning
  // handle's birth for a view lent from a `proxy` or `shared_proxy`, so a
  // lent view recovers exactly what its owner could. On success the result
  // is a new view over the same target; the source is copied from, never
  // consumed, which is why this is const where the owning flavor is an
  // rvalue method. On failure, including an empty source, the result is
  // empty. Like copying, this escapes the instance-level deep-const
  // guardrail; the guarantee tier is `const_proxy_view`, whose downcast
  // stays const.
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] constexpr proxy_view<D> try_downcast() const noexcept {
    if (!this->vtable_) return {};
    const auto* table = details::find_ancestor(*this->vtable_->ancestry,
        &details::facade_tag_v<D>);
    if (!table) return {};
    return proxy_view<D>{this->target_,
        static_cast<const details::vtbuild_t<D>::vtable_t*>(table)};
  }

private:
  // `proxy_view`: for `try_downcast`, whose target and table are already
  // resolved.
  constexpr proxy_view(void* target, const base::vtable_t* vtable) noexcept
      : base{target, vtable} {}

  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
};

// `proxy_impl`: library-provided binding so that a view satisfies its own
// facade and every facade that facade extends (as in Rust, where `dyn Trait`
// implements `Trait` and meets its supertrait bounds).
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
  // `on`: the qualified spelling keeps the forwarded key unambiguous when
  // `D`'s flattened list collides on `Key`; see `qualified_key`.
  template<fixed_string Key, typename... Args>
  static constexpr decltype(auto)
  on(method_key<Key>, proxy_view<D>& view, Args&&... args) noexcept(
      noexcept(view.template call<details::qualified_key<F, Key>()>(
          std::forward<Args>(args)...))) {
    return view.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
  template<fixed_string Key, typename... Args>
  static constexpr decltype(auto)
  on(method_key<Key>, const proxy_view<D>& view, Args&&... args) noexcept(
      noexcept(view.template call<details::qualified_key<F, Key>()>(
          std::forward<Args>(args)...))) {
    return view.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
};

// `const_proxy_view`: non-owning read-only erased handle: Rust's `&dyn Trait`,
// the `const_iterator` to `proxy_view`'s `iterator`.
//
// Constness is part of the type, so unlike a `const proxy_view` it survives
// copying. A const view only ever copies or converts to another const view.
// It binds const and mutable targets alike, and dispatches only the
// const-qualified facade methods, sharing the mutable view's dispatch table
// (the non-const slots are simply unreachable). The target must outlive the
// view.
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
  // `const_proxy_view`: an empty view holds no target; see the class comment.
  const_proxy_view() = default;

  // `const_proxy_view`: converting constructor from an lvalue target, const or
  // not.
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

  // `const_proxy_view`: converting constructor from the mutable view.
  //
  // Dropping mutability is implicit and safe, like `T*` to `const T*`, and
  // there is no path back.
  constexpr explicit(false)
      const_proxy_view(const proxy_view<F>& view) noexcept
      : base{view.target_, view.vtable_} {}

  // `const_proxy_view`: upcasting constructor from a const view over a facade
  // that extends `F` (Rust trait upcasting).
  //
  // Intentionally implicit; see the mutable view's upcasting constructor.
  template<Facade D>
  requires Extends<D, F>
  constexpr explicit(false)
      const_proxy_view(const const_proxy_view<D>& view) noexcept
      : base{view.target_, details::upcast_vtable<F, D>(view.vtable_)} {}

  // `const_proxy_view`: upcasting constructor from the mutable view of a
  // facade that extends `F`, dropping mutability and upcasting in one implicit
  // step.
  template<Facade D>
  requires Extends<D, F>
  constexpr explicit(false)
      const_proxy_view(const proxy_view<D>& view) noexcept
      : base{view.target_, details::upcast_vtable<F, D>(view.vtable_)} {}

  // `const_proxy_view`: viewing constructor from an owning `proxy` of `F`, or
  // of a facade that extends it.
  //
  // Intentionally implicit, and lvalue-only. The proxy must be non-empty and
  // must outlive the view. Mutable and const proxies alike yield the const
  // view; there is no path back to mutability.
  template<Facade D, proxy_policy P>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) const_proxy_view(const proxy<D, P>& p) noexcept
      : base{p.target(), details::upcast_vtable<F, D>(&p.vtable_->vt)} {}

  // `const_proxy_view`: viewing constructor from a `shared_proxy` of `F`, or
  // of a facade that extends it; see the owning-proxy constructor above.
  template<Facade D>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) const_proxy_view(const shared_proxy<D>& p) noexcept
      : base{p.target(), details::upcast_vtable<F, D>(p.vtable_)} {}

  // No need for a `using` because `call` is inherited. The base's const-method
  // dispatch is the entire interface, since the mutable methods do not exist
  // on this view.

  // `try_downcast`: attempt a const view of `D`, a facade extending `F`, over
  // a target that may have been upcast away from it; see
  // `proxy_view::try_downcast`.
  //
  // The result is another const view, so downcasting never reopens
  // mutability.
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] constexpr const_proxy_view<D> try_downcast() const noexcept {
    if (!this->vtable_) return {};
    const auto* table = details::find_ancestor(*this->vtable_->ancestry,
        &details::facade_tag_v<D>);
    if (!table) return {};
    return const_proxy_view<D>{this->target_,
        static_cast<const details::vtbuild_t<D>::vtable_t*>(table)};
  }

private:
  // `const_proxy_view`: for `try_downcast`, whose target and table are already
  // resolved.
  constexpr const_proxy_view(const void* target,
      const base::vtable_t* vtable) noexcept
      : base{target, vtable} {}

  template<Facade G>
  friend class const_proxy_view;
};

// `proxy_impl`: library-provided binding so that a const view satisfies its
// own facade, and the facades that facade extends, where that is possible.
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
  template<fixed_string Key, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<Key>())
  static constexpr decltype(auto) on(method_key<Key>,
      const const_proxy_view<D>& view, Args&&... args) noexcept(noexcept(view
          .template call<details::qualified_key<F, Key>()>(
              std::forward<Args>(args)...))) {
    return view.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
};

// `make_proxy_view`: make a view over `target`: a convenience for spelling the
// facade at the call site.
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

// `proxy`: owning erased handle over any `Proxiable` target: Rust's `Box<dyn
// Trait>`, ngcpp's `proxy`.
//
// Move-only. Storage follows `Policy` (see `proxy_policy`): by default,
// eligible targets are stored inline and anything else lives in a
// unique-owned heap allocation. The owning dispatch table carries destroy
// and relocate slots alongside the facade methods, so destruction and moves
// work without knowing the target type.
//
// Proxies of different policies interconvert as rvalues, in the same move
// that upcasts. The source's policy never matters: this proxy accommodates
// whatever target actually arrives, changing its storage mode when the
// policy demands it (re-boxing an inline arrival onto the heap under
// `heap_only`, un-boxing a heap arrival into the buffer under `sbo_only`).
// Only the conversions that might change the mode can throw; everything
// else is `noexcept`, including every same-policy move.
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
// An rvalue proxy also upcasts: it converts implicitly to a proxy of any
// facade its facade extends (Rust: `Box<dyn Derived>` to `Box<dyn Base>`).
// The conversion is a move, transferring ownership of the target and leaving
// the source empty. The owning tables remember the facade the target was
// born as, so an upcast is undoable: `try_downcast` recovers a proxy of any
// facade in the birth ancestry.
//
// When the facade defines a nested `api`, the proxy inherits it, so the
// member-call sugar forwarders dispatch alongside `call`.
template<Facade F, proxy_policy Policy>
class proxy: public details::api_base_t<F> {
  using vtbuild_t = details::vtbuild_t<F>;
  using owning_vtable_t = vtbuild_t::owning_vtable_t;

  // The buffer may only grow from its defaults, so any target eligible for
  // the default buffer stays eligible for every buffer; a `heap_only` proxy
  // has no buffer for the knobs to apply to.
  static_assert(
      Policy.alloc == proxy_alloc::heap_only ||
          (Policy.sbo_size >= proxy_policy{}.sbo_size &&
              Policy.sbo_align >= proxy_policy{}.sbo_align),
      "sbo_size and sbo_align may not shrink below their defaults");
  static_assert(std::has_single_bit(Policy.sbo_align),
      "sbo_align must be a power of two");

public:
  using facade_t = F;

  // `sbo_size`: inline storage capacity in bytes (which a `heap_only` policy
  // never uses).
  //
  // See `proxy_policy` for the inline-eligibility conditions.
  static constexpr std::size_t sbo_size = Policy.sbo_size;

  // `proxy`: an empty proxy holds no target.
  proxy() = default;

  proxy(const proxy&) = delete;
  proxy& operator=(const proxy&) = delete;

  // `proxy`: construct an owning proxy holding a `T` built in place from
  // `args`.
  //
  // Usually spelled through `make_proxy`.
  //
  // This is the moment the policy meets the concrete type: the storage mode
  // is chosen here and baked into the owning table, and an `sbo_only` policy
  // rejects an ineligible target here.
  template<typename T, typename... Args>
  requires(Proxiable<T, F> && std::constructible_from<T, Args...>)
  explicit proxy(std::in_place_type_t<T>, Args&&... args)
      : vtable_{&details::owning_vtable_for<F, F, T,
            details::can_store_inline<T>(Policy)>} {
    static_assert(
        Policy.alloc != proxy_alloc::sbo_only || details::sbo_fits<T>(Policy),
        "the target is not eligible for an sbo_only proxy's inline buffer");
    if constexpr (details::can_store_inline<T>(Policy))
      ::new (static_cast<void*>(storage_.buf)) T(std::forward<Args>(args)...);
    else
      storage_.ptr = new T(std::forward<Args>(args)...);
  }

  // `proxy`: construct an owning proxy adopting a heap target already owned by
  // a `std::unique_ptr` (with the default deleter; the proxy destroys through
  // `delete` either way).
  //
  // Usually spelled through `make_proxy`.
  //
  // The allocation is adopted as-is: the proxy takes the heap path even for
  // a target the policy could store inline, so nothing is copied or moved
  // and the target's address stays stable.
  //
  // The exception is `sbo_only`, which cannot hold a heap target: it un-boxes
  // the target into its buffer (the type is concrete here, so the fit is
  // checked at compile time) and frees the allocation.
  //
  // A null pointer yields an empty proxy. Ownership arrives from a raw pointer
  // only by way of a `unique_ptr`; there is deliberately no raw-pointer
  // constructor.
  template<typename T>
  requires Proxiable<T, F>
  explicit proxy(std::unique_ptr<T> target) {
    if (!target) return;
    if constexpr (Policy.alloc == proxy_alloc::sbo_only) {
      static_assert(details::sbo_fits<T>(Policy),
          "the target is not eligible for an sbo_only proxy's inline buffer");
      ::new (static_cast<void*>(storage_.buf)) T(std::move(*target));
      vtable_ = &details::owning_vtable_for<F, F, T, true>;
    } else {
      storage_.ptr = target.release();
      vtable_ = &details::owning_vtable_for<F, F, T, false>;
    }
  }

  // `proxy`: move construction and assignment leave the source empty.
  //
  // Inline targets relocate through the table's move slot, while heap targets
  // move by pointer steal.
  proxy(proxy&& other) noexcept { do_adopt(other); }

  // `proxy`: converting move constructor from an owning proxy of any facade
  // that extends `F` (an upcast), of any other policy, or both at once.
  //
  // Intentionally implicit, like the view upcasts, but consuming: the target
  // moves into this proxy and the source is left empty, so an upcast is
  // one-way as a conversion (`try_downcast` is the way back).
  //
  // An empty source yields an empty proxy. The source's policy never
  // constrains the conversion; storage is accommodated per target at runtime
  // (see `do_adopt`), and a conversion that might have to change the storage
  // mode is exactly as `noexcept` as that allows.
  //
  // A throw leaves the source intact and this proxy empty; `can_adopt` checks
  // accommodation up front.
  template<Facade D, proxy_policy P>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) proxy(proxy<D, P>&& other) noexcept(
      !details::adopt_may_throw(Policy, P)) {
    do_adopt(other);
  }

  proxy& operator=(proxy&& other) noexcept {
    if (this != &other) {
      do_reset();
      do_adopt(other);
    }
    return *this;
  }

  ~proxy() { do_reset(); }

  // `call`: call the facade method named `Key`, forwarding `args` through the
  // erased signature.
  //
  // The call is `noexcept` when the method is. The const overload is
  // constrained to const-qualified methods, enforcing deep const at overload
  // resolution so the rejection is visible to `requires` probes as well. It
  // is not `[[nodiscard]]`, because discardability belongs to the facade
  // method rather than the dispatcher (the `std::invoke` precedent).
  template<fixed_string Key, typename... Args>
  decltype(auto) call(Args&&... args) noexcept(
      vtbuild_t::template is_noexcept<Key, false, Args...>()) {
    return details::dispatch<F, false, Key>(vtable_->vt.thunks, target(),
        std::forward<Args>(args)...);
  }

  template<fixed_string Key, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<Key>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  decltype(auto) call(Args&&... args) const
      noexcept(vtbuild_t::template is_noexcept<Key, true, Args...>()) {
    return details::dispatch<F, true, Key>(vtable_->vt.thunks, target(),
        std::forward<Args>(args)...);
  }

  // `operator bool`: an empty proxy (default-constructed or moved-from) holds
  // no target.
  [[nodiscard]] explicit operator bool() const noexcept { return vtable_; }

  // `can_clone`: whether `clone` would produce a faithful copy, meaning that
  // the target is copy-constructible, or there is no target at all (an empty
  // proxy clones to an empty proxy).
  //
  // The answer is a runtime property of the erased target, not of the proxy
  // type; a container of proxies can mix cloneable and uncloneable targets.
  [[nodiscard]] bool can_clone() const noexcept {
    return !vtable_ || vtable_->copy;
  }

  // `can_adopt`: whether this `proxy` type can accommodate `source`'s current
  // target, so that converting (or assigning) from it will not throw
  // `std::length_error`.
  //
  // This is the up-front check that advertises, and lets a caller sidestep,
  // the one conversion that can fail for reasons other than memory
  // availability.
  //
  // Static, because the answer is a property of this proxy TYPE against the
  // source's runtime target; it works before any destination instance
  // exists, and is equally callable through one. Only an `sbo_only`
  // destination can ever answer no (everything else has the heap to fall
  // back on), and an empty source is always adoptable, to empty. It does
  // not promise the allocation a mode-changing adoption may need.
  template<Facade D, proxy_policy P>
  requires(std::same_as<D, F> || Extends<D, F>)
  [[nodiscard]] static bool can_adopt(const proxy<D, P>& source) noexcept {
    if constexpr (Policy.alloc != proxy_alloc::sbo_only) {
      return true;
    } else {
      const auto* src = source.vtable_;
      if (!src) return true;
      return src->size <= buf_size && src->align <= buf_align &&
             (src->relocate || src->to_sbo);
    }
  }

  // `clone`: clone the `proxy`, creating a new instance with the same policy,
  // owning a copy of the target made through the table's copy slot.
  //
  // Cloning an empty proxy yields an empty one, and so does cloning a proxy
  // whose target is not copy-constructible; `can_clone` is the up-front check
  // that tells those apart. The copy itself can throw (the target's copy
  // constructor, or the allocation), in which case nothing leaks and no clone
  // is produced.
  //
  // This is deliberately a named method rather than a copy constructor: an
  // unconditional copy constructor would satisfy `std::copyable` for every
  // proxy while failing at runtime for uncloneable targets, turning a
  // concept-probed guarantee into a lie.
  [[nodiscard]] proxy clone() const {
    proxy result;
    if (!vtable_ || !vtable_->copy) return result;
    if (vtable_->relocate)
      (void)vtable_->copy(target(), result.storage_.buf);
    else
      result.storage_.ptr = vtable_->copy(target(), nullptr);
    result.vtable_ = vtable_;
    return result;
  }

  // `extract`: move the target out into a `std::unique_ptr<T>`, leaving the
  // proxy empty.
  //
  // The inverse of the adopting constructor, and the only way ownership leaves
  // a proxy other than destruction; a raw pointer is never exposed.
  //
  // `T` must be the target's exact type, verified at runtime through the
  // table's type tag: on a mismatch, or an empty proxy, the result is null
  // and the proxy is untouched. A heap-stored target hands over its
  // allocation as-is; an inline target moves onto the heap first (the one
  // case with target activity, and the one case that can throw, again
  // leaving the proxy untouched).
  template<typename T>
  [[nodiscard]] std::unique_ptr<T> extract() {
    if (!vtable_ || vtable_->type_tag != &details::type_tag_v<T>)
      return nullptr;
    if (!vtable_->relocate) {
      auto* ptr = static_cast<T*>(storage_.ptr);
      vtable_ = nullptr;
      return std::unique_ptr<T>{ptr};
    }
    if constexpr (std::is_move_constructible_v<T>) {
      std::unique_ptr<T> result{new T(std::move(*static_cast<T*>(target())))};
      do_reset();
      return result;
    } else {
      // Unreachable: an immovable target is never stored inline.
      return nullptr;
    }
  }

  // `try_downcast`: attempt to recover a proxy of `D`, a facade extending `F`,
  // from a proxy that may have been upcast away from it.
  //
  // The owning table remembers the facade the target was born as, meaning
  // the facade it was constructed under, not the concrete type's full
  // conformance: a target made through `make_proxy<marshal, texas_ranger>`
  // downcasts to `marshal` but never to `ranger`. Its birth ancestry, the
  // born facade plus every facade that one extends, is searched at runtime
  // for `D` itself; a target born as a facade that extends `D` therefore
  // matches, and through a diamond, the common base can sidecast to either
  // sibling.
  //
  // On success the target moves into the result (whose table carries the
  // same birth, so further casts in either direction still work) and the
  // source is left empty. On failure, including an empty source, the result
  // is empty and the source is untouched. Consuming only on success is why
  // this is spelled as a method on an rvalue rather than a conversion.
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] proxy<D, Policy> try_downcast() && {
    proxy<D, Policy> result;
    if (!vtable_) return result;
    const auto* table =
        details::find_ancestor(*vtable_->ancestry, &details::facade_tag_v<D>);
    if (!table) return result;
    result.vtable_ =
        static_cast<const details::vtbuild_t<D>::owning_vtable_t*>(table);
    if (vtable_->relocate)
      vtable_->relocate(storage_.buf, result.storage_.buf);
    else
      // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign): see target
      result.storage_.ptr = storage_.ptr;
    vtable_ = nullptr;
    return result;
  }

private:
  // `buf_size`, `buf_align`: a `heap_only` proxy shrinks the buffer to the
  // pointer it overlays, so the whole handle is two words, like a view.
  static constexpr std::size_t buf_size =
      Policy.alloc == proxy_alloc::heap_only ? sizeof(void*) : Policy.sbo_size;
  static constexpr std::size_t buf_align =
      Policy.alloc == proxy_alloc::heap_only
          ? alignof(void*)
          : Policy.sbo_align;

  union storage_t {
    alignas(buf_align) std::byte buf[buf_size];
    void* ptr;
  };

  // `target`: the target address, inline or heap, which is meaningless when
  // empty.
  //
  // The active union member is keyed by the table's `relocate` slot (null
  // means heap), an invariant every write site maintains but the static
  // analyzer cannot see, so the union reads here and in `do_adopt` and
  // `try_downcast` suppress its uninitialized-value checks.
  [[nodiscard]] void* target() noexcept {
    assert(vtable_);
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.UndefReturn)
    return vtable_->relocate ? static_cast<void*>(storage_.buf) : storage_.ptr;
  }
  [[nodiscard]] const void* target() const noexcept {
    assert(vtable_);
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.UndefReturn)
    return vtable_->relocate
               ? static_cast<const void*>(storage_.buf)
               : storage_.ptr;
  }

  // `do_reset`: destroy the target, if any, leaving the proxy empty.
  void do_reset() noexcept {
    if (!vtable_) return;
    vtable_->destroy(target());
    vtable_ = nullptr;
  }

  // `do_adopt`: take over `other`'s target, upcasting its table when `other`'s
  // facade extends `F`, and leaving `other` empty.
  //
  // Assumes `*this` holds no target (freshly constructed or just reset). Note
  // that we don't need to clear `buf` or `ptr` on `other.storage_` because
  // `other.vtable_` defines whether it's empty.
  //
  // The source's policy does not matter here: this proxy accommodates
  // whatever target actually arrives, per target, at runtime.
  //
  // An inline arrival relocates into the buffer when it fits (guaranteed, and
  // checked only at compile time, when this buffer dominates the source's) and
  // otherwise re-boxes onto the heap. A heap arrival moves by pointer
  // steal, or un-boxes into an `sbo_only` proxy's buffer.
  //
  // A mode change switches to the table's other-mode sibling, which carries
  // its own mode's birth ancestry. Only the mode-changing paths can throw (the
  // re-boxing allocation, or `std::length_error` when an erased target
  // cannot be stored inline and the policy forbids the heap), and a throw
  // happens before anything moves, leaving `other` intact and `*this`
  // empty.
  template<Facade D, proxy_policy P>
  void
  do_adopt(proxy<D, P>& other) noexcept(!details::adopt_may_throw(Policy, P)) {
    const auto* src = other.vtable_;
    if (!src) return;
    const auto* vt = details::upcast_owning_vtable<F, D>(src);
    if (src->relocate)
      do_take_inline(other, vt);
    else
      do_take_heap(other, vt);
    other.vtable_ = nullptr;
  }

  // `do_take_inline`: the inline-arrival half of `do_adopt`, which relocates
  // into the buffer when the target fits, else re-boxes onto the heap (or
  // throws, under `sbo_only`).
  //
  // Statically impossible paths are pruned rather than left dynamically
  // unreachable, so a `noexcept` adoption contains no throw at all.
  template<Facade D, proxy_policy P>
  void do_take_inline(proxy<D, P>& other, const owning_vtable_t* vt) {
    if constexpr (P.alloc == proxy_alloc::heap_only) {
      // Unreachable: a heap_only source never carries an inline target.
    } else if constexpr (details::inline_fit_guaranteed(Policy, P)) {
      vt->relocate(other.storage_.buf, storage_.buf);
      vtable_ = vt;
    } else {
      const bool inline_ok =
          Policy.alloc != proxy_alloc::heap_only && vt->size <= buf_size &&
          vt->align <= buf_align;
      if (inline_ok) {
        vt->relocate(other.storage_.buf, storage_.buf);
        vtable_ = vt;
        return;
      }
      if constexpr (Policy.alloc != proxy_alloc::sbo_only) {
        storage_.ptr = vt->to_heap(other.storage_.buf);
        vtable_ = vt->heap_table;
      } else {
        throw std::length_error(
            "the target cannot be stored in an sbo_only proxy's buffer");
      }
    }
  }

  // `do_take_heap`: the heap-arrival half of `do_adopt`, which steals the
  // pointer, or un-boxes into an `sbo_only` `proxy`'s buffer (or throws, when
  // the erased target does not fit it or cannot move).
  template<Facade D, proxy_policy P>
  void do_take_heap(proxy<D, P>& other, const owning_vtable_t* vt) {
    if constexpr (P.alloc == proxy_alloc::sbo_only) {
      // Unreachable: an sbo_only source never carries a heap target.
    } else if constexpr (Policy.alloc != proxy_alloc::sbo_only) {
      // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign): see target
      storage_.ptr = other.storage_.ptr;
      vtable_ = vt;
    } else {
      if (!vt->to_sbo || vt->size > buf_size || vt->align > buf_align)
        throw std::length_error(
            "the target cannot be stored in an sbo_only proxy's buffer");
      // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage): see target
      vt->to_sbo(other.storage_.ptr, storage_.buf);
      vtable_ = vt->sbo_table;
    }
  }

  storage_t storage_;
  const owning_vtable_t* vtable_{};

  template<Facade G, proxy_policy P>
  friend class proxy;
  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
  template<Facade G>
  friend class shared_proxy;
};

// `proxy_impl`: library-provided binding so that an owning `proxy` satisfies
// its own facade and every facade that facade extends, like the view.
//
// Calls forward through the proxy, with conditional `noexcept`. The const
// overload serves const-qualified methods, matching the proxy's deep const,
// and the non-const overload serves the rest.
template<Facade F, Facade D, proxy_policy P>
requires(std::same_as<D, F> || Extends<D, F>)
struct proxy_impl<F, proxy<D, P>> {
  // Qualified forwarding, as with the view bindings; see `qualified_key`.
  template<fixed_string Key, typename... Args>
  static decltype(auto)
  on(method_key<Key>, proxy<D, P>& p, Args&&... args) noexcept(
      noexcept(p.template call<details::qualified_key<F, Key>()>(
          std::forward<Args>(args)...))) {
    return p.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
  template<fixed_string Key, typename... Args>
  static decltype(auto)
  on(method_key<Key>, const proxy<D, P>& p, Args&&... args) noexcept(
      noexcept(p.template call<details::qualified_key<F, Key>()>(
          std::forward<Args>(args)...))) {
    return p.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
};

// `make_proxy`: make an owning `proxy` of facade `F` holding a `T` constructed
// in place from `args`.
//
// To move an existing object in, pass it as the constructor argument:
// `make_proxy<F, T>(std::move(obj))`. A non-default storage policy is the
// optional third argument: `make_proxy<F, T, proxy_policy{...}>(...)`.
template<Facade F, typename T, proxy_policy Policy = proxy_policy{},
    typename... Args>
requires Proxiable<T, F>
[[nodiscard]] proxy<F, Policy> make_proxy(Args&&... args) {
  return proxy<F, Policy>{std::in_place_type<T>, std::forward<Args>(args)...};
}

// `make_proxy`: make an owning `proxy` of facade `F`, adopting a heap target
// already owned by a `std::unique_ptr`; see the adopting constructor.
template<Facade F, proxy_policy Policy = proxy_policy{}, typename T>
requires Proxiable<T, F>
[[nodiscard]] proxy<F, Policy> make_proxy(std::unique_ptr<T> target) {
  return proxy<F, Policy>{std::move(target)};
}

#pragma endregion
#pragma region Shared ownership

// `shared_proxy`: shared-owning erased handle, like Rust's `Rc<dyn Trait>`,
// backed by a `std::shared_ptr<void>`.
//
// Copyable, and a copy shares the one target rather than cloning it; the
// target dies with its last owner.
//
// Storing the pointer as `shared_ptr<void>` does not lose destruction: a
// `shared_ptr`'s deleter lives in its control block, fixed when the first
// owner was created, and converting a `shared_ptr<T>` to `shared_ptr<void>`
// hands over that control block untouched, so the target is still destroyed as
// a `T`. (Adoption from an owning `proxy` has no typed pointer to start from;
// it supplies the owning table's destroy thunk as the deleter instead.)
//
// The control block therefore type-erases destruction on its own, which is why
// no owning table is needed: the handle is the shared pointer plus the same
// dispatch table the views use, born-keyed the same way. There is no
// inline-storage mode, so the target's address is always stable.
//
// Ownership interoperates with `std`: construct from a `std::shared_ptr<T>`
// (sharing with any outside holders) or a `std::unique_ptr<T>` (which
// converts), or build target and control block in one allocation with
// `make_shared_proxy`. Reference counting is `std::shared_ptr`'s, with its
// usual thread-safety: the count is atomic, the target's own state is the
// user's business.
//
// Deep-const as an instance, like the views: only const-qualified methods
// dispatch through a const handle, and copying escapes it (a copy of a
// `const shared_proxy` is mutable). A `weak_proxy` observes without owning.
//
// A default-constructed or moved-from handle is empty: testable via
// `operator bool`, but calling through it is undefined behavior. Handles
// upcast implicitly, by copy or by move, to any facade theirs extends.
template<Facade F>
class shared_proxy: public details::api_base_t<F> {
  using vtbuild_t = details::vtbuild_t<F>;
  using vtable_t = vtbuild_t::vtable_t;

public:
  using facade_t = F;

  // `shared_proxy`: an empty handle holds no target.
  shared_proxy() = default;

  // `shared_proxy`: adopting constructor from shared ownership of a concrete
  // target. A null pointer yields an empty handle.
  //
  // Usually spelled through `make_shared_proxy`; there is deliberately no
  // raw-pointer constructor.
  template<typename T>
  requires Proxiable<T, F>
  explicit shared_proxy(std::shared_ptr<T> target) noexcept
      : target_{std::move(target)} {
    if (target_) vtable_ = &details::vtable_for<F, T>;
  }

  // `shared_proxy`: adopting constructor from unique ownership, which becomes
  // shared (this allocates the control block, so unlike the shared flavor it
  // can throw).
  template<typename T>
  requires Proxiable<T, F>
  explicit shared_proxy(std::unique_ptr<T> target)
      : shared_proxy{std::shared_ptr<T>{std::move(target)}} {}

  // `shared_proxy`: adopting constructor from an owning `proxy` of `F`, or of
  // a facade that extends it.
  //
  // The unique ownership becomes shared (Rust: `Box<dyn T>` into `Rc<dyn T>`),
  // consuming the source.
  //
  // A heap-stored target is adopted as-is, the owning table's destroy slot
  // becoming the control block's deleter, so only the control block
  // allocates; an inline target moves onto the heap first. On a throw from
  // the control-block allocation the target is destroyed rather than
  // leaked, and the source is left empty (the same contract as
  // `std::shared_ptr`'s constructor from a `unique_ptr`); a throw from the
  // re-boxing leaves the source intact.
  //
  // The reverse conversion deliberately does not exist: unique ownership
  // cannot be recovered from a shared target, even at a use count of one,
  // without racing the other owners, which is also why `std::shared_ptr`
  // has no `release`.
  template<Facade D, proxy_policy P>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit shared_proxy(proxy<D, P>&& source) {
    const auto* src = source.vtable_;
    if (!src) return;
    const auto* ovt = details::upcast_owning_vtable<F, D>(src);
    void* ptr = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    if (src->relocate) {
      ptr = ovt->to_heap(source.storage_.buf);
      destroy = ovt->heap_table->destroy;
    } else {
      ptr = source.storage_.ptr;
      destroy = ovt->destroy;
    }
    source.vtable_ = nullptr;
    target_ = std::shared_ptr<void>{ptr, destroy};
    vtable_ = &ovt->vt;
  }

  // `shared_proxy`: upcasting converting constructors from a `shared_proxy`
  // of a facade that extends `F`, sharing (copy) or transferring (move)
  // ownership. Intentionally implicit, like every handle upcast.
  template<Facade D>
  requires Extends<D, F>
  explicit(false) shared_proxy(const shared_proxy<D>& other) noexcept
      : target_{other.target_},
        vtable_{
            other ? details::upcast_vtable<F, D>(other.vtable_) : nullptr} {}

  template<Facade D>
  requires Extends<D, F>
  explicit(false) shared_proxy(shared_proxy<D>&& other) noexcept
      : vtable_{
            other ? details::upcast_vtable<F, D>(other.vtable_) : nullptr} {
    target_ = std::move(other.target_);
  }

  // `call`: call the facade method named `Key`, forwarding `args` through the
  // erased signature; the same dispatch as the other handles, deep const
  // included.
  template<fixed_string Key, typename... Args>
  decltype(auto) call(Args&&... args) noexcept(
      vtbuild_t::template is_noexcept<Key, false, Args...>()) {
    return details::dispatch<F, false, Key>(vtable_->thunks, target(),
        std::forward<Args>(args)...);
  }

  template<fixed_string Key, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<Key>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  decltype(auto) call(Args&&... args) const
      noexcept(vtbuild_t::template is_noexcept<Key, true, Args...>()) {
    return details::dispatch<F, true, Key>(vtable_->thunks, target(),
        std::forward<Args>(args)...);
  }

  // `operator bool`: an empty handle (default-constructed or moved-from) holds
  // no target.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(target_);
  }

  // `try_downcast`: attempt to recover a `shared_proxy` of `D`, a facade
  // extending `F`, from a handle that may have been upcast away from it.
  //
  // The table remembers the facade the target was born as, exactly as with
  // the owning proxy (see `proxy::try_downcast`), including a birth adopted
  // from a consumed `proxy`. Because shared ownership is copyable, the
  // lvalue flavor shares: on success the result is another owner of the one
  // target and the source keeps its own share. The rvalue flavor transfers
  // instead, consuming the source only on success. On failure, including an
  // empty source, the result is empty and the source is untouched.
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] shared_proxy<D> try_downcast() const& noexcept {
    const auto* table = do_find_downcast<D>();
    if (!table) return {};
    return shared_proxy<D>{target_, table};
  }
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] shared_proxy<D> try_downcast() && noexcept {
    const auto* table = do_find_downcast<D>();
    if (!table) return {};
    shared_proxy<D> result{std::move(target_), table};
    vtable_ = nullptr;
    return result;
  }

private:
  // `target`: the target address; meaningless when empty.
  [[nodiscard]] void* target() noexcept {
    assert(target_);
    return target_.get();
  }
  [[nodiscard]] const void* target() const noexcept {
    assert(target_);
    return target_.get();
  }

  // `do_find_downcast`: `D`'s view table from the birth ancestry, or null when
  // the handle is empty or was not born as `D` or a facade extending it.
  template<Facade D>
  [[nodiscard]] const details::vtbuild_t<D>::vtable_t*
  do_find_downcast() const noexcept {
    if (!vtable_) return nullptr;
    return static_cast<const details::vtbuild_t<D>::vtable_t*>(
        details::find_ancestor(*vtable_->ancestry, &details::facade_tag_v<D>));
  }

  // `shared_proxy`: for `weak_proxy::lock` and `try_downcast`, whose vtable
  // pointer is already resolved; a null target (an expired weak pointer)
  // yields an empty handle.
  shared_proxy(std::shared_ptr<void> target, const vtable_t* vtable) noexcept
      : target_{std::move(target)}, vtable_{vtable} {}

  std::shared_ptr<void> target_;
  const vtable_t* vtable_{};

  template<Facade G>
  friend class shared_proxy;
  template<Facade G>
  friend class weak_proxy;
  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
};

// `proxy_impl`: library-provided binding so that a shared proxy satisfies its
// own facade and every facade that facade extends, like the other handles; see
// the owning proxy's binding.
template<Facade F, Facade D>
requires(std::same_as<D, F> || Extends<D, F>)
struct proxy_impl<F, shared_proxy<D>> {
  template<fixed_string Key, typename... Args>
  static decltype(auto)
  on(method_key<Key>, shared_proxy<D>& p, Args&&... args) noexcept(
      noexcept(p.template call<details::qualified_key<F, Key>()>(
          std::forward<Args>(args)...))) {
    return p.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
  template<fixed_string Key, typename... Args>
  static decltype(auto)
  on(method_key<Key>, const shared_proxy<D>& p, Args&&... args) noexcept(
      noexcept(p.template call<details::qualified_key<F, Key>()>(
          std::forward<Args>(args)...))) {
    return p.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
};

// `weak_proxy`: weak counterpart to `shared_proxy`, which observes the target
// without owning it, via a `std::weak_ptr<void>`.
//
// It carries no dispatch at all; regaining access always goes through
// `lock`, which returns a shared proxy (empty if every owner is gone), so
// there is no way to call through a target that might be dying.
template<Facade F>
class weak_proxy {
  using vtable_t = details::vtbuild_t<F>::vtable_t;

public:
  // `weak_proxy`: an empty weak proxy observes nothing; `lock` yields an
  // empty handle.
  weak_proxy() = default;

  // `weak_proxy`: observe a `shared_proxy` of `F`, or of a facade that
  // extends it (the upcast happens here, so `lock` is cheap).
  //
  // Intentionally implicit.
  template<Facade D>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) weak_proxy(const shared_proxy<D>& p) noexcept
      : target_{p.target_},
        vtable_{p ? details::upcast_vtable<F, D>(p.vtable_) : nullptr} {}

  // `weak_proxy`: upcasting converting constructors from a `weak_proxy` of a
  // facade that extends `F`, by copy or by move, mirroring the
  // `shared_proxy`'s.
  //
  // Intentionally implicit, like every handle upcast. An expired source
  // upcasts like a live one, still observing the same target; expiry stays
  // `lock`'s business.
  template<Facade D>
  requires Extends<D, F>
  explicit(false) weak_proxy(const weak_proxy<D>& other) noexcept
      : target_{other.target_},
        vtable_{other.vtable_ ? details::upcast_vtable<F, D>(other.vtable_)
                              : nullptr} {}

  template<Facade D>
  requires Extends<D, F>
  explicit(false) weak_proxy(weak_proxy<D>&& other) noexcept
      : vtable_{other.vtable_ ? details::upcast_vtable<F, D>(other.vtable_)
                              : nullptr} {
    target_ = std::move(other.target_);
  }

  // `expired`: whether the target is already gone.
  //
  // As with `std::weak_ptr`, a false answer is stale the moment it is read;
  // `lock` is the reliable gate.
  [[nodiscard]] bool expired() const noexcept { return target_.expired(); }

  // `lock`: regain shared ownership by creating a `shared_proxy` over the
  // target, or an empty one when every owner is gone.
  [[nodiscard]] shared_proxy<F> lock() const noexcept {
    return shared_proxy<F>{target_.lock(), vtable_};
  }

private:
  std::weak_ptr<void> target_;
  const vtable_t* vtable_{};

  template<Facade G>
  friend class weak_proxy;
};

// `make_shared_proxy`: make a shared proxy of facade `F` holding a `T`
// constructed in place from `args`, with target and control block in one
// allocation (`std::make_shared`).
template<Facade F, typename T, typename... Args>
requires Proxiable<T, F>
[[nodiscard]] shared_proxy<F> make_shared_proxy(Args&&... args) {
  return shared_proxy<F>{std::make_shared<T>(std::forward<Args>(args)...)};
}

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
// are too generic to export and facade and impl authors are already working in
// that domain.
using prox::const_proxy_view;
using prox::make_proxy;
using prox::make_proxy_view;
using prox::make_shared_proxy;
using prox::proxy;
using prox::proxy_alloc;
using prox::Proxiable;
using prox::proxy_policy;
using prox::proxy_view;
using prox::proxy_impl_base;
using prox::shared_proxy;
using prox::weak_proxy;

#pragma endregion

}} // namespace corvid::meta
