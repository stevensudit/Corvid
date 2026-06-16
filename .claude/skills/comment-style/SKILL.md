---
name: comment-style
description: How to structure class- and function-level doc comments in this C++ codebase. Functions open with an imperative sentence ("Split the string."), types open with a descriptive noun phrase, plus the paragraph, whitespace, and pragma-region layout the headers use. Use when writing or reviewing comments on declarations.
---

# Comment style

How declarations are documented in Corvid. These rules are derived from the
existing headers; match them when adding or reviewing comments. They build on
the quoting and ASCII rules in `CLAUDE.md`.

## Function and method comments

Open with one **imperative** sentence naming what the function does, ending
with a period:

```cpp
// Extract next delimited piece destructively from `whole`.
// Split all pieces by delimiters and return parts in vector.
// Locate the first instance of a single `value` in `s`, starting at `pos`.
```

Imperative mood, not third person: "Extract", not "Extracts"; "Split", not
"Splits". (See `corvid/strings/splitting.h`, `locating.h`.)

Describe the **contract, not the implementation**: what it does, its
preconditions, what the return value means, and how it fails. Scale length to
complexity. A trivial accessor needs one short line or none; a subtle function
earns several paragraphs.

Put extra detail in later paragraphs, each separated by a blank `//` line. The
return value, when it needs explaining, is usually its own follow-on sentence,
and there "Returns ..." / "returning ..." is correct (the imperative rule is
only about the opening sentence):

```cpp
// Extract next delimited piece into `part`, removing it from `whole`.
//
// Returns true so long as there's more work to do.
// Pass an owning string type as `part` to make a deep copy.
```

(`corvid/strings/splitting.h`.)

## Type comments (class / struct / enum)

Open with a **descriptive noun phrase** saying what the thing *is*, not an
imperative verb:

```cpp
// Thread-safe fixed-capacity object pool with LIFO slot reuse.
// Delimiter wrapper.
```

(`corvid/containers/utils/object_pool.h`, `corvid/strings/delimiting.h`.)

Follow with paragraphs covering how to use it, its semantics, and caveats, each
separated by a blank `//` line. A longer block may introduce a list with a
labeled lead-in line and `-` bullets:

```cpp
// The precise semantics vary depending upon context:
// - When splitting, checks for any of the characters.
// - When joining, appends the entire string.
// - When manipulating braces, treated as an open/close pair.
```

## Whitespace and layout

- The doc comment hugs its declaration: no blank line between the comment and
  the thing it documents.
- Blank lines separate distinct declarations or groups, not every line. Pack
  closely related declarations together without blanks (an overload set, a
  cluster of type aliases, a run of trivial one-line members); a single comment
  often documents the whole group. The blank line goes between such groups.
- Separate comment paragraphs with a blank `//` line. Write each paragraph as a
  single logical line; do not hand-wrap. clang-format reflows it at the column
  limit (`ReflowComments`, 79 columns).
- `#pragma region` / `#pragma endregion` blocks:
  - A blank line after the `#pragma region X` opener, and a blank line before
    the `#pragma endregion`.
  - No blank line between a `#pragma endregion` and the next `#pragma region`;
    the two pragma lines are adjacent.
  - When a region begins at an access-specifier change, the `#pragma region X`
    line comes first and the `public:`/`private:` sits immediately on the next
    line, with no blank line between them and the region's content following
    directly: the access specifier takes the place of the usual blank line
    after the opener. The class's opening access specifier is the exception, it
    precedes the first `#pragma region` (which then keeps its blank line).
  - Pragmas are moved to column 0 by clang-format.

```cpp
#pragma region Construction

  // The default delimiter is a single space.
  constexpr basic_delim() noexcept : base{view_t{&delim_space<char_t>, 1}} {}

#pragma endregion
#pragma region Locating
```

(`corvid/strings/delimiting.h`.)

Region-with-access example: the opening `public:` precedes the first region,
while a later region that switches access hugs its specifier.

```cpp
class circular_buffer final {
public:
#pragma region Types

  using value_type = T;

#pragma endregion
#pragma region iterator_t
private:
  template<typename CB>
  class iterator_t { ... };

#pragma endregion
};
```

(`corvid/containers/core/circular_buffer.h`.)

## Region organization

Wrap declarations in `#pragma region <name>` / `#pragma endregion` blocks at two
levels:

- **File scope** (inside the namespace): each top-level declaration (class,
  struct, enum, related free-function group) gets its own region named after the
  symbol, e.g. `#pragma region quic_conn`.
- **Class scope**: group logically related members under nested regions, e.g.
  `Construction`, `Accessors`, `IO`, `Expiry`, `Handlers`, `Helpers`, `Data
  members`, or domain-specific names (`Stream lifecycle`, `Flow control
  feedback`). Don't use plain `// section` header comments for this; promote them
  to regions.

## Quoting and punctuation

These apply to every comment:

- Backticks for code symbols (types, vars, functions, enums, templates,
  constants): `` `whole` ``, `` `npos` ``. Single quotes for characters: `'@'`.
  Double quotes for strings and filenames: `"config.json"`.
- Name a function as `do_thing`, not `do_thing()`. The backticks quote the
  symbol itself, not a call. Add parentheses only to show specific arguments,
  e.g. `foo(nullptr)`.
- Plain 7-bit ASCII. No em dashes, and no `--` or a lone `-` standing in for
  one: rewrite with a comma, a colon, parentheses, or two sentences.
