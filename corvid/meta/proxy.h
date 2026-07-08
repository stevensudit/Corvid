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
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "fixed_string.h"

// Registration-based runtime polymorphism ("proxy") system.
//
// Type-erased handles over an interface definition (a facade), without
// inheritance, vtable pointers in the target type, or macros.
//
// Conformance is based on name, not shape. A (facade, type) pair is bound by
// an explicit `proxy_impl` specialization or by registering the pair to a
// facade author's boilerplate impl, in the same spirit as registered enums.
// See "proxy.md" for the design.
namespace corvid { inline namespace meta { namespace prox {

#pragma region Method and key

// Method key.
//
// A tag carrying a method name as a compile-time string.
//
// `proxy_impl` bindings overload their `on` hook on it; it is how code
// canonically names a method, given that a library cannot mint a member with a
// caller-chosen name.
template<fixed_string Name>
struct key {
  static constexpr auto name_v = Name;
};

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
// A method derives from its `key`, so a method tag is usable anywhere its key
// is, including `on` overload selection and deduction of `key<K>` from a
// method argument. This also leaves the door open for bindings that overload
// on the full method (signature included) if per-name overload sets are ever
// supported.
template<fixed_string Name, typename Sig>
struct method;

template<fixed_string Name, typename R, typename... Args>
struct method<Name, R(Args...)>: key<Name> {
  static constexpr bool const_v = false;
  using result_t = R;
};

template<fixed_string Name, typename R, typename... Args>
struct method<Name, R(Args...) const>: key<Name> {
  static constexpr bool const_v = true;
  using result_t = R;
};

#pragma endregion
#pragma region Facade

// Facade base: an interface definition.
//
// Derive a named, empty struct from `facade`, listing the methods. For
// example:
//
//    struct animal : facade<method<"speak", void() const>> {};
//
// The derived type, `animal` here, IS the facade: it is the `F` in
// `proxy_view<F>` and `proxy_impl<F, T>`, and the type registration keys on.
//
// Because identity is this named type rather than the method list, same-named
// methods in unrelated facades cannot cross-contaminate. This is also why we
// inherit from `facade` instead of just aliasing it.
template<typename... Methods>
struct facade {};

namespace details {

// Probe for the unique public `facade` base of `F`. Declared only; used in
// unevaluated contexts.
template<typename... Ms>
auto facade_probe(const facade<Ms...>&) -> facade<Ms...>;

} // namespace details

// Concept for a type derived from a single `facade` base.
template<typename F>
concept Facade = requires(const F& f) { details::facade_probe(f); };

#pragma endregion
#pragma region Registration and binding

// Binding point between a facade and a concrete type (Rust's `impl Trait for
// Type`).
//
// Intentionally undefined. A specialization provides one static `on` overload
// per facade method, taking the method's `key`, the target (const-qualified
// per the method), and the method's arguments.
//
// A facade author typically also provides a boilerplate partial
// specialization, generic over any registered `T` whose member names line up.
// An explicit full specialization for one type outranks the boilerplate and
// needs no registration.
//
// For example, given `struct animal : facade<method<"speak", void() const>>`:
//
//    // Facade author's boilerplate, written once, serving any registered
//    // type whose member names line up:
//    template<typename T>
//    requires ProxyRegistered<animal, T>
//    struct proxy_impl<animal, T> {
//      static void on(key<"speak">, const T& t) { t.speak(); }
//    };
//
//    // Conforming `dog`, whose names line up: pure registration.
//    consteval auto corvid_proxy_spec(animal*, dog*) {
//      return make_proxy_spec<animal, dog>();
//    }
//
//    // Conforming `cat`, whose names differ: a custom impl, not registration.
//    template<>
//    struct proxy_impl<animal, cat> {
//      static void on(key<"speak">, const cat& c) { c.meow(); }
//    };
template<typename F, typename T>
struct proxy_impl;

// Minimal registration spec.
//
// The value a registration hook returns, currently carrying no knobs. Distinct
// from `corvid_proxy_spec`, which is the hook itself, provided by the user.
// Because each hook's return type is deduced independently, richer spec types
// can be added later without touching existing registrations.
template<typename F, typename T>
struct proxy_spec {};

// Make the registration spec for a (facade, type) pair.
//
// Call this from a `corvid_proxy_spec` overload, just as
// `make_sequence_enum_spec` is returned from `corvid_enum_spec`.
template<typename F, typename T>
[[nodiscard]] consteval proxy_spec<F, T> make_proxy_spec() noexcept {
  return {};
}

// Concept for a (facade, type) pair being registered.
//
// To register a pair, declare a `corvid_proxy_spec(F*, T*)` overload returning
// `make_proxy_spec<F, T>()`, in the namespace of either the facade or the
// type; it is found here by ADL. The library never defines this function:
// declaring an overload IS the act of registration. The `corvid_` prefix
// marks it as a cross-namespace protocol name, the same convention as
// `corvid_enum_spec`; a plain verb like `register_proxy` would risk collision
// in user namespaces and would not read as a Corvid hook. The pointer
// parameters are never dereferenced; they only carry the types.
//
// Registration gates a facade author's boilerplate impl; an explicit
// `proxy_impl` specialization does not need it.
template<typename F, typename T>
concept ProxyRegistered = requires {
  corvid_proxy_spec(static_cast<F*>(nullptr), static_cast<T*>(nullptr));
};

// Central access point for the registered spec, mirroring `enum_spec_v`. Each
// specialization deduces its own type, so richer spec types can be introduced
// later without touching existing registrations.
template<typename F, typename T>
requires ProxyRegistered<F, T>
constexpr inline auto proxy_spec_v =
    corvid_proxy_spec(static_cast<F*>(nullptr), static_cast<T*>(nullptr));

#pragma endregion
#pragma region Dispatch details

namespace details {

// Per-method dispatch machinery, split on the const qualification of the
// erased signature: the thunk pointer type, the compile-time check that a
// `proxy_impl` binding exists, and the thunk itself, which is where the
// concrete type is seen for the last time before erasure.
template<typename M>
struct method_traits;

template<fixed_string Name, typename R, typename... Args>
struct method_traits<method<Name, R(Args...)>> {
  using thunk_ptr_t = R (*)(void*, Args...);

