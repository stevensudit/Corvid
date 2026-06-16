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

#include "../meta/concepts.h"
#include "../meta/crossplatform.h"
#include "strings_shared.h"
#include "delimiting.h"
#include "opt_string_view.h"

namespace corvid::strings { inline namespace splitting {

#pragma region Split

// For all split functions, `delim` defaults to " " and can be specified as any
// set of characters.

// Extract next delimited piece destructively from `whole`.
//
// The return type `R` effectively defaults to a `std::basic_string_view` of
// `whole`'s own code unit; specify `R` as an owning string type (e.g.
// `std::string`) to make a deep copy.
template<typename R = void, CharType C>
[[nodiscard]] constexpr auto extract_piece(std::basic_string_view<C>& whole,
    std::type_identity_t<basic_delim<C>> d = {}) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string_view<C>, R>;
  auto pos = std::min(whole.size(), d.find_in(whole));
  auto part = whole.substr(0, pos);
  whole.remove_prefix(std::min(whole.size(), pos + 1));
  return result_t{part};
}

// Extract next delimited piece into `part`, removing it from `whole`.
//
// Returns true so long as there's more work to do.
// Pass an owning string type as `part` to make a deep copy.
template<typename R, CharType C>
[[nodiscard]] constexpr bool
more_pieces(R& part, std::basic_string_view<C>& whole,
    std::type_identity_t<basic_delim<C>> d = {}) {
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
  std::basic_string_view<C> whole{as_view(whole_in)};
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
template<typename R = void, CharType C>
[[nodiscard]] constexpr auto split(std::basic_string<C>&& whole,
    std::type_identity_t<basic_delim<C>> d = {}) {
  using result_t =
      std::conditional_t<std::is_void_v<R>, std::basic_string<C>, R>;
  return split<result_t>(std::basic_string_view<C>(whole), d);
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
  { t.more_pieces(s) };
};

#pragma endregion
#pragma region piece_generator

// Finds the next delimiter in the remainder of the string. What constitutes a
// delimiter is baked into the callable. It is always fed the remainder, so
// there's no need for a `pos` parameter. Returns a pair of positions for the
// start and end of the delimiter; if not found, the first is `npos` and the
// second is unused.
template<typename F, typename Char>
concept DelimFinder =
    std::invocable<F, std::basic_string_view<Char>> &&
    std::convertible_to<std::invoke_result_t<F, std::basic_string_view<Char>>,
        std::pair<size_t, size_t>>;

// Filters a candidate piece. Returns a `null` value to skip it. Can strip
// padding, or even use an internal buffer to unescape.
template<typename F, typename Char>
concept PieceFilter =
    std::invocable<F, std::basic_string_view<Char>> &&
    std::convertible_to<std::invoke_result_t<F, std::basic_string_view<Char>>,
        basic_opt_string_view<void, Char>>;

// Default delimiter finder: splits on a single space.
template<CharType Char>
struct default_delim_finder {
  constexpr std::pair<size_t, size_t> operator()(
      std::basic_string_view<Char> s) const {
    auto pos = s.find(Char(' '));
    return {pos, pos + 1};
  }
};

// Default piece filter: passes every piece through unchanged.
template<CharType Char>
struct default_piece_filter {
  constexpr basic_opt_string_view<void, Char> operator()(
      std::basic_string_view<Char> s) const {
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
template<CharType Char = char,
    DelimFinder<Char> Finder = default_delim_finder<Char>,
    PieceFilter<Char> Filter = default_piece_filter<Char>>
struct basic_piece_generator {
#pragma region Member types

  // The code unit and its view / optional-view types.
  using char_t = Char;
  using view_t = std::basic_string_view<Char>;
  using opt_view_t = basic_opt_string_view<void, Char>;

#pragma endregion
#pragma region Reset

  // This is technically not a requirement for a PieceGenerator, but it's a
  // good idea, especially if you have state that needs to be cleared in
  // between calls. The return allows passing `piece_generator.reset(x)` to the
  // `split` function.
  auto& reset(view_t new_whole) {
    whole = new_whole;
    return *this;
  }

#pragma endregion
#pragma region Pieces

  // Stateless static helper, which can be easily reused from your own class.
  //
  // Fills `part` with the next piece from `whole` and returns `true`. On
  // failure, such as when there's nothing left to parse, returns `false`.
  [[nodiscard]] static bool more_pieces(view_t& part, opt_view_t& whole,
      const DelimFinder<Char> auto& finder,
      const PieceFilter<Char> auto& filter) {
    for (;;) {
      if (whole.null()) return false;
      const auto [pos, next] = std::pair<size_t, size_t>(finder(whole));
      const opt_view_t opt_part = filter(whole.substr(0, pos));
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
  [[nodiscard]] bool more_pieces(view_t& part) {
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
template<CharType Char>
basic_piece_generator(std::basic_string_view<Char>)
    -> basic_piece_generator<Char>;
template<CharType Char, DelimFinder<Char> Finder, PieceFilter<Char> Filter>
basic_piece_generator(std::basic_string_view<Char>, Finder, Filter)
    -> basic_piece_generator<Char, Finder, Filter>;

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

}} // namespace corvid::strings::splitting
