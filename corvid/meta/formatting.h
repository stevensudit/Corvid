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
#include <cstdint>
#include <format>

#include "concepts.h"

// Reusable `std::formatter` bases for the recurring shapes in this library:
// forwarding a wrapper to its underlying value's formatter, the same with a
// null state, and delegating a custom rendering to a `format_to` member. Each
// reduces a per-type `std::formatter` specialization to a one-line `: base
// {}`.
namespace corvid { inline namespace meta { inline namespace formatting {

namespace specs {

enum class aligned : std::uint8_t { left, right, center };

template<CharType CharT>
struct parsed_spec {
  std::size_t width = 0;
  std::optional<std::size_t> precision; // Meaningful for numerics.
  bool debug = false; // May be set externally by `set_debug_format`.
  CharT fill = CharT(' ');
  aligned align = aligned::left;
  char sign = '-';        // `-` default, `+` always, ` ` space for positive.
  bool alternate = false; // `#`
  bool zero_pad = false;
  bool has_locale = false;
  CharT type = CharT(0); // Debug is '?'.
  bool dynamic = false;  // Either dynamic width or precision was attempted.
};

template<CharType CharT>
constexpr bool is_align(CharT c) {
  return c == CharT('<') || c == CharT('>') || c == CharT('^');
}

template<CharType CharT>
constexpr aligned to_aligned(CharT c) {
  if (c == CharT('>')) return aligned::right;
  if (c == CharT('^')) return aligned::center;
  return aligned::left;
}

// Read a count (a width or precision) at `ndx`, advancing it
template<CharType CharT>
constexpr std::optional<std::size_t>
read_count(std::basic_string_view<CharT> spec, std::size_t& ndx) {
  const std::size_t n = spec.size();
  if (ndx < n && spec[ndx] == CharT('{')) {
    while (ndx < n && spec[ndx] != CharT('}')) ++ndx;
    if (ndx < n) ++ndx;
    return std::nullopt;
  }
  std::size_t value = 0;
  for (; ndx < n && spec[ndx] >= CharT('0') && spec[ndx] <= CharT('9'); ++ndx)
    value = (value * 10) + static_cast<std::size_t>(spec[ndx] - CharT('0'));
  return value;
}

// Parse the standard format spec into a `parsed_spec`: fill, align, sign, the
// `#` alternate flag, zero-pad, width, precision, the `L` locale flag, and the
// type char, plus the derived `debug` (a `?` type) and `dynamic` (a `{}` width
// or precision). A dynamic width or precision is only flagged; its value lives
// in the format args, unreadable from a `string_view`.
template<CharType CharT>
constexpr auto parse_pad(std::basic_string_view<CharT> spec) {
  parsed_spec<CharT> result{};
  std::size_t ndx = 0;
  const std::size_t cnt = spec.size();
  // [fill] align
  if (cnt >= 2 && is_align(spec[1])) {
    result.fill = spec[0];
    result.align = to_aligned(spec[1]);
    ndx = 2;
  } else if (cnt >= 1 && is_align(spec[0])) {
    result.align = to_aligned(spec[0]);
    ndx = 1;
  }
  // sign
  if (ndx < cnt &&
      (spec[ndx] == CharT('+') || spec[ndx] == CharT('-') ||
          spec[ndx] == CharT(' ')))
  {
    result.sign = static_cast<char>(spec[ndx]);
    ++ndx;
  }
  // `#` alternate
  if (ndx < cnt && spec[ndx] == CharT('#')) {
    result.alternate = true;
    ++ndx;
  }
  // `0` zero-pad
  if (ndx < cnt && spec[ndx] == CharT('0')) {
    result.zero_pad = true;
    ++ndx;
  }
  // width
  if (const auto width = read_count(spec, ndx)) result.width = *width;
  // `.` precision
  if (ndx < cnt && spec[ndx] == CharT('.')) {
    ++ndx;
    result.precision = read_count(spec, ndx);
  }
  // `L` locale
  if (ndx < cnt && spec[ndx] == CharT('L')) {
    result.has_locale = true;
    ++ndx;
  }
  // type: the one remaining char, if any
  if (ndx < cnt) result.type = spec[ndx];

  // The `?` type is debug. Set-only: `set_debug_format` may have turned debug
  // on externally even when the spec carries no `?`.
  if (result.type == CharT('?')) result.debug = true;

  // A `{` flags a dynamic width or precision (value unreadable here).
  if (spec.find(CharT('{')) != spec.npos) result.dynamic = true;

  return result;
}

template<CharType CharT, CharType InCharT, typename OutIt>
constexpr OutIt write_repeat(OutIt out, InCharT c, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i)
    *out++ = static_cast<CharT>(static_cast<wchar_t>(c));
  return out;
}

template<CharType CharT, CharType InCharT, typename OutIt>
constexpr OutIt write_sv(OutIt out, std::basic_string_view<InCharT> sv) {
  for (const auto c : sv) *out++ = static_cast<CharT>(static_cast<wchar_t>(c));
  return out;
}

constexpr std::pair<size_t, size_t> calc_padding(aligned align,
    std::size_t content_width, std::size_t total_width) {
  std::size_t lead = 0;
  std::size_t trail = 0;
  if (total_width > content_width) {
    const std::size_t pad = total_width - content_width;
    lead =
        align == aligned::right ? pad
        : align == aligned::center
            ? pad / 2
            : 0;
    trail = pad - lead;
  }
  return {lead, trail};
}

// Emit `content` (narrow, 7-bit ASCII) widened to `CharT`, padded to `width`
// with `fill` per `align`. A width no larger than the content is a no-op
// (width is a minimum). Used to render a `nullable_formatter`'s null sentinel.
template<CharType CharT, typename OutIt>
constexpr OutIt write_padded(OutIt out, std::string_view content, CharT fill,
    aligned align, std::size_t width) {
  auto [lead, trail] = calc_padding(align, content.size(), width);
  out = write_repeat<CharT>(out, fill, lead);
  out = write_sv<CharT>(out, content);
  out = write_repeat<CharT>(out, fill, trail);
  return out;
}

} // namespace specs

// Whether a null renders as an empty field or the text sentinel (default
// `(null)`). Selected independently for the plain and debug specs of
// `nullable_formatter`.
enum class null_formatting : bool { empty = false, sentinel = true };

// Base for a `std::formatter` on a wrapper that should format exactly like
// some underlying value of type `U`. It inherits the underlying type's
// formatter, so it reuses the full spec grammar (fill, align, width,
// precision, and the `?` debug spec), and forwards by converting the wrapper
// to `const U&`.
//
// A deriving specialization needs only `: forwarding_formatter<U, CharT> {}`.
// The wrapper type is deduced at format time and must be convertible to `U`
// (explicit conversion is fine, since the cast is explicit).
template<typename U, CharType CharT>
struct forwarding_formatter: std::formatter<U, CharT> {
  template<typename W, typename FormatContext>
  auto format(const W& w, FormatContext& ctx) const {
    return std::formatter<U, CharT>::format(static_cast<const U&>(w), ctx);
  }
};

// Base for a `std::formatter` on a nullable, pointer-like wrapper. It inherits
// the pointee/element type `U`'s formatter and forwards the dereferenced value
// with its full spec grammar; a null wrapper instead renders either an empty
// field or a text sentinel (default `(null)`).
//
// `PlainNull` and `DebugNull` independently select what a null shows under a
// plain spec and under the `?` debug spec. `null_formatting::sentinel` (the
// default for both) shows the sentinel, padded by the spec's fill/align/width;
// `null_formatting::empty` shows an empty field, rendered through the
// inherited formatter so a dynamic width still holds. A string wrapper, where
// null reads as empty, passes `..., null_formatting::empty>` so a plain null
// is empty while a debug null still shows the sentinel.
//
// A deriving specialization needs only `: nullable_formatter<U, CharT> {}`.
// The wrapper type is deduced at format time and must be contextually
// convertible to `bool` and dereferenceable to `U`. To customize the sentinel
// text, give the deriving formatter a default constructor that delegates to
// the marker constructor, e.g. a `file_handle` formatter: `constexpr
// formatter() : nullable_formatter<int, CharT>{"closed"} {}`.
//
// Only fill, align, and width reach the sentinel; it is a fixed string, so
// precision and type are meaningless for it, while the present value still
// honors the whole spec through the inherited formatter. A dynamic width or
// precision combined with the debug spec is rejected, since the sentinel's own
// padding cannot read an argument bound to the real format context.
template<typename U, CharType CharT,
    null_formatting PlainNull = null_formatting::sentinel,
    null_formatting DebugNull = null_formatting::sentinel>
struct nullable_formatter: std::formatter<U, CharT> {
  using base = std::formatter<U, CharT>;

