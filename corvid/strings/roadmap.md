# Formatting roadmap

Status and plan for `corvid/strings`: modernizing Corvid string
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

Shipped in [enum_formatter.h](../enums/enum_formatter.h), tested by
[../../../tests/enum_formatter_test.cpp](../../tests/enum_formatter_test.cpp).

`std::format` does not format enums at all. A single
`std::formatter<E, CharT>` partial specialization, constrained on
`corvid::ScopedEnum`, covers all three flavors by writing straight into the
format context's output iterator through `corvid::strings::append_enum` (see
[../enums/enum_conversion.h](../enums/enum_conversion.h)): a registered
sequence enum prints by name, a registered bitmask enum prints as its
"a + b + c" combination, and an unregistered scoped enum prints as its numeric
underlying value.

No intermediate string: the formatter wraps `ctx.out()` in
`output_iterator_appendable<It, SrcChar, DestChar>` (added to
[targeting.h](targeting.h), wired into the `AppendTarget`
concepts in [../../meta/concepts.h](../meta/concepts.h)). It is an append
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
parser at [../../proto/misc/json_parser.h](../proto/misc/json_parser.h) owns
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
[fixed_string.h](fixed_string.h): call the value's `view()` and
forward to `std::formatter<std::basic_string_view<Char>, Char>`.

#### containers

With the string and enum leaves formatting, the std range, map, tuple, and pair
formatters cover most Corvid containers for free: format an element type and the
container comes along. What is left is container types that are not plain ranges
or want custom rendering. See the work list below.

#### Work list

The value types under `strings/` and `containers/` that should format, to go
down in order. "Done" means a tested formatter exists; "auto" means an existing
formatter already covers it.

Done / covered:

- [x] `opt_string_view`, `cstring_view`: wrapper formatter, tested.
- [x] `delim` (`basic_delim`, [delimiting.h](delimiting.h)): a
  `string_view_wrapper` child, so the wrapper formatter already covers it (auto).
  Regression test in `strings_test.cpp` (`DelimFormats`).

Strings:

- [x] `fixed_string` (`basic_fixed_string`,
  [fixed_string.h](fixed_string.h)): own formatter forwarding
  `view()`, per the `fixed_string` section above. Not a wrapper child, cannot be
  null; like the wrappers it exposes `begin`/`end`, so it also sets
  `format_kind = disabled`. Tested in `fixed_string_test.cpp`.

Containers needing a custom formatter (std gets these wrong or will not touch
them):

- [x] `interval` ([../../containers/utils/interval.h](../containers/utils/interval.h)):
  custom formatter plus `format_kind = disabled` (it is iterable, so the std
  range formatter would otherwise enumerate every value). Regular `{}` shows the
  closed `[min, max]` (`[]` empty, `[invalid]` reversed); debug `{:?}` shows the
  raw half-open underlying `[begin, end)`. Narrow only. Tested in
  `interval_test.cpp`. (Also refactored off `std::pair` inheritance to
  composition with a conversion operator, which the debug path reuses.)
- [x] `fixed_bitset` ([../../containers/core/fixed_bitset.h](../containers/core/fixed_bitset.h)):
  renders the `std::bitset` string (`'0'`/`'1'`, MSB first), verified equal to
  `std::bitset::to_string()`. Streams the bits one at a time to the output (no
  buffer the size of the bit count, which would dwarf the N/8-byte instance),
  plus `format_kind = disabled` because its iterator yields set-bit positions,
  not bits. Three mutually exclusive representations: default bit string; `#`
  lists the ascending set-bit indexes (`[1, 2]`, numeric, bit 0 = LSB); `?`
  (debug) dumps the `word_t` words in hex, most significant first, no `0x`. No
  `set_debug_format` (a range of bitsets shows the bit strings, not auto-quoted
  or hex). No width/fill yet (addable later, the length is known). Narrow only.
  Tested in `fixed_bitset_test.cpp`. (Note: `std::bitset` itself has no
  `std::formatter`
  in this libc++, and `fixed_bitset` has no `to_string` yet, a standing TODO; if
  added, the formatter should delegate to it.)
- [x] `strong_type` ([../../containers/core/strong_type.h](../containers/core/strong_type.h)):
  own formatter inheriting `std::formatter<T, CharT>` and forwarding `value`,
  the modern replacement for its `operator<<`. Constrained on
  `std::formattable<T, CharT>`, so it exists only when the underlying formats,
  and narrow or wide comes along with the inherited spec grammar. It
  conditionally forwards `begin`/`end`, so it also sets `format_kind = disabled`
  to avoid the enumerate-vs-forward ambiguity when the underlying is a range (a
  no-op for a non-range underlying). Tested in `containers_test.cpp`
  (`[StrongType]` Extended).
- [ ] `enum_variant` ([../../containers/core/enum_variant.h](../containers/core/enum_variant.h)):
  std formats no variant; print the active alternative (`tag: value`).
- [x] `interned_value` ([../../containers/utils/intern.h](../containers/utils/intern.h)):
  own formatter inheriting `std::formatter<T, CharT>`. Plain `{}` forwards to
  the interned value's formatter (honoring its full spec); debug `{:?}` instead
  renders the `(value, id)` pair through the std pair formatter, so the value's
  debug quoting comes along and the id prints as its numeric underlying
  (promoted past `char`-sized types). Both modes read the value, so an empty
  `interned_value` is a precondition violation, as it is for `value`. Tested in
  `intern_table_test.cpp`.
