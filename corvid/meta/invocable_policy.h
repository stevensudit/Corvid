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

#include "bool_enums.h"
#include "padding.h"

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
enum class storage_policy : std::uint8_t {
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
enum class on_empty : std::uint8_t { silent, raise, terminate };

#pragma endregion
#pragma region Allocation mode

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
// `direct_eligible`), such as a captureless lambda or a stateless functor like
// `std::plus<>`. Such a target occupies no storage under any policy,
// `heap_only` included, and reports a size of 0.
//
// Unlike `storage_policy`, which is a policy choice, this is the outcome for
// one target, and it is what the owner's thunks are keyed on.
enum class storage_mode : std::uint8_t { direct, inlined, dynamic };

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
// by its value's type, `with_alignment` sets the buffer alignment, and
// `with_size` and `with_storage_size` resize the buffer by the instance size
// or the buffer size, rounding up to the buffer alignment so no padding is
// wasted (which means you must set the alignment before the size):
//
//   flexi_function<int(), invocable_policy::heap.with(on_empty::silent)>
//   proxy<F, invocable_policy::basic.with(policy_enforcement::strict)>
//   flexi_function<int(), invocable_policy::fixed.with_size(64)>
//   flexi_function<int(), invocable_policy::fixed.with_alignment(8)>
//
// The sizes assume `flexi_function`'s layout, two thunk pointers ahead of the
// buffer, which `size` reports for any policy. Designated initializers remain
// available for anything the fluent forms do not cover.
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

  // A copy whose buffer is aligned to `align`, a power of two, with the
  // buffer size rounded up to it so the instance stays padding-free.
  //
  // Set the alignment before the size: `with_size` pads to whatever
  // alignment is in force. The policy must have a buffer, enforced at compile
  // time.
  [[nodiscard]] consteval invocable_policy with_alignment(
      size_t align) const noexcept {
    if (storage == storage_policy::heap_only) needs_a_buffer();
    if (!std::has_single_bit(align)) must_be_a_power_of_two();
    auto p = *this;
    p.inline_align = align;
    p.inline_size = padded_size(inline_size, align);
    return p;
  }

  // A copy whose buffer fills out an instance of `sz` bytes, rounded up to
  // the buffer alignment.
  //
  // `sz` must exceed the two thunk pointers, and the policy must have a
  // buffer, both enforced at compile time.
  [[nodiscard]] consteval invocable_policy with_size(
      size_t sz) const noexcept {
    if (storage == storage_policy::heap_only) needs_a_buffer();
    const auto total = padded_size(sz, inline_align);
    const auto header = padded_size(dispatch_size, inline_align);
    if (total <= header) must_exceed_two_pointers();
    auto p = *this;
    p.inline_size = total - header;
    return p;
  }

  // A copy whose buffer holds `sz` bytes, rounded up as `with_size` rounds.
  [[nodiscard]] consteval invocable_policy with_storage_size(
      size_t sz) const noexcept {
    return with_size(sz + padded_size(dispatch_size, inline_align));
  }

  // The instance size of an owner with this policy: the two thunk pointers
  // and the buffer, or the heap pointer under `heap_only`.
  [[nodiscard]] consteval size_t size() const noexcept {
    if (storage == storage_policy::heap_only)
      return dispatch_size + sizeof(void*);
    return padded_size(dispatch_size, inline_align) + inline_size;
  }

private:
  static constexpr size_t dispatch_size = 2 * sizeof(void*);

  // Not defined: naming one in a constant expression is how the buffer
  // members reject their arguments.
  static void needs_a_buffer();
  static void must_exceed_two_pointers();
  static void must_be_a_power_of_two();
};

inline constexpr invocable_policy invocable_policy::basic{};
inline constexpr invocable_policy invocable_policy::heap{
    .storage = storage_policy::heap_only};
inline constexpr invocable_policy invocable_policy::fixed{
    .storage = storage_policy::inline_only};

namespace details {

// `inline_eligible`: whether `T` is eligible for policy `P`'s inline buffer.
//
// `constexpr` rather than `consteval`: `flexi_function`'s lifespan thunk
// evaluates a type-erased destination's policy at runtime.
template<typename T>
constexpr bool inline_eligible(invocable_policy p) noexcept {
  return (sizeof(T) <= p.inline_size) && (alignof(T) <= p.inline_align) &&
         std::is_nothrow_move_constructible_v<T>;
}

// `can_store_inline`: whether policy `P` can store `T` inline.
//
// An `inline_only` policy over an ineligible target is rejected separately,
// with its own diagnostic.
template<typename T>
constexpr bool can_store_inline(invocable_policy p) noexcept {
  return (p.storage != storage_policy::heap_only) && inline_eligible<T>(p);
}

// `direct_eligible`: whether a `T` can be called without storing one, so that
// only its type survives, in the thunk generated for it.
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
consteval bool direct_eligible() noexcept {
  return (std::is_empty_v<T> && std::is_trivially_default_constructible_v<T> &&
          std::is_trivially_destructible_v<T>);
}

// `storage_mode_of`: where policy `p` keeps a `T`: nowhere when it is direct
// eligible, inline when it can be, and on the heap otherwise.
//
// An `inline_only` policy over a target that can be neither is rejected
// separately, with its own diagnostic.
template<typename T>
consteval storage_mode storage_mode_of(invocable_policy p) noexcept {
  if (direct_eligible<T>()) return storage_mode::direct;
  if (can_store_inline<T>(p)) return storage_mode::inlined;
  return storage_mode::dynamic;
}

// `can_store_nothrow`: whether storing a `T` under policy `p` cannot throw,
// which is whenever it does not go to the heap.
template<typename T>
consteval bool can_store_nothrow(invocable_policy p) noexcept {
  return (storage_mode_of<T>(p) != storage_mode::dynamic);
}

// `inline_fit_guaranteed`: whether every inline target the source policy
// admits is guaranteed to fit the destination's buffer, letting adoption skip
// the runtime fit check (and, with it, every mode-changing path for inline
// arrivals).
consteval bool
inline_fit_guaranteed(invocable_policy to, invocable_policy from) noexcept {
  return (to.storage != storage_policy::heap_only) &&
         (to.inline_size >= from.inline_size) &&
         (to.inline_align >= from.inline_align);
}

// `adopt_may_throw`: whether adopting from policy `from` into policy `to` can
// throw.
//
// Could be an inline arrival that might not stay inline (a boxing
// allocation, or nowhere at all to put it under `inline_only`), or a heap
// arrival that must un-box into an `inline_only` buffer it might not fit.
consteval bool
adopt_may_throw(invocable_policy to, invocable_policy from) noexcept {
  const auto from_inline = (from.storage != storage_policy::heap_only);
  const auto from_heap = (from.storage != storage_policy::inline_only);
  return (from_inline && !inline_fit_guaranteed(to, from)) ||
         (from_heap && (to.storage == storage_policy::inline_only));
}

} // namespace details

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
