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
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../bool_enums.h"
#include "../padding.h"

namespace corvid { inline namespace meta {
namespace invocables {

// `invocable_policy` and related types are used by owning type-erased types,
// such as `proxy` and `flexi_function`, to determine how to store and invoke
// their targets. "Owner" below stands for whichever of these is specialized
// on the policy.
//
// An owner type is specialized on a policy, which fixes these choices for
// all of its instances at compile time.

#pragma region Storage policy

// `storage_policy` is where a policy permits its owner to store a target.
//
// `inline_or_heap` stores a target inline when it is eligible and efficient
// (see `invocable_policy`), and on the heap otherwise.
//
// `inline_only` forbids the heap path, so that constructing an owner over an
// ineligible target is a compile error. An erased target arriving through a
// converting move must end up in the buffer (a heap arrival is un-boxed into
// it), and the adoption throws `std::length_error` when the target does not
// fit or cannot move inline at all. This is the one runtime failure, since an
// erased arrival cannot be checked statically.
//
// `heap_only` forbids the inline path, so every target has a stable heap
// address, and the owner carries no inline buffer at all, past the pointer
// into the heap block it owns. An inline target arriving through a converting
// move is boxed onto the heap. With no buffer for them to size, `inline_size`
// and `inline_align` are ignored; leave them at their defaults rather than
// spelling out a zero.
enum class storage_policy : uint8_t {
  inline_only = 1 << 0,
  heap_only = 1 << 1,
  inline_or_heap = inline_only | heap_only
};

#pragma endregion
#pragma region On empty

// `on_empty` is what invoking an empty owner does.
//
// `silent` returns a value-initialized result (or nothing, for `void`).
// Invalid when the result cannot be value-initialized, and, when the call
// operator is `noexcept`, when that value-initialization can throw.
//
// `raise` throws `std::bad_function_call`, as `std::function` does. Invalid
// when the call operator is `noexcept`.
//
// `terminate` calls `std::terminate`. Always valid, and the only choice when
// the call operator is `noexcept` and the result cannot be value-initialized
// without throwing.
//
// `flexi_function` admits only a behavior its one signature supports, so
// termination is never reached there by accident. `proxy` treats the value as
// a floor per method, since a facade mixes methods; see
// `invocable_policy::empty`.
enum class on_empty : uint8_t { silent, raise, terminate };

#pragma endregion
#pragma region Storage mode

// `storage_mode` is where an owner keeps its target, which is also how the
// owner's invoke thunk reaches it.
//
// Normally the owner's `invoke` slot points at a thunk that first finds the
// target in the owner's storage and then calls it. Under `inlined` the target
// is constructed in the owner's buffer, and under `dynamic` the buffer holds a
// pointer to the target's heap block, one more dereference away.
//
// Under `direct` there is no finding step. The `invoke` slot points at a
// thunk whose body is the call itself, which the optimizer collapses into a
// direct call to the target's code, and the buffer is never read. This is
// possible only for a type that carries nothing per instance: no data
// members, and trivial default construction and destruction (see
// `is_direct_eligible`), such as a captureless lambda or a stateless functor
// like `std::plus<>`. Such a target occupies no storage under any policy,
// `heap_only` included, and reports a size of 0.
//
// Unlike `storage_policy`, which is a policy choice, this is the outcome for
// one target, and it is what the owner's thunks are keyed on.
enum class storage_mode : uint8_t { direct, inlined, dynamic };

#pragma endregion
#pragma region Adoption

// `adoption` is what becomes of a target arriving from a sibling owner: the
// route it takes into the destination's storage, or a refusal.
//
// `relocate` moves an inline arrival into the destination's buffer, and `box`
// moves one onto the heap. `hand_over` passes a heap arrival's block to the
// destination as is, and `unbox` moves one into the destination's buffer and
// frees the block. `refuse` is the answer when the destination admits no home
// the arrival can take. The rule that picks the route is `adoption_of`.
enum class adoption : uint8_t { relocate, box, hand_over, unbox, refuse };

#pragma endregion
#pragma region invocable_policy

// `invocable_policy` is the combined policy, which is used as a template
// parameter.
//
// The storage members default to a two-pointer inline buffer at
// `std::max_align_t` alignment, falling back to the heap. An owner whose
// typical targets are a little too big for the default buffer can be given
// a larger `inline_size`.
//
// The `inline_size` must be a multiple of `inline_align` (except when
// `heap_only`, which ignores `inline_size`) because a smaller value would
// occupy the padded size anyway and waste the difference. Instead of
// hardcoding a number that might only be valid on a particular platform, you
// should pass the size through `padded_size` to get a conforming value.
//
// A target is stored inline when it fits `inline_size` and `inline_align` and
// is nothrow-move-constructible (an owner move relocates an inline target, and
// owner moves are unconditionally `noexcept`). The exception is when the
// source is on the heap and the policy allows heap storage, in which case only
// the pointer is moved.
//
// `empty` selects what invoking an empty owner (default-constructed,
// moved-from, or reset) does; see `on_empty`. The behavior is baked into the
// owner's type; it cannot be changed at runtime. For `flexi_function`, whose
// one signature is chosen deliberately, a behavior the signature does not
// admit is a compile error naming the alternatives. For `proxy`, whose facade
// mixes methods, the value is a floor, and each method takes the mildest
// behavior at or above it that its own signature admits, so `silent` falls
// back to `raise` for a result that cannot be value-initialized, and either
// falls back to `terminate` for a `noexcept` method.
//
// `enforcement` selects lenient or strict enforcement of the policy; see
// `policy_enforcement`. What strictness rejects depends on the owner. For
// `flexi_function` it is a raw function or member pointer as a target, whose
// value is runtime data and costs a second indirect call, so the caller must
// say which kind of target it is: `constant_fn<f>{}` for one known at compile
// time (called directly, nothing stored), `runtime_fn{p}` for one that really
// is a runtime value. The check is at the border only, where a callable is
// stored; a move from a sibling transplants whatever it held, unchecked. For
// `proxy` it is any method that would take a behavior other than `empty`
// itself. Flipping the default to `strict` and rebuilding is a one-edit
// audit.
//
// For `proxy`, policies are checked at proxy construction, not at
// registration. Registration is per (facade, type) and knows nothing about
// any particular proxy's storage. One facade can serve proxies of different
// policies, and views, simultaneously.
//
// A policy is usually spelled fluently, from one of three starting points:
// `basic` (the default, `inline_or_heap`), `heap` (`heap_only`), and `fixed`
// (`inline_only`), each with the default buffer. `with` replaces one member
// by its value's type, and `with_storage_size` resizes the buffer, at its
// current alignment or at one given alongside, rounding the size up to the
// alignment so no padding is wasted:
//
//   flexi_function<int(), invocable_policy::heap.with(on_empty::silent)>
//   proxy<F, invocable_policy::basic.with(policy_enforcement::strict)>
//   flexi_function<int(), invocable_policy::fixed.with_storage_size(48)>
//   flexi_function<int(), invocable_policy::fixed.with_storage_size(16, 8)>
//
// The policy describes the buffer alone. What an owner keeps ahead of it is
// the owner's business, so `sizeof` the owner is the only instance size.
// Designated initializers remain available for anything the fluent forms do
// not cover; `is_well_formed` is what every owner checks either way.
struct invocable_policy {
  size_t inline_size = 2 * sizeof(void*);
  size_t inline_align = alignof(std::max_align_t);
  storage_policy storage = storage_policy::inline_or_heap;
  on_empty empty = on_empty::raise;
  policy_enforcement enforcement = policy_enforcement::lenient;

