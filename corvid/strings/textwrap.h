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
#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../meta/concepts.h"
#include "../meta/fixed_string.h"
#include "string_literals.h"
#include "cstring_view.h"
#include "cases.h"
#include "expand_tabs.h"
#include "no_zero.h"
#include "splitting.h"

namespace corvid::strings { namespace textwrap {

// Textwrap
//
// Counterparts of Python's `textwrap` module: `dedent`, `indent`, `wrap`,
// `fill`, and `shorten`.
//
// In Python these are scoped under the module, so they have rather broad
// names. As a result, we chose to place it in a `textwrap` namespace that is
// not implicit and the caller is expected to reference them as
// `strings::textwrap::dedent`, `strings::textwrap::wrap`, and so on.
//
// For related functionality, see "expand_tabs.h".
//
// `dedent` is the piece that makes indented multi-line raw string literals
// usable, and its consteval overload makes them free. Pass the literal as a
// template argument and get back a `cstring_view` of a dedented, zero-
// terminated compile-time constant, with nothing at all left to do at
// runtime. This is the best way to use it.
//
//   constexpr auto usage = textwrap::dedent<R"(
//       usage: frob [-x] file...
//         -x  enable X mode)">();
//   std::cout << usage;
//
// `wrap` and `fill` reflow a paragraph to a width, with the knobs gathered in
// `wrap_options`, which is designed for designated initializers. For example:
// `textwrap::fill(text, {.width = 40})`.
//
// Everything here is `constexpr`, so results can be computed and checked
// entirely in constant evaluation. A `std::string` result cannot initialize a
// constexpr variable, though, since the allocation must stay within the
// evaluation. The consteval `dedent` is the exception that escapes, by parking
// its result in a `fixed_string` constant (see `dedented`) and handing out a
// view of it.

#pragma region Dedent

// Dedent implementation details, shared by the runtime and compile-time
// forms so the two cannot drift. Nothing here touches `std::string`; the
// helpers work purely on views and a caller-provided buffer.
namespace details {

// Return the count of leading spaces and tabs on `line`.
template<CharType CharT>
[[nodiscard]] constexpr size_t
indent_of(std::basic_string_view<CharT> line) noexcept {
  size_t ndx{};
  while (ndx < line.size() &&
         (line[ndx] == CharT(' ') || line[ndx] == CharT('\t')))
    ++ndx;
  return ndx;
}

// Return the length of the whitespace margin shared, code unit for code unit,
// by every content line of `sv`.
template<CharType CharT>
[[nodiscard]] constexpr size_t
dedent_margin(std::basic_string_view<CharT> sv) noexcept {
  std::basic_string_view<CharT> margin;
  bool found{};
  auto whole{sv};
  std::basic_string_view<CharT> line;
  while (more_lines(line, whole)) {
    const auto ndx = indent_of(line);
    if (ndx == line.size()) continue;
    const auto line_indent = line.substr(0, ndx);
    if (!found) {
      margin = line_indent;
      found = true;
      continue;
    }
    const auto cnt = std::min(margin.size(), line_indent.size());
    size_t match{};
    while (match < cnt && margin[match] == line_indent[match]) ++match;
    margin = margin.substr(0, match);
  }
  return margin.size();
}

// Return the length of `sv`'s dedented form, given its `margin` length.
template<CharType CharT>
[[nodiscard]] constexpr size_t
dedent_size(std::basic_string_view<CharT> sv, size_t margin) noexcept {
  size_t cnt{};
  auto whole{sv};
  std::basic_string_view<CharT> line;
  while (more_lines(line, whole, line_ends::keep)) {
    auto brk = line.size();
    while (
        brk && (line[brk - 1] == CharT('\n') || line[brk - 1] == CharT('\r')))
      --brk;
    const auto content = line.substr(0, brk);
    if (indent_of(content) != content.size()) cnt += content.size() - margin;
    cnt += line.size() - brk;
  }
  return cnt;
}

// Write the dedented form of `sv` into `out_span`, stripping `margin` from
// each content line and keeping only the break of whitespace-only lines. The
// caller sizes `out_span` exactly, via `dedent_size`; this is asserted.
template<CharType CharT>
constexpr void dedent_fill(std::basic_string_view<CharT> sv, size_t margin,
    std::span<std::type_identity_t<CharT>> out_span) noexcept {
  auto out = out_span.data();
  auto whole{sv};
  std::basic_string_view<CharT> line;
  while (more_lines(line, whole, line_ends::keep)) {
    auto brk = line.size();
    while (
        brk && (line[brk - 1] == CharT('\n') || line[brk - 1] == CharT('\r')))
      --brk;
    const auto content = line.substr(0, brk);
    if (indent_of(content) != content.size())
      for (const auto c : content.substr(margin)) *out++ = c;
    for (const auto c : line.substr(brk)) *out++ = c;
  }
  assert(out == out_span.data() + out_span.size());
}

} // namespace details

// Return a copy of `s` with the common leading whitespace removed from every
// line, Python `textwrap.dedent`-style.
//
// The margin is the longest run of spaces and tabs shared, code unit for code
// unit, by every line with non-whitespace content. Tabs and spaces are
// distinct, and nothing is expanded. Lines consisting solely of spaces and
// tabs do not participate in the margin and are normalized to just their line
// break in the result.
//
// One divergence from Python: line breaks are universal ("\r\n", "\r", or
// "\n"), matching `split_lines`, where Python recognizes only "\n" and treats
// a stray '\r' as line content.
//
// For example, `dedent("    hello\n      world\n")` returns
// "hello\n  world\n".
//
// When the text is a literal, as it most often will be, prefer the consteval
// overload below, which leaves nothing at all for runtime.
template<StringViewLike S>
[[nodiscard]] constexpr auto dedent(const S& s) {
  using C = char_type_of_t<S>;
  const auto sv{as_view(s)};
  const auto margin = details::dedent_margin(sv);
  std::basic_string<C> r;
  no_zero{r}.resize_to(details::dedent_size(sv, margin));
  details::dedent_fill(sv, margin, r);
  return r;
}

// The dedented form of the literal `Text`, as a `fixed_string` constant.
//
// This inline variable is the backing storage that the consteval `dedent`
// hands out views into, deduplicated across translation units. Use it
// directly when the `fixed_string` itself is wanted, e.g. for further
// compile-time composition.
template<basic_fixed_string Text>
inline constexpr auto dedented = [] {
  using char_t = decltype(Text)::char_t;
  constexpr auto margin = details::dedent_margin(Text.view());
  constexpr auto cnt = details::dedent_size(Text.view(), margin);
  std::array<char_t, cnt + 1> buf{};
  details::dedent_fill(Text.view(), margin, std::span{buf.data(), cnt});
  return basic_fixed_string{buf.data(), std::integral_constant<size_t, cnt>{}};
}();

// Dedent the literal `Text` entirely at compile time, returning a
// `cstring_view` of a static constant.
//
// This is the best way to dedent a raw string literal: the text goes in as a
// template argument, the result views storage computed at compile time and
// shared program-wide (see `dedented`), and nothing is left to do at runtime.
// As a bonus, the result is zero-terminated, so `c_str` feeds C interfaces
// directly. The semantics match the runtime overload exactly.
//
// Call it with the literal as the template argument and bind the result to a
// constexpr `cstring_view`, spelled `auto`:
//
//   constexpr auto hello = textwrap::dedent<"    hello\n      world\n">();
template<basic_fixed_string Text>
[[nodiscard]] consteval auto dedent() noexcept {
  return dedented<Text>.cview();
}

#pragma endregion
#pragma region Indent

// Return a copy of `s` with `prefix` prepended to selected lines, Python
// `textwrap.indent`-style.
//
// The predicate is called with each line, including its line break, and a true
// return adds the prefix. Line breaks are universal ("\r\n", "\r", or "\n"),
// as in Python.
template<StringViewLike S,
    std::predicate<std::basic_string_view<char_type_of_t<S>>> Pred>
[[nodiscard]] constexpr auto indent(const S& s,
    std::basic_string_view<char_type_of_t<S>> prefix, Pred pred) {
  using C = char_type_of_t<S>;
  auto whole{as_view(s)};
  std::basic_string<C> r;
  r.reserve(whole.size());
  std::basic_string_view<C> line;
  while (more_lines(line, whole, line_ends::keep)) {
    if (pred(line)) r.append(prefix);
    r.append(line);
  }
  return r;
}

// Return a copy of `s` with `prefix` prepended to every line that has
// non-whitespace content, so blank and whitespace-only lines pass through
// untouched.
//
// For example, `indent("one\n\ntwo", "> ")` returns "> one\n\n> two".
template<StringViewLike S>
[[nodiscard]] constexpr auto
indent(const S& s, std::basic_string_view<char_type_of_t<S>> prefix) {
  using C = char_type_of_t<S>;
  return indent(s, prefix, [](std::basic_string_view<C> line) {
    return !is_space(line);
  });
}

#pragma endregion
#pragma region Wrap

// The default `wrap_options` truncation marker, " [...]", per code unit.
template<CharType CharT>
inline constexpr auto default_placeholder_chars = std::array{CharT(' '),
    CharT('['), CharT('.'), CharT('.'), CharT('.'), CharT(']')};
template<CharType CharT>
inline constexpr std::basic_string_view<CharT> default_placeholder{
    default_placeholder_chars<CharT>};

// Options for `wrap`, `fill`, and `shorten`, mirroring Python's
// `textwrap.TextWrapper` knobs.
//
// Designed for designated initializers. For example: `wrap(text, {.width = 40,
// .break_long_words = false})`. The defaults match Python's.
//
// Python's `fix_sentence_endings` and `break_on_hyphens` are deliberately
// omitted. The former is an English-specific heuristic that Python itself
// defaults to off; the latter encodes English hyphenation rules in a regex,
// out of scope for this band. `wrap` therefore matches Python running with
// both off, which means overlong hyphenated words break at the width, not at
// their hyphens.
template<CharType CharT>
struct wrap_options {
  // Maximum line length, counting any indent. Python raises on zero; here zero
  // is not an error, and each line instead makes progress with a single code
  // unit of content, overflowing minimally.
  size_t width{70};

