# Pythonesque gap analysis

Python is batteries-included where C++ is not, and the `strings` band exists
partly to fill that gap. This is a survey of what Python offers for strings,
out of the box or in ubiquitous stdlib modules, that Corvid still lacks. It is
input for further analysis, not a commitment to build any particular item.

## Already covered (no gap despite first appearances)

- `startswith` / `endswith` / `in`: `std::string_view` has `starts_with`,
  `ends_with`, and `contains` since C++20/23, forwarded by
  [string_view_wrapper.h](string_view_wrapper.h).
- `center` / `ljust` / `rjust` / `zfill`: `std::format` fill, align, and width
  (`{:^10}`, `{:0>8}`) subsume all four for formatting, but the direct
  functions were later ruled worth shipping anyway; see the DONE entry below.
- `join`: deliberately retired with `concat_join`. `std::format("{:n}", range)`
  and `std::views::join_with` cover it, per [roadmap.md](roadmap.md). An eager
  `join(range, delim) -> std::string` one-liner remains a mild ergonomic gap,
  since format specs do not compose as fluidly as `", ".join(...)`.
- `repr()`: the `{:?}` debug spec plus [debug_escaping.h](debug_escaping.h).
- `int(s)` / `float(s)`: `parse_num` and `extract_num` in
  [conversion.h](conversion.h).
- `strip(chars)`: `trim` with a delim set in [trimming.h](trimming.h).
- `count`: `count_located` in [locating.h](locating.h).
- `replace`: `substitute` and `excise` in [locating.h](locating.h).
- `isdecimal` / `isdigit` / `isnumeric`: the trio only diverges outside ASCII
  (decimal characters, plus superscript-style digits, plus anything with a
  numeric value, respectively, each a superset of the last). Within ASCII all
  three collapse to '0' through '9', which is exactly `is_digit` in
  [cases.h](cases.h), so under the ASCII-only policy there is no gap and no
  reason to ship three names.

## Missing, and natural fits for the existing files (ASCII scope, small)

- `partition` / `rpartition`: split-once returning (head, sep, tail)
  non-destructively. `token_parser::next_delimited` is the nearest relative,
  but it is destructive and collapses "no separator found" into the last-token
  case. DONE: `string_partition` and `string_rpartition` in
  [string_partition.h](string_partition.h), sharing a `string_partition_base`
  that holds the three views. Ruling: these parameterize on the code unit,
  not on the view type, matching the rest of the band; view-templating was
  considered and rejected (it admits owning or exotic view types that would
  silently break the anchoring contract, while custom traits are already
  erased at the `as_view` boundary).
- `removeprefix` / `removesuffix`: strip-if-present. The std only has the
  unconditional count-based `remove_prefix`, so everyone writes the
  `starts_with` plus `substr` dance by hand. DONE: `trim_prefix` and
  `trim_suffix` in [trimming.h](trimming.h), named for Go's
  `strings.TrimPrefix` / `TrimSuffix`, which made the same
  exact-affix-once-if-present versus repeated-set-trim distinction; the
  Python names would collide conceptually with the std's count-based
  `remove_prefix`.
- `splitlines`: universal-newline splitting. A delim set of `{'\r', '\n'}`
  gets `\r\n` wrong (it emits an empty piece), so this genuinely needs its own
  finder. It would also be a nice shipped example of a custom `DelimFinder`
  for `piece_generator`. DONE: `extract_line`, `more_lines`, `split_lines`,
  and `line_delim_finder` in [splitting.h](splitting.h). `extract_line` is
  the destructive one-at-a-time primitive, modeled on `extract_piece`;
  `more_lines` wraps it in the fill-and-report loop driver, following the
  `basic_piece_generator::more_pieces` contract (true with a line, false at
  exhaustion) rather than the free `more_pieces` one, whose
  final-piece-with-false protocol would emit the trailing empty line;
  `split_lines` is a loop over `more_lines`. The finder is
  `DelimFinder`-conformant and plugs into `basic_piece_generator`. `split_lines` does not route through
  the generator because the Python rule that a trailing line break adds no
  trailing empty line cannot be expressed by a generator finder or filter
  (neither can tell final-empty from interior-empty). Python's `keepends` is
  the `line_ends::keep` / `discard` two-value enum, defined locally
  (at the time, `bool_enums.h` lived in the enums band, which strings cannot
  depend on; it is now in meta). ASCII scope: the exotic Python line
  breaks (`\v`, `\f`, `\x1c` through `\x1e`, `\x85`, `\u2028`, `\u2029`) are
  deliberately out.
