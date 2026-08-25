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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
#include <bit>
#include <cassert>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "concepts.h"
#include "invocable_policy.h"
#include "padding.h"
#include "traits.h"

namespace corvid { inline namespace meta {

//  `flexi_function<Policy, Sig>` is a wrapper for an invocable target.
//
//  It is a move-only type-erased callable, like `std::move_only_function` or
//  `stdext::inplace_function`, whose storage and empty-call behavior follow a
//  configurable `invocable_policy`.

#pragma region Traits

// Fwd.
template<invocable_policy Policy, class Sig,
    class FunctionT = signature_function_t<Sig>>
class flexi_function;

// Trait to determine whether `T` is a `flexi_function` (of any policy).
template<typename T>
constexpr inline bool is_flexi_function_v = false;

template<invocable_policy Policy, class Sig, class FunctionT>
constexpr inline bool
    is_flexi_function_v<flexi_function<Policy, Sig, FunctionT>> = true;

#pragma endregion
#pragma region fn_details

namespace fn_details {

// `flexi_thunks` are the type-erased thunks for one signature, shared by every
// `flexi_function` of that signature, regardless of policy.
//
// This is what lets siblings of different policies transplant a stored
// callable between them.
//
// A stored callable lives either inline in the wrapper's buffer or on the
// heap, owned by the pointer kept in that buffer. The `SourceAlloc` parameter
// in each thunk selects how the `storage` is interpreted.
//
// The second parameter mirrors `flexi_function`'s third: it is the
// pattern-matching hook, not a choice.
template<class Sig, class FunctionT = signature_function_t<Sig>>
struct flexi_thunks;

template<class Sig, class ResultT, class... Args>
struct flexi_thunks<Sig, ResultT(Args...)> {
  using traits = signature_traits<Sig>;

  // Whether the signature is `noexcept`, which makes the invoke thunks, and
  // the wrapper's call operator, `noexcept`.
  static constexpr bool noexcept_call =
      (traits::noexcept_specifier == noexcept_spec::present);

  // `F` with the signature's cv-qualifier applied.
  template<class F>
  using cv_qualified_target_t = std::conditional_t<
      (traits::const_qualifier == const_qual::present), const F, F>;

  // `F` with the signature's qualifiers applied, which is how the stored
  // target is invoked: `F cv&`, or `F cv&&` under an `&&` signature.
  //
  // As with `std::move_only_function`, an unqualified and an `&`-qualified
  // signature both invoke the target as an lvalue; the difference between them
  // is entirely in what call operators the wrapper exposes.
  template<class F>
  using qualified_target_t =
      std::conditional_t<(traits::ref_qualifier == ref_qual::rvalue),
          cv_qualified_target_t<F>&&, cv_qualified_target_t<F>&>;

  // Whether `F`, invoked as `qualified_target_t<F>`, yields `ResultT`, without
  // throwing for a `noexcept` signature.
  //
  // Construction and assignment are constrained on this to force the user to
  // explicitly select the correct `on_empty` policy.
  template<class F>
  static constexpr bool invocable_v =
      noexcept_call
          ? std::is_nothrow_invocable_r_v<ResultT, qualified_target_t<F>,
                Args...>
          : std::is_invocable_r_v<ResultT, qualified_target_t<F>, Args...>;

  // Invocation function pointer, where the `void*` is the type-erased storage.
  using invoke_fn_t = ResultT (*)(void*, Args...) noexcept(noexcept_call);

  struct destination_spec; // Fwd.

  // Lifespan manager.
  //
  // See `destination_spec` and `lifespan_impl` for details.
  using lifespan_fn_t = size_t (*)(void*, destination_spec*);

  // The lifespan thunk's answer to a probe or relocation it does not accept.
  //
  // In contrast, a 0 is a legitimate answer when the invocation points
  // directly at the function and there's no storage, or further indirection,
  // needed.
  static constexpr size_t refused = std::numeric_limits<size_t>::max();

