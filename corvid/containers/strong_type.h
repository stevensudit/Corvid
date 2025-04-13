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

// Fwd.
template<typename T, typename TAG>
class strong_type;

// Concept for any strong_type.
template<typename T>
concept StrongType = corvid::is_specialization_of_v<T, strong_type>;

template<typename T>
concept NotStrongType = !StrongType<T>;

// Generic strongly-typed wrapper.
//
// Does not inherit from `T`. Attempts to be useful as a `T` substitute without
// being a `T`, as such. Note that, for integers, you can usually get all you
// want from a class enum, especially using a sequence enum.
//
// Exposes operators, as well as pointer-like access semantics, for the
// underlying value. These work homogenously, when both sides are identical.
// They also work heterogenously, when one side is a different type, whether
// it's the underlying type or some other that can interact or convert with it.
// However, we never allow two different `strong_type` instances specialized on
// different tags to interact, even if the underlying type is identical.
//
// Usage:
// ```
//  using FirstName = strong_type<std::string, struct FirstNameTag>;
// ```
//
// TODO: Test use case of nested types, where a T is itself a strong_type on a
// different tag.
// TODO: Test use case of a lambda.
// TODO: Consider changing the requires to AND in a flag that can be
// controlled. This would allow disabling various sets of operations, without
// doing the whole mixin thing.
template<typename T, typename TAG>
class strong_type {
public:
  using UnderlyingType = T;
  using Tag = TAG;
  static_assert(!std::is_reference_v<T>,
      "strong_type cannot wrap a reference type");

  // Constructors.
  constexpr strong_type() = default;

  constexpr strong_type(const strong_type&) noexcept(
      std::is_nothrow_copy_constructible_v<T>) = default;
  constexpr strong_type(strong_type&&) noexcept(
      std::is_nothrow_move_constructible_v<T>) = default;

  constexpr explicit strong_type(const T& value) noexcept(
      std::is_nothrow_copy_constructible_v<T>)
      : value_{value} {}
  constexpr explicit strong_type(T&& value) noexcept(
      std::is_nothrow_move_constructible_v<T>)
      : value_{std::move(value)} {}

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
  // TODO: Add conversion assignments.

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

  // Homogeneous comparison operators for `strong_type`.
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

  // Heterogeneous comparison operators for `strong_type` and other types.
  friend constexpr bool
  operator==(const strong_type& lhs, const NotStrongType auto& rhs) {
    return lhs.value_ == rhs;
  }
  friend constexpr bool
  operator!=(const strong_type& lhs, const NotStrongType auto& rhs) {
    return !(lhs == rhs);
  }
  friend constexpr bool
  operator<(const strong_type& lhs, const NotStrongType auto& rhs) {
    return lhs.value_ < rhs;
  }
  friend constexpr bool
  operator<=(const strong_type& lhs, const NotStrongType auto& rhs) {
    return !(rhs < lhs);
  }
  friend constexpr bool
  operator>(const strong_type& lhs, const NotStrongType auto& rhs) {
    return rhs < lhs.value_;
  }
  friend constexpr bool
  operator>=(const strong_type& lhs, const NotStrongType auto& rhs) {
    return !(lhs.value_ < rhs);
  }
  friend constexpr bool
  operator==(const NotStrongType auto& lhs, const strong_type& rhs) {
    return lhs == rhs.value_;
  }
  friend constexpr bool
  operator!=(const NotStrongType auto& lhs, const strong_type& rhs) {
    return !(lhs == rhs);
  }
  friend constexpr bool
  operator<(const NotStrongType auto& lhs, const strong_type& rhs) {
    return lhs < rhs.value_;
  }
  friend constexpr bool
  operator<=(const NotStrongType auto& lhs, const strong_type& rhs) {
    return !(rhs.value_ < lhs);
  }
  friend constexpr bool
  operator>(const NotStrongType auto& lhs, const strong_type& rhs) {
    return rhs.value_ < lhs;
  }
  friend constexpr bool
  operator>=(const NotStrongType auto& lhs, const strong_type& rhs) {
    return !(lhs < rhs.value_);
  }

