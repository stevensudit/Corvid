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

#include "../../meta/concepts.h"
#include "../core/targeting.h"
#include "../utils/enum_conversion.h"

// Formatter for any scoped enum, narrow or wide.
//
// Writes the value straight into the format context's output iterator through
// Corvid's enum string conversion, so all three flavors are covered by the
// existing registry with no intermediate string: a registered sequence enum
// prints by name, a registered bitmask enum prints as its "a + b + c"
// combination, and an unregistered scoped enum prints as its numeric
// underlying value.
//
// Enum names are stored as char; for a wide CharT the per-unit widening
// happens in the output target (see
// corvid::strings::output_iterator_appendable). Corvid names are 7-bit ASCII,
// so the widening is a direct per-unit conversion.
//
// Only the default (empty) format spec is accepted: there is no fill, align,
// width, or precision.
template<corvid::ScopedEnum E, typename CharT>
struct std::formatter<E, CharT> {
  constexpr auto parse(auto& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(E e, FormatContext& ctx) const {
    corvid::strings::output_iterator_appendable<
        typename FormatContext::iterator, char, CharT>
        target{ctx.out()};
    corvid::strings::append_enum(target, e);
    return target.out;
  }
};
