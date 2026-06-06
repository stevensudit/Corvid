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
#include "string_view_wrapper.h"
#include "strings_shared.h"
#include "targeting.h"

namespace corvid::strings { inline namespace delimiting {

#pragma region delim

// Storage backing the default single-space delimiter, one per code unit.
template<CharType CharT>
inline constexpr CharT delim_space = CharT(' ');

// Delimiter wrapper.
//
// This class is not intended for standalone use. While it provides some
// utility, it is very limited and internal. The only reason it's externally
// visible at all is to document delimiter parameters with a distinct type.
//
// The precise semantics vary depending upon context:
// - When splitting, checks for any of the characters.
// - When joining, appends the entire string.
// - When manipulating braces, treated as an open/close pair.
template<CharType CharT = char>
struct basic_delim: public string_view_wrapper<basic_delim<CharT>, CharT> {
  using base = string_view_wrapper<basic_delim<CharT>, CharT>;
  using view_t = base::view_t;
  using char_t = base::char_t;

#pragma region Construction

  // The default delimiter is a single space.
  constexpr basic_delim() noexcept : base{view_t{&delim_space<char_t>, 1}} {}

  // Implicit construction from any view, so string literals, strings, and
  // other view wrappers all pass through transparently. Raw pointers route
  // through the base's null-safe constructor.
  constexpr basic_delim(view_t list) noexcept : base{list} {}
  constexpr basic_delim(const char_t* psz) : base{psz} {}

#pragma endregion Construction
#pragma region Locating

  [[nodiscard]] constexpr auto find_in(view_t whole) const {
    if (this->size() == 1) return whole.find(this->front());
    return whole.find_first_of(*this);
  }

  [[nodiscard]] constexpr auto find_not_in(view_t whole) const {
    if (this->size() == 1) return whole.find_first_not_of(this->front());
    return whole.find_first_not_of(*this);
  }

  [[nodiscard]] constexpr auto find_last_not_in(view_t whole) const {
    if (this->size() == 1) return whole.find_last_not_of(this->front());
    return whole.find_last_not_of(*this);
  }

#pragma endregion Locating
#pragma region Append

  // Append.
  constexpr auto& append(BasicAppendTarget<char_t> auto& target) const {
    appender{target}.append(*this);
    return target;
  }

  // Append after the first time.
  //
  // Caller must set `first` initially. Then, on the first call, `first` will
  // be cleared, but nothing will be appended. On subsequent calls `first` will
  // remain cleared, so the delimiter will be appended.
  constexpr auto& append_skip_first(BasicAppendTarget<char_t> auto& target,
      bool& first) const {
    if (!first)
      append(target);
    else
      first = false;
    return target;
  }

  // Append when `emit`.
  template<bool emit = true>
  constexpr auto& append_if(BasicAppendTarget<char_t> auto& target) const {
    if constexpr (emit) append(target);
    return target;
  }

#pragma endregion Append
};

// The default delimiter type, over `char`.
using delim = basic_delim<char>;

#pragma endregion delim

}} // namespace corvid::strings::delimiting
