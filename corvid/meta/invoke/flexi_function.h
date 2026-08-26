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
#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../concepts.h"
#include "../crossplatform.h"
#include "../maybe.h"
#include "invocable_common.h"
#include "invocable_policy.h"
#include "../traits.h"

namespace corvid { inline namespace meta {
namespace flexi {

// The shared invocable vocabulary (`invocable_policy`, `storage_mode`, and
// kin) is spelled unqualified throughout.
using namespace invocables;

// `flexi_function<Sig, Policy>` is a wrapper for an invocable target.
//
// It is a move-only type-erased callable, like `std::move_only_function` or
// `stdext::inplace_function`, whose storage and empty-call behavior follow a
// configurable `invocable_policy`.

#pragma region Traits

// Fwd.
//
// The third parameter is the pattern-matching hook that gives the class body
// `ResultT` and `Args...` even when `Sig` is qualified. It is derived from
// the signature and constrained to that derivation, so any other spelling
// is ill-formed where it is named, and every `flexi_function` is spelled by
// its first two parameters alone.
template<class Sig, invocable_policy Policy = invocable_policy::basic,
    class FunctionT = signature_function_t<Sig>>
requires std::same_as<FunctionT, signature_function_t<Sig>>
class flexi_function;

// `is_flexi_function_v` is whether `T` is a `flexi_function` (of any policy).
template<typename T>
constexpr inline bool is_flexi_function_v = false;

template<class Sig, invocable_policy Policy>
constexpr inline bool is_flexi_function_v<flexi_function<Sig, Policy>> = true;

#pragma endregion
#pragma region details

namespace details {

// The shared working parts live with `invocable_policy` and in
// invocable_common.h; bring them in whole, so unqualified `details::` calls
// find them.
using namespace invocables::implementation;

// `flexi_thunks` are the type-erased thunks for one signature, shared by every
// `flexi_function` of that signature, regardless of policy.
//
// This is what lets siblings of different policies transplant a stored
// callable between them.
//
// A stored callable lives either inline in the wrapper's buffer or on the
// heap, owned by the pointer kept in that buffer. The `SourceStorage`
// parameter in each thunk selects how the `storage` is interpreted.
//
// The thunks see that storage only as the erased address a wrapper hands
// them, because a wrapper's `storage_area` is sized by its policy and the
// thunks serve every policy, so no thunk can name the storage type.
// `stored_fn` is where the address becomes the callable's type again, and
// nothing past it is erased.
//
// The second parameter mirrors `flexi_function`'s third: it is the
// pattern-matching hook, not a choice, and is constrained the same way.
template<class Sig, class FunctionT = signature_function_t<Sig>>
requires std::same_as<FunctionT, signature_function_t<Sig>>
struct flexi_thunks;

template<class Sig, class ResultT, class... Args>
struct flexi_thunks<Sig, ResultT(Args...)> {
  using traits = signature_traits<Sig>;

  // Whether the signature is `noexcept`, which makes the invoke thunks, and
  // the wrapper's call operator, `noexcept`.
  static constexpr bool is_noexcept = traits::is_noexcept;

  // `F` with the signature's cv-qualifier applied.
  template<class F>
  using cv_qualified_target_t = conditional_const_t<traits::access, F>;

  // `qualified_target_t` is `F` with the signature's qualifiers applied,
  // which is how the stored target is invoked: `F cv&`, or `F cv&&` under an
  // `&&` signature.
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
  static constexpr bool is_invocable =
      is_noexcept
          ? std::is_nothrow_invocable_r_v<ResultT, qualified_target_t<F>,
                Args...>
          : std::is_invocable_r_v<ResultT, qualified_target_t<F>, Args...>;

  // Invocation function pointer, where the `void*` is the type-erased storage.
  using invoke_fn_t = ResultT (*)(void*, Args...) noexcept(is_noexcept);

  struct destination_spec; // Fwd.

  // Lifespan manager.
  //
  // See `destination_spec` and `lifespan_impl` for details.
  using lifespan_fn_t = size_t (*)(void*, destination_spec*);

