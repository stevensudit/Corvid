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
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../padding.h"
#include "proxy_view.h"

// The owning erased handle, `proxy`, along with `make_proxy`.
//
// See "proxy.md" for the design.

namespace corvid { inline namespace meta {
namespace prox {

#pragma region proxy

// `proxy` is the owning erased handle over any `Proxiable` target, akin to
// Rust's `Box<dyn Trait>` or ngcpp's `proxy`.
//
// Move-only. Storage follows `Policy` (see `invocable_policy`). By default,
// eligible targets are stored inline, and anything else lives in a
// unique-owned heap allocation. The owning dispatch table carries destroy
// and relocate slots alongside the facade methods, so destruction and moves
// work without knowing the target type.
//
// Proxies of different policies interconvert as rvalues, in the same move
// that upcasts. The source's policy never matters. This proxy accommodates
// whatever target actually arrives, changing its storage mode when the
// policy demands it (boxing an inline arrival onto the heap under
// `heap_only`, un-boxing a heap arrival into the buffer under `inline_only`).
// Only the conversions that might change the mode can throw; everything
// else is `noexcept`, including every same-policy move.
//
// That holds even for a target whose move constructor can throw, because
// inline eligibility requires a nothrow move (see `fits_inline`). Such a
// target only ever lives on the heap, where a move passes the pointer, and an
// `inline_only` proxy refuses it at compile time.
//
// The proxy is deep-const, so only const-qualified facade methods dispatch
// through a const proxy. Being move-only, it cannot be copied out of that
// constness the way a view can: no const reference to a proxy can ever be
// moved from.
//
// A default-constructed, moved-from, or reset proxy is empty. It is
// destructible, assignable, and testable via `operator bool`, and calling
// through it runs the policy's `on_empty` behavior, taken per method as a
// floor (see `invocable_policy::empty`); strict enforcement rejects a facade
// when it has any methods that cannot take the floor exactly.
//
// A non-empty lvalue to a proxy converts implicitly to a `proxy_view` (when
// not const) or `const_proxy_view` of its own facade, or of any facade it
// extends. The view re-points at the stored target, and the proxy must outlive
// it. See the view constructors for details.
//
// An rvalue proxy also upcasts, converting implicitly to a proxy of any facade
// its own extends (like Rust: `Box<dyn Derived>` to `Box<dyn Base>`). The
// conversion is a move. The owning tables remember the facade the target was
// born as, so an upcast is undoable: `try_downcast` recovers a proxy of any
// facade in the birth ancestry.
//
// When the facade defines a nested `api`, the proxy inherits it, so the
// member-call sugar forwarders dispatch alongside `call`.
template<Facade F, invocable_policy Policy>
class proxy: public details::api_base_t<F> {
  using vtbuild_t = details::vtbuild_t<F>;
  using owning_vtable_t = vtbuild_t::owning_vtable_t;

  // The buffer may only grow from its defaults, so any target eligible for
  // the default buffer stays eligible for every buffer. A `heap_only` proxy
  // keeps only the heap pointer in its storage area, so the knobs are not
  // consulted (and `with_size` and `with_alignment` refuse to set them).
  static_assert(
      !Policy.admits_inline() ||
          (Policy.inline_size >= invocable_policy{}.inline_size &&
              Policy.inline_align >= invocable_policy{}.inline_align),
      "inline_size and inline_align may not shrink below their defaults");
  static_assert(std::has_single_bit(Policy.inline_align),
      "inline_align must be a power of two");
  static_assert(
      !Policy.admits_inline() ||
          Policy.inline_size ==
              padded_size(Policy.inline_size, Policy.inline_align),
      "inline_size that is not a multiple of inline_align would waste the "
      "difference as padding; pass it through padded_size");
  // Strict enforcement detonates per method (see `empty_fit_check`), each
  // error naming its method, so this assert carries no message of its own. A
  // failed detonation is no longer a constant expression, and the error
  // reported here is the trailing one those detonations leave behind.
  static_assert(vtbuild_t::template empty_fits_policy<Policy>());

public:
  using facade_t = F;

