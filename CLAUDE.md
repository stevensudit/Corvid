# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## General

If the user's intent is ever unclear, always ask before proceeding.

When a function can fail, always consider returning some status indication instead of being void.

## Build System

CMakeLists.txt lives in `tests/` only — there is none at the project root. Build directory: `tests/build/`.

```sh
# Clean build + run all tests
./cleanbuild.sh                        # default (libc++)
./cleanbuild.sh libstdcpp              # use libstdc++ instead
./cleanbuild.sh tidy                   # also run clang-tidy

# Build and run a single test (pass the .cpp filename as first arg)
./cleanbuild.sh strings_test.cpp       # build + run only that target

# Format all source files (required before committing)
./format_all.sh
```

## Code Style

- Run `./format_all.sh` after code changes and before `git commit`. Claude's Edit/Write tools bypass IDE save hooks, so this must be done manually.
- When referencing code locations, use markdown link format: `[filename.ext:line](filename.ext#Lline)`
- In comments, use quoting as follows: backticks for literal names (types, variables, functions, enums, template parameters, constants), single quotes for individual characters, and double quotes for strings and filenames. E.g., `` `store_id_t::invalid` ``, `` `is_alive()` ``, `` `OWN_COUNT` ``; `'@'` for the at-sign character; `"config.json"` for a filename.
- In comments and documentation, use only plain 7-bit ASCII characters. Never use Unicode arrows (use `->` not the Unicode rightwards arrow), em dashes, curly quotes, or any other non-ASCII characters.
- Never use a trailing underscore for private methods. If a private helper needs to be distinguished from a public function, prefix it with `do_` instead. Example: public `close()`, private `do_close()`, not `close_()`.
- Prefer uniform initialization (`int i{4};`, `foo(int option) : option_{option}`) over function-call syntax (`: option_(option)`) or assignment syntax (`int i = 4;`). Do not use `{}` for variables that have a default constructor (clang-tidy flags this), but do use it for variables that lack one (such as `int` or `bool`).
- Use `std::chrono` literal suffixes (`1s`, `500ms`, `100us`, etc.) rather than explicit constructor calls (`std::chrono::seconds{1}`). The file-level `using namespace std::chrono_literals;` already brings these in for library headers; add it in test files as needed.

## Git Workflow

- Use `git add .` when committing (to include any user-made changes alongside Claude's)
- Use `git switch` instead of `git checkout`
- PRs are squash-merged — respond to review with additional commits, not amends
- When writing PR descriptions, verify every claim against the actual code — do not rely on general knowledge about design patterns or algorithms. For example, a "timing wheel" in this codebase is single-level; do not assume hierarchical.
- When asked to push a branch as a new PR with an appropriate description, first review all changes and flag any errors (bugs, incorrect documentation, style violations, etc.) before writing the description and pushing.

## Testing

Framework: custom minitest (`tests/minitest.h`). All classes have test coverage in `tests/` — tests may be per-class, grouped by related classes, or cover an entire subfolder (e.g., all ECS headers tested together). Check `tests/` before asking about coverage.

## TODO File

The root `TODO` file tracks enhancement requests and design decisions. When encountering TODO comments in code that represent enhancements (not immediate work), move them there. Rewrite positional references ("below", "above") to name the specific function or class.

Corvid is a **header-only C++23 library** (no external dependencies beyond libc++). All headers live under `corvid/`.

## Reuse Library Utilities

Corvid provides many utilities that replace direct calls on standard types. Always search the library before reaching for `std::string`, `std::string_view`, or container member functions directly. Examples:

- Map/set lookup: prefer `find_opt` (returns `std::optional`) over `find` + end-check.
- String searching and splitting: prefer the parsers and locators in `corvid/strings/` over `std::string::find`, `substr`, etc.

When writing new code in `corvid/proto/` or elsewhere in the library, scan the relevant headers first to avoid reimplementing what already exists.

## Non-Obvious Locations

- `npos` / base string position types: `corvid/strings/string_base.h`
- `npos_choice`: `corvid/strings/locating.h`