  constexpr nullable_formatter() = default;
  constexpr explicit nullable_formatter(std::string_view marker)
      : marker_{marker} {}

  // Offered only when the underlying formatter has it, so a containing range
  // or map can enable element quoting; also records debug mode for the
  // sentinel.
  constexpr void set_debug_format()
  requires requires(base b) { b.set_debug_format(); }
  {
    spec_.debug = true;
    base::set_debug_format();
  }

  constexpr auto parse(std::basic_format_parse_context<CharT>& ctx) {
    const auto begin = ctx.begin();
    const auto it = base::parse(ctx);
    spec_ = specs::parse_pad(std::basic_string_view<CharT>{begin,
        static_cast<std::size_t>(it - begin)});
    if (spec_.debug && spec_.dynamic)
      throw std::format_error("dynamic is unsupported");
    return it;
  }

  template<typename W, typename FormatContext>
  auto format(const W& w, FormatContext& ctx) const {
    if (w) return base::format(*w, ctx);
    // A null shows the sentinel or an empty field per the spec mode. The
    // sentinel is padded by our own fill/align/width so the base's debug
    // quoting is bypassed; an empty field goes through the inherited formatter
    // so a dynamic width is still honored.
    if constexpr (PlainNull == null_formatting::sentinel &&
                  DebugNull == null_formatting::sentinel)
    {
      return pad_sentinel(ctx);
    } else {
      const null_formatting mode = spec_.debug ? DebugNull : PlainNull;
      return mode == null_formatting::sentinel
                 ? pad_sentinel(ctx)
                 : base::format(U{}, ctx);
    }
  }

private:
  template<typename FormatContext>
  auto pad_sentinel(FormatContext& ctx) const {
    return specs::write_padded<CharT>(ctx.out(), marker_, spec_.fill,
        spec_.align, spec_.width);
  }