  // Inline storage capacity in bytes.
  //
  // For `heap_only` policy, this is always 0, as the pointer it keeps instead
  // of the buffer does not count as inline capacity.
  //
  // See `invocable_policy` for the inline-eligibility conditions.
  static constexpr size_t inline_size =
      Policy.admits_inline() ? Policy.inline_size : 0;

  proxy() = default;

  proxy(const proxy&) = delete;
  proxy& operator=(const proxy&) = delete;

  // Constructor for an owning proxy holding a `T` built in place from `args`.
  //
  // Usually spelled through `make_proxy`.
  //
  // This is the moment the policy meets the concrete type: the storage mode
  // is chosen here and baked into the owning table, and an `inline_only`
  // policy rejects an ineligible target here.
  template<typename T, typename... Args>
  requires(Proxiable<T, F> && std::constructible_from<T, Args...>)
  explicit proxy(std::in_place_type_t<T>, Args&&... args)
      : vtable_{&details::owning_vtable_for<F, F, T,
            details::storage_mode_of<T>(Policy)>} {
    static_assert(
        Policy.admits_heap() || details::is_inline_eligible<T>(Policy),
        "the target is not eligible for an inline_only proxy's inline buffer");
    if constexpr (details::can_store_inline<T>(Policy))
      ::new (storage_area_.buf) T(std::forward<Args>(args)...);
    else
      storage_area_.ptr = new T(std::forward<Args>(args)...);
  }

  // Constructor for an owning proxy adopting a heap target already
  // owned by a `std::unique_ptr`. (The signature ensures that it is
  // specialized on the default deleter, since proxy eventually destroys the
  // target using `delete`.)
  //
  // Usually spelled through `make_proxy`.
  //
  // The allocation is adopted as-is: the proxy takes the heap path even for
  // a target the policy could store inline, so nothing is copied or moved
  // and the target's address stays stable.
  //
  // The exception is `inline_only`, which cannot hold a heap target: it
  // un-boxes the target into its buffer (the type is concrete here, so the fit
  // is checked at compile time) and frees the allocation.
  //
  // A null pointer yields an empty proxy. Ownership arrives from a raw pointer
  // only by way of a `unique_ptr`; there is deliberately no raw-pointer
  // constructor.
  template<typename T>
  requires Proxiable<T, F>
  explicit proxy(std::unique_ptr<T> target) noexcept {
    if (!target) return;
    if constexpr (!Policy.admits_heap()) {
      static_assert(details::is_inline_eligible<T>(Policy),
          "the target is not eligible for an inline_only proxy's buffer.");
      ::new (storage_area_.buf) T(std::move(*target));
      vtable_ = &details::owning_vtable_for<F, F, T, storage_mode::inlined>;
    } else {
      storage_area_.ptr = target.release();
      vtable_ = &details::owning_vtable_for<F, F, T, storage_mode::dynamic>;
    }
  }

  // Move constructor.
  //
  // Inline targets relocate through the table's move slot, while heap targets
  // move by pointer steal.
  proxy(proxy&& other) noexcept { do_adopt(other); }

  // Converting move constructor from an owning proxy of any facade that
  // extends `F` (an upcast), of any other policy, or both at once.
  //
  // Intentionally implicit, like the view upcasts, but consuming. The target
  // moves into this proxy and the source is left empty, so an upcast is
  // one-way as a conversion (`try_downcast` is the way back).
  //
  // An empty source yields an empty proxy. The source's policy never
  // constrains the conversion. Storage is accommodated per target at runtime
  // (see `do_adopt`), and a conversion that might have to change the storage
  // mode is exactly as `noexcept` as that allows.
  //
  // A throw leaves the source intact and this proxy empty, because
  // `do_adopt` settles the route before anything moves.
  template<Facade D, invocable_policy P>
  requires(std::same_as<D, F> || Extends<D, F>)
  proxy(proxy<D, P>&& other) noexcept(!details::adopt_may_throw(Policy, P)) {
    do_adopt(other);
  }