  // `refusal` is the lifespan thunk's answer to a probe or relocation it does
  // not accept.
  //
  // In contrast, a 0 is a legitimate answer when the invocation points
  // directly at the function and there's no storage, or further indirection,
  // needed.
  static constexpr size_t refusal = std::numeric_limits<size_t>::max();

  // `thunk_pair` is the pair of thunk pointers a wrapper keeps ahead of its
  // storage, which is what a relocation transfers.
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

  // `destination_spec` describes where a stored callable relocates to, based
  // on the characteristics of the receiving wrapper.
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
  //
  // `is_inline_fit_guaranteed` is the caller's compile-time knowledge that
  // every inline target the source's policy admits fits this buffer (see
  // `implementation::is_inline_fit_guaranteed`), which every same-type move
  // has. With it, an inline source relocates without the runtime fit check.
  struct destination_spec {
    invocable_policy policy;
    thunk_pair* dispatch;
    void* to;
    bool is_inline_fit_guaranteed;
  };

  // `invoke_impl` to invoke the stored callable, where `storage` is the
  // wrapper's type-erased buffer.
  //
  // An `storage_mode::direct` callable is not in the buffer at all. The
  // `target` named here is what the object model requires to call a member
  // `operator()` (a null object is not an option, even for a body that never
  // touches `this`); it has no data and no runtime presence, and the
  // signature's qualifiers apply to it exactly as they would to a stored one.
  template<class F, storage_mode SourceStorage>
  static ResultT
  invoke_impl(void* storage, Args... args) noexcept(is_noexcept) {
    if constexpr (SourceStorage == storage_mode::direct) {
      F target{};
      return std::invoke_r<ResultT>(static_cast<qualified_target_t<F>>(target),
          std::forward<Args>(args)...);
    } else {
      return std::invoke_r<ResultT>(
          static_cast<qualified_target_t<F>>(
              *stored_fn<F, SourceStorage>(storage)),
          std::forward<Args>(args)...);
    }
  }

  // The empty-call rules for `ResultT`.
  using empty_traits = empty_call_traits<ResultT>;

  // The invoke stub for an empty wrapper, which performs the empty call under
  // `Behavior`.
  //
  // The wrapper's own `static_assert`s have already established that
  // `Behavior` is admitted.
  template<on_empty Behavior>
  static ResultT
  empty_invoke_impl([[maybe_unused]] void*, [[maybe_unused]] Args...) noexcept(
      empty_traits::template is_nothrow<Behavior>) {
    return empty_traits::template invoke<Behavior>();
  }

  // `lifespan_impl` to manage the lifespan of a stored callable through a
  // single function that can size, destroy, probe, or relocate, depending on
  // the arguments.
  //
  // - When `from` is null, it is a pure size query that returns the stored
  // size: `sizeof(F)`, or 0 for an `storage_mode::direct` source.
  //
  // - When `from` is set and `dest` is null, it destroys the callable at
  // `from` (freeing its heap block, if applicable).
  //
  // - When `from` and `dest` are set, but `dest->to` is null, it is a probe
  // that returns the stored size when a relocation into `dest` would be
  // accepted and `refusal` otherwise.
  //
  // - When `from` (and `dest`) and `dest->to` are set, it relocates the
  // callable into `dest->to`, filling `dest->dispatch`.
  //
  // Here, the destination calls the source's lifespan thunk, passing a
  // `destination_spec` describing itself. Only the source's thunk knows the
  // stored type; the destination's own thunk pair is a passive output,
  // written on success.
  //
  // -- The route is `adoption_of`'s answer for the destination's policy: a
  // heap block is handed over as is, or un-boxed into an `inline_only`
  // buffer; an inline source moves into the destination's buffer when it
  // fits, and is otherwise boxed onto the heap; a source that can go nowhere
  // is refused with `refusal`, leaving both sides intact. Boxing is the one
  // step that can throw (the allocation; the move into the block cannot,
  // since an inline source is nothrow-move), and a throw leaves both sides
  // intact.
  //
  // -- On success, the destination's thunk pair is written for the mode the
  // callable landed in. The source's own pair is the caller's
  // responsibility: each wrapper's empty-call behavior is baked into its
  // type, so the caller reinstalls its own empty pair.
  //
  // An `storage_mode::direct` source has nothing to size, destroy, or
  // move: its size is 0, destruction is a no-op, every destination accepts it,
  // and a relocation only writes the thunk pair.
  template<class F, storage_mode SourceStorage>
  requires(SourceStorage == storage_mode::direct)
  static size_t lifespan_impl(void* from, destination_spec* dest) {
    if (from && dest && dest->to)
      *dest->dispatch = dispatch_for<F, SourceStorage>();
    return 0;
  }

