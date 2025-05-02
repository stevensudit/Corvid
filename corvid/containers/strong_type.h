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

// Concepts for any strong_type and for not being one.
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
// underlying value. These work homogeneously, when both sides are identical.
// They also work heterogeneously, when one side is a different type, whether
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
  static_assert(NotStrongType<T>,
      "strong_type cannot wrap another strong_type");

#pragma region ctors

  constexpr strong_type() noexcept(std::is_nothrow_default_constructible_v<T>)
  requires(std::is_default_constructible_v<T>)
  = default;

  constexpr strong_type(const strong_type&) noexcept(
      std::is_nothrow_copy_constructible_v<T>)
  requires(std::is_copy_constructible_v<T>)
  = default;

  constexpr strong_type(strong_type&&) noexcept(
      std::is_nothrow_move_constructible_v<T>)
  requires(std::is_move_constructible_v<T>)
  = default;

  template<typename U>
  requires(std::is_constructible_v<T, U &&>)
  constexpr explicit strong_type(U&& value) noexcept(
      std::is_nothrow_constructible_v<T, U&&>)
      : value_{std::forward<U>(value)} {}

#pragma endregion ctors
#pragma region assignment

  // Assignment and move.
  constexpr strong_type& operator=(const strong_type& rhs) noexcept(
      std::is_nothrow_copy_assignable_v<T>)
  requires(std::is_copy_assignable_v<T>)
  = default;

  constexpr strong_type&
  operator=(strong_type&& rhs) noexcept(std::is_nothrow_move_assignable_v<T>)
  requires(std::is_move_assignable_v<T>)
  = default;

  template<typename U>
  requires requires(T t, U&& rhs) { t = std::forward<U>(rhs); }
  constexpr strong_type&
  operator=(U&& rhs) noexcept(std::is_nothrow_assignable_v<T&, U&&>) {
    value_ = std::forward<U>(rhs);
    return *this;
  }

#pragma endregion assignment
#pragma region access

  // Access.
  [[nodiscard]] constexpr decltype(auto) value(this auto&& self) noexcept {
    return std::forward_like<decltype(self)>(self.value_);
  }

#pragma endregion access
#pragma region pointers

  // Deference and pointer semantics for access. Unlike `std::optional` or
  // `std::unique_ptr`, there is no possibility of a null. Also, note that
  // mutable references allow changing or moving.
  [[nodiscard]] constexpr decltype(auto) operator*(this auto&& self) noexcept {
    return std::forward_like<decltype(self)>(self.value_);
  }

  [[nodiscard]] constexpr decltype(auto) operator->(
      this auto&& self) noexcept {
    return &std::forward_like<decltype(self)>(self.value_);
  }

  // Note: There is no upside to overloading `operator->*`. At best, it would
  // provide a bit of syntactic sugar to avoid calling an accessor. In reality,
  // this doesn't work very well at all.