  // The pair of thunk pointers a wrapper keeps ahead of its storage, which
  // is what a relocation transfers.
  //
  // This structure is the heart of what makes a `flexi_function` work. The
  // `invoke` is directly called by `operator()`, while `lifespan` handles all
  // the type-specific movement and destruction. The rest of the wrapper is
  // just a buffer for the stored callable, or at least a pointer to it on the
  // heap.
  //
  // When the wrapper is empty, the `lifespan` is null and `invoke` holds the
  // wrapper's empty invoker.
  struct thunk_pair {
    invoke_fn_t invoke;
    lifespan_fn_t lifespan;
  };

  // Lifespan destination spec.
  //
  // Describes where a stored callable relocates to, based on the
  // characteristics of the receiving wrapper.
  //
  // The `policy` is the receiving wrapper's `invocable_policy`, whose storage
  // members decide whether the callable lands inline or on the heap or at all.
  // For a `heap_only` receiver, the policy's buffer numbers are not used. And
  // `empty` is likewise unused, because it's baked into the type.
  //
  // `dispatch` is the destination wrapper's thunk pair, which the `lifespan`
  // implementation writes on success, based on the mode the callable ends up
  // in.
  //
  // `to` is the destination buffer, or null for a probe.
  struct destination_spec {
    invocable_policy policy;
    thunk_pair* dispatch;
    void* to;
  };

  // Invoke the stored callable, where `storage` is the wrapper's type-erased
  // buffer.
  //
  // An `allocation_mode::direct` callable is not in the buffer at all. The
  // `target` named here is what the object model requires to call a member
  // `operator()` (a null object is not an option, even for a body that never
  // touches `this`); it has no data and no runtime presence, and the
  // signature's qualifiers apply to it exactly as they would to a stored one.
  template<class F, allocation_mode SourceAlloc>
  static ResultT
  invoke_impl(void* storage, Args... args) noexcept(noexcept_call) {
    if constexpr (SourceAlloc == allocation_mode::direct) {
      F target{};
      return std::invoke_r<ResultT>(static_cast<qualified_target_t<F>>(target),
          std::forward<Args>(args)...);
    } else {
      return std::invoke_r<ResultT>(
          static_cast<qualified_target_t<F>>(
              *stored_fn<F, SourceAlloc>(storage)),
          std::forward<Args>(args)...);
    }
  }

  // Whether `ResultT` supports `on_empty::silent`: it can be value-initialized
  // or is `void`.
  static constexpr bool silenceable =
      (std::is_void_v<ResultT> || std::is_default_constructible_v<ResultT>);

  // Whether calling an empty wrapper with `on_empty::silent` is noexcept; a
  // subset of `silenceable`.
  static constexpr bool nothrow_silenceable =
      (std::is_void_v<ResultT> ||
          std::is_nothrow_default_constructible_v<ResultT>);

  // The invoke stub for an empty wrapper.
  //
  // Returns `ResultT{}`, throws `std::bad_function_call`, or terminates, per
  // `Mode`. The wrapper's own `static_assert` has already ruled out `silent`
  // on a result that is not `silenceable`.
  template<on_empty Mode>
  static ResultT
  empty_invoke_impl([[maybe_unused]] void*, [[maybe_unused]] Args...) noexcept(
      (Mode == on_empty::terminate) ||
      ((Mode == on_empty::silent) && nothrow_silenceable)) {
    if constexpr (Mode == on_empty::silent) {
      if constexpr (std::is_void_v<ResultT>)
        return;
      else
        return ResultT{};
    } else if constexpr (Mode == on_empty::terminate) {
      std::terminate();
    } else {
      throw std::bad_function_call();
    }
  }

