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
#include <cassert>
#include <cstddef>
#include <functional>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "concepts.h"
#include "padding.h"

namespace corvid { inline namespace meta {

template<size_t SZ, class Sig>
class fixed_function;

// Determine whether `T` is a `fixed_function`.
template<typename T>
constexpr inline bool is_fixed_function_v = false;

template<size_t SZ, class Sig>
constexpr inline bool is_fixed_function_v<fixed_function<SZ, Sig>> = true;

#pragma region fixed_function

// `fixed_function<SZ, RP(ARGS...)>` is a move-only, zero-allocation
// type-erased callable: like `std::move_only_function`, but with a fixed
// inline storage size `SZ` and no dynamic allocation.
//
// This is also similar in principle to the proposed
// `stdext::inplace_function`, but a bit more specific.
//
// `SZ` is the total instance size in bytes. The stored functor must fit
// within `SZ - 2*sizeof(void*)` bytes and have alignment <=
// `alignof(std::max_align_t)`. If it doesn't fit, a `static_assert` fires.
//
// `SZ` must be a multiple of the storage alignment,
// `alignof(std::max_align_t)`, because a smaller value would occupy the padded
// size anyway and waste the difference. Instead of hardcoding a number that
// might only be valid on a particular platform, you should pass the size
// through `padded_size` to get a conforming value.
//
// Unlike `std::function`, no dynamic allocation is ever performed by
// `fixed_function` itself. However, we can't stop a functor from allocating
// internally and we do support explicit conversion from `std::function` and
// `std::move_only_function` (although only at a performance penalty), and both
// of these are capable of dynamic allocation.
//
// `fixed_function` instances that differ only in `SZ` can be freely assigned,
// so long as the source fits in the target. A downsizing assignment that would
// not fit throws `std::length_error` and leaves both sides intact. A same-size
// or upsizing assignment always succeeds, transplanting the stored callable
// rather than nesting the wrapper.
//
// `size` reports the stored payload's byte size and can be checked against
// `capacity` before assignment.
template<size_t SZ, class RP, class... ARGS>
class fixed_function<SZ, RP(ARGS...)> {
  static constexpr size_t pointer_pair_size = 2 * sizeof(void*);
  static_assert(SZ > pointer_pair_size,
      "fixed_function: SZ must be greater than 2*sizeof(void*)");
  static_assert(SZ == padded_size(SZ),
      "fixed_function: SZ that is not a multiple of the storage alignment "
      "would waste the difference as padding; pass it through padded_size");

  // Siblings transplant across sizes, which needs access to the erased
  // state.
  template<size_t, class>
  friend class fixed_function;

public:
  static constexpr size_t storage_size = SZ - pointer_pair_size;

#pragma region Construction

  fixed_function() = default;
  fixed_function(const fixed_function&) = delete;
  fixed_function& operator=(const fixed_function&) = delete;

  explicit fixed_function(std::nullptr_t) noexcept {}

  // Move-construct from any callable whose signature matches `RP(ARGS...)`.
  //
  // The functor is move-constructed into internal storage. The std
  // polymorphic function wrappers are deliberately excluded here; wrapping
  // one takes the explicit constructor below.
  template<MoveConsumable FN>
  requires(std::is_invocable_r_v<RP, std::decay_t<FN>, ARGS...> &&
           !is_std_function_wrapper_v<std::decay_t<FN>>)
  fixed_function(FN&& fn) noexcept {
    using FD = std::decay_t<FN>;

    // An `fn` that is a `fixed_function` and lands here has a different
    // signature (a same-signature sibling takes the transplanting constructor
    // instead) and is stored as an ordinary callable, but, matching
    // `std::function`, wrapping an empty one produces an empty function rather
    // than a truthy shell that throws when called.
    if constexpr (is_fixed_function_v<FD>)
      if (!fn) return;

    // The `MoveConsumable` concept ensures that `FN` is an rvalue reference
    // type, so the following clang-tidy warning does not apply.
    // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
    do_store<FD>(std::move(fn));
  }

  // Explicitly wrap a std polymorphic function wrapper, `std::function` or
  // `std::move_only_function`, whose signature is compatible with
  // `RP(ARGS...)`.
  //
  // This is the escape hatch for a payload too large for the inline buffer:
  // the wrapper keeps the oversized functor on its own heap allocation, and
  // only its small shell must fit inline. The costs are why the wrap is
  // explicit: every call double-indirects, and the shell may allocate
  // dynamically, which is an exception to the zero-allocation guarantee.
  //
  // Invocability is checked against an lvalue wrapper because that is how
  // the stored one is invoked. For a ref-qualified `std::move_only_function`
  // signature, this admits `int() &` and rejects `int() &&`.
  //
  // Like `std::function`, wrapping an empty wrapper produces an empty
  // function rather than a truthy shell that throws when called.
  template<MoveConsumable FN>
  requires(is_std_function_wrapper_v<std::decay_t<FN>> &&
           std::is_invocable_r_v<RP, std::decay_t<FN>&, ARGS...>)
  explicit fixed_function(FN&& fn) noexcept {
    using FD = std::decay_t<FN>;
    if (!fn) return;
    // The `MoveConsumable` concept ensures that `FN` is an rvalue reference
    // type, so the following clang-tidy warning does not apply.
    // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
    do_store<FD>(std::move(fn));
  }