#pragma endregion pointers
#pragma region relational

  // Relational operators.

  // Homogeneous relational operators.
  // (Not automatically generated from the spaceship operator because we also
  // specify heterogeneous comparison operators.)
  [[nodiscard]] friend constexpr auto
  operator<=>(const strong_type& lhs, const strong_type& rhs) = default;

  [[nodiscard]] friend constexpr bool
  operator==(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) == 0;
  }
  [[nodiscard]] friend constexpr bool
  operator!=(const strong_type& lhs, const strong_type& rhs) {
    return !(lhs == rhs);
  }
  [[nodiscard]] friend constexpr bool
  operator<(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) < 0;
  }
  [[nodiscard]] friend constexpr bool
  operator<=(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) <= 0;
  }
  [[nodiscard]] friend constexpr bool
  operator>(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) > 0;
  }
  [[nodiscard]] friend constexpr bool
  operator>=(const strong_type& lhs, const strong_type& rhs) {
    return (lhs <=> rhs) >= 0;
  }

  // Heterogeneous relational operators.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t <=> rhs; }
  [[nodiscard]] friend constexpr auto
  operator<=>(const strong_type& lhs, const U& rhs) {
    return lhs.value_ <=> rhs;
  }

  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t == rhs; }
  [[nodiscard]] friend constexpr bool
  operator==(const strong_type& lhs, const U& rhs) {
    return lhs.value_ == rhs;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t != rhs; }
  [[nodiscard]] friend constexpr bool
  operator!=(const strong_type& lhs, const U& rhs) {
    return !(lhs == rhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t < rhs; }
  [[nodiscard]] friend constexpr bool
  operator<(const strong_type& lhs, const U& rhs) {
    return lhs.value_ < rhs;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t <= rhs; }
  [[nodiscard]] friend constexpr bool
  operator<=(const strong_type& lhs, const U& rhs) {
    return !(rhs < lhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t > rhs; }
  [[nodiscard]] friend constexpr bool
  operator>(const strong_type& lhs, const U& rhs) {
    return rhs < lhs.value_;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t >= rhs; }
  [[nodiscard]] friend constexpr bool
  operator>=(const strong_type& lhs, const U& rhs) {
    return !(lhs.value_ < rhs);
  }

  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs <=> t; }
  [[nodiscard]] friend constexpr auto
  operator<=>(const U& lhs, const strong_type& rhs) {
    return lhs <=> rhs.value_;
  }

  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs == t; }
  [[nodiscard]] friend constexpr bool
  operator==(const U& lhs, const strong_type& rhs) {
    return lhs == rhs.value_;
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs != t; }
  [[nodiscard]] friend constexpr bool
  operator!=(const U& lhs, const strong_type& rhs) {
    return !(lhs == rhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs < t; }
  [[nodiscard]] friend constexpr bool
  operator<(const U& lhs, const strong_type& rhs) {
    return lhs < rhs.value_;
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs <= t; }
  [[nodiscard]] friend constexpr bool
  operator<=(const U& lhs, const strong_type& rhs) {
    return !(rhs.value_ < lhs);
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs > t; }
  [[nodiscard]] friend constexpr bool
  operator>(const U& lhs, const strong_type& rhs) {
    return rhs.value_ < lhs;
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs >= t; }
  [[nodiscard]] friend constexpr bool
  operator>=(const U& lhs, const strong_type& rhs) {
    return !(lhs < rhs.value_);
  }

#pragma endregion relational
#pragma region unary

  // Unary operators.
  template<typename = void>
  requires requires(T t) { +t; }
  [[nodiscard]] constexpr strong_type operator+() const {
    return *this;
  }
  template<typename = void>
  requires requires(T t) { -t; }
  [[nodiscard]] constexpr strong_type operator-() const {
    return strong_type{-value_};
  }
  template<typename = void>
  requires requires(T t) { ~t; }
  [[nodiscard]] constexpr strong_type operator~() const {
    return strong_type{~value_};
  }
  template<typename = void>
  requires requires(T t) { !t; }
  [[nodiscard]] constexpr strong_type operator!() const {
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

  template<typename = void>
  requires requires(T t) { t ? true : false; }
  [[nodiscard]] explicit operator bool() const {
    return value_ ? true : false;
  }
  [[nodiscard]] explicit operator const T&() const { return value_; }
  [[nodiscard]] explicit operator T&() { return value_; }

#pragma endregion unary
#pragma region binarymath

  // Binary arithmetic operators.

  // Homogeneous binary arithmetic operators.
  template<typename = void>
  requires requires(T t) { t + t; }
  [[nodiscard]] constexpr strong_type operator+(const strong_type& rhs) const {
    return strong_type{value_ + rhs.value_};
  }
  template<typename = void>
  requires requires(T t) { t - t; }
  [[nodiscard]] constexpr strong_type operator-(const strong_type& rhs) const {
    return strong_type{value_ - rhs.value_};
  }
  template<typename = void>
  requires requires(T t) { t * t; }
  [[nodiscard]] constexpr strong_type operator*(const strong_type& rhs) const {
    return strong_type{value_ * rhs.value_};
  }
  template<typename = void>
  requires requires(T t) { t / t; }
  [[nodiscard]] constexpr strong_type operator/(const strong_type& rhs) const {
    return strong_type{value_ / rhs.value_};
  }
  template<typename = void>
  requires requires(T t) { t % t; }
  [[nodiscard]] constexpr strong_type operator%(const strong_type& rhs) const {
    return strong_type{value_ % rhs.value_};
  }

  // Heterogeneous arithmetic operators.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t + rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator+(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ + rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t - rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator-(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ - rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t * rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator*(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ * rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t / rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator/(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ / rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t % rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator%(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ % rhs};
  }

  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs + t; }
  [[nodiscard]] friend constexpr strong_type
  operator+(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs + rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs - t; }
  [[nodiscard]] friend constexpr strong_type
  operator-(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs - rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs * t; }
  [[nodiscard]] friend constexpr strong_type
  operator*(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs * rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs / t; }
  [[nodiscard]] friend constexpr strong_type
  operator/(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs / rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs % t; }
  [[nodiscard]] friend constexpr strong_type
  operator%(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs % rhs.value_};
  }

#pragma endregion binarymath
#pragma region binarybitwise

  // Binary bitwise operators.

  // Homogeneous binary bitwise operators.
  template<typename = void>
  requires requires(T t) { t & t; }
  [[nodiscard]] constexpr strong_type operator&(const strong_type& rhs) const {
    return strong_type{value_ & rhs.value_};
  }
  template<typename = void>
  requires requires(T t) { t | t; }
  [[nodiscard]] constexpr strong_type operator|(const strong_type& rhs) const {
    return strong_type{value_ | rhs.value_};
  }
  template<typename = void>
  requires requires(T t) { t ^ t; }
  [[nodiscard]] constexpr strong_type operator^(const strong_type& rhs) const {
    return strong_type{value_ ^ rhs.value_};
  }
  template<typename = void>
  requires requires(T t) { t << t; }
  [[nodiscard]] constexpr strong_type
  operator<<(const strong_type& rhs) const {
    return strong_type{value_ << rhs.value_};
  }
  template<typename = void>
  requires requires(T t) { t >> t; }
  [[nodiscard]] constexpr strong_type
  operator>>(const strong_type& rhs) const {
    return strong_type{value_ >> rhs.value_};
  }

  // Heterogeneous binary bitwise operators.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t & rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator&(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ & rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t | rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator|(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ | rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t ^ rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator^(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ ^ rhs};
  }

  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs & t; }
  [[nodiscard]] friend constexpr strong_type
  operator&(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs & rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs | t; }
  [[nodiscard]] friend constexpr strong_type
  operator|(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs | rhs.value_};
  }
  template<NotStrongType U>
  requires requires(T t, const U& lhs) { lhs ^ t; }
  [[nodiscard]] friend constexpr strong_type
  operator^(const U& lhs, const strong_type& rhs) {
    return strong_type{lhs ^ rhs.value_};
  }

  // Heterogeneous bitwise shift operators.
  //
  // We do not attempt to support streaming into or from T. We also do not
  // support streaming into or from T on LHS. See below, outside the class,
  // for streaming overrides using `strong_type` only as the value.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t << rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator<<(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ << rhs};
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t >> rhs; }
  [[nodiscard]] friend constexpr strong_type
  operator>>(const strong_type& lhs, const U& rhs) {
    return strong_type{lhs.value_ >> rhs};
  }

#pragma endregion binarybitwise
#pragma region assignmath

  // Arithmetic assignment operators.

  // Homogeneous arithmetic assignment operators.
  template<typename = void>
  requires requires(T t) { t += t; }
  constexpr strong_type& operator+=(const strong_type& rhs) {
    value_ += rhs.value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t -= t; }
  constexpr strong_type& operator-=(const strong_type& rhs) {
    value_ -= rhs.value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t *= t; }
  constexpr strong_type& operator*=(const strong_type& rhs) {
    value_ *= rhs.value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t /= t; }
  constexpr strong_type& operator/=(const strong_type& rhs) {
    value_ /= rhs.value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t %= t; }
  constexpr strong_type& operator%=(const strong_type& rhs) {
    value_ %= rhs.value_;
    return *this;
  }

  // Heterogeneous arithmetic assignment operators.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t += rhs; }
  constexpr strong_type& operator+=(const U& rhs) {
    value_ += rhs;
    return *this;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t -= rhs; }
  constexpr strong_type& operator-=(const U& rhs) {
    value_ -= rhs;
    return *this;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t *= rhs; }
  constexpr strong_type& operator*=(const U& rhs) {
    value_ *= rhs;
    return *this;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t /= rhs; }
  constexpr strong_type& operator/=(const U& rhs) {
    value_ /= rhs;
    return *this;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t %= rhs; }
  constexpr strong_type& operator%=(const U& rhs) {
    value_ %= rhs;
    return *this;
  }

