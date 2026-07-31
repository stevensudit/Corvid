# Enums roadmap

Status and next steps for `corvid/enums`, following the registration-mechanism
migration.

## Done

- cstring_view accessor: the registration always stores names as
  independently null-terminated fields (`make_nulled`), and `enum_as_view`
  returns `cstring_view` directly, so names can be handed straight to C APIs
  (e.g. nghttp3). No separate opt-in accessor was needed.
- Hole-aware bitmask `range_length`: resolved as clean failure. A mask with
  non-contiguous valid bits is rejected at compile time, because for a holey
  mask the two honest answers (valid-value count and iteration span) diverge
  and no caller has needed either.
- Unscoped enums and `enum_as_string`: answered, no change. `enum_as_string`
  requires `ScopedEnum`; an unscoped enum already prints as its underlying
  integer, which is the intended "counts as an int" behavior.

## Deferred / decided against

The 2026-07-31 sweep of the long-parked bitmask/sequence ideas closed them
all: none ever acquired a motivating caller, and real usage runs about fifty
sequence registrations against nine bitmask files.

- Bitmask: msb-aligned bit-name lists (nothing pads with comma runs in
  practice); a binary printer ("RgB"); a compile-time sorted-map parser (same
  rationale as the reverse index below); `operator[]` / `set_at` proxy sugar
  (the whole `get_at` / `set_at` / `make_at` family has no callers outside
  the band and its test); `some()` / `all()` comparison helpers (flirts with
  plain `==` interference).
- Sequence: `std::numeric_limits` specialization (legal for program-defined
  enums, but nothing wants it); `arithmetic_enum` for homogeneous operations
  (overlaps `strong_type`; decide which mechanism owns the use case when a
  motivating type appears).
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