- `rsplit` and `maxsplit`: split from the right, and split-at-most-N. The
  `piece_generator` doc explicitly names "limit how many pieces" as a
  motivating use case, but nothing shipped does it. DONE: `rextract_piece`,
  `rmore_pieces`, `rsplit`, `split_n`, and `rsplit_n` in
  [splitting.h](splitting.h), with `rfind_in` added to `basic_delim`.
  Rulings: Corvid's `rsplit` genuinely iterates from the right, returning
  parts in right-to-left encounter order (exactly `split` reversed, an
  invariant pinned in tests); Python's `rsplit` is not that (it only charges
  its split limit from the right and still presents left-to-right). Full
  Python parity is nonetheless mechanical: unbounded Python `rsplit` is
  simply `split`, and bounded Python `rsplit(sep, n)` is `rsplit_n` with the
  vector reversed (also pinned in tests). `maxsplit` is the separate
  `split_n` / `rsplit_n` pair rather than a defaulted parameter on `split`;
  at most `n` splits, and the final part is the untouched remainder (for
  `rsplit_n`, that remainder is the head). The `piece_generator` machinery is
  considered experimental
  (heavy, no known callers), so the limiting battery went on `split` proper
  rather than into a generator example.
- `translate` / `maketrans`: table-driven per-character mapping and deletion,
  with `tr(1)` semantics. `substitute` handles paired from-to lists; a
  256-entry table version is both faster for that shape and covers
  delete-sets. DECLINED: not wanted; `substitute` and `excise` already cover
  the practical shapes, and a translation table has no known caller.
- `capitalize` / `title` / `swapcase`, plus whole-string predicates
  (`isdigit`, `isalpha`, `isspace`, `isupper` over a string):
  [cases.h](cases.h) has the per-character predicates and upper/lower
  conversion only. All are trivial in the established ASCII-only style. A
  per-character `is_space` is itself missing. DONE: per-character `is_space`
  (the six ASCII whitespace characters), `StringViewLike` overloads of all
  seven predicates (true when non-empty and every code unit passes; ruling:
  unlike Python `islower` / `isupper`, uncased characters are not ignored).
  The Python ignore-uncased rule is also available by name:
  `is_python_lower` / `is_python_upper` (at least one letter, none of the
  opposite case; single pass with early out). Also included are the case
  transforms as in-place `to_*` / value-returning `as_*` pairs:
  `to_swapped` with `as_swapped` (which also has a per-character overload),
  `to_capitalized` with `as_capitalized`, and `to_titled` with `as_titled`.
  Naming ruling: `to_*` is reserved for in-place mutation and `as_*` for
  returning the converted value, so the preexisting per-character `to_upper`
  and `to_lower` were renamed to `as_upper` / `as_lower` (matching
  `as_hex_lc_digit`), with the range forms keeping their `to_*` names.
  `as_titled` keeps the Python `title` quirk (any non-letter starts a new
  word, so "they're" becomes "They'Re"), documented in the header. Companion
  ruling: the split and trim default delimiter stays a lone space (speed and
  simplicity), but [splitting.h](splitting.h) now names the full set as the
  `whitespace` / `wwhitespace` view pair, for Python-style whitespace
  splitting or trimming; a test pins the pair to `is_space` exactly so they
  cannot drift. Companion addition beyond Python parity: `ci_compare`, the
  ordering counterpart to `ci_equal`, returning `std::weak_ordering` (equal
  ignoring case is equivalent, not equal). Companion ruling: the `as_*`
  transforms deliberately stay two-pass (copy, then the in-place `to_*`);
  fusing would only save the memcpy-class copy while breaking the layering,
  and `resize_and_overwrite` remains available as a local change if a
  profile ever demands it. Later ruling: the nine dual-form predicates
  (`is_lower` through `is_printable`) were rebuilt as constexpr predicate
  objects (`details::code_unit_pred` over per-character functor structs),
  eliminating the nine duplicated string-overload bodies and making the names
  directly passable to algorithms and range adaptors
  (`std::views::filter(is_digit)`), which is pinned in tests. Call syntax is
  unchanged; `is_lc_hex_alpha` / `is_uc_hex_alpha` stay plain per-character
  functions, and the string-only `is_python_*` / `is_title` stay function
  templates.
