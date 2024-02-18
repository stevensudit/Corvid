// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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

namespace corvid { inline namespace ownptr {
namespace details {

// Per spec, the pointer type is std::remove_reference_t<Deleter>::pointer if
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

template<class Deleter>
concept AllowDefaultConstruction =
    std::is_default_constructible_v<Deleter> && !std::is_pointer_v<Deleter>;
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
// - Does not support arrays at this time.
template<typename T, class Deleter = std::default_delete<T>>
class own_ptr {
public:
  using pointer = details::pointer<T, Deleter>;
  using element_type = T;
  using deleter_type = Deleter;

  static constexpr bool is_default_constructible_deleter_v =
      std::is_default_constructible_v<deleter_type> &&
      (!std::is_pointer_v<deleter_type>);

  static constexpr bool is_move_constructible_deleter_v =
      std::is_move_constructible_v<deleter_type>;

  static constexpr bool is_deleter_non_reference_v =
      !std::is_reference_v<deleter_type>;

  static constexpr bool is_deleter_lvalue_reference_v =
      std::is_lvalue_reference_v<deleter_type> &&
      !std::is_const_v<std::remove_reference_t<deleter_type>>;

  static constexpr bool is_deleter_const_lvalue_reference_v =
      std::is_lvalue_reference_v<deleter_type> &&
      std::is_const_v<std::remove_reference_t<deleter_type>>;

  template<typename D>
  static constexpr bool is_deleter =
      std::is_same_v<std::remove_reference_t<D>, std::remove_cvref_t<Deleter>>;

  template<typename U, typename E>
  static constexpr bool is_convertible_pointer =
      std::is_convertible_v<typename own_ptr<U, E>::pointer, pointer>;

  // c) either Deleter is a reference type and E is the same type as D, or
  // Deleter is not a reference type and E is implicitly convertible to D.
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
  constexpr explicit own_ptr(pointer&& ptr) noexcept
  requires is_default_constructible_deleter_v
      : ptr_(ptr) {
    ptr = nullptr;
  }

  // TODO: For construction from pointer and deleter, ensure that these are not
  // selected for CTAD. This means that there should not be any automatic
  // deduction guides for these. The way to do this is to write deduction
  // guides for all the other constructors but omit this one. Those explicit
  // guides suppress the creation of implicit ones. The way to test for CTAD is
  // to construct an instance using `auto u = own_ptr{new int,
  // std::default_delete<int>{}};` and see if it compiles. It shouldn't. It
  // should instead require`auto u = own_ptr<int, std::default_delete<int>>{new
  // int, std::default_delete<int>{}};`.

  // Construct with pointer and deleter, when specialized on non-reference
  // deleter.
  constexpr own_ptr(pointer p, const deleter_type& d) noexcept
  requires is_deleter_non_reference_v
      : ptr_(p), del_(d) {}

  constexpr own_ptr(pointer p, auto&& d) noexcept
  requires is_deleter_non_reference_v && is_deleter<decltype(d)> &&
               std::is_rvalue_reference_v<decltype(d)>
      : ptr_(p), del_(std::move(d)) {}

  // Construct with pointer and deleter, when specialized on mutable lvalue
  // deleter reference.
  constexpr own_ptr(pointer p, deleter_type& d) noexcept
  requires is_deleter_lvalue_reference_v
      : ptr_(p), del_(d) {}

  // Construct with pointer and deleter, when specialized on const lvalue
  // deleter reference.
  constexpr own_ptr(pointer p, const deleter_type& d) noexcept
  requires is_deleter_const_lvalue_reference_v
      : ptr_(p), del_(d) {}

  constexpr own_ptr(pointer p, auto&& d) noexcept
  requires is_deleter_const_lvalue_reference_v && is_deleter<decltype(d)> &&
               std::is_rvalue_reference_v<decltype(d)>
  = delete;

  // Construct from pointer to child.
  template<class U, class E>
  constexpr own_ptr(own_ptr<U, E>&& u) noexcept
  requires is_convertible_own_ptr<U, E> && std::is_reference_v<E>
      : ptr_(u.release()), del_(u.get_deleter()) {}

  template<class U, class E>
  constexpr own_ptr(own_ptr<U, E>&& u) noexcept
  requires is_convertible_own_ptr<U, E> && (!std::is_reference_v<E>)
      : ptr_(u.release()), del_(std::move(u.get_deleter())) {}

  constexpr own_ptr(own_ptr&& other) noexcept
  requires is_move_constructible_deleter_v
      : ptr_(other.ptr_), del_(std::move(other.del_)) {
    other.ptr_ = nullptr;
  }

  own_ptr(own_ptr&) = delete;

  ~own_ptr() { do_delete(); }

  own_ptr(const own_ptr&) = delete;
  own_ptr& operator=(const own_ptr&) = delete;

  // TODO: Support passing in deleter as parameter.

  own_ptr(own_ptr&& other) noexcept : ptr_(other.ptr_) {
    other.ptr_ = nullptr;
  }

  own_ptr& operator=(own_ptr&& other) noexcept {
    if (this != &other) do_delete(ptr_) = std::exchange(other.ptr_, nullptr);
    return *this;
  }

  // Added nodiscard.
  [[nodiscard]] constexpr T* operator->() const { return ptr_; }
  [[nodiscard]] constexpr T& operator*() const { return *ptr_; }

  [[nodiscard]] constexpr T* get() const { return ptr_; }
  [[nodiscard]] explicit operator bool() const { return ptr_ != nullptr; }

  void reset(T* ptr = nullptr) { do_delete(ptr_) = ptr; }

  [[nodiscard]] constexpr T* release() noexcept {
    return std::exchange(ptr_, nullptr);
  }

  constexpr deleter_type& get_deleter() noexcept { return del_; }
  constexpr const deleter_type& get_deleter() const noexcept { return del_; }

  // TODO: Add a static make().

private:
  T* ptr_{};
  [[no_unique_address]] Deleter del_;

  auto& do_delete() {
    del_(ptr_);
    return ptr_;
  }
};
}} // namespace corvid::ownptr
