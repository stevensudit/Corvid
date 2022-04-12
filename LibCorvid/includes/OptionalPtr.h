// Corvid: A general-purpose C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022 Steven Sudit
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
#include <optional>
#include "Meta.h"

namespace corvid {

// Pointer adapter with `std::optional` semantics, specialized on a raw or
// smart pointer.
//
// The sole purpose of this class is to be used as a return value, not a
// parameter or local. This makes it ideal for lookups, where you can then
// chain calls to methods such as `value_or`. So if you find yourself declaring
// variables of this type, you're doing it wrong.
//
// Note that it can be used like a pointer, both in terms of evaluating to bool
// in a predicate expression and being dereferenceable when has_value.
//
// For safety, enforces nodiscard and deletes some unwanted raw pointer
// behavior.
template<typename Ptr>
class optional_ptr {
public:
  using P = Ptr;
  using E = pointer_element_t<P>;
  constexpr static bool is_raw = std::is_pointer_v<P>;

  using element_type = E;
  using raw_pointer = E*;
  using pointer = P;

  // Construct/copy.

  constexpr optional_ptr() noexcept {}
  constexpr optional_ptr(std::nullptr_t) noexcept {}
  constexpr optional_ptr(std::nullopt_t) noexcept {}

  constexpr optional_ptr(const P& p) noexcept : ptr_(p) {}
  constexpr optional_ptr(P&& p) noexcept : ptr_(std::move(p)) {}

  constexpr optional_ptr(const optional_ptr&) noexcept = default;
  constexpr optional_ptr(optional_ptr&&) noexcept = default;

  constexpr const optional_ptr& operator=(optional_ptr&& o) noexcept {
    ptr_ = std::forward<optional_ptr>(o.ptr_);
    return *this;
  }

  // For raw pointers, the implicit conversion to pointer operator means it can
  // be evaluated as a bool in a predicate expression or dereferenced.
  constexpr [[nodiscard]] operator P() const noexcept { return ptr_; }
  constexpr [[nodiscard]] auto operator->() const noexcept { return &*ptr_; }

  // For smart pointers, we need to forward these two calls.
  template<typename = std::enable_if_t<!is_raw>>
  constexpr [[nodiscard]] auto& operator*() const {
    return *ptr_;
  }

  template<typename = std::enable_if_t<!is_raw>>
  constexpr [[nodiscard]] explicit operator bool() const noexcept {
    return ptr_ ? true : false;
  }

  // Access.

  constexpr [[nodiscard]] const P& get() const& noexcept { return ptr_; }
  constexpr [[nodiscard]] P&& get() && noexcept { return std::move(ptr_); }

  constexpr void reset(const P& p = nullptr) noexcept { ptr_ = p; }
  constexpr void reset(P&& p) noexcept { ptr_ = std::move(p); }

  constexpr [[nodiscard]] bool has_value() const noexcept {
    return has_value(ptr_);
  }

  // Get value, throwing if null. Returns by reference.
  constexpr [[nodiscard]] E& value() const { return deref(ptr_); }

  // Get value, or if null, `default_val`. Returns by value.
  constexpr [[nodiscard]] E value_or(auto&& default_val) const {
    return has_value() ? *ptr_ : default_val;
  }

  // Get value, or if null, default value. Returns by value.
  constexpr [[nodiscard]] E value_or() const { return value_or(E{}); }

  // Get value, or if null, dereferences `default_ptr`. Returns by ref.
  constexpr [[nodiscard]] auto& value_or_ptr(auto* default_ptr) const {
    return deref(has_value() ? ptr_ : default_ptr);
  }

  // Get value, or if null, call `f` to get value. Returns by value.
  constexpr [[nodiscard]] E value_or_fn(auto&& f) const {
    return has_value() ? *ptr_ : f();
  }

  // Disabled ops.

  void operator[](size_t) const = delete;

  template<typename U>
  friend void operator+(const optional_ptr&, const U&) = delete;

  template<typename U>
  friend void operator+(const U&, const optional_ptr&) = delete;

  template<typename U>
  friend void operator-(const optional_ptr&, const U&) = delete;

  template<typename U>
  friend void operator-(const U&, const optional_ptr&) = delete;

private:
  P ptr_{};

  constexpr static [[nodiscard]] bool has_value(auto& p) noexcept {
    return p ? true : false;
  }

  constexpr static [[nodiscard]] auto& deref(auto p) {
    if (has_value(p)) return *p;
    throw std::bad_optional_access{};
  }
};

} // namespace corvid
