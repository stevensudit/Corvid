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
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include "../../meta/concepts.h"
#include "../utils/enum_conversion.h"

// Formatter for any scoped enum, narrow or wide.
//
// Renders the value through Corvid's enum string conversion, so all three
// flavors are covered by the existing registry: a registered sequence enum
// prints by name, a registered bitmask enum prints as its "a + b + c"
// combination, and an unregistered scoped enum prints as its numeric
// underlying value. The standard string format spec (fill, align, width,
// precision) is honored, inherited from the string_view formatter.
//
// Enum names are stored as char; for a wide CharT they are widened at format
// time. Corvid names are 7-bit ASCII, so the widening is a direct per-unit
// conversion.
template<corvid::ScopedEnum E, typename CharT>
struct std::formatter<E, CharT>
    : std::formatter<std::basic_string_view<CharT>, CharT> {
  using base = std::formatter<std::basic_string_view<CharT>, CharT>;

  template<typename FormatContext>
  auto format(E e, FormatContext& ctx) const {
    const std::string narrow = corvid::strings::enum_as_string(e);
    if constexpr (std::is_same_v<CharT, char>) {
      return base::format(narrow, ctx);
    } else {
      const std::basic_string<CharT> wide(narrow.begin(), narrow.end());
      return base::format(wide, ctx);
    }
  }
};