  template<typename F, typename T>
  static constexpr bool bound_v = requires(T& t, Args... args) {
    {
      proxy_impl<F, T>::on(key<Name>{}, t, std::forward<Args>(args)...)
    } -> std::convertible_to<R>;
  };

  template<typename F, typename T>
  static consteval thunk_ptr_t make_thunk() noexcept {
    return [](void* target, Args... args) -> R {
      return proxy_impl<F, T>::on(key<Name>{}, *static_cast<T*>(target),
          std::forward<Args>(args)...);
    };
  }
};

template<fixed_string Name, typename R, typename... Args>
struct method_traits<method<Name, R(Args...) const>> {
  using thunk_ptr_t = R (*)(const void*, Args...);

  template<typename F, typename T>
  static constexpr bool bound_v = requires(const T& t, Args... args) {
    {
      proxy_impl<F, T>::on(key<Name>{}, t, std::forward<Args>(args)...)
    } -> std::convertible_to<R>;
  };

  template<typename F, typename T>
  static consteval thunk_ptr_t make_thunk() noexcept {
    return [](const void* target, Args... args) -> R {
      return proxy_impl<F, T>::on(key<Name>{}, *static_cast<const T*>(target),
          std::forward<Args>(args)...);
    };
  }
};

// Facade-wide dispatch machinery, specialized on the `facade` base to get at
// the method pack: method count, name-to-slot lookup, the dispatch table type,
// the all-methods-bound check behind `Proxiable`, and the table builder.
template<typename FB>
struct vtable_builder;

template<typename... Ms>
struct vtable_builder<facade<Ms...>> {
  static constexpr std::size_t count_v = sizeof...(Ms);

  using vtable_t = std::tuple<typename method_traits<Ms>::thunk_ptr_t...>;

  // Slot index of the method named `K`, or `count_v` (as an `npos` or `end`
  // flag) when there is none.
  template<fixed_string K>
  static consteval std::size_t index_of() noexcept {
    constexpr std::array<bool, sizeof...(Ms)> matches{(Ms::name_v == K)...};
    for (std::size_t ndx = 0; ndx != matches.size(); ++ndx)
      if (matches[ndx]) return ndx;
    return count_v;
  }

