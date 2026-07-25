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
#include <concepts>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <utility>

#include "../meta/concepts.h"
#include "string_literals.h"
#include "cases.h"

#pragma region fnmatch

namespace corvid::strings { namespace fnmatch {

// Fnmatch
//
// Counterpart of Python's `fnmatch` module: shell-style wildcard matching
// over strings, with `fnmatch`, `fnmatchcase`, `filter`, and `filterfalse`.
//
// In Python these are scoped under the module, so they have rather broad
// names. As a result, we chose to place it in an `fnmatch` namespace that is
// not inline. The intended way to use it is through a namespace alias, as in
// `namespace fnmatch = corvid::strings::fnmatch;`, after which calls read as
// they do in Python: `fnmatch::fnmatch`, `fnmatch::filter`, and so on.
//
// The wildcard language is Python's, pinned against CPython in the tests:
// - `*` matches any run of code units, including an empty one.
// - `?` matches exactly one code unit.
// - `[abc]` matches one code unit from the set; `[a-z]` ranges are inclusive,
//   and a reversed range like `[z-a]` is empty. `[!...]` negates the set. A
//   `]` first in the set is a literal member, as is a `-` first or last. `*`
//   and `?` are literal inside a set, and an unterminated `[` is a literal.
// - There is no escape character; a backslash is an ordinary code unit.
//
// The whole name must match (anchoring at both ends is implicit), and this is
// string matching, not path globbing: `*` and `?` cross path separators and
// newlines like any other code unit. For path-aware matching, where
// wildcards stop at separators, see "pure_path.h".
//
// One deliberate divergence: Python's `fnmatch` inherits its case rule (and a
// slash-to-backslash rewrite) from the host OS via `os.path.normcase`. Here
// the behavior is the same on every platform: `fnmatch` folds ASCII case on
// both sides and never touches separators, while `fnmatchcase` compares
// exactly.
//
// One omission: Python's `translate`, which converts a pattern into regex
// source, was not ported. CPython matches by compiling that regex; here the
// matcher interprets the pattern directly, so there is no regex to expose,
// and `std::regex` is not a foundation worth compiling to anyway.
//
//   assert(fnmatch::fnmatch("Notes.TXT", "*.txt"));
//   assert(fnmatch::fnmatchcase("data_07.csv", "data_[0-9][0-9].csv"));
//   for (auto& name : fnmatch::filter(names, "*.cpp")) use(name);

namespace details {

// Locate the index of the ']' closing the bracket set that opens at `pos`,
// returning `npos` if the set is unterminated.
//
// Mirrors Python's parse: a leading '!' and then a leading ']' are consumed
// as content before the search for the closer begins, so "[]]" and "[!]]"
// are complete sets while "[]" and "[!]" are not.
template<CharType CharT>
[[nodiscard]] constexpr size_t
locate_class_end(std::basic_string_view<CharT> pat, size_t pos) noexcept {
  auto ndx = pos + 1;
  if (ndx < pat.size() && pat[ndx] == CharT{'!'}) ++ndx;
  if (ndx < pat.size() && pat[ndx] == CharT{']'}) ++ndx;
  while (ndx < pat.size() && pat[ndx] != CharT{']'}) ++ndx;
  return ndx < pat.size() ? ndx : npos;
}

// Match pattern code unit `pc` against name code unit `nc`, folding ASCII
// case when `fold` is set.
template<CharType CharT>
[[nodiscard]] constexpr bool
match_char(CharT pc, CharT nc, bool fold) noexcept {
  return fold ? as_lower(pc) == as_lower(nc) : pc == nc;
}

// Test `c` for membership in the bracket set whose `content` sits between
// the brackets, folding ASCII case when `fold` is set.
//
// A leading '!' negates the result. A '-' with a code unit on each side is
// an inclusive range (empty when reversed); anywhere else it is a literal
// member. Folding lowercases `c` and each member, including range endpoints,
// matching Python's normalize-then-match behavior.
template<CharType CharT>
[[nodiscard]] constexpr bool is_class_member(
    std::basic_string_view<CharT> content, CharT c, bool fold) noexcept {
  bool negated{};
  if (!content.empty() && content.front() == CharT{'!'}) {
    negated = true;
    content.remove_prefix(1);
  }
  if (fold) c = as_lower(c);
  bool found{};
  for (size_t ndx{}; ndx < content.size();) {
    auto lo = content[ndx];
    auto hi = lo;
    if (ndx + 2 < content.size() && content[ndx + 1] == CharT{'-'}) {
      hi = content[ndx + 2];
      ndx += 3;
    } else {
      ++ndx;
    }
    if (fold) {
      lo = as_lower(lo);
      hi = as_lower(hi);
    }
    if (lo <= c && c <= hi) found = true;
  }
  return found != negated;
}

// Match all of `name` against wildcard `pat`, folding ASCII case when `fold`
// is set.
//
// The classic iterative wildcard walk: a '*' is tentatively matched empty,
// and on a later mismatch we backtrack to the most recent '*', growing what
// it swallowed by one code unit. Worst case is O(name * pat), typical
// patterns are linear, and no allocation ever happens.
template<CharType CharT>
[[nodiscard]] constexpr bool do_match(std::basic_string_view<CharT> name,
    std::basic_string_view<CharT> pat, bool fold) noexcept {
  size_t name_ndx{};
  size_t pat_ndx{};
  auto star_pat = npos;
  size_t star_name{};
  while (name_ndx < name.size()) {
    if (pat_ndx < pat.size()) {
      const auto pc = pat[pat_ndx];
      if (pc == CharT{'*'}) {
        star_pat = ++pat_ndx;
        star_name = name_ndx;
        continue;
      }
      if (pc == CharT{'?'}) {
        ++pat_ndx;
        ++name_ndx;
        continue;
      }
      // A '[' opening a complete set consumes through its ']'; otherwise the
      // code unit, '[' included, is a literal.
      auto next_pat = pat_ndx + 1;
      bool hit{};
      const auto cls_end =
          (pc == CharT{'['}) ? locate_class_end(pat, pat_ndx) : npos;
      if (cls_end != npos) {
        hit = is_class_member(pat.substr(pat_ndx + 1, cls_end - pat_ndx - 1),
            name[name_ndx], fold);
        next_pat = cls_end + 1;
      } else {
        hit = match_char(pc, name[name_ndx], fold);
      }
      if (hit) {
        pat_ndx = next_pat;
        ++name_ndx;
        continue;
      }
    }
    if (star_pat == npos) return false;
    pat_ndx = star_pat;
    name_ndx = ++star_name;
  }
  while (pat_ndx < pat.size() && pat[pat_ndx] == CharT{'*'}) ++pat_ndx;
  return pat_ndx == pat.size();
}

} // namespace details

#pragma region Matching

// Match `name` against shell wildcard `pattern`, ignoring ASCII case.
//
// For exact-case matching, use `fnmatchcase`. Both arguments must be
// string-like with the same code-unit type.
template<StringViewLike N, StringViewLike P>
requires std::same_as<char_type_of_t<N>, char_type_of_t<P>>
[[nodiscard]] constexpr bool
fnmatch(const N& name, const P& pattern) noexcept {
  return details::do_match(as_view(name), as_view(pattern), true);
}

// Match `name` against shell wildcard `pattern`, case-sensitively.
template<StringViewLike N, StringViewLike P>
requires std::same_as<char_type_of_t<N>, char_type_of_t<P>>
[[nodiscard]] constexpr bool
fnmatchcase(const N& name, const P& pattern) noexcept {
  return details::do_match(as_view(name), as_view(pattern), false);
}

#pragma endregion
#pragma region Filtering

// Return a lazy view of the elements of `names` that match `pattern`, under
// `fnmatch` rules (ASCII case folded).
//
// The result is a `std::views::filter` pipeline, so it composes with other
// range adaptors and evaluates on demand. It holds a view of `pattern`,
// which must outlive it, and borrows `names` unless an rvalue container is
// moved in. Collect with `std::ranges::to` when an owning result is wanted.
template<std::ranges::viewable_range R, StringViewLike P>
[[nodiscard]] constexpr auto filter(R&& names, const P& pattern) {
  return std::forward<R>(names) |
         std::views::filter([pattern = as_view(pattern)](const auto& name) {
           return fnmatch(name, pattern);
         });
}

// Return a lazy view of the elements of `names` that do not match `pattern`:
// the complement of `filter`, with the same lifetime rules.
template<std::ranges::viewable_range R, StringViewLike P>
[[nodiscard]] constexpr auto filterfalse(R&& names, const P& pattern) {
  return std::forward<R>(names) |
         std::views::filter([pattern = as_view(pattern)](const auto& name) {
           return !fnmatch(name, pattern);
         });
}

#pragma endregion

}} // namespace corvid::strings::fnmatch

#pragma endregion
