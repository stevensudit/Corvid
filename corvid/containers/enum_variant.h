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
#include <variant>

#include "../meta/concepts.h"

namespace corvid { inline namespace container { inline namespace rust_like {

#pragma region Underlying variant helpers.

// Helpers that wrap variant-related `std` free functions to work equally well
// with `enum_variant`.

// Indirect check for `enum_variant`.
template<typename T>
concept HasUnderlyingType = requires {
  typename std::decay_t<T>::underlying_type;
};

// Get the underlying type of a variant, which is either the type itself
// or the type of its underlying value if it has one.
template<typename T>
using underlying_variant_type_t = std::conditional_t<requires {
  HasUnderlyingType<T>;
}, typename std::decay_t<T>::underlying_type, std::decay_t<T>>;

// Check if `T` has a `visit` member template that can be called with a
// variant of type `V`.
template<typename T, typename V>
concept HasVisitMemberTemplate = requires(const T& t, const V& v) {
  {
    t.visit(v)
  };
};

// Variant size, which works on `enum_variant` and `std::variant`.
template<typename T>
constexpr std::size_t variant_size_v =
    std::variant_size_v<underlying_variant_type_t<T>>;

// Return the variant, which is either the parameter or its underlying value.
template<typename T>
constexpr decltype(auto) extract_variant(T&& v) {
  if constexpr (HasUnderlyingType<T>) {
    return std::forward<T>(v).get_underlying();
  } else {
    return std::forward<T>(v);
  }
}

// Variant index, which works on `enum_variant` and `std::variant`.
template<std::size_t I, typename Variant>
constexpr decltype(auto) variant_get(Variant&& v) {
  return std::get<I>(extract_variant(std::forward<Variant>(v)));
}

#pragma endregion
#pragma region Tag types.

// Replacement for `std::in_place_index_t` tagged value that uses scoped enums
// instead of `size_t` for the index.
template<auto V>
struct in_place_enum_t {
  static_assert(
      std::is_enum_v<decltype(V)> && std::is_scoped_enum_v<decltype(V)>,
      "in_place_enum_t requires a scoped enum value");

  explicit in_place_enum_t() = default;
};

// Like `std::in_place_index`, but for `in_place_enum_t`.
template<auto V>
inline constexpr in_place_enum_t<V> in_place_enum{};

// C++26 promises `std::index_constant`.
template<std::size_t N>
using index_constant = std::integral_constant<std::size_t, N>;

#pragma endregion
#pragma region callback wrappers

// Callbacks.

// Overloaded callbacks.
//
// A variation on the standard `overloads` helper class for `std::visit`, which
// allows for a visitor to be constructed from multiple lambdas overloaded on
// the types contained within the variant.
//
// That works by inheriting from each of the lambdas, and using the
// `using Lambdas::operator()...;` trick to allow the overloaded operator() to
// be called with the correct lambda based on the type of the variant value.
//
// This variation includes `visit` methods and works correctly with
// `enum_variant` as well as `std::variant`.
template<typename... Lambdas>
struct overloaded_callbacks: Lambdas... {
  using Lambdas::operator()...;

  // Visit the variant with this callback.
  template<typename Variant>
  constexpr decltype(auto) visit(Variant&& v) const {
    return std::visit(*this, extract_variant(std::forward<Variant>(v)));
  }
  template<typename R, typename Variant>
  constexpr decltype(auto) visit(Variant&& v) const {
    return std::visit<R>(*this, extract_variant(std::forward<Variant>(v)));
  }
};

// Indexed callbacks.
//
// Like the `overloaded` helper documented for `std::visit`, except that it's
// designed for calls indexed on which element of the variant is present, as
// opposed to matching on overloads.
//
// Works with `enum_variant` or `std::variant`.
//
// The constructor must be passed a lambda for each of the values in the
// variant, with each one taking the type of the corresponding value. All
// lambdas must return the same type (which could be void).
//
// Note: This cannot be used with `std::visit` as it is not a proper callable.
// You can only use it with the member `visit`.
template<typename... Lambdas>
struct indexed_callbacks {
  constexpr static std::size_t size_v = sizeof...(Lambdas);