  friend constexpr bool
  operator==(const invocable_policy&, const invocable_policy&) = default;

  // The starting points, defined after the class.
  static const invocable_policy basic;
  static const invocable_policy heap;
  static const invocable_policy fixed;

  // A copy with `empty` replaced.
  [[nodiscard]] consteval invocable_policy with(on_empty e) const noexcept {
    auto p = *this;
    p.empty = e;
    return p;
  }

  // A copy with `enforcement` replaced.
  [[nodiscard]] consteval invocable_policy with(
      policy_enforcement e) const noexcept {
    auto p = *this;
    p.enforcement = e;
    return p;
  }

  // A copy whose buffer holds `sz` bytes, rounded up to the buffer alignment
  // so no padding is wasted: the current alignment, or `align` (a power of
  // two), which is set together with the size so that neither is rounded
  // against a stale value of the other. The policy must have a buffer,
  // enforced at compile time. Zero is allowed: an `inline_only` policy with
  // an empty buffer serves only `direct` targets, which store nothing (see
  // `is_well_formed`).
  [[nodiscard]] consteval invocable_policy with_storage_size(
      size_t sz) const noexcept {
    return with_storage_size(sz, inline_align);
  }
  [[nodiscard]] consteval invocable_policy
  with_storage_size(size_t sz, size_t align) const noexcept {
    if (!admits_inline()) needs_a_buffer();
    if (!std::has_single_bit(align)) must_be_a_power_of_two();
    auto p = *this;
    p.inline_align = align;
    p.inline_size = padded_size(sz, align);
    return p;
  }