  proxy& operator=(proxy&& other) noexcept {
    if (this != &other) {
      do_reset();
      do_adopt(other);
    }
    return *this;
  }

  // Move assignment from a proxy of a facade that is `F` or extends it, of
  // any policy, under the same transplant rules as the converting move
  // constructor.
  //
  // A refusal is detected by a pre-flight `can_adopt`, throwing before either
  // side is touched. A boxing allocation can still throw, after this proxy
  // has released its own target, leaving it empty and the source intact.
  template<Facade D, invocable_policy P>
  requires((std::same_as<D, F> || Extends<D, F>) &&
           !(std::same_as<D, F> && P == Policy))
  proxy& operator=(proxy<D, P>&& other) noexcept(
      !details::adopt_may_throw(Policy, P)) {
    if constexpr (details::adopt_may_throw(Policy, P)) {
      if (!can_adopt(other))
        throw std::length_error{
            "the target cannot be stored in an inline_only proxy's buffer"};
    }
    do_reset();
    do_adopt(other);
    return *this;
  }

  ~proxy() { do_reset(); }

  void reset() noexcept { do_reset(); }

  proxy& operator=(std::nullptr_t) noexcept {
    do_reset();
    return *this;
  }

  // Call the facade method named `Key`, forwarding `args` through the erased
  // signature.
  //
  // The call is `noexcept` when the method is and the argument conversions
  // cannot throw (they are the caller's, as with the `api` forwarders).
  //
  // The const overload is constrained to const-qualified methods, enforcing
  // deep const at overload resolution so the rejection is visible to
  // `requires` probes as well. It is not `[[nodiscard]]`, because
  // discardability belongs to the facade method rather than the dispatcher
  // (which is the `std::invoke` precedent).
  template<fixed_string Key, typename... Args>
  decltype(auto)
  call(Args&&... args) noexcept(vtbuild_t::template is_noexcept<Key,
      access_mode::as_mutable, Args...>()) {
    return details::dispatch<F, access_mode::as_mutable, Key>(
        vtable_->vt.thunks, target(), std::forward<Args>(args)...);
  }

  template<fixed_string Key, typename... Args>
  requires(details::vtbuild_t<F>::template is_const<Key>())
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  decltype(auto) call(Args&&... args) const noexcept(
      vtbuild_t::template is_noexcept<Key, access_mode::as_const, Args...>()) {
    return details::dispatch<F, access_mode::as_const, Key>(vtable_->vt.thunks,
        target(), std::forward<Args>(args)...);
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return (vtable_ != empty_vtable);
  }

  // Whether `clone` would produce a faithful copy, meaning that the target is
  // copy-constructible, or there is no target at all (an empty proxy clones to
  // an empty proxy).
  //
  // The answer is a runtime property of the erased target, not of the proxy
  // type, so a container of proxies can mix cloneable and uncloneable targets.
  [[nodiscard]] bool can_clone() const noexcept {
    return (!*this || vtable_->copy);
  }

  // Whether this `proxy` type can accommodate `source`'s current target, so
  // that converting (or assigning) from it will not throw `std::length_error`.
  //
  // This is the up-front check that advertises, and lets a caller sidestep,
  // the one conversion that can fail for reasons other than memory
  // availability.
  //
  // Static, because the answer is a property of this proxy TYPE against the
  // source's runtime target. It works before any destination instance exists,
  // and is equally callable through one.
  //
  // Only an `inline_only` destination can ever answer no (since everything
  // else has the heap to fall back on), and an empty source is always
  // adoptable, to empty. It does not promise the allocation a mode-changing
  // adoption may need.
  template<Facade D, invocable_policy P>
  requires(std::same_as<D, F> || Extends<D, F>)
  [[nodiscard]] static bool can_adopt(const proxy<D, P>& source) noexcept {
    if constexpr (Policy.admits_heap()) {
      return true;
    } else {
      if (!source) return true;
      const auto* vt = details::upcast_owning_vtable<F, D>(source.vtable_);
      return (adoption_for<P>(vt) != adoption::refuse);
    }
  }

