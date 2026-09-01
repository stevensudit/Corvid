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
#include <exception>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include "invocable_policy.h"
#include "../traits.h"

namespace corvid { inline namespace meta {
namespace invocables {

// What an owner of an invocable target (such as `flexi_function` or `proxy`)
// needs beyond the `invocable_policy`: the rules for a call on an empty owner,
// the housekeeping on a stored target, and the spellings for a function
// target.
//
// `empty_call_traits<R>` answers, for a result type, which `on_empty`
// behaviors a call can take, and performs the empty call itself, so that both
// owners' empty invokers follow one rule in one place.
//
// The `storage_area` is the raw storage an owner keeps, and the housekeeping
// (`destroy_inline`, `destroy_heap`, `relocate_inline`, `box`, `unbox`) is
// what an owner does to a target in either of its homes, over the erased
// address of that storage.
//
// `constant_fn<f>{}` names a compile-time target, called directly with nothing
// stored, and `runtime_fn{p}` names a pointer that really is a runtime value;
// see `policy_enforcement` for when an owner insists on the choice.

#pragma region Empty calls

// What a call on an empty owner can do with a result of type `R`.
//
// `on_empty::silent` needs a result that can be value-initialized (or is
// `void`), and one whose value-initialization cannot throw when the call is
// `noexcept`; `raise` needs a call that may throw; `terminate` needs nothing.
// `admits` asks about one behavior exactly, `resolve_floor` finds the mildest
// at or above a floor, and `invoke` performs the call.
template<class R>
struct empty_call_traits {
  // Whether `R` can be value-initialized, or is `void`.
  static constexpr bool is_silenceable =
      (std::is_void_v<R> || std::is_default_constructible_v<R>);

  // Whether that value-initialization cannot throw; a subset of
  // `is_silenceable`.
  static constexpr bool is_nothrow_silenceable =
      (std::is_void_v<R> || std::is_nothrow_default_constructible_v<R>);

  // Whether `invoke<Behavior>` cannot throw.
  template<on_empty Behavior>
  static constexpr bool is_nothrow =
      (Behavior == on_empty::terminate) ||
      ((Behavior == on_empty::silent) && is_nothrow_silenceable);

  // Whether the call can take `behavior` exactly, given its `noexcept`
  // specifier `noex`.
  static consteval bool
  admits(on_empty behavior, noexcept_spec noex) noexcept {
    const auto may_throw = (noex == noexcept_spec::none);
    if (behavior == on_empty::silent)
      return is_silenceable && (may_throw || is_nothrow_silenceable);
    if (behavior == on_empty::raise) return may_throw;
    return true;
  }

  // The mildest behavior at or above `floor` that the call admits,
  // `terminate` being the ceiling.
  static consteval on_empty
  resolve_floor(on_empty floor, noexcept_spec noex) noexcept {
    if ((floor == on_empty::silent) && admits(on_empty::silent, noex))
      return on_empty::silent;
    if ((floor != on_empty::terminate) && admits(on_empty::raise, noex))
      return on_empty::raise;
    return on_empty::terminate;
  }