  // Manage the lifespan of a stored callable through a single function that
  // can size, destroy, probe, or relocate, depending on the arguments.
  //
  // - When `from` is null, it is a pure size query that returns the stored
  // size: `sizeof(F)`, or 0 for an `allocation_mode::direct` source.
  //
  // - When `from` is set and `dest` is null, it destroys the callable at
  // `from` (freeing its heap block, if applicable).
  //
  // - When `from` and `dest` are set, but `dest->to` is null, it is a probe
  // that returns the stored size when a relocation into `dest` would be
  // accepted and `refused` otherwise.
  //
  // - When `from` (and `dest`) and `dest->to` are set, it relocates the
  // callable into `dest->to`, filling `dest->dispatch`.
  //
  // Here, the destination calls the source's lifespan thunk, passing a
  // `destination_spec` describing itself. Only the source's thunk knows the
  // stored type; the destination's own thunk pair is a passive output,
  // written on success.
  //
  // -- When the destination allows heap storage and the source has the
  // invocable stored in a heap block, it is handed over as is.
  //
  // -- When the destination allows inline storage and the source is eligible
  // (because it fits, matches alignment, and cannot throw on move), it is
  // moved into the destination's buffer.
  //
  // -- Otherwise, when the destination allows heap storage, the source is
  // boxed onto the heap. The allocation can throw, and the move into the
  // block cannot (an inline source is nothrow-move), so a throw leaves both
  // sides intact.
  //
  // -- When the destination allows only inline storage but the source doesn't
  // fit (or, to be more specific, is ineligible due to size, alignment, or
  // a throwing move) it refuses with `refused`, leaving both sides intact.
  //
  // -- On success, the destination's thunk pair is written for the mode the
  // callable landed in. The source's own pair is the caller's
  // responsibility: each wrapper's empty-call behavior is baked into its
  // type, so the caller reinstalls its own empty pair.
  //
  // An `allocation_mode::direct` source has nothing to size, destroy, or
  // move: its size is 0, destruction is a no-op, every destination accepts it,
  // and a relocation only writes the thunk pair.
  template<class F, allocation_mode SourceAlloc>
  static size_t lifespan_impl(void* from, destination_spec* dest) {
    if constexpr (SourceAlloc == allocation_mode::direct) {
      if (from && dest && dest->to)
        *dest->dispatch = dispatch_for<F, SourceAlloc>();
      return 0;
    } else {
      if (!from) return sizeof(F);

      F* f = stored_fn<F, SourceAlloc>(from);

      if (!dest) return do_destroy<F, SourceAlloc>(f);

      // First decide whether a relocation is allowed.
      const bool fits_inline =
          policy_details::can_store_inline<F>(dest->policy);
      const bool may_heap =
          (dest->policy.alloc != invocable_alloc::inline_only);
      if (!fits_inline && !may_heap) return refused;
      if (!dest->to) return sizeof(F);

      if constexpr (SourceAlloc == allocation_mode::dynamic) {
        // A dynamically-allocated source never moves to a new box. It either
        // hands its box over or un-boxes into an `inline_only` buffer.
        if (may_heap) return hand_over<F>(f, dest);
        if constexpr (std::is_nothrow_move_constructible_v<F>) {
          // An `inline_only` destination forces un-boxing, and the refusal
          // check above guaranteed the fit.
          assert(fits_inline);
          return move_inlined<F, SourceAlloc>(f, dest);
        } else {
          // Unreachable: a throwing-move `F` is never `fits_inline`, while
          // `may_heap` was consumed above, so the refusal check has already
          // returned.
          //
          // The arm exists because it is the branch the `if constexpr`
          // instantiates for a throwing-move `F` (whose `move_inlined` would
          // not compile), and it returns the refusal value so that a future
          // logic error fails safe instead of reporting a successful
          // relocation.
          assert(false);
          return refused;
        }
      } else {
        // An inline source stays inline when the destination is eligible, and
        // otherwise moves to a new block.
        if (fits_inline) return move_inlined<F, SourceAlloc>(f, dest);
        return move_dynamic<F, SourceAlloc>(f, dest);
      }
    }
  }

  // The pair for a callable of type `F` stored under `SourceAlloc`.
  //
  // A function rather than a variable template, so that the static analyzer
  // can see the thunk addresses flow into the pair, and follow the heap
  // block's deletion through the erased lifespan call.
  template<class F, allocation_mode SourceAlloc>
  static consteval thunk_pair dispatch_for() noexcept {
    return {&invoke_impl<F, SourceAlloc>, &lifespan_impl<F, SourceAlloc>};
  }

private:
  // The stored callable inside `storage`.
  //
  // An inline callable is constructed in the buffer itself, so `storage`
  // points at it directly. A heap callable is instead reached through the
  // `F*` that the buffer holds, hence the extra dereference.
  template<class F, allocation_mode SourceAlloc>
  static F* stored_fn(void* storage) noexcept {
    return (SourceAlloc == allocation_mode::dynamic)
               ? *static_cast<F**>(storage)
               : static_cast<F*>(storage);
  }

