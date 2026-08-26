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
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace corvid { inline namespace meta { inline namespace traits {

// Note: Some of these definitions are universal traits that apply anywhere,
// while others enforce distinctions that are specific to this library.

#pragma region Specialization

// Determine whether `T` is a specialization of `B`.
//
// Only works when `B` is a class that is specialized on types, not values
// (so `std::pair` is good, `std::array` is not).
template<typename T, template<typename...> typename B>
constexpr bool is_specialization_of_v = false;

template<template<typename...> typename B, typename... Args>
constexpr bool is_specialization_of_v<B<Args...>, B> = true;

#pragma endregion
#pragma region Detection
#pragma region Character, boolean

// Determine whether `T` is a `char`.
template<typename T>
constexpr bool is_char_v = std::is_same_v<std::remove_cvref_t<T>, char>;

// Determine whether `T` is a `char*` (including `char[]`).
template<typename T>
constexpr bool is_char_ptr_v =
    std::is_pointer_v<std::decay_t<T>> &&
    std::is_same_v<std::remove_cvref_t<std::remove_pointer_t<std::decay_t<T>>>,
        char>;

// Code-unit type that `T` views as: the `C` for which `T` is convertible to
// `std::basic_string_view<C>`, or `void` when `T` is not string-view-like.
// Covers `std::basic_string`, `std::basic_string_view`, character pointers and
// arrays, and any wrapper that converts to a `std::basic_string_view`.
template<typename T>
struct char_type_of {
private:
  using U = std::remove_cvref_t<T>;

  template<typename C>
  static constexpr bool conv =
      std::is_convertible_v<U, std::basic_string_view<C>>;

public:
  using type = std::conditional_t<conv<char>, char,
      std::conditional_t<conv<char8_t>, char8_t,
          std::conditional_t<conv<char16_t>, char16_t,
              std::conditional_t<conv<char32_t>, char32_t,
                  std::conditional_t<conv<wchar_t>, wchar_t, void>>>>>;
};

template<typename T>
using char_type_of_t = char_type_of<T>::type;

// Determine whether `T` is a `bool`.
template<typename T>
constexpr bool is_bool_v = std::is_same_v<std::remove_cvref_t<T>, bool>;

#pragma endregion
#pragma region Tuple, variant

// Determine whether `T` is a `std::variant`.
template<typename T>
constexpr bool is_variant_v = is_specialization_of_v<T, std::variant>;

// Determine whether `T` is a `std::tuple`.
template<typename T>
constexpr bool is_tuple_v = is_specialization_of_v<T, std::tuple>;

// Determine whether `T` is a `std::pair`.
template<typename T>
constexpr bool is_pair_v = is_specialization_of_v<T, std::pair>;

// Determine whether `T` behaves like a pair.
//
// This intentionally accepts either:
// - types that can be used to construct `std::pair<F, S>`, or
// - `std::tuple<F, S>` itself.
//
// The explicit tuple case keeps this trait stable across standard-library
// implementations: some do not model `std::tuple<F, S>` as implicitly
// convertible to, or even directly constructible as, `std::pair<F, S>`.
template<typename T>
constexpr bool is_pair_convertible_v = false;

template<template<typename...> typename C, typename F, typename S>
constexpr bool is_pair_convertible_v<C<F, S>> =
    std::is_constructible_v<std::pair<F, S>, C<F, S>> || is_tuple_v<C<F, S>>;

// Specialization to handle cv-qualified and reference types
template<typename T>
requires(!std::same_as<T, std::remove_cvref_t<T>>)
constexpr bool is_pair_convertible_v<T> =
    is_pair_convertible_v<std::remove_cvref_t<T>>;

#pragma endregion
#pragma region Callables

// Determine whether `T` is a `std::function`.
template<typename T>
constexpr bool is_std_function_v = is_specialization_of_v<T, std::function>;

// Determine whether `T` is a `std::move_only_function`. Always false when the
// standard library does not provide the type (libc++ has not yet shipped it).
#ifdef __cpp_lib_move_only_function
template<typename T>
constexpr bool is_std_move_only_function_v =
    is_specialization_of_v<T, std::move_only_function>;
#else
template<typename T>
constexpr bool is_std_move_only_function_v = false;
#endif

// Determine whether `T` is a std polymorphic function wrapper: either of the
// above.
template<typename T>
constexpr bool is_std_function_wrapper_v =
    is_std_function_v<T> || is_std_move_only_function_v<T>;

#pragma endregion
#pragma region Sequences

// Determine whether `T` is a `std::array`.
// Note: Can't use `is_specialization_of_v` because `std::array` specializes
// on a number.
namespace details {
template<typename... Ts>
constexpr bool is_std_array_impl_v = false;

template<typename T, size_t N>
constexpr bool is_std_array_impl_v<std::array<T, N>> = true;
} // namespace details

template<typename T>
constexpr bool is_std_array_v =
    details::is_std_array_impl_v<std::remove_cvref_t<T>>;

// Determine whether `T` is `std::span`.
// Note: We likewise can't use `is_specialization_of_v` because `std::span`
// specializes on a number.
namespace details {
template<typename... Ts>
constexpr bool is_span_impl_v = false;

template<typename T, size_t N>
constexpr bool is_span_impl_v<std::span<T, N>> = true;
} // namespace details

template<typename T>
constexpr bool is_span_v = details::is_span_impl_v<std::remove_cvref_t<T>>;

// Helper for span compatibility check.
template<typename T, typename V>
constexpr bool is_span_compatible_impl() noexcept {
  if constexpr (is_span_v<T>) {
    return std::same_as<std::remove_cv_t<V>,
               std::remove_cv_t<typename T::element_type>> &&
           (!std::is_same_v<V, std::remove_const_t<V>> ||
               !std::is_const_v<typename T::element_type>);
  } else {
    return false;
  }
}

// Determine whether span `T` is compatible with element type `V` in a
// const-safe way. When `V` is const, the span's element type can be const or
// non-const. When `V` isn't const, the span's element type must be non-const.
template<typename T, typename V>
constexpr bool is_span_compatible_v = is_span_compatible_impl<T, V>();

// Determine whether `T` is a `std::initializer_list`.
template<typename T>
constexpr bool is_initializer_list_v =
    is_specialization_of_v<T, std::initializer_list>;

#pragma endregion
#pragma endregion
#pragma region Signatures

// The const qualifier of a function signature, or of a type.
enum class const_qual : bool { none = false, present = true };

// The reference qualifier of a function signature: `none`, `lvalue` (`&`), or
// `rvalue` (`&&`).
enum class ref_qual : uint8_t { none, lvalue, rvalue };

// The noexcept specifier of a function signature. Note that `none` matches
// both `noexcept(false)` and no specifier at all.
enum class noexcept_spec : bool { none = false, present = true };

namespace details {

// Common base supplying the members for the `signature_traits`
// specializations.
template<typename R, const_qual Const, ref_qual Ref, noexcept_spec Noex,
    typename... Args>
struct signature_traits_base {
  using result_t = R;
  using args_t = std::tuple<Args...>;
  using function_t = R(Args...);
  static constexpr const_qual const_qualifier = Const;
  static constexpr ref_qual ref_qualifier = Ref;
  static constexpr noexcept_spec noexcept_specifier = Noex;
  static constexpr bool is_const = (Const == const_qual::present);
  static constexpr bool is_noexcept = (Noex == noexcept_spec::present);
};

} // namespace details

// `signature_traits` is the decomposition of a function signature into its
// result, parameters, and qualifiers.
//
// A signature here is what `std::move_only_function` accepts as `Sig`. This is
// a function type `R(Args...)`, optionally qualified by `const`, by a
// reference (`&` or `&&`), and by `noexcept`, for twelve variants in all.
//
// The trait provides `result_t`, `args_t` (the parameter types, as a
// `std::tuple`), `function_t` (the signature with every qualifier stripped,
// `noexcept` included), and one constant per axis: `const_qualifier`,
// `ref_qualifier`, and `noexcept_specifier`, each typed by its own enum. The
// two-valued axes are restated as the bools `is_const` and `is_noexcept`, for
// boolean expressions and `noexcept` clauses.
//
// Only the twelve variants are specialized, so the trait doubles as the gate
// for what counts as a signature: anything else, `volatile` qualification and
// C-style variadics included, leaves the primary undefined.
template<typename Sig>
struct signature_traits;

template<typename R, typename... Args>
struct signature_traits<R(Args...)>
    : details::signature_traits_base<R, const_qual::none, ref_qual::none,
          noexcept_spec::none, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) noexcept>
    : details::signature_traits_base<R, const_qual::none, ref_qual::none,
          noexcept_spec::present, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) const>
    : details::signature_traits_base<R, const_qual::present, ref_qual::none,
          noexcept_spec::none, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) const noexcept>
    : details::signature_traits_base<R, const_qual::present, ref_qual::none,
          noexcept_spec::present, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) &>
    : details::signature_traits_base<R, const_qual::none, ref_qual::lvalue,
          noexcept_spec::none, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) & noexcept>
    : details::signature_traits_base<R, const_qual::none, ref_qual::lvalue,
          noexcept_spec::present, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) const&>
    : details::signature_traits_base<R, const_qual::present, ref_qual::lvalue,
          noexcept_spec::none, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) const & noexcept>
    : details::signature_traits_base<R, const_qual::present, ref_qual::lvalue,
          noexcept_spec::present, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) &&>
    : details::signature_traits_base<R, const_qual::none, ref_qual::rvalue,
          noexcept_spec::none, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) && noexcept>
    : details::signature_traits_base<R, const_qual::none, ref_qual::rvalue,
          noexcept_spec::present, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) const&&>
    : details::signature_traits_base<R, const_qual::present, ref_qual::rvalue,
          noexcept_spec::none, Args...> {};

