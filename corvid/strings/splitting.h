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
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../meta/concepts.h"
#include "../meta/crossplatform.h"
#include "delimiting.h"
#include "opt_string_view.h"
#include "string_literals.h"

namespace corvid::strings { inline namespace splitting {

#pragma region Split

// For all split functions, `delim` defaults to " " and can be specified as any
// set of characters.

// The full ASCII whitespace set (exactly the characters `is_space` in
// "cases.h" accepts), narrow and wide.
//
// The default delimiter stays a lone space for speed and simplicity; pass one
// of these instead to split on Python-style whitespace. The trim functions
// default the same way and accept these the same way.
inline constexpr std::string_view whitespace = " \t\n\v\f\r";
inline constexpr std::wstring_view wwhitespace = L" \t\n\v\f\r";

// Extract next delimited piece destructively from `whole`.
//
// The return type `R` effectively defaults to a `std::basic_string_view` of
// `whole`'s own code unit; specify `R` as an owning string type (e.g.
// `std::string`) to make a deep copy.
template<typename R = void, CharType CharT>
[[nodiscard]] constexpr auto
extract_piece(std::basic_string_view<CharT>& whole,
    std::type_identity_t<basic_delim<CharT>> d = {}) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<CharT>, R>;
  auto pos = std::min(whole.size(), d.find_in(whole));
  auto part = whole.substr(0, pos);
  whole.remove_prefix(std::min(whole.size(), pos + 1));
  return result_t{part};
}

// Extract next delimited piece into `part`, removing it from `whole`.
//
// Returns true so long as there's more work to do.
// Pass an owning string type as `part` to make a deep copy.
template<typename R, CharType CharT>
[[nodiscard]] constexpr bool
more_pieces(R& part, std::basic_string_view<CharT>& whole,
    std::type_identity_t<basic_delim<CharT>> d = {}) {
  auto all = whole.size();
  part = extract_piece<R>(whole, d);
  return part.size() != all;
}

// Split all pieces by delimiters and return parts in vector.
//
// Does not omit empty parts. The vector element type `R` effectively defaults
// to a `std::basic_string_view` of `whole`'s own code unit; specify `R` as an
// owning string type (e.g. `std::string`) to make deep copies.
template<typename R = void, StringViewLike S>
[[nodiscard]] constexpr auto
split(const S& whole_in, basic_delim<char_type_of_t<S>> d = {}) {
  using C = char_type_of_t<S>;
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<C>, R>;
  auto whole = as_view(whole_in);
  std::vector<result_t> parts;
  std::basic_string_view<C> part;
  for (bool more = !whole.empty(); more;) {
    more = more_pieces(part, whole, d);
    parts.emplace_back(part);
  }
  return parts;
}

// Split a temporary string by delimiters, returning deep copies of parts in a
// vector.
//
// The vector element type `R` effectively defaults to an owning string of
// `whole`'s code unit, since views into the temporary would dangle.
template<typename R = void, CharType CharT>
[[nodiscard]] constexpr auto split(std::basic_string<CharT>&& whole,
    std::type_identity_t<basic_delim<CharT>> d = {}) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string<CharT>, R>;
  // A trivially copyable `R` is a non-owning view, which would dangle into
  // the temporary; this overload exists precisely to prevent that.
  static_assert(!std::is_trivially_copyable_v<result_t>,
      "R must be an owning string type");
  return split<result_t>(std::basic_string_view<CharT>{whole}, d);
}

// Split at most `n` times by delimiters and return parts in vector.
//
// As the bounded variant of `split`, it performs up to `n` splits, so the
// vector holds at most `n + 1` parts, and the final part is the untouched
// remainder, delimiters intact. With a large enough `n`, it is equivalent to
// `split`.
//
// The vector element type `R` effectively defaults to a
// `std::basic_string_view` of `whole`'s own code unit; specify `R` as an
// owning string type (e.g. `std::string`) to make deep copies.
template<typename R = void, StringViewLike S>
[[nodiscard]] constexpr auto
split_n(const S& whole_in, size_t n, basic_delim<char_type_of_t<S>> d = {}) {
  using C = char_type_of_t<S>;
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<C>, R>;
  auto whole = as_view(whole_in);
  std::vector<result_t> parts;
  std::basic_string_view<C> part;
  for (bool more = !whole.empty(); more;) {
    if (n == 0) {
      parts.emplace_back(whole);
      break;
    }
    --n;
    more = more_pieces(part, whole, d);
    parts.emplace_back(part);
  }
  return parts;
}