  // The tuple stores lambdas corresponding to each index of the variant.
  std::tuple<Lambdas...> callbacks;

  // Fill each index of the tuple with the corresponding lambda.
  constexpr indexed_callbacks(Lambdas... ls) : callbacks(std::move(ls)...) {}

  // Call the Ith lambda with the argument. This uses the `callbacks` member,
  // which is filled in automatically from the constructor. (Note the use of a
  // tag type to pass the index.)
  template<std::size_t I, typename Arg>
  constexpr decltype(auto) operator()(index_constant<I>, Arg&& arg) const {
    return std::get<I>(callbacks)(std::forward<Arg>(arg));
  }

  // Visit the variant with the indexed callback.
  template<typename Variant>
  constexpr decltype(auto) visit(Variant&& v) const {
    static_assert(variant_size_v<Variant> == size_v, "Must be exhaustive");
    return visit(*this, std::forward<Variant>(v));
  }
  template<typename R, typename Variant>
  constexpr R visit(Variant&& v) const {
    static_assert(variant_size_v<Variant> == size_v, "Must be exhaustive");
    return visit<R>(*this, std::forward<Variant>(v));
  }

  // Static implementation of the visit function.
  template<typename Callback, typename Variant>
  static constexpr decltype(auto) visit(Callback&& cb, Variant&& v) {
    // Default the return type to the common one.
    using IndexSeq = std::make_index_sequence<variant_size_v<Variant>>;
    return [&]<std::size_t... Is>(
               std::index_sequence<Is...>) -> decltype(auto) {
      using Ret = std::common_type_t<decltype(cb(index_constant<Is>{},
          variant_get<Is>(std::forward<Variant>(v))))...>;
      // Now that we've deduced the return type, we specify it.
      return visit<Ret>(std::forward<Callback>(cb), std::forward<Variant>(v));
    }(IndexSeq{});
  }
  template<typename R, typename Callback, typename Variant>
  static constexpr R visit(Callback&& cb, Variant&& v) {
    // Define an immediately-invoked lambda to expand the index sequence.
    using IndexSeq = std::make_index_sequence<variant_size_v<Variant>>;
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> R {
      // Define a function pointer type that takes the variant and callback,
      // and returns the common type. This effectively erases the types of the
      // callbacks.
      using Fn = R (*)(Variant&&, Callback&&);
      // Create a table of function pointers, one for each index of the
      // variant. Each function pointer calls the corresponding lambda with
      // the index and the value at that index in the variant.
      constexpr Fn table[] = {[](Variant&& var, Callback&& f) -> R {
        return f(index_constant<Is>{},
            variant_get<Is>(std::forward<Variant>(var)));
      }...};
      // Invoke the callback for the current index of the variant.
      const auto idx = static_cast<size_t>(v.index());
      return table[idx](std::forward<Variant>(v), std::forward<Callback>(cb));
    }(IndexSeq{});
  }
};
#pragma endregion

//
// Enum variant.
//
// Similar to Rust's `enum`, this is a wrapper over `std::variant` that exposes
// the index as a scoped enum type.
//
// Specialize on a scoped enum type `E` and a list of types `Ts...` for the
// underlying variant. `E` should have defined values from 0 up to N-1. For
// enum values with no corresponding variant type, use `std::monostate` or
// declare a trivial struct.
//
// As with `std::variant`, each `T` should ideally be distinct, which is easy
// enough if you use `strong_type`. But even if they're not, you can still
// visit based on index.
template<ScopedEnumType E, typename... Ts>
class enum_variant {
public:
  using underlying_type = std::variant<Ts...>;
  using enum_type = E;
  static constexpr enum_type variant_npos = -1;
  static constexpr std::size_t variant_size = sizeof...(Ts);

#pragma region Construct and Assign

  //
  // Constructors.
  //

  // Default constructor.
  constexpr enum_variant() noexcept(
      std::is_nothrow_default_constructible_v<underlying_type>)
  requires std::is_default_constructible_v<underlying_type>
  = default;

  // Copy constructor.
  constexpr enum_variant(const enum_variant& other)
  requires std::is_copy_constructible_v<underlying_type>
  = default;

