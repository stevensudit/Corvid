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
#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

#include "proxy_common.h"

// The non-owning erased handles, `proxy_view` and `const_proxy_view`, along
// with `make_proxy_view`.
//
// See "proxy.md" for the design.

namespace corvid { inline namespace meta {
namespace prox {

#pragma region Views

namespace implementation {

// Storage, const-method dispatch, and downcasting shared by the two view
// flavors, which differ only in the constness of the erased target pointer.
//
// The `call` here serves const-qualified methods, the only dispatch a const
// handle allows. The `proxy_view` class layers the unrestricted non-const
// overload on top.
//
// This is also where the views pick up the facade's `api` base, keeping each
// view a single-inheritance chain.
template<Facade F, access_mode Access>
class view_base: public api_base_t<F, view_t<F, Access>> {
protected:
  using vtable_t = vtbuild_t<F>::vtable_t;
  using target_ptr_t = conditional_const_t<Access, void>*;

public:
  using facade_t = F;

  // Call the const-qualified facade method named `Key`, forwarding `args`
  // through the erased signature.
  //
  // The call is `noexcept` when the method is and the argument conversions
  // cannot throw (they are the caller's, as with the `api` forwarders). It is
  // not `[[nodiscard]]`, because discardability belongs to the facade method
  // rather than the dispatcher (the `std::invoke` precedent).
  template<fixed_string Key, typename... Args>
  requires(vtbuild_t<F>::template is_const<Key>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  constexpr decltype(auto)
  call(Args&&... args) const noexcept(vtbuild_t<F>::template is_noexcept<Key,
      access_mode::as_const, Args...>()) {
    return dispatch<F, access_mode::as_const, Key>(vtable_->thunks, target_,
        std::forward<Args>(args)...);
  }

  // Whether the view is non-empty.
  //
  // Calling through an empty view runs the `on_empty` behavior its table
  // carries, which is `raise` for a view built empty. A view built over an
  // empty target gets that target's empty behavior.
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return target_;
  }

  // Try to downcast this instance to make a view of `D`, which is a facade
  // extending `F`, over a target that may have been upcast away from it.
  //
  // The view table remembers the facade the target was born as. For a view
  // built directly over a target, that is the view's own facade. For a view
  // lent from a `proxy` or `shared_proxy`, it is the owner's birth facade,
  // so a lent view recovers exactly what its owner could.
  //
  // On success, the result is a new view over the same target. The source
  // is copied from, never consumed (in contrast to the owning flavor of
  // `try_downcast` in `proxy`, which is an rvalue method). On failure,
  // including an empty source, the result is empty.
  //
  // The result's flavor follows the access the source grants: a mutable
  // `proxy_view` downcasts to a `proxy_view`, while a `const_proxy_view`, or
  // a `proxy_view` reached through `const`, downcasts to a
  // `const_proxy_view`.
  //
  // Copying a const `proxy_view` reopens mutability, and nothing prevents
  // that; the downcast simply declines to be the copy that does it.
  template<Facade D>
  requires Extends<D, F> && (Access == access_mode::as_mutable)
  [[nodiscard]] constexpr proxy_view<D> try_downcast() noexcept {
    const auto* table = find_downcast_table<D, F>(vtable_);
    if (!table) return {};
    return proxy_view<D>{target_, table};
  }
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] constexpr const_proxy_view<D> try_downcast() const noexcept {
    const auto* table = find_downcast_table<D, F>(vtable_);
    if (!table) return {};
    return const_proxy_view<D>{target_, table};
  }

protected:
  constexpr view_base() noexcept = default;
  constexpr view_base(target_ptr_t target, const vtable_t* vtable) noexcept
      : vtable_{vtable}, target_{target} {}

#pragma region Data members

