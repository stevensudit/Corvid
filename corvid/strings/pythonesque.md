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
  (`{:^10}`, `{:0>8}`) subsume all four.
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
  the `line_ends::keep` / `discard` two-value enum, defined locally because
  the strings band cannot depend on `bool_enums.h`. ASCII scope: the exotic Python line
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
  delete-sets.
- `capitalize` / `title` / `swapcase`, plus whole-string predicates
  (`isdigit`, `isalpha`, `isspace`, `isupper` over a string):
  [cases.h](cases.h) has the per-character predicates and upper/lower
  conversion only. All are trivial in the established ASCII-only style. A
  per-character `is_space` is itself missing.
- `expandtabs`: tab-to-column-stop expansion. Niche, but it has no std
  equivalent at all.

## Missing, module-sized (Python stdlib beyond `str`)

- `textwrap`: `dedent` is the standout, since it is what makes multi-line raw
  string literals usable, and C++ raw strings have exactly the same
  indentation problem. `wrap`, `fill`, `indent`, and `shorten`
  (truncate-with-ellipsis) round it out. Probably the highest
  value-per-effort item on this list.
- `string.Template`: `$name` substitution from a map, with a
  `safe_substitute` mode. Small, self-contained, and a common real need for
  config expansion and message templating.
- `fnmatch`: shell wildcard matching (`*`, `?`, `[abc]`). A small state
  machine with no dependencies, frequently reinvented.
- `shlex` / `csv`-style quote-aware splitting: a shipped quote-respecting
  `DelimFinder` / `PieceFilter` pair. The splitting machinery was explicitly
  designed for this ("use an internal buffer to unescape"), but no battery is
  included.
- `difflib`: `get_close_matches` and edit distance are the useful kernel, for
  did-you-mean suggestions. Full `SequenceMatcher` diffs are a bigger lift.
- `binascii` / `base64`: per-digit hex helpers exist in
  [conversion.h](conversion.h), but whole-buffer `hexlify` / `unhexlify` does
  not, and base-64 exists only as a websocket-handshake helper in
  [base-64.h](../proto/misc/base-64.h). Promoting and generalizing those into
  the strings band would fit the general-code-to-library rule.

## Missing for lack of demand (not excluded by policy)

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

`removeprefix` / `removesuffix`, `partition`, and `dedent` are the three that
callers would likely miss first. `splitlines` and a quote-aware splitter
would exercise machinery that already exists.