// Split a temporary string at most `n` times, returning deep copies of parts
// in a vector.
//
// The vector element type `R` effectively defaults to an owning string of
// `whole`'s code unit, since views into the temporary would dangle.
template<typename R = void, CharType CharT>
[[nodiscard]] constexpr auto split_n(std::basic_string<CharT>&& whole,
    size_t n, std::type_identity_t<basic_delim<CharT>> d = {}) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string<CharT>, R>;
  // A trivially copyable `R` is a non-owning view, which would dangle into
  // the temporary; this overload exists precisely to prevent that.
  static_assert(!std::is_trivially_copyable_v<result_t>,
      "R must be an owning string type");
  return split_n<result_t>(std::basic_string_view<CharT>{whole}, n, d);
}

#pragma endregion
#pragma region RSplit

// The r-prefixed functions mirror the plain ones from the right: pieces come
// off the tail, so a full `rsplit` returns the parts in right-to-left
// encounter order, exactly the reverse of `split`.
//
// Python parity, for reference: an unbounded Python `rsplit` is simply
// `split`, and a bounded Python `rsplit(sep, n)` is `rsplit_n` with the
// resulting vector reversed.

// Extract last delimited piece destructively from `whole`.
//
// The return type `R` effectively defaults to a `std::basic_string_view` of
// `whole`'s own code unit; specify `R` as an owning string type (e.g.
// `std::string`) to make a deep copy.
template<typename R = void, CharType CharT>
[[nodiscard]] constexpr auto
rextract_piece(std::basic_string_view<CharT>& whole,
    std::type_identity_t<basic_delim<CharT>> d = {}) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<CharT>, R>;
  const auto pos = d.rfind_in(whole);
  const auto part = whole.substr(pos == npos ? 0 : pos + 1);
  whole.remove_suffix(part.size() + (pos == npos ? 0 : 1));
  return result_t{part};
}

// Extract last delimited piece into `part`, removing it from `whole`.
//
// Returns true so long as there's more work to do.
// Pass an owning string type as `part` to make a deep copy.
template<typename R, CharType CharT>
[[nodiscard]] constexpr bool
rmore_pieces(R& part, std::basic_string_view<CharT>& whole,
    std::type_identity_t<basic_delim<CharT>> d = {}) {
  auto all = whole.size();
  part = rextract_piece<R>(whole, d);
  return part.size() != all;
}

// Split all pieces from the right and return parts in vector.
//
// Returns exactly the reverse of `split`: same parts, right-to-left encounter
// order, empty parts included. The vector element type `R` effectively
// defaults to a `std::basic_string_view` of `whole`'s own code unit; specify
// `R` as an owning string type (e.g. `std::string`) to make deep copies.
template<typename R = void, StringViewLike S>
[[nodiscard]] constexpr auto
rsplit(const S& whole_in, basic_delim<char_type_of_t<S>> d = {}) {
  using C = char_type_of_t<S>;
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<C>, R>;
  auto whole = as_view(whole_in);
  std::vector<result_t> parts;
  std::basic_string_view<C> part;
  for (bool more = !whole.empty(); more;) {
    more = rmore_pieces(part, whole, d);
    parts.emplace_back(part);
  }
  return parts;
}

// Split a temporary string from the right, returning deep copies of parts in a
// vector.
//
// The vector element type `R` effectively defaults to an owning string of
// `whole`'s code unit, since views into the temporary would dangle.
template<typename R = void, CharType CharT>
[[nodiscard]] constexpr auto rsplit(std::basic_string<CharT>&& whole,
    std::type_identity_t<basic_delim<CharT>> d = {}) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string<CharT>, R>;
  // A trivially copyable `R` is a non-owning view, which would dangle into
  // the temporary; this overload exists precisely to prevent that.
  static_assert(!std::is_trivially_copyable_v<result_t>,
      "R must be an owning string type");
  return rsplit<result_t>(std::basic_string_view<CharT>{whole}, d);
}

