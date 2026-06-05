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
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "string_view_wrapper.h"

namespace corvid {
inline namespace optstringview {

// Optional string view
//
// A `std::string_view` with optional semantics, distinguishing null from
// empty.
//
// Fundamentally it's a `std::string_view` with different construction and a
// few `std::optional`-like methods. It changes construction and interpretation
// but does not constrain content.
//
// It is built on `string_view_wrapper`, but converts to a
// `std::string_view` freely. The wrapper supplies the read-only view API, the
// null/empty distinction, and the optional interface; this class adds the
// lenient constructors and the reslicing operations (`substr`,
// `remove_prefix`, `remove_suffix`) that its lack of any invariant makes safe.
//
// Unlike using `std::string` for everything, this class avoids the overhead of
// copying while still preserving the distinction between `empty` and `null`.
//
// Like a `std::string_view`, `data` sometimes returns `nullptr` when `size` is
// 0. When it's not `nullptr`, then (as in `std::string`) it returns an empty
// string.
//
// Unlike `std::string_view`, it can be constructed from a `nullptr`.
//
// Like `std::optional<std::string_view>`, it can distinguish between missing
// null and empty, and offers such things as existence checks and defaults.
//
// The most convenient way to declare a `constexpr opt_string_view` is with a
// literal using the `_osv` UDL.
//
// For comparison purposes, `empty` and `null` values are always equivalent. If
// you want to check for an exact match that distinguishes between these two
// states, use `same`.

#pragma region basic_opt_string_view

template<typename char_t = char>
class basic_opt_string_view final
    : public string_view_wrapper<basic_opt_string_view<char_t>, char_t> {
  using wrapper = string_view_wrapper<basic_opt_string_view<char_t>, char_t>;

#pragma region Member types
public:
  using SV = typename wrapper::view_t;
  using size_type = typename wrapper::size_type;
  using wrapper::npos;

#pragma endregion
#pragma region Construction

  constexpr basic_opt_string_view() noexcept = default;
  constexpr basic_opt_string_view(std::nullptr_t) noexcept {}
  constexpr basic_opt_string_view(std::nullopt_t) noexcept {}

  constexpr basic_opt_string_view(SV sv) noexcept : wrapper{sv} {}
  constexpr basic_opt_string_view(const std::basic_string<char_t>& s) noexcept
      : wrapper{SV{s}} {}
  constexpr basic_opt_string_view(const char_t* ps, size_type l)
      : wrapper{ps, l} {}
  constexpr basic_opt_string_view(const char_t* psz) : wrapper{psz} {}
  template<std::contiguous_iterator It, std::sized_sentinel_for<It> End>
  requires std::same_as<std::iter_value_t<It>, char_t> &&
           (!std::convertible_to<End, size_type>)
  constexpr basic_opt_string_view(It first, End last)
      : wrapper{std::to_address(first), static_cast<size_type>(last - first)} {
  }

  // Optional as null.
  constexpr basic_opt_string_view(const std::optional<SV>& osv) noexcept
      : wrapper{osv ? SV{*osv} : SV{}} {}
  constexpr basic_opt_string_view(
      const std::optional<std::basic_string<char_t>>& os) noexcept
      : wrapper{os ? SV{*os} : SV{}} {}

  constexpr basic_opt_string_view& operator=(SV sv) noexcept {
    this->sv_ = sv;
    return *this;
  }

#pragma endregion
#pragma region Reslicing

  // Safe because, unlike `cstring_view`, this imposes no termination
  // invariant.
  [[nodiscard]] constexpr SV
  substr(size_type pos = 0, size_type n = npos) const {
    return this->sv_.substr(pos, n);
  }
  constexpr void remove_prefix(size_type n) { this->sv_.remove_prefix(n); }
  constexpr void remove_suffix(size_type n) { this->sv_.remove_suffix(n); }

#pragma endregion
};

using opt_string_view = basic_opt_string_view<char>;

#pragma endregion

} // namespace optstringview

namespace literals {

#pragma region UDL

// opt_string_view literal.
constexpr opt_string_view
operator""_osv(const char* ps, std::size_t n) noexcept {
  return opt_string_view{ps, n};
}

// Null literal; must pass 0.
constexpr opt_string_view operator""_osv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("opt_string_view not zero");
  return opt_string_view{};
}

#pragma endregion

} // namespace literals
} // namespace corvid
