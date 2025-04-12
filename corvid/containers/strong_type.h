// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2025 Steven Sudit
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

namespace corvid { inline namespace strongtypes {

// Generic strongly-typed wrapper.
//
// Does not inherit from `T`.
//
// Exposes operators, as well as pointer-like access semantics, for the
// underlying value. Uses SFINAE for functions that are expected to exist, but
// hides less likely functions behind `requires` clauses.
//
// Usage:
// ```
//  using FirstName = strong_type<std::string, struct FirstNameTag>;
// ```
//
// TODO: Test use case of nested types, where a T is itself a strong_type on a
// different tag.
// TODO: Test use case of a lambda.
template<typename T, typename TAG>
class strong_type {
public:
  using UnderlyingType = T;
  static_assert(!std::is_reference_v<T>,
      "strong_type cannot wrap a reference type");

  // Constructors.
  constexpr strong_type() = default;
  constexpr explicit strong_type(const T& value) noexcept(
      std::is_nothrow_copy_constructible_v<T>)
      : value_(value) {}
  constexpr explicit strong_type(T&& value) noexcept(
      std::is_nothrow_move_constructible_v<T>)
      : value_(std::move(value)) {}

  // Assignment and move.
  constexpr strong_type& operator=(const strong_type& other) noexcept(
      std::is_nothrow_copy_assignable_v<T>) = default;
  constexpr strong_type& operator=(strong_type&&) noexcept(
      std::is_nothrow_move_assignable_v<T>) = default;
  constexpr strong_type& operator=(const T& value) noexcept(
      std::is_nothrow_copy_assignable_v<T>) {
    value_ = value;
    return *this;
  }
  constexpr strong_type& operator=(T&& value) noexcept(
      std::is_nothrow_move_assignable_v<T>) {
    value_ = std::move(value);
    return *this;
  }

  // Access.
  [[nodiscard]] constexpr const T& get() const& noexcept { return value_; }
  [[nodiscard]] constexpr T& get() & noexcept { return value_; }
  [[nodiscard]] constexpr const T&& get() const&& noexcept {
    return std::move(value_);
  }
  [[nodiscard]] constexpr T&& get() && noexcept { return std::move(value_); }

  // Pointer-like access.
  [[nodiscard]] constexpr const T& operator*() const& noexcept {
    return value_;
  }
  [[nodiscard]] constexpr T& operator*() & noexcept { return value_; }
  [[nodiscard]] constexpr const T&& operator*() const&& noexcept {
    return std::move(value_);
  }
  [[nodiscard]] constexpr T&& operator*() && noexcept {
    return std::move(value_);
  }

  [[nodiscard]] constexpr const T* operator->() const noexcept {
    return &value_;
  }
  [[nodiscard]] constexpr T* operator->() noexcept { return &value_; }

  // Comparison operators.

  // Homogeneous comparison operators for strong_type.
  // (Not automatically generated from the spaceship operator because we also
  // specify heterogenous comparison operators.)
  friend constexpr auto
  operator<=>(const strong_type& lhs, const strong_type& rhs) = default;
  friend constexpr bool
  operator==(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) == 0;
  }
  friend constexpr bool
  operator!=(const strong_type& lhs, const strong_type& rhs) {
    return !(lhs == rhs);
  }
  friend constexpr bool
  operator<(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) < 0;
  }
  friend constexpr bool
  operator<=(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) <= 0;
  }
  friend constexpr bool
  operator>(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) > 0;
  }
  friend constexpr bool
  operator>=(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) >= 0;
  }

  // Heterogeneous comparison operators for strong_type and other types.
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator==(const strong_type& lhs, const U& rhs) {
    return lhs.value_ == rhs;
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator!=(const strong_type& lhs, const U& rhs) {
    return !(lhs == rhs);
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator<(const strong_type& lhs, const U& rhs) {
    return lhs.value_ < rhs;
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator<=(const strong_type& lhs, const U& rhs) {
    return !(rhs < lhs.value_);
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator>(const strong_type& lhs, const U& rhs) {
    return rhs < lhs.value_;
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator>=(const strong_type& lhs, const U& rhs) {
    return !(lhs.value_ < rhs);
  }

  // Reverse heterogeneous comparison operators for other types and
  // strong_type.
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator==(const U& lhs, const strong_type& rhs) {
    return lhs == rhs.value_;
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator!=(const U& lhs, const strong_type& rhs) {
    return !(lhs == rhs);
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator<(const U& lhs, const strong_type& rhs) {
    return lhs < rhs.value_;
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator<=(const U& lhs, const strong_type& rhs) {
    return !(rhs.value_ < lhs);
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator>(const U& lhs, const strong_type& rhs) {
    return rhs.value_ < lhs;
  }
  template<typename U>
  requires(!std::is_same_v<std::remove_cvref_t<U>, strong_type>)
  friend constexpr bool operator>=(const U& lhs, const strong_type& rhs) {
    return !(lhs < rhs.value_);
  }

private:
  T value_;
};

// Output stream operator (requires that the underlying type supports
// `operator<<`).
template<typename T, typename TAG>
requires requires(std::ostream& os, const T& value) { os << value; }
constexpr std::ostream&
operator<<(std::ostream& os, const strong_type<T, TAG>& obj) {
  return os << obj.get();
}

// Input stream operator (requires that the underlying type supports
// `operator>>`).
template<typename T, typename TAG>
requires requires(std::istream& is, T& value) { is >> value; }
constexpr std::istream&
operator>>(std::istream& is, strong_type<T, TAG>& obj) {
  return is >> obj.get();
}

}} // namespace corvid::strongtypes

// Support hash for wrapper, if supported for underlying type.
namespace std {
template<typename T, typename TAG>
struct hash<corvid::strongtypes::strong_type<T, TAG>>
    : std::enable_if_t<std::is_default_constructible_v<std::hash<T>>,
          std::true_type> {
  constexpr size_t operator()(
      const corvid::strongtypes::strong_type<T, TAG>& obj) const noexcept {
    return std::hash<T>{}(obj.get());
  }
};
} // namespace std
