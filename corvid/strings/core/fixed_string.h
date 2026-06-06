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
#include <concepts>
#include <ostream>
#include <string_view>
#include <type_traits>

#include "../../meta.h"
#include "cstring_view.h"

namespace corvid::strings { inline namespace fixed {

#pragma region basic_fixed_string

// Fixed string, suitable for use as a non-type template parameter.
//
// While I didn't know it at the start, it turns out that, much like
// `cstring_view`, this class likely owes its existence to a dropped ANSI
// committee proposal by Andrew Tomazos (and Michael Price).
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0259r0.pdf
//
// As of C++23/26, it's still needed, but there are proposals to add a
// `std::fixed_string` to the standard, which would make this class redundant.
// See https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3094r0.html
template<CharType CharT, std::size_t N>
struct basic_fixed_string {
#pragma region Types

  using char_t = CharT;
  using value_type = char_t;
  using pointer = char_t*;
  using const_pointer = const char_t*;
  using reference = char_t&;
  using const_reference = const char_t&;
  using const_iterator = const char_t*;
  using iterator = const_iterator;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

#pragma endregion
#pragma region Construction

  // Construct from a string literal. The deduction guide supplies `N` as the
  // literal length, less the terminator.
  constexpr explicit(false)
      basic_fixed_string(const char_t (&txt)[N + 1]) noexcept {
    for (size_t ndx = 0; ndx != N; ++ndx) do_not_use[ndx] = txt[ndx];
  }

  // Construct from a pointer when there is no array to bind to, with `N`
  // carried as a compile-time tag.
  constexpr basic_fixed_string(const char_t* ptr,
      std::integral_constant<std::size_t, N>) noexcept {
    for (size_t ndx = 0; ndx != N; ++ndx) do_not_use[ndx] = ptr[ndx];
  }

  // Construct from exactly `N` characters.
  template<std::convertible_to<CharT>... Rest>
  requires(1 + sizeof...(Rest) == N)
  constexpr explicit basic_fixed_string(char_t first, Rest... rest) noexcept {
    size_t ndx = 0;
    do_not_use[ndx++] = first;
    ((do_not_use[ndx++] = static_cast<CharT>(rest)), ...);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] constexpr bool empty() const noexcept { return N == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return N; }
  [[nodiscard]] constexpr const_pointer data() const noexcept {
    return do_not_use;
  }
  [[nodiscard]] constexpr const char_t* c_str() const noexcept {
    return do_not_use;
  }
  [[nodiscard]] constexpr value_type operator[](
      size_type index) const noexcept {
    return do_not_use[index];
  }

  [[nodiscard]] constexpr const_iterator begin() const noexcept {
    return do_not_use;
  }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
    return do_not_use;
  }
  [[nodiscard]] constexpr const_iterator end() const noexcept {
    return do_not_use + N;
  }
  [[nodiscard]] constexpr const_iterator cend() const noexcept {
    return do_not_use + N;
  }

#pragma endregion
#pragma region Conversions

  [[nodiscard]] constexpr std::basic_string_view<CharT> view() const {
    // NOLINTNEXTLINE(bugprone-string-constructor)
    return {do_not_use, N};
  }
  [[nodiscard]] constexpr operator std::basic_string_view<CharT>() const {
    return view();
  }

  // A fixed string is necessarily terminated.
  [[nodiscard]] constexpr basic_cstring_view<std::basic_string_view<char_t>>
  cview() const {
    return basic_cstring_view<std::basic_string_view<char_t>>{do_not_use,
        N + 1};
  }

#pragma endregion
#pragma region Operations

  // Concatenation. The result length is the sum of the operand lengths.
  template<std::size_t N2>
  [[nodiscard]] constexpr friend basic_fixed_string<CharT, N + N2>
  operator+(const basic_fixed_string& lhs,
      const basic_fixed_string<CharT, N2>& rhs) noexcept {
    CharT buf[N + N2 + 1]{};
    for (size_t ndx = 0; ndx != N; ++ndx) buf[ndx] = lhs.do_not_use[ndx];
    for (size_t ndx = 0; ndx != N2; ++ndx) buf[N + ndx] = rhs.do_not_use[ndx];
    return basic_fixed_string<CharT, N + N2>{buf,
        std::integral_constant<std::size_t, N + N2>{}};
  }

  [[nodiscard]] constexpr bool operator==(
      const basic_fixed_string& other) const {
    return view() == other.view();
  }
  template<std::size_t N2>
  [[nodiscard]] friend constexpr bool operator==(const basic_fixed_string& lhs,
      const basic_fixed_string<CharT, N2>& rhs) {
    return lhs.view() == rhs.view();
  }

  template<std::size_t N2>
  [[nodiscard]] friend constexpr auto
  operator<=>(const basic_fixed_string& lhs,
      const basic_fixed_string<CharT, N2>& rhs) {
    return lhs.view() <=> rhs.view();
  }

  template<typename Traits>
  friend std::basic_ostream<CharT, Traits>&
  operator<<(std::basic_ostream<CharT, Traits>& os,
      const basic_fixed_string<CharT, N>& str) {
    return os << str.view();
  }

#pragma endregion
#pragma region Data members

  // This can't be made private or const, but do not ever use it.
  CharT do_not_use[N + 1]{};

#pragma endregion
};

// Deduction guide for basic_fixed_string from string literal.
template<CharType CharT, std::size_t N>
basic_fixed_string(CharT const (&)[N]) -> basic_fixed_string<CharT, N - 1>;

// Deduction guide for the pointer-plus-size-tag constructor.
template<CharType CharT, std::size_t N>
basic_fixed_string(const CharT*, std::integral_constant<std::size_t, N>)
    -> basic_fixed_string<CharT, N>;

// Deduction guide for the character-pack constructor.
template<CharType CharT, std::convertible_to<CharT>... Rest>
basic_fixed_string(CharT, Rest...)
    -> basic_fixed_string<CharT, 1 + sizeof...(Rest)>;

// The common case: a fixed string of `char`.
template<std::size_t N>
using fixed_string = basic_fixed_string<char, N>;

#pragma endregion

}} // namespace corvid::strings::fixed