- `isascii` / `isprintable` / `istitle`: predicates the first survey pass
  missed. DONE: in [cases.h](cases.h), per-character `is_ascii` (0 through
  0x7f) and `is_printable` (space through tilde) with the standard string
  overloads, plus the string-only `is_title`, which follows the `as_titled`
  word rule (quirk included), matching Python `istitle` exactly within ASCII;
  `is_title(as_titled(s))` holds whenever `s` has a letter, pinned in tests.
  One divergence noted in the header: Python `isascii` is true for the empty
  string, but the band's non-empty rule applies uniformly. `isidentifier` was
  not included (Python-specific rules, no caller).
- `ljust` / `rjust` / `center` / `zfill` as direct functions, promoted from
  the "Already covered" tier by ruling. DONE: [justification.h](justification.h),
  keeping the Python names because they are well known and more intuitive than
  the pad-side alternatives considered (`ljust` -> `rpad` and so on); a single
  universal padder taking an alignment was also considered and rejected. The
  names describe where the content goes, not the padding. `zfill` is
  sign-aware, exactly as in Python. One divergence, documented in the header:
  `center` places odd padding on the right, matching `std::format`'s `^` and
  `calc_padding`, while Python picks the side from the parities of the width
  and margin. Companion refactor: `calc_padding` and its `aligned` enum moved
  from `parsed_spec` in [../meta/formatting.h](../meta/formatting.h) to the
  new [../meta/padding.h](../meta/padding.h), which also absorbed
  `padded_size` from the now-deleted meta/memory.h (both are padding, of
  memory and of text); `justification.h` builds on it, since strings may
  depend on meta but not the reverse.
- `expandtabs`: tab-to-column-stop expansion. Niche, but it has no std
  equivalent at all. DONE: `expand_tabs` in [expand_tabs.h](expand_tabs.h),
  originally named indenting.h and renamed when the `textwrap` items landed:
  `expandtabs` is a `str` method while `textwrap` is a separate module, so
  housing them together would be odd. Each header's lede points at the other.
  The inline namespace is `tab_expansion`, since naming it `expand_tabs`
  would make the qualified function name ambiguous. Python semantics: a tab
  advances to the next multiple of `tab_size` (default 8), the column resets
  after `\n` or `\r`, and a `tab_size` of zero deletes tabs. Naming ruling:
  plain `expand_tabs` rather than an `as_*` form, since there is no in-place
  `to_*` counterpart to distinguish it from (the length changes, so in-place
  makes no sense).

## Missing, module-sized (Python stdlib beyond `str`)

