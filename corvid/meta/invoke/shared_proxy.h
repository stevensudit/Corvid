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

#include "owning_proxy.h"

// The shared-owning erased handles, `shared_proxy` and `const_shared_proxy`,
// their observers `weak_proxy` and `const_weak_proxy`, along with the makers.
//
// See "proxy.md" for the design.

namespace corvid { inline namespace meta {
namespace prox {

#pragma region Shared ownership

namespace details {

// Storage, moves, const-method dispatch, and downcasting shared by the two
// shared-owning flavors, which differ only in the constness of the erased
// target pointer.
//
// The `call` here serves const-qualified methods, the only dispatch a const
// handle allows. The `shared_proxy` class layers the unrestricted non-const
// overload on top, as `proxy_view` does over `view_base`.
//
// The moves live here because they must leave the source on its own type's
// empty table, which is the one piece of behavior a moved-from shared handle
// has. Copies are the defaults: a copy shares the one target.
template<Facade F, access_mode Access>
class shared_base: public api_base_t<F> {
protected:
  using vtable_t = vtbuild_t<F>::vtable_t;
  using target_t = conditional_const_t<Access, void>;
  using shared_ptr_t = std::shared_ptr<target_t>;
  template<typename T>
  using typed_t = conditional_const_t<Access, T>;

  // The shared-owning flavor with this access mode over facade `D`, which is
  // what the transferring `try_downcast` returns.
  template<Facade D>
  using shared_t = std::conditional_t<(Access == access_mode::as_const),
      const_shared_proxy<D>, shared_proxy<D>>;

public:
  using facade_t = F;

  // Call the const-qualified facade method named `Key`, forwarding `args`
  // through the erased signature; see `view_base::call`.
  template<fixed_string Key, typename... Args>
  requires(vtbuild_t<F>::template is_const<Key>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  decltype(auto)
  call(Args&&... args) const noexcept(vtbuild_t<F>::template is_noexcept<Key,
      access_mode::as_const, Args...>()) {
    return dispatch<F, access_mode::as_const, Key>(vtable_->thunks, target(),
        std::forward<Args>(args)...);
  }

  [[nodiscard]] explicit operator bool() const noexcept { return !!target_; }

  // Try to share the target out as a typed `std::shared_ptr`,
  // `const`-qualified through a const handle.
  //
  // The shared analog of `proxy::extract<T>`, non-consuming because ownership
  // is shared. The result is another owner of the one target, alongside this
  // handle and every outside holder, and it outlives them all if it is the
  // last.
  //
  // `T` must be the target's exact type, verified at runtime through the
  // table's type tag. On a mismatch, or an empty handle, the result is null
  // and the handle is untouched.
  template<typename T>
  [[nodiscard]] std::shared_ptr<typed_t<T>> try_share() const noexcept {
    if (vtable_->type_tag != &type_tag_v<T>) return nullptr;
    return std::static_pointer_cast<typed_t<T>>(target_);
  }

  // Try to downcast this instance to recover a shared handle of `D`, which is
  // a facade extending `F`, from a handle that may have been upcast away from
  // it.
  //
  // The table remembers the facade the target was born as, exactly as with
  // the owning proxy (see `proxy::try_downcast`), including a birth adopted
  // from a consumed `proxy`.
  //
  // Called on an lvalue, the result is another owner of the one target and
  // the source keeps its own share, because shared ownership is copyable.
  // Called on an rvalue, the source's share transfers to the result, so the
  // source is consumed, but only on success. On failure, including an empty
  // source, the result is empty and the source is untouched.
  //
  // The result's flavor follows the access the source grants: a mutable
  // `shared_proxy` downcasts to a `shared_proxy`, while a
  // `const_shared_proxy`, or a `shared_proxy` reached through `const`,
  // downcasts to a `const_shared_proxy`.
  //
  // Copying a const `shared_proxy` reopens mutability, and nothing prevents
  // that; the downcast simply declines to be the copy that does it.
  template<Facade D>
  requires Extends<D, F> && (Access == access_mode::as_mutable)
  [[nodiscard]] shared_proxy<D> try_downcast() & noexcept {
    const auto* table = find_downcast_table<D, F>(vtable_);
    if (!table) return {};
    return shared_proxy<D>{target_, table};
  }
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] const_shared_proxy<D> try_downcast() const& noexcept {
    const auto* table = find_downcast_table<D, F>(vtable_);
    if (!table) return {};
    return const_shared_proxy<D>{target_, table};
  }
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] shared_t<D> try_downcast() && noexcept {
    const auto* table = find_downcast_table<D, F>(vtable_);
    if (!table) return {};
    shared_t<D> result{std::move(target_), table};
    vtable_ = empty_vtable;
    return result;
  }

protected:
  shared_base() = default;

