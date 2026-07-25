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
#include <cstddef>
#include <string>

#include "../meta/concepts.h"
#include "string_literals.h"

namespace corvid::strings { inline namespace tab_expansion {

// Expand tabs
//
// The counterpart of Python's `str.expandtabs`. Like the rest of the band, it
// works on any code-unit type, with deliberately ASCII-only semantics.
//
// For related functionality, see "textwrap.h".

#pragma region Expand tabs

// Return a copy of `s` with each tab replaced by spaces, Python
// `expandtabs`-style.
//
// A tab advances the column to the next multiple of `tab_size`, so how many
// spaces it becomes depends on where it falls; every other code unit occupies
// one column. The column resets to zero after a newline or carriage return.
// When `tab_size` is zero, tabs are deleted.
template<StringViewLike S>
[[nodiscard]] constexpr auto expand_tabs(const S& s, size_t tab_size = 8) {
  using C = char_type_of_t<S>;
  const auto sv{as_view(s)};
  std::basic_string<C> r;
  r.reserve(sv.size());
  size_t col{};
  for (const auto c : sv) {
    if (c == C('\t')) {
      if (tab_size) {
        const auto fill = tab_size - (col % tab_size);
        r.append(fill, C(' '));
        col += fill;
      }
    } else {
      r.push_back(c);
      col = (c == C('\n') || c == C('\r')) ? 0 : col + 1;
    }
  }
  return r;
}

#pragma endregion

}} // namespace corvid::strings::tab_expansion