  // Clone the `proxy`, creating a new instance with the same policy, owning a
  // copy of the target made through the table's copy slot.
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
    if (!vtable_->copy) return result;
    if (vtable_->relocate)
      (void)vtable_->copy(target(), result.storage_area_.buf);
    else
      result.storage_area_.ptr = vtable_->copy(target(), nullptr);
    result.vtable_ = vtable_;
    return result;
  }

  // Extract the target out into a `std::unique_ptr<T>`, leaving the proxy
  // empty.
  //
  // The inverse of the adopting constructor, and the only way ownership leaves
  // a proxy other than destruction, since a raw pointer is never exposed.
  //
  // `T` must be the target's exact type, verified at runtime through the
  // table's type tag. On a mismatch, or an empty proxy, the result is null
  // and the proxy is untouched.
  //
  // A heap-stored target hands over its allocation as-is, while an inline
  // target moves onto the heap first (the one case with target activity, and
  // the one case that can throw, again leaving the proxy untouched).
  template<typename T>
  [[nodiscard]] std::unique_ptr<T> extract() {
    if (vtable_->vt.type_tag != &details::type_tag_v<T>) return nullptr;
    if constexpr (Policy.admits_heap()) {
      if (!vtable_->relocate) {
        auto* ptr = static_cast<T*>(storage_area_.ptr);
        vtable_ = empty_vtable;
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDelete): see target
        return std::unique_ptr<T>{ptr};
      }
    }
    if constexpr (Policy.admits_inline() && std::is_move_constructible_v<T>) {
      std::unique_ptr<T> result{new T(std::move(*static_cast<T*>(target())))};
      do_reset();
      return result;
    } else {
      // Unreachable: an immovable target is never stored inline, and a
      // `heap_only` proxy stores nothing inline.
      return nullptr;
    }
  }

  // Try to downcast this instance to recover a proxy of `D`, which is a facade
  // extending `F`, from a proxy that may have been upcast away from it.
  //
  // The owning table remembers the facade the target was born as, meaning
  // the facade it was constructed under, not the concrete type's full
  // conformance. This means that a target made through `make_proxy<marshal,
  // texas_ranger>` downcasts to `marshal` but never to `ranger`.
  //
  // Its birth ancestry, the born facade plus every facade that one
  // extends, is searched at runtime for `D` itself. A target born as a
  // facade that extends `D` therefore matches, and through a diamond, the
  // common base can sidecast to either sibling.
  //
  // On success, the target moves into the result (whose table carries the
  // same birth, so further casts in either direction still work) and the
  // source is left empty. On failure, including an empty source, the
  // result is empty and the source is untouched. Consuming only on success
  // is why this is spelled as a method on an rvalue rather than a
  // conversion.
  template<Facade D>
  requires Extends<D, F>
  [[nodiscard]] proxy<D, Policy> try_downcast() && noexcept {
    proxy<D, Policy> result;
    if (!*this) return result;
    const auto* table =
        details::find_ancestor(*vtable_->ancestry, &details::facade_tag_v<D>);
    if (!table) return result;
    result.vtable_ =
        static_cast<const details::vtbuild_t<D>::owning_vtable_t*>(table);
    if (vtable_->relocate)
      vtable_->relocate(storage_area_.buf, result.storage_area_.buf);
    else
      // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign): see target
      result.storage_area_.ptr = storage_area_.ptr;
    vtable_ = empty_vtable;
    return result;
  }

private:
  // A `heap_only` proxy shrinks the buffer to the pointer it overlays, so the
  // whole handle is two words, like a view.
  static constexpr size_t buf_size = Policy.buffer_size();
  static constexpr size_t buf_align = Policy.buffer_align();

