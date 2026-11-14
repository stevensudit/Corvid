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
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <string_view>

#include "../meta/concepts.h"
#include "../meta/fixed_string.h"
#include "string_view_wrapper.h"

namespace corvid { inline namespace internedstringview {

#pragma region interned_string_view_wrapper

// Interned string view wrapper
//
// CRTP layer over `string_view_wrapper` for views whose identity is their
// location. Equality and ordering use the address and length alone, hashing
// the address alone, and none of them reads the characters, so comparing two
// interned views costs two integer compares no matter how long they are.
//
// The class keeps no intern table. The caller interns, or does the moral
// equivalent, such as handing out copies of a few `static constexpr`
// constants. The caller must guarantee two things: no two addresses ever hold
// the same contents, and the storage never moves.
//
// A node-based set of strings is stable. A vector of strings is not, and a
// short string copies its bytes with it. Comparison asserts the first
// guarantee, which opportunistically catches a violation when it happens to
// be compared.
//
// Two ways to use it:
//
// - Derive, when the child supplies the interning (the child type is the tag,
//   and may itself be tagged). The protected constructors are the only way in,
//   so the child decides who may construct and whether a default instance
//   exists:
//
//     class symbol: public interned_string_view_wrapper<symbol> {
//       using base = interned_string_view_wrapper<symbol>;
//       friend class symbol_table; // The sole minter.
//       explicit symbol(view_t sv) noexcept : base{sv} {}
//     public:
//       constexpr symbol() noexcept : base{nullptr} {} // Optional.
//     };
//
// - Specialize `basic_interned_string_view` (usually through one of the
//   `interned_string_view` aliases) on a tag, choosing the default-instance
//   policy by type. Its `create` mints a view over text the caller has
//   interned.
//
// Either way, construction from a raw view, literal, or string is otherwise
// impossible, and conversion to `std::string_view` is explicit, so an interned
// view never slips into a content comparison.
template<typename Child, CharType CharT = char>
class interned_string_view_wrapper: public string_view_wrapper<Child, CharT> {
  using base = string_view_wrapper<Child, CharT>;

#pragma region Member types
public:
  using child_t = base::child_t;
  using char_t = base::char_t;
  using view_t = base::view_t;
  using size_type = base::size_type;

  // Opts out of the base's content comparison and selects the identity hash.
  static constexpr bool compares_by_identity = true;

#pragma endregion
#pragma region Underlying

  // Explicit, hiding the base's implicit conversion.
  [[nodiscard]] constexpr explicit operator view_t() const noexcept {
    return this->view();
  }

#pragma endregion
#pragma region Comparison

  // Identity: the same address and length.
  [[nodiscard]] friend constexpr bool
  operator==(const child_t& l, const child_t& r) noexcept {
    assert(invariant_holds(l, r));
    return identical(l, r);
  }

  // Identity order: by address under the pointer total order, then by length,
  // so a prefix minted over the start of the same storage sorts before the
  // whole.
  [[nodiscard]] friend constexpr std::strong_ordering
  operator<=>(const child_t& l, const child_t& r) noexcept(
      noexcept(std::compare_three_way{}(l.data(), r.data()))) {
    assert(invariant_holds(l, r));
    if (const auto by_address = std::compare_three_way{}(l.data(), r.data());
        by_address != 0)
      return by_address;
    return l.size() <=> r.size();
  }

#pragma endregion
#pragma region Construction
protected:
  // The null instance, for a child that wants a default.
  constexpr explicit interned_string_view_wrapper(std::nullptr_t) noexcept {}

  // Any view, for a child's own interning or its own default.
  constexpr explicit interned_string_view_wrapper(view_t sv) noexcept
      : base{sv} {}

#pragma endregion
#pragma region Helpers
private:
  [[nodiscard]] static constexpr bool
  identical(const child_t& l, const child_t& r) noexcept {
    return l.data() == r.data() && l.size() == r.size();
  }

  // Whether this pair respects the caller's guarantee that two interned
  // strings at different addresses never share contents. Comparison asserts
  // it, which opportunistically catches a violation when it happens to be
  // compared.
  [[nodiscard]] static constexpr bool
  invariant_holds(const child_t& l, const child_t& r) noexcept {
    return identical(l, r) || l.null() || r.null() || l.view() != r.view();
  }

#pragma endregion
};

#pragma endregion
#pragma region Default policies

// No default instances. Ironically, this is the default default policy.
struct no_default {};

// Default instances are null, so all of them are identical: the degenerate
// case of interning, where every default fails to point in the same way.
struct nullptr_default {};

// Default instances point at one static copy of `S`.
//
// Every module (DLL or shared object) carries its own copy, so defaults made
// on opposite sides of a module boundary have equal contents at different
// addresses and break identity. Prefer `nullptr_default` across such
// boundaries.
template<basic_fixed_string S>
struct literal_default {
  static constexpr auto value = S;
};

#pragma endregion
#pragma region basic_interned_string_view

// Interned string view, specialized on a tag.
//
// `Tag` makes distinct specializations distinct types. `DefaultT` picks the
// default-instance policy: `no_default`, `nullptr_default`, or
// `literal_default<"...">`. See `interned_string_view_wrapper` for the
// contract.
template<typename Tag, CharType CharT = char, typename DefaultT = no_default>
class basic_interned_string_view final
    : public interned_string_view_wrapper<
          basic_interned_string_view<Tag, CharT, DefaultT>, CharT> {
  using base = interned_string_view_wrapper<
      basic_interned_string_view<Tag, CharT, DefaultT>, CharT>;

#pragma region Member types
public:
  using tag_type = Tag;
  using default_type = DefaultT;
  using char_t = base::char_t;
  using view_t = base::view_t;

  static_assert(
      std::same_as<default_type, no_default> ||
          std::same_as<default_type, nullptr_default> ||
          requires {
            { default_type::value.view() } -> std::same_as<view_t>;
          },
      "DefaultT must be no_default, nullptr_default, or a literal_default of "
      "the view's character type");

#pragma endregion
#pragma region Minting

  // Mint a view over `sv`, which the caller has already interned.
  [[nodiscard]] static constexpr basic_interned_string_view create(
      view_t sv) noexcept {
    return basic_interned_string_view{sv};
  }

#pragma endregion
#pragma region Construction

  // The default instance, when the policy provides one.
  constexpr basic_interned_string_view() noexcept
  requires(!std::same_as<default_type, no_default>)
      : base{default_view()} {}

private:
  constexpr explicit basic_interned_string_view(view_t sv) noexcept
      : base{sv} {}

#pragma endregion
#pragma region Helpers

  [[nodiscard]] static constexpr view_t default_view() noexcept
  requires(!std::same_as<default_type, no_default>)
  {
    if constexpr (std::same_as<default_type, nullptr_default>)
      return view_t{};
    else
      return default_type::value.view();
  }

#pragma endregion
};

// Specialized aliases.
template<typename Tag, typename DefaultT = no_default>
using interned_string_view = basic_interned_string_view<Tag, char, DefaultT>;
template<typename Tag, typename DefaultT = no_default>
using interned_wstring_view =
    basic_interned_string_view<Tag, wchar_t, DefaultT>;

#pragma endregion

}} // namespace corvid::internedstringview