// Split at most `n` times from the right and return parts in vector.
//
// As the bounded variant of `rsplit`, it performs up to `n` splits off the
// tail, so the vector holds at most `n + 1` parts, and the final part is the
// untouched remainder (the head), delimiters intact. With a large enough `n`,
// it is equivalent to `rsplit`; reversed, it matches Python `rsplit` with
// `maxsplit`.
//
// The vector element type `R` effectively defaults to a
// `std::basic_string_view` of `whole`'s own code unit; specify `R` as an
// owning string type (e.g. `std::string`) to make deep copies.
template<typename R = void, StringViewLike S>
[[nodiscard]] constexpr auto
rsplit_n(const S& whole_in, size_t n, basic_delim<char_type_of_t<S>> d = {}) {
  using C = char_type_of_t<S>;
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<C>, R>;
  auto whole = as_view(whole_in);
  std::vector<result_t> parts;
  std::basic_string_view<C> part;
  for (bool more = !whole.empty(); more;) {
    if (n == 0) {
      parts.emplace_back(whole);
      break;
    }
    --n;
    more = rmore_pieces(part, whole, d);
    parts.emplace_back(part);
  }
  return parts;
}

// Split a temporary string at most `n` times from the right, returning deep
// copies of parts in a vector.
//
// The vector element type `R` effectively defaults to an owning string of
// `whole`'s code unit, since views into the temporary would dangle.
template<typename R = void, CharType CharT>
[[nodiscard]] constexpr auto rsplit_n(std::basic_string<CharT>&& whole,
    size_t n, std::type_identity_t<basic_delim<CharT>> d = {}) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string<CharT>, R>;
  // A trivially copyable `R` is a non-owning view, which would dangle into
  // the temporary; this overload exists precisely to prevent that.
  static_assert(!std::is_trivially_copyable_v<result_t>,
      "R must be an owning string type");
  return rsplit_n<result_t>(std::basic_string_view<CharT>{whole}, n, d);
}

#pragma endregion
#pragma region PieceGenerator

// Concept to detect whether a type is a piece_generator.
//
// This requires it to expose a `char_t` code unit, be moveable, be
// constructible from a view of that code unit, and support the `more_pieces`
// method.
//
// A piece generator is plugged into a split adapter to perform split
// operations. This modularized approach makes it possible to define delimiters
// any way you like, or strip padding, or skip empty pieces. Using a stateful
// object also allows you to do things like limit how many pieces are returned,
// alternate the delimiters, or make a mutable copy of each so as to unescape.
//
// Note: If the piece generator returns a view into a copy of the piece, then
// the output must copy it into a `std::string` (or equivalent) to avoid
// dangling references. Alternately, you can choose to provide memory backing
// for all of the pieces, relaxing this requirement.
template<typename T>
concept PieceGenerator = requires(T t,
    std::basic_string_view<typename T::char_t> s) {
  typename T::char_t;
  requires std::is_move_constructible_v<T>;
  { T{s} };
  { t.more_pieces(s) } -> std::convertible_to<bool>;
};

#pragma endregion
#pragma region piece_generator

// Finds the next delimiter in the remainder of the string. What constitutes a
// delimiter is baked into the callable. It is always fed the remainder, so
// there's no need for a `pos` parameter. Returns a pair of positions for the
// start and end of the delimiter; if not found, the first is `npos` and the
// second is unused.
template<typename F, typename CharT>
concept DelimFinder =
    std::invocable<F, std::basic_string_view<CharT>> &&
    std::convertible_to<std::invoke_result_t<F, std::basic_string_view<CharT>>,
        std::pair<size_t, size_t>>;

// Filters a candidate piece. Returns a `null` value to skip it. Can strip
// padding, or even use an internal buffer to unescape.
template<typename F, typename CharT>
concept PieceFilter =
    std::invocable<F, std::basic_string_view<CharT>> &&
    std::convertible_to<std::invoke_result_t<F, std::basic_string_view<CharT>>,
        basic_opt_string_view<void, CharT>>;

// Default delimiter finder: splits on a single space.
template<CharType CharT>
struct default_delim_finder {
  constexpr std::pair<size_t, size_t> operator()(
      std::basic_string_view<CharT> s) const {
    auto pos = s.find(CharT{' '});
    return {pos, pos + 1};
  }
};

