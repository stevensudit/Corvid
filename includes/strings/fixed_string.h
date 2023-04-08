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

namespace corvid::strings {
inline namespace fixed {

// Fixed string, suitable for use as a non-type template parameter.
template<unsigned N>
struct fixed_string {
  // Construct from a string literal. Requires `N` to be specified by the
  // deduction guide.
  constexpr fixed_string(char const* s) {
    for (size_t i = 0; i != N; ++i) buf[i] = s[i];
  }

  // Conversions.
  constexpr const char* c_str() const { return buf; }
  constexpr std::string_view view() const { return {buf, N}; }
  constexpr operator std::string_view() const { return view(); }

  // TODO: Consider being dependent upon cstring_view so that we can convert
  // natively to it.

  // Some compilers might still need this.
  constexpr auto operator<=>(const fixed_string&) const = default;

  char buf[N + 1]{};
};

// Deduction guide for fixed_string from string literal.
template<unsigned N>
fixed_string(char const (&)[N]) -> fixed_string<N - 1>;

// Split fixed string by delimiter, returning array of string views.
template<strings::fixed_string S, char delim = ','>
consteval auto fixed_split() {
  std::array<std::string_view,
      std::count(S.view().begin(), S.view().end(), delim) + 1>
      result;

  auto s = S.view();
  for (size_t i = 0; i != result.size(); ++i) {
    auto pos = s.find(delim);
    result[i] = s.substr(0, pos);
    s = s.substr(pos + 1);
  }

  return result;
}

// TODO: Consider writing a version of split that also does a search/replace in
// parallel, so as to be able to remove placeholders while still letting them
// count. Or, even better, trim each piece. This lets a space be used, quite
// intuitively, as a placeholder. The trimming version could be controlled by a
// flag in the template list.

} // namespace fixed
} // namespace corvid::strings
