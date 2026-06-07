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
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <type_traits>

#include "concepts.h"

// Reusable `std::formatter` bases for the recurring shapes in this library:
// forwarding a wrapper to its underlying value's formatter, the same with a
// null state, and delegating a custom rendering to a `format_to` member. Each
// reduces a per-type `std::formatter` specialization to a one-line `: base
// {}`.
namespace corvid { inline namespace meta { inline namespace formatting {

#pragma region parsed_spec

// A parsed standard format spec: the individual fields, plus the operations to
// parse a spec string into them and to emit a padded field. The fields are
// public and the helpers are a few methods on top; this is a struct with
// benefits, not a tightly encapsulated value type.
template<CharType CharT>
struct parsed_spec {
#pragma region Types
  enum class aligned : std::uint8_t { left, right, center };

  enum class arg_kind : std::uint8_t { none, fixed, automatic, manual };

  // An argument value containing a width or precision field. This can be a
  // fixed value `10`, an auto `{}`, a manual `{n}`, or absent. `value` carries
  // the fixed value, the manual arg id, or the auto arg id once claimed from
  // the parse context.
  struct arg_value_t {
    arg_kind kind = arg_kind::none;
    std::size_t value = 0;

    // Read an arg value from `spec` at `ndx`, returning an index past the
    // consumed text.
    [[nodiscard]] constexpr std::size_t
    parse(std::basic_string_view<CharT> spec, std::size_t ndx) {
      *this = make_from_parse(spec, ndx);
      return ndx;
    }

    // Read an arg value (a width or precision) at `ndx`, advancing it. A
    // `{...}` is dynamic: empty is auto, digits are a manual arg id.
    [[nodiscard]] static constexpr arg_value_t
    make_from_parse(std::basic_string_view<CharT> spec, std::size_t& ndx) {
      const std::size_t n = spec.size();
      if (ndx < n && spec[ndx] == CharT('{')) {
        ++ndx;
        std::size_t id = 0;
        bool has_id = false;
        for (; ndx < n && spec[ndx] >= CharT('0') && spec[ndx] <= CharT('9');
            ++ndx)
        {
          id = (id * 10) + static_cast<std::size_t>(spec[ndx] - CharT('0'));
          has_id = true;
        }
        if (ndx < n && spec[ndx] == CharT('}')) ++ndx;
        return has_id ? arg_value_t{arg_kind::manual, id}
                      : arg_value_t{arg_kind::automatic, 0};
      }
      std::size_t value = 0;
      bool any = false;
      for (; ndx < n && spec[ndx] >= CharT('0') && spec[ndx] <= CharT('9');
          ++ndx)
      {
        value =
            (value * 10) + static_cast<std::size_t>(spec[ndx] - CharT('0'));
        any = true;
      }
      return any ? arg_value_t{arg_kind::fixed, value}
                 : arg_value_t{arg_kind::none, 0};
    }

    [[nodiscard]] bool is_dynamic() const {
      return kind == arg_kind::automatic || kind == arg_kind::manual;
    }

    [[nodiscard]] bool is_empty() const { return kind == arg_kind::none; }

    [[nodiscard]] bool is_automatic() const {
      return kind == arg_kind::automatic;
    }

    [[nodiscard]] std::optional<size_t> get_arg_id() const {
      if (kind != arg_kind::manual) return std::nullopt;
      return value;
    }

    [[nodiscard]] std::optional<size_t> get_fixed() const {
      if (kind != arg_kind::fixed) return std::nullopt;
      return value;
    }

    [[nodiscard]] std::optional<size_t> get_automatic() const {
      if (kind != arg_kind::automatic) return std::nullopt;
      return value;
    }

    template<typename FormatContext>
    [[nodiscard]] std::size_t get_dynamic(FormatContext& ctx) const {
      if (!is_dynamic()) return 0;
      return get_dynamic_num(ctx, value);
    }

    // Resolve a dynamic width or precision: arg `id` as a non-negative
    // integer.
    template<typename FormatContext>
    static std::size_t get_dynamic_num(FormatContext& ctx, std::size_t id) {
      return std::visit_format_arg(
          [](auto value) -> std::size_t {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
              if constexpr (std::is_signed_v<T>)
                if (value < 0) throw std::format_error("negative arg");
              return static_cast<std::size_t>(value);
            } else
              throw std::format_error("arg is not an integer");
          },
          ctx.arg(id));
    }
  };

#pragma endregion
#pragma region Fields

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

  arg_value_t width_arg;
  arg_value_t precision_arg;

#pragma endregion
#pragma region Operations

  // Whether any width or precision is dynamic.
  [[nodiscard]] constexpr bool is_dynamic() const {
    return width_arg.is_dynamic() || precision_arg.is_dynamic();
  }