  // Perform the empty call under `Behavior`.
  //
  // Either returns `R{}` (or nothing), throws `std::bad_function_call`, or
  // terminates.  The caller has already established that `Behavior` is
  // admitted.
  template<on_empty Behavior>
  static R invoke() noexcept(is_nothrow<Behavior>) {
    if constexpr (Behavior == on_empty::silent) {
      if constexpr (std::is_void_v<R>)
        return;
      else
        return R{};
    } else if constexpr (Behavior == on_empty::terminate) {
      std::terminate();
    } else {
      throw std::bad_function_call();
    }
  }
};

#pragma endregion
#pragma region Storage

namespace implementation {

// `storage_area` is the raw storage an owner keeps behind its dispatch state:
// a buffer for an inline target, overlaid by the pointer to a heap target's
// block.
//
// A union rather than a byte array, so that the heap pointer is read and
// written as the pointer it is, with no reinterpretation, and so that both
// homes share the one address an owner hands its thunks erased. Which member
// is live, and whether either is, is keyed by the owner's dispatch state, so
// an owner leaves it uninitialized rather than zeroing the buffer on every
// construction.
//
// The pointer is a `void*` because the storage is one member of an owner
// that holds any target type; the type comes back where the owner's thunks
// know it.
template<size_t Size, size_t Align>
union storage_area {
  alignas(Align) std::byte buf[Size];
  void* ptr;
};

#pragma endregion
#pragma region Housekeeping

// The housekeeping an owner performs on a stored target: destruction in
// either home, and the three moves `adoption_of` can route (the analog of
// Rust's drop glue, plus relocation).
//
// Each is typed on the target. A destination buffer is `void*` because it is
// raw storage until the target is constructed in it, which is placement new's
// own contract.
//
// Nothing here throws except `box`'s allocation: a target lives inline only
// when its move cannot throw, so every move below is nothrow, and `box`
// allocates before it touches the source.

// Destroy `target`, which lives in a buffer.
template<typename T>
void destroy_inline(T* target) noexcept {
  target->~T();
}

// Destroy `target`, which lives in a heap block, freeing the block.
template<typename T>
void destroy_heap(T* target) noexcept {
  delete target;
}

// Move the target at `from`, which lives in a storage area, into the storage
// area at `to`, destroying the source.
template<typename T>
void relocate_inline(T* from, void* to) noexcept {
  // The analyzer cannot see that the caller's fit check ties `sizeof(T)` to
  // the true capacity of the buffer behind `to`.
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.PlacementNew)
  ::new (to) T(std::move(*from));
  from->~T();
}

// Move the target at `from`, which lives in a storage area, into a fresh heap
// block, destroying the source, and return the block.
//
// The allocation can throw, and it throws before the source is touched.
template<typename T>
T* box(T* from) {
  auto* block = new T(std::move(*from));
  from->~T();
  return block;
}

// Move the target at `from`, which lives in a heap block, into the storage
// area at `to`, freeing the block.
//
// The caller has already established the fit and the nothrow move (see
// `adoption_of`).
template<typename T>
void unbox(T* from, void* to) noexcept {
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.PlacementNew): see relocate_inline
  ::new (to) T(std::move(*from));
  delete from;
}

#pragma endregion
#pragma region Function targets

// Whether `Fn` is a null function or member pointer, which is the one kind of
// constant that is not a target.
template<auto Fn>
consteval bool is_null_constant() noexcept {
  if constexpr (std::is_pointer_v<decltype(Fn)> ||
                std::is_member_pointer_v<decltype(Fn)>)
    return (Fn == nullptr);
  else
    return false;
}

} // namespace implementation

// `constant_fn<Fn>` is a callable whose target is the compile-time constant
// `Fn`, so that the target is part of the type rather than a value the
// instance carries.
//
// `Fn` may be a function (with or without its address taken), a member
// function or member object pointer (invoked as `std::invoke` does, with the
// object as the first argument), or a structural callable object such as a
// captureless lambda. A null pointer is refused at compile time.
//
// The instance has no data, and trivial construction and destruction, so it is
// `direct_eligible`: an owner calls it without storing it, and the call
// reaches `Fn` directly.
template<auto Fn>
struct constant_fn {
  static_assert(!implementation::is_null_constant<Fn>(),
      "constant_fn: a null function or member pointer is not a target");

  template<class... Args>
  requires(std::is_invocable_v<decltype(Fn), Args...>)
  constexpr decltype(auto) operator()(Args&&... args) const
      noexcept(std::is_nothrow_invocable_v<decltype(Fn), Args...>) {
    return std::invoke(Fn, std::forward<Args>(args)...);
  }
};

// `runtime_fn<Ptr>` is a callable holding a function or member pointer whose
// value is only known at runtime, spelled `runtime_fn{p}`.
//
// It is what a bare pointer target is, made explicit: stored as the pointer
// and called through it, with the object as the first argument for a member
// pointer. It may be null; an owner treats a null one as no callable, and
// calling a null one is the same undefined behavior as calling a null
// pointer.
template<class Ptr>
requires(
    (std::is_pointer_v<Ptr> &&
        std::is_function_v<std::remove_pointer_t<Ptr>>) ||
    std::is_member_pointer_v<Ptr>)
struct runtime_fn {
  Ptr fn;

  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return fn;
  }

  template<class... Args>
  requires(std::is_invocable_v<Ptr, Args...>)
  constexpr decltype(auto) operator()(Args&&... args) const
      noexcept(std::is_nothrow_invocable_v<Ptr, Args...>) {
    return std::invoke(fn, std::forward<Args>(args)...);
  }
};

template<class Ptr>
runtime_fn(Ptr) -> runtime_fn<Ptr>;

// Whether `T` is a `runtime_fn`.
template<class T>
constexpr bool is_runtime_fn_v = false;

template<class Ptr>
constexpr bool is_runtime_fn_v<runtime_fn<Ptr>> = true;

#pragma endregion
} // namespace invocables

#pragma region Exports
// Call-site vocabulary, exported to `corvid::meta`; see invocable_policy.h.
using invocables::constant_fn;
using invocables::runtime_fn;
#pragma endregion
}} // namespace corvid::meta
