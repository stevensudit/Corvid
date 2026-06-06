# Formatting roadmap

Status and plan for `corvid/strings/format`: modernizing Corvid string
formatting onto C++23 `std::format` rather than porting the bespoke
`concat_join.h` machinery to wide code units.

## Why std::format, not a wide concat_join

`std::formatter<T, CharT>` and `std::format` / `std::wformat` are already
code-unit-generic, so anything built on them is wide-for-free: no parallel
narrow and wide implementations, no `concat_join` wide port at all. Leaning on
the standard also means less bespoke code and composable format specs (`{:n}`,
`{:?}`, `{::?}`) we would otherwise reinvent.

## What std::format already covers

Verified with the project toolchain (clang-22, libc++, `-std=c++23
-fexperimental-library`):

- ranges: `{}` -> `[1, 2, 3]`; `{:n}` -> `1, 2, 3` (no brackets)
- maps: `{}` -> `{1: "a", 2: "b"}` (keyed and quoted already)
- tuples and pairs: `(1, 2.5, 'x')`, `(1, "hi")`
- debug quote/escape: `{:?}` -> `"he said \"hi\"\n"`
- nested per-element spec: `{::?}` -> `["a", "he\"llo"]`
- wide: `std::format(L"{}", vec)` works

So the old `join_opt` surface is largely subsumed: flat -> `{:n}`, quoted ->
`{:?}`, keyed -> map formatting, nested -> `{::?}`.

## Stages

### 1. Enum formatter (done)

Shipped in [enum_formatter.h](enum_formatter.h), tested by
[../../../tests/enum_formatter_test.cpp](../../../tests/enum_formatter_test.cpp).

`std::format` does not format enums at all. A single
`std::formatter<E, CharT>` partial specialization, constrained on
`corvid::ScopedEnum`, covers all three flavors by delegating to
`corvid::strings::enum_as_string` (see
[../utils/enum_conversion.h](../utils/enum_conversion.h)): a registered
sequence enum prints by name, a registered bitmask enum prints as its
"a + b + c" combination, and an unregistered scoped enum prints as its numeric
underlying value. It inherits `std::formatter<basic_string_view<CharT>, CharT>`
to reuse the standard fill/align/width/precision spec.

Additive, no std conflict (std formats no enums), and `CharT`-templated so wide
comes along. Enum-name storage stays `char`: there is no multi-type name
storage and no justification for one. The wide formatter widens the ASCII name
`char` -> `CharT` at format time.

### 2. corvid::fmt wrappers (scope to confirm after stage 1)

For behaviors std::format cannot express, or cannot legally attach to a
non-owned type, wrap the value and format the wrapper. We cannot specialize
`std::formatter<std::vector<...>>` and friends because C++23 already defines
them. Follow cppreference's `QuotableString` pattern in shape, but COMPOSE the
wrapped value (the way [../core/string_view_wrapper.h](../core/string_view_wrapper.h)
does), do NOT inherit the std type as that example does.

Candidate wrappers:

- `json(x)`: JSON-exact escaping per RFC 8259. `{:?}` uses C++ debug rules
  (`\u{..}`, no `/`) and leaves map keys unquoted, so it is not valid JSON.
- quoted / optional / null / pointer "null" semantics: `std::optional` has no
  formatter before C++26, so small Corvid wrappers fill the gap.

### 3. Migration (separate, later)

As the std::format path matures, parts of the char legacy layer may retire.
That is a distinct migration, tracked separately, and gated on the std path
being a full replacement for each consumer.

## Keep concat_join

Do NOT delete or port [../utils/concat_join.h](../utils/concat_join.h). It is
load-bearing:

- the JSON writer uses `append_escaped`
  ([../../proto/misc/json_parser.h](../../proto/misc/json_parser.h))
- `interval` registers an `append_join_with` override and uses `join_opt`
  ([../../containers/utils/interval.h](../../containers/utils/interval.h))

It stays the `char` legacy layer. `json_parser` keeps its own writer
regardless.

## Deferred / decided against

- Exact JSON escaping is a wrapper concern (stage 2), not a change to `{:?}`.
- `char8_t` / UTF-8 names transcoded to UTF-16/32 for wider targets: the only
  future reason to revisit `char` name storage. Corvid has no UTF-8 support
  anywhere yet, so this is explicitly deferred.
