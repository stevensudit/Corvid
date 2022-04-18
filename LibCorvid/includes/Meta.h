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
#include <type_traits>
#include <string>
#include <tuple>
#include <array>
#include <utility>

namespace corvid {
inline namespace specialized {

//
// Specialization
//

// Determine whether T is a specialization of C.
//
// Only works when C is specialized on types, not values (so `std::pair` is
// good, `std::array` is not).
template<typename T, template<typename...> typename C>
constexpr bool is_specialization_of_v = false;

template<template<typename...> typename C, typename... Args>
constexpr bool is_specialization_of_v<C<Args...>, C> = true;

// Get underlying element type of a raw or smart pointer.
//
// When not a pointer, returns void.
namespace details {
template<typename P>
auto pointer_element(int)
    -> std::remove_reference_t<decltype(*std::declval<P>())>;

template<typename P>
auto pointer_element(...) -> void;
} // namespace details

template<typename P>
using pointer_element_t = decltype(details::pointer_element<P>(0));

} // namespace specialized
inline namespace dereferencing {

//
// Dereferenceable
//

// Determine whether P can be dereferenced like a pointer, even if it's not a
// raw pointer. This detects iterators, smart pointers, and even
// `std::optional`.
//
// Note that this technique is subtly different, and better, than using
// `std::is_member_function_pointer` for `operator*`.
template<typename P>
constexpr bool is_dereferenceable_v = !std::is_void_v<pointer_element_t<P>>;

} // namespace dereferencing
inline namespace finding {

//
// Find
//

// Determine whether C has a find method taking K.
namespace details {
template<typename C, typename K>
using find_ret_t = decltype(std::declval<C>().find(std::declval<K>()));

template<typename C, typename K>
auto has_find(int)
    -> decltype(std::declval<find_ret_t<C, K>>(), std::true_type{});

template<typename C, typename K>
auto has_find(...) -> std::false_type;
} // namespace details

template<typename C, typename K>
constexpr bool has_find_v = decltype(details::has_find<C, K>(0))::value;

} // namespace finding
inline namespace ranging {

//
// Ranged-for
//

// Determine whether C can be ranged-for over.
namespace details {
using namespace std;

template<typename C>
using find_container_it_t = decltype(cbegin(std::declval<C>()));

template<typename C>
auto can_ranged_for(int)
    -> decltype(std::declval<find_container_it_t<C>>(), std::true_type{});

template<typename C>
auto can_ranged_for(...) -> std::false_type;
} // namespace details

template<typename C>
constexpr bool can_ranged_for_v =
    decltype(details::can_ranged_for<C>(0))::value;

} // namespace ranging
inline namespace detection {

//
// Detection
//

// Determine whether T is a std::pair.
template<typename T>
constexpr bool is_pair_v = is_specialization_of_v<T, std::pair>;

// Determine whether T is a std::array.
template<typename... Ts>
constexpr bool is_array_v = false;

template<typename T, std::size_t N>
constexpr bool is_array_v<std::array<T, N>> = true;

// Extract value from container element.
constexpr [[nodiscard]] auto& container_element_v(auto&& it) {
  if constexpr (is_pair_v<std::decay_t<decltype(*it)>>)
    return it->second;
  else
    return *it;
}

// Determine whether T is convertible to `std::string_view`, which includes
// `std::string_view`, `std::string`, and `char*` (but not char).
template<typename T>
constexpr bool is_string_view_convertible_v =
    std::is_convertible_v<T, std::string_view>;

// Determine whether C is a container, including arrays but excluding char[],
// string and string_view.
template<typename C>
constexpr bool is_container_v =
    can_ranged_for_v<C> && !is_string_view_convertible_v<C>;

// Determine whether T is bool.
template<typename T>
constexpr bool is_bool_v = std::is_same_v<std::decay_t<T>, bool>;

// Determine whether T is a number (excluding `bool` and `enum`).
template<typename T>
constexpr bool is_number_v =
    std::is_arithmetic_v<std::decay_t<T>> && !is_bool_v<T>;

// Determine whether T is an integral number (excluding `bool` and `enum`).
template<typename T>
constexpr bool is_integral_number_v =
    std::is_arithmetic_v<std::decay_t<T>> && !is_bool_v<T> &&
    std::is_integral_v<T>;

// Determine whether T is a floating-point number.
template<typename T>
constexpr bool is_floating_number_v =
    std::is_arithmetic_v<std::decay_t<T>> && !std::is_integral_v<T>;

// Determine whether T is an `enum`. Works for unscoped and scoped (class)
// `enum`.
template<typename T>
constexpr bool is_enum_v = std::is_enum_v<std::decay_t<T>>;

// Determine whether T is a tuple.
template<typename T>
constexpr bool is_tuple_v = is_specialization_of_v<T, std::tuple>;

// Determine whether T is like a tuple, which is to say that you can std::apply
// to it.
//
// NOTE: In principle, we could sniff out std::tuple_size<T> so that we detect
// any user-defined specializations (which are explicitly permitted).
// Unfortunately, while this works under clang, it fails under gcc and MSVC due
// to their implementation errors. Perhaps a future improvement would be to
// #ifdef between the two solutions, depending on the compiler and version. For
// now, we can specialize this for any other tuple-like objects.
template<typename T>
constexpr bool is_tuple_like_v =
    is_tuple_v<T> || is_pair_v<T> || is_array_v<T>;

// Determine whether T is equivalent to a tuple, which is to say that it
// contains potentially heterogenous types and cannot be iterated through in a
// simple loop.
//
// This include std::tuple and std::pair but excludes the otherwise tuple-like
// std::array, because it's homogenous in type and can be iterated through with
// ranged for.
template<typename T>
constexpr bool is_tuple_equiv_v = is_tuple_v<T> || is_pair_v<T>;

} // namespace detection
inline namespace naming {

//
// Typename
//

// Extract fully-qualified type name.
//
// This is a crude solution, but sufficient for debugging.
template<typename T>
std::string type_name() {
  using TR = typename std::remove_reference<T>::type;
  std::unique_ptr<char, void (*)(void*)> own(
#ifndef _MSC_VER
      abi::__cxa_demangle(typeid(TR).name(), nullptr, nullptr, nullptr),
#else
      nullptr,
#endif
      std::free);
  std::string r = own ? own.get() : typeid(TR).name();
  if (std::is_const_v<TR>) r += " const";
  if (std::is_volatile_v<TR>) r += " volatile";
  if (std::is_lvalue_reference_v<T>)
    r += "&";
  else if (std::is_rvalue_reference_v<T>)
    r += "&&";
  return r;
}

// Extract fully-qualified type name, deducing it from the parameter.
template<typename T>
std::string type_name(T&&) {
  return type_name<T>();
}

} // namespace naming
} // namespace corvid

// TODO: Consider writing `enable_if_bool<P>` to encapsulate
// `std::enable_if_t<P, bool>`.
