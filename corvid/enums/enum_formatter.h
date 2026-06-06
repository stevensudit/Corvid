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

#include "../meta/concepts.h"
#include "../strings/targeting.h"
#include "enum_conversion.h"
#include "../strings/debug_escaping.h"

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
// Supports the empty spec (the plain rendering above) and the `?` debug spec,
// which quotes and escapes the rendering with the standard's debug rules so an
// enum composes with the std range and map formatters: `{::?}` quotes each
// element, and `set_debug_format` lets a `map<int, E>` print `{1: "red"}`.
// There is no fill, align, width, or precision; those would need a
// materialized string to pad.
template<corvid::ScopedEnum E, corvid::CharType CharT>
struct std::formatter<E, CharT> {
  constexpr void set_debug_format() { debug_ = true; }

  constexpr auto parse(auto& ctx) {
    auto it = ctx.begin();
    if (it != ctx.end() && *it == '?') {
      debug_ = true;
      ++it;
    }
    if (it != ctx.end() && *it != '}')
      throw std::format_error("enum format spec accepts only '?'");
    return it;
  }

  template<typename FormatContext>
  auto format(E e, FormatContext& ctx) const {
    using namespace corvid::strings;
    using It = FormatContext::iterator;
    if (!debug_) {
      output_iterator_appendable<It, char, CharT> target{ctx.out()};
      append_enum(target, e);
      return target.out;
    }
    auto out = ctx.out();
    *out++ = CharT('"');
    debug_escaping_appendable<It, CharT> target{out};
    append_enum(target, e);
    out = target.out;
    *out++ = CharT('"');
    return out;
  }

private:
  bool debug_{false};
};