  // Whether the policy permits storing a target inline, or on the heap.
  //
  // The two questions every owner asks of its policy, in place of comparing
  // `storage` against the value that forbids each. `storage_policy`'s bit
  // layout says the same thing: `inline_only` and `heap_only` are the two
  // bits, and `inline_or_heap` is both.
  //
  // `constexpr` rather than `consteval` because `flexi_function`'s lifespan
  // thunk asks a type-erased destination's policy at runtime.
  [[nodiscard]] constexpr bool admits_inline() const noexcept {
    return (storage != storage_policy::heap_only);
  }
  [[nodiscard]] constexpr bool admits_heap() const noexcept {
    return (storage != storage_policy::inline_only);
  }

  // The geometry of the buffer an owner actually keeps. This is the policy's
  // inline buffer, or, under `heap_only`, just the pointer to the heap block,
  // so that the whole owner is as small as a view.
  [[nodiscard]] consteval size_t buffer_size() const noexcept {
    return admits_inline() ? inline_size : sizeof(void*);
  }
  [[nodiscard]] consteval size_t buffer_align() const noexcept {
    return admits_inline() ? inline_align : alignof(void*);
  }

  // `is_well_formed` determines whether an owner can honor the buffer this
  // policy describes, which every owner asserts.
  //
  // True, or not a constant expression at all: a broken rule is reported by
  // naming it, on the pattern of the fluent setters.
  //
  // The rules: the alignment is a power of two; the size is a multiple of it,
  // since the difference would be wasted as padding; and, because the buffer
  // overlays the pointer to a heap block (see `storage_area`), a buffer that
  // can be asked to hold one is at least a pointer's size and alignment. Only
  // an `inline_only` buffer may be empty; a smaller one would silently grow to
  // the pointer it overlays.
  //
  // The multiple-of-alignment rule costs nothing: `storage_area` is a union,
  // whose size rounds up to its alignment regardless, so a smaller
  // `inline_size` would only underreport the buffer it already has. An
  // alignment larger than the size is pointless for the same reason a type's
  // size is a multiple of its alignment, which is that no type that needs the
  // alignment could fit.
  [[nodiscard]] consteval bool is_well_formed() const noexcept {
    if (!admits_inline()) return true;
    if (!std::has_single_bit(inline_align)) must_be_a_power_of_two();
    if (inline_size != padded_size(inline_size, inline_align))
      must_be_a_multiple_of_the_alignment();
    if (inline_size == 0) {
      if (admits_heap()) must_hold_a_pointer();
      return true;
    }
    if (inline_size < sizeof(void*) || inline_align < alignof(void*))
      must_be_pointer_sized_or_empty();
    return true;
  }

private:
  // Not defined: naming one in a constant expression is how the buffer
  // members and `is_well_formed` reject what they are given.
  static void needs_a_buffer();
  static void must_be_a_power_of_two();
  static void must_be_a_multiple_of_the_alignment();
  static void must_hold_a_pointer();
  static void must_be_pointer_sized_or_empty();
};

inline constexpr invocable_policy invocable_policy::basic{};
inline constexpr invocable_policy invocable_policy::heap{
    .storage = storage_policy::heap_only};
inline constexpr invocable_policy invocable_policy::fixed{
    .storage = storage_policy::inline_only};

// This is called `implementation`, not `details` because these are not
// private to this file. Rather, they are working parts an owner builds on, and
// are imported into the owner's own `details` wholesale.
namespace implementation {

// Whether a target of the given size and alignment, whose move cannot throw
// when `is_nothrow_move`, is eligible for policy `p`'s inline buffer.
//
// The three conditions of inline eligibility, over values, so that the same
// test serves a type (`is_inline_eligible`) and an erased arrival whose
// footprint is known only at runtime (`adoption_of`).
//
// `constexpr` rather than `consteval` because `flexi_function`'s lifespan
// thunk evaluates a type-erased destination's policy at runtime.
constexpr bool fits_inline(invocable_policy p, size_t size, size_t align,
    bool is_nothrow_move) noexcept {
  return (size <= p.inline_size) && (align <= p.inline_align) &&
         is_nothrow_move;
}

// Whether `T` is eligible for policy `p`'s inline buffer, as `fits_inline`
// decides.
template<typename T>
constexpr bool is_inline_eligible(invocable_policy p) noexcept {
  return fits_inline(p, sizeof(T), alignof(T),
      std::is_nothrow_move_constructible_v<T>);
}

// Whether policy `p` can store `T` inline.
//
// An `inline_only` policy over an ineligible target is rejected separately,
// with its own diagnostic.
template<typename T>
constexpr bool can_store_inline(invocable_policy p) noexcept {
  return p.admits_inline() && is_inline_eligible<T>(p);
}

// Whether a `T` can be called without storing one, so that only its type
// survives, in the thunk generated for it.
//
// No data members (which is what `std::is_empty` checks, along with no
// virtual functions) rules out per-instance state such as a capture or a
// bound pointer. Trivial default construction rules out a constructor with
// side effects (a counter, a registration), and trivial destruction a
// destructor with them. Together they make the instance the thunk names to
// call `operator()` a formality with no runtime presence.
//
// No policy parameter, because no policy affects the answer.
template<typename T>
consteval bool is_direct_eligible() noexcept {
  return (std::is_empty_v<T> && std::is_trivially_default_constructible_v<T> &&
          std::is_trivially_destructible_v<T>);
}

// Where policy `p` keeps a `T`: nowhere when it is direct eligible, inline
// when it can be, and on the heap otherwise.
//
// An `inline_only` policy over a target that can be neither is rejected
// separately, with its own diagnostic.
template<typename T>
consteval storage_mode function_storage_mode_of(invocable_policy p) noexcept {
  if (is_direct_eligible<T>()) return storage_mode::direct;
  if (can_store_inline<T>(p)) return storage_mode::inlined;
  return storage_mode::dynamic;
}

// Whether storing a `T` under policy `p` cannot throw, which is whenever it
// does not go to the heap.
template<typename T>
consteval bool can_store_nothrow(invocable_policy p) noexcept {
  return (function_storage_mode_of<T>(p) != storage_mode::dynamic);
}

// Whether every inline target the source policy admits is guaranteed to fit
// the destination's buffer, letting adoption skip the runtime fit check (and,
// with it, every mode-changing path for inline arrivals).
consteval bool
is_inline_fit_guaranteed(invocable_policy to, invocable_policy from) noexcept {
  return to.admits_inline() && (to.inline_size >= from.inline_size) &&
         (to.inline_align >= from.inline_align);
}

// Whether adopting from policy `from` into policy `to` can throw.
//
// Could be an inline arrival that might not stay inline (a boxing
// allocation, or nowhere at all to put it under `inline_only`), or a heap
// arrival that must un-box into an `inline_only` buffer it might not fit.
consteval bool
adopt_may_throw(invocable_policy to, invocable_policy from) noexcept {
  return (from.admits_inline() && !is_inline_fit_guaranteed(to, from)) ||
         (from.admits_heap() && !to.admits_heap());
}

// `adoption_of` is the route a target of the given size and alignment, whose
// move cannot throw when `is_nothrow_move`, takes into policy `to`, arriving
// from storage mode `from`.
//
// The one statement of the adoption rule. A heap arrival is handed over
// whenever `to` admits the heap, never un-boxed, since the allocation is
// already paid for; it is un-boxed only into an `inline_only` buffer it fits.
// An inline arrival stays inline when it fits and is boxed otherwise. When no
// home is open, the arrival is refused. "Fits" is `fits_inline`.
//
// A `direct` arrival is not routed here: it has no storage to move, and every
// destination accepts it.
//
// `constexpr` rather than `consteval`: an owner may ask at runtime, with a
// type-erased destination's policy or an erased arrival's footprint.
constexpr adoption adoption_of(invocable_policy to, storage_mode from,
    size_t size, size_t align, bool is_nothrow_move) noexcept {
  const auto does_fit =
      to.admits_inline() && fits_inline(to, size, align, is_nothrow_move);
  if (from == storage_mode::dynamic) {
    if (to.admits_heap()) return adoption::hand_over;
    return does_fit ? adoption::unbox : adoption::refuse;
  }
  if (does_fit) return adoption::relocate;
  return to.admits_heap() ? adoption::box : adoption::refuse;
}

} // namespace implementation

#pragma endregion
} // namespace invocables

#pragma region Exports
// Call-site vocabulary, exported to `corvid::meta`.
//
// A name is exported when a caller spells it at a call site, and it then
// carries its own qualifier, since the wider namespace no longer supplies
// one. The rest of the vocabulary leans on `invocables::` and stays home;
// `flexi_function` and `proxy` see all of it through a using-directive, and
// a caller reaches the storage choices through `invocable_policy`'s fluent
// starting points.
using invocables::invocable_policy;
using invocables::on_empty;
#pragma endregion
}} // namespace corvid::meta