  shared_base(const shared_base&) = default;
  shared_base& operator=(const shared_base&) = default;

  shared_base(shared_base&& other) noexcept
      : vtable_{std::exchange(other.vtable_, empty_vtable)},
        target_{std::move(other.target_)} {}

  shared_base& operator=(shared_base&& other) noexcept {
    if (this != &other) {
      target_ = std::move(other.target_);
      vtable_ = std::exchange(other.vtable_, empty_vtable);
    }
    return *this;
  }

  ~shared_base() = default;

  // Construct over `target` with `vtable`, taken verbatim so that an empty
  // source's own empty table carries over on an upcast.
  shared_base(shared_ptr_t target, const vtable_t* vtable) noexcept
      : vtable_{vtable}, target_{std::move(target)} {}

  // The target address, or null when empty.
  [[nodiscard]] target_t* target() const noexcept { return target_.get(); }

  // Table of a handle built empty or emptied by a move.
  static constexpr const vtable_t* empty_vtable =
      &empty_vtable_for<F, on_empty::raise>;

  const vtable_t* vtable_ = empty_vtable;
  shared_ptr_t target_;
};

// Storage, `expired`, and `lock` shared by the two weak flavors, which differ
// only in the constness of the observed target and, through it, of the
// shared handle `lock` returns.
//
// There is no `api` base and no `call` here: a weak handle carries its table
// only to hand to the shared handle it locks into.
template<Facade F, access_mode Access>
class weak_base {
protected:
  using vtable_t = vtbuild_t<F>::vtable_t;
  using weak_ptr_t = std::weak_ptr<conditional_const_t<Access, void>>;

  // The shared-owning flavor with this access mode over `F`, which is what
  // `lock` returns.
  using shared_t = std::conditional_t<(Access == access_mode::as_const),
      const_shared_proxy<F>, shared_proxy<F>>;

public:
  // Whether the target is already gone.
  //
  // As with `std::weak_ptr`, a false response is stale the moment it is read
  // while a true one is sticky. The reliable gate is `lock`.
  [[nodiscard]] bool expired() const noexcept { return target_.expired(); }

  // Lock to regain shared ownership by creating a shared handle over the
  // target, or an empty one when every owner is gone.
  [[nodiscard]] shared_t lock() const noexcept {
    return shared_t{target_.lock(), vtable_};
  }

protected:
  weak_base() = default;
  ~weak_base() = default;

  // Construct observing `target` with `vtable`, taken verbatim so that an
  // upcast carries the source's table over.
  weak_base(weak_ptr_t target, const vtable_t* vtable) noexcept
      : vtable_{vtable}, target_{std::move(target)} {}

  const vtable_t* vtable_ = &empty_vtable_for<F, on_empty::raise>;
  weak_ptr_t target_;
};

} // namespace details

// `shared_proxy` is the shared-owning erased handle, like Rust's `Rc<dyn
// Trait>`, backed by a `std::shared_ptr<void>`.
//
// Copyable, and a copy shares the one target rather than cloning it. The
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
// Ownership interoperates with `std`, allowing it to construct from a
// `std::shared_ptr<T>` (sharing with any outside holders) or a
// `std::unique_ptr<T>` (which converts), or build the target and control block
// in one allocation with `make_shared_proxy`.
//
// Reference counting is `std::shared_ptr`'s, with its usual thread-safety
// guarantees. Essentially, the count is atomic, while the target's own state
// is the user's business.
//
// Deep-const as an instance, like the views. Only const-qualified methods
// dispatch through a const handle, and copying escapes it (a copy of a
// `const shared_proxy` is mutable). Constness that survives copying is
// `const_shared_proxy`, which this handle converts to and never back from. A
// `weak_proxy` observes without owning.
//
// A default-constructed or moved-from handle is empty. This is testable via
// `operator bool`, and a call through it raises `std::bad_function_call`,
// or, when the handle was adopted from an empty `proxy`, runs that proxy's
// `on_empty` behavior. Handles upcast implicitly, by copy or by move, to any
// facade theirs extends.
template<Facade F>
class shared_proxy: public details::shared_base<F, access_mode::as_mutable> {
  using base = details::shared_base<F, access_mode::as_mutable>;
  using vtbuild_t = details::vtbuild_t<F>;
  using vtable_t = base::vtable_t;

public:
  shared_proxy() = default;