  template<class F, storage_mode SourceStorage>
  requires(SourceStorage != storage_mode::direct)
  static size_t lifespan_impl(void* from, destination_spec* dest) {
    if (!from) return sizeof(F);

    F* f = stored_fn<F, SourceStorage>(from);

    if (!dest) return do_destroy<F, SourceStorage>(f);

    // First decide whether a relocation is allowed, and which route it takes.
    // An inline source whose fit the destination guarantees relocates
    // outright; the `SourceStorage` test folds, so the `inlined` thunk tests
    // one flag and the `dynamic` thunk keeps the full rule.
    const auto route =
        (SourceStorage == storage_mode::inlined &&
            dest->is_inline_fit_guaranteed)
            ? adoption::relocate
            : details::adoption_of(dest->policy, SourceStorage, sizeof(F),
                  alignof(F), std::is_nothrow_move_constructible_v<F>);
    if (route == adoption::refuse) return refusal;
    if (!dest->to) return sizeof(F);

    if constexpr (SourceStorage == storage_mode::dynamic) {
      // A dynamically-allocated source never moves to a new box. It either
      // hands its box over or un-boxes into an `inline_only` buffer.
      if (route == adoption::hand_over) return hand_over(f, dest);
      if constexpr (std::is_nothrow_move_constructible_v<F>) {
        assert(route == adoption::unbox);
        return move_inlined<F, SourceStorage>(f, dest);
      } else {
        // Unreachable: a throwing-move `F` never fits inline, so the rule has
        // already answered `hand_over` or `refuse`.
        //
        // The arm exists because it is the branch the `if constexpr`
        // instantiates for a throwing-move `F` (whose `move_inlined` would
        // not compile), and it returns the refusal value so that a future
        // logic error fails safe instead of reporting a successful
        // relocation.
        assert(false);
        return refusal;
      }
    } else {
      // An inline source stays inline when the destination is eligible, and
      // otherwise moves to a new block.
      if (route == adoption::relocate)
        return move_inlined<F, SourceStorage>(f, dest);
      assert(route == adoption::box);
      return move_dynamic<F, SourceStorage>(f, dest);
    }
  }

  // The pair for a callable of type `F` stored under `SourceStorage`.
  //
  // A function rather than a variable template, so that the static analyzer
  // can see the thunk addresses flow into the pair, and follow the heap
  // block's deletion through the erased lifespan call.
  template<class F, storage_mode SourceStorage>
  static consteval thunk_pair dispatch_for() noexcept {
    return {&invoke_impl<F, SourceStorage>, &lifespan_impl<F, SourceStorage>};
  }

private:
  // `stored_fn` is the stored callable inside `storage`, typed.
  //
  // The storage arrives erased, and this is where the type comes back, as
  // early as the thunk can know it. An inline callable is constructed in the
  // buffer itself, so `storage` points at it directly. A heap callable is
  // instead reached through the pointer the storage holds (see
  // `storage_area`), hence the extra dereference.
  template<class F, storage_mode SourceStorage>
  static F* stored_fn(void* storage) noexcept {
    if constexpr (SourceStorage == storage_mode::dynamic)
      return static_cast<F*>(*static_cast<void**>(storage));
    else
      return static_cast<F*>(storage);
  }

  // Store the heap block at `block` into `dest`'s pointer slot, and point
  // `dest`'s slots at the dynamic thunks.
  //
  // The one place a block's address is erased into the storage.
  template<class F>
  static void store_block(F* block, destination_spec* dest) noexcept {
    // The cast is spelled out because `F` may itself be a pointer type, and
    // clang-tidy flags an implicit pointer-to-pointer conversion to `void*`
    // as a lost level.
    *static_cast<void**>(dest->to) = static_cast<void*>(block);
    *dest->dispatch = dispatch_for<F, storage_mode::dynamic>();
  }

