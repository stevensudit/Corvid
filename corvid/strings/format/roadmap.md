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
`corvid::ScopedEnum`, covers all three flavors by writing straight into the
format context's output iterator through `corvid::strings::append_enum` (see
[../utils/enum_conversion.h](../utils/enum_conversion.h)): a registered
sequence enum prints by name, a registered bitmask enum prints as its
"a + b + c" combination, and an unregistered scoped enum prints as its numeric
underlying value.

No intermediate string: the formatter wraps `ctx.out()` in
`output_iterator_appendable<It, SrcChar, DestChar>` (added to
[../core/targeting.h](../core/targeting.h), wired into the `AppendTarget`
concepts in [../../meta/concepts.h](../../meta/concepts.h)). It is an append
target generic in its input code unit `SrcChar` (matched by the concepts via an
`append_char_type` member), converting each unit to `DestChar` on the way out:
identity when the units match, a value-preserving widen when narrower. The enum
case is `char` in, `CharT` out; a later wide-content formatter (quoted strings)
can be `CharT` in, `CharT` out. Real multibyte transcoding (UTF-8 to UTF-16) is
out of scope. This is reusable infrastructure: any later formatter can append
into `ctx.out()` the same way.

Additive, no std conflict (std formats no enums), and `CharT`-templated so wide
comes along. Enum-name storage stays `char`: there is no multi-type name
storage and no justification for one; the output target does the ASCII `char`
-> `CharT` widening at format time.

The `?` debug spec is supported by a streaming quoting/escaping target
([debug_escaping.h](debug_escaping.h)): emit the opening quote, run
`append_enum` through an escaper that applies the standard's C++ debug rules
per code unit, then the closing quote. No temporary, since escaping is a
per-unit transform. This was verified necessary: registration ALLOWS names with
embedded quotes, backslashes, and control characters, and naive quote-wrapping
would leave them unescaped (invalid output). With `?` plus `set_debug_format`,
enums quote consistently inside the std range and map formatters: `{::?}` ->
`["red", "green"]`, and `map<int, E>` -> `{1: "red"}`.

Still deferred: fill, align, width, precision. Those depend on the rendered
length, so they would need a materialized string to pad; revisit only if a
caller needs it. Numeric width is already covered by formatting `*e` (or
`std::to_underlying(e)`), which routes to the standard integer formatter.

### 2. Formatter support for Corvid value types

Goal: broad `std::formatter` coverage for the Corvid classes that benefit, so
anything formattable composes inside the std range, map, and tuple formatters.
This replaces the earlier `json()` wrapper plan. JSON is out of scope: the
parser at [../../proto/misc/json_parser.h](../../proto/misc/json_parser.h) owns
JSON parsing and writing, and its `json_writer` already does RFC-correct
escaping, quoted keys, non-finite-float handling, and a trusted-bytes bypass. A
second JSON renderer here would just reinvent it. The `concat_join` "JSON" was
really `{:?}`-style debug escaping with non-JSON rules, which `std::format` now
provides directly.

#### string_view_wrapper children

`opt_string_view`, `cstring_view`, and `enum_name` all derive from
`string_view_wrapper<Child, Char>`, and the base forwards `begin` and `end`, so
today they are ranges of `char`: `std::format("{}", ov)` is claimed by the std
range formatter and prints `['h', 'e', ...]`. A single concept-constrained
`std::formatter<Child, Char>` matching the wrapper children fixes this for all
of them at once.

It inherits `std::formatter<std::basic_string_view<Char>, Char>` to reuse the
full spec grammar (fill, align, width, precision, `?`), and in `format()`
converts the wrapper to its `string_view` and delegates. Regular `{}` is pure
delegation. The one addition: in `{:?}` debug mode a null wrapper (null is
distinct from empty, the reason the type exists) renders as `(null)` unquoted
instead of `""`. So content prints as `"text"`, empty as `""`, and null as
`(null)`: three visibly distinct debug outputs. Unquoted `(null)` cannot be
confused with a string whose contents are those letters, which prints
`"(null)"`.

The marker honors fill, align, width, and precision: a second
`std::formatter<basic_string_view>` member captures the spec with its `?`
stripped (parsed in `parse` via a local context) and formats the marker through
it, reusing the base's padding while bypassing its debug quoting. The one
combination it cannot serve is a dynamic width or precision (`{:{}?}`), whose
argument is bound to the real parse context that a local re-parse cannot read.
Rather than silently drop the padding and corrupt tabular output, `parse`
rejects that spec with a `std::format_error` (a compile error for a literal
format string). The rejection is value-independent, by design: a spec whose
validity depends on whether the value happens to be null would be a trap, so
`{:{}?}` is rejected for null and non-null wrappers alike. Format the
underlying `view()` for dynamic-width debug on a known non-null wrapper.

#### fixed_string

`basic_fixed_string` cannot be null, so its formatter is pure delegation with
no debug branch. Define it at the bottom of
[../core/fixed_string.h](../core/fixed_string.h): call the value's `view()` and
forward to `std::formatter<std::basic_string_view<Char>, Char>`.

#### containers

With the string and enum leaves formatting, the std range, map, tuple, and pair
formatters cover most Corvid containers for free: format an element type and the
container comes along. What is left is container types that are not plain ranges
or want custom rendering. `interval`
([../../containers/utils/interval.h](../../containers/utils/interval.h)) would
need its own formatter, but it is low priority: little-used, and originally a
proof of concept.

### 3. Retire concat_join (migration)

`concat_join` never met its original goal. It handles basic types, but its
ad-hoc register-a-type extension mechanism was never broadly adopted. Broad
`std::formatter` coverage makes it redundant: anything that benefits gets a
formatter instead. Once each consumer moves to the std path, `concat_join`
retires. A few pieces may be worth salvaging as a simple, fast path for the
spots that already use it that way, but the file as a whole is slated for
removal. This is gated on the std path being a full replacement per consumer.

## concat_join: frozen until its consumers migrate

Until stage 3, do NOT extend or port
[../utils/concat_join.h](../utils/concat_join.h). It is still load-bearing, and
these are the migration targets:

- the JSON writer uses `append_escaped`
  ([../../proto/misc/json_parser.h](../../proto/misc/json_parser.h))
- `interval` registers an `append_join_with` override and uses `join_opt`
  ([../../containers/utils/interval.h](../../containers/utils/interval.h))

New formatting goes through `std::format`; `concat_join` gets no new code.
`json_parser` keeps its own writer regardless.

## Deferred / decided against

- Exact JSON escaping is out of scope for the format submodule entirely;
  `json_parser.h` owns JSON.
- `char8_t` / UTF-8 names transcoded to UTF-16/32 for wider targets: the only
  future reason to revisit `char` name storage. Corvid has no UTF-8 support
  anywhere yet, so this is explicitly deferred.