  // Heterogeneous comparison operators for `strong_type` and `NotStrongType`.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t == rhs; }
  friend constexpr bool operator==(const strong_type& lhs, const U& rhs) {
    return lhs.value_ == rhs;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t != rhs; }
  friend constexpr bool operator!=(const strong_type& lhs, const U& rhs) {
    return !(lhs == rhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t < rhs; }
  friend constexpr bool operator<(const strong_type& lhs, const U& rhs) {
    return lhs.value_ < rhs;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t <= rhs; }
  friend constexpr bool operator<=(const strong_type& lhs, const U& rhs) {
    return !(rhs < lhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t > rhs; }
  friend constexpr bool operator>(const strong_type& lhs, const U& rhs) {
    return rhs < lhs.value_;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t >= rhs; }
  friend constexpr bool operator>=(const strong_type& lhs, const U& rhs) {
    return !(lhs.value_ < rhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs == t; }
  friend constexpr bool operator==(const U& lhs, const strong_type& rhs) {
    return lhs == rhs.value_;
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs != t; }
  friend constexpr bool operator!=(const U& lhs, const strong_type& rhs) {
    return !(lhs == rhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs < t; }
  friend constexpr bool operator<(const U& lhs, const strong_type& rhs) {
    return lhs < rhs.value_;
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs <= t; }
  friend constexpr bool operator<=(const U& lhs, const strong_type& rhs) {
    return !(rhs.value_ < lhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs > t; }
  friend constexpr bool operator>(const U& lhs, const strong_type& rhs) {
    return rhs.value_ < lhs;
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs >= t; }
  friend constexpr bool operator>=(const U& lhs, const strong_type& rhs) {
    return !(lhs < rhs.value_);
  }

  // Unary operators.
  template<typename = void>
  requires requires(T t) { +t; }
  constexpr strong_type operator+() const {
    return *this;
  }
  template<typename = void>
  requires requires(T t) { -t; }
  constexpr strong_type operator-() const {
    return strong_type{-value_};
  }
  template<typename = void>
  requires requires(T t) { ~t; }
  constexpr strong_type operator~() const {
    return strong_type{~value_};
  }
  template<typename = void>
  requires requires(T t) { !t; }
  constexpr strong_type operator!() const {
    return strong_type{!value_};
  }
  template<typename = void>
  requires requires(T t) { ++t; }
  constexpr strong_type& operator++() {
    ++value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t++; }
  constexpr strong_type operator++(int) {
    strong_type temp{*this};
    value_++;
    return temp;
  }
  template<typename = void>
  requires requires(T t) { --t; }
  constexpr strong_type& operator--() {
    --value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t--; }
  constexpr strong_type operator--(int) {
    strong_type temp{*this};
    value_--;
    return temp;
  }

  // Homogeneous binary arithmetic operators.
  template<typename = void>
  requires requires(T t) { t + t; }
  constexpr strong_type operator+(const strong_type& other) const {
    return strong_type{value_ + other.value_};
  }
  template<typename = void>
  requires requires(T t) { t - t; }
  constexpr strong_type operator-(const strong_type& other) const {
    return strong_type{value_ - other.value_};
  }
  template<typename = void>
  requires requires(T t) { t * t; }
  constexpr strong_type operator*(const strong_type& other) const {
    return strong_type{value_ * other.value_};
  }
  template<typename = void>
  requires requires(T t) { t / t; }
  constexpr strong_type operator/(const strong_type& other) const {
    return strong_type{value_ / other.value_};
  }
  template<typename = void>
  requires requires(T t) { t % t; }
  constexpr strong_type operator%(const strong_type& other) const {
    return strong_type{value_ % other.value_};
  }

  // Heterogeneous arithmetic operators for `strong_type` and `NotStrongType`.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t + rhs; }
  friend constexpr strong_type
  operator+(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ + rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t - rhs; }
  friend constexpr strong_type
  operator-(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ - rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t * rhs; }
  friend constexpr strong_type
  operator*(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ * rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t / rhs; }
  friend constexpr strong_type
  operator/(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ / rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t % rhs; }
  friend constexpr strong_type
  operator%(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ % rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs + t; }
  friend constexpr strong_type
  operator+(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs + rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs - t; }
  friend constexpr strong_type
  operator-(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs - rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs * t; }
  friend constexpr strong_type
  operator*(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs * rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs / t; }
  friend constexpr strong_type
  operator/(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs / rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs % t; }
  friend constexpr strong_type
  operator%(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs % rhs.value_};
  }

  // Heterogeneous bitwise operators for `strong_type` and `NotStrongType`.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t & rhs; }
  friend constexpr strong_type
  operator&(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ & rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t | rhs; }
  friend constexpr strong_type
  operator|(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ | rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t ^ rhs; }
  friend constexpr strong_type
  operator^(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ ^ rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs & t; }
  friend constexpr strong_type
  operator&(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs & rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs | t; }
  friend constexpr strong_type
  operator|(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs | rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs ^ t; }
  friend constexpr strong_type
  operator^(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs ^ rhs.value_};
  }

  // Heterogeneous bitwise shift operators for `strong_type` and
  // `NotStrongType` on RHS. We do not attempt to support streaming into or
  // from T. We also do not support streaming into or from T on LHS. See below,
  // outside the class, for streaming overrides using `strong_type` only as the
  // value.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t << rhs; }
  friend constexpr strong_type
  operator<<(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ << rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t >> rhs; }
  friend constexpr strong_type
  operator>>(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ >> rhs};
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
  return os << *obj;
}

// Input stream operator (requires that the underlying type supports
// `operator>>`).
template<typename T, typename TAG>
requires requires(std::istream& is, T& value) { is >> value; }
constexpr std::istream&
operator>>(std::istream& is, strong_type<T, TAG>& obj) {
  return is >> *obj;
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