  // Hand the heap block at `f` over to `dest`, which admits heap storage.
  //
  // The allocation is already paid for, so un-boxing would cost a move for
  // nothing.
  template<class F>
  static size_t hand_over(F* f, destination_spec* dest) noexcept {
    store_block(f, dest);
    return sizeof(F);
  }

  // Move the callable at `f` inline into `dest`'s buffer and point `dest`'s
  // slots at the inlined thunks.
  //
  // Serves both the buffer-to-buffer move (`relocate_inline`) and the
  // un-boxing of a dynamic source into an `inline_only` buffer (`unbox`); the
  // caller has already checked the fit.
  template<class F, storage_mode SourceStorage>
  static size_t move_inlined(F* f, destination_spec* dest) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<F>,
        "move_inlined: only a nothrow-move source moves inline");
    if constexpr (SourceStorage == storage_mode::dynamic)
      details::unbox(f, dest->to);
    else
      details::relocate_inline(f, dest->to);
    *dest->dispatch = dispatch_for<F, storage_mode::inlined>();
    return sizeof(F);
  }

  // Box the inline callable at `f` onto the heap and store the block in
  // `dest`.
  //
  // Only an inline source ever moves to dynamic; a dynamic block is handed
  // over or un-boxed instead. The allocation can throw, and the move cannot
  // (because an inline source is nothrow-move), so both sides are untouched on
  // a throw.
  template<class F, storage_mode SourceStorage>
  static size_t move_dynamic(F* f, destination_spec* dest) {
    static_assert(SourceStorage == storage_mode::inlined,
        "move_dynamic: only an inline source moves to dynamic");
    store_block(details::box(f), dest);
    return sizeof(F);
  }

  // Destroy the stored callable, freeing its heap block when dynamic.
  template<class F, storage_mode SourceStorage>
  static size_t do_destroy(F* f) noexcept {
    if constexpr (SourceStorage == storage_mode::dynamic)
      details::destroy_heap(f);
    else
      details::destroy_inline(f);
    return sizeof(F);
  }
};

} // namespace details

#pragma endregion
#pragma region flexi_function

// `flexi_function` is an invocation wrapper whose core goalsare control and
// flexibility (hence the name, `controlo_flecto_functo`... oh, wait, that's
// not the name, though it really should be, since it's so much fun to say).
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
// - A target that is a function known at compile time is called directly,
// with nothing stored, when it is spelled as such: `constant_fn<f>{}`, or a
// captureless lambda that names it. A bare function or member pointer is
// stored, and called through one more indirect call per invocation, because
// its value is runtime data; `runtime_fn{p}` is the same thing spelled out.
// A function name itself (`f = foo;`) is refused at compile time, since it is
// the one spelling that cannot be a runtime value, and a policy with
// `policy_enforcement::strict` refuses bare pointers too, so that the caller
// chooses. See `invocable_common.h`.
//
// - It is inflexibly move-only, because copying wrappers has limited uses, and
// at a high cost to design choices. A copyable wrapper would need to be a
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
template<class Sig, invocable_policy Policy, class ResultT, class... Args>
class flexi_function<Sig, Policy, ResultT(Args...)> {
  using thunks = details::flexi_thunks<Sig>;

  // The stored callable with the signature's qualifiers applied.
  template<class F>
  using qualified_target_t = thunks::template qualified_target_t<F>;

  using invoke_fn_t = thunks::invoke_fn_t;
  using lifespan_fn_t = thunks::lifespan_fn_t;
  using destination_spec = thunks::destination_spec;
  using thunk_pair = thunks::thunk_pair;

