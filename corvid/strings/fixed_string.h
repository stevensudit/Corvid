// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
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
#include <string>
#include <string_view>

#ifndef CORVID_AVOID_CSTRINGVIEW
#include "cstring_view.h"
#endif

namespace corvid::strings { inline namespace fixed {

// Fixed string, suitable for use as a non-type template parameter.
//
// While I didn't know it at the start, it turns out that, much like
// `cstring_view`, this class likely owes its existence to a dropped ANSI
// committee proposal by Andrew Tomazos (and Michael Price).
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0259r0.pdf
template<unsigned N>
struct fixed_string {
  // Construct from a string literal. Requires `N` to be specified by the
  // deduction guide.
  constexpr fixed_string(char const* s) {
    for (size_t i = 0; i != N; ++i) do_not_use[i] = s[i];
  }

  // Conversions.
  constexpr const char* c_str() const { return do_not_use; }
  constexpr std::string_view view() const { return {do_not_use, N}; }
  constexpr operator std::string_view() const { return view(); }

#ifndef CORVID_AVOID_CSTRINGVIEW
  // A fixed string is necessarily terminated.
  constexpr cstring_view cview() const {
    return cstring_view{do_not_use, N + 1};
  }
#endif

  // Some compilers might still need this.
  constexpr auto operator<=>(const fixed_string&) const = default;

  // This can't be made private or const, but do not ever use it.
  char do_not_use[N + 1]{};
};

// Deduction guide for fixed_string from string literal.
template<unsigned N>
fixed_string(char const (&)[N]) -> fixed_string<N - 1>;

}} // namespace corvid::strings::fixed