  // Parse the standard format spec into this instance, stopping at the closing
  // `}` (as there may be more after it). Returns the count of code units
  // consumed, which is the offset of that `}`.
  constexpr std::size_t parse_pad(std::basic_string_view<CharT> spec) {
    std::size_t ndx = 0;
    const std::size_t cnt = spec.size();
    // [fill] align
    if (cnt >= 2 && is_align(spec[1])) {
      fill = spec[0];
      align = to_aligned(spec[1]);
      ndx = 2;
    } else if (cnt >= 1 && is_align(spec[0])) {
      align = to_aligned(spec[0]);
      ndx = 1;
    }
    // sign
    if (ndx < cnt &&
        (spec[ndx] == CharT('+') || spec[ndx] == CharT('-') ||
            spec[ndx] == CharT(' ')))
    {
      sign = static_cast<char>(spec[ndx]);
      ++ndx;
    }
    // `#` alternate
    if (ndx < cnt && spec[ndx] == CharT('#')) {
      alternate = true;
      ++ndx;
    }
    // `0` zero-pad
    if (ndx < cnt && spec[ndx] == CharT('0')) {
      zero_pad = true;
      ++ndx;
    }
    // width
    ndx = width_arg.parse(spec, ndx);
    if (const auto fixed = width_arg.get_fixed()) width = *fixed;
    // `.` precision
    if (ndx < cnt && spec[ndx] == CharT('.')) {
      ++ndx;
      ndx = precision_arg.parse(spec, ndx);
      if (const auto fixed = precision_arg.get_fixed()) precision = *fixed;
    }
    // `L` locale
    if (ndx < cnt && spec[ndx] == CharT('L')) {
      has_locale = true;
      ++ndx;
    }
    // type: the one remaining char before the closing `}`, if any
    if (ndx < cnt && spec[ndx] != CharT('}')) {
      type = spec[ndx];
      ++ndx;
    }
    // The `?` type is debug.
    if (type == CharT('?')) debug = true;

    return ndx;
  }

  // Rewrite the spec to make all dynamic fields explicit. In other words,
  // every auto `{}` is replaced by a manual `{n}` with the claimed id `n` so
  // that the base reads the same arg the auto field would have.
  //
  // Only call once the ids are claimed; assumes any dynamic field here is
  // auto, as a format string cannot mix auto and manual indexing.
  [[nodiscard]] std::basic_string<CharT> rewrite_spec_as_explicit(
      std::basic_string_view<CharT> spec) const {
    std::array<std::size_t, 2> ids{};
    std::size_t got = 0;
    if (const auto id = width_arg.get_automatic()) ids[got++] = *id;
    if (const auto id = precision_arg.get_automatic()) ids[got++] = *id;

    std::basic_string<CharT> out;
    out.reserve(spec.size() + (got * 4));
    std::size_t next = 0;
    for (std::size_t i = 0; i < spec.size(); ++i) {
      out.push_back(spec[i]);
      if (spec[i] == CharT('{') && i + 1 < spec.size() &&
          spec[i + 1] == CharT('}'))
        append_decimal(out, ids[next++]);
    }
    return out;
  }

  // Emit `content`.
  //
  // The input is (narrow, 7-bit ASCII), so it's widened to `CharT`. It is
  // then padded to `field_width` with this spec's `fill` per its `align`. A
  // width no larger than the content is a no-op (width is a minimum).
  template<typename OutIt>
  [[nodiscard]] constexpr OutIt write_padded(OutIt out,
      std::string_view content, std::size_t field_width) const {
    auto [lead, trail] = calc_padding(align, content.size(), field_width);
    out = write_repeat(out, fill, lead);
    out = write_sv(out, content);
    out = write_repeat(out, fill, trail);
    return out;
  }

#pragma endregion
#pragma region Helpers
private:
  static constexpr bool is_align(CharT c) {
    return c == CharT('<') || c == CharT('>') || c == CharT('^');
  }

  static constexpr aligned to_aligned(CharT c) {
    if (c == CharT('>')) return aligned::right;
    if (c == CharT('^')) return aligned::center;
    return aligned::left;
  }

  // Append `v` as decimal digits, widened to `CharT`.
  // TODO: I know we're very low in the stack, but do we have to reinvent this
  // particular wheel?
  static void append_decimal(std::basic_string<CharT>& out, std::size_t v) {
    CharT buf[20];
    std::size_t len = 0;
    do {
      buf[len++] = static_cast<CharT>(CharT('0') + (v % 10));
      v /= 10;
    } while (v != 0);
    while (len != 0) out.push_back(buf[--len]);
  }