  // The buffer rules (a pointer's worth at least, or empty under
  // `inline_only`; a power-of-two alignment; no padding) are the policy's own,
  // each broken rule named in the error.
  static_assert(Policy.is_well_formed());
  static_assert((Policy.empty != on_empty::silent) ||
                    thunks::empty_traits::is_silenceable,
      "flexi_function: on_empty::silent needs a result that can be "
      "value-initialized (or void); choose raise or terminate for this "
      "signature");
  static_assert(!thunks::is_noexcept || (Policy.empty != on_empty::raise),
      "flexi_function: on_empty::raise cannot serve a noexcept signature; "
      "choose terminate, or silent (for a nothrow-value-initializable "
      "result)");
  static_assert(!thunks::is_noexcept || (Policy.empty != on_empty::silent) ||
                    thunks::empty_traits::is_nothrow_silenceable,
      "flexi_function: on_empty::silent under a noexcept signature needs a "
      "result whose value-initialization cannot throw; choose terminate");

  // Siblings are friends.
  template<class S, invocable_policy P, class FunctionT>
  requires std::same_as<FunctionT, signature_function_t<S>>
  friend class flexi_function;

  // Actual buffer geometry: a `heap_only` instance keeps just the pointer,
  // and an empty `inline_only` buffer, which serves only `direct` targets,
  // is `empty_t`, so that `CORVID_NO_UNIQUE_ADDRESS` can hide it.
  static constexpr size_t buf_size = Policy.buffer_size();
  static constexpr size_t buf_align = Policy.buffer_align();
  using storage_area_t =
      maybe_t<details::storage_area<buf_size, buf_align>, (buf_size > 0)>;

#pragma region Creation
public:
  static constexpr invocable_policy policy = Policy;

  // Inline storage capacity in bytes, 0 for a `heap_only` policy.
  static constexpr size_t inline_size =
      Policy.admits_inline() ? Policy.inline_size : 0;

#pragma region Constructors

  flexi_function() = default;
  explicit flexi_function(std::nullptr_t) noexcept {}

  flexi_function(const flexi_function&) = delete;
  flexi_function& operator=(const flexi_function&) = delete;

  // Converting constructor from any callable that the signature can invoke:
  // one that, invoked as `qualified_target_t`, yields `ResultT` (and for a
  // `noexcept` signature, without throwing).
  //
  // Intentionally implicit. The callable is consumed (moved, or copied when
  // that is trivial, see `Consumable`) into internal storage, or onto the heap
  // when the policy sends it there.
  //
  // The inline path can't throw, but the heap path can throw on allocation,
  // and so can the move itself (which is precisely why a callable whose move
  // constructor may throw is heap-bound). A throw leaves this instance empty.
  // An `storage_mode::direct` callable takes neither path: nothing is
  // stored, under any policy.
  //
  // The std polymorphic function wrappers are deliberately excluded
  // here; wrapping one takes the explicit constructor below.
  template<Consumable FN>
  requires(thunks::template is_invocable<std::decay_t<FN>> &&
           !is_std_function_wrapper_v<std::decay_t<FN>>)
  flexi_function(FN&& fn) noexcept(
      details::can_store_nothrow<std::decay_t<FN>>(Policy)) {
    if (is_null_callable(fn)) return;
    do_store<std::decay_t<FN>>(std::forward<FN>(fn));
  }

  // Explicit constructor wrapping a std polymorphic function wrapper,
  // `std::function` or `std::move_only_function`, whose signature is
  // compatible with `ResultT(Args...)`.
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
           thunks::template is_invocable<std::decay_t<FN>>)
  explicit flexi_function(FN&& fn) noexcept(
      details::can_store_nothrow<std::decay_t<FN>>(Policy)) {
    if (is_null_callable(fn)) return;
    do_store<std::decay_t<FN>>(std::forward<FN>(fn));
  }

  flexi_function(flexi_function&& other) noexcept { do_adopt(other); }

  // Converting move constructor from a same-signature sibling of another
  // policy, transplanting the stored callable rather than nesting the wrapper.
  //
  // The callable lands inline when it fits this buffer and on the heap when
  // this policy admits one. A callable that can do neither throws
  // `std::length_error`, leaving the source intact, and a boxing
  // allocation can throw `std::bad_alloc` likewise. Neither throw is reachable
  // when every callable the source policy admits is guaranteed a home here,
  // and then the constructor is `noexcept`.
  template<invocable_policy P>
  requires(P != Policy)
  flexi_function(flexi_function<Sig, P>&& other) noexcept(
      !details::adopt_may_throw(Policy, P)) {
    do_adopt(other);
  }

