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

#include "../../meta/concepts.h"
#include "string_view_wrapper.h"

namespace corvid {
inline namespace optstringview {

#pragma region basic_opt_string_view

// Optional string view
//
// A `std::string_view` with optional semantics, distinguishing null from
// empty. This is designed as a general-purpose `std::string_view` replacement
// for when you need to honor null semantics and/or enforce type-safety.
//
// It is built on `string_view_wrapper`, but converts to a
// `std::string_view` freely in both directions. The wrapper supplies the
// read-only view API, support for the null/empty distinction, and the
// `std::optional` interface; this class adds the lenient, null-aware
// constructors and the reslicing operations (`substr`, `remove_prefix`,
// `remove_suffix`) that its lack of any invariant makes safe.
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
// Like `std::optional<std::string_view>`, it can distinguish between null and
// empty, and offers existence checks and defaults.
//
// The most convenient way to declare a `constexpr opt_string_view` is with a
// literal using the `_osv` UDL.
//
// For comparison purposes, `empty` and `null` values are equivalent. If you
// want to check for an exact match that distinguishes between these two
// states, use `same`.
//
// The optional `Tag` parameter turns this into a tagged view: a non-void `Tag`
// makes distinct tags distinct types that don't implicitly mix, and forces the
// view-taking (and other string-like) constructors to be `explicit` so a raw
// `std::string_view` can't silently become a tagged one.
//
// The untagged default (`Tag` is `void`) keeps those conversions implicit, so
// `opt_string_view` stays a drop-in replacement for `std::string_view`. The
// `tagged_string_view` alias names the tagged form. Conversion the other way
// (to `std::string_view`) stays implicit for both, as does comparison against
// any view.
template<typename Tag = void, CharType Char = char>
class basic_opt_string_view final
    : public string_view_wrapper<basic_opt_string_view<Tag, Char>, Char> {
  using base = string_view_wrapper<basic_opt_string_view<Tag, Char>, Char>;

  // A tagged view forces string-like conversions to be explicit; the untagged
  // default leaves them implicit.
  static constexpr bool tagged = !std::is_void_v<Tag>;

#pragma region Member types
public:
  using tag_type = Tag;
  using char_t = base::char_t;
  using view_t = base::view_t;
  using size_type = base::size_type;
  using base::npos;

  using string_t = std::basic_string<Char>;

#pragma endregion
#pragma region Construction

  constexpr basic_opt_string_view() noexcept = default;
  constexpr basic_opt_string_view(std::nullptr_t) noexcept {}
  constexpr basic_opt_string_view(std::nullopt_t) noexcept {}

  constexpr explicit(tagged) basic_opt_string_view(view_t sv) noexcept
      : base{sv} {}
  constexpr explicit(tagged) basic_opt_string_view(const string_t& s) noexcept
      : base{view_t{s}} {}
  constexpr explicit(tagged)
      basic_opt_string_view(const char_t* ps, size_type l)
      : base{ps, l} {}
  constexpr explicit(tagged) basic_opt_string_view(const char_t* psz)
      : base{psz} {}
  template<std::contiguous_iterator It, std::sized_sentinel_for<It> End>
  requires std::same_as<std::iter_value_t<It>, char_t> &&
           (!std::convertible_to<End, size_type>)
  constexpr explicit(tagged) basic_opt_string_view(It first, End last)
      : base{std::to_address(first), static_cast<size_type>(last - first)} {}

  // Optional as null.
  constexpr explicit(tagged)
      basic_opt_string_view(const std::optional<view_t>& osv) noexcept
      : base{osv ? view_t{*osv} : view_t{}} {}
  constexpr explicit(tagged)
      basic_opt_string_view(const std::optional<string_t>& os) noexcept
      : base{os ? view_t{*os} : view_t{}} {}

  // View assignment exists only for the untagged form; a tagged view admits a
  // raw view only through its explicit constructor.
  constexpr basic_opt_string_view& operator=(view_t sv) noexcept
  requires(!tagged)
  {
    base::sv_ = sv;
    return *this;
  }
  constexpr basic_opt_string_view& operator=(const char_t* s) noexcept
  requires(!tagged)
  {
    base::sv_ = base::from_ptr(s);
    return *this;
  }

#pragma endregion
#pragma region Reslicing

  [[nodiscard]] constexpr basic_opt_string_view
  substr(size_type pos = 0, size_type n = npos) const {
    return basic_opt_string_view{base::sv_.substr(pos, n)};
  }
  constexpr void remove_prefix(size_type n) { base::sv_.remove_prefix(n); }
  constexpr void remove_suffix(size_type n) { base::sv_.remove_suffix(n); }

#pragma endregion
};

#pragma region opt_string_view

// Untagged: a drop-in replacement for `std::string_view` with null/empty and
// optional semantics.
using opt_string_view = basic_opt_string_view<>;

#pragma endregion
#pragma region tagged_string_view

// Tagged: distinct `Tag` types are distinct, non-interconverting view types,
// and construction from a raw view is explicit.
template<typename Tag, CharType Char = char>
using tagged_string_view = basic_opt_string_view<Tag, Char>;

#pragma endregion
#pragma endregion

} // namespace optstringview

namespace literals {

#pragma region UDL

// opt_string_view literal.
consteval opt_string_view
operator""_osv(const char* ps, std::size_t n) noexcept {
  return opt_string_view{ps, n};
}

// Null literal; must pass 0.
consteval opt_string_view operator""_osv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("opt_string_view not zero");
  return opt_string_view{};
}

#pragma endregion

} // namespace literals
} // namespace corvid