  // Adopting constructor from shared ownership of a concrete target. A null
  // pointer yields an empty handle.
  //
  // Usually spelled through `make_shared_proxy`. There is deliberately no
  // raw-pointer constructor.
  template<typename T>
  requires Proxiable<T, F>
  explicit shared_proxy(std::shared_ptr<T> target) noexcept
      : shared_proxy{std::shared_ptr<void>{std::move(target)},
            &details::vtable_for<F, T>} {}

  // Adopting constructor from unique ownership, which becomes shared (this
  // allocates the control block, so unlike the shared flavor, it can throw).
  template<typename T>
  requires Proxiable<T, F>
  explicit shared_proxy(std::unique_ptr<T> target)
      : shared_proxy{std::shared_ptr<T>{std::move(target)}} {}

  // Adopting constructor from an owning `proxy` of `F`, or of a facade that
  // extends it.
  //
  // The unique ownership becomes shared (like in Rust: `Box<dyn T>` into
  // `Rc<dyn T>`), consuming the source.
  //
  // A heap-stored target is adopted as-is, the owning table's destroy slot
  // becoming the control block's deleter, so only the control block allocates.
  // An inline target moves onto the heap first.
  //
  // On a throw from the control-block allocation, the target is destroyed
  // rather than leaked, and the source is left empty (the same contract as
  // `std::shared_ptr`'s constructor from a `unique_ptr`). A throw from the
  // boxing leaves the source intact.
  //
  // The reverse conversion deliberately does not exist because unique
  // ownership cannot be recovered from a shared target, even at a use count of
  // one, without racing the other owners. This is also why `std::shared_ptr`
  // has no `release`.
  template<Facade D, invocable_policy P>
  requires ExtendsOrIs<D, F>
  explicit shared_proxy(proxy<D, P>&& source) {
    const auto* src = source.vtable_;
    const auto* ovt = details::upcast_owning_vtable<F, D>(src);
    this->vtable_ = &ovt->vt;
    if (!source) return;
    void* ptr{};
    void (*destroy)(void*) noexcept {};
    if (src->relocate) {
      ptr = ovt->to_heap(source.storage_area_.buf);
      destroy = ovt->heap_table->destroy;
    } else {
      // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign): see target
      ptr = source.storage_area_.ptr;
      destroy = ovt->destroy;
    }
    source.vtable_ = source.empty_vtable;
    this->target_ = std::shared_ptr<void>{ptr, destroy};
  }

