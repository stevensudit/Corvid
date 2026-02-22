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
#include "./meta_shared.h"
#include "./traits.h"

namespace corvid { inline namespace meta { inline namespace concepts {

// Note: Some of these definitions are universal concepts that apply anywhere,
// while others enforce distinctions that are specific to this library.

// `T` must be the same as `U`, ignoring cvref.
template<typename T, typename U>
concept SameAs = std::same_as<T, std::remove_cvref_t<U>>;

// `T` must be a type derived from `std::ostream`.
template<typename T>
concept OStreamDerived = std::derived_from<T, std::ostream>;

// `T` must be a type that can be inserted into a `std::ostream`.
template<typename T>
concept OStreamable = requires(T t, std::ostream& os) { os << t; };

// `T` must be a `std::string`.
template<typename T>
concept StdString = SameAs<std::string, T>;

// `T` must be a target to append to.
template<typename T>
concept AppendTarget = OStreamDerived<T> || StdString<T>;

// `T` must be an enum, which could be scoped or not.
template<typename T>
concept StdEnum = std::is_enum_v<std::remove_cvref_t<T>>;

// `T` must be a scoped enum.
template<typename T>
concept ScopedEnum = std::is_scoped_enum_v<std::remove_cvref_t<T>>;

// `T` must be bool.
template<typename T>
concept Bool = is_bool_v<T>;

// `T` must be integral, excluding bool.
template<typename T>
concept Integer = std::integral<T> && (!Bool<T>);

// `T` must be an enum or integral type.
template<typename T>
concept IntegerOrEnum = Integer<T> || StdEnum<T>;

// `T` must be nullptr_t.
template<typename T>
concept NullPtr = SameAs<T, std::nullptr_t>;

// `T` must be a char.
template<typename T>
concept Char = SameAs<char, T>;

// `T` must be a char*.
template<typename T>
concept CharPtr = SameAs<char*, std::remove_cvref_t<T>>;

// `T` must be a char[].
template<typename T>
concept CharArray =
    std::is_array_v<std::remove_cvref_t<T>> &&
    SameAs<char, std::remove_extent_t<T>>;

// `T` must be implicitly convertible to `std::string_view`.
template<typename T>
concept StringViewConvertible =
    std::is_convertible_v<T, std::string_view> && (!NullPtr<T>);

// `T` must be a void pointer.
template<typename T>
concept VoidPointer = std::is_void_v<std::remove_pointer_t<T>>;

// `T` must be dereferenceable, like a pointer or iterator.
template<typename T>
concept Dereferenceable =
    (requires(T t) { *t; } || requires(T t) { t.operator*(); });

// `T` must be a bool-like type, which means it can be used in a predicate.
template<typename T>
concept BoolLike = requires(T t) { t ? 1 : 2; };

// `T` must be a pointer-like type, which means it can be dereferenced and
// used in a predicate.
template<typename T>
concept PointerLike = Dereferenceable<T> && BoolLike<T>;

// `T` must be a raw pointer.
template<typename T>
concept RawPointer = std::is_pointer_v<std::remove_cvref_t<T>>;

// `T` must be like a pointer but not be a raw one.
template<typename T>
concept SmartPointer = PointerLike<T> && (!RawPointer<T>);

// `T` must be a range.
template<typename T>
concept Range = std::ranges::range<T>;

// `T` must be `std::optional` or act like it.
template<typename T>
concept OptionalLike =
    Dereferenceable<T> && (!ScopedEnum<T>) && (!StringViewConvertible<T>) &&
    (!Range<T>);

// `T` must be a `std::pair`.
template<typename T>
concept StdPair = is_specialization_of_v<std::remove_cvref_t<T>, std::pair>;

// `T` must be `std::pair` or convertible to it.
template<typename T>
concept PairConvertible = is_pair_convertible_v<T>;

// `T` must be a `std::tuple` or convertible to it.
template<typename T>
concept StdTuple = is_tuple_v<T>;

// `T` must be a `std::tuple` or convertible to a pair.
template<typename T>
concept TupleLike = StdTuple<T> || PairConvertible<T>;

// `T` must be a `std::array`.
template<typename T>
concept StdArray = is_std_array_v<std::remove_cvref_t<T>>;

// `T` must be a `std::span` compatible with `V` in a const-safe way. When `V`
// is non-const, the span's element type can be either const or non-const. When
// `V` is const, the span's element type must also be const.
template<typename T, typename V>
concept Span = is_span_compatible_v<T, V>;

// `T` must be a `std::span` of `char` or `const char`.
template<typename T>
concept ConstCharSpan = Span<T, const char>;

// `T` must be convertible to a `std::span` of some sort.
template<typename T>
concept SpanConvertible = requires(T t) { std::span{t}; };

// `T` must be a `std::span` of `std::string_view` or `const std::string_view`.
template<typename T>
concept ConstStringViewSpan = Span<T, const std::string_view>;

// `T` must be a `std::initializer_list`.
template<typename T>
concept InitializerList = is_initializer_list_v<T>;

// `T` must be a `std::span` of something StringViewConvertible.
template<typename T>
concept StringViewConvertibleSpan =
    is_span_v<T> && StringViewConvertible<typename T::element_type>;

// `T` must be a container, which excludes strings and pairs.
template<typename T>
concept Container =
    Range<T> && (!StringViewConvertible<T>) && (!PairConvertible<T>);

// `T` must be a `std::variant`.
template<typename T>
concept Variant = is_variant_v<T>;

// `T` must be a `std::monostate`.
template<typename T>
concept MonoState = SameAs<std::monostate, T>;

// `T` must have a `find` method that takes a `T::key_type`.
template<typename T>
concept KeyFindable = has_key_find_v<T>;

// `T` must be a container that lacks a `find` method.
template<typename T>
concept RangeWithoutFind = Range<T> && (!KeyFindable<T>);

// `U` must be usable for constructing a `T`.
template<typename T, typename U>
concept Makeable = std::constructible_from<T, U>;

// `U` must be comparable with a `T`.
template<typename T, typename U>
concept Comparable = std::totally_ordered_with<T, U>;

// `U` must act as a view for `T`.
template<typename T, typename U>
concept Viewable =
    Makeable<std::remove_cvref_t<T>, std::remove_cvref_t<U>> &&
    Comparable<std::remove_cvref_t<T>, std::remove_cvref_t<U>>;

// `F` must be callable with `Args` and return void.
template<typename F, typename... Args>
concept CallableReturningVoid = requires(F f, Args&&... args) {
  { f(std::forward<Args>(args)...) } -> std::same_as<void>;
};

// `F` must be callable with `Args` and return something other than void.
template<typename F, typename... Args>
concept CallableReturningNonVoid = requires(F f, Args&&... args) {
  {
    f(std::forward<Args>(args)...)
  } -> std::same_as<std::invoke_result_t<F, Args...>>;
} && (!std::is_void_v<std::invoke_result_t<F, Args...>>);

}}} // namespace corvid::meta::concepts
