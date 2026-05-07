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
#include "./meta_shared.h"
#include "./concepts.h"
#include <cassert>
#include <cstddef>
#include <new>

namespace corvid { inline namespace meta {

template<size_t SZ, class Sig>
class fixed_function;

#pragma region fixed_function

// `fixed_function<SZ, RP(ARGS...)>` is a move-only, zero-allocation
// type-erased callable.
//
// This is similar in principle to the proposed `stdext::inplace_function`, but
// a bit more specific.
//
// `SZ` is the total instance size in bytes. The stored functor must fit within
// `SZ - 2*sizeof(void*)` bytes and have alignment <=
// `alignof(std::max_align_t)`.
//
// If either constraint is violated, a `static_assert` fires with a clear
// message. Unlike `std::function`, no dynamic allocation is ever performed.
template<size_t SZ, class RP, class... ARGS>
class fixed_function<SZ, RP(ARGS...)> {
  static constexpr size_t pointer_pair_size = 2 * sizeof(void*);
  static_assert(SZ >= pointer_pair_size,
      "fixed_function: SZ must be at least 2*sizeof(void*)");

public:
  static constexpr size_t storage_size = SZ - pointer_pair_size;

#pragma region Construction

  fixed_function() = default;
  fixed_function(const fixed_function&) = delete;
  fixed_function& operator=(const fixed_function&) = delete;

  // Move-construct from any callable whose signature matches `RP(ARGS...)`.
  // The functor is move-constructed into internal storage.
  template<MoveConsumable FN>
  requires std::is_invocable_r_v<RP, std::decay_t<FN>, ARGS...>
  fixed_function(FN&& fn) {
    using FD = std::decay_t<FN>;
    static_assert(sizeof(FD) <= storage_size,
        "fixed_function: functor too large for storage");
    static_assert(alignof(FD) <= alignof(std::max_align_t),
        "fixed_function: functor alignment exceeds max_align_t");
    static_assert(!std::is_reference_v<RP> ||
                      std::is_reference_v<std::invoke_result_t<FD, ARGS...>>,
        "fixed_function: callable returns a prvalue but RP is a reference "
        "type; every call would produce a dangling reference");

    // The `MoveConsumable` concept ensures that `FN` is an rvalue reference
    // type, so the following clang-tidy warning does not apply.
    // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
    new (storage_) FD{std::move(fn)};
    invoke_ = &invoke_impl<FD>;
    lifespan_ = &manage_impl<FD>;
  }

  // Move constructor, leaves RHS empty.
  fixed_function(fixed_function&& other) noexcept
      : invoke_{std::exchange(other.invoke_, &default_invoke_impl)},
        lifespan_{std::exchange(other.lifespan_, nullptr)} {
    if (lifespan_) lifespan_(other.storage_, storage_);
  }

  // Move assignment, leaves RHS empty.
  fixed_function& operator=(fixed_function&& other) noexcept {
    if (this == &other) return *this;
    if (lifespan_) lifespan_(storage_, nullptr);
    invoke_ = std::exchange(other.invoke_, &default_invoke_impl);
    lifespan_ = std::exchange(other.lifespan_, nullptr);
    if (lifespan_) lifespan_(other.storage_, storage_);
    return *this;
  }

  // Assign nullptr to make the instance empty.
  fixed_function& operator=(std::nullptr_t) noexcept {
    if (lifespan_) lifespan_(storage_, nullptr);
    invoke_ = &default_invoke_impl;
    lifespan_ = nullptr;
    return *this;
  }

  ~fixed_function() {
    if (lifespan_) lifespan_(storage_, nullptr);
  }

  void swap(fixed_function& other) noexcept {
    fixed_function tmp{std::move(*this)};
    *this = std::move(other);
    other = std::move(tmp);
  }

  friend void swap(fixed_function& a, fixed_function& b) noexcept {
    a.swap(b);
  }

#pragma endregion
#pragma region Invocation

  // Invoke through the type-erased `invoke_` function pointer.
  RP operator()(ARGS... args) {
    return invoke_(storage_, std::forward<ARGS>(args)...);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] explicit operator bool() const noexcept { return lifespan_; }
  [[nodiscard]] bool operator!() const noexcept { return !lifespan_; }

#pragma endregion
#pragma region Implementation
private:
  // Type erasure function pointer types for invocation and lifespan
  // management.
  using invoke_fn_t = RP (*)(void*, ARGS...);
  using lifespan_fn_t = void (*)(void*, void*);

  // Invoke through a downcast pointer to the stored callable. Uses
  // `std::invoke` so member function pointers and data member pointers work
  // alongside lambdas, free functions, and functors.
  template<class F>
  static RP invoke_impl(void* p, ARGS... args) {
    assert(p);
    return std::invoke(*static_cast<F*>(p), std::forward<ARGS>(args)...);
  }

  // Default invoke implementation for empty state. Always throws.
  [[noreturn]] static RP
  default_invoke_impl([[maybe_unused]] void*, [[maybe_unused]] ARGS...) {
    throw std::bad_function_call();
  }

  // When `from` and `to` are both non-null: move-constructs `*from` into `to`.
  // Destructs the object at `from`, regardless.
  template<class F>
  static void manage_impl(void* from, void* to) {
    assert(from);
    auto* f = static_cast<F*>(from);
    if (to) new (to) F{std::move(*f)};
    f->~F();
  }

#pragma endregion
#pragma region Data members

  invoke_fn_t invoke_{&default_invoke_impl};
  lifespan_fn_t lifespan_{};
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