template<typename R, typename... Args>
struct signature_traits<R(Args...) const && noexcept>
    : details::signature_traits_base<R, const_qual::present, ref_qual::rvalue,
          noexcept_spec::present, Args...> {};

// The `function_t` of `Sig`, which is the signature with every qualifier
// stripped.
//
// An alias template, so no dependent `typename` is needed at the use site.
template<typename Sig>
using signature_function_t = signature_traits<Sig>::function_t;

#pragma endregion
#pragma region pointers

inline namespace pointers {

// Get underlying element type of a raw or smart pointer.
//
// When not a pointer, returns void.
namespace details {
template<typename T>
auto pointer_element(int)
    -> std::remove_reference_t<decltype(*std::declval<T>())>;

template<typename>
auto pointer_element(...) -> void;
} // namespace details

template<typename T>
using pointer_element_t = decltype(details::pointer_element<T>(0));

} // namespace pointers

#pragma endregion
#pragma region keyfinding

inline namespace keyfinding {

// Determine whether `T` has a `find` method which takes a `T::key_type`.
namespace details {
template<typename T, typename = void>
struct has_key_find_method: std::false_type {};

template<typename T>
struct has_key_find_method<T,
    std::void_t<decltype(std::declval<T&>().find(
        std::declval<typename T::key_type>()))>>: std::true_type {};
} // namespace details

template<typename T>
constexpr bool has_key_find_v =
    details::has_key_find_method<std::remove_cvref_t<T>>::value;

} // namespace keyfinding

