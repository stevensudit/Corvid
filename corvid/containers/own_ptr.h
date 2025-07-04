// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include "containers_shared.h"

#ifdef _WIN32
#define NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace corvid { inline namespace ownptr {
namespace details {

// Per spec, the pointer type is `std::remove_reference_t<Deleter>::pointer` if
// present, T* otherwise.
template<typename T, class Deleter, typename = void>
struct get_pointer_type {
  using type = T*;
};

template<typename T, class Deleter>
struct get_pointer_type<T, Deleter,
    std::void_t<typename std::remove_reference_t<Deleter>::pointer>> {
  using type = typename std::remove_reference_t<Deleter>::pointer;
};

template<typename T, class Deleter>
using pointer = typename get_pointer_type<T, Deleter>::type;
} // namespace details

// The `own_ptr` class is a replacement for `std::unique_ptr` that takes
// advantage of language improvements since the original was standardized. It
// is essentially a drop-in replacement, but harder to break.
//
// All methods are as per https://en.cppreference.com/w/cpp/memory/unique_ptr
// unless otherwise stated.
//
// Changes:
// - Getters are marked `[[nodiscard]]`.
// - The rhs is always an rvalue reference, even for a raw ptr.
// - Works with a deleter that supports a custom_handle that is not a pointer.
// - Does not support arrays at this time.
//
// TODO: Consider supporting arrays.
// TODO: Consider supporting interop with `std::shared_ptr`.
template<typename T, class Deleter = std::default_delete<T>>
class own_ptr final {
public:
  using pointer = details::pointer<T, Deleter>;
  using element_type = T;
  using deleter_type = Deleter;

  // `participate in overload resolution only if
  // std::is_default_constructible<Deleter>::value is true and Deleter is not a
  // pointer type.`
  static constexpr bool is_default_constructible_deleter_v =
      std::is_default_constructible_v<deleter_type> &&
      std::is_nothrow_default_constructible_v<deleter_type> &&
      (!std::is_pointer_v<deleter_type>);

  // `only participates in overload resolution if
  // std::is_move_constructible<Deleter>::value is true. If Deleter is not a
  // reference type, requires that it is nothrow-MoveConstructible (if Deleter
  // is a reference, get_deleter() and u.get_deleter() after move construction
  // reference the same value)`
  static constexpr bool is_move_constructible_deleter_v =
      std::is_move_constructible_v<deleter_type> &&
      (!std::is_reference_v<deleter_type> ||
          std::is_nothrow_move_constructible_v<deleter_type>);

  static constexpr bool is_deleter_non_reference_v =
      !std::is_reference_v<deleter_type>;

  static constexpr bool is_deleter_lvalue_reference_v =
      std::is_lvalue_reference_v<deleter_type> &&
      !std::is_const_v<std::remove_reference_t<deleter_type>>;

  static constexpr bool is_deleter_const_lvalue_reference_v =
      std::is_lvalue_reference_v<deleter_type> &&
      std::is_const_v<std::remove_reference_t<deleter_type>>;

  template<typename D>
  static constexpr bool is_deleter = std::is_same_v<std::remove_reference_t<D>,
      std::remove_cvref_t<deleter_type>>;

  template<typename U, typename E>
  static constexpr bool is_convertible_pointer =
      std::is_convertible_v<typename own_ptr<U, E>::pointer, pointer>;

  // `c) either Deleter is a reference type and E is the same type as D, or
  // Deleter is not a reference type and E is implicitly convertible to D.`
  template<typename U, typename E>
  static constexpr bool is_convertible_deleter =
      (std::is_reference_v<deleter_type> && std::is_same_v<deleter_type, E>) ||
      (!std::is_reference_v<deleter_type> &&
          std::is_convertible_v<E, deleter_type>);

  template<typename U, typename E>
  static constexpr bool is_convertible_own_ptr =
      is_convertible_pointer<U, E> && is_convertible_deleter<U, E>;

public:
  // Default constructor.
  constexpr own_ptr() noexcept
  requires is_default_constructible_deleter_v
  = default;

  // Construct from nullptr.
  constexpr own_ptr(std::nullptr_t) noexcept
  requires is_default_constructible_deleter_v
  {}

  // Construct by moving pointer. This is novel.
  // `There is no class template argument deduction from pointer type because
  // it is impossible to distinguish a pointer obtained from array and
  // non-array forms of new.`
  constexpr explicit own_ptr(pointer&& ptr) noexcept
  requires is_default_constructible_deleter_v
      : ptr_{ptr} {
    ptr = pointer{};
  }

