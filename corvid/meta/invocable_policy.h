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
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "meta_enums.h"

namespace corvid { inline namespace meta {

// `invocable_policy` and related types are used by owning type-erased handles,
// such as `proxy` and `flexi_function`, to determine how to store and invoke
// their targets.
//
// A handle type is specialized on a policy, and that becomes the default for
// its instances. Some aspects of the policy can be overridden at runtime, such
// as whether invoking an empty handle throws or returns a value-initialized
// result.

#pragma region Invocable alloc

// `invocable_alloc` is the allocation strategy.
//
// `sbo_or_heap` stores a target inline when it is eligible and efficient (see
// `invocable_policy`), and on the heap otherwise.
//
// `sbo_only` forbids the heap path: constructing a handle over an ineligible
// target is a compile error. An erased target arriving through a converting
// move must end up in the buffer (a heap arrival is un-boxed into it), and the
// adoption throws `std::length_error` when the target does not fit or cannot
// move inline at all, the one runtime failure, since an erased arrival cannot
// be checked statically.
//
// `heap_only` forbids the inline path, so every target has a stable heap
// address, and the handle carries no inline buffer at all; an inline target
// arriving through a converting move is re-boxed onto the heap. With no buffer
// for them to size, `sbo_size` and `sbo_align` are ignored; leave them at
// their defaults rather than spelling out a zero.
enum class invocable_alloc : std::uint8_t {
  sbo_only = 1 << 0,
  heap_only = 1 << 1,
  sbo_or_heap = sbo_only | heap_only
};

#pragma endregion
#pragma region invocable_policy

// `invocable_policy` is the combined policy, which is used as a template
// parameter.
//
// The storage members default to a two-pointer inline buffer at
// `std::max_align_t` alignment, falling back to the heap. A handle whose
// typical targets are a little too big for the default buffer can be handled
// with a larger `sbo_size`.
//
// The `sbo_size` must be a multiple of `sbo_align` (except when `heap_only`,
// which ignores `sbo_size`) because a smaller value would occupy the padded
// size anyway and waste the difference. Instead of hardcoding a number that
// might only be valid on a particular platform, you should pass the size
// through `padded_size` to get a conforming value.
//
// A target is stored inline when it fits `sbo_size` and `sbo_align` and is
// nothrow-move-constructible (a handle move relocates an inline target, and
// handle moves are unconditionally `noexcept`). The exception is when the
// source is on the heap and the policy allows heap storage, in which case only
// the pointer is moved.
//
// `empty` selects what invoking an empty handle (default-constructed,
// moved-from, or reset) does: `raise` throws `std::bad_function_call`, as
// `std::function` does; `ignore` returns a value-initialized result. A result
// type that cannot be value-initialized (a reference, or a type without a
// default constructor) falls back to `raise` for that call, which lets one
// policy serve a facade whose methods differ in this respect. This member is
// the compile-time default; the handle's `reset(on_failure)` overrides it at
// runtime.
//
// For `proxy`, policies are checked at proxy construction, not at
// registration. Registration is per (facade, type) and knows nothing about
// any particular handle's storage. One facade can serve proxies of different
// policies, and views, simultaneously.
struct invocable_policy {
  size_t sbo_size = 2 * sizeof(void*);
  size_t sbo_align = alignof(std::max_align_t);
  invocable_alloc alloc = invocable_alloc::sbo_or_heap;
  on_failure empty = on_failure::raise;

  friend constexpr bool
  operator==(const invocable_policy&, const invocable_policy&) = default;
};

namespace policy_details {

// `sbo_fits`: whether `T` is eligible for policy `P`'s inline buffer.
template<typename T>
consteval bool sbo_fits(invocable_policy p) noexcept {
  return sizeof(T) <= p.sbo_size && alignof(T) <= p.sbo_align &&
         std::is_nothrow_move_constructible_v<T>;
}

// `can_store_inline`: whether policy `P` can store `T` inline.
//
// An `sbo_only` policy over an ineligible target is rejected separately, with
// its own diagnostic.
template<typename T>
consteval bool can_store_inline(invocable_policy p) noexcept {
  return p.alloc != invocable_alloc::heap_only && sbo_fits<T>(p);
}

// `inline_fit_guaranteed`: whether every inline target the source policy
// admits is guaranteed to fit the destination's buffer, letting adoption skip
// the runtime fit check (and, with it, every mode-changing path for inline
// arrivals).
consteval bool
inline_fit_guaranteed(invocable_policy to, invocable_policy from) noexcept {
  return to.alloc != invocable_alloc::heap_only &&
         to.sbo_size >= from.sbo_size && to.sbo_align >= from.sbo_align;
}

// `adopt_may_throw`: whether adopting from policy `from` into policy `to` can
// throw.
//
// Could be an inline arrival that might not stay inline (a re-boxing
// allocation, or nowhere at all to put it under `sbo_only`), or a heap arrival
// that must un-box into an `sbo_only` buffer it might not fit.
consteval bool
adopt_may_throw(invocable_policy to, invocable_policy from) noexcept {
  const auto from_sbo = (from.alloc != invocable_alloc::heap_only);
  const auto from_heap = (from.alloc != invocable_alloc::sbo_only);
  return (from_sbo && !inline_fit_guaranteed(to, from)) ||
         (from_heap && to.alloc == invocable_alloc::sbo_only);
}

} // namespace policy_details

#pragma endregion
}} // namespace corvid::meta
