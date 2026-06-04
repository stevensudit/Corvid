# Enums roadmap

Status and next steps for `corvid/enums`, following the registration-mechanism
migration.

## 1. cstring_view accessor (optional)

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
