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
#include <array>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

// For `type_name`.
#ifndef _MSC_VER
#include <cxxabi.h>
#endif

namespace corvid {
inline namespace specialized {

//
// Specialization
//

// Determine whether `T` is a specialization of `B`.
//
// Only works when `B` is a class that is specialized on types, not values (so
// `std::pair` is good, `std::array` is not).
template<typename T, template<typename...> typename B>
constexpr bool is_specialization_of_v = false;

template<template<typename...> typename B, typename... Args>
constexpr bool is_specialization_of_v<B<Args...>, B> = true;

} // namespace specialized
inline namespace dereferencing {

//
// Dereference
//

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

// Determine whether `T` can be dereferenced like a pointer, even if it's not a
// raw pointer. This detects iterators, smart pointers, and even
// `std::optional`.
template<typename T>
constexpr bool is_dereferenceable_v = !std::is_void_v<pointer_element_t<T>>;

} // namespace dereferencing
inline namespace finding {

//
// Find
//

// Determine whether `T` has a `find` method taking `K`.
namespace details {
template<typename T, typename K>
using find_ret_t = decltype(std::declval<T>().find(std::declval<K>()));

template<typename T, typename K>
auto has_find(int)
    -> decltype(std::declval<find_ret_t<T, K>>(), std::true_type{});

template<typename, typename>
auto has_find(...) -> std::false_type;
} // namespace details

template<typename C, typename K>
constexpr bool has_find_v = decltype(details::has_find<C, K>(0))::value;

} // namespace finding
inline namespace ranging {

//
// Ranged-for
//

// Determine whether `T` can be ranged-for over.
namespace details {
using namespace std;

template<typename T>
using find_container_it_t = decltype(cbegin(std::declval<T>()));

template<typename T>
auto can_ranged_for(int)
    -> decltype(std::declval<find_container_it_t<T>>(), std::true_type{});

template<typename>
auto can_ranged_for(...) -> std::false_type;
} // namespace details

template<typename T>
constexpr bool can_ranged_for_v =
    decltype(details::can_ranged_for<T>(0))::value;

} // namespace ranging
inline namespace streamable {

//
// Streamable
//

// Determine whether `T` can be streamed out.
//
// Note: This exhibits false negatives, such as with the external overloads for
// BitmaskEnum.
namespace details {
template<class T>
auto can_stream_out(int)
    -> decltype(std::declval<std::ostream>() << std::declval<T>());

template<typename>
static auto can_stream_out(...) -> void;
} // namespace details

template<typename T>
constexpr bool can_stream_out_v =
    !std::is_void_v<decltype(details::can_stream_out<T>(0))>;

} // namespace streamable
inline namespace detection {

//
// Detection
//

// Wrapper for `std::enable_if`, allowing this abbreviated usage:
//    enable_if_0<is_thingy_v<T>> = 0
template<bool B>
using enable_if_0 = std::enable_if_t<B, bool>;

// Determine whether `T` is a `std::array`.
template<typename... Ts>
constexpr bool is_array_v = false;

template<typename T, std::size_t N>
constexpr bool is_array_v<std::array<T, N>> = true;

// Determine whether `T` is a `std::pair`.
template<typename T>
constexpr bool is_pair_v =
    is_specialization_of_v<std::remove_cvref_t<T>, std::pair>;

// Determine whether `T` is equivalent to a `std::pair`.
//
// Unlike using `is_specialization_of`, this also detects anything that
// converts to a `std::pair`, including a child.
namespace details {
template<typename... Ts>
constexpr bool is_pair_like_impl = false;

template<template<typename...> typename C, typename F, typename S>
constexpr bool is_pair_like_impl<C<F, S>> =
    std::is_convertible_v<C<F, S>, std::pair<F, S>>;
} // namespace details

template<typename T>
constexpr bool is_pair_like_v =
    details::is_pair_like_impl<std::remove_cvref_t<T>>;

// Extract value from container element, including the key if `keyed`.
template<bool keyed = false, typename T>
constexpr [[nodiscard]] auto& container_element_v(T&& it) {
  if constexpr (is_pair_v<decltype(*it)> && !keyed)
    return it->second;
  else
    return *it;
}

// Determine whether `T` is convertible to `std::string_view`, which includes
// `std::string_view`, `std::string`, and `char*` (but not char).
template<typename T>
constexpr bool is_string_view_convertible_v =
    std::is_convertible_v<T, std::string_view> &&
    !std::is_same_v<nullptr_t, std::remove_cvref_t<T>>;

// Determine whether `T` is a container, including arrays but excluding
// `char[]`, `std::string` and `std::string_view`, as well as excluding any
// pair.
template<typename T>
constexpr bool is_container_v =
    can_ranged_for_v<T> && !is_string_view_convertible_v<T> &&
    !is_pair_like_v<T>;

// Determine whether `T` is bool.
template<typename T>
constexpr bool is_bool_v = std::is_same_v<bool, std::remove_cvref_t<T>>;

// Determine whether `T` is a number (excluding `bool` and `enum`).
template<typename T>
constexpr bool is_number_v =
    std::is_arithmetic_v<std::remove_cvref_t<T>> && !is_bool_v<T>;

// Determine whether `T` is an integral number (excluding `bool` and `enum`).
template<typename T>
constexpr bool is_integral_number_v =
    std::is_arithmetic_v<std::remove_cvref_t<T>> && !is_bool_v<T> &&
    std::is_integral_v<T>;

// Determine whether `T` is a floating-point number.
template<typename T>
constexpr bool is_floating_number_v =
    std::is_arithmetic_v<std::remove_cvref_t<T>> && !std::is_integral_v<T>;

// Determine whether `T` is an enum. Works for unscoped and scoped (class)
// enum.
template<typename T>
constexpr bool is_enum_v = std::is_enum_v<std::remove_cvref_t<T>>;

// Determine whether `T` is a `std::tuple`.
template<typename T>
constexpr bool is_tuple_v = is_specialization_of_v<T, std::tuple>;

// Determine whether `T` is a `std::initializer_list`.
template<typename T>
constexpr bool is_initializer_list_v =
    is_specialization_of_v<T, std::initializer_list>;

// Determine whether `T` is a `std::optional`, or at least optional-like.
template<typename T>
constexpr bool is_optional_like_v =
    is_dereferenceable_v<T> && !is_enum_v<T> &&
    !is_string_view_convertible_v<T> && !is_container_v<T>;

// Determine whether `T` is a `std::variant`.
template<typename T>
constexpr bool is_variant_v = is_specialization_of_v<T, std::variant>;

// Determine whether `T` is `void*`.
template<typename T>
constexpr bool is_void_ptr_v =
    std::is_pointer_v<T> &&
    (std::is_void_v<std::remove_pointer_t<std::remove_cvref_t<T>>>);

// Determine whether `T` is a `char*` (including `char[]`).
template<typename T>
constexpr bool is_char_ptr_v =
    std::is_pointer_v<std::decay_t<T>> &&
    (std::is_same_v<
        std::remove_cvref_t<std::remove_pointer_t<std::decay_t<T>>>, char>);

// Determine whether `T` is like a `std::tuple`, which is to say that you can
// `std::apply` to it. This includes `std::tuple`, `std::pair` and
// `std::array`.
//
// NOTE: In principle, we could sniff out `std::tuple_size<T>` so that we
// detect any user-defined specializations (which are explicitly permitted).
// Unfortunately, while this works under clang, it currently fails under gcc
// and MSVC due to their implementation errors. Perhaps a future improvement
// would be to #ifdef between the two solutions, depending on the compiler and
// version. For now, we can specialize this for any other tuple-like objects.
template<typename T>
constexpr bool is_tuple_like_v =
    is_tuple_v<T> || is_pair_like_v<T> || is_array_v<T>;

// Determine whether `T` is equivalent to a `std::tuple`, which is to say that
// it contains potentially heterogenous types and cannot be iterated through in
// a runtime loop.
//
// This includes `std::tuple` and `std::pair` but excludes the otherwise
// tuple-like `std::array`, because it's always homogenous in type and can be
// iterated through with ranged-for.
template<typename T>
constexpr bool is_tuple_equiv_v = is_tuple_v<T> || is_pair_like_v<T>;

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
inline namespace underlying {

// Cast enum to underlying integer value.
//
// Similar to `std::to_underlying_type` in C++23, but more forgiving. If `T` is
// not an enum, just passes the value through unchanged.
template<typename T>
constexpr auto as_underlying(T v) noexcept {
  if constexpr (is_enum_v<T>) {
    return static_cast<std::underlying_type_t<T>>(v);
  } else {
    return v;
  }
}

// Determine underlying type of enum. If not enum, harmlessly returns `T`.
template<typename T>
using as_underlying_t = decltype(as_underlying(std::declval<T>()));

// Cast underlying value to enum.
//
// Similar to `static_cast<T>(U)` except that, when `T` isn't an enum, instead
// returns a default-constructed `X`.
//
// If this seems like a strange thing to want to do, you're not wrong, but it
// turns out to be surprisingly useful.
template<typename T, typename X = std::byte, typename V>
constexpr auto from_underlying(const V& u) {
  if constexpr (is_enum_v<T>) {
    return static_cast<T>(u);
  } else {
    return X{};
  }
}

} // namespace underlying
} // namespace corvid

//
// TODO
//

// TODO: Figure out how to fix the false negatives in `can_stream_out_v`.

// TODO: Consider reorganizing the `detection` namespace by breaking it down
// further.