  using storage_area_t = details::storage_area<buf_size, buf_align>;

  // The target address, inline or heap, or the buffer's address when empty
  // (whose contents the empty thunks never read).
  //
  // The active union member, and emptiness itself, are keyed by the table
  // (`relocate` null means heap while the empty table's is `empty_relocate`).
  // This is an invariant every write site maintains but the static analyzer
  // cannot see, so the union reads here and in `do_adopt`, `try_downcast`,
  // `extract`, and the `shared_proxy` adoption suppress its
  // uninitialized-value and use-after-release checks.
  [[nodiscard]] void* target() noexcept {
    // NOLINTBEGIN(clang-analyzer-core.uninitialized.UndefReturn)
    // NOLINTBEGIN(clang-analyzer-cplusplus.NewDelete)
    return vtable_->relocate
               ? static_cast<void*>(storage_area_.buf)
               : storage_area_.ptr;
    // NOLINTEND(clang-analyzer-cplusplus.NewDelete)
    // NOLINTEND(clang-analyzer-core.uninitialized.UndefReturn)
  }
  [[nodiscard]] const void* target() const noexcept {
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.UndefReturn)
    return vtable_->relocate
               ? static_cast<const void*>(storage_area_.buf)
               : storage_area_.ptr;
  }

  void do_reset() noexcept {
    if (!*this) return;
    vtable_->destroy(target());
    vtable_ = empty_vtable;
  }

  // `do_adopt` to take over `other`'s target, upcasting its table when
  // `other`'s facade extends `F`, and leaving `other` empty on its own type's
  // empty table, so no empty behavior travels with a target.
  //
  // Assumes `*this` holds no target (freshly constructed or just reset). Note
  // that we don't need to clear `buf` or `ptr` on `other.storage_area_`
  // because `other.vtable_` defines whether it's empty.
  //
  // The source's policy does not matter here because this proxy accommodates
  // whatever target actually arrives at runtime, on the route `adoption_for`
  // picks. A mode change (boxing or un-boxing) switches to the table's
  // other-mode sibling, which carries its own mode's birth ancestry.
  //
  // Only the mode-changing routes can throw (the boxing allocation, or
  // `std::length_error` on a refusal, when an erased target cannot be stored
  // inline and the policy forbids the heap), and a throw happens before
  // anything moves, leaving `other` intact and `*this` empty. The routes
  // that move the target (relocating, un-boxing, and the move into a fresh
  // box) never throw doing so, since only a nothrow-move target is ever
  // eligible for a buffer. The throw is pruned rather than left dynamically
  // unreachable, so that a `noexcept` adoption contains no throw at all.
  template<Facade D, invocable_policy P>
  void
  do_adopt(proxy<D, P>& other) noexcept(!details::adopt_may_throw(Policy, P)) {
    if (!other) return;
    const auto* vt = details::upcast_owning_vtable<F, D>(other.vtable_);
    switch (adoption_for<P>(vt)) {
    case adoption::relocate:
      vt->relocate(other.storage_area_.buf, storage_area_.buf);
      vtable_ = vt;
      break;
    case adoption::box:
      storage_area_.ptr = vt->to_heap(other.storage_area_.buf);
      vtable_ = vt->heap_table;
      break;
    case adoption::hand_over:
      // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign): see target
      storage_area_.ptr = other.storage_area_.ptr;
      vtable_ = vt;
      break;
    case adoption::unbox:
      // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage): see target
      vt->to_inline(other.storage_area_.ptr, storage_area_.buf);
      vtable_ = vt->inline_table;
      break;
    case adoption::refuse:
      if constexpr (details::adopt_may_throw(Policy, P))
        throw std::length_error{
            "the target cannot be stored in an inline_only proxy's buffer"};
      assert(false); // unreachable: adopt_may_throw ruled refusal out
      break;
    }
    other.vtable_ = other.empty_vtable;
  }