// Default piece filter: passes every piece through unchanged.
template<CharType CharT>
struct default_piece_filter {
  constexpr basic_opt_string_view<void, CharT> operator()(
      std::basic_string_view<CharT> s) const {
    return s;
  }
};

// Implements the PieceGenerator concept to provide a working example that is
// composable enough to handle many common cases.
//
// The delimiter finder and piece filter are stored by value as their own
// types (any invocables matching `DelimFinder` / `PieceFilter`), so no
// `std::function` type erasure is imposed. Build one with custom callables via
// CTAD; the `piece_generator` alias uses the defaults.
//
// Treats input as `basic_opt_string_view`, returning a piece when `empty` but
// not `null`. The end state is `null`.
template<CharType CharT = char,
    DelimFinder<CharT> Finder = default_delim_finder<CharT>,
    PieceFilter<CharT> Filter = default_piece_filter<CharT>>
struct basic_piece_generator {
#pragma region Member types

  // The code unit and its view / optional-view types.
  using char_t = CharT;
  using view_t = std::basic_string_view<CharT>;
  using opt_view_t = basic_opt_string_view<void, CharT>;

#pragma endregion
#pragma region Reset

  // This is technically not a requirement for a PieceGenerator, but it's a
  // good idea, especially if you have state that needs to be cleared in
  // between calls. The return allows passing `piece_generator.reset(x)` to the
  // `split` function.
  constexpr auto& reset(view_t new_whole) {
    whole = new_whole;
    return *this;
  }

#pragma endregion
#pragma region Pieces

  // Stateless static helper, which can be easily reused from your own class.
  //
  // Fills `part` with the next piece from `whole` and returns `true`. On
  // failure, such as when there's nothing left to parse, returns `false`.
  [[nodiscard]] static constexpr bool more_pieces(view_t& part,
      opt_view_t& whole, const DelimFinder<CharT> auto& finder,
      const PieceFilter<CharT> auto& filter) {
    for (;;) {
      if (whole.null()) return false;
      const auto [pos, next] = finder(whole);
      const opt_view_t opt_part{filter(whole.substr(0, pos))};
      if (pos == npos)
        whole = std::nullopt;
      else
        whole.remove_prefix(next);
      if (!opt_part) continue;
      part = *opt_part;
      return true;
    }
  }

  // Fills `part` with the next piece and returns `true`. On failure, such as
  // when there's nothing left to parse, returns `false`.
  [[nodiscard]] constexpr bool more_pieces(view_t& part) {
    return more_pieces(part, whole, finder, filter);
  }

#pragma endregion
#pragma region Data members

  opt_view_t whole;
  CORVID_NO_UNIQUE_ADDRESS Finder finder{};
  CORVID_NO_UNIQUE_ADDRESS Filter filter{};

#pragma endregion
};

// The default piece generator, over `char`.
using piece_generator = basic_piece_generator<char>;

// Deduce the code unit and the callable types from the constructor arguments.
template<CharType CharT>
basic_piece_generator(std::basic_string_view<CharT>)
    -> basic_piece_generator<CharT>;
template<CharType CharT, DelimFinder<CharT> Finder, PieceFilter<CharT> Filter>
basic_piece_generator(std::basic_string_view<CharT>, Finder, Filter)
    -> basic_piece_generator<CharT, Finder, Filter>;

#pragma endregion
#pragma region split_gen

// Split `whole` using the PieceGenerator and return parts in vector.
template<PieceGenerator PG = piece_generator, typename R = void>
[[nodiscard]] constexpr auto
split_gen(std::basic_string_view<typename PG::char_t> whole) {
  return split<R>(PG{whole});
}

// Use this version when you want to set additional generator parameters or
// need access to it afterwards.
template<typename R = void, PieceGenerator PG>
[[nodiscard]] constexpr auto split(PG pgen) {
  using C = PG::char_t;
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<C>, R>;
  std::vector<result_t> parts;
  std::basic_string_view<C> part;
  while (pgen.more_pieces(part)) parts.emplace_back(part);
  return parts;
}

// For alternative design choices when solving a similar problem, compare
// with:
// https://github.com/abseil/abseil-cpp/blob/master/absl/strings/internal/str_split_internal.h

#pragma endregion
#pragma region split_lines