  // Hand the heap block at `f` over to `dest`, which admits heap storage, and
  // point dest's slots at the dynamic thunks.
  //
  // The allocation is already paid for, so un-boxing would cost a move for
  // nothing.
  template<class F>
  static size_t hand_over(F* f, destination_spec* dest) noexcept {
    *static_cast<F**>(dest->to) = f;
    *dest->dispatch = dispatch_for<F, allocation_mode::dynamic>();
    return sizeof(F);
  }

  // Move the callable at `f` inline into `dest`'s buffer and point `dest`'s
  // slots at the inlined thunks.
  //
  // Serves both the buffer-to-buffer move and the un-boxing of a dynamic
  // source into an `inline_only` buffer; the caller has already checked the
  // fit.
  template<class F, allocation_mode SourceAlloc>
  static size_t move_inlined(F* f, destination_spec* dest) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<F>,
        "move_inlined: only a nothrow-move source moves inline");
    // The analyzer cannot see that the caller's fit check ties `sizeof(F)`
    // to the true capacity of the buffer behind `dest->to`.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.PlacementNew)
    new (dest->to) F(std::move(*f));
    do_destroy<F, SourceAlloc>(f);
    *dest->dispatch = dispatch_for<F, allocation_mode::inlined>();
    return sizeof(F);
  }

  // Box the inline callable at `f` onto the heap and point `dest`'s slots at
  // the dynamic thunks.
  //
  // Only an inline source ever moves to dynamic; a dynamic block is handed
  // over or un-boxed instead. The allocation can throw, and the move cannot
  // (because an inline source is nothrow-move), so both sides are untouched on
  // a throw.
  template<class F, allocation_mode SourceAlloc>
  static size_t move_dynamic(F* f, destination_spec* dest) {
    static_assert(SourceAlloc == allocation_mode::inlined,
        "move_dynamic: only an inline source moves to dynamic");
    F* boxed = new F(std::move(*f));
    f->~F();
    *static_cast<F**>(dest->to) = boxed;
    *dest->dispatch = dispatch_for<F, allocation_mode::dynamic>();
    return sizeof(F);
  }

  // Destroy the stored callable, freeing its heap block when dynamic.
  template<class F, allocation_mode SourceAlloc>
  static size_t do_destroy(F* f) noexcept {
    if constexpr (SourceAlloc == allocation_mode::dynamic)
      delete f;
    else
      f->~F();
    return sizeof(F);
  }
};

} // namespace fn_details

#pragma endregion
#pragma region flexi_function

// The core goals of this invocation wrapper are control and flexibility (hence
// the name, `controlo_flecto_functo`... oh, wait, that's not the name, though
// it really should be, since it's so much fun to say).
//
// - The storage policy is configurable, and types specialized on different
// policies can interoperate. Instances that differ only in policy can be
// freely assigned, as long as the destination can accommodate the stored
// callable.
//
// - The wrapper can be configured to avoid using the heap, or to always use
// the heap, or to use the heap only when necessary. If the source already
// stores the callable on the heap and the target allows heap storage, then
// only pointer ownership is moved.
//
// - The size of the inline buffer can be configured, along with its alignment
// requirements. When a specialization is allowed to use either inline or heap,
// the inline buffer acts as a small-buffer optimization. When heap-only, these
// configuration values are ignored, and the wrapper is always sized for the
// `thunk_pair` and the pointer to the heap storage.
//
// - The default behavior of an empty wrapper when invoked is to throw
// `std::bad_function_call`, just like `std::function` does. But it can also
// be configured to return the value-initialized (or void) result, or to
// terminate; see `on_empty`. The silent option is refused at compile time for
// a result that cannot be value-initialized, and the raise option is refused
// for a `noexcept` signature.
//
// - The signature may carry `const`, `&` or `&&`, and `noexcept`, in any
// combination, with the meaning `std::move_only_function` gives them: the
// cv- and ref-qualifiers say which wrappers may call and how the target is
// invoked, and `noexcept` constrains construction to nothrow-invocable
// callables in exchange for a `noexcept` call operator.
//
// - It is inflexibly move-only, because copying wrappers have limited uses,
// and at a high cost to design choices. A copyable wrapper would need to be a
// different class, like `std::copyable_function`.
//
// A few odds and ends:
//
// - `size` reports the stored callable's byte size, and `capacity` the
// inline buffer's. `can_adopt` is the up-front check that assigning from a
// given sibling would be accepted, so a refusal can be averted rather than
// caught.
//
// - For an `inline_only` instance, no dynamic allocation is performed.
// However, we can't stop a callable from allocating internally, and we do
// support explicit conversion from `std::function` and
// `std::move_only_function`, and both of these are capable of dynamic
// allocation.
//
// (Note that wrapping either of these std polymorphic function wrappers
// requires nesting, so there's a performance penalty; that's why it's
// `explicit`.)
//
// The mechanism, with memory layouts and the thunk protocol, is documented in
// "flexi_function.md".
template<invocable_policy Policy, class Sig, class ResultT, class... Args>
class flexi_function<Policy, Sig, ResultT(Args...)> {
  static_assert(std::is_same_v<ResultT(Args...), signature_function_t<Sig>>,
      "flexi_function: the third template parameter is derived from the "
      "signature; do not pass it");