  static constexpr std::pair<size_t, size_t> calc_padding(aligned align,
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

  template<CharType InCharT, typename OutIt>
  static constexpr OutIt
  write_repeat(OutIt out, InCharT c, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
      *out++ = static_cast<CharT>(static_cast<wchar_t>(c));
    return out;
  }

  template<CharType InCharT, typename OutIt>
  static constexpr OutIt
  write_sv(OutIt out, std::basic_string_view<InCharT> sv) {
    for (const auto c : sv)
      *out++ = static_cast<CharT>(static_cast<wchar_t>(c));
    return out;
  }

#pragma endregion
};

#pragma endregion
#pragma region null_formatting

// Whether a null renders as an empty field or the text sentinel (default
// `(null)`). Selected independently for the plain and debug specs of
// `nullable_formatter`.
enum class null_formatting : bool { empty = false, sentinel = true };

#pragma endregion
#pragma region forwarding_formatter

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

#pragma endregion
#pragma region nullable_formatter

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
// honors the whole spec through the inherited formatter. A dynamic width
// reaches the sentinel, too: a manual id is read from the spec, and an auto
// `{}` is claimed from the parse context and re-presented to the base as an
// explicit id, so the sentinel and the present value resolve the same
// argument.
template<typename U, CharType CharT,
    null_formatting PlainNull = null_formatting::sentinel,
    null_formatting DebugNull = null_formatting::sentinel>
struct nullable_formatter: std::formatter<U, CharT> {
#pragma region Construction

  using base = std::formatter<U, CharT>;

  constexpr nullable_formatter() = default;
  constexpr explicit nullable_formatter(std::string_view marker)
      : marker_{marker} {}

#pragma endregion
#pragma region Parse

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
    if consteval {
      // Compile-time validation: let the base consume any dynamic arg ids and
      // check the spec. The recovered ids are only needed at format time, and
      // a manually constructed parse context cannot run those checks during
      // constant evaluation anyway.
      return base::parse(ctx);
    }

    const auto begin = ctx.begin();
    const auto spec_text = std::basic_string_view<CharT>{begin,
        static_cast<std::size_t>(ctx.end() - begin)};
    const auto consumed = spec_.parse_pad(spec_text);

    // The sentinel resolves its own dynamic width, so when it will be shown we
    // must learn the arg id. A manual `{n}` is in the text (and the base reads
    // it directly), but an auto `{}` has no id there: claim it from the
    // context and re-present the spec to the base with explicit ids. Otherwise
    // the base parses the real spec.
    constexpr bool is_plain_sentinel = PlainNull == null_formatting::sentinel;
    constexpr bool is_debug_sentinel = DebugNull == null_formatting::sentinel;
    const bool is_sentinel_shown =
        spec_.debug ? is_debug_sentinel : is_plain_sentinel;
    const bool is_width_auto = spec_.width_arg.is_automatic();
    const bool is_precision_auto = spec_.precision_arg.is_automatic();
    if (is_sentinel_shown && (is_width_auto || is_precision_auto)) {
      if (is_width_auto) spec_.width_arg.value = ctx.next_arg_id();
      if (is_precision_auto) spec_.precision_arg.value = ctx.next_arg_id();
      const auto synthetic = spec_.rewrite_spec_as_explicit(
          std::basic_string_view<CharT>{begin, consumed});
      std::basic_format_parse_context<CharT> sctx(synthetic);
      base::parse(sctx);
      return begin + consumed;
    }
    return base::parse(ctx);
  }

#pragma endregion
#pragma region Format

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

#pragma endregion
#pragma region Helpers
private:
  template<typename FormatContext>
  auto pad_sentinel(FormatContext& ctx) const {
    const std::size_t field_width =
        spec_.width_arg.is_dynamic()
            ? spec_.width_arg.get_dynamic(ctx)
            : spec_.width;
    return spec_.write_padded(ctx.out(), marker_, field_width);
  }

#pragma endregion
#pragma region Data members

  std::string_view marker_{"(null)"};
  parsed_spec<CharT> spec_;

#pragma endregion
};

#pragma endregion
#pragma region self_rendering_formatter

// Base for a `std::formatter` on a type that renders itself through a
// `format_to(out)` member, the modern analog of `operator<<`. A deriving
// specialization needs only `: self_rendering_formatter<CharT> {}`; the type
// is deduced at format time.
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
struct self_rendering_formatter {
#pragma region Parse

  constexpr void set_debug_format() { spec_.debug = true; }

  constexpr auto parse(std::basic_format_parse_context<CharT>& ctx)
      -> std::basic_format_parse_context<CharT>::iterator {
    const auto begin = ctx.begin();
    const auto it = ctx.end();
    spec_.parse_pad(std::basic_string_view<CharT>{begin,
        static_cast<std::size_t>(it - begin)});
    if (spec_.debug && spec_.is_dynamic())
      throw std::format_error("dynamic is unsupported");
    return it;
  }

#pragma endregion
#pragma region Format

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

#pragma endregion
#pragma region Data members
private:
  parsed_spec<CharT> spec_;

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::meta::formatting