  // Move constructor, leaves RHS empty.
  fixed_function(fixed_function&& other) noexcept
      : invoke_{std::exchange(other.invoke_, &default_invoke_impl)},
        lifespan_{std::exchange(other.lifespan_, nullptr)} {
    if (lifespan_) lifespan_(other.storage_, storage_, storage_size);
  }

  // Move from a same-signature sibling of another size, transplanting the
  // stored callable rather than nesting the wrapper.
  //
  // The thunks depend only on the signature and the stored type, never on the
  // buffer size, so the source's pointers serve directly.
  //
  // The payload is guaranteed to fit when the source buffer is no larger
  // than ours. A downsizing move instead has its fit checked inside the
  // lifespan thunk, atomically with the move, and a payload too big for this
  // buffer throws `std::length_error`, leaving the source intact.
  template<size_t SZ2>
  requires(SZ2 != SZ)
  fixed_function(fixed_function<SZ2, RP(ARGS...)>&& other) noexcept(
      fixed_function<SZ2, RP(ARGS...)>::storage_size <= storage_size) {
    using other_t = fixed_function<SZ2, RP(ARGS...)>;
    [[maybe_unused]] const bool refused =
        other.lifespan_ &&
        (other.lifespan_(other.storage_, storage_, storage_size) == 0);
    // A refusal is only reachable when downsizing; the guard keeps the
    // guaranteed-fit instantiation genuinely non-throwing.
    if constexpr (other_t::storage_size > storage_size) {
      if (refused)
        throw std::length_error{
            "fixed_function: payload too large for the destination buffer"};
    }
    invoke_ = std::exchange(other.invoke_, &other_t::default_invoke_impl);
    lifespan_ = std::exchange(other.lifespan_, nullptr);
  }

  // Move assignment, leaves RHS empty.
  fixed_function& operator=(fixed_function&& other) noexcept {
    if (this == &other) return *this;
    if (lifespan_) lifespan_(storage_, nullptr, 0);
    invoke_ = std::exchange(other.invoke_, &default_invoke_impl);
    lifespan_ = std::exchange(other.lifespan_, nullptr);
    if (lifespan_) lifespan_(other.storage_, storage_, storage_size);
    return *this;
  }

  // Move assignment from a same-signature sibling of another size, under the
  // same transplant-and-fit rules as the converting move constructor.
  //
  // A downsizing move checks fit with a pre-flight size query, throwing
  // before either side is touched, so a refusal leaves both intact. Nothing
  // can alter the payload between the query and the transplant, so this is as
  // sound as the constructor's in-thunk check, and it costs one indirect call
  // instead of an extra move.
  template<size_t SZ2>
  requires(SZ2 != SZ)
  fixed_function& operator=(fixed_function<SZ2, RP(ARGS...)>&& other) noexcept(
      fixed_function<SZ2, RP(ARGS...)>::storage_size <= storage_size) {
    using other_t = fixed_function<SZ2, RP(ARGS...)>;
    if constexpr (other_t::storage_size > storage_size) {
      if (other.lifespan_ &&
          other.lifespan_(nullptr, nullptr, 0) > storage_size)
        throw std::length_error{
            "fixed_function: payload too large for the destination buffer"};
    }
    if (lifespan_) lifespan_(storage_, nullptr, 0);
    invoke_ = std::exchange(other.invoke_, &other_t::default_invoke_impl);
    lifespan_ = std::exchange(other.lifespan_, nullptr);
    if (lifespan_) lifespan_(other.storage_, storage_, storage_size);
    return *this;
  }

  // Assign nullptr to make the instance empty.
  fixed_function& operator=(std::nullptr_t) noexcept {
    if (lifespan_) lifespan_(storage_, nullptr, 0);
    invoke_ = &default_invoke_impl;
    lifespan_ = nullptr;
    return *this;
  }

  ~fixed_function() noexcept {
    if (lifespan_) lifespan_(storage_, nullptr, 0);
  }

  void swap(fixed_function& other) noexcept {
    auto tmp = std::move(*this);
    *this = std::move(other);
    other = std::move(tmp);
  }

  friend void swap(fixed_function& a, fixed_function& b) noexcept {
    a.swap(b);
  }

#pragma endregion
#pragma region Invocation

