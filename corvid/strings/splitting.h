// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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
#include "strings_shared.h"
#include "delimiting.h"
#include "opt_string_view.h"

namespace corvid::strings { inline namespace splitting {

//
// Split
//

// For all split functions, `delim` defaults to " " and can be specified as any
// set of characters.

// Extract next delimited piece destructively from `whole`.
//
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto
extract_piece(std::string_view& whole, delim d = {}) {
  auto pos = std::min(whole.size(), d.find_in(whole));
  auto part = whole.substr(0, pos);
  whole.remove_prefix(std::min(whole.size(), pos + 1));
  return R{part};
}

// Extract next delimited piece into `part`, removing it from `whole`.
//
// Returns true so long as there's more work to do.
// Specify R as `std::string` to make a deep copy.
template<typename R>
[[nodiscard]] constexpr bool
more_pieces(R& part, std::string_view& whole, delim d = {}) {
  auto all = whole.size();
  part = extract_piece<R>(whole, d);
  return part.size() != all;
}

// TODO: Probably kill the hardcoded vector-only versions below.

// Split all pieces by delimiters and return parts in vector.
//
// Does not omit empty parts.
// Specify R as `std::string` to make a deep copy.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto split(std::string_view whole, delim d = {}) {
  std::vector<R> parts;
  std::string_view part;
  for (bool more = !whole.empty(); more;) {
    more = more_pieces(part, whole, d);
    parts.emplace_back(part);
  }
  return parts;
}

// Split a temporary string by delimiters, making deep copies of the parts.
[[nodiscard]] inline constexpr auto split(std::string&& whole, delim d = {}) {
  return split<std::string>(std::string_view(whole), d);
}

// Concept to detect whether a type is a piece_generator.
//
// This requires it to be moveable, be constructable from `std::string_view`,
// and to support the `more_pieces` method.
//
// A piece generator is plugged into a split adapter to perform split
// operations. This modularized approach makes it possible to define delimiters
// any way you like, strip padding, or skip empty pieces. Using a stateful
// objects also allows you to do things like limit how many pieces are
// returned, alternate the delimiters, or make a mutable copy of each so as to
// unescape.
//
// Note: If the piece generator returns a view into a copy of the piece, then
// the output must copy it into a `std::string` (or equivalent) to avoid
// dangling references. Alternately, you can choose to provide memory backing
// for all of the pieces, relaxing this requirement.
template<typename T>
concept PieceGenerator = requires(T t, std::string_view s) {
  requires std::is_move_constructible_v<T>;
  {
    T{s}
  };
  {
    t.more_pieces(s)
  };
};

// Implements the PieceGenerator concept to provide a working example that is
// composable enough to handle many common cases.
//
// Treats input as `opt_string_view`, returning a piece when `empty` but not
// `null`. The end state is `null`.
struct piece_generator {
  // Callback to find next delimiter. What constitutes a delimiter is baked
  // into the lambda. Likewise, it is always fed the remainder of the string,
  // so there's no need for a `pos` parameter. Returns a pair of positions for
  // the start and end of the delimiter. If not found, the first value is
  // `npos` and the second is unused.
  using find_delim_cb =
      std::function<std::pair<size_t, size_t>(std::string_view)>;

  // Callback to filter out a piece. Returns a `null` value to skip. Can strip
  // out padding, or even use an internal buffer to unescape.
  using filter_piece_cb = std::function<opt_string_view(std::string_view)>;

  // This is technically not a requirement for a PieceGenerator, but it's a
  // good idea, especially if you have state that needs to be cleared in
  // between calls. The return allows passing `piece_generator.reset(x)` to the
  // `split` function.
  //
  // TODO: Consider supporting this as an extended concept, such that there's a
  // version of `split` that takes an instance and a `whole`.
  auto& reset(std::string_view new_whole) {
    whole = new_whole;
    return *this;
  }

  // Stateless static helper, which can be easily reused from your own class.
  //
  // Fills `part` with the next piece from `whole` and returns `true`. On
  // failure, such as when there's nothing left to parse, returns `false`.
  [[nodiscard]] static bool more_pieces(std::string_view& part,
      opt_string_view& whole, find_delim_cb finder, filter_piece_cb filter) {
    for (;;) {
      if (whole.null()) return false;
      auto [pos, next] = finder(whole);
      auto opt_part = filter(whole.substr(0, pos));
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
  [[nodiscard]] bool more_pieces(std::string_view& part) {
    return more_pieces(part, whole, finder, filter);
  }

  opt_string_view whole = {};
  find_delim_cb finder = [](std::string_view s) {
    auto pos = s.find(' ');
    return std::pair{pos, pos + 1};
  };
  filter_piece_cb filter = [](std::string_view s) { return s; };
};

// Split `whole` using the PieceGenerator and return parts in vector.
template<PieceGenerator PG = piece_generator, typename R = std::string_view>
[[nodiscard]] constexpr auto split_gen(std::string_view whole) {
  return split<R>(PG{whole});
}

// Use this version when you want to set additional generator parameters or
// need access to it afterwards.
template<typename R = std::string_view>
[[nodiscard]] constexpr auto split(PieceGenerator auto pgen) {
  std::vector<R> parts;
  std::string_view part;
  while (pgen.more_pieces(part)) parts.emplace_back(part);
  return parts;
}

// TODO: Write helper in "locating.h" to take a `location` and return a
// begin/end pair for the thing found. This is needed for adapting multi-value
// `locate`. Maybe promote it to a type, like position_range?

// TODO: Write split_adapter version that works with any container that can
// construct from a string_view (as well as `std::pair` and `std::tuple`).

// TODO: Basic plan...
// We need a concept for whether a type is a target for splitting into. We can
// check for a container by looking for `const_iterator`, but we want to filter
// out `std::initializer_list` because it could lead to ambiguity. For most
// containers, we need to ensure that we construct its `value_type` from
// `std::string_view`, but for keyed containers, this has to be true for both
// `key_type` and `mapped_type`. We can reuse some of the logic from
// concat_join here.
//
// The key is that we create an object that supports begin/end, such that its
// iterator returns a std::string_view. It also implicitly converts to anything
// fitting the target concept above, as well as pair.
//
// For maps, which are containers of pairs, we insert even pieces as keys and
// set odd pieces as values of the previously-inserted key.

// TODO: Optimizations (here and elsewhere)
// - If filling a vector of strings, instead fill a vector of string_view and
// then convert it all at once after reserving.

// https://github.com/abseil/abseil-cpp/blob/master/absl/strings/internal/str_split_internal.h

}} // namespace corvid::strings::splitting
