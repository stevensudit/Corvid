// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
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
// For std::getenv, which is deprecated.
#define _CRT_SECURE_NO_WARNINGS 1
#include <cstdlib>

#include <iostream>
#include <string>
#include <string_view>
#include <optional>
#include <stdexcept>

namespace corvid {
inline namespace cstringview {

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
// literal using the `_csv` UDL.
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
// This revanchist implementation is based closely on Andrew Tomazos'
// wrongly-rejected ANSI committee proposal.
// http://open-std.org/JTC1/SC22/WG21/docs/papers/2019/p1402r0.pdf
// https://github.com/cplusplus/papers/issues/189
template<typename T = std::string_view>
class basic_cstring_view {
public:
  using SV = T;
  using CharT = SV::value_type;
  using traits_type = SV::traits_type;
  using value_type = SV::value_type;
  using pointer = SV::pointer;
  using const_pointer = SV::const_pointer;
  using reference = SV::reference;
  using const_reference = SV::const_reference;
  using const_iterator = SV::const_iterator;
  using iterator = SV::iterator;
  using const_reverse_iterator = SV::const_reverse_iterator;
  using reverse_iterator = SV::reverse_iterator;
  using size_type = SV::size_type;
  using difference_type = SV::difference_type;
  static constexpr size_type npos = SV::npos;

  //
  // Construction
  //

  // Safe construction.
  //
  // Always works.
  constexpr basic_cstring_view() noexcept {}
  constexpr basic_cstring_view(std::nullptr_t) noexcept {}
  constexpr basic_cstring_view(std::nullopt_t) noexcept {}

  constexpr basic_cstring_view(const basic_cstring_view&) noexcept = default;
  constexpr basic_cstring_view(const std::string& s) noexcept : sv_{s} {}
  constexpr basic_cstring_view(const char* psz) : sv_{from_ptr(psz)} {}

  // Risky construction.
  //
  // To demonstrate that it's actually terminated, the input must extend so
  // that the last character is a terminator. Otherwise, this is a logic error
  // and we throw.
  constexpr explicit basic_cstring_view(SV sv) : sv_{from_sv(sv)} {}
  constexpr explicit basic_cstring_view(const CharT* ps, size_type len)
      : sv_{from_sv(std::string_view{ps, len})} {}
  template<std::contiguous_iterator It, std::sized_sentinel_for<It> End>
  requires std::same_as<std::iter_value_t<It>, char> &&
           (!std::convertible_to<End, size_type>)
  constexpr explicit basic_cstring_view(It first, End last)
      : basic_cstring_view{std::to_address(first), size_type(last - first)} {}

  // Optional as null.
  template<typename U>
  requires std::is_constructible_v<SV, U>
  constexpr basic_cstring_view(const std::optional<U>& opt)
      : basic_cstring_view{opt.has_value() ? basic_cstring_view{*opt}
                                           : basic_cstring_view{}} {}

  //
  // Passthrough
  //

  constexpr basic_cstring_view& operator=(
      const basic_cstring_view& csv) noexcept {
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
  constexpr void swap(basic_cstring_view& csv) noexcept { sv_.swap(csv.sv_); }
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

  friend auto constexpr operator<=>(const basic_cstring_view&,
      const basic_cstring_view&) noexcept = default;

  friend auto constexpr operator<=>(const std::string_view& lv,
      const basic_cstring_view& r) noexcept {
    return lv <=> r.view();
  };

  friend auto constexpr operator<=>(const basic_cstring_view& l,
      const std::string_view& rv) noexcept {
    return l.view() <=> rv;
  };

  friend std::ostream& operator<<(std::ostream& os, basic_cstring_view csv) {
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
    return data() ? data() : reinterpret_cast<const_pointer>(U"");
  }

  // The precedent for this is `std::optional`.
  constexpr explicit operator bool() const noexcept { return !null(); }

  // Essentially `operator===`, distinguishing between empty and null.
  constexpr bool same(basic_cstring_view v) const noexcept {
    return ((*this == v) && (null() == v.null()));
  }

private:
  SV sv_;

  static constexpr SV from_ptr(const char* psz) {
    return psz ? SV{psz} : SV{};
  }

  static constexpr SV from_sv(SV sv) {
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
};

// Specialized aliases.
using cstring_view = basic_cstring_view<std::string_view>;
using wcstring_view = basic_cstring_view<std::wstring_view>;
using u8cstring_view = basic_cstring_view<std::u8string_view>;
using u16cstring_view = basic_cstring_view<std::u16string_view>;
using u32cstring_view = basic_cstring_view<std::u32string_view>;
} // namespace cstringview
namespace literals {

//
// UDL
//

// basic_cstring_view literals.
constexpr cstring_view operator""_csv(const char* ps, std::size_t n) noexcept {
  return cstring_view{std::string_view{ps, n + 1}};
}
constexpr wcstring_view
operator""_wcsv(const wchar_t* ps, std::size_t n) noexcept {
  return wcstring_view{std::wstring_view{ps, n + 1}};
}
constexpr u8cstring_view
operator""_u8csv(const char8_t* ps, std::size_t n) noexcept {
  return u8cstring_view{std::u8string_view{ps, n + 1}};
}
constexpr u16cstring_view
operator""_u16csv(const char16_t* ps, std::size_t n) noexcept {
  return u16cstring_view{std::u16string_view{ps, n + 1}};
}
constexpr u32cstring_view
operator""_u32csv(const char32_t* ps, std::size_t n) noexcept {
  return u32cstring_view{std::u32string_view{ps, n + 1}};
}

// Null literal; must pass 0.
constexpr cstring_view operator""_csv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("cstring_view not zero");
  return cstring_view{};
}
constexpr wcstring_view operator""_wcsv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("wcstring_view not zero");
  return wcstring_view{};
}
constexpr u8cstring_view operator""_u8csv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("u8cstring_view not zero");
  return u8cstring_view{};
}
constexpr u16cstring_view operator""_u16csv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("u16cstring_view not zero");
  return u16cstring_view{};
}
constexpr u32cstring_view operator""_u32csv(unsigned long long zero_only) {
  if (zero_only) throw std::out_of_range("u32cstring_view not zero");
  return u32cstring_view{};
}

// Environment.
cstring_view operator""_env(const char* ps, std::size_t) noexcept {
  return std::getenv(ps);
}

} // namespace literals
} // namespace corvid