#pragma endregion assignmath
#pragma region assignbitwise

  // Bitwise assignment operators.

  // Homogeneous bitwise assignment operators.
  template<typename = void>
  requires requires(T t) { t &= t; }
  constexpr strong_type& operator&=(const strong_type& rhs) {
    value_ &= rhs.value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t |= t; }
  constexpr strong_type& operator|=(const strong_type& rhs) {
    value_ |= rhs.value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t ^= t; }
  constexpr strong_type& operator^=(const strong_type& rhs) {
    value_ ^= rhs.value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t <<= t; }
  constexpr strong_type& operator<<=(const strong_type& rhs) {
    value_ <<= rhs.value_;
    return *this;
  }
  template<typename = void>
  requires requires(T t) { t >>= t; }
  constexpr strong_type& operator>>=(const strong_type& rhs) {
    value_ >>= rhs.value_;
    return *this;
  }

  // Heterogeneous bitwise assignment operators.
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t &= rhs; }
  constexpr strong_type& operator&=(const U& rhs) {
    value_ &= rhs;
    return *this;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t |= rhs; }
  constexpr strong_type& operator|=(const U& rhs) {
    value_ |= rhs;
    return *this;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t ^= rhs; }
  constexpr strong_type& operator^=(const U& rhs) {
    value_ ^= rhs;
    return *this;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t <<= rhs; }
  constexpr strong_type& operator<<=(const U& rhs) {
    value_ <<= rhs;
    return *this;
  }
  template<NotStrongType U>
  requires requires(T t, const U& rhs) { t >>= rhs; }
  constexpr strong_type& operator>>=(const U& rhs) {
    value_ >>= rhs;
    return *this;
  }

#pragma endregion assignbitwise
#pragma region iteration

  // Iteration.
  template<typename = void>
  requires requires(T t) { t.begin(); }
  [[nodiscard]] constexpr auto begin() const {
    return value_.begin();
  }
  template<typename = void>
  requires requires(T t) { t.end(); }
  [[nodiscard]] constexpr auto end() const {
    return value_.end();
  }

  template<typename = void>
  requires requires(T t) { t.rbegin(); }
  [[nodiscard]] constexpr auto rbegin() const {
    return value_.rbegin();
  }
  template<typename = void>
  requires requires(T t) { t.rend(); }
  [[nodiscard]] constexpr auto rend() const {
    return value_.rend();
  }
#pragma endregion iteration
#pragma region braces

  // Function call returning void.
  template<typename... Args>
  requires CallableReturningVoid<T&, Args...>
  constexpr void operator()(Args&&... args) {
    std::invoke(value_, std::forward<Args>(args)...);
  }
  template<typename... Args>
  requires CallableReturningVoid<const T&, Args...>
  constexpr void operator()(Args&&... args) const {
    std::invoke(value_, std::forward<Args>(args)...);
  }

  // Function call returning a non-discardable value.
  template<typename... Args>
  requires CallableReturningNonVoid<T&, Args...>
  [[nodiscard]]
  constexpr auto operator()(Args&&... args) {
    return std::invoke(value_, std::forward<Args>(args)...);
  }
  template<typename... Args>
  requires CallableReturningNonVoid<const T&, Args...>
  [[nodiscard]]
  constexpr auto operator()(Args&&... args) const {
    return std::invoke(value_, std::forward<Args>(args)...);
  }

  // Collection.
  template<typename Key>
  requires requires(T t, const Key& key) { t[key]; }
  [[nodiscard]] constexpr auto& operator[](const Key& key) {
    return value_[key];
  }
  template<typename Key>
  requires requires(T t, const Key& key) { t[key]; }
  [[nodiscard]] constexpr const auto& operator[](const Key& key) const {
    return value_[key];
  }

#pragma endregion braces

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
    return std::hash<T>{}(obj.value());
  }
};
} // namespace std