  using thunks = fn_details::flexi_thunks<Sig>;

  // The stored callable with the signature's qualifiers applied.
  template<class F>
  using qualified_target_t = thunks::template qualified_target_t<F>;

  using invoke_fn_t = thunks::invoke_fn_t;
  using lifespan_fn_t = thunks::lifespan_fn_t;
  using destination_spec = thunks::destination_spec;
  using thunk_pair = thunks::thunk_pair;

  static constexpr bool supports_inline =
      (Policy.alloc != invocable_alloc::heap_only);
  static constexpr bool supports_dynamic =
      (Policy.alloc != invocable_alloc::inline_only);

  // Under `inline_or_heap`, the buffer doubles as the pointer slot for a
  // heap-stored callable, so it must be able to hold a pointer.
  static_assert(
      !(supports_inline && supports_dynamic) ||
          ((Policy.inline_size >= sizeof(void*)) &&
              (Policy.inline_align >= alignof(void*))),
      "flexi_function: under inline_or_heap the buffer doubles as the pointer "
      "slot for a heap-stored callable, so inline_size and inline_align must "
      "accommodate a pointer");
  static_assert(std::has_single_bit(Policy.inline_align),
      "flexi_function: inline_align must be a power of two");
  static_assert((Policy.empty != on_empty::silent) || thunks::silenceable,
      "flexi_function: on_empty::silent needs a result that can be "
      "value-initialized (or void); choose raise or terminate for this "
      "signature");
  static_assert(!thunks::noexcept_call || (Policy.empty != on_empty::raise),
      "flexi_function: on_empty::raise cannot serve a noexcept signature; "
      "choose terminate, or silent (for a nothrow-value-initializable "
      "result)");
  static_assert(!thunks::noexcept_call || (Policy.empty != on_empty::silent) ||
                    thunks::nothrow_silenceable,
      "flexi_function: on_empty::silent under a noexcept signature needs a "
      "result whose value-initialization cannot throw; choose terminate");
  static_assert(
      !supports_inline ||
          Policy.inline_size ==
              padded_size(Policy.inline_size, Policy.inline_align),
      "flexi_function: inline_size that is not a multiple of inline_align "
      "would waste the difference as padding; pass it through padded_size");

  // Siblings are friends.
  template<invocable_policy, class, class>
  friend class flexi_function;

  // Actual buffer geometry: a `heap_only` instance keeps just the pointer.
  static constexpr size_t buf_size =
      supports_inline ? Policy.inline_size : sizeof(void*);
  static constexpr size_t buf_align =
      supports_inline ? Policy.inline_align : alignof(void*);

#pragma region Creation
public:
  static constexpr invocable_policy policy = Policy;

  // Inline storage capacity in bytes, 0 for a `heap_only` policy.
  static constexpr size_t storage_size =
      supports_inline ? Policy.inline_size : 0;

#pragma region Constructors

  flexi_function() = default;
  explicit flexi_function(std::nullptr_t) noexcept {}

  flexi_function(const flexi_function&) = delete;
  flexi_function& operator=(const flexi_function&) = delete;