// Whether to keep each line's terminating line break as part of the returned
// line or discard it. The equivalent of Python's `keepends`.
enum class line_ends : bool { discard = false, keep = true };

// Line-break delimiter finder: treats "\r\n", "\r", and "\n" as single line
// breaks.
//
// Usable with `basic_piece_generator`, where standard split semantics apply,
// so a trailing line break yields a trailing empty piece; `split_lines`
// instead drops it.
template<CharType CharT>
struct line_delim_finder {
  constexpr std::pair<size_t, size_t> operator()(
      std::basic_string_view<CharT> s) const {
    constexpr std::array line_breaks{CharT{'\r'}, CharT{'\n'}};
    const auto pos = s.find_first_of(
        std::basic_string_view<CharT>{line_breaks.data(), line_breaks.size()});
    if (pos == npos) return {npos, npos};
    auto next = pos + 1;
    if (s[pos] == CharT{'\r'} && next < s.size() && s[next] == CharT{'\n'})
      ++next;
    return {pos, next};
  }
};

// Extract next line destructively from `whole`.
//
// A line break is "\r\n", "\r", or "\n", and ends the line; `line_ends::keep`
// retains it as part of the returned line, while the default discards it.
// Either way, it is consumed from `whole`. The last line needs no break, and
// an empty `whole` yields an empty line, so check `whole` before calling, or
// let `more_lines` do it for you.
//
// The return type `R` effectively defaults to a `std::basic_string_view` of
// `whole`'s own code unit; specify `R` as an owning string type (e.g.
// `std::string`) to make a deep copy.
template<typename R = void, CharType CharT>
[[nodiscard]] constexpr auto extract_line(std::basic_string_view<CharT>& whole,
    line_ends ends = line_ends::discard) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<CharT>, R>;
  const auto [pos, next] = line_delim_finder<CharT>{}(whole);
  const bool found = (pos != npos);
  const auto end =
      !found ? whole.size()
      : ends == line_ends::keep
          ? next
          : pos;
  const auto line = whole.substr(0, end);
  whole.remove_prefix(found ? next : whole.size());
  return result_t{line};
}

// Extract next line into `line`, removing it (and its break) from `whole`.
//
// Returns true when a line was extracted; returns false when `whole` is
// exhausted, leaving `line` untouched. Pass an owning string type as `line` to
// make a deep copy.
template<typename R, CharType CharT>
[[nodiscard]] constexpr bool
more_lines(R& line, std::basic_string_view<CharT>& whole,
    line_ends ends = line_ends::discard) {
  if (whole.empty()) return false;
  line = extract_line<R>(whole, ends);
  return true;
}

// Split into lines on universal line breaks and return them in a vector.
//
// A line break is "\r\n", "\r", or "\n"; pass `line_ends::keep` to retain each
// line's break. Matching Python `str.splitlines` (and unlike `split`), a
// trailing line break does not produce a trailing empty line, and an empty
// input produces no lines; interior empty lines are kept. The vector element
// type `R` effectively defaults to a `std::basic_string_view` of `whole`'s own
// code unit; specify `R` as an owning string type (e.g. `std::string`) to make
// deep copies.
template<typename R = void, StringViewLike S>
[[nodiscard]] constexpr auto
split_lines(const S& whole_in, line_ends ends = line_ends::discard) {
  using C = char_type_of_t<S>;
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<C>, R>;
  auto whole = as_view(whole_in);
  std::vector<result_t> lines;
  std::basic_string_view<C> line;
  while (more_lines(line, whole, ends)) lines.emplace_back(line);
  return lines;
}

// Split lines of a temporary string, returning deep copies in a vector.
//
// The vector element type `R` effectively defaults to an owning string of
// `whole`'s code unit, since views into the temporary would dangle.
template<typename R = void, CharType CharT>
[[nodiscard]] constexpr auto split_lines(std::basic_string<CharT>&& whole,
    line_ends ends = line_ends::discard) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string<CharT>, R>;
  // A trivially copyable `R` is a non-owning view, which would dangle into
  // the temporary; this overload exists precisely to prevent that.
  static_assert(!std::is_trivially_copyable_v<result_t>,
      "R must be an owning string type");
  return split_lines<result_t>(std::basic_string_view<CharT>{whole}, ends);
}

#pragma endregion

}} // namespace corvid::strings::splitting
