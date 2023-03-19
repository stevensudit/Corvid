// Corvid20: A general-purpose C++ 20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
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
#include "strings_shared.h"

namespace corvid {
inline namespace cstringview {

// String view of a C-style, zero-terminated string.
//
// The purpose of this class is to provide a drop-in replacement for
// `std::string_view` that works seamlessly with functions that have a C string
// interface based on `const char*`. (It also provides a shallow interface into
// a `std::optional` of a `std::string` or `const char*`.)
//
// Unlike using `std::string` for everything, this avoids the overhead of
// copying and preserves the distinction between `empty` and `null`. This makes
// it suitable for holding the return value from a function like `getenv` or
// passing in a value to a function like `setenv`.
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
// can be dereferenced.
//
// The solution in these cases is to require the inclusion of the terminator in
// the length passed in. The constructor can then inspect the last character to
// ensure that it's the terminator, adjusting the length to exclude it. If the
// terminator is not found, the constructor will throw.
//
// Notes:
//
// Can be explicitly cast from `std::string_view` and implicitly cast to
// `std::string_view` (or cast by calling `view`).
//
// The most convenient way to declare a `constexpr cstring_view` is with a
// literal using the `_csv` UDL.
//
// The `substr` and `remove_suffix` functions cannot be supported because they
// would violate the termination invariant. The workaround is to cast to
// `std::string_view` and modify that copy, instead.
//
// For comparison purposes, `empty` and `null` values are always equivalent. If
// you want to check for an exact match that distinguishes between these two
// states, use `same`.
//
// Both `c_str` and `data` return a pointer such that the range
// `[foo; foo + size()]` is valid. The difference is that, when `null`, a call
// to `c_str` returns an empty, terminated string but `data` returns `nullptr`.
//
// Based closely on Andrew Tomazos' wrongly-rejected ANSI committee proposal.
// http://open-std.org/JTC1/SC22/WG21/docs/papers/2019/p1402r0.pdf
// https://github.com/cplusplus/papers/issues/189
class cstring_view {
public:
  using T = std::string_view;
  using CharT = T::value_type;
  using traits_type = T::traits_type;
  using value_type = T::value_type;
  using pointer = T::pointer;
  using const_pointer = T::const_pointer;
  using reference = T::reference;
  using const_reference = T::const_reference;
  using const_iterator = T::const_iterator;
  using iterator = T::iterator;
  using const_reverse_iterator = T::const_reverse_iterator;
  using reverse_iterator = T::reverse_iterator;
  using size_type = T::size_type;
  using difference_type = T::difference_type;
  static constexpr size_type npos = T::npos;

  //
  // Construction
  //

  // Safe construction.
  //
  // Always works.
  constexpr cstring_view() noexcept {}
  constexpr cstring_view(std::nullptr_t) {}

  constexpr cstring_view(const cstring_view&) = default;
  constexpr cstring_view(const std::string& s) noexcept : sv_(s) {}
  constexpr cstring_view(const char* psz) noexcept : sv_(from_ptr(psz)) {}

  // Risky construction.
  //
  // To demonstrate that it's actually terminated, the input must extend so
  // that the last character is a terminator. Otherwise, this is a logic error
  // and we throw.
  constexpr explicit cstring_view(std::string_view sv) : sv_(from_sv(sv)) {}
  constexpr explicit cstring_view(const char* ps, size_type len)
      : sv_(from_sv(std::string_view{ps, len})) {}

  // Optional as null.
  template<typename T>
  constexpr cstring_view(const std::optional<T>& opt)
      : cstring_view(opt.has_value() ? cstring_view{*opt} : cstring_view{}) {}

  //
  // Passthrough
  //

  constexpr cstring_view& operator=(const cstring_view& csv) noexcept {
    sv_ = csv.sv_;
    return *this;
  }

  constexpr auto begin() const noexcept { return sv_.begin(); };
  constexpr auto end() const noexcept { return sv_.end(); };
  constexpr auto cbegin() const noexcept { return sv_.cbegin(); };
  constexpr auto cend() const noexcept { return sv_.cend(); };

  constexpr auto rbegin() const noexcept { return sv_.rbegin(); };
  constexpr auto rend() const noexcept { return sv_.rend(); };
  constexpr auto crbegin() const noexcept { return sv_.crbegin(); };
  constexpr auto crend() const noexcept { return sv_.crend(); };

  constexpr auto& operator[](size_type pos) const { return sv_[pos]; };
  constexpr auto& at(size_type pos) const { return sv_.at(pos); }
  constexpr auto& front() const { return sv_.front(); }
  constexpr auto& back() const { return sv_.back(); }
  constexpr auto data() const noexcept { return sv_.data(); }

  constexpr auto size() const noexcept { return sv_.size(); }
  constexpr auto length() const noexcept { return sv_.length(); }
  constexpr auto max_size() const noexcept { return sv_.max_size(); }

  [[nodiscard]] constexpr bool empty() const noexcept { return sv_.empty(); }
  constexpr void remove_prefix(size_type n) { sv_.remove_prefix(n); }
  constexpr void swap(cstring_view& csv) noexcept { sv_.swap(csv.sv_); }
  constexpr auto copy(CharT* dest, size_type count, size_type pos = 0) const {
    return sv_.copy(dest, count, pos);
  }

  template<typename... Args>
  constexpr auto compare(Args&&... args) const noexcept {
    return sv_.compare(std::forward<Args>(args)...);
  }

  template<typename... Args>
  constexpr auto find(Args&&... args) const noexcept {
    return sv_.find(std::forward<Args>(args)...);
  }

  template<typename... Args>
  constexpr auto rfind(Args&&... args) const noexcept {
    return sv_.rfind(std::forward<Args>(args)...);
  }

  template<typename... Args>
  constexpr auto find_first_of(Args&&... args) const noexcept {
    return sv_.find_first_of(std::forward<Args>(args)...);
  }

  template<typename... Args>
  constexpr auto find_last_of(Args&&... args) const noexcept {
    return sv_.find_last_of(std::forward<Args>(args)...);
  }

  template<typename... Args>
  constexpr auto find_first_not_of(Args&&... args) const noexcept {
    return sv_.find_first_not_of(std::forward<Args>(args)...);
  }

  template<typename... Args>
  constexpr auto find_last_not_of(Args&&... args) const noexcept {
    return sv_.find_last_not_of(std::forward<Args>(args)...);
  }

  friend auto constexpr
  operator<=>(const cstring_view&, const cstring_view&) noexcept = default;

  friend auto constexpr
  operator<=>(const std::string_view& lv, const cstring_view& r) noexcept {
    return lv <=> r.view();
  };

  friend auto constexpr
  operator<=>(const cstring_view& l, const std::string_view& rv) noexcept {
    return l.view() <=> rv;
  };

  friend std::ostream& operator<<(std::ostream& os, cstring_view csv) {
    return os << csv.sv_;
  }

  //
  // Omitted
  //

  // `remove_suffix` would break the termination invariant.

  // `substr` would likewise do so if `count` isn't `npos` or `size()`.

  //
  // New
  //

  // Whether `data` is `nullptr`.
  constexpr bool null() const noexcept { return !sv_.data(); }

  // Conversion to `std::string_view`.
  constexpr std::string_view view() const noexcept { return sv_; }
  constexpr operator std::string_view() const noexcept { return sv_; }

  // Pointer to terminated string; never `nullptr`.
  constexpr const_pointer c_str() const noexcept {
    return data() ? data() : "";
  }

  // Quirky but convenient Boolean.
  constexpr explicit operator bool() const noexcept { return size(); }

  // Essentially `operator===`, distinguishing between empty and null.
  constexpr bool same(cstring_view v) const noexcept {
    return ((*this == v) && (null() == v.null()));
  }

private:
  T sv_;

  static constexpr T from_ptr(const char* psz) { return psz ? T{psz} : T{}; }

  static constexpr T from_sv(T sv) {
    // Empty is allowed, but only when null. A non-null empty must include the
    // terminator in its length.
    if (sv.empty()) {
      if (sv.data()) throw std::length_error("c_string_view len");
      return sv;
    }

    // Ensure terminator is there, and then exclude it from length.
    if (sv.back()) throw std::invalid_argument("c_string_view arg");
    sv.remove_suffix(1);
    return sv;
  }
};

} // namespace cstringview
namespace literals {

//
// UDL
//

// cstring_view literal.
constexpr cstring_view operator""_csv(const char* ps, std::size_t n) noexcept {
  return cstring_view{std::string_view{ps, n + 1}};
}

// Null literal; must pass 0.
constexpr cstring_view operator""_csv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("c_string_view not zero");
  return cstring_view{};
}

// Environment.
cstring_view operator""_env(const char* ps, std::size_t) noexcept {
  return std::getenv(ps);
}

} // namespace literals
} // namespace corvid

template<>
struct std::hash<corvid::cstring_view> {
  std::size_t operator()(const corvid::cstring_view& csv) const noexcept {
    return std::hash<std::string_view>()(csv.view());
  }
};