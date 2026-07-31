# Enums roadmap

Status and next steps for `corvid/enums`, following the registration-mechanism
migration.

## Done

- cstring_view accessor: the registration always stores names as
  independently null-terminated fields (`make_nulled`), and `enum_as_view`
  returns `cstring_view` directly, so names can be handed straight to C APIs
  (e.g. nghttp3). No separate opt-in accessor was needed.

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
