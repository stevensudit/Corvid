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
#include <memory>
#include <new>
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
// A `noexcept` qualifier is likewise honored: conformance then requires the
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
// `noexcept`): the qualification flags and result type, which are the only
// things that vary. Also supplies the `method_key` base.
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
auto probe(const facade<Ms...>&) -> facade<Ms...>;

} // namespace details

// Concept for a type derived from a single `facade` base.
template<typename F>
concept Facade = requires(const F& f) { details::probe(f); };

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
// A facade author who factors the boilerplate bindings into a plain
// inheritable class (with the partial specialization deriving from it) also
// enables partial overrides: a type whose names line up except for one method
// writes a full specialization that inherits that class, re-exposes its `on`
// overloads with a using-declaration, and declares only the divergent
// binding. See "proxy.md" for the mechanics and "proxy_test.cpp" for an
// example in `proxy_impl_gunslinger`.
//
// For example, given `struct animal : facade<method<"speak", void() const>>`:
//
//    // Facade author's boilerplate, written once, serving any registered
//    // type whose member names line up:
//    template<typename T>
//    requires ProxyRegistered<animal, T>
//    struct proxy_impl<animal, T> {
//      static void on(method_key<"speak">, const T& t) { t.speak(); }
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
//      static void on(method_key<"speak">, const cat& c) { c.meow(); }
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
// type; it is found here by ADL.
//
// The library never defines this function: declaring an overload IS the act of
// registration.
//
// Registration gates a facade author's boilerplate impl; an explicit
// `proxy_impl` specialization does not need it.
template<typename F, typename T>
concept ProxyRegistered = requires {
  corvid_proxy_spec(static_cast<F*>(nullptr), static_cast<T*>(nullptr));
};

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
// `noexcept`); anything else lives in a unique-owned heap allocation.
inline constexpr std::size_t sbo_size = 2 * sizeof(void*);

template<typename T>
constexpr inline bool sbo_eligible_v =
    sizeof(T) <= sbo_size && alignof(T) <= alignof(std::max_align_t) &&
    std::is_nothrow_move_constructible_v<T>;