  ~flexi_function() noexcept {
    if (dispatch_.lifespan) dispatch_.lifespan(&storage_area_, nullptr);
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
  flexi_function& operator=(flexi_function<Sig, P>&& other) noexcept(
      !details::adopt_may_throw(Policy, P)) {
    if constexpr (details::adopt_may_throw(Policy, P)) {
      if (!can_adopt(other))
        throw std::length_error{"flexi_function: callable too large"};
    }
    reset();
    do_adopt(other);
    return *this;
  }

  // `operator=` to replace the stored callable with `fn`.
  //
  // This stores `fn` directly, avoiding a transplant through a temporary
  // sibling. The std wrappers are excluded here as they are for construction;
  // wrap one explicitly and move-assign the result.
  //
  // A throw leaves this instance empty.
  template<Consumable FN>
  requires(
      thunks::template is_invocable<std::decay_t<FN>> &&
      !is_std_function_wrapper_v<std::decay_t<FN>> &&
      !is_flexi_function_v<std::decay_t<FN>>)
  flexi_function& operator=(FN&& fn) noexcept(
      details::can_store_nothrow<std::decay_t<FN>>(Policy)) {
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
    dispatch_.lifespan(&storage_area_, nullptr);
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
  static constexpr bool is_callable_through =
      (thunks::traits::is_const ||
          !std::is_const_v<std::remove_reference_t<Self>>) &&
      ((thunks::traits::ref_qualifier == ref_qual::none) ||
          ((thunks::traits::ref_qualifier == ref_qual::lvalue) ==
              std::is_lvalue_reference_v<Self>));

  // Call operator to invoke through the type-erased invoke thunk.
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
  requires(is_callable_through<Self>)
  ResultT
  operator()(this Self&& self, Args... args) noexcept(thunks::is_noexcept) {
    return self.dispatch_.invoke(
        const_cast<storage_area_t*>(&self.storage_area_),
        std::forward<Args>(args)...);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] explicit operator bool() const noexcept {
    return dispatch_.lifespan;
  }

  // Size of the stored callable in bytes; 0 when empty, and also for an
  // `storage_mode::direct` callable, which is not stored.
  [[nodiscard]] size_t size() const noexcept {
    return dispatch_.lifespan ? dispatch_.lifespan(nullptr, nullptr) : 0;
  }

  // Capacity of the inline storage in bytes, 0 for a `heap_only` policy.
  [[nodiscard]] size_t capacity() const noexcept { return inline_size; }

  // `can_adopt` is whether this `flexi_function` type can accommodate
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
  can_adopt(const flexi_function<Sig, P>& source) noexcept {
    if constexpr (Policy.admits_heap()) {
      return true;
    } else {
      if (!source.dispatch_.lifespan) return true;
      destination_spec probe{.policy = Policy,
          .dispatch = nullptr,
          .to = nullptr,
          .is_inline_fit_guaranteed =
              details::is_inline_fit_guaranteed(Policy, P)};
      // The probe only reads, but the erased signature is mutable.
      auto& probed = const_cast<flexi_function<Sig, P>&>(source);
      return (probed.dispatch_.lifespan(&probed.storage_area_, &probe) !=
              thunks::refusal);
    }
  }

#pragma endregion
#pragma region Implementation
private:
  // Whether `fn` is a null callable, which yields an empty wrapper rather
  // than a truthy shell that is undefined (or throws) when called, matching
  // `std::function`.
  //
  // The one list of nullable kinds: pointers, member pointers, this family's
  // wrappers, `runtime_fn`, and the std polymorphic wrappers.
  template<class FD>
  static bool is_null_callable(const FD& fn) noexcept {
    if constexpr (std::is_pointer_v<FD> || std::is_member_pointer_v<FD> ||
                  is_flexi_function_v<FD> || is_runtime_fn_v<FD> ||
                  is_std_function_wrapper_v<FD>)
      return !fn;
    else
      return false;
  }

  // `do_store` to consume `fn` into storage and publish its thunks.
  //
  // `FD` is the stored type, already decayed and always passed explicitly;
  // `fn` is forwarded into it, so an rvalue is moved and a (trivially
  // copyable) lvalue is copied. An `storage_mode::direct` `fn` is not
  // consumed at all: only its type survives, in the thunks.
  //
  // The caller handles any null-callable special case before calling, and
  // the instance is empty on entry.
  template<class FD, class FN>
  void do_store(FN&& fn) noexcept(details::can_store_nothrow<FD>(Policy)) {
    static_assert(!std::is_reference_v<FD>,
        "flexi_function: do_store requires the decayed stored type");
    static_assert(!std::is_function_v<std::remove_reference_t<FN>>,
        "flexi_function: a function name is a compile-time target. Wrap it as "
        "constant_fn<f>{} to call it directly with nothing stored, or, when "
        "the reference is a runtime value (it came through a forwarding "
        "parameter or a conditional), take its address to store it as a "
        "pointer");
    static_assert((Policy.enforcement == policy_enforcement::lenient) ||
                      !(std::is_pointer_v<FD> || std::is_member_pointer_v<FD>),
        "flexi_function: under strict enforcement a raw function or member "
        "pointer must be wrapped: constant_fn<f>{} when the target is known "
        "at compile time (a direct call, nothing stored), or runtime_fn{p} to "
        "store the pointer and call through it");
    static_assert(Policy.admits_heap() || details::is_direct_eligible<FD>() ||
                      details::is_inline_eligible<FD>(Policy),
        "flexi_function: the callable is not eligible for an inline_only "
        "instance's inline buffer: too large, over-aligned, or its move "
        "constructor may throw (a direct target needs no buffer)");
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

    constexpr auto mode = details::function_storage_mode_of<FD>(Policy);
    if constexpr (mode == storage_mode::inlined)
      new (storage_area_.buf) FD(std::forward<FN>(fn));
    else if constexpr (mode == storage_mode::dynamic)
      storage_area_.ptr = new FD(std::forward<FN>(fn));
    dispatch_ = thunks::template dispatch_for<FD, mode>();
  }

  // Take over `other`'s callable, when it has one.
  //
  // This instance must be empty on entry, and `other` is left empty.
  template<invocable_policy P>
  void do_adopt(flexi_function<Sig, P>& other) {
    using other_t = flexi_function<Sig, P>;
    assert(!dispatch_.lifespan);
    if (!other.dispatch_.lifespan) return;
    destination_spec dest{.policy = Policy,
        .dispatch = &dispatch_,
        .to = &storage_area_,
        .is_inline_fit_guaranteed =
            details::is_inline_fit_guaranteed(Policy, P)};
    [[maybe_unused]] const auto is_refused =
        (other.dispatch_.lifespan(&other.storage_area_, &dest) ==
            thunks::refusal);
    if constexpr (details::adopt_may_throw(Policy, P)) {
      if (is_refused)
        throw std::length_error{"flexi_function: callable too large"};
    } else {
      assert(!is_refused);
    }
    other.dispatch_ = other_t::empty_dispatch;
  }

  // The empty state, which is this type's compile-time empty invoker, and no
  // lifespan thunk.
  static constexpr thunk_pair empty_dispatch{
      &thunks::template empty_invoke_impl<Policy.empty>, nullptr};

#pragma endregion
#pragma region Data members

  thunk_pair dispatch_ = empty_dispatch;

  // Deliberately no initializer: occupancy and the live member are keyed by
  // `dispatch_.lifespan`, and zeroing the buffer on every construction would
  // be pure waste. Under an empty `inline_only` buffer (a direct-only
  // wrapper) the area is an empty type and takes no space.
  CORVID_NO_UNIQUE_ADDRESS storage_area_t storage_area_;

#pragma endregion
};

#pragma endregion
} // namespace flexi

#pragma region Exports
// Call-site vocabulary, exported to `corvid::meta`; see invocable_policy.h
// for the rule.
using flexi::flexi_function;
using flexi::is_flexi_function_v;
#pragma endregion
}} // namespace corvid::meta
