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
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "../../meta/fixed_string.h"
#include "optional_ptr.h"

namespace corvid { inline namespace container {
inline namespace value_or_errors {

// A `value_or_error` is a result that is either the value or the error
// explaining its absence. Like `std::expected`, it is parallel to a
// `std::optional`, except that it carries an error message instead of being
// empty.
//
// In fact, this class is a deliberately reduced alternative to
// `std::expected`: an improper subset, and blunt about it. In exchange for
// requiring that the value and error types be distinct, non-interconvertible,
// and nothrow-movable, it does without the `std::unexpected` shuttle and the
// wide construction surface.
//
// It's therefore simpler and more reliable in use because a `T` is a value, an
// `E` is an error, and each converts directly. The motto here is: expect less,
// receive more.
//
// This is what it looks like in action:
//
//   using result = value_or_error<std::string, rejection_reason>;
//   return "Great success!"s;  // a value
//   return rejection_reason::great_failure;  // an error
//
// Results also convert across specializations, along whichever wing they
// share.
//
// This means that a failed `value_or_error<T, E>` converts to a failed
// `value_or_error<U, E>` by copying that `E` error value, even though `T` and
// `U` are different.
//
// A successful `value_or_error<T, E>` converts to a successful
// `value_or_error<T, F>`, copying that `T` success value, even though `E` and
// `F` are different.
//
// Propagation in either direction comes down to returning what you already
// have:
//
//   if (!r) return r;  // The error travels as is.
//   if (r) return r;   // So does the value.
//
// Optional access uses `maybe_value` and `maybe_error`. These return an
// `optional_ptr`, which is  akin to `std::optional` but contains a pointer
// instead of a copy. Its adapters (`value_or`, `value_or_fn`, and the rest)
// provide a monadic interface so this class doesn't have to. Less is more.

#pragma region value_or_error

// `value_or_error` holds either a value `T` or an error `E`, discriminated.
//
// The enforced contract: `T` and `E` are object types or lvalue references,
// neither convertible to the other (which also rules out `T` being `E`), and
// nothrow-movable as stored. Construction from `T` or `E` is unambiguous and
// moves cannot fail, so there is no valueless state to reason about.
// Assignment converts exactly as construction does.
//
// A cv-qualified wing is rejected: access is already read-only, so `const`
// would add nothing while deleting assignment. A const referent belongs in
// a reference wing, which is fine because the stored wrapper stays
// assignable.
//
// What the narrowness costs is deliberate, not an oversight:
// - No construction from a type merely convertible to `T` or `E`. One type
//   convertible to both would be ambiguous, so the conversion is spelled at
//   the call site instead. In particular, a bare string literal is a `char[]`,
//   not a `std::string`, so write `"reason"s`.
// - Access is a narrow contract: check before reaching in. Failing to check
//   is a logic error in the caller, so it is asserted, not converted into a
//   runtime error.
// - No throwing `value` accessor: the whole point is to signal failure more
//   gently than a throw, and a throwing accessor would reward skipping the
//   check. A caller who wants `if (!r) throw x;` can write exactly that.
// - No `emplace` and no mutable access: a result is a lightweight,
//   fire-and-forget return value. To "change" its contents, assign a fresh
//   one.
//
// Access follows the model of `std::optional`, with `operator*` and
// `operator->` to reach the value, and `as_error` to get the error, each
// asserting its presence but intentionally not throwing. The `maybe_value` and
// `maybe_error` methods return an `optional_ptr` that is empty when the other
// alternative is engaged, allowing you to do the check and then just
// dereference. For example:
//
//  // Access and convert in one step; no need to call`as_error`.
//  if (const auto e = r.maybe_error()) return handle(*e);
//  // No need to call `r.as_value()` here and check, because we know already.
//  return use(*r);
//
// Distinctness between results comes directly from the distinctness of the `T`
// and `E` types. Typically, `E` is custom to a particular function, while `T`
// is a well-known value type, such as `std::string`. However, it's just as
// possible, if perhaps less common, to have a few result types share the `E`,
// such as an enum, while using whichever `T` is appropriate to that function.
//
// It works logically in any combination because propagation follows the
// overlap. An error travels to any result with the same `E`, a value to any
// result with the same `T`, and specializations sharing neither have no common
// ground that would allow conversion.
//
// To keep two same-shaped results apart, give them distinct error types;
// the optional `error_value` utility class, below, makes that a one-liner, but
// you aren't limited to it.
//
// You can use references or pointers for either wing: it just works. Of
// course, the referent must outlive the result, so construction from a
// temporary (including one materialized by conversion) is deleted. The stored
// form is a `std::reference_wrapper`, but the accessors return the reference
// itself, without a `get` ceremony. The `value_type` and `error_type`
// report the stored forms.
//
// A value-less result (did it work, and if not, why not) is spelled
// `value_or_error<std::monostate, E>`.
template<typename T, typename E>
class [[nodiscard]] value_or_error final {
public:
#pragma region Types

  // Map a declared type to its stored form: a reference is stored as a
  // `std::reference_wrapper`.
  template<typename X>
  using stored_t = std::conditional_t<std::is_reference_v<X>,
      std::reference_wrapper<std::remove_reference_t<X>>, X>;

  using value_type = stored_t<T>;
  using error_type = stored_t<E>;

  static_assert((std::is_object_v<T> || std::is_lvalue_reference_v<T>) &&
                    (std::is_object_v<E> || std::is_lvalue_reference_v<E>),
      "T and E must be object types or lvalue references");
  static_assert(!std::is_const_v<T> && !std::is_volatile_v<T> &&
                    !std::is_const_v<E> && !std::is_volatile_v<E>,
      "T and E must not be cv-qualified: access is already read-only");
  static_assert(!std::is_convertible_v<T, E> && !std::is_convertible_v<E, T>,
      "T and E must be distinct and non-interconvertible");
  static_assert(std::is_nothrow_move_constructible_v<stored_t<T>> &&
                    std::is_nothrow_move_constructible_v<stored_t<E>>,
      "T and E must be nothrow-movable as stored");

private:
  // Access and intake spellings per wing. A reference wing hands back the
  // reference itself; `*_take` is the sink parameter, which for a reference
  // wing is the rvalue overload that gets deleted.
  using value_ref = std::conditional_t<std::is_reference_v<T>, T, const T&>;
  using value_move = std::conditional_t<std::is_reference_v<T>, T, T&&>;
  using value_take = std::conditional_t<std::is_reference_v<T>,
      std::remove_reference_t<T>&&, T&&>;
  using value_ptr = std::add_pointer_t<std::remove_reference_t<value_ref>>;

  using error_ref = std::conditional_t<std::is_reference_v<E>, E, const E&>;
  using error_move = std::conditional_t<std::is_reference_v<E>, E, E&&>;
  using error_take = std::conditional_t<std::is_reference_v<E>,
      std::remove_reference_t<E>&&, E&&>;
  using error_ptr = std::add_pointer_t<std::remove_reference_t<error_ref>>;

#pragma endregion
#pragma region Construction
public:
  // Default is an error holding a value-initialized `E`.
  //
  // A result made from nothing has no value to claim, so `return {};` reports
  // a generic failure. (`std::expected` defaults to a value instead; we
  // consider that a phantom success.)
  constexpr value_or_error() noexcept(
      std::is_nothrow_default_constructible_v<stored_t<E>>)
  requires(std::is_default_constructible_v<stored_t<E>>)
      : v_{std::in_place_type<error_type>} {}

  // Implicit conversion from `T` and `E`.
  //
  // A `T` is a value; an `E` is an error. A reference `T` or `E` binds an
  // lvalue and stores the binding, not a copy; there its rvalue overload is
  // deleted, and since a conversion result is a prvalue, that also rejects a
  // temporary materialized by conversion.
  constexpr value_or_error(value_ref v) noexcept(
      std::is_nothrow_copy_constructible_v<stored_t<T>>)
      : v_{std::in_place_type<value_type>, v} {}

  constexpr value_or_error(value_take v) noexcept
  requires(!std::is_reference_v<T>)
      : v_{std::in_place_type<value_type>, std::move(v)} {}

  value_or_error(value_take)
  requires(std::is_reference_v<T>)
  = delete;

  constexpr value_or_error(error_ref e) noexcept(
      std::is_nothrow_copy_constructible_v<stored_t<E>>)
      : v_{std::in_place_type<error_type>, e} {}

  constexpr value_or_error(error_take e) noexcept
  requires(!std::is_reference_v<E>)
      : v_{std::in_place_type<error_type>, std::move(e)} {}

  value_or_error(error_take)
  requires(std::is_reference_v<E>)
  = delete;

  // Implicit conversion of error from compatible `value_or_error`.
  //
  // Precondition: `other` holds an error.
  template<typename U>
  requires(!std::same_as<U, T>)
  constexpr value_or_error(const value_or_error<U, E>& other) noexcept(
      std::is_nothrow_copy_constructible_v<stored_t<E>>)
      : v_{std::in_place_type<error_type>, other.as_error()} {}

  template<typename U>
  requires(!std::same_as<U, T>)
  constexpr value_or_error(value_or_error<U, E>&& other) noexcept
      : v_{std::in_place_type<error_type>, std::move(other).as_error()} {}

  // Implicit conversion of value from compatible `value_or_error`.
  //
  // Precondition: `other` holds a value.
  template<typename F>
  requires(!std::same_as<F, E>)
  constexpr value_or_error(const value_or_error<T, F>& other) noexcept(
      std::is_nothrow_copy_constructible_v<stored_t<T>>)
      : v_{std::in_place_type<value_type>, *other} {}

  template<typename F>
  requires(!std::same_as<F, E>)
  constexpr value_or_error(value_or_error<T, F>&& other) noexcept
      : v_{std::in_place_type<value_type>, *std::move(other)} {}

#pragma endregion
#pragma region Access
private:
  // Return the stored pointer for the `X` wing, unwrapping a reference wing,
  // or null when the other wing is engaged. Constness follows `self` for an
  // object wing and the referent for a reference wing.
  //
  // The wing is named by its own type rather than by index, which the
  // distinctness of `T` and `E` guarantees is unambiguous.
  template<typename X>
  constexpr auto do_get_ptr(this auto&& self) noexcept {
    auto p = std::get_if<stored_t<X>>(&self.v_);
    if constexpr (std::is_reference_v<X>)
      return p ? &p->get() : nullptr;
    else
      return p;
  }

public:
  // Whether there is a value rather than an error.
  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<value_type>(v_);
  }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_value();
  }

