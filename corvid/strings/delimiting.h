// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include "strings_shared.h"
#include "targeting.h"

namespace corvid::strings { inline namespace delimiting {

//
// Delimiter
//

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
struct delim: public std::string_view {
  constexpr delim() : delim(" "sv) {}

  template<typename T>
  constexpr delim(T&& list) : std::string_view(std::forward<T>(list)) {}

  [[nodiscard]] constexpr auto find_in(std::string_view whole) const {
    if (size() == 1) return whole.find(front());
    return whole.find_first_of(*this);
  }

  [[nodiscard]] constexpr auto find_not_in(std::string_view whole) const {
    if (size() == 1) return whole.find_first_not_of(front());
    return whole.find_first_not_of(*this);
  }

  [[nodiscard]] constexpr auto find_last_not_in(std::string_view whole) const {
    if (size() == 1) return whole.find_last_not_of(front());
    return whole.find_last_not_of(*this);
  }

  // Append.
  constexpr auto& append(AppendTarget auto& target) const {
    appender{target}.append(*this);
    return target;
  }

  // Append after the first time.
  //
  // Caller must set `first` initially. Then, on the first call, `first` will
  // be cleared, but nothing will be appended. On subsequent calls `first` will
  // remain cleared, so the delimiter will be appended.
  constexpr auto&
  append_skip_first(AppendTarget auto& target, bool& first) const {
    if (!first)
      append(target);
    else
      first = false;
    return target;
  }

  // Append when `emit`.
  template<bool emit = true>
  constexpr auto& append_if(AppendTarget auto& target) const {
    if constexpr (emit) append(target);
    return target;
  }
};

}} // namespace corvid::strings::delimiting