  // Move constructor.
  constexpr enum_variant(enum_variant&& other) noexcept(
      std::is_nothrow_move_constructible_v<underlying_type>)
  requires std::is_move_constructible_v<underlying_type>
  = default;

  // Compile-time conversion constructor from constexpr enum index. Only works
  // if the value corresonding to the index is constexpr default-constructible.
  // Note that, being implicit, this lets you assign an `enum_type` to an
  // `enum_variant`.
  consteval enum_variant(enum_type e)
      : value_(construct(static_cast<std::size_t>(e))) {}

  // Conversion constructor, where `T` is one of the types in `Ts...`.
  template<typename T>
  requires std::is_constructible_v<underlying_type, T&&>
  constexpr enum_variant(T&& t) noexcept(
      std::is_nothrow_constructible_v<underlying_type, T&&>)
      : value_{std::forward<T>(t)} {}

  // Emplace constructor for a specific type `T`. Consider using `make<T>()`
  // instead.
  template<typename T, typename... Args>
  constexpr explicit enum_variant(std::in_place_type_t<T>,
      Args&&... args) noexcept(std::is_nothrow_constructible_v<underlying_type,
      std::in_place_type_t<T>, Args&&...>)
  requires std::is_constructible_v<underlying_type, std::in_place_type_t<T>,
      Args&&...>
      : value_{std::in_place_type<T>, std::forward<Args>(args)...} {}

  // Emplace constructor for a specific type `T` with an initializer list.
  // Consider using `make<T>()` instead.
  template<typename T, typename U, typename... Args>
  constexpr explicit enum_variant(std::in_place_type_t<T>,
      std::initializer_list<U> il, Args&&... args)
  requires std::is_constructible_v<underlying_type, std::in_place_type_t<T>,
      std::initializer_list<U>, Args&&...>
      : value_{std::in_place_type<T>, il, std::forward<Args>(args)...} {}

  // Emplace constructor by enum index. Consider using `make<T>()` instead.
  template<auto V, typename... Args>
  constexpr explicit enum_variant(in_place_enum_t<V>,
      Args&&... args) noexcept(std::is_nothrow_constructible_v<underlying_type,
      std::in_place_index_t<static_cast<std::size_t>(V)>, Args&&...>)
  requires std::is_constructible_v<underlying_type,
      std::in_place_index_t<static_cast<std::size_t>(V)>, Args&&...>
      : value_{std::in_place_index<static_cast<std::size_t>(V)>,
            std::forward<Args>(args)...} {}

  // Emplace constructor by enum index with an initializer list. Consider using
  // `make<T>()` instead.
  template<auto V, typename U, typename... Args>
  constexpr explicit enum_variant(in_place_enum_t<V>,
      std::initializer_list<U> il, Args&&... args)
  requires std::is_constructible_v<underlying_type,
      std::in_place_index_t<static_cast<std::size_t>(V)>,
      std::initializer_list<U>, Args&&...>
      : value_{std::in_place_index<static_cast<std::size_t>(V)>, il,
            std::forward<Args>(args)...} {}

  // Make on `enum_type`, with arguments. Avoids the `in_place_enum` hack.
  template<enum_type I, typename... Args>
  [[nodiscard]] static constexpr enum_variant make(
      Args&&... args) noexcept(std::is_nothrow_constructible_v<underlying_type,
      std::in_place_index_t<static_cast<std::size_t>(I)>, Args&&...>) {
    return enum_variant(in_place_enum<I>, std::forward<Args>(args)...);
  }

  // Make on `enum_type`, with an initializer list.
  template<enum_type I, typename U, typename... Args>
  [[nodiscard]] static constexpr enum_variant make(std::initializer_list<U> il,
      Args&&... args) noexcept(std::is_nothrow_constructible_v<underlying_type,
      std::in_place_index_t<static_cast<std::size_t>(I)>,
      std::initializer_list<U>, Args&&...>) {
    return enum_variant(in_place_enum<I>, il, std::forward<Args>(args)...);
  }

  // Copy conversion constructor from `underlying_variant`.
  constexpr explicit enum_variant(const underlying_type& v) noexcept(
      std::is_nothrow_copy_constructible_v<underlying_type>)
  requires std::is_copy_constructible_v<underlying_type>
      : value_{v} {}

