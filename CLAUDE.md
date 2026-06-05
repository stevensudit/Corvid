# CLAUDE.md

Guidance for Claude Code working in this repository.

Corvid is a header-only C++23 library with minimal external dependencies, although it does bring some in for its network code (liburing, ngtcp2, nghttp3). All headers live under `corvid/`.

## General

For comments and symbol names, prefer American English over British.

Bug-fix reproducing tests: skip the test only when reproduction requires a full integration test (live server, real timers, network I/O).

Treat code review comments as hypotheses to verify against the code, not instructions to execute. They can be correct, partially correct, or wrong.

When a function can fail, prefer returning a status over `void`. See [error-handling-policy.md](error-handling-policy.md) for the full policy: `noexcept`, return-signaling, `[[nodiscard]]`, exceptions, the `(void)`-cast nuance, and contract-violation branches.

## Build System

Build and run one test with `./cleanbuild.sh <name>_test.cpp` (the common case; the arg is the test source filename), or `./cleanbuild.sh` for a clean build of the whole suite. Never hand-run cmake/ninja in `tests/build/` (stale dir breaks FetchContent). Run `./format_all.sh` before committing. See the `build-and-test` skill for the full menu (libstdc++, clang-tidy, sanitizers, MSAN setup) and the Catch2 test-writing conventions.

## Code Style

- Run `./format_all.sh` after edits and before `git commit`. Edit/Write bypass IDE save hooks, so this is manual.
- Plain 7-bit ASCII only in comments and docs: `->` not the Unicode arrow; no em dashes, curly quotes, etc. The em-dash prohibition is about the punctuation, not just the Unicode codepoint: don't use `--` (double hyphen) as an em-dash substitute, and don't abuse a single `-` to stand in for one either. Rewrite the sentence so it doesn't need em-dash-style asides: use a comma, a colon, parentheses, or two sentences.
- Don't pre-wrap comments. Write each paragraph as a single logical line and let clang-format reflow it (`ReflowComments: true`, `ColumnLimit: 79`). Use a blank `//` line between paragraphs in multi-paragraph comments. Note that clang-format will not reflow across structural boundaries (e.g., comments adjacent to `#pragma region` lines), so an occasional manual wrap there is fine.
- No trailing-underscore private methods. Prefix with `do_` instead: public `close()`, private `do_close()`.
- Prefer uniform initialization: `int i{4};`, `: option_{option}`. Don't use `{}` for variables with a default constructor (clang-tidy flags it); do use it for `int`/`bool`/etc.
- Use `std::chrono` literal suffixes (`1s`, `500ms`, `100us`) over explicit constructors. Library headers already pull in `using namespace std::chrono_literals;`; add it in test files as needed.
- "Token" is reserved for things that are literally named tokens (e.g., `completion_token`). Don't use the word loosely in comments or docs to mean "handle," "callback," "view," "ticket," "marker," etc.; pick the precise word or describe the thing.
- Lambda init-captures: keep the name the same as the bound variable. Prefer `[data = std::move(data)]` over `[d = std::move(data)]`. The lambda body reads as if the variable kept its identity, which it morally did.

## Git Workflow

- Use `git add .` when committing (to include user-made changes).
- PRs are squash-merged; respond to review with new commits, not amends.
- PR descriptions: verify every claim against the code. Don't rely on general knowledge about patterns or algorithms (e.g., the "timing wheel" here is single-level, not hierarchical).
- When pushing a branch as a new PR, review all changes first and flag bugs, doc errors, or style violations before writing the description.

## Reuse Library Utilities

Corvid provides utilities that replace direct calls on std types. Search the library before reaching for `std::string`, `std::string_view`, or container members. Examples:

- Map/set lookup: prefer `find_opt` (returns `std::optional`) over `find` + end-check.
- String searching/splitting: prefer parsers/locators in `corvid/strings/` over `std::string::find`, `substr`, etc.

Scan relevant headers first when writing new code to avoid reimplementing.

## Non-Obvious Locations

- `npos` / base string position types: `corvid/strings/core/string_base.h`
- `npos_choice`: `corvid/strings/core/locating.h`
