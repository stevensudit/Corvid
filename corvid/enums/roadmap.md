# Enums roadmap

Status and next steps for `corvid/enums`, following the registration-mechanism
migration.

## Done: ADL registration

Registration moved from specializing the `enum_spec_v` variable template
(out-of-namespace, fully qualified) to an ADL-found `corvid_enum_spec(E*)` hook
declared in the enum's own namespace, directly under the enum. The
`enum_spec_v<E>` consumer interface and all call sites are unchanged; a
`ScopedEnum` fallback still supplies the numeric default. All production and
test registrations were migrated, the redundant namespace close/reopen
scaffolding removed, and the docs updated.

Immediate checkpoint: a full `./cleanbuild.sh` sweep to confirm the cleanup
across every target (verified piecemeal so far).

## The problem the rest of this roadmap addresses

A sparse sequence enum is registered today as one positional name list indexed
by `value - min`, so a gap of K values costs K empty comma-fields. Three costs:

- Authoring: `protocol_type` needs 256 fields to land `raw` at 255;
  `qpack_token` needs 1009 to land `priority` at 1008. The comma runs are
  hand-counted and easy to break (a recent compaction silently dropped 36 of
  protocol_type's trailing slots, shifting `raw` off 255).
- Space: the forward `names` array is sized to the value *range*, not the
  named *count* (~16 KB of mostly-empty `string_view`s for `qpack_token`).
- Reverse lookup: `string -> enum` scans the whole range array, O(range), not
  O(named-count).

Two reframes guide the fixes:

- Reverse-lookup *time* scales with N, the number of named entries; forward
  *space* scales with R, the value range. Sparse enums stress R, not N
  (qpack N ~ 64, errno N ~ 134).
- All names come from one contiguous `fixed_string` blob, so a linear scan of
  the *packed* names is cache-friendly and, at the N we have, beats binary
  search or hashing on a cold cache. Reverse lookup stays linear.

## Next

### 1. Segmented registration syntax + packed storage

Replace the single padded list with manually segmented runs and a compact
backing store.

Syntax: `'|'` is a pure segment delimiter (no leading or trailing pipe). Split
on `'|'`; a single segment (no pipe) keeps today's behavior verbatim, so
existing dense registrations are untouched. With two or more segments, each
segment's first comma-field is its absolute start value and the rest are names;
the inner delimiter stays `','`. (`'|'`, not `':'`, because pseudo-header names
begin with `':'`.) `min` and `max` are derived from the segments, so the
explicit `minseq`/`maxseq` arguments are unneeded in the segmented form.

Storage: a packed array of the named entries plus a `{start, length}` segment
table.

- Forward (enum -> string): find the segment containing the value (linear over
  the few segments), index `packed[offset + (value - start)]`. A dense enum is
  one segment, i.e. O(1) and unchanged.
- Reverse (string -> enum): linear scan of the packed names, segment by
  segment, recovering `value = segment.start + intra_offset`. Gaps never
  appear in the scan.

This kills the comma walls, drops space from O(range) to O(count), and makes
reverse lookup O(count). `protocol_type` and `qpack_token` are the witnesses.

Status: done. `make_sequence_enum_spec` detects the `'|'` form and builds the
packed array plus a `{start, length}` segment table; a dense enum is a single
segment, so existing registrations are unchanged. Segments must be listed in
ascending order and separated by more than one value (overlap, adjacency, and
single-value gaps are compile-time errors): a single-value gap costs the same as
a placeholder, so it must be written as a placeholder within one segment, not as
a split. This keeps the form canonical, lets forward lookup stop early, and
keeps the door open for a future binary search. The `Segmented` case in
`sequence_enum_test.cpp` covers forward, reverse, in-segment placeholders,
negative starts, and multi-segment ascending runs.

Both witnesses are converted, with the name<->value mapping preserved exactly
(verified against the prior dense form). `qpack_token` goes from 17957 bytes to
~2010, as 9 segments over 68 packed slots (61 names plus 7 placeholders for
single-value gaps). `protocol_type` is maintained by hand in the same style and
additionally names `tp` (29), which the dense form had left to print as `"29"`.

### 2. Self-contained bitmask lookup (layering)

The reverse path for bitmask enums parses the `"a + b + c"` combination in the
strings layer (`strings/enum_conversion.h` splits on `'+'` and ORs the pieces),
which forces `strings` to pull in the full bitmask adapter. Move that
combination parsing into the bitmask spec's `lookup`, so `spec.lookup(v, "a + b
+ c")` is self-contained. The strings reverse path then routes through
`enum_spec_v<E>.lookup` for every enum, exactly like the forward path already
does, and `strings` depends on `enums` only through the lightweight
`enum_registry.h`. Best done alongside the sequence `lookup` rewrite in item 1.

### 3. cstring_view accessor (optional)

A registration may opt into a `'\0'`-delimited backing blob, enabling an
`enum_as_cstring_view` accessor for handing names straight to C APIs (e.g.
nghttp3). Not the default: `cstring_view` is not a `string_view`, so flipping
the default return type would change `auto` deductions and break callers that
use `substr`/`remove_suffix`. The default return stays `string_view`.

## Deferred / decided against

- Binary-search reverse index: profile-gated. Cold-cache linear over a packed
  blob is competitive or better at the N we have; revisit only if a measured
  hot path with hundreds-plus names appears.
- Perfect / constexpr hashing (frozen-style): off the table until an enum has
  thousands of *names* (not range). Adds compile time and complexity for a
  guarantee we do not need.
- Aliases (several names for one value): needs a registration-format change to
  attach multiple names to a slot, plus a standalone sorted `{name, value}`
  reverse array (which defeats the compact packed form). Deferred; today one
  name per value, chosen at registration.

## Settled (no work needed)

- Sentinels (`unknown`, `invalid`): handled per registration, either by
  including the value as a name (bidirectional) or excluding it via `minseq` so
  it prints numerically and is not matched. No special marker. For example,
  `http_method::invalid` and the leading socket placeholders are excluded.