- [x] `optional_ptr` ([../../containers/core/optional_ptr.h](../containers/core/optional_ptr.h)):
  own formatter inheriting the pointee's `std::formatter`, forwarding `value()`
  with its full spec (so `{:?}` debugs the pointee when supported). A null
  `optional_ptr` renders the unquoted `(null)` marker, parallel to the
  `opt_string_view` null handling (std formats no `optional` before C++26).
  Unlike `opt_string_view`, the marker is not padded: fill/align/width apply
  only to a present pointee. Not a range, so no `format_kind` disabling. Tested
  in `optional_ptr_test.cpp`.
- [x] `custom_handle` ([../../containers/core/custom_handle.h](../containers/core/custom_handle.h)):
  own formatter inheriting the `element_type`'s `std::formatter`, dereferencing
  a live handle and forwarding with its full spec; a null handle (equal to
  `null_v`) renders the unquoted `(null)` marker, exactly like `optional_ptr`
  (marker unpadded). Not a range, so no `format_kind` disabling. Tested in
  `containers_test.cpp` (`[CustomHandleTest]` Format). Note the element must
  itself be formattable, so an enum `element_type` (the file-descriptor case)
  needs the enum formatter in scope at the call site.
- [x] `indirect_hash_key` / `indirect_map_key`
  ([../../containers/core/indirect_key.h](../containers/core/indirect_key.h)):
  each acts like its referenced key, so its formatter inherits the key's
  `std::formatter<T, CharT>` and forwards the `key` reference, honoring the
  key's full spec grammar. No null state. Tested in `containers_test.cpp`
  (`[IndirectKey]`).
- [ ] Lower value: `own_ptr` (pointee or address, questionable).

Containers already free via std ranges:

- [x] `circular_buffer` ([../../containers/core/circular_buffer.h](../containers/core/circular_buffer.h)):
  formats as `[...]` via the std range formatter, in logical front-to-back
  order. This needed an iterator fix, not a formatter: `iterator_t` advertised
  `forward_iterator_tag` but did not model even `input_iterator`, so the type
  was not a `std::ranges::range` and would not format. Two defects, both fixed:
  no default constructor (not `semiregular`, so it failed `sentinel_for`) and
  non-const `operator*`/`operator->` (failed `indirectly_readable`). It now
  models `forward_range`. Tested in `circular_buffer_test.cpp` (`Format`).
- [x] `enum_vector` ([../../containers/utils/enum_vector.h](../containers/utils/enum_vector.h)):
  free as a plain sequence through the std range formatter, narrow and wide, no
  code. Tested in `containers_test.cpp` (`[EnumVector]`). A custom formatter to
  render enum keys (`{red: 1, green: 2}`) instead of a bare list remains
  optional and unimplemented.

Excluded (not value types to print): `any_strings` (a `std::variant` alias,
awkward to format), `pos_range` / `location` (marginal debug structs), and the
utility, RAII, algorithm, comparator, and allocator types (`targeting`
appenders, `token_parser`, `splitting` generators, `ostream_redirector`,
`charconv` result structs, `trimming`, `arena_allocator`, `hash_combiner`,
`scoped_value`, `transparent`, `opt_find`, `object_pool`, `no_zero`).

Two patterns recur, worth factoring once: disabling `format_kind` for any
iterable type that should print as a summary rather than a sequence (`interval`,
range-backed `strong_type`), and "forward to the underlying type's formatter"
(`fixed_string`, `strong_type`, `interned_value`, `optional_ptr`).

#### Status: paused pending a forwarding helper

Stage 2 is paused here. Every Corvid type that materially benefits now formats:
the string wrappers and `fixed_string`, the enum formatter, `interval`,
`fixed_bitset`, `strong_type`, `interned_value`, `optional_ptr`,
`custom_handle`, and the indirect keys, with `circular_buffer` and
`enum_vector` coming along as ranges.

What is left is a long tail of small wrappers whose formatter would all be the
same trivial shape: forward an underlying scalar to its std formatter. For
example, `os_file` ([../../filesys/os_file.h](../filesys/os_file.h)) is an
owning fd wrapper whose handle is an `int`, so its formatter would just forward
that number to the integer formatter. Each one is only a few lines, but they
are near-identical lines, and one hand-written `std::formatter` specialization
per type does not scale.

So before adding more, build the "forward to the underlying type's formatter"
helper noted just above: a small CRTP base or generator that, given an
accessor, synthesizes the inherit-and-forward formatter, reducing a new
forwarder to a single declaration instead of a full specialization. Factor the
`format_kind`-disabling pattern at the same time. Revisit this tail once that
helper exists, or sooner if a specific type needs to print.

### 3. Retire concat_join (done)

`concat_join` has been removed. It never met its original goal: it handled the
basic types, but its ad-hoc register-a-type extension mechanism was never
broadly adopted, and broad `std::formatter` coverage made it redundant. Its
only non-test consumers were migrated off it first:

- `ast_pred` ([../../lang/ast_pred.h](../lang/ast_pred.h)): the `append`
  calls became `std::string::operator+=`.
- the JSON writer
  ([../../proto/misc/json_parser.h](../proto/misc/json_parser.h)): `append`
  calls became `+=`, integers route through `append_num`
  ([conversion.h](conversion.h)), and `append_escaped` plus its
  `needs_escaping` helper were ported into the writer's own `detail` namespace,
  so the writer now owns its JSON escaping. The writer is `std::string`-only;
  the old `AppendTarget` / `std::ostream` genericity that rode along with
  `concat_join` is gone.

`interval` carried only a stale leftover include, which was dropped. The
escaping helpers were the one piece worth keeping, and they moved into
`json_parser` rather than surviving as a shared utility.

## Deferred / decided against

- Exact JSON escaping is out of scope for the format submodule entirely;
  `json_parser.h` owns JSON.
- `char8_t` / UTF-8 names transcoded to UTF-16/32 for wider targets: the only
  future reason to revisit `char` name storage. Corvid has no UTF-8 support
  anywhere yet, so this is explicitly deferred.
