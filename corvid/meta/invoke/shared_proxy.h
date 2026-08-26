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
#include <functional>
#include <memory>
#include <utility>

#include "proxy.h"

// The shared-owning erased handle, `shared_proxy`, its observer
// `weak_proxy`, and `make_shared_proxy`.
//
// See "proxy.md" for the design.

namespace corvid { inline namespace meta {
namespace prox {

#pragma region Shared ownership

// `shared_proxy` is the shared-owning erased handle, like Rust's `Rc<dyn
// Trait>`, backed by a `std::shared_ptr<void>`.
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
// `operator bool`, and a call through it raises `std::bad_function_call`,
// or, when the handle was adopted from an empty `proxy`, runs that proxy's
// `on_empty` behavior. Handles upcast implicitly, by copy or by move, to any
// facade theirs extends.
template<Facade F>
class shared_proxy: public details::api_base_t<F> {
  using vtbuild_t = details::vtbuild_t<F>;
  using vtable_t = vtbuild_t::vtable_t;

public:
  using facade_t = F;

  shared_proxy() = default;

  shared_proxy(const shared_proxy&) = default;
  shared_proxy& operator=(const shared_proxy&) = default;

  shared_proxy(shared_proxy&& other) noexcept
      : target_{std::move(other.target_)},
        vtable_{std::exchange(other.vtable_, empty_vtable)} {}

  shared_proxy& operator=(shared_proxy&& other) noexcept {
    if (this != &other) {
      target_ = std::move(other.target_);
      vtable_ = std::exchange(other.vtable_, empty_vtable);
    }
    return *this;
  }

  ~shared_proxy() = default;

  // Adopting constructor from shared ownership of a concrete target; a null
  // pointer yields an empty handle.
  //
  // Usually spelled through `make_shared_proxy`; there is deliberately no
  // raw-pointer constructor.
  template<typename T>
  requires Proxiable<T, F>
  explicit shared_proxy(std::shared_ptr<T> target) noexcept
      : target_{std::move(target)} {
    if (target_) vtable_ = &details::vtable_for<F, T>;
  }

  // Adopting constructor from unique ownership, which becomes shared (this
  // allocates the control block, so unlike the shared flavor it can throw).
  template<typename T>
  requires Proxiable<T, F>
  explicit shared_proxy(std::unique_ptr<T> target)
      : shared_proxy{std::shared_ptr<T>{std::move(target)}} {}

  // Adopting constructor from an owning `proxy` of `F`, or of a facade that
  // extends it.
  //
  // The unique ownership becomes shared (Rust: `Box<dyn T>` into `Rc<dyn T>`),
  // consuming the source.
  //
  // A heap-stored target is adopted as-is, the owning table's destroy slot
  // becoming the control block's deleter, so only the control block allocates;
  // an inline target moves onto the heap first. On a throw from the
  // control-block allocation the target is destroyed rather than leaked, and
  // the source is left empty (the same contract as `std::shared_ptr`'s
  // constructor from a `unique_ptr`); a throw from the boxing leaves the
  // source intact.
  //
  // The reverse conversion deliberately does not exist because unique
  // ownership cannot be recovered from a shared target, even at a use count of
  // one, without racing the other owners, which is also why `std::shared_ptr`
  // has no `release`.
  template<Facade D, invocable_policy P>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit shared_proxy(proxy<D, P>&& source) {
    const auto* src = source.vtable_;
    const auto* ovt = details::upcast_owning_vtable<F, D>(src);
    vtable_ = &ovt->vt;
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
    target_ = std::shared_ptr<void>{ptr, destroy};
  }

  // Upcasting converting constructors from a `shared_proxy` of a facade that
  // extends `F`, sharing (copy) or transferring (move) ownership.
  // Intentionally implicit, like every handle upcast.
  //
  // An empty source upcasts to an empty handle with the same empty behavior;
  // a moved-from source is left on its own type's `raise` table.
  template<Facade D>
  requires Extends<D, F>
  explicit(false) shared_proxy(const shared_proxy<D>& other) noexcept
      : target_{other.target_},
        vtable_{details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires Extends<D, F>
  explicit(false) shared_proxy(shared_proxy<D>&& other) noexcept
      : target_{std::move(other.target_)},
        vtable_{details::upcast_vtable<F, D>(other.vtable_)} {
    other.vtable_ = other.empty_vtable;
  }

  // Call the facade method named `Key`, forwarding `args` through the erased
  // signature; the same dispatch as the other handles, deep const included.
  template<fixed_string Key, typename... Args>
  decltype(auto)
  call(Args&&... args) noexcept(vtbuild_t::template is_noexcept<Key,
      access_mode::as_mutable, Args...>()) {
    return details::dispatch<F, access_mode::as_mutable, Key>(vtable_->thunks,
        target(), std::forward<Args>(args)...);
  }

  template<fixed_string Key, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<Key>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  decltype(auto) call(Args&&... args) const noexcept(
      vtbuild_t::template is_noexcept<Key, access_mode::as_const, Args...>()) {
    return details::dispatch<F, access_mode::as_const, Key>(vtable_->thunks,
        target(), std::forward<Args>(args)...);
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(target_);
  }

  // Try to downcast this instance to recover a `shared_proxy` of `D`, which is
  // a facade extending `F`, from a handle that may have been upcast away from
  // it.
  //
  // The table remembers the facade the target was born as, exactly as with
  // the owning proxy (see `proxy::try_downcast`), including a birth adopted
  // from a consumed `proxy`.
  //
  // Because shared ownership is copyable, the lvalue flavor shares: on success
  // the result is another owner of the one target and the source keeps its own
  // share. The rvalue flavor transfers instead, consuming the source only on
  // success. On failure, including an empty source, the result is empty and
  // the source is untouched.
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] shared_proxy<D> try_downcast() const& noexcept {
    const auto* table = details::find_downcast_table<D, F>(vtable_);
    if (!table) return {};
    return shared_proxy<D>{target_, table};
  }
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] shared_proxy<D> try_downcast() && noexcept {
    const auto* table = details::find_downcast_table<D, F>(vtable_);
    if (!table) return {};
    shared_proxy<D> result{std::move(target_), table};
    vtable_ = empty_vtable;
    return result;
  }

private:
  // The target address, or null when empty (which the empty thunks never
  // read).
  [[nodiscard]] void* target() noexcept { return target_.get(); }
  [[nodiscard]] const void* target() const noexcept { return target_.get(); }

