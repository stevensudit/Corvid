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
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "string_literals.h"
#include "fnmatch.h"

#pragma region pure_path

namespace corvid::strings { namespace pure_path {

// Pure path matching.
//
// Counterpart of Python `pathlib`'s `PurePath.match` and `full_match`,
// providing glob-style wildcard matching over paths, without ever touching the
// filesystem.
//
// In Python these are scoped under the module, so they have rather broad
// names. As a result, we chose to place it in a `pure_path` namespace that is
// not inline. The intended way to use it is through a namespace alias, as in
// `namespace pure_path = corvid::strings::pure_path;`.
//
// The division of labor: `std::filesystem::path` does the part it is good
// for, parsing a path into components under the host's path grammar (which
// already covers the lexical `PurePath` surface), and each component is then
// matched through the "fnmatch.h" kernel.
//
// The per-component wildcard language is therefore exactly that of `fnmatch`,
// but because a wildcard only ever sees one component, `*` and `?` never cross
// a separator. For flat string matching where they do, use "fnmatch.h"
// instead.
//
// `match` mirrors Python: a relative pattern matches a trailing run of
// components (right-anchored), while an anchored pattern (one with a root
// name or root directory) must cover the whole path, and `**` is not
// special, degrading to `*`.
//
// `full_match` must cover the whole path, and a `**` component matches any run
// of components; a `**` embedded in a larger component degrades to `*`, as
// CPython's does. Both are pinned against CPython 3.14 in the tests, including
// the root subtleties: a wildcard never matches a bare root, and a `**` can
// only absorb an anchor together with what follows it (a lone `/` never, a
// full drive-plus-root pair on its own).
//
// Parsing mirrors `PurePath`: "." components, duplicate separators, and a
// trailing separator all normalize away, while ".." stays literal. Two
// deliberate divergences, in line with the rest of the band: the case rule
// is deterministic rather than flavor-dependent (`match` and `full_match`
// fold ASCII case, `match_case` and `full_match_case` compare exactly), and
// nothing raises (an empty pattern, which Python rejects with `ValueError`,
// simply matches nothing).
//
//   assert(pure_path::match("src/strings/pure_path.h", "strings/*.h"));
//   assert(pure_path::full_match("src/strings/pure_path.h", "src/**/*.h"));

namespace details {

using path_char = std::filesystem::path::value_type;
using component = std::basic_string<path_char>;
using component_view = std::basic_string_view<path_char>;

// Whether `comp` is the recursive wildcard: exactly "**".
[[nodiscard]] constexpr bool is_double_star(component_view comp) noexcept {
  return comp.size() == 2 && comp.front() == path_char{'*'} &&
         comp.back() == path_char{'*'};
}

// Parse `p` into its matchable components, in the generic format (so
// separators compare uniformly), dropping the "." components and the empty
// component a trailing separator produces, which mirrors `PurePath`'s
// parsing. A root name and root directory arrive as leading components.
[[nodiscard]] inline std::vector<component> parse_components(
    const std::filesystem::path& p) {
  std::vector<component> parts;
  for (const auto& elem : p) {
    auto comp = elem.generic_string<path_char>();
    if (comp.empty() || (comp.size() == 1 && comp.front() == path_char{'.'}))
      continue;
    parts.push_back(std::move(comp));
  }
  return parts;
}

// Count the leading components of `p` that are anchors: its root name and
// root directory.
[[nodiscard]] inline size_t count_anchors(const std::filesystem::path& p) {
  return (p.has_root_name() ? 1U : 0U) + (p.has_root_directory() ? 1U : 0U);
}

// Match one component pair through `fnmatch`.
[[nodiscard]] inline bool
match_component(component_view name, component_view pat, bool fold) noexcept {
  return fold ? fnmatch::fnmatch(name, pat) : fnmatch::fnmatchcase(name, pat);
}

// Match the component sequence `parts` against the pattern sequence `pats`,
// where a `**` component matches any run of zero or more components and
// every other component matches through the `fnmatch` kernel.
//
// The same star-backtracking walk as `fnmatch`, lifted one level: from code
// units within a component to components within a path.
[[nodiscard]] inline bool do_walk(std::span<const component> parts,
    std::span<const component> pats, bool fold) {
  size_t part_ndx{};
  size_t pat_ndx{};
  auto star_pat = npos;
  size_t star_part{};
  while (part_ndx < parts.size()) {
    if (pat_ndx < pats.size()) {
      if (is_double_star(pats[pat_ndx])) {
        star_pat = ++pat_ndx;
        star_part = part_ndx;
        continue;
      }
      if (match_component(parts[part_ndx], pats[pat_ndx], fold)) {
        ++pat_ndx;
        ++part_ndx;
        continue;
      }
    }
    if (star_pat == npos) return false;
    pat_ndx = star_pat;
    part_ndx = ++star_part;
  }
  while (pat_ndx < pats.size() && is_double_star(pats[pat_ndx])) ++pat_ndx;
  return pat_ndx == pats.size();
}

// Right-anchored match, per Python `PurePath.match`: the trailing components
// of `path` for a relative pattern, the whole path for an anchored one.
[[nodiscard]] inline bool do_match(const std::filesystem::path& path,
    const std::filesystem::path& pattern, bool fold) {
  const auto parts = parse_components(path);
  const auto pats = parse_components(pattern);
  if (pats.empty()) return false;
  if (count_anchors(pattern)) {
    if (pats.size() != parts.size()) return false;
  } else if (pats.size() + count_anchors(path) > parts.size()) {
    // A relative pattern matches only real components, never an anchor.
    return false;
  }
  for (auto ndx = 0UZ; ndx < pats.size(); ++ndx)
    if (!match_component(parts[parts.size() - 1 - ndx],
            pats[pats.size() - 1 - ndx], fold))
      return false;
  return true;
}

// Whole-path match, per Python `PurePath.full_match`, with `**` recursion.
[[nodiscard]] inline bool do_full_match(const std::filesystem::path& path,
    const std::filesystem::path& pattern, bool fold) {
  const auto parts = parse_components(path);
  auto pats = parse_components(pattern);
  // Consecutive `**` components mean the same as one.
  const auto dup =
      std::ranges::unique(pats, [](const component& a, const component& b) {
        return is_double_star(a) && is_double_star(b);
      });
  pats.erase(dup.begin(), dup.end());
  if (pats.empty()) return false;
  // A bare `**` matches every path, anchored or not.
  if (pats.size() == 1 && is_double_star(pats.front())) return true;
  const auto path_anchors = count_anchors(path);
  const auto pat_anchors = count_anchors(pattern);
  if (pat_anchors) {
    // An anchored pattern requires the same anchor structure, matching
    // anchors, and the rest walks normally.
    if (pat_anchors != path_anchors ||
        pattern.has_root_name() != path.has_root_name())
      return false;
    for (auto ndx = 0UZ; ndx < pat_anchors; ++ndx)
      if (!match_component(parts[ndx], pats[ndx], fold)) return false;
    return do_walk(std::span{parts}.subspan(pat_anchors),
        std::span{pats}.subspan(pat_anchors), fold);
  }
  const auto rest = std::span{parts}.subspan(path_anchors);
  if (!path_anchors) return do_walk(rest, pats, fold);
  // A relative pattern against an anchored path: only a leading `**` can
  // reach across the anchor, and it must absorb the anchor together with
  // what follows: a full drive-plus-root pair stands on its own, but a lone
  // root (or lone drive) only falls to `**` along with at least one real
  // component. Pinned against CPython, where this drops out of the string
  // form: the `**` segment must end at a separator with content before it.
  if (!is_double_star(pats.front())) return false;
  if (path_anchors == 2) return do_walk(rest, pats, fold);
  return !rest.empty() && do_walk(rest.subspan(1), pats, fold);
}

} // namespace details

#pragma region Matching

// Match `path` against `pattern`, Python `PurePath.match` style, ignoring
// ASCII case.
//
// A relative pattern matches a trailing run of components; an anchored one
// must cover the whole path. `**` degrades to `*`. For exact-case matching,
// use `match_case`; for whole-path matching with `**` recursion, use
// `full_match`.
[[nodiscard]] inline bool match(const std::filesystem::path& path,
    const std::filesystem::path& pattern) {
  return details::do_match(path, pattern, true);
}

// Match `path` against `pattern`, Python `PurePath.match` style,
// case-sensitively.
[[nodiscard]] inline bool match_case(const std::filesystem::path& path,
    const std::filesystem::path& pattern) {
  return details::do_match(path, pattern, false);
}

#pragma endregion
#pragma region Full matching

// Match all of `path` against `pattern`, Python `PurePath.full_match`
// style, ignoring ASCII case.
//
// The pattern must cover the whole path, with a `**` component matching any
// run of components. For exact-case matching, use `full_match_case`.
[[nodiscard]] inline bool full_match(const std::filesystem::path& path,
    const std::filesystem::path& pattern) {
  return details::do_full_match(path, pattern, true);
}

// Match all of `path` against `pattern`, Python `PurePath.full_match`
// style, case-sensitively.
[[nodiscard]] inline bool full_match_case(const std::filesystem::path& path,
    const std::filesystem::path& pattern) {
  return details::do_full_match(path, pattern, false);
}

#pragma endregion

}} // namespace corvid::strings::pure_path

#pragma endregion
