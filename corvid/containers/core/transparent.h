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
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../../meta/concepts.h"

namespace corvid { inline namespace containers {

// Transparent comparators allow comparing any stringlike values without
// constructing temporary `std::string` instances from them. This works for
// `find` but not `operator[]`.
//
// For ordered collections, we need to define a `less`. For unordered, we need
// `hash` and `equal`.
//
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r3.html

#pragma region transparent less

// Transparent `less` over stringlike values of `CharT`.
template<CharType CharT>
struct basic_transparent_less {
  using is_transparent = void;
  using view_t = std::basic_string_view<CharT>;

  template<typename T, typename V>
  constexpr bool operator()(const T& l, const V& r) const {
    return view_t{l} < view_t{r};
  }
};

#pragma endregion
#pragma region transparent hash

// Transparent `hash` and `equal_to` over stringlike values of `CharT`.
template<CharType CharT>
struct basic_transparent_hash_equal {
  using is_transparent = void;
  using view_t = std::basic_string_view<CharT>;

  template<typename T>
  constexpr size_t operator()(const T& t) const {
    return std::hash<view_t>{}(view_t{t});
  }

  template<typename T, typename V>
  constexpr bool operator()(const T& l, const V& r) const {
    return static_cast<view_t>(l) == static_cast<view_t>(r);
  }
};

#pragma endregion
#pragma region Aliases

// Map keyed by `std::basic_string<CharT>`, with transparent search. Value
// type, `V`, defaults to that of the key.
template<CharType CharT, typename V = std::basic_string<CharT>,
    typename A = std::allocator<std::pair<const std::basic_string<CharT>, V>>>
using basic_string_map =
    std::map<std::basic_string<CharT>, V, basic_transparent_less<CharT>, A>;

// Set of `std::basic_string<CharT>`, with transparent search.
template<CharType CharT, typename A = std::allocator<std::basic_string<CharT>>>
using basic_string_set =
    std::set<std::basic_string<CharT>, basic_transparent_less<CharT>, A>;

// Unordered map keyed by `std::basic_string<CharT>`, with transparent search.
// Value type, `V`, defaults to that of the key.
template<CharType CharT, typename V = std::basic_string<CharT>,
    typename A = std::allocator<std::pair<const std::basic_string<CharT>, V>>>
using basic_string_unordered_map = std::unordered_map<std::basic_string<CharT>,
    V, basic_transparent_hash_equal<CharT>,
    basic_transparent_hash_equal<CharT>, A>;

// Unordered set of `std::basic_string<CharT>`, with transparent search.
template<CharType CharT, typename A = std::allocator<std::basic_string<CharT>>>
using basic_string_unordered_set = std::unordered_set<std::basic_string<CharT>,
    basic_transparent_hash_equal<CharT>, basic_transparent_hash_equal<CharT>,
    A>;

// Specialized aliases over `char`.
template<typename V = std::string,
    typename A = std::allocator<std::pair<const std::string, V>>>
using string_map = basic_string_map<char, V, A>;

template<typename A = std::allocator<std::string>>
using string_set_with_alloc = basic_string_set<char, A>;

using string_set = string_set_with_alloc<>;

template<typename V = std::string,
    typename A = std::allocator<std::pair<const std::string, V>>>
using string_unordered_map = basic_string_unordered_map<char, V, A>;

template<typename A = std::allocator<std::string>>
using string_unordered_set_with_alloc = basic_string_unordered_set<char, A>;

using string_unordered_set = string_unordered_set_with_alloc<>;

// Specialized aliases over `wchar_t`.
template<typename V = std::wstring,
    typename A = std::allocator<std::pair<const std::wstring, V>>>
using wstring_map = basic_string_map<wchar_t, V, A>;

template<typename A = std::allocator<std::wstring>>
using wstring_set_with_alloc = basic_string_set<wchar_t, A>;

using wstring_set = wstring_set_with_alloc<>;

template<typename V = std::wstring,
    typename A = std::allocator<std::pair<const std::wstring, V>>>
using wstring_unordered_map = basic_string_unordered_map<wchar_t, V, A>;

template<typename A = std::allocator<std::wstring>>
using wstring_unordered_set_with_alloc =
    basic_string_unordered_set<wchar_t, A>;

using wstring_unordered_set = wstring_unordered_set_with_alloc<>;

// Specialized aliases over `char8_t`.
template<typename V = std::u8string,
    typename A = std::allocator<std::pair<const std::u8string, V>>>
using u8string_map = basic_string_map<char8_t, V, A>;

template<typename A = std::allocator<std::u8string>>
using u8string_set_with_alloc = basic_string_set<char8_t, A>;

using u8string_set = u8string_set_with_alloc<>;

template<typename V = std::u8string,
    typename A = std::allocator<std::pair<const std::u8string, V>>>
using u8string_unordered_map = basic_string_unordered_map<char8_t, V, A>;

template<typename A = std::allocator<std::u8string>>
using u8string_unordered_set_with_alloc =
    basic_string_unordered_set<char8_t, A>;

using u8string_unordered_set = u8string_unordered_set_with_alloc<>;

// Specialized aliases over `char16_t`.
template<typename V = std::u16string,
    typename A = std::allocator<std::pair<const std::u16string, V>>>
using u16string_map = basic_string_map<char16_t, V, A>;

template<typename A = std::allocator<std::u16string>>
using u16string_set_with_alloc = basic_string_set<char16_t, A>;

using u16string_set = u16string_set_with_alloc<>;

template<typename V = std::u16string,
    typename A = std::allocator<std::pair<const std::u16string, V>>>
using u16string_unordered_map = basic_string_unordered_map<char16_t, V, A>;

template<typename A = std::allocator<std::u16string>>
using u16string_unordered_set_with_alloc =
    basic_string_unordered_set<char16_t, A>;

using u16string_unordered_set = u16string_unordered_set_with_alloc<>;

// Specialized aliases over `char32_t`.
template<typename V = std::u32string,
    typename A = std::allocator<std::pair<const std::u32string, V>>>
using u32string_map = basic_string_map<char32_t, V, A>;

template<typename A = std::allocator<std::u32string>>
using u32string_set_with_alloc = basic_string_set<char32_t, A>;

using u32string_set = u32string_set_with_alloc<>;

template<typename V = std::u32string,
    typename A = std::allocator<std::pair<const std::u32string, V>>>
using u32string_unordered_map = basic_string_unordered_map<char32_t, V, A>;

template<typename A = std::allocator<std::u32string>>
using u32string_unordered_set_with_alloc =
    basic_string_unordered_set<char32_t, A>;

using u32string_unordered_set = u32string_unordered_set_with_alloc<>;

#pragma endregion
}} // namespace corvid::containers