#pragma endregion
#pragma region Tuple metafunctions

// True if `T` appears at least once in `Tuple`.
template<typename T, typename Tuple>
struct tuple_contains;
template<typename T, typename... Ts>
struct tuple_contains<T, std::tuple<Ts...>>
    : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};
template<typename T, typename Tuple>
inline constexpr bool tuple_contains_v = tuple_contains<T, Tuple>::value;

// Helper: append `T` to `Tuple` only if `T` is not already present.
template<typename T, typename Tuple>
struct tuple_append_unique;
template<typename T, typename... Ts>
struct tuple_append_unique<T, std::tuple<Ts...>> {
  using type = std::conditional_t<(std::is_same_v<T, Ts> || ...),
      std::tuple<Ts...>, std::tuple<Ts..., T>>;
};

// Recursively accumulate types from `SrcTuples` into `AccTuple`, skipping
// duplicates. Handles both type-list and empty-tuple cases.
template<typename AccTuple, typename... SrcTuples>
struct tuple_union_impl {
  using type = AccTuple;
};
template<typename AccTuple, typename Head, typename... Tail, typename... Rest>
struct tuple_union_impl<AccTuple, std::tuple<Head, Tail...>, Rest...> {
  using type =
      tuple_union_impl<typename tuple_append_unique<Head, AccTuple>::type,
          std::tuple<Tail...>, Rest...>::type;
};
template<typename AccTuple, typename... Rest>
struct tuple_union_impl<AccTuple, std::tuple<>, Rest...> {
  using type = tuple_union_impl<AccTuple, Rest...>::type;
};

// Deduplicated union of types across all `Tuples`. For example,
// `tuple_union_t<tuple<A,B,C>, tuple<A,D,E>>` yields `tuple<A,B,C,D,E>`.
template<typename... Tuples>
using tuple_union_t = tuple_union_impl<std::tuple<>, Tuples...>::type;

// 0-based index of `T` in `Tuple`. Fails to compile if `T` is not present.
template<typename T, typename Tuple, size_t I = 0UZ>
struct tuple_index_impl;
template<typename T, size_t I>
struct tuple_index_impl<T, std::tuple<>, I> {
  static_assert(false, "type not found in tuple");
};
template<typename T, typename Head, typename... Tail, size_t I>
struct tuple_index_impl<T, std::tuple<Head, Tail...>, I>
    : std::conditional_t<std::is_same_v<T, Head>,
          std::integral_constant<size_t, I>,
          tuple_index_impl<T, std::tuple<Tail...>, I + 1>> {};

template<typename T, typename Tuple>
inline constexpr size_t tuple_index_v = tuple_index_impl<T, Tuple>::value;

// Transform `std::tuple<C0, C1, ...>` to
// `std::tuple<std::optional<C0>, std::optional<C1>, ...>`.
template<typename Tuple>
struct wrap_optionals;
template<typename... Cs>
struct wrap_optionals<std::tuple<Cs...>> {
  using type = std::tuple<std::optional<Cs>...>;
};
template<typename Tuple>
using wrap_optionals_t = wrap_optionals<Tuple>::type;

#pragma endregion
}}} // namespace corvid::meta::traits
