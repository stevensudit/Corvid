# CLAUDE.md

This folder holds the modern formatting path: thin `std::formatter`
specializations and format wrappers built on C++23 `std::format`. See
[roadmap.md](roadmap.md) for the staged plan and rationale.

## Core decisions

- Build on `std::formatter<T, CharT>` and `std::format`. These are
  code-unit-generic, so a single `CharT`-templated formatter serves narrow and
  wide alike. Do not hand-roll parallel wide variants.
- The legacy `char`-only path is [../utils/concat_join.h](../utils/concat_join.h)
  (`append` / `join` / `join_opt` / `append_escaped`). Do not extend it or port
  it to wide; it stays as-is and remains load-bearing (json writer, interval).
- Format wrappers COMPOSE the wrapped value; they do not inherit std types. We
  cannot specialize `std::formatter` for std containers (C++23 defines those),
  so wrap and format the wrapper, the way
  [../core/string_view_wrapper.h](../core/string_view_wrapper.h) composes rather
  than inherits.
- Registered-enum names are stored as `char`; the enum formatter widens
  `char` -> `CharT` at format time. No multi-type name storage.
