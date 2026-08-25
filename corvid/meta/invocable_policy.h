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
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "bool_enums.h"

namespace corvid { inline namespace meta {

// `invocable_policy` and related types are used by owning type-erased types,
// such as `proxy` and `flexi_function`, to determine how to store and invoke
// their targets. "Owner" below stands for whichever of these is specialized
// on the policy.
//
// An owner type is specialized on a policy, which fixes these choices for
// all of its instances at compile time.

#pragma region Invocable alloc

// `invocable_alloc` is the allocation strategy.
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
enum class invocable_alloc : std::uint8_t {
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
// By design, termination on an empty call is never reached by accident; it has
// to be asked for by name.
enum class on_empty : std::uint8_t { silent, raise, terminate };

#pragma endregion
#pragma region Allocation mode

// `allocation_mode` is how an owner's invoke thunk reaches its target, and
// therefore where the target lives.
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
// `direct_eligible`), such as a captureless lambda or a `std::plus<>`. Such
// a target occupies no storage under any policy, `heap_only` included, and
// reports a size of 0.
//
// Unlike `invocable_alloc`, which is a policy choice, this is the outcome for
// one target, and it is what the owner's thunks are keyed on.
enum class allocation_mode : std::uint8_t { inlined, dynamic, direct };

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
// owner's type; it cannot be changed at runtime.
//
// A result type that cannot be value-initialized (a reference, or a type
// without a default constructor) cannot be `silent`. For `flexi_function`,
// whose one signature is chosen deliberately, that is a compile error naming
// `raise` and `terminate` as the alternatives. For `proxy`, whose facade may
// mix such methods with ordinary ones, the call falls back to `raise`, which
// lets one policy serve the whole facade.
//
// For `proxy`, policies are checked at proxy construction, not at
// registration. Registration is per (facade, type) and knows nothing about
// any particular proxy's storage. One facade can serve proxies of different
// policies, and views, simultaneously.
struct invocable_policy {
  size_t inline_size = 2 * sizeof(void*);
  size_t inline_align = alignof(std::max_align_t);
  invocable_alloc alloc = invocable_alloc::inline_or_heap;
  on_empty empty = on_empty::raise;

  friend constexpr bool
  operator==(const invocable_policy&, const invocable_policy&) = default;
};

namespace policy_details {

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
  return (p.alloc != invocable_alloc::heap_only) && inline_eligible<T>(p);
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

// `storage_mode`: where policy `p` keeps a `T`: nowhere when it is direct
// eligible, inline when it can be, and on the heap otherwise.
//
// An `inline_only` policy over a target that can be neither is rejected
// separately, with its own diagnostic.
template<typename T>
consteval allocation_mode storage_mode(invocable_policy p) noexcept {
  if (direct_eligible<T>()) return allocation_mode::direct;
  if (can_store_inline<T>(p)) return allocation_mode::inlined;
  return allocation_mode::dynamic;
}

// `can_store_nothrow`: whether storing a `T` under policy `p` cannot throw,
// which is whenever it does not go to the heap.
template<typename T>
consteval bool can_store_nothrow(invocable_policy p) noexcept {
  return (storage_mode<T>(p) != allocation_mode::dynamic);
}

// `inline_fit_guaranteed`: whether every inline target the source policy
// admits is guaranteed to fit the destination's buffer, letting adoption skip
// the runtime fit check (and, with it, every mode-changing path for inline
// arrivals).
consteval bool
inline_fit_guaranteed(invocable_policy to, invocable_policy from) noexcept {
  return (to.alloc != invocable_alloc::heap_only) &&
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
  const auto from_inline = (from.alloc != invocable_alloc::heap_only);
  const auto from_heap = (from.alloc != invocable_alloc::inline_only);
  return (from_inline && !inline_fit_guaranteed(to, from)) ||
         (from_heap && (to.alloc == invocable_alloc::inline_only));
}

} // namespace policy_details

#pragma endregion
}} // namespace corvid::meta