  // Implicitly construct from any callable that the signature can invoke:
  // one that, invoked as `qualified_target_t`, yields `ResultT` (and for a
  // `noexcept` signature, without throwing).
  //
  // The callable is consumed (moved, or copied when that is trivial, see
  // `Consumable`) into internal storage, or onto the heap when the policy
  // sends it there.
  //
  // The inline path can't throw, but the heap path can throw on allocation,
  // and so can the move itself (which is precisely why a callable whose move
  // constructor may throw is heap-bound). A throw leaves this instance empty.
  // An `allocation_mode::direct` callable takes neither path: nothing is
  // stored, under any policy.
  //
  // The std polymorphic function wrappers are deliberately excluded
  // here; wrapping one takes the explicit constructor below.
  template<Consumable FN>
  requires(thunks::template invocable_v<std::decay_t<FN>> &&
           !is_std_function_wrapper_v<std::decay_t<FN>>)
  flexi_function(FN&& fn) noexcept(
      policy_details::can_store_nothrow<std::decay_t<FN>>(Policy)) {
    if (is_null_callable(fn)) return;
    do_store<std::decay_t<FN>>(std::forward<FN>(fn));
  }

  // Explicitly wrap a std polymorphic function wrapper, `std::function` or
  // `std::move_only_function`, whose signature is compatible with
  // `ResultT(Args...)`.
  //
  // Besides providing compatibility (at a cost), this is the escape hatch for
  // a payload too large for an `inline_only` buffer, in which the wrapper
  // keeps the oversized callable on its own heap allocation, and only its
  // small shell must fit inline. The costs are why the wrap is explicit: every
  // call double-indirects, and the shell may allocate dynamically, which is an
  // exception to the zero-allocation guarantee under `inline_only`.
  //
  // Invocability is checked the same way as for any other callable: the
  // wrapped wrapper is invoked as `qualified_target_t`. So an unqualified
  // signature admits a `std::move_only_function<int() &>` and rejects an
  // `int() &&` one, while an `&&`-qualified signature does the reverse.
  template<Consumable FN>
  requires(is_std_function_wrapper_v<std::decay_t<FN>> &&
           thunks::template invocable_v<std::decay_t<FN>>)
  explicit flexi_function(FN&& fn) noexcept(
      policy_details::can_store_nothrow<std::decay_t<FN>>(Policy)) {
    if (!fn) return;
    do_store<std::decay_t<FN>>(std::forward<FN>(fn));
  }

  // Move from same type.
  flexi_function(flexi_function&& other) noexcept { do_adopt(other); }

  // Move from a same-signature sibling of another policy, transplanting the
  // stored callable rather than nesting the wrapper.
  //
  // The callable lands inline when it fits this buffer and on the heap when
  // this policy admits one. A callable that can do neither throws
  // `std::length_error`, leaving the source intact, and a boxing
  // allocation can throw `std::bad_alloc` likewise. Neither throw is reachable
  // when every callable the source policy admits is guaranteed a home here,
  // and then the constructor is `noexcept`.
  template<invocable_policy P>
  requires(P != Policy)
  flexi_function(flexi_function<P, Sig>&& other) noexcept(
      !policy_details::adopt_may_throw(Policy, P)) {
    do_adopt(other);
  }

  ~flexi_function() noexcept {
    if (dispatch_.lifespan) dispatch_.lifespan(storage_, nullptr);
  }

#pragma endregion
#pragma region Assignment

  // Move assignment from same type.
  flexi_function& operator=(flexi_function&& other) noexcept {
    if (this == &other) return *this;
    reset();
    do_adopt(other);
    return *this;
  }

  // Move assignment from a same-signature sibling of another policy, under
  // the same transplant rules as the converting move constructor.
  //
  // A refusal is detected by a pre-flight `can_adopt`, throwing before
  // either side is touched. A boxing allocation can still throw, leaving the
  // instance empty.
  template<invocable_policy P>
  requires(P != Policy)
  flexi_function& operator=(flexi_function<P, Sig>&& other) noexcept(
      !policy_details::adopt_may_throw(Policy, P)) {
    if constexpr (policy_details::adopt_may_throw(Policy, P)) {
      if (!can_adopt(other))
        throw std::length_error{"flexi_function: callable too large"};
    }
    reset();
    do_adopt(other);
    return *this;
  }