  // `adoption_for` is the route the erased target behind `vt` takes into this
  // proxy, arriving from a proxy of policy `P`; see `adoption_of`.
  //
  // The table is the arrival's witness: `relocate` marks an inline target,
  // nothrow-move by eligibility, and `to_inline` a heap target that could live
  // inline; `size` and `align` are its footprint. When every inline target
  // the source policy admits is guaranteed to fit this buffer, which covers
  // every same-policy move, an inline arrival skips the runtime fit check.
  template<invocable_policy P>
  static adoption adoption_for(const owning_vtable_t* vt) noexcept {
    if (vt->relocate) {
      if constexpr (details::is_inline_fit_guaranteed(Policy, P))
        return adoption::relocate;
      return details::adoption_of(Policy, storage_mode::inlined, vt->size,
          vt->align, true);
    }
    return details::adoption_of(Policy, storage_mode::dynamic, vt->size,
        vt->align, static_cast<bool>(vt->to_inline));
  }

  // Table of an empty proxy of this type; see `empty_owning_vtable_for`.
  static constexpr const owning_vtable_t* empty_vtable =
      &details::empty_owning_vtable_for<F, Policy.empty>;

#pragma region Data members

  const owning_vtable_t* vtable_ = empty_vtable;

  // Storage area deliberately has no initializer because emptiness and the
  // active member are keyed by `vtable_` (see `target`), and zeroing the
  // buffer on every construction would be pure waste.
  storage_area_t storage_area_;

#pragma endregion

  template<Facade G, invocable_policy P>
  friend class proxy;
  template<Facade G>
  friend class proxy_view;
  template<Facade G>
  friend class const_proxy_view;
  template<Facade G>
  friend class shared_proxy;
};

// `proxy_impl` is the library-provided binding so that an owning `proxy`
// satisfies its own facade and every facade that facade extends, like the
// view.
//
// Calls forward through the proxy, with conditional `noexcept`, through a
// single deduced-handle binding that serves const and mutable proxies alike;
// deep const is enforced by the proxy's own `call` overloads.
template<Facade F, Facade D, invocable_policy P>
requires(std::same_as<D, F> || Extends<D, F>)
struct proxy_impl<F, proxy<D, P>> {
  // Qualified forwarding, as with the view bindings; see `qualified_key`. The
  // deduced handle parameter serves const and mutable proxies alike.
  template<fixed_string Key, typename Handle, typename... Args>
  static decltype(auto)
  on(method_key<Key>, Handle& p, Args&&... args) noexcept(
      noexcept(p.template call<details::qualified_key<F, Key>()>(
          std::forward<Args>(args)...))) {
    return p.template call<details::qualified_key<F, Key>()>(
        std::forward<Args>(args)...);
  }
};

// Make an owning `proxy` of facade `F` holding a `T` constructed in place from
// `args`.
//
// To move an existing object in, pass it as the rvalue constructor argument,
// as in `make_proxy<F, T>(std::move(obj))`. A non-default storage policy is
// the optional third argument: `make_proxy<F, T, invocable_policy{...}>(...)`.
template<Facade F, typename T, invocable_policy Policy = invocable_policy{},
    typename... Args>
requires Proxiable<T, F>
[[nodiscard]] proxy<F, Policy> make_proxy(Args&&... args) {
  return proxy<F, Policy>{std::in_place_type<T>, std::forward<Args>(args)...};
}

// `make_proxy` to make an owning `proxy` of facade `F`, adopting a heap target
// already owned by a `std::unique_ptr`; see the adopting constructor.
template<Facade F, invocable_policy Policy = invocable_policy{}, typename T>
requires Proxiable<T, F>
[[nodiscard]] proxy<F, Policy> make_proxy(std::unique_ptr<T> target) noexcept {
  return proxy<F, Policy>{std::move(target)};
}

#pragma endregion

} // namespace prox

#pragma region Exports

// Call-site vocabulary, exported to `corvid::meta`; see proxy_common.h.
using prox::make_proxy;
using prox::proxy;

#pragma endregion

}} // namespace corvid::meta