  const vtable_t* vtable_ = &empty_vtable_for<F, on_empty::raise>;
  target_ptr_t target_{};

#pragma endregion
};

} // namespace implementation

// `proxy_view` is a non-owning erased handle over any `Proxiable` target, akin
// to Rust's `&mut dyn Trait` or ngcpp's `proxy_view`.
//
// It consists of two pointers: the target and the per-(facade, type, born
// facade) dispatch table. The target must outlive the view, and the standard
// limitations of views apply.
//
// A view lent from an owning `proxy` is tied to that proxy's contents, not
// just its lifetime: ownership is exclusive, so removing or replacing the
// target (moving the proxy from, assigning over it, `extract`) invalidates
// every view lent from it. Using an invalidated view is undefined behavior,
// exactly as with any other view into an exclusively-owned container.
//
// A view lent from a `shared_proxy` does not share ownership (no view does),
// so its validity follows the underlying object rather than the lending
// handle. In other words, the view stays good as long as any owner keeps the
// object alive, but the view does not participate in the reference-counting
// scheme. Code that needs to guarantee survival holds a copy of the
// `shared_proxy` instead of a view into it.
//
// A view also converts implicitly from any handle of a facade that extends a
// given `F` (Rust trait upcasting), and from an lvalue owning `proxy`. It
// repoints at the handle's target rather than wrapping the handle. An upcast
// view is indistinguishable from one built directly over the target.
//
// Deep-const as an instance, so only const-qualified facade methods dispatch
// through a `const proxy_view`. Because views are freely copyable, that is a
// guardrail rather than a guarantee, as copying a `const proxy_view` yields a
// mutable view onto the same target, much as a `T* const` pointer copies to a
// plain `T*`. Code that enforces read-only access must use `const_proxy_view`,
// where constness is part of the type and survives copying.
//
// A default-constructed view is empty, like a default-constructed `proxy`. It
// is testable via `operator bool` and rebindable by assignment. Calling
// through an empty view raises `std::bad_function_call`, or for `noexcept`
// methods, terminates. A view constructed from an empty `proxy` or
// `shared_proxy` is also empty, but calling through it follows the behavior of
// that source.
//
// When the facade defines a nested `api`, the view inherits it, so the
// member-call sugar forwarders dispatch alongside `call`.
template<Facade F>
class proxy_view
    : public implementation::view_base<F, access_mode::as_mutable> {
  using base = implementation::view_base<F, access_mode::as_mutable>;
  using vtbuild_t = implementation::vtbuild_t<F>;

public:
  proxy_view() = default;

  // Converting constructor from an lvalue `target`, which is an object whose
  // type is registered for `F`, thus erasing the type behind the facade.
  //
  // Intentionally implicit, like `string_view` from `string`. Rvalues do not
  // bind, so construction from a temporary is rejected at compile time.
  //
  // You cannot construct a `proxy_view` on a const `target`; you have to
  // instead construct a `const_proxy_view` on it.
  //
  // Handles of `F`, or of a facade extending it, take the dedicated
  // constructors below instead of being wrapped as targets.
  template<typename T>
  requires(Proxiable<T, F> && !std::is_const_v<T> &&
           !implementation::is_handle_for<T, F>())
  constexpr proxy_view(T& target) noexcept
      : base{std::addressof(target), &implementation::vtable_for<F, T>} {}

  // Upcasting constructor from a view over a facade that extends `F` (Rust
  // trait upcasting).
  //
  // Intentionally implicit, like derived-to-base pointer conversion. The
  // target carries over unchanged and the dispatch table narrows to `F`'s by
  // following the embedded base-table pointers, so the upcast view dispatches
  // exactly what a directly-built `F` view of the target would. An empty view
  // upcasts to an empty view with the same empty behavior.
  template<Facade D>
  requires Extends<D, F>
  constexpr proxy_view(const proxy_view<D>& view) noexcept
      : base{view.target_, implementation::upcast_vtable<F, D>(view.vtable_)} {
  }

  // Viewing constructor from an owning `proxy` of `F`, or of a facade that
  // extends it.
  //
  // Intentionally implicit, and lvalue-only, so a view cannot be left dangling
  // by a temporary `proxy`. An empty `proxy` lends an empty view that keeps
  // the proxy's `on_empty` behavior; otherwise the view is good until the
  // `proxy` dies or has its contents removed or replaced (see the class
  // comment).
  //
  // You cannot construct a `proxy_view` on a const `proxy` reference; you have
  // to instead construct a `const_proxy_view` on it.
  template<Facade D, invocable_policy P>
  requires ExtendsOrIs<D, F>
  proxy_view(proxy<D, P>& p) noexcept
      : base{p ? p.target() : nullptr,
            implementation::upcast_vtable<F, D>(&p.vtable_->vt)} {}

  // Viewing constructor from a `shared_proxy` of `F`, or of a facade that
  // extends it, under the same rules as viewing an owning `proxy`.
  //
  // The view does not share ownership, so it is good exactly as long as some
  // owner keeps the target alive (see the class comment).
  template<Facade D>
  requires ExtendsOrIs<D, F>
  proxy_view(shared_proxy<D>& p) noexcept
      : base{p ? p.target() : nullptr,
            implementation::upcast_vtable<F, D>(p.vtable_)} {}

  // Call the facade method named `Key`, forwarding `args` through the erased
  // signature.
  //
  // The call is `noexcept` when the method's signature is `noexcept` and each
  // of `args` converts to its parameter type without the possibility of an
  // exception. The conversions run inside the dispatcher, so a throwing one
  // would escape a `noexcept` call; see `is_noexcept` on the table builder.
  //
  // This overload dispatches every method. The inherited const overload,
  // re-exposed by the using-declaration, is constrained to const-qualified
  // methods, mirroring the owning `proxy`'s deep const.
  template<fixed_string Key, typename... Args>
  constexpr decltype(auto)
  call(Args&&... args) noexcept(vtbuild_t::template is_noexcept<Key,
      access_mode::as_mutable, Args...>()) {
    return implementation::dispatch<F, access_mode::as_mutable, Key>(
        this->vtable_->thunks, this->target_, std::forward<Args>(args)...);
  }

  using base::call;

private:
  constexpr proxy_view(void* target, const base::vtable_t* vtable) noexcept
      : base{target, vtable} {}

  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
  template<Facade G, access_mode A>
  friend class implementation::view_base;
};

// The library-provided binding so that a `proxy_view` satisfies its own
// facade and every facade that one extends.
template<Facade F, Facade D>
requires ExtendsOrIs<D, F>
struct proxy_impl<F, proxy_view<D>>: implementation::handle_impl<F> {};

// `const_proxy_view` is the non-owning read-only erased handle, akin to Rust's
// `&dyn Trait`. To put it another way, it is the `const_iterator` to
// `proxy_view`'s `iterator`.
//
// Constness is part of the type, so unlike a `const proxy_view`, the constness
// survives copying. A const view only ever copies or converts to another const
// view. It binds const and mutable targets alike, and dispatches only the
// const-qualified facade methods, sharing the mutable view's dispatch table
// (where the non-const slots are simply unreachable). The target must outlive
// the view, under the same lending and invalidation rules as `proxy_view` (see
// its class comment).
//
// A default-constructed view is empty. It is testable via `operator bool` and
// rebindable by assignment.
//
// When the facade defines a nested `api`, the view inherits it. Only the const
// forwarders are callable; a mutable forwarder fails inside its `call` if
// used.
template<Facade F>
class const_proxy_view
    : public implementation::view_base<F, access_mode::as_const> {
  using base = implementation::view_base<F, access_mode::as_const>;

public:
  const_proxy_view() = default;

  // Converting constructor from an lvalue `target`, const or not, which is
  // an object whose type is registered for `F`, thus erasing the type behind
  // the facade and viewing it as const.
  //
  // Intentionally implicit. A temporary is refused by the deleted rvalue
  // overload below, since the const reference would otherwise bind it and
  // dangle.
  //
  // Handles of `F`, or of a facade extending it, take the dedicated
  // constructors below instead of being wrapped as targets.
  template<typename T>
  requires(Proxiable<T, F> && !implementation::is_handle_for<T, F>())
  constexpr const_proxy_view(const T& target) noexcept
      : base{std::addressof(target), &implementation::vtable_for<F, T>} {}

  template<typename T>
  requires(Proxiable<T, F> && !implementation::is_handle_for<T, F>())
  const_proxy_view(const T&&) = delete;

  // Converting constructor from the mutable view.
  //
  // Dropping mutability is implicit and safe, like `T*` to `const T*`, and
  // there is no path back.
  constexpr const_proxy_view(const proxy_view<F>& view) noexcept
      : base{view.target_, view.vtable_} {}

  // Upcasting constructor from a const view over a facade that extends `F`
  // (Rust trait upcasting).
  //
  // Intentionally implicit; see the mutable view's upcasting constructor.
  template<Facade D>
  requires Extends<D, F>
  constexpr const_proxy_view(const const_proxy_view<D>& view) noexcept
      : base{view.target_, implementation::upcast_vtable<F, D>(view.vtable_)} {
  }

  // Upcasting constructor from the mutable view of a facade that extends `F`,
  // dropping mutability and upcasting in one implicit step.
  template<Facade D>
  requires Extends<D, F>
  constexpr const_proxy_view(const proxy_view<D>& view) noexcept
      : base{view.target_, implementation::upcast_vtable<F, D>(view.vtable_)} {
  }

  // Viewing constructor from an owning `proxy` of `F`, or of a facade that
  // extends it.
  //
  // Intentionally implicit, and lvalue-only. An empty `proxy` lends an empty
  // view that keeps the proxy's `on_empty` behavior; otherwise the view is
  // good until the `proxy` dies or has its contents removed or replaced (see
  // `proxy_view`'s class comment). Mutable and const proxies alike yield the
  // const view; there is no path back to mutability.
  template<Facade D, invocable_policy P>
  requires ExtendsOrIs<D, F>
  const_proxy_view(const proxy<D, P>& p) noexcept
      : base{p ? p.target() : nullptr,
            implementation::upcast_vtable<F, D>(&p.vtable_->vt)} {}

  // Temporary owners must not lend a view: without this deletion, the const
  // reference above would bind an rvalue and dangle.
  template<Facade D, invocable_policy P>
  requires ExtendsOrIs<D, F>
  const_proxy_view(const proxy<D, P>&&) = delete;

  // Viewing constructor from a `shared_proxy` of `F`, or of a facade that
  // extends it; see the owning-proxy constructor above. A temporary
  // `shared_proxy` is refused the same way, by the deleted rvalue overload
  // below.
  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_proxy_view(const shared_proxy<D>& p) noexcept
      : base{p ? p.target() : nullptr,
            implementation::upcast_vtable<F, D>(p.vtable_)} {}

  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_proxy_view(const shared_proxy<D>&&) = delete;

  // Viewing constructor from a `const_shared_proxy` of `F`, or of a facade
  // that extends it, under the same rules, and the only view that handle
  // lends.
  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_proxy_view(const const_shared_proxy<D>& p) noexcept
      : base{p ? p.target() : nullptr,
            implementation::upcast_vtable<F, D>(p.vtable_)} {}

  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_proxy_view(const const_shared_proxy<D>&&) = delete;

  // No need for a `using` because we do not declare a `call` here that shadows
  // the base's const-only `call`.

private:
  constexpr const_proxy_view(const void* target,
      const base::vtable_t* vtable) noexcept
      : base{target, vtable} {}

  template<Facade G>
  friend class const_proxy_view;
  template<Facade G, access_mode A>
  friend class implementation::view_base;
};

// The library-provided binding so that a `const_proxy_view` satisfies its
// own facade, and the ones that facade extends, where that is possible; see
// `implementation::handle_impl`.
//
// It dispatches only const methods, so the invariant holds exactly for
// all-const facades.
template<Facade F, Facade D>
requires ExtendsOrIs<D, F>
struct proxy_impl<F, const_proxy_view<D>>: implementation::handle_impl<F> {};

// Make a `proxy_view` over `target`.
//
// When `target` is already a view of `F` it is copied rather than wrapped, so
// generic facade-constrained code can erase its argument without stacking
// indirections. A handle of a facade extending `F`, or an owning proxy,
// likewise re-points at its target (upcasting as needed) through the
// dedicated view constructors.
//
// A const view is refused along with a const target. These dispatch only const
// methods, so there is no mutable access to lend, and `make_const_proxy_view`
// re-erases it without widening it. (Copying a `const proxy_view` directly
// still yields a mutable view. That is the documented guardrail that can still
// be bypassed, and this helper declines to be the place for that.)
template<Facade F, typename T>
requires Proxiable<T, F>
[[nodiscard]] constexpr proxy_view<F> make_proxy_view(T& target) noexcept {
  if constexpr (std::same_as<T, proxy_view<F>>)
    return target;
  else
    return proxy_view<F>{target};
}

// Make a `const_proxy_view` over `target`.
//
// The const counterpart of `make_proxy_view`, accepting what that refuses (a
// const target) along with everything the const view's constructors take, so a
// view of either constness is copied or upcast rather than wrapped.
//
// The gate is those constructors rather than `Proxiable` because a const view
// is not itself a full target of a facade with non-const methods.
template<Facade F, typename T>
requires std::constructible_from<const_proxy_view<F>, const T&>
[[nodiscard]] constexpr const_proxy_view<F>
make_const_proxy_view(const T& target) noexcept {
  return const_proxy_view<F>{target};
}

#pragma endregion

} // namespace prox

#pragma region Exports

// Call-site vocabulary, exported to `corvid::meta`; see proxy_common.h.
using prox::const_proxy_view;
using prox::make_const_proxy_view;
using prox::make_proxy_view;
using prox::proxy_view;

#pragma endregion

}} // namespace corvid::meta
