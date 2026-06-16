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
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>

#include "string_view_wrapper.h"
#include "../meta/crossplatform.h"

namespace corvid {
inline namespace cstringview {

// C-string view
//
// This header defines `basic_cstring_view`, a `std::string_view` that also
// guarantees zero termination, along with its per-character-type aliases
// (`cstring_view`, `wcstring_view`, and so on) and the `_czsv` family of UDLs
// in the `literals` namespace. See the class comment below for the rationale
// and invariants.

#pragma region basic_cstring_view

// String view of a C-style, zero-terminated string.
//
// The purpose of this class is to provide a drop-in replacement for
// `std::string_view` that works seamlessly with functions that have a C string
// interface based on `const char*`. (It also provides a shallow interface into
// `const char*` or a `std::optional` of a `std::string`.)
//
// Unlike using `std::string` for everything, it avoids the overhead of
// copying, and preserves the distinction between `empty` and `null`. This
// makes it suitable for holding the return value from a function like `getenv`
// or passing in a value to a function like `setenv`.
//
// Like a `std::string`, the terminator is not included in the `size` but is
// guaranteed to be there after the last character returned from `c_str` (which
// never returns `nullptr`).
//
// Like a `std::string_view`, `data` sometimes returns `nullptr` when `size` is
// 0. When it's not `nullptr`, then (as in `std::string`) the terminator is
// guaranteed because it contains an empty string.
//
// Constructing from a `std::string` or a `const char*` is safe and fast, as
// the termination is guaranteed, but constructing from inputs that include the
// length but don't guarantee termination, like `const char*, size_t` or
// `std::string_view`, is trickier.
//
// The constructor has to be able to confirm that it's terminated, but it can't
// look past the end of the buffer because that's outside the valid range.
// There are no guarantees possible about what's in that byte or even that it
// can be dereferenced. It also amounts to an implicit requirement, which would
// be a questionable design choice here.
//
// The solution in these cases is to require the explicit inclusion of the
// terminator in the length passed in. The constructor can then inspect the
// last character to ensure that it's the terminator, adjusting the length to
// exclude it. If the terminator is not found, the constructor will throw.
//
// Notes:
//
// Can be explicitly cast from `std::string_view` (with the above proviso about
// including the terminator) and implicitly cast to `std::string_view` (or
// explicitly cast by calling `view`).
//
// The most convenient way to declare a `constexpr cstring_view` is with a
// literal using the `_czsv` UDL.
//
// The `substr` and `remove_suffix` functions cannot be supported because they
// would violate the termination invariant. The workaround is to copy to
// `std::string_view` and modify that, instead.
//
// For comparison purposes, `empty` and `null` values are always equivalent. If
// you want to check for an exact match that distinguishes between these two
// states, use `same`.
//
// Both `c_str` and `data` return a pointer such that the range
// `[foo; foo + size()]` is valid. The difference is that, when `null`, a call
// to `c_str` returns an empty, terminated string but `data` returns `nullptr`.
//
// This revanchist implementation is based closely on Andrew Tomazos'  wrongly
// rejected ANSI committee proposal.
// http://open-std.org/JTC1/SC22/WG21/docs/papers/2019/p1402r0.pdf
// https://github.com/cplusplus/papers/issues/189
template<typename T = std::string_view>
class basic_cstring_view final
    : public string_view_wrapper<basic_cstring_view<T>,
          typename T::value_type> {
  using base =
      string_view_wrapper<basic_cstring_view<T>, typename T::value_type>;

#pragma region Member types
public:
  using view_t = base::view_t;
  using char_t = view_t::value_type;
  using size_type = base::size_type;
  using const_pointer = base::const_pointer;
  using base::npos;

#pragma endregion
#pragma region Construction

  // Safe construction.
  //
  // Always works.
  constexpr basic_cstring_view() noexcept = default;
  constexpr basic_cstring_view(std::nullptr_t) noexcept {}
  constexpr basic_cstring_view(std::nullopt_t) noexcept {}

  constexpr basic_cstring_view(const std::string& s) noexcept
      : base{view_t{s}} {}
  // Allows `nullptr`.
  constexpr basic_cstring_view(const char_t* psz) : base{psz} {}

  // Risky construction.
  //
  // To demonstrate that it's actually terminated, the input must extend so
  // that the last character is a terminator. Otherwise, this is a logic error
  // and we throw.
  constexpr explicit basic_cstring_view(view_t sv) : base{from_sv(sv)} {}
  constexpr explicit basic_cstring_view(const char_t* ps, size_type len)
      : base{from_sv(base::from_ptr(ps, len))} {}
  template<std::contiguous_iterator It, std::sized_sentinel_for<It> End>
  requires std::same_as<std::iter_value_t<It>, char> &&
           (!std::convertible_to<End, size_type>)
  constexpr explicit basic_cstring_view(It first, End last)
      : basic_cstring_view{std::to_address(first), size_type(last - first)} {}

  // Optional as null.
  template<typename U>
  requires std::is_constructible_v<view_t, U>
  constexpr basic_cstring_view(const std::optional<U>& opt)
      : basic_cstring_view{opt.has_value() ? basic_cstring_view{*opt}
                                           : basic_cstring_view{}} {}

#pragma endregion
#pragma region Reslicing

  // Safe because trimming the front keeps the terminator at the end.
  constexpr void remove_prefix(size_type n) { this->sv_.remove_prefix(n); }

  // `remove_suffix` and `substr` are omitted: they would break the termination
  // invariant unless the cut happened to land on the terminator.

#pragma endregion
#pragma region c_str

  // Pointer to a terminated string; never `nullptr`. When `null`, returns an
  // empty, terminated string. (In contrast, `data` returns `nullptr` when
  // `null`.)
  [[nodiscard]] constexpr const_pointer c_str() const noexcept {
    if (const auto p = this->data()) return p;
    static constexpr char_t empty[1]{};
    return empty;
  }

#pragma endregion
#pragma region Helpers
private:
  [[nodiscard]] static constexpr view_t from_sv(view_t sv) {
    // Empty is allowed, but only when null. A non-null empty must include the
    // terminator in its length.
    if (sv.empty()) {
      if (sv.data()) throw std::length_error("cstring_view len");
      return sv;
    }

    // Ensure terminator is there, and then exclude it from length.
    if (sv.back()) throw std::invalid_argument("cstring_view arg");
    sv.remove_suffix(1);
    return sv;
  }

#pragma endregion
};

// Specialized aliases.
using cstring_view = basic_cstring_view<std::string_view>;
using wcstring_view = basic_cstring_view<std::wstring_view>;
using u8cstring_view = basic_cstring_view<std::u8string_view>;
using u16cstring_view = basic_cstring_view<std::u16string_view>;
using u32cstring_view = basic_cstring_view<std::u32string_view>;

#pragma endregion

} // namespace cstringview
namespace literals {

#pragma region UDL

// basic_cstring_view literals.
consteval cstring_view operator""_czsv(const char* ps, std::size_t n) {
  return cstring_view{std::string_view{ps, n + 1}};
}
consteval wcstring_view operator""_wczsv(const wchar_t* ps, std::size_t n) {
  return wcstring_view{std::wstring_view{ps, n + 1}};
}
consteval u8cstring_view operator""_u8csv(const char8_t* ps, std::size_t n) {
  return u8cstring_view{std::u8string_view{ps, n + 1}};
}
consteval u16cstring_view
operator""_u16csv(const char16_t* ps, std::size_t n) {
  return u16cstring_view{std::u16string_view{ps, n + 1}};
}
consteval u32cstring_view
operator""_u32csv(const char32_t* ps, std::size_t n) {
  return u32cstring_view{std::u32string_view{ps, n + 1}};
}

// Null literal; must pass 0.
consteval cstring_view operator""_czsv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("cstring_view not zero");
  return cstring_view{};
}
consteval wcstring_view operator""_wczsv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("wcstring_view not zero");
  return wcstring_view{};
}
consteval u8cstring_view operator""_u8csv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("u8cstring_view not zero");
  return u8cstring_view{};
}
consteval u16cstring_view operator""_u16csv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("u16cstring_view not zero");
  return u16cstring_view{};
}
consteval u32cstring_view operator""_u32csv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("u32cstring_view not zero");
  return u32cstring_view{};
}

// Environment.
cstring_view operator""_env(const char* ps, std::size_t) noexcept {
  // MSVC's CRT deprecates getenv in favor of _dupenv_s, but the borrowed
  // pointer is exactly what this non-owning view wants, so suppress the nag
  // rather than change the contract. clang-cl flags it as
  // -Wdeprecated-declarations, MSVC cl as C4996.
  PRAGMA_DIAG(push)
  PRAGMA_IGNORED("-Wdeprecated-declarations")
  PRAGMA_MSVC_IGNORED(4996)
  return std::getenv(ps);
  PRAGMA_DIAG(pop)
}

#pragma endregion

} // namespace literals
} // namespace corvid