  // Move conversion constructor from `underlying_variant`.
  constexpr explicit enum_variant(underlying_type&& v) noexcept(
      std::is_nothrow_move_constructible_v<underlying_type>)
  requires std::is_move_constructible_v<underlying_type>
      : value_{std::move(v)} {}

  //
  // Assignment.
  //

  // Assignment by value.
  constexpr enum_variant& operator=(const enum_variant& v) noexcept(
      std::is_nothrow_copy_assignable_v<underlying_type>)
  requires std::is_copy_assignable_v<underlying_type>
  = default;

  // Assignment by move.
  constexpr enum_variant& operator=(enum_variant&& v) noexcept(
      std::is_nothrow_move_assignable_v<underlying_type>)
  requires std::is_move_assignable_v<underlying_type>
  = default;

  // Conversion assignment from `T`.
  template<typename T>
  requires std::is_constructible_v<underlying_type, T>
  constexpr enum_variant& operator=(T&& t) noexcept(
      std::is_nothrow_constructible_v<underlying_type, T&&>) {
    value_ = std::forward<T>(t);
    return *this;
  }

  // Conversion assigment from copy of `underlying_variant`.
  constexpr enum_variant& operator=(const underlying_type& v) noexcept(
      std::is_nothrow_copy_assignable_v<underlying_type>)
  requires std::is_copy_assignable_v<underlying_type>
  {
    value_ = v;
    return *this;
  }

  // Conversion assignment from move of `underlying_variant`.
  constexpr enum_variant& operator=(underlying_type&& v) noexcept(
      std::is_nothrow_move_assignable_v<underlying_type>)
  requires std::is_move_assignable_v<underlying_type>
  {
    value_ = std::move(v);
    return *this;
  }

  //
  // Emplace
  //

  // Emplace a value of type `T`.
  template<typename T, typename... Args>
  requires std::is_constructible_v<underlying_type, std::in_place_type_t<T>,
      Args&&...>
  constexpr T& emplace(Args&&... args) {
    return value_.template emplace<T>(std::forward<Args>(args)...);
  }

  // Emplace a value of type `T` with an initializer list.
  template<typename T, typename U, typename... Args>
  requires std::is_constructible_v<underlying_type, std::in_place_type_t<T>,
      std::initializer_list<U>, Args&&...>
  constexpr T& emplace(std::initializer_list<U> il, Args&&... args) {
    return value_.template emplace<T>(il, std::forward<Args>(args)...);
  }

  // Emplace a value of type `T` by enum index.
  template<enum_type I, typename... Args>
  requires std::is_constructible_v<underlying_type,
      std::in_place_index_t<static_cast<std::size_t>(I)>, Args&&...>
  constexpr auto& emplace(Args&&... args) {
    return value_.template emplace<static_cast<std::size_t>(I)>(
        std::forward<Args>(args)...);
  }

  // Emplace a value of type `T` by enum index with an initializer list.
  template<enum_type I, typename U, typename... Args>
  requires std::is_constructible_v<underlying_type,
      std::in_place_index_t<static_cast<std::size_t>(I)>,
      std::initializer_list<U>, Args&&...>
  constexpr auto& emplace(std::initializer_list<U> il, Args&&... args) {
    return value_.template emplace<static_cast<std::size_t>(I)>(il,
        std::forward<Args>(args)...);
  }

  // Swap.
  constexpr void swap(enum_variant& other) noexcept(
      std::is_nothrow_swappable_v<underlying_type>) {
    using std::swap;
    swap(value_, other.value_);
  }

#pragma endregion
#pragma region Accessors

  // Accessors.

  // Index, using the enum type.
  [[nodiscard]] constexpr enum_type index() const noexcept {
    return static_cast<enum_type>(value_.index());
  }

  // Valueless by exception.
  [[nodiscard]] constexpr bool valueless_by_exception() const noexcept {
    return value_.valueless_by_exception();
  }

  // Get the underlying variant.
  [[nodiscard]] constexpr auto& get_underlying(this auto&& self) noexcept {
    return std::forward<decltype(self)>(self).value_;
  }

