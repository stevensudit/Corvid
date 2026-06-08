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
#include <optional>
#include <string>
#include <string_view>

#include "../meta/concepts.h"
#include "../meta/formatting.h"
#include "../strings/targeting.h"
#include "enum_conversion.h"
#include "sequence_enum.h"
#include "bitmask_enum.h"
#include "../strings/debug_escaping.h"

#pragma region enum formatter

// Formatter for registered Corvid enums (sequence or bitmask), narrow or wide.
//
// Deliberately constrained to enums that opt into the registry rather than to
// every scoped enum: [namespace.std] only permits specializing
// `std::formatter` on a program-defined type, so a blanket scoped-enum
// specialization would also match std scoped enums such as `std::byte` and
// `std::errc`, which is undefined behavior and can collide with another
// library's enum formatter. Unregistered scoped enums therefore have no
// formatter.
//
// Writes the value into the format context's output iterator through Corvid's
// enum string conversion. A sequence enum prints its name, or its decimal
// underlying value when the value is unnamed; a bitmask enum prints its named
// bits as "a + b + c", with any leftover bits in hex. This numeric fallback is
// the plain default rendering of the number: the spec here cannot give it an
// alternate base, a sign, or zero padding. To format the number that way,
// format the underlying value via `operator*`, e.g. `{:#06x}` on `*e`.
//
// Enum names are stored as char; for a wide CharT the per-unit widening
// happens in the output target (see
// corvid::strings::output_iterator_appendable). Corvid names are 7-bit ASCII,
// so the widening is a direct per-unit conversion.
//
// Supports the parts of the standard spec grammar that apply to a name: fill
// and align, width, precision, and the `?` debug spec. Debug quotes and
// escapes the rendering with the standard's debug rules so an enum composes
// with the std range and map formatters: `{::?}` quotes each element, and
// `set_debug_format` lets a `map<int, E>` print `{1: "red"}`. With neither
// width nor precision, the rendering streams straight through with no
// intermediate string. Width or precision needs the rendered length, so a
// named sequence value pads its stored view in place while a bitmask
// combination, an unnamed value's numeric form, or the escaped debug spec
// renders into a string first. Sign, `#`, zero-pad, locale, and any non-`?`
// type do not apply to a name and are rejected.
template<typename E, corvid::CharType CharT>
requires(corvid::enums::sequence::SequentialEnum<E> ||
         corvid::enums::bitmask::BitmaskEnum<E>)
struct std::formatter<E, CharT> {
#pragma region Parse

  constexpr void set_debug_format() { spec_.debug = true; }

  constexpr auto parse(std::basic_format_parse_context<CharT>& ctx) {
    const auto begin = ctx.begin();
    const auto spec_text = std::basic_string_view<CharT>{begin,
        static_cast<std::size_t>(ctx.end() - begin)};
    const auto consumed = spec_.parse(spec_text);
    validate_spec();

    // An automatic `{}` width or precision has no id in the spec string, so
    // claim one from the parse context now.
    if (spec_.width_arg.is_automatic())
      spec_.width_arg.value = ctx.next_arg_id();
    if (spec_.precision_arg.is_automatic())
      spec_.precision_arg.value = ctx.next_arg_id();

    // Stop at the spec-terminating `}`.
    return begin + consumed;
  }

#pragma endregion
#pragma region Format

  template<typename FormatContext>
  auto format(E e, FormatContext& ctx) const {
    std::size_t width = spec_.width;
    if (spec_.width_arg.is_dynamic()) width = spec_.width_arg.get_dynamic(ctx);
    std::optional<std::size_t> prec = spec_.precision;
    if (spec_.precision_arg.is_dynamic())
      prec = spec_.precision_arg.get_dynamic(ctx);

    // Fast path: with neither width nor precision, there is nothing to measure
    // the rendering against, so stream it straight into the output with no
    // intermediate string.
    if (!width && !prec) return render<CharT>(e, ctx.out());

    // A named sequence value is already stored as a view, so trim and pad it
    // in place with no buffer.
    if constexpr (corvid::enums::sequence::SequentialEnum<E>) {
      if (!spec_.debug) {
        const std::string_view name = corvid::enums::sequence::enum_as_view(e);
        if (!name.empty()) return write_field(ctx.out(), name, prec, width);
      }
    }

    // A bitmask combination, an unnamed value's numeric form, and the escaped
    // debug spec are produced on the fly, so render into a narrow ASCII buffer
    // (names and debug escapes are 7-bit) before trimming and padding.
    std::string buffer;
    render<char>(e, std::back_inserter(buffer));
    return write_field(ctx.out(), buffer, prec, width);
  }

#pragma endregion
#pragma region Helpers
private:
  // Render `e` into `out`: the plain name, or the quoted and escaped name for
  // the debug spec. Each char is widened to `RenderCharT`.
  template<corvid::CharType RenderCharT, typename OutIt>
  OutIt render(E e, OutIt out) const {
    using namespace corvid::strings;
    if (!spec_.debug) {
      output_iterator_appendable<OutIt, char, RenderCharT> target{out};
      append_enum(target, e);
      return target.out;
    }
    *out++ = RenderCharT('"');
    debug_escaping_appendable<OutIt, RenderCharT> target{out};
    append_enum(target, e);
    out = target.out;
    *out++ = RenderCharT('"');
    return out;
  }

  // Trim `content` to precision and pad it to width, widening to `CharT`.
  template<typename OutIt>
  OutIt write_field(OutIt out, std::string_view content,
      std::optional<std::size_t> prec, std::size_t width) const {
    if (prec) content = content.substr(0, *prec);
    return spec_.write_padded(out, content, width);
  }

  // Reject spec fields that do not apply to a name: sign, `#`, zero-pad,
  // locale, and any type other than the `?` debug spec.
  constexpr void validate_spec() const {
    if (spec_.sign != '-' || spec_.alternate || spec_.zero_pad ||
        spec_.has_locale ||
        (spec_.type != CharT(0) && spec_.type != CharT('?')))
      throw std::format_error(
          "enum format spec accepts only fill and align, "
          "width, precision, and '?'");
  }

#pragma endregion
#pragma region Data members

  corvid::spec_parser<CharT> spec_;

#pragma endregion
};

#pragma endregion