- `textwrap`: `dedent` is the standout, since it is what makes multi-line raw
  string literals usable, and C++ raw strings have exactly the same
  indentation problem. `wrap`, `fill`, `indent`, and `shorten`
  (truncate-with-ellipsis) round it out. DONE: [textwrap.h](textwrap.h) ships
  all five. Rulings: the functions live in the non-inline `textwrap`
  namespace, since names as broad as `wrap` and `fill` must stay qualified (a
  `struct textwrap` full of statics was the considered alternative), and the
  `TextWrapper` class is replaced by the `wrap_options` aggregate, taken by
  `wrap` / `fill` / `shorten` and designed for designated initializers
  (`{.width = 40}`), with every member defaulting to Python's default. Two
  knobs are deliberately omitted: `fix_sentence_endings` (an English-specific
  heuristic Python itself defaults to off) and `break_on_hyphens` (English
  hyphenation rules encoded in a regex). Otherwise `wrap` faithfully ports
  CPython's `TextWrapper._wrap_chunks`, including the fiddly `max_lines`
  placeholder endgame, and the tests pin expectations generated from CPython
  3.12 running with those two knobs off. Divergences, each documented in the
  header: `dedent` recognizes universal line breaks where Python's is
  "\n"-only (a blank CRLF line zeroes Python's margin); nothing raises, so a
  zero `width` degrades to one code unit of content per line and an
  unfittable placeholder is emitted anyway, where Python throws `ValueError`;
  `max_lines` is zero-means-unlimited rather than `None`; `tabsize` is
  spelled `tab_size`. `indent` matches Python exactly (universal line breaks
  on both sides), with the default predicate prefixing only lines that have
  non-whitespace content. Polish round: `wrap` was decomposed into
  `details::` helpers (`munge_whitespace`, `chunk_runs`, `take_line`,
  `truncate_line`, `emit_line`) after clang-tidy flagged its cognitive
  complexity; every function is constexpr and the tests pin compile-time
  evaluation with `static_assert` (results can be computed and compared in
  constant evaluation, though a `std::string` result cannot escape into a
  constexpr variable); each function's comment shows an input/output example.
  Compiler gotcha uncovered by those asserts: cl miscomputes a ternary of
  `std::basic_string` prvalues in constant evaluation (silent wrong value at
  compile time, correct at runtime), so `munge_whitespace` spells it as
  if/else, with a one-line workaround comment. Beyond Python: a consteval
  `dedent` overload takes the literal as a `fixed_string` template argument
  and returns a dedented `fixed_string` constant (two passes over the
  runtime implementation: one for the size, one to fill), so an indented raw
  string literal costs nothing at runtime; the header lede presents it as
  the preferred form. Second round, per user: the dedent machinery avoids
  `std::string` entirely (shared `details::` helpers `dedent_margin`,
  `dedent_size`, and `dedent_fill` work on views and a caller buffer; the
  runtime overload is now size-then-fill over the same helpers, so the two
  forms cannot drift), and the consteval overload returns a `cstring_view`
  into the `dedented` inline `fixed_string` variable template, giving static
  storage deduplicated across translation units plus a zero-terminated
  `c_str` for C interfaces. QoI round: `dedent_fill` takes an exactly-sized
  `std::span` (via `type_identity_t`, so a string converts at the call site)
  and asserts it lands on the span's end; the runtime overload sizes its
  result through `no_zero` rather than zero-filling first. A consteval
  `indent` counterpart was considered and rejected: static indentation is
  directly expressible in the literal itself (`dedent` preserves per-line
  indent beyond the common margin), so unlike `dedent` it has no
  source-formatting problem to solve.
- `format_map`: named-field substitution from a runtime mapping. DONE, and
  beyond Python: the `enable_format` wrapper in [enable_format.h](enable_format.h)
  makes any keyed collection formattable with per-field key lookup
  (`{0:city}`), nested value specs (`{0:temperature:.2f}`), runtime-selected
  keys (`{0:{1}}`), variant values, and an opt-in stand-in for missing keys.
  A companion `enable_format` specialization makes `std::variant` itself
  formattable. Full design notes in [roadmap.md](roadmap.md) stage 4.
- `fnmatch`: shell wildcard matching (`*`, `?`, `[abc]`). A small state
  machine with no dependencies, frequently reinvented. DONE:
  [fnmatch.h](fnmatch.h) ships `fnmatch` (ASCII case folded), `fnmatchcase`
  (exact), `filter`, and `filterfalse` (Python itself only added the last in
  3.14), in a non-inline `fnmatch` namespace per the `textwrap` precedent.
  The matcher is the classic iterative star-backtracking walk over views: no
  translation to a regex, no allocation, fully constexpr, code-unit generic.
  The wildcard language is pinned against CPython in the tests (3.12 and
  3.14 agree on every case; `filter` and `filterfalse` were pinned against
  3.14, where the latter was added),
  bracket-set edge cases included (`[]]`, `[!]]`, an unterminated `[` as a
  literal, a leading or trailing `-` as a member, reversed ranges as empty,
  no escape character). Nothing in std compares: `std::filesystem` iterates
  directories but has no pattern language, and `std::regex` is a different
  and slower tool. One deliberate divergence, documented in the header:
  Python's `fnmatch` case rule is OS-dependent via `os.path.normcase` (which
  on Windows also rewrites `/` to `\`), while ours is deterministic
  everywhere, with `fnmatch` folding ASCII case and never touching
  separators. `filter` / `filterfalse` return lazy `std::views::filter`
  pipelines that compose with other range adaptors. `translate` was not
  ported, since it returns a regex string and so presumes the engine the
  survey declines to build on.
- `PurePath.match` / `full_match`: glob's path-aware matching logic with the
  file access removed. Wildcards do not cross separators; matching is per
  component over the `fnmatch` kernel. DONE: [pure_path.h](pure_path.h)
  ships `match` (a relative pattern matches a trailing run of components,
  right-anchored; an anchored one must cover the whole path; `**` is not
  special and degrades to `*`) and `full_match` (whole path, with a `**`
  component matching any run, per Python 3.13+), each with a `_case` exact
  variant per the deterministic-case ruling, in a non-inline `pure_path`
  namespace. `std::filesystem::path` does the part it is good for, parsing
  under the host's path grammar (components taken in generic format, with
  "." and trailing-empty components dropped to mirror `PurePath`); the
  component walk is the `fnmatch` star-backtracking algorithm lifted from
  code units to components. Everything is pinned against CPython 3.14,
  including the root subtleties, which fall out of CPython's regex-over-
  string implementation but are reproduced here in component terms: a
  wildcard never matches a bare root, and a leading `**` in a relative
  pattern absorbs an anchor only together with what follows (a full
  drive-plus-root pair on its own, a lone root or lone drive only along
  with at least one real component); embedded `**` degrades to `*` exactly
  as 3.14 does. Divergence, same shape as elsewhere: nothing raises, so an
  empty pattern (or "."), which Python rejects with `ValueError`, matches
  nothing. Layering ruling on record: including `<filesystem>` from the
  strings band is fine, since the layering rule is about not referencing
  Corvid's filesys band, not std headers; if filesys later wraps
  `<filesystem>` and `pure_path.h` would be cleaner over that wrapper, the
  header can move up a band then. Drive-letter tests are Windows-only (the
  host grammar is the parser); the rest of the pins are grammar-neutral and
  hold on both platforms.
- `shlex` / `csv`-style quote-aware splitting: a shipped quote-respecting
  `DelimFinder` / `PieceFilter` pair. The splitting machinery was explicitly
  designed for this ("use an internal buffer to unescape"), but no battery is
  included.
- `difflib`: `get_close_matches` and edit distance are the useful kernel, for
  did-you-mean suggestions. Full `SequenceMatcher` diffs are a bigger lift.
- `binascii` / `base64`: per-digit hex helpers exist in
  [conversion.h](conversion.h), but whole-buffer `hexlify` / `unhexlify` does
  not, and base-64 exists only as a websocket-handshake helper in
  [base_64.h](../proto/misc/base_64.h). Promoting and generalizing those into
  the strings band would fit the general-code-to-library rule.

## Missing for lack of demand (not excluded by policy)

- `string.Template`: `$name` substitution from a map, with a
  `safe_substitute` mode. Demoted from the module-sized list by ruling: the
  substitution machinery is subsumed by the `enable_format` wrapper (named
  lookup from a keyed collection, runtime-selected keys, a stand-in for
  missing keys), and what remains distinct is narrow. `$name` placeholders
  live in plain text, so literal braces need no doubling, and
  `safe_substitute` leaves an unknown placeholder verbatim for a later pass.
  That serves templates authored outside the source (config files,
  user-supplied messages), and no such consumer has shown up.
- `glob`: filesystem-walking wildcard expansion, considered when `fnmatch`
  shipped. Ruling: out of the strings band, since it mixes walking the
  filesystem with filtering; if it ever lands, it is a filesys-band battery
  layered on the `fnmatch` kernel, to revisit if filesys becomes
  cross-platform rather than a Linux wrapper. The pure matching piece was
  promoted to the module-sized list as the `PurePath.match` item.
- Everything Unicode: `casefold`, `unicodedata` normalization, `encode` /
  `decode`, UTF-8 validation and transcoding. This is the single biggest
  divergence from Python, whose `str` is Unicode-native. Nothing rules it
  out; there just has not been much call for it yet, so it stays deferred
  until a consumer shows up.
- `re`: Python's regex is arguably its most-used string battery. `std::regex`
  exists but is notoriously slow, and filling the gap properly means wrapping
  or writing a real engine (RE2 or CTRE territory), which is a scope decision
  rather than a checklist item.

## Priority read

The three that callers would likely miss first (`removeprefix` /
`removesuffix`, `partition`, `dedent`) have all shipped, as have `splitlines`,
the rest of `textwrap`, `fnmatch`, and the `PurePath.match` / `full_match`
pair. Of what remains, a quote-aware splitter would exercise machinery that
already exists.
