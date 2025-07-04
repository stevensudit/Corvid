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

// This is internal, like it says, so don't import it.
namespace corvid::internal {

// Pointer adapter with `std::optional` semantics, specialized on a raw or
// smart pointer. It satisfies the requirements of NullablePointer, per
// https://en.cppreference.com/w/cpp/named_req/NullablePointer.
//
// The sole purpose of this class is to be used as a lightweight return value,
// not a parameter or local. This makes it ideal for lookups, where you can
// then chain calls to methods such as `value_or`. So if you find yourself
// declaring variables of this type, you're doing it wrong. That's why it's
// hidden behind the `internal` namespace.
//
// Note that it can be used like a pointer, both in terms of evaluating to
// `bool` in a predicate expression and being dereferenceable when `has_value`.
//
// For safety, enforces `[[nodiscard]]` and deletes some unwanted raw pointer
// behavior.
template<PointerLike Ptr>
class optional_ptr final {
public:
  // Infer.
  using P = Ptr;
  using E = pointer_element_t<P>;

  // Publish.
  using element_type = E;
  using raw_pointer = E*;
  using pointer = P;

  //
  // Construct
  //

  constexpr optional_ptr() noexcept {}
  constexpr optional_ptr(std::nullptr_t) noexcept {}
  constexpr optional_ptr(std::nullopt_t) noexcept {}

  constexpr optional_ptr(const pointer& p) noexcept(
      std::is_nothrow_copy_assignable_v<pointer>)
      : ptr_{p} {}
  constexpr optional_ptr(pointer&& p) noexcept(
      std::is_nothrow_move_assignable_v<pointer>)
      : ptr_{std::move(p)} {}

  constexpr optional_ptr(const optional_ptr&) noexcept(
      std::is_nothrow_copy_assignable_v<pointer>) = default;
  constexpr optional_ptr(optional_ptr&&) noexcept(
      std::is_nothrow_move_assignable_v<pointer>) = default;

  constexpr optional_ptr& operator=(const optional_ptr& o) noexcept(
      std::is_nothrow_copy_assignable_v<pointer>) {
    ptr_ = o.ptr_;
    return *this;
  }
  constexpr optional_ptr& operator=(optional_ptr&& o) noexcept(
      std::is_nothrow_move_assignable_v<pointer>) {
    ptr_ = std::move(o.ptr_);
    return *this;
  }

  //
  // Access
  //

  // For raw pointers, the implicit conversion to pointer operator means it can
  // be evaluated as a bool in a predicate expression or dereferenced.
  [[nodiscard]] constexpr operator pointer() const noexcept { return ptr_; }
  [[nodiscard]] constexpr auto operator->() const noexcept { return &*ptr_; }

  // For smart pointers, we need to forward these two calls explicitly.
  template<SmartPointer U = P>
  [[nodiscard]] constexpr auto& operator*() const {
    return *ptr_;
  }

  template<SmartPointer U = P>
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return ptr_ ? true : false;
  }

  // Get underlying pointer.
  [[nodiscard]] constexpr const pointer& get() const& noexcept { return ptr_; }
  [[nodiscard]] constexpr pointer&& get() && noexcept {
    return std::move(ptr_);
  }

  // Get raw pointer.
  template<SmartPointer U = P>
  [[nodiscard]] constexpr const element_type& get_ptr() const& noexcept {
    return *ptr_;
  }
  template<SmartPointer U = P>
  [[nodiscard]] constexpr element_type&& get_ptr() && noexcept {
    return std::move(*ptr_);
  }

  // Reset to new pointer value or null.
  constexpr void reset() noexcept(std::is_nothrow_move_assignable_v<pointer>) {
    ptr_ = pointer{};
  }
  constexpr void
  reset(const pointer& p) noexcept(std::is_nothrow_copy_assignable_v<pointer>)
  requires std::is_copy_assignable_v<pointer>
  {
    ptr_ = p;
  }
  constexpr void
  reset(pointer&& p) noexcept(std::is_nothrow_move_assignable_v<pointer>)
  requires std::is_move_assignable_v<pointer>
  {
    ptr_ = std::move(p);
  }

  // Whether there is a value.
  [[nodiscard]] constexpr bool has_value() const noexcept {
    return ptr_ ? true : false;
  }

  // Get value, throwing if null. Returns by reference.
  [[nodiscard]] constexpr element_type& value() const { return deref(ptr_); }

  // Get value, or if null, `default_val`. Returns by value.
  [[nodiscard]] constexpr element_type value_or(auto&& default_val) const {
    return ptr_ ? *ptr_
                : element_type{
                      std::forward<decltype(default_val)>(default_val)};
  }

  // Get value, or if null, default value. Returns by value.
  [[nodiscard]] constexpr element_type value_or() const {
    return value_or(E{});
  }

  // Get value, or if null, dereferences `default_ptr`. Returns by reference,
  // so default_ptr must not be null.
  template<PointerLike D>
  [[nodiscard]] constexpr auto& value_or_ptr(D&& default_ptr) const {
    return ptr_ ? *ptr_ : deref(std::forward<D>(default_ptr));
  }

  // Get value, or if null, call `f` to get value. Returns by value.
  [[nodiscard]] constexpr element_type value_or_fn(auto&& f) const {
    return ptr_ ? *ptr_ : f();
  }

  //
  // Disabled ops.
  //

  void operator[](size_t) const = delete;

  template<typename V>
  friend void operator+(const optional_ptr&, const V&) = delete;

  template<typename V>
  friend void operator+(const V&, const optional_ptr&) = delete;

  template<typename V>
  friend void operator-(const optional_ptr&, const V&) = delete;

  template<typename V>
  friend void operator-(const V&, const optional_ptr&) = delete;

private:
  pointer ptr_{};

  [[nodiscard]] constexpr static auto& deref(auto&& p) {
    if (p) return *p;
    throw std::bad_optional_access{};
  }
};

} // namespace corvid::internal
