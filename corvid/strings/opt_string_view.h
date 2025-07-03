// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
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
#include <iostream>
#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>

namespace corvid {
inline namespace optstringview {

// Optional string view.
//
// The purpose of this class is to provide a std::string_view with
// optional semantics, better handling null vs. empty.
//
// Fundamentally, it's just a `std::string_view` with some semantic differences
// in the construction and a few `std::optional`-like methods. It changes
// construction and interpretation, but does not fundamentally constrain
// content, which is why it can be implemented as a child of
// `std::string_view`. In specific, it assumes that any string view it's fed
// was valid.
//
// Unlike using `std::string` for everything, this avoids the overhead of
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
// The most convenient way to declare a `constexpr opt_string_view` is
// with a literal using the `_osv` UDL.
//
// For comparison purposes, `empty` and `null` values are always equivalent. If
// you want to check for an exact match that distinguishes between these two
// states, use `same`.
class opt_string_view: public std::string_view {
public:
  using base = std::string_view;

  //
  // Construction
  //

  constexpr opt_string_view() noexcept {}
  constexpr opt_string_view(std::nullptr_t) noexcept {}
  constexpr opt_string_view(std::nullopt_t) noexcept {}

  constexpr opt_string_view(const base& sv) noexcept : base{sv} {};
  constexpr opt_string_view(const std::string& s) noexcept : base{s} {}
  constexpr opt_string_view(const char* ps, size_t l)
      : base{from_ptr(ps, l)} {};
  constexpr opt_string_view(const char* psz) : base{from_ptr(psz)} {};
  template<std::contiguous_iterator It, std::sized_sentinel_for<It> End>
  requires std::same_as<std::iter_value_t<It>, char> &&
           (!std::convertible_to<End, size_type>)
  constexpr opt_string_view(It first, End last)
      : base{from_ptr(std::to_address(first), last - first)} {}

  // Optional as null.
  constexpr opt_string_view(
      const std::optional<std::string_view>& osv) noexcept
      : base{osv ? *osv : base{}} {}
  constexpr opt_string_view(const std::optional<std::string>& os) noexcept
      : base{os ? *os : base{}} {}

  constexpr opt_string_view& operator=(const base& sv) noexcept {
    return *this = opt_string_view{sv};
  }

  // Novel

  // Whether `data` is `nullptr`.
  constexpr bool null() const noexcept { return !data(); }

  // Essentially `operator===`, distinguishing between empty and null.
  constexpr bool same(opt_string_view v) const noexcept {
    return ((*this == v) && (null() == v.null()));
  }

  // std::optional workalike.
  constexpr bool has_value() const noexcept { return !null(); }
  constexpr explicit operator bool() const noexcept { return has_value(); }
  constexpr const base& value() const noexcept { return *this; }
  constexpr base& operator*() noexcept { return *this; }
  constexpr const base& operator*() const noexcept { return *this; }
  constexpr base value_or(base v) const noexcept {
    return *this ? base{*this} : v;
  }
  constexpr base* operator->() { return this; }
  constexpr const base* operator->() const { return this; }
  constexpr explicit
  operator std::optional<std::string_view>() const noexcept {
    return *this ? *this : std::nullopt;
  }
  template<typename T>
  constexpr void swap(std::optional<T>& other) noexcept {
    opt_string_view tmp = *this;
    *this = other;
    other = tmp;
  }
  constexpr void reset() noexcept { *this = std::nullopt; }
  template<class... Args>
  constexpr opt_string_view& emplace(Args&&... args) {
    return *this = opt_string_view{std::forward<Args>(args)...};
  }
  template<class U, class... Args>
  constexpr opt_string_view&
  emplace(std::initializer_list<U> ilist, Args&&... args) {
    return *this = opt_string_view{ilist, std::forward<Args>(args)...};
  }

private:
  static constexpr base from_ptr(const char* psz) {
    // Null pointer maps to default instance.
    return psz ? base{psz} : base{};
  }

  static constexpr base from_ptr(const char* ps, size_t l) {
    // Null is always zero-length. But note that we do not enforce this when
    // dealing with base instances. We expect them to be valid, since a null
    // with a non-zero length is undefined behavior for them, but not us.
    if (!ps && l) l = 0;
    return base{ps, l};
  }
};

} // namespace optstringview

namespace literals {
//
// UDL
//

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

} // namespace literals
} // namespace corvid