  shared_proxy(std::shared_ptr<void> target, const vtable_t* vtable) noexcept
      : target_{std::move(target)}, vtable_{target_ ? vtable : empty_vtable} {}

  // Table of a handle built empty or emptied by a move; see
  // `empty_vtable_for`.
  static constexpr const vtable_t* empty_vtable =
      &details::empty_vtable_for<F, on_empty::raise>;

#pragma region Data members

  std::shared_ptr<void> target_;
  const vtable_t* vtable_ = empty_vtable;

#pragma endregion

  template<Facade G>
  friend class shared_proxy;
  template<Facade G>
  friend class weak_proxy;
  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
};

// `proxy_impl` is the library-provided binding so that a shared proxy
// satisfies its own facade and every facade that facade extends, like the
// other handles; see the owning proxy's binding.
template<Facade F, Facade D>
requires(std::same_as<D, F> || Extends<D, F>)
struct proxy_impl<F, shared_proxy<D>> {
  // The deduced handle parameter serves const and mutable handles alike, as
  // with the other self-conformance bindings.
  template<fixed_string Key, typename Handle, typename... Args>
  static decltype(auto)
  on(method_key<Key>, Handle& p, Args&&... args) noexcept(
      noexcept(p.template call<details::qualified_key<F, Key>()>(
          std::forward<Args>(args)...))) {
    return p.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
};

// `weak_proxy` is the weak counterpart to `shared_proxy`, which observes the
// target without owning it, via a `std::weak_ptr<void>`.
//
// It carries no dispatch at all; regaining access always goes through
// `lock`, which returns a shared proxy (empty if every owner is gone), so
// there is no way to call through a target that might be dying.
template<Facade F>
class weak_proxy {
  using vtable_t = details::vtbuild_t<F>::vtable_t;

public:
  weak_proxy() = default;

  // Conversion constructor from a `shared_proxy` of `F`, or of a facade that
  // extends it (the upcast happens here, so `lock` is cheap).
  //
  // Intentionally implicit.
  template<Facade D>
  requires(std::same_as<D, F> || Extends<D, F>)
  explicit(false) weak_proxy(const shared_proxy<D>& p) noexcept
      : target_{p.target_}, vtable_{details::upcast_vtable<F, D>(p.vtable_)} {}

  // Upcasting converting constructors from a `weak_proxy` of a facade that
  // extends `F`, by copy or by move, mirroring the `shared_proxy`'s.
  //
  // Intentionally implicit, like every handle upcast. An expired source
  // upcasts like a live one, still observing the same target; expiry stays
  // `lock`'s business.
  template<Facade D>
  requires Extends<D, F>
  explicit(false) weak_proxy(const weak_proxy<D>& other) noexcept
      : target_{other.target_},
        vtable_{details::upcast_vtable<F, D>(other.vtable_)} {}

  template<Facade D>
  requires Extends<D, F>
  explicit(false) weak_proxy(weak_proxy<D>&& other) noexcept
      : target_{std::move(other.target_)},
        vtable_{details::upcast_vtable<F, D>(other.vtable_)} {}

  // Whether the target is already gone.
  //
  // As with `std::weak_ptr`, a false answer is stale the moment it is read;
  // `lock` is the reliable gate.
  [[nodiscard]] bool expired() const noexcept { return target_.expired(); }

  // Lock to regain shared ownership by creating a `shared_proxy` over the
  // target; or an empty one when every owner is gone.
  [[nodiscard]] shared_proxy<F> lock() const noexcept {
    return shared_proxy<F>{target_.lock(), vtable_};
  }

#pragma region Data members
private:
  std::weak_ptr<void> target_;
  const vtable_t* vtable_ = &details::empty_vtable_for<F, on_empty::raise>;

#pragma endregion

  template<Facade G>
  friend class weak_proxy;
};

// Make a shared proxy of facade `F` holding a `T` constructed in place from
// `args`, with target and control block in one allocation
// (`std::make_shared`).
template<Facade F, typename T, typename... Args>
requires Proxiable<T, F>
[[nodiscard]] shared_proxy<F> make_shared_proxy(Args&&... args) {
  return shared_proxy<F>{std::make_shared<T>(std::forward<Args>(args)...)};
}

#pragma endregion

} // namespace prox

#pragma region Exports

// Call-site vocabulary, exported to `corvid::meta`; see proxy_common.h.
using prox::make_shared_proxy;
using prox::shared_proxy;
using prox::weak_proxy;

#pragma endregion

}} // namespace corvid::meta