  // Check if the variant holds a value of type `T`.
  template<typename T>
  [[nodiscard]] constexpr bool holds_alternative() const noexcept {
    return std::holds_alternative<T>(value_);
  }

  // Get the value by enum index, or throw if it does not hold that type.
  template<enum_type I, typename Self>
  [[nodiscard]] constexpr decltype(auto) get(this Self&& self) {
    return std::get<static_cast<std::size_t>(I)>(
        std::forward<Self>(self).value_);
  }

  // Get the value by enum index, or null.
  template<enum_type I, typename Self>
  [[nodiscard]] constexpr auto* get_if(this Self&& self) noexcept {
    return std::get_if<static_cast<std::size_t>(I)>(
        &std::forward<Self>(self).value_);
  }

  // Get the value of type `T`, or throw if it does not hold that type.
  template<typename T, typename Self>
  [[nodiscard]] constexpr decltype(auto) get(this Self&& self) {
    return std::get<T>(std::forward<Self>(self).value_);
  }

  // Get pointer to value of type `T`, or null.
  template<typename T, typename Self>
  [[nodiscard]] constexpr auto* get_if(this Self&& self) noexcept {
    return std::get_if<T>(&std::forward<Self>(self).value_);
  }

#pragma endregion
#pragma region Visit

  // Visit the variant with a visitor. This is similar to `std::visit`, but
  // works correctly with any kind of visitor, including `indexed_callbacks`.
  template<typename Self, typename Visitor>
  constexpr decltype(auto) visit(this Self&& self, Visitor&& vis) {
    if constexpr (HasVisitMemberTemplate<Visitor, Self>) {
      return vis.visit(std::forward<Self>(self).value_);
    } else {
      return std::visit(std::forward<Visitor>(vis),
          std::forward<Self>(self).value_);
    }
  }
  template<typename R, typename Self, typename Visitor>
  constexpr R visit(this Self&& self, Visitor&& vis) {
    if constexpr (HasVisitMemberTemplate<Visitor, Self>) {
      return std::forward<Visitor>(vis).template visit<R>(
          std::forward<Self>(self).value_);
    } else {
      return std::visit<R>(std::forward<Visitor>(vis),
          std::forward<Self>(self).value_);
    }
  }

#pragma endregion
#pragma region comparison

  // Comparison operators.
  friend constexpr bool
  operator==(const enum_variant& lhs, const enum_variant& rhs) {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr bool
  operator!=(const enum_variant& lhs, const enum_variant& rhs) {
    return !(lhs == rhs);
  }
  friend constexpr bool
  operator<(const enum_variant& lhs, const enum_variant& rhs) {
    return lhs.value_ < rhs.value_;
  }
  friend constexpr bool
  operator<=(const enum_variant& lhs, const enum_variant& rhs) {
    return !(rhs < lhs);
  }
  friend constexpr bool
  operator>(const enum_variant& lhs, const enum_variant& rhs) {
    return rhs < lhs;
  }
  friend constexpr bool
  operator>=(const enum_variant& lhs, const enum_variant& rhs) {
    return !(lhs < rhs);
  }

#pragma endregion
private:
  underlying_type value_;

  // Used by consteval constructor.
  template<std::size_t I = 0>
  static consteval underlying_type construct(std::size_t idx) {
    if constexpr (I < sizeof...(Ts)) {
      if (idx == I) {
        return underlying_type(std::in_place_index<I>);
      } else {
        return construct<I + 1>(idx);
      }
    } else {
      return {}; // Unreachable, avoids warning
    }
  }
};
}}} // namespace corvid::container::rust_like

// Hash support.
namespace std {
template<corvid::meta::concepts::ScopedEnumType E, typename... Ts>
struct hash<corvid::container::rust_like::enum_variant<E, Ts...>> {
  using argument_type = corvid::container::rust_like::enum_variant<E, Ts...>;
  using result_type = std::size_t;

  [[nodiscard]] constexpr result_type operator()(
      const argument_type& v) const noexcept {
    return std::hash<typename argument_type::underlying_type>{}(
        v.get_underlying());
  }
};
} // namespace std
