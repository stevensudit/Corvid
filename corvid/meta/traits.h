// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
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
#include "./meta_shared.h"

namespace corvid { inline namespace meta { inline namespace traits {

// Note: Some of these definitions are universal traits that apply anywhere,
// while others enforce distinctions that are specific to this library.

//
// Specialization
//

// Determine whether `T` is a specialization of `B`.
//
// Only works when `B` is a class that is specialized on types, not values
// (so `std::pair` is good, `std::array` is not).
template<typename T, template<typename...> typename B>
constexpr bool is_specialization_of_v = false;

template<template<typename...> typename B, typename... Args>
constexpr bool is_specialization_of_v<B<Args...>, B> = true;

// Detect.

// Determine whether `T` is a `char`.
template<typename T>
constexpr bool is_char_v = std::is_same_v<std::remove_cvref_t<T>, char>;

// Determine whether `T` is a `char*` (including `char[]`).
template<typename T>
constexpr bool is_char_ptr_v =
    std::is_pointer_v<std::decay_t<T>> &&
    (std::is_same_v<
        std::remove_cvref_t<std::remove_pointer_t<std::decay_t<T>>>, char>);

// Determine whether `T` is a `bool`.
template<typename T>
constexpr bool is_bool_v = std::is_same_v<std::remove_cvref_t<T>, bool>;

// Determine whether `T` is a `std::variant`.
template<typename T>
constexpr bool is_variant_v = is_specialization_of_v<T, std::variant>;

// Determine whether `T` is a `std::tuple`.
template<typename T>
constexpr bool is_tuple_v = is_specialization_of_v<T, std::tuple>;

// Determine whether `T` is a `std::pair`.
template<typename T>
constexpr bool is_pair_v = is_specialization_of_v<T, std::pair>;

// Determine whether `T` is convertible to `std::pair`.
template<typename T>
constexpr bool is_pair_convertible_v = false;

template<template<typename...> typename C, typename F, typename S>
constexpr bool is_pair_convertible_v<C<F, S>> =
    std::is_convertible_v<C<F, S>, std::pair<F, S>>;

// Determine whether `T` is a `std::array`.
// Note: Can't use `is_specialization_of_v` because `std::array` specializes
// on a number.
template<typename... Ts>
constexpr bool is_std_array_v = false;

template<typename T, std::size_t N>
constexpr bool is_std_array_v<std::array<T, N>> = true;

// Determine whether `T` is `std::span`.
// Note: We likewise can't use `is_specialization_of_v` because `std::span`
// specializes on a number.
template<typename... Ts>
constexpr bool is_span_v = false;

template<typename T, std::size_t N>
constexpr bool is_span_v<std::span<T, N>> = true;

// Determine whether `T` is a `std::initializer_list`.
template<typename T>
constexpr bool is_initializer_list_v =
    is_specialization_of_v<T, std::initializer_list>;

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
}}} // namespace corvid::meta::traits