  // Invoke through the type-erased `invoke_` function pointer. Intentionally
  // disallows invocation through a `const this`.
  RP operator()(ARGS... args) {
    return invoke_(storage_, std::forward<ARGS>(args)...);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] explicit operator bool() const noexcept { return lifespan_; }
  [[nodiscard]] bool operator!() const noexcept { return !lifespan_; }

  // Size of the stored callable's payload in bytes, or 0 when empty.
  [[nodiscard]] size_t size() const noexcept {
    return lifespan_ ? lifespan_(nullptr, nullptr, 0) : 0;
  }

  // Capacity of the inline storage in bytes.
  [[nodiscard]] size_t capacity() const noexcept { return storage_size; }

#pragma endregion
#pragma region Implementation
private:
  // Type erasure function pointer types for invocation and lifespan
  // management.
  using invoke_fn_t = RP (*)(void*, ARGS...);
  using lifespan_fn_t = size_t (*)(void*, void*, size_t) noexcept;

  // Move `fn` into inline storage and publish its thunks.
  //
  // `FD` is the stored type, already decayed and always passed explicitly,
  // so the parameter is a plain rvalue reference despite its forwarding
  // spelling, and `std::forward` below is equivalent to `std::move`. The
  // caller handles any empty-wrapper special case before calling.
  template<class FD>
  void do_store(FD&& fn) noexcept {
    static_assert(!std::is_reference_v<FD>,
        "fixed_function: do_store requires the decayed stored type");
    static_assert(sizeof(FD) <= storage_size,
        "fixed_function: functor too large for storage");
    static_assert(alignof(FD) <= alignof(std::max_align_t),
        "fixed_function: functor alignment exceeds max_align_t");
    static_assert(std::is_nothrow_move_constructible_v<FD>,
        "fixed_function: functor move constructor may throw; inline storage "
        "relocates the functor, so its move must be noexcept");
    static_assert(std::is_nothrow_destructible_v<FD>,
        "fixed_function: functor destructor may throw; inline storage "
        "destroys the functor, so its destructor must be noexcept");
    static_assert(!std::is_reference_v<RP> ||
                      std::is_reference_v<std::invoke_result_t<FD, ARGS...>>,
        "fixed_function: callable returns a prvalue but RP is a reference "
        "type; every call would produce a dangling reference");

    new (storage_) FD(std::forward<FD>(fn));
    invoke_ = &invoke_impl<FD>;
    lifespan_ = &manage_impl<FD>;
  }

  // Invoke through a downcast pointer to the stored callable. Uses
  // `std::invoke_r` so member function pointers and data member pointers work
  // alongside lambdas, free functions, and functors, and so a value returned
  // by the callable is discarded when `RP` is void rather than making the
  // `return` ill-formed.
  template<class F>
  static RP invoke_impl(void* p, ARGS... args) {
    assert(p);
    return std::invoke_r<RP>(*static_cast<F*>(p), std::forward<ARGS>(args)...);
  }

  // Default invoke implementation for empty state. Always throws.
  [[noreturn]] static RP
  default_invoke_impl([[maybe_unused]] void*, [[maybe_unused]] ARGS...) {
    throw std::bad_function_call();
  }

  // Implementation of `lifespan_`.
  //
  // Provides move, destruct, and size.
  //
  // When `from` and `to` are distinct and both non-null: move-constructs
  // `*from` into `to` and destructs the object at `from`, but only when the
  // payload fits `to_size`. A move that does not fit does nothing and
  // returns 0, so the caller can refuse with both objects intact.
  //
  // When `from` is not null and `to` is null: destructs the object at `from`,
  // ignoring `to_size`.
  //
  // When `from == to` (canonically both null), this is a pure size query: no
  // move, no destruct. Returns the payload size in every non-refusal case.
  template<class F>
  static size_t manage_impl(void* from, void* to, size_t to_size) noexcept {
    if (to != from) {
      assert(from);
      auto* f = static_cast<F*>(from);
      if (to) {
        if (to_size < sizeof(F)) return 0;
        new (to) F(std::move(*f));
      }
      f->~F();
    }
    return sizeof(F);
  }

#pragma endregion
#pragma region Data members

  invoke_fn_t invoke_{&default_invoke_impl};
  lifespan_fn_t lifespan_{};
  // Deliberately no initializer: occupancy is keyed by `lifespan_`, and
  // zeroing the buffer on every construction would be pure waste.
  alignas(std::max_align_t) std::byte storage_[storage_size];

#pragma endregion
};

#pragma endregion
#pragma region fixed_function_of

// `fixed_function_of<SZ>` pins the storage size and leaves the signature open,
// letting a single size constant be shared across a family of aliases.
//
// Example:
//   using my_fns     = fixed_function_of<64>;
//   using callback_t = my_fns::type<void(int)>;
//   using pred_t     = my_fns::type<bool(int)>;
template<size_t SZ>
struct fixed_function_of {
  template<class Sig>
  using type = fixed_function<SZ, Sig>;
};

#pragma endregion
}} // namespace corvid::meta