  template<typename F, typename T>
  static constexpr bool all_bound_v =
      (method_traits<Ms>::template bound_v<F, T> && ...);

  template<typename F, typename T>
  static consteval vtable_t make_vtable() noexcept {
    return {method_traits<Ms>::template make_thunk<F, T>()...};
  }
};

// Facade-wide machinery for the derived facade type `F`.
template<Facade F>
using vtbuild_t =
    vtable_builder<decltype(facade_probe(std::declval<const F&>()))>;

// Per-(facade, type) dispatch table instance. The handle stores a pointer to
// this; the table pointer lives in the handle, not in the target (a fat
// handle, like Rust's `&dyn`).
template<Facade F, typename T>
constexpr inline auto vtable_for = vtbuild_t<F>::template make_vtable<F, T>();

} // namespace details

#pragma endregion
#pragma region Proxiable

// Concept: `T` can back facade `F`.
//
// Satisfied when a usable `proxy_impl<F, T>` binding exists for every method
// of `F`. An explicit specialization satisfies it directly; a facade author's
// boilerplate satisfies it exactly when the pair is registered. This is the
// gate on proxy construction and doubles as the trait bound for
// static-dispatch templates.
template<typename T, typename F>
concept Proxiable =
    Facade<F> && details::vtbuild_t<F>::template all_bound_v<F, T>;

#pragma endregion
#pragma region proxy_view

// Non-owning erased handle over any `Proxiable` target: Rust's `&dyn Trait`,
// ngcpp's `proxy_view`.
//
// Two pointers: the target and the per-(facade, type) dispatch table. Copyable
// and shallow-const like other views: `call` is const and dispatches const and
// non-const facade methods alike. The target must outlive the view.
template<Facade F>
class proxy_view {
public:
  using facade_t = F;

  // Converting constructor from an lvalue target. Intentionally implicit, like
  // `string_view` from `string`. Rvalues do not bind, so construction from a
  // temporary is rejected at compile time; const targets are rejected until a
  // const view flavor exists.
  template<typename T>
  requires(Proxiable<T, F> && !std::is_const_v<T> &&
              !std::same_as<T, proxy_view>)
  constexpr explicit(false) proxy_view(T& target) noexcept
      : target_{std::addressof(target)}, vtable_{&details::vtable_for<F, T>} {}

  // Call the facade method named `K`, forwarding `args` through the erased
  // signature. Fails to compile when no method has that name; the `if
  // constexpr` discards the dispatch on that path so the `static_assert` is
  // the only error emitted.
  template<fixed_string K, typename... Args>
  constexpr decltype(auto) call(Args&&... args) const {
    constexpr auto ndx = vtbuild_t::template index_of<K>();
    static_assert(ndx != vtbuild_t::count_v,
        "facade has no method with this name");
    return std::get<ndx>(*vtable_)(target_, std::forward<Args>(args)...);
  }

private:
  using vtbuild_t = details::vtbuild_t<F>;
  using vtable_t = typename vtbuild_t::vtable_t;

  void* target_;
  const vtable_t* vtable_;
};

// Library-provided binding so that a view itself satisfies its own facade (as
// in Rust, where `dyn Trait` implements `Trait`).
//
// Calls forward through the wrapped view. This makes facade-constrained
// generic code accept concrete and erased arguments interchangeably, and
// allows views of views.
template<Facade F>
struct proxy_impl<F, proxy_view<F>> {
  template<fixed_string K, typename... As>
  static constexpr decltype(auto)
  on(key<K>, const proxy_view<F>& view, As&&... args) {
    return view.template call<K>(std::forward<As>(args)...);
  }
};

// Make a view over `target`.
//
// A convenience for spelling the facade at the call site. When `target` is
// already a view of `F` it is copied rather than wrapped, so generic
// facade-constrained code can erase its argument without stacking
// indirections.
template<Facade F, typename T>
requires Proxiable<T, F>
[[nodiscard]] constexpr proxy_view<F> make_proxy_view(T& target) noexcept {
  if constexpr (std::same_as<std::remove_cv_t<T>, proxy_view<F>>)
    return target;
  else
    return proxy_view<F>{target};
}

#pragma endregion

}}} // namespace corvid::meta::prox