  // Replace the stored callable with `fn`.
  //
  // This stores `fn` directly, avoiding a transplant through a temporary
  // sibling. The std wrappers are excluded here as they are for construction;
  // wrap one explicitly and move-assign the result.
  //
  // A throw leaves this instance empty.
  template<Consumable FN>
  requires(
      thunks::template invocable_v<std::decay_t<FN>> &&
      !is_std_function_wrapper_v<std::decay_t<FN>> &&
      !is_flexi_function_v<std::decay_t<FN>>)
  flexi_function& operator=(FN&& fn) noexcept(
      policy_details::can_store_nothrow<std::decay_t<FN>>(Policy)) {
    reset();
    if (is_null_callable(fn)) return *this;
    do_store<std::decay_t<FN>>(std::forward<FN>(fn));
    return *this;
  }

  // Assign nullptr to make the instance empty.
  flexi_function& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  // Empty the instance.
  void reset() noexcept {
    if (!dispatch_.lifespan) return;
    dispatch_.lifespan(storage_, nullptr);
    dispatch_ = empty_dispatch;
  }

  void swap(flexi_function& other) noexcept {
    auto tmp = std::move(*this);
    *this = std::move(other);
    other = std::move(tmp);
  }

  friend void swap(flexi_function& a, flexi_function& b) noexcept {
    a.swap(b);
  }

#pragma endregion
#pragma endregion
#pragma region Invocation

  // Whether the signature permits a call through a wrapper of type `Self`, as
  // `operator()` deduces it.
  //
  // A `const` wrapper needs a `const` signature, an rvalue wrapper is refused
  // by an `&` signature, and an lvalue wrapper by an `&&` one.
  template<class Self>
  static constexpr bool callable_through_v =
      ((thunks::traits::const_qualifier == const_qual::present) ||
          !std::is_const_v<std::remove_reference_t<Self>>) &&
      ((thunks::traits::ref_qualifier == ref_qual::none) ||
          ((thunks::traits::ref_qualifier == ref_qual::lvalue) ==
              std::is_lvalue_reference_v<Self>));

  // Invoke through the type-erased invoke thunk.
  //
  // One member covers every cv/ref qualification because `Self` deduces the
  // calling wrapper's constness and value category, and the constraint admits
  // exactly what the signature permits.
  //
  // The thunk applies the signature's qualifiers to the target itself, which
  // is why casting away the buffer's constness for a `const` call is sound:
  // the target is only ever reached as `const`.
  //
  // The call is `noexcept` exactly when the signature is, which the
  // construction constraint and the `on_empty` checks above make sound.
  template<class Self>
  requires(callable_through_v<Self>)
  ResultT
  operator()(this Self&& self, Args... args) noexcept(thunks::noexcept_call) {
    return self.dispatch_.invoke(const_cast<std::byte*>(self.storage_),
        std::forward<Args>(args)...);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] explicit operator bool() const noexcept {
    return dispatch_.lifespan;
  }

  // Size of the stored callable in bytes; 0 when empty, and also for an
  // `allocation_mode::direct` callable, which is not stored.
  [[nodiscard]] size_t size() const noexcept {
    return dispatch_.lifespan ? dispatch_.lifespan(nullptr, nullptr) : 0;
  }

  // Capacity of the inline storage in bytes, 0 for a `heap_only` policy.
  [[nodiscard]] size_t capacity() const noexcept { return storage_size; }

  // `can_adopt`: whether this `flexi_function` type can accommodate
  // `source`'s current callable, so that converting (or assigning) from it
  // will not throw `std::length_error`.
  //
  // Static, because the answer is a property of this wrapper type against
  // the source's runtime callable. It works before any destination instance
  // exists, and is equally callable through one. Only an `inline_only`
  // destination can ever answer no (everything else has the heap to fall
  // back on), and an empty source is always adoptable, to empty. It does
  // not promise the allocation a boxing adoption may need.
  template<invocable_policy P>
  [[nodiscard]] static bool
  can_adopt(const flexi_function<P, Sig>& source) noexcept {
    if constexpr (Policy.alloc != invocable_alloc::inline_only) {
      return true;
    } else {
      if (!source.dispatch_.lifespan) return true;
      destination_spec probe{.policy = Policy,
          .dispatch = nullptr,
          .to = nullptr};
      // The probe only reads, but the erased signature is mutable.
      return (
          source.dispatch_.lifespan(const_cast<std::byte*>(source.storage_),
              &probe) != thunks::refused);
    }
  }

#pragma endregion
#pragma region Implementation
private:
  // Whether `fn` is a null callable, which yields an empty wrapper rather
  // than a truthy shell that is undefined (or throws) when called, matching
  // `std::function`.
  template<class FD>
  static bool is_null_callable(const FD& fn) noexcept {
    if constexpr (std::is_pointer_v<FD> || std::is_member_pointer_v<FD> ||
                  is_flexi_function_v<FD>)
      return !fn;
    else
      return false;
  }