  // Access the value, asserting its presence.
  [[nodiscard]] constexpr value_ref operator*() const& {
    assert(has_value());
    return *do_get_ptr<T>();
  }
  [[nodiscard]] constexpr value_move operator*() && {
    assert(has_value());
    return static_cast<value_move>(*do_get_ptr<T>());
  }
  [[nodiscard]] constexpr value_ptr operator->() const {
    assert(has_value());
    return do_get_ptr<T>();
  }

  // Access the error, asserting its presence.
  [[nodiscard]] constexpr error_ref as_error() const& {
    assert(!has_value());
    return *do_get_ptr<E>();
  }
  [[nodiscard]] constexpr error_move as_error() && {
    assert(!has_value());
    return static_cast<error_move>(*do_get_ptr<E>());
  }

  // The value, or empty if this is an error.
  [[nodiscard]] constexpr optional_ptr<value_ptr>
  maybe_value() const noexcept {
    return do_get_ptr<T>();
  }

  // The error, or empty if this is a value.
  [[nodiscard]] constexpr optional_ptr<error_ptr>
  maybe_error() const noexcept {
    return do_get_ptr<E>();
  }

#pragma endregion
#pragma region Data members
private:
  std::variant<stored_t<T>, stored_t<E>> v_;

#pragma endregion
};

#pragma endregion
#pragma region error_value

// Strongly typed error reason.
//
// Companion for making one error type distinct from another: `Tag` gives the
// wrapped reason a nominal identity, in the manner of `strong_type`, and it
// is mandatory because distinctness is the whole point. A distinct error
// type keeps failures from crossing between the `value_or_error`
// specializations built on it; a good value still crosses, by way of
// success propagation.
//
// The reason type defaults to `std::string`, and `Default` supplies the
// reason's initial value, so the minimal spelling names only the domain and
// default-constructs to "Unknown error". That composes with
// `value_or_error`'s own default constructor: `return {};` is a
// self-describing generic failure. A domain with its own reason type passes
// its own default, such as an enumerator.
//
// Spell an error by naming its type: `parse_error{"bad digit"}`. The
// anonymous `return {{"bad digit"}};` spelling is an anti-pattern: it
// resolves by conversion rank, so when `T` can also absorb the inner list
// it silently constructs a value instead of an error.
//
//   using parse_error = error_value<struct ParseTag>;
//   using io_error = error_value<struct IoTag, io_errc, io_errc::unknown>;
template<typename Tag, typename E = std::string,
    auto Default = basic_fixed_string{"Unknown error"}>
struct error_value final {
  static_assert(
      requires { E{Default}; }, "Default must be able to initialize E");
  E reason{Default};
};

#pragma endregion

}}} // namespace corvid::container::value_or_errors