  // Upcasting converting constructors from a `shared_proxy` of a facade that
  // extends `F`, sharing (copy) or transferring (move) ownership.
  // Intentionally implicit, like every handle upcast.
  //
  // An empty source upcasts to an empty handle with the same empty behavior. A
  // moved-from source is left with its own type's `raise` table.
  template<Facade D>
  requires Extends<D, F>
  shared_proxy(const shared_proxy<D>& other) noexcept
      : base{other.target_, details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires Extends<D, F>
  shared_proxy(shared_proxy<D>&& other) noexcept
      : base{std::move(other.target_),
            details::upcast_vtable<F, D>(other.vtable_)} {
    other.vtable_ = other.empty_vtable;
  }

  // Call the facade method named `Key`, forwarding `args` through the erased
  // signature. This is the same dispatch as the other handles, deep const
  // included.
  //
  // This overload dispatches every method, and the inherited const overload,
  // re-exposed by the using-declaration, only the const-qualified ones.
  template<fixed_string Key, typename... Args>
  decltype(auto)
  call(Args&&... args) noexcept(vtbuild_t::template is_noexcept<Key,
      access_mode::as_mutable, Args...>()) {
    return details::dispatch<F, access_mode::as_mutable, Key>(
        this->vtable_->thunks, this->target(), std::forward<Args>(args)...);
  }

  using base::call;

private:
  // Construct over `target` with `vtable`, or empty (on the `raise` table)
  // when `target` is null, which is how a failed downcast and an expired lock
  // come out empty.
  shared_proxy(std::shared_ptr<void> target, const vtable_t* vtable) noexcept
      : base{std::move(target), vtable} {
    if (!this->target_) this->vtable_ = base::empty_vtable;
  }

  template<Facade G>
  friend class shared_proxy;
  template<Facade G>
  friend class const_shared_proxy;
  template<Facade G>
  friend class weak_proxy;
  template<Facade G>
  friend class const_weak_proxy;
  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
  template<Facade G, access_mode A>
  friend class details::shared_base;
  template<Facade G, access_mode A>
  friend class details::weak_base;
};

// The library-provided binding so that a `shared_proxy` satisfies its own
// facade and every facade that facade extends.
template<Facade F, Facade D>
requires ExtendsOrIs<D, F>
struct proxy_impl<F, shared_proxy<D>>: details::handle_impl<F> {};

// `const_shared_proxy` is the shared-owning read-only erased handle, akin to
// Rust's `Rc<dyn Trait>` as it actually behaves (shared access is immutable
// access there), or to `std::shared_ptr<const T>`. It is to `shared_proxy`
// what `const_proxy_view` is to `proxy_view`.
//
// Constness is part of the type, so unlike a `const shared_proxy`, it survives
// copying. A const handle only ever copies or converts to another const
// handle, lends only `const_proxy_view`, and dispatches only the
// const-qualified facade methods, sharing the mutable flavor's dispatch table
// (where the non-const slots are simply unreachable). Ownership is otherwise
// the same, where a copy shares the one target, which dies with its last
// owner, whichever flavor that owner is.
//
// It constructs from a `std::shared_ptr` or `std::unique_ptr` of `const T` or
// of `T`, an owning `proxy` (consumed), or a `shared_proxy`
// (sharing by copy or transferring by move, with the `shared_ptr<const T>`
// using the conversion from `shared_ptr<T>`), each of `F` or of a facade
// extending it.
//
// There is no path back to mutability. A `const_weak_proxy` observes without
// owning.
//
// Emptiness, empty behavior, and upcasting are as for `shared_proxy`.
template<Facade F>
class const_shared_proxy
    : public details::shared_base<F, access_mode::as_const> {
  using base = details::shared_base<F, access_mode::as_const>;
  using vtable_t = base::vtable_t;

public:
  const_shared_proxy() = default;

  // Adopting constructors from shared ownership of a concrete target, const
  // or not. A null pointer yields an empty handle.
  //
  // Usually spelled through `make_const_shared_proxy`. There is deliberately
  // no raw-pointer constructor.
  template<typename T>
  requires Proxiable<T, F>
  explicit const_shared_proxy(std::shared_ptr<const T> target) noexcept
      : const_shared_proxy{std::shared_ptr<const void>{std::move(target)},
            &details::vtable_for<F, T>} {}

  template<typename T>
  requires Proxiable<T, F>
  explicit const_shared_proxy(std::shared_ptr<T> target) noexcept
      : const_shared_proxy{std::shared_ptr<const T>{std::move(target)}} {}

  // Adopting constructors from unique ownership of a concrete target, const
  // or not, which becomes shared; see `shared_proxy`'s, including that it can
  // throw.
  template<typename T>
  requires Proxiable<T, F>
  explicit const_shared_proxy(std::unique_ptr<const T> target)
      : const_shared_proxy{std::shared_ptr<const T>{std::move(target)}} {}

  template<typename T>
  requires Proxiable<T, F>
  explicit const_shared_proxy(std::unique_ptr<T> target)
      : const_shared_proxy{std::shared_ptr<T>{std::move(target)}} {}

  // Adopting constructor from an owning `proxy` of `F`, or of a facade that
  // extends it, consuming the source; see `shared_proxy`'s, whose contract
  // this shares because the adoption goes through it.
  template<Facade D, invocable_policy P>
  requires ExtendsOrIs<D, F>
  explicit const_shared_proxy(proxy<D, P>&& source)
      : const_shared_proxy{shared_proxy<D>{std::move(source)}} {}

  // Converting constructors from a `shared_proxy` of `F`, or of a facade that
  // extends it, sharing (copy) or transferring (move) ownership.
  //
  // Intentionally implicit, like `shared_ptr<const T>` from `shared_ptr<T>`.
  // An empty source converts to an empty handle with the same empty behavior,
  // and a moved-from source is left with its own type's `raise` table.
  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_shared_proxy(const shared_proxy<D>& other) noexcept
      : base{other.target_, details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_shared_proxy(shared_proxy<D>&& other) noexcept
      : base{std::move(other.target_),
            details::upcast_vtable<F, D>(other.vtable_)} {
    other.vtable_ = other.empty_vtable;
  }

  // Upcasting converting constructors from a `const_shared_proxy` of a facade
  // that extends `F`, by copy or by move, under the same rules.
  template<Facade D>
  requires Extends<D, F>
  const_shared_proxy(const const_shared_proxy<D>& other) noexcept
      : base{other.target_, details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires Extends<D, F>
  const_shared_proxy(const_shared_proxy<D>&& other) noexcept
      : base{std::move(other.target_),
            details::upcast_vtable<F, D>(other.vtable_)} {
    other.vtable_ = other.empty_vtable;
  }

  // No need for a `using` because we do not declare a `call` here that shadows
  // the base's const-only `call`.

private:
  // Construct over `target` with `vtable`, or empty when `target` is null;
  // see `shared_proxy`'s.
  const_shared_proxy(std::shared_ptr<const void> target,
      const vtable_t* vtable) noexcept
      : base{std::move(target), vtable} {
    if (!this->target_) this->vtable_ = base::empty_vtable;
  }

  template<Facade G>
  friend class const_shared_proxy;
  template<Facade G>
  friend class const_weak_proxy;
  template<Facade G>
  friend class const_proxy_view;
  template<Facade G, access_mode A>
  friend class details::shared_base;
  template<Facade G, access_mode A>
  friend class details::weak_base;
};

// The library-provided binding so that a `const_shared_proxy` satisfies its
// own facade, and the ones that facade extends, where that is possible; see
// `details::handle_impl`.
//
// It dispatches only const methods, so the invariant holds exactly for
// all-const facades, as with `const_proxy_view`.
template<Facade F, Facade D>
requires ExtendsOrIs<D, F>
struct proxy_impl<F, const_shared_proxy<D>>: details::handle_impl<F> {};

// `weak_proxy` is the weak counterpart to `shared_proxy`, which observes the
// target, via a `std::weak_ptr<void>`, without owning it.
//
// It carries no dispatch at all. Regaining access always goes through
// `lock`, which returns a shared proxy (empty if every owner is gone), so
// there is no way to call through a target that might be dying.
template<Facade F>
class weak_proxy: public details::weak_base<F, access_mode::as_mutable> {
  using base = details::weak_base<F, access_mode::as_mutable>;

public:
  weak_proxy() = default;

  // Conversion constructor from a `shared_proxy` of `F`, or of a facade that
  // extends it (the upcast happens here, so `lock` is cheap).
  //
  // Intentionally implicit. A `const_shared_proxy` is not accepted, because
  // `lock` would reopen mutability. That handle is observed by a
  // `const_weak_proxy` instead.
  template<Facade D>
  requires ExtendsOrIs<D, F>
  weak_proxy(const shared_proxy<D>& p) noexcept
      : base{p.target_, details::upcast_vtable<F, D>(p.vtable_)} {}

  // Upcasting converting constructors from a `weak_proxy` of a facade that
  // extends `F`, by copy or by move, mirroring the `shared_proxy`'s.
  //
  // Intentionally implicit, like every handle upcast. An expired source
  // upcasts like a live one, still observing the same target, while expiration
  // stays `lock`'s business.
  //
  // A moved-from source keeps its table. Nothing reads the table except
  // through `target_`, and `lock` comes out empty once that is gone, so
  // there is nothing to clear.
  template<Facade D>
  requires Extends<D, F>
  weak_proxy(const weak_proxy<D>& other) noexcept
      : base{other.target_, details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires Extends<D, F>
  weak_proxy(weak_proxy<D>&& other) noexcept
      : base{std::move(other.target_),
            details::upcast_vtable<F, D>(other.vtable_)} {}

private:
  template<Facade G>
  friend class weak_proxy;
  template<Facade G>
  friend class const_weak_proxy;
};

// `const_weak_proxy` is the weak counterpart to `const_shared_proxy`, which
// observes the target, via a `std::weak_ptr<const void>`, without owning it.
//
// It is `weak_proxy` with constness in the type. It observes either shared
// flavor, or converts from a `weak_proxy`. Its `lock` returns a
// `const_shared_proxy`, so mutability never reopens through it.
template<Facade F>
class const_weak_proxy: public details::weak_base<F, access_mode::as_const> {
  using base = details::weak_base<F, access_mode::as_const>;

public:
  const_weak_proxy() = default;

  // Conversion constructors from a shared handle of either flavor, of `F` or
  // of a facade that extends it (the upcast happens here, so `lock` is
  // cheap).
  //
  // Intentionally implicit.
  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_weak_proxy(const const_shared_proxy<D>& p) noexcept
      : base{p.target_, details::upcast_vtable<F, D>(p.vtable_)} {}

  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_weak_proxy(const shared_proxy<D>& p) noexcept
      : base{p.target_, details::upcast_vtable<F, D>(p.vtable_)} {}

  // Converting constructors from a `weak_proxy` of `F`, or of a facade that
  // extends it, dropping mutability, and upcasting constructors from a
  // `const_weak_proxy` of a facade that extends `F`, each by copy or by move.
  //
  // Intentionally implicit, like every handle upcast. An expired source
  // converts like a live one, still observing the same target, while
  // expiration stays `lock`'s business.
  //
  // A moved-from source keeps its table. Nothing reads the table except
  // through `target_`, and `lock` comes out empty once that is gone, so
  // there is nothing to clear.
  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_weak_proxy(const weak_proxy<D>& other) noexcept
      : base{other.target_, details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires ExtendsOrIs<D, F>
  const_weak_proxy(weak_proxy<D>&& other) noexcept
      : base{std::move(other.target_),
            details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires Extends<D, F>
  const_weak_proxy(const const_weak_proxy<D>& other) noexcept
      : base{other.target_, details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires Extends<D, F>
  const_weak_proxy(const_weak_proxy<D>&& other) noexcept
      : base{std::move(other.target_),
            details::upcast_vtable<F, D>(other.vtable_)} {}

private:
  template<Facade G>
  friend class const_weak_proxy;
};

// Make a shared proxy of facade `F` holding a `T` constructed in place from
// `args`, with target and control block in one allocation
// (`std::make_shared`).
template<Facade F, typename T, typename... Args>
requires Proxiable<T, F>
[[nodiscard]] shared_proxy<F> make_shared_proxy(Args&&... args) {
  return shared_proxy<F>{std::make_shared<T>(std::forward<Args>(args)...)};
}

// Make a const shared proxy of facade `F` holding a `T` constructed in place
// from `args`; the const counterpart of `make_shared_proxy`.
template<Facade F, typename T, typename... Args>
requires Proxiable<T, F>
[[nodiscard]] const_shared_proxy<F> make_const_shared_proxy(Args&&... args) {
  return const_shared_proxy<F>{
      std::make_shared<T>(std::forward<Args>(args)...)};
}

#pragma endregion

} // namespace prox

#pragma region Exports

// Call-site vocabulary, exported to `corvid::meta`; see proxy_common.h.
using prox::const_shared_proxy;
using prox::const_weak_proxy;
using prox::make_const_shared_proxy;
using prox::make_shared_proxy;
using prox::shared_proxy;
using prox::weak_proxy;

#pragma endregion

}} // namespace corvid::meta