  // Consume `fn` into storage and publish its thunks.
  //
  // `FD` is the stored type, already decayed and always passed explicitly;
  // `fn` is forwarded into it, so an rvalue is moved and a (trivially
  // copyable) lvalue is copied. An `allocation_mode::direct` `fn` is not
  // consumed at all: only its type survives, in the thunks.
  //
  // The caller handles any null-callable special case before calling, and
  // the instance is empty on entry.
  template<class FD, class FN>
  void
  do_store(FN&& fn) noexcept(policy_details::can_store_nothrow<FD>(Policy)) {
    static_assert(!std::is_reference_v<FD>,
        "flexi_function: do_store requires the decayed stored type");
    static_assert(!std::is_function_v<std::remove_reference_t<FN>>,
        "flexi_function: a function name is a compile-time target. Wrap it as "
        "constant_fn<f>{} to call it directly with nothing stored, or, when "
        "the reference is a runtime value (it came through a forwarding "
        "parameter or a conditional), take its address to store it as a "
        "pointer");
    static_assert(
        supports_dynamic || policy_details::inline_eligible<FD>(Policy),
        "flexi_function: the callable is not eligible for an inline_only "
        "instance's inline buffer: too large, over-aligned, or its move "
        "constructor may throw");
    static_assert(std::is_nothrow_destructible_v<FD>,
        "flexi_function: callable destructor may throw; the instance destroys "
        "the callable, so its destructor must be noexcept");
    static_assert(
        !std::is_reference_v<ResultT> ||
            std::is_reference_v<
                std::invoke_result_t<qualified_target_t<FD>, Args...>>,
        "flexi_function: callable returns a prvalue but ResultT is a "
        "reference type; every call would produce a dangling reference");
    assert(!dispatch_.lifespan);

    constexpr auto mode = policy_details::storage_mode<FD>(Policy);
    if constexpr (mode == allocation_mode::inlined)
      new (storage_) FD(std::forward<FN>(fn));
    else if constexpr (mode == allocation_mode::dynamic)
      *reinterpret_cast<FD**>(storage_) = new FD(std::forward<FN>(fn));
    dispatch_ = thunks::template dispatch_for<FD, mode>();
  }

  // Take over `other`'s callable, when it has one.
  //
  // This instance must be empty on entry, and `other` is left empty.
  template<invocable_policy P>
  void do_adopt(flexi_function<P, Sig>& other) {
    using other_t = flexi_function<P, Sig>;
    assert(!dispatch_.lifespan);
    if (!other.dispatch_.lifespan) return;
    destination_spec dest{.policy = Policy,
        .dispatch = &dispatch_,
        .to = storage_};
    [[maybe_unused]] const bool is_refused =
        (other.dispatch_.lifespan(other.storage_, &dest) == thunks::refused);
    if constexpr (policy_details::adopt_may_throw(Policy, P)) {
      if (is_refused)
        throw std::length_error{"flexi_function: callable too large"};
    } else {
      assert(!is_refused);
    }
    other.dispatch_ = other_t::empty_dispatch;
  }

  // The empty state: this type's compile-time empty invoker, and no
  // lifespan thunk.
  static constexpr thunk_pair empty_dispatch{
      &thunks::template empty_invoke_impl<Policy.empty>, nullptr};

#pragma endregion
#pragma region Data members

  thunk_pair dispatch_ = empty_dispatch;

  // Deliberately no initializer: occupancy is keyed by `dispatch_.lifespan`,
  // and zeroing the buffer on every construction would be pure waste.
  alignas(buf_align) std::byte storage_[buf_size];

#pragma endregion
};

#pragma endregion
}} // namespace corvid::meta
