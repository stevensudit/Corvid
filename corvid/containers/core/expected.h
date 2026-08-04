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
#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

#include "optional_ptr.h"

namespace corvid { inline namespace container { inline namespace expecteds {

// Expected: a result that is either a value or the error explaining its
// absence.
//
// A deliberately constrained alternative to `std::expected`. In exchange for
// requiring that the value and error types be distinct, non-interconvertible,
// and nothrow-movable, it does without the `std::unexpected` shuttle and the
// wide construction surface: a `T` is a value, an `E` is an error, and each
// converts directly.
//
// A failed `expected` converts to a failed `expected` over any other value
// type with the same error type, so propagating a failure out of a function
// whose return type differs needs no repacking:
//
//   using parsed = expected<int, parse_error>;
//   expected<double, parse_error> to_double(const parsed& p) {
//     if (!p) return p;  // The error travels as is.
//     return *p * 2.0;
//   }
//
// Optional access is through `optional_ptr` (`maybe_value`, `maybe_error`),
// whose adapters (`value_or`, `value_or_fn`, and the rest) serve in place of
// a monadic interface.

#pragma region expected

// Result holding either a value `T` or an error `E`, discriminated.
//
// The contract, enforced below: `T` and `E` are object types, neither
// convertible to the other (which also rules out `T` being `E`), and both
// nothrow-movable. Under it, construction from `T` or `E` is unambiguous and
// moves cannot fail, so there is no valueless state to reason about.
//
// Access follows the house grammar: `operator*` and `operator->` reach the
// value and `as_error` the error, each asserting its presence; `maybe_value`
// and `maybe_error` return an `optional_ptr` that is empty when the other
// alternative is engaged.
//
// `Tag` optionally distinguishes same-`T`, same-`E` specializations
// nominally, in the manner of `strong_type`. Error propagation converts
// across value types and tags alike, gated only on an identical `E`.
template<typename T, typename E, typename Tag = void>
class [[nodiscard]] expected final {
public:
  static_assert(std::is_object_v<T> && std::is_object_v<E>,
      "T and E must be object types");
  static_assert(!std::is_convertible_v<T, E> && !std::is_convertible_v<E, T>,
      "T and E must be distinct and non-interconvertible");
  static_assert(std::is_nothrow_move_constructible_v<T> &&
                    std::is_nothrow_move_constructible_v<E>,
      "T and E must be nothrow-movable");

#pragma region Types

  using value_type = T;
  using error_type = E;
  using tag_t = Tag;

#pragma endregion
#pragma region Construction

  // Default is a value-initialized `T`.
  constexpr expected() noexcept(
      std::is_nothrow_default_constructible_v<T>) = default;

  // A `T` is a value; an `E` is an error.
  constexpr expected(const T& v) noexcept(
      std::is_nothrow_copy_constructible_v<T>)
      : v_{std::in_place_index<0>, v} {}
  constexpr expected(T&& v) noexcept
      : v_{std::in_place_index<0>, std::move(v)} {}
  constexpr expected(const E& e) noexcept(
      std::is_nothrow_copy_constructible_v<E>)
      : v_{std::in_place_index<1>, e} {}
  constexpr expected(E&& e) noexcept
      : v_{std::in_place_index<1>, std::move(e)} {}

  // Propagate the error of a failed `expected` over a different value type.
  //
  // Precondition: `other` holds an error.
  template<typename U, typename Tag2>
  requires(!std::same_as<U, T>)
  constexpr expected(const expected<U, E, Tag2>& other) noexcept(
      std::is_nothrow_copy_constructible_v<E>)
      : v_{std::in_place_index<1>, other.as_error()} {}
  template<typename U, typename Tag2>
  requires(!std::same_as<U, T>)
  constexpr expected(expected<U, E, Tag2>&& other) noexcept
      : v_{std::in_place_index<1>, std::move(other).as_error()} {}

#pragma endregion
#pragma region Access

  // Whether there is a value rather than an error.
  [[nodiscard]] constexpr bool has_value() const noexcept {
    return v_.index() == 0;
  }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_value();
  }

  // Access the value, asserting its presence.
  [[nodiscard]] constexpr const T& operator*() const {
    assert(has_value());
    return *std::get_if<0>(&v_);
  }
  [[nodiscard]] constexpr const T* operator->() const {
    assert(has_value());
    return std::get_if<0>(&v_);
  }

  // Access the error, asserting its presence.
  [[nodiscard]] constexpr const E& as_error() const& {
    assert(!has_value());
    return *std::get_if<1>(&v_);
  }
  [[nodiscard]] constexpr E&& as_error() && {
    assert(!has_value());
    return std::move(*std::get_if<1>(&v_));
  }

  // The value, or empty if this is an error.
  [[nodiscard]] constexpr optional_ptr<const T*> maybe_value() const noexcept {
    return std::get_if<0>(&v_);
  }

  // The error, or empty if this is a value.
  [[nodiscard]] constexpr optional_ptr<const E*> maybe_error() const noexcept {
    return std::get_if<1>(&v_);
  }

#pragma endregion
#pragma region Data members
private:
  std::variant<T, E> v_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::container::expecteds