// Housekeeping thunks for the owning `proxy`: the analog of Rust's drop glue.
//
// `sbo_relocate` move-constructs `*from` into `to` and destroys the source;
// the heap path has none because a heap target moves by pointer steal.
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

  // Qualification of the method named `K`. Both are `false` when no method
  // has that name; rejecting an unknown name is `index_of`'s job.
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

  template<typename F, typename T>
  static constexpr bool all_bound_v =
      (method_traits<Ms>::template bound_v<F, T> && ...);

  template<typename F, typename T>
  static consteval vtable_t make_vtable() noexcept {
    return {method_traits<Ms>::template make_thunk<F, T>()...};
  }

  // Owning dispatch table: the facade methods plus housekeeping slots. A null
  // `relocate` marks a heap-stored target, which moves by pointer steal
  // rather than relocation.
  struct owning_vtable_t {
    vtable_t methods;
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

// Facade-wide machinery for the derived facade type `F`.
template<Facade F>
using vtbuild_t = vtable_builder<decltype(probe(std::declval<const F&>()))>;

// Per-(facade, type) dispatch table instance. The handle stores a pointer to
// this; the table pointer lives in the handle, not in the target (a fat
// handle, like Rust's `&dyn`).
template<Facade F, typename T>
constexpr inline auto vtable_for = vtbuild_t<F>::template make_vtable<F, T>();

// Per-(facade, type) owning dispatch table instance, for `proxy`.
template<Facade F, typename T>
constexpr inline auto owning_vtable_for =
    vtbuild_t<F>::template make_owning_vtable<F, T>();

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

namespace details {

// Storage and const-method dispatch shared by the two view flavors, which
// differ only in the constness of the erased target pointer.
//
// The `call` here serves const-qualified methods, the only dispatch a const
// handle allows; `proxy_view` layers the unrestricted non-const overload on
// top.
template<Facade F, bool Const>
class view_base {
public:
  using facade_t = F;

  // Call the const-qualified facade method named `K`, forwarding `args`
  // through the erased signature. The call is `noexcept` when the method is.
  // Not `[[nodiscard]]`: discardability belongs to the facade method, not
  // the dispatcher (the `std::invoke` precedent).
  template<fixed_string K, typename... Args>
  requires(vtbuild_t<F>::template is_const<K>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  constexpr decltype(auto) call(Args&&... args) const
      noexcept(vtbuild_t<F>::template is_noexcept<K>()) {
    constexpr auto ndx = vtbuild_t<F>::template index_of<K>();
    static_assert(ndx != vtbuild_t<F>::count_v, "no matching signature");
    return std::get<ndx>(*vtable_)(target_, std::forward<Args>(args)...);
  }

protected:
  using vtable_t = vtbuild_t<F>::vtable_t;
  using target_ptr_t = std::conditional_t<Const, const void*, void*>;

  constexpr view_base(target_ptr_t target, const vtable_t* vtable) noexcept
      : target_{target}, vtable_{vtable} {}

  target_ptr_t target_;
  const vtable_t* vtable_;
};

} // namespace details

// Forward declaration so the mutable view can befriend the const flavor.
template<Facade F>
class const_proxy_view;

// Non-owning erased handle over any `Proxiable` target: Rust's `&mut dyn
// Trait`, ngcpp's `proxy_view`.
//
// Two pointers: the target and the per-(facade, type) dispatch table. The
// target must outlive the view.
//
// Deep-const as an instance: only const-qualified facade methods dispatch
// through a `const proxy_view`. Because views are freely copyable, that is a
// guardrail rather than a guarantee: copying a `const proxy_view` yields a
// mutable view onto the same target, much as a `T* const` pointer copies to
// a plain `T*`. Code that means read-only access should use
// `const_proxy_view`, where constness is part of the type and survives
// copying.
template<Facade F>
class proxy_view: public details::view_base<F, false> {
  using base = details::view_base<F, false>;
  using vtbuild_t = details::vtbuild_t<F>;

public:
  // Converting constructor from an lvalue target. Intentionally implicit, like
  // `string_view` from `string`. Rvalues do not bind, so construction from a
  // temporary is rejected at compile time; const targets take a
  // `const_proxy_view`.
  template<typename T>
  requires(
      Proxiable<T, F> && !std::is_const_v<T> && !std::same_as<T, proxy_view>)
  constexpr explicit(false) proxy_view(T& target) noexcept
      : base{std::addressof(target), &details::vtable_for<F, T>} {}

  // Call the facade method named `K`, forwarding `args` through the erased
  // signature. The call is `noexcept` when the method is.
  //
  // This overload dispatches every method; the inherited const overload,
  // re-exposed by the using-declaration, is constrained to const-qualified
  // methods, mirroring the owning proxy's deep const.
  template<fixed_string K, typename... Args>
  constexpr decltype(auto)
  call(Args&&... args) noexcept(vtbuild_t::template is_noexcept<K>()) {
    constexpr auto ndx = vtbuild_t::template index_of<K>();
    static_assert(ndx != vtbuild_t::count_v, "no matching signature");
    return std::get<ndx>(
        *this->vtable_)(this->target_, std::forward<Args>(args)...);
  }

  using base::call;

private:
  friend const_proxy_view<F>;
};

// Library-provided binding so that a view itself satisfies its own facade (as
// in Rust, where `dyn Trait` implements `Trait`).
//
// Calls forward through the wrapped view, with conditional `noexcept` so the
// invariant also holds for facades with noexcept methods. The const overload
// serves const-qualified methods, matching the view's instance-level deep
// const. This makes facade-constrained generic code accept concrete and
// erased arguments interchangeably, and allows views of views.
template<Facade F>
struct proxy_impl<F, proxy_view<F>> {
  template<fixed_string K, typename... Args>
  static constexpr decltype(auto)
  on(method_key<K>, proxy_view<F>& view, Args&&... args) noexcept(
      noexcept(view.template call<K>(std::forward<Args>(args)...))) {
    return view.template call<K>(std::forward<Args>(args)...);
  }
  template<fixed_string K, typename... Args>
  static constexpr decltype(auto)
  on(method_key<K>, const proxy_view<F>& view, Args&&... args) noexcept(
      noexcept(view.template call<K>(std::forward<Args>(args)...))) {
    return view.template call<K>(std::forward<Args>(args)...);
  }
};

// Non-owning read-only erased handle: Rust's `&dyn Trait`, the
// `const_iterator` to `proxy_view`'s `iterator`.
//
// Constness is part of the type, so unlike a `const proxy_view` it survives
// copying: a const view only ever copies or converts to another const view.
// It binds const and mutable targets alike, and dispatches only the
// const-qualified facade methods, sharing the mutable view's per-(facade,
// type) dispatch table (the non-const slots are simply unreachable). The
// target must outlive the view.
template<Facade F>
class const_proxy_view: public details::view_base<F, true> {
  using base = details::view_base<F, true>;

public:
  // Converting constructor from an lvalue target, const or not. Intentionally
  // implicit; rvalues do not bind.
  template<typename T>
  requires(
      Proxiable<std::remove_const_t<T>, F> &&
      !std::same_as<std::remove_const_t<T>, const_proxy_view> &&
      !std::same_as<std::remove_const_t<T>, proxy_view<F>>)
  constexpr explicit(false) const_proxy_view(T& target) noexcept
      : base{std::addressof(target),
            &details::vtable_for<F, std::remove_const_t<T>>} {}

  // Converting constructor from the mutable view: dropping mutability is
  // implicit and safe, like `T*` to `const T*`. There is no path back.
  constexpr explicit(false)
      const_proxy_view(const proxy_view<F>& view) noexcept
      : base{view.target_, view.vtable_} {}

  // `call` is inherited: the base's const-method dispatch is the entire
  // interface, since the mutable methods do not exist on this view.
};

// Library-provided binding so that a const view satisfies its own facade
// where that is possible: it dispatches only const methods, so the invariant
// holds exactly for all-const facades.
//
// The `on` is itself constrained to const methods so that a mixed facade
// fails conformance cleanly at overload resolution, rather than erroring
// during return type deduction of a forwarder whose `call` cannot compile.
template<Facade F>
struct proxy_impl<F, const_proxy_view<F>> {
  template<fixed_string K, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<K>())
  static constexpr decltype(auto)
  on(method_key<K>, const const_proxy_view<F>& view, Args&&... args) noexcept(
      noexcept(view.template call<K>(std::forward<Args>(args)...))) {
    return view.template call<K>(std::forward<Args>(args)...);
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
#pragma region proxy

// Owning erased handle over any `Proxiable` target: Rust's `Box<dyn Trait>`,
// ngcpp's `proxy`.
//
// Move-only. Small targets (at most `sbo_size` bytes, at most
// `std::max_align_t` alignment, nothrow-move-constructible) are stored
// inline; anything else lives in a unique-owned heap allocation. The owning
// dispatch table carries destroy and relocate slots alongside the facade
// methods, so destruction and moves work without knowing the target type.
//
// Unlike the shallow-const view, the proxy owns its target and is deep-const:
// only const-qualified facade methods dispatch through a const proxy.
//
// A default-constructed or moved-from proxy is empty: destructible,
// assignable, and testable via `operator bool`, but calling through it is
// undefined behavior.
template<Facade F>
class proxy {
  using vtbuild_t = details::vtbuild_t<F>;
  using owning_vtable_t = vtbuild_t::owning_vtable_t;

public:
  using facade_t = F;

  // Inline storage capacity in bytes; see the class comment for the other
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
  // relocate through the table's move slot; heap targets move by pointer
  // steal.
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
  // signature. The call is `noexcept` when the method is.
  //
  // The const overload is constrained to const-qualified methods: deep const,
  // enforced at overload resolution so the rejection is visible to `requires`
  // probes as well. Not `[[nodiscard]]`: discardability belongs to the facade
  // method, not the dispatcher (the `std::invoke` precedent).
  template<fixed_string K, typename... Args>
  decltype(auto)
  call(Args&&... args) noexcept(vtbuild_t::template is_noexcept<K>()) {
    constexpr auto ndx = vtbuild_t::template index_of<K>();
    static_assert(ndx != vtbuild_t::count_v, "no matching signature");
    return std::get<ndx>(
        vtable_->methods)(target(), std::forward<Args>(args)...);
  }

  template<fixed_string K, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<K>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  decltype(auto) call(Args&&... args) const
      noexcept(vtbuild_t::template is_noexcept<K>()) {
    constexpr auto ndx = vtbuild_t::template index_of<K>();
    static_assert(ndx != vtbuild_t::count_v, "no matching signature");
    return std::get<ndx>(
        vtable_->methods)(target(), std::forward<Args>(args)...);
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
};

// Library-provided binding so that an owning proxy satisfies its own facade,
// like the view.
//
// Calls forward through the proxy, with conditional `noexcept`. The const
// overload serves const-qualified methods, matching the proxy's deep const;
// the non-const overload serves the rest.
template<Facade F>
struct proxy_impl<F, proxy<F>> {
  template<fixed_string K, typename... Args>
  static decltype(auto)
  on(method_key<K>, proxy<F>& p, Args&&... args) noexcept(
      noexcept(p.template call<K>(std::forward<Args>(args)...))) {
    return p.template call<K>(std::forward<Args>(args)...);
  }
  template<fixed_string K, typename... Args>
  static decltype(auto)
  on(method_key<K>, const proxy<F>& p, Args&&... args) noexcept(
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
// the registration machinery) stays inside `prox`: those names are too
// generic to export, and facade and impl authors are already working in that
// domain.
using prox::const_proxy_view;
using prox::make_proxy;
using prox::make_proxy_view;
using prox::proxy;
using prox::Proxiable;
using prox::proxy_view;

#pragma endregion

}} // namespace corvid::meta