  std::string_view marker_{"(null)"};
  specs::parsed_spec<CharT> spec_;
};

// Base for a `std::formatter` on a type that renders itself through a
// `format_to(out)` member, the modern analog of `operator<<`. A deriving
// specialization needs only `: format_to_formatter<CharT> {}`; the type is
// deduced at format time.
//
// Supports the empty spec (plain rendering) and the `?` debug spec. In debug
// mode it calls `debug_format_to(out)` when the type provides one, otherwise
// it falls back to `format_to` (a type without a debug rendering reads the
// same in either mode). `set_debug_format` lets the type auto-quote inside the
// std range and map formatters. Both `format_to` and `debug_format_to` must
// return the advanced output iterator.
//
// Fill, align, width, and precision are not supported: padding needs the
// rendered length, which would require materializing the rendering into a
// temporary. Add that here (render to a temporary, then pad through a
// `std::formatter<basic_string_view<CharT>>`) if a caller needs it; until then
// this streams straight to the output with no buffer.
template<CharType CharT>
struct format_to_formatter {
  constexpr void set_debug_format() { spec_.debug = true; }

  constexpr auto parse(std::basic_format_parse_context<CharT>& ctx) ->
      typename std::basic_format_parse_context<CharT>::iterator {
    const auto begin = ctx.begin();
    const auto it = ctx.end();
    spec_ = specs::parse_pad(std::basic_string_view<CharT>{begin,
        static_cast<std::size_t>(it - begin)});
    if (spec_.debug && spec_.dynamic)
      throw std::format_error("dynamic is unsupported");
    return it;
  }

  template<typename T, typename FormatContext>
  auto format(const T& obj, FormatContext& ctx) const {
    if constexpr (requires { obj.format_to_spec(spec_, ctx.out()); })
      return obj.format_to_spec(spec_, ctx.out());

    if (spec_.debug) {
      if constexpr (requires { obj.debug_format_to(ctx.out()); })
        return obj.debug_format_to(ctx.out());
    }
    return obj.format_to(ctx.out());
  }

private:
  specs::parsed_spec<CharT> spec_;
};

}}} // namespace corvid::meta::formatting