  // Prefix for the first line and for every later line, respectively. Each
  // counts toward `width`.
  std::basic_string_view<CharT> initial_indent{};
  std::basic_string_view<CharT> subsequent_indent{};

  // Expand tabs to spaces first, exactly as `strings::expand_tabs` with
  // `tab_size` does.
  bool expand_tabs{true};
  size_t tab_size{8};

  // Replace each remaining whitespace code unit with a space.
  bool replace_whitespace{true};

  // Drop whitespace at the start and end of every line, after wrapping but
  // before indenting. As in Python, whitespace at the start of the first line
  // is kept when non-whitespace follows it.
  bool drop_whitespace{true};

  // Break words longer than a line so no line overflows `width`; when false,
  // an overlong word gets a line to itself.
  bool break_long_words{true};

  // Truncate the output to at most this many lines, marking the cut with
  // `placeholder`; zero means unlimited. Python raises when the placeholder
  // cannot fit; here it is emitted on the final line anyway, overflowing
  // `width`.
  size_t max_lines{};
  std::basic_string_view<CharT> placeholder{default_placeholder<CharT>};
};

// Wrap implementation details, a faithful decomposition of CPython's
// `TextWrapper._wrap_chunks`.
namespace details {

// Whether `chunk` is a whitespace run (or an empty piece of one).
template<CharType CharT>
[[nodiscard]] constexpr bool
is_ws_chunk(std::basic_string_view<CharT> chunk) noexcept {
  return chunk.empty() || is_space(chunk.front());
}

// Normalize the paragraph for wrapping: expand tabs, then flatten line breaks
// and the rest of the whitespace to plain spaces, per the options.
template<CharType CharT>
[[nodiscard]] constexpr std::basic_string<CharT> munge_whitespace(
    std::basic_string_view<CharT> sv, const wrap_options<CharT>& options) {
  // If/else, not a ternary because MSVC cl miscomputes a ternary of string
  // prvalues in constant evaluation.
  std::basic_string<CharT> text;
  if (options.expand_tabs)
    text = expand_tabs(sv, options.tab_size);
  else
    text.assign(sv);
  if (options.replace_whitespace)
    for (auto& c : text)
      if (is_space(c)) c = CharT(' ');
  return text;
}

// Chop the munged text into alternating runs of whitespace and
// non-whitespace. Whitespace runs are chunks too, so inter-word gaps count
// toward line widths.
template<CharType CharT>
[[nodiscard]] constexpr auto chunk_runs(std::basic_string_view<CharT> tv) {
  std::vector<std::basic_string_view<CharT>> chunks;
  for (size_t pos = 0; pos < tv.size();) {
    const bool ws = is_space(tv[pos]);
    auto end = pos + 1;
    while (end < tv.size() && is_space(tv[end]) == ws) ++end;
    chunks.push_back(tv.substr(pos, end - pos));
    pos = end;
  }
  return chunks;
}

// Join `line_indent` and `pieces` into a fresh line at the back of `lines`.
template<CharType CharT>
constexpr void emit_line(std::vector<std::basic_string<CharT>>& lines,
    std::basic_string_view<CharT> line_indent,
    const std::vector<std::basic_string_view<CharT>>& pieces) {
  auto& line_out = lines.emplace_back(line_indent);
  for (const auto piece : pieces) line_out.append(piece);
}

// Take the next line's pieces greedily from `chunks`, advancing `next`.
//
// Applies the drop-whitespace and long-word rules; a broken long word leaves
// its remainder in place at `next`. Returns the pieces and their total
// length. Empty pieces mean the line dissolved to nothing (a dropped
// whitespace run), and the caller just moves on.
template<CharType CharT>
[[nodiscard]] constexpr auto
take_line(std::vector<std::basic_string_view<CharT>>& chunks, size_t& next,
    size_t width, bool first_line, const wrap_options<CharT>& options) {
  std::vector<std::basic_string_view<CharT>> cur;
  size_t cur_len{};
  // Continuation lines drop their leading whitespace.
  if (options.drop_whitespace && !first_line && is_ws_chunk(chunks[next]))
    ++next;
  while (next < chunks.size() && cur_len + chunks[next].size() <= width) {
    cur_len += chunks[next].size();
    cur.push_back(chunks[next++]);
  }
  // An overlong chunk: break it at the width or grant it a line to itself.
  if (next < chunks.size() && chunks[next].size() > width) {
    if (options.break_long_words) {
      const auto space_left = width < 1 ? 1 : width - cur_len;
      cur.push_back(chunks[next].substr(0, space_left));
      chunks[next].remove_prefix(space_left);
      cur_len += cur.back().size();
    } else if (cur.empty()) {
      cur.push_back(chunks[next++]);
      cur_len = cur.back().size();
    }
  }
  // The line's trailing whitespace goes too.
  if (options.drop_whitespace && !cur.empty() && is_ws_chunk(cur.back())) {
    cur_len -= cur.back().size();
    cur.pop_back();
  }
  return std::pair{std::move(cur), cur_len};
}

// End the output at the `max_lines` limit, marking the cut with the
// placeholder.
//
// Pops pieces until the placeholder fits after real content; failing that,
// grafts it onto the rstripped previous line when that stays within the full
// width; failing that, it gets a line of its own, without its own leading
// whitespace, overflowing the width if it must.
template<CharType CharT>
constexpr void truncate_line(std::vector<std::basic_string<CharT>>& lines,
    std::vector<std::basic_string_view<CharT>>& cur, size_t cur_len,
    std::basic_string_view<CharT> line_indent, size_t width,
    const wrap_options<CharT>& options) {
  while (!cur.empty()) {
    if (!is_ws_chunk(cur.back()) &&
        cur_len + options.placeholder.size() <= width)
    {
      cur.push_back(options.placeholder);
      emit_line(lines, line_indent, cur);
      return;
    }
    cur_len -= cur.back().size();
    cur.pop_back();
  }
  if (!lines.empty()) {
    auto prev = as_view(lines.back());
    while (!prev.empty() && is_space(prev.back())) prev.remove_suffix(1);
    if (prev.size() + options.placeholder.size() <= options.width) {
      lines.back().resize(prev.size());
      lines.back().append(options.placeholder);
      return;
    }
  }
  auto ph = options.placeholder;
  while (!ph.empty() && is_space(ph.front())) ph.remove_prefix(1);
  auto& line_out = lines.emplace_back(line_indent);
  line_out.append(ph);
}

} // namespace details

// Wrap the single paragraph in `s` into lines at most `options.width` wide,
// returning them as a vector of strings.
//
// Tabs are expanded, remaining whitespace collapses to single spaces, and
// words are packed greedily onto lines. See `wrap_options` for every knob and
// for the deliberate divergences from Python. Purely-whitespace input yields
// no lines at all.
//
// For example, `wrap("The quick brown fox jumped over the lazy dog",
// {.width = 15})` returns {"The quick brown", "fox jumped over",
// "the lazy dog"}.
template<StringViewLike S>
[[nodiscard]] constexpr auto
wrap(const S& s, const wrap_options<char_type_of_t<S>>& options = {}) {
  using C = char_type_of_t<S>;
  const auto text = details::munge_whitespace(as_view(s), options);
  auto chunks = details::chunk_runs(std::basic_string_view<C>{text});
  // Pack chunks greedily onto lines, faithfully following CPython's
  // `TextWrapper._wrap_chunks`.
  std::vector<std::basic_string<C>> lines;
  size_t next{};
  while (next < chunks.size()) {
    const auto line_indent =
        lines.empty() ? options.initial_indent : options.subsequent_indent;
    const auto width =
        options.width > line_indent.size()
            ? options.width - line_indent.size()
            : 0;
    auto [cur, cur_len] =
        details::take_line(chunks, next, width, lines.empty(), options);
    if (cur.empty()) continue;
    // Emit the line, unless it is the last that `max_lines` allows and more
    // content remains, in which case the placeholder takes over.
    const auto remaining = chunks.size() - next;
    const auto fits_last =
        (remaining == 0 ||
            (options.drop_whitespace && remaining == 1 &&
                details::is_ws_chunk(chunks[next]))) &&
        cur_len <= width;
    if (!options.max_lines || lines.size() + 1 < options.max_lines ||
        fits_last)
    {
      details::emit_line(lines, line_indent, cur);
      continue;
    }
    details::truncate_line(lines, cur, cur_len, line_indent, width, options);
    break;
  }
  return lines;
}

#pragma endregion
#pragma region Fill

// Wrap the single paragraph in `s` and return it as one string with the lines
// joined by newlines, Python `textwrap.fill`-style.
//
// For example, `fill("one two three", {.width = 8})` returns
// "one two\nthree".
template<StringViewLike S>
[[nodiscard]] constexpr auto
fill(const S& s, const wrap_options<char_type_of_t<S>>& options = {}) {
  using C = char_type_of_t<S>;
  const auto lines = wrap(s, options);
  std::basic_string<C> r;
  size_t total{lines.empty() ? 0 : lines.size() - 1};
  for (const auto& line : lines) total += line.size();
  r.reserve(total);
  bool first{true};
  for (const auto& line : lines) {
    if (!first) r.push_back(C('\n'));
    r.append(line);
    first = false;
  }
  return r;
}

#pragma endregion
#pragma region Shorten

// Collapse the whitespace in `s` and truncate it to a single line of at most
// `width`, Python `textwrap.shorten`-style.
//
// Runs of whitespace collapse to single spaces and the ends are stripped. If
// the result fits in `width` it is returned whole; otherwise words drop from
// the end and `placeholder` marks the cut.
//
// For example, `shorten("Hello  world!", 12)` returns "Hello world!", while
// at a width of 11 it returns "Hello [...]".
template<StringViewLike S>
[[nodiscard]] constexpr auto shorten(const S& s, size_t width,
    std::basic_string_view<char_type_of_t<S>> placeholder =
        default_placeholder<char_type_of_t<S>>) {
  using C = char_type_of_t<S>;
  const auto sv{as_view(s)};
  std::basic_string<C> collapsed;
  collapsed.reserve(sv.size());
  bool pending{};
  for (const auto c : sv) {
    if (is_space(c)) {
      pending = true;
      continue;
    }
    if (pending && !collapsed.empty()) collapsed.push_back(C(' '));
    pending = false;
    collapsed.push_back(c);
  }
  return fill(collapsed,
      {.width = width, .max_lines = 1, .placeholder = placeholder});
}

#pragma endregion

}} // namespace corvid::strings::textwrap