  // Construct with pointer and deleter, when specialized on non-reference
  // deleter.
  constexpr own_ptr(pointer p, const deleter_type& d) noexcept
  requires is_deleter_non_reference_v
      : ptr_{p}, del_{d} {}

  constexpr own_ptr(pointer p, auto&& d) noexcept
  requires is_deleter_non_reference_v && is_deleter<decltype(d)> &&
               std::is_rvalue_reference_v<decltype(d)>
      : ptr_{p}, del_{std::forward<decltype(d)>(d)} {}

  // Construct with pointer and deleter, when specialized on mutable lvalue
  // deleter reference.
  constexpr own_ptr(pointer p, deleter_type& d) noexcept
  requires is_deleter_lvalue_reference_v
      : ptr_{p}, del_{d} {}

  // Construct with pointer and deleter, when specialized on const lvalue
  // deleter reference.
  constexpr own_ptr(pointer p, const deleter_type& d) noexcept
  requires is_deleter_const_lvalue_reference_v
      : ptr_{p}, del_{d} {}

  constexpr own_ptr(pointer p, auto&& d) noexcept
  requires is_deleter_const_lvalue_reference_v && is_deleter<decltype(d)> &&
               std::is_rvalue_reference_v<decltype(d)>
  = delete;

  // Construct from pointer to child.
  template<class U, class E>
  constexpr own_ptr(own_ptr<U, E>&& u) noexcept
  requires is_convertible_own_ptr<U, E> && std::is_reference_v<E>
      : ptr_{u.release()}, del_{u.get_deleter()} {}

  template<class U, class E>
  constexpr own_ptr(own_ptr<U, E>&& u) noexcept
  requires is_convertible_own_ptr<U, E> && (!std::is_reference_v<E>)
      : ptr_{u.release()}, del_{std::move(u.get_deleter())} {}

  constexpr own_ptr(own_ptr&& other) noexcept
  requires is_move_constructible_deleter_v
      : ptr_(other.ptr_), del_{std::move(other.del_)} {
    other.ptr_ = pointer{};
  }

  ~own_ptr() { do_delete() = pointer{}; }

  own_ptr(own_ptr&) = delete;
  own_ptr(const own_ptr&) = delete;
  own_ptr& operator=(const own_ptr&) = delete;

  constexpr own_ptr& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  constexpr own_ptr& operator=(own_ptr&& other) noexcept
  requires is_move_constructible_deleter_v
  {
    if (this != &other) {
      do_delete() = std::exchange(other.ptr_, pointer{});
      del_ = std::move(other.del_);
    }
    return *this;
  }

  template<class U, class E>
  constexpr own_ptr& operator=(own_ptr<U, E>&& other) noexcept
  requires is_convertible_own_ptr<U, E> &&
           std::is_assignable_v<deleter_type&, E&&>
  {
    if (this != &other) {
      do_delete() = std::exchange(other.ptr_, pointer{});
      del_ = std::move(other.del_);
    }
    return *this;
  }

  // Added nodiscard.
  [[nodiscard]] constexpr pointer operator->() const { return ptr_; }
  [[nodiscard]] constexpr element_type& operator*() const { return *ptr_; }

  [[nodiscard]] constexpr pointer get() const { return ptr_; }
  [[nodiscard]] constexpr explicit operator bool() const {
    return ptr_ != pointer{};
  }

  constexpr void reset(pointer&& ptr = pointer{}) {
    do_delete() = std::exchange(ptr, pointer{});
  }

  [[nodiscard]] constexpr pointer release() noexcept {
    return std::exchange(ptr_, pointer{});
  }

  [[nodiscard]] constexpr deleter_type& get_deleter() noexcept { return del_; }
  [[nodiscard]] constexpr const deleter_type& get_deleter() const noexcept {
    return del_;
  }

  // Make an owned instance from parameters. Moral equivalent of
  // `std::make_unique`.
  template<typename... Args>
  static constexpr own_ptr<T, Deleter> make(Args&&... args) {
    return own_ptr{new T{std::forward<Args>(args)...}};
  }

private:
  pointer ptr_{};
  NO_UNIQUE_ADDRESS deleter_type del_;

  auto& do_delete() {
    del_(std::move(ptr_));
    return ptr_;
  }
};

}} // namespace corvid::ownptr
