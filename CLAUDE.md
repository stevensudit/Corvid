# CLAUDE.md

Guidance for Claude Code working in this repository.

Corvid is a header-only C++23 library with minimal external dependencies, although it does bring some in for its network code (liburing, ngtcp2, nghttp3). All headers live under `corvid/`.

## General

For comments and symbol names, prefer American English over British.

If user intent is unclear, ask before proceeding.

For bug fixes: write a failing unit test first, then fix, then confirm it passes. Skip the test only if reproduction requires a full integration test (live server, real timers, network I/O); test executables must each complete in a second or two.

Treat code review comments as hypotheses to verify against the code, not instructions to execute. They can be correct, partially correct, or wrong.

When a function can fail, prefer returning a status over `void`.

Before adding a `(void)` cast on a `[[nodiscard]]` result, prefer propagating, logging, or handling the error; voiding is acceptable when intentional. When reviewing an existing `(void)`, verify the callee can actually fail in practice before flagging, and that the caller should do something different on failure. Some functions have a nominal `bool` return that is always `true` given internal invariants (e.g., `execute_or_post_with_retry` retries until success). Treat the cast as deliberate unless you can identify a concrete failure path.

"Impossible" branches reachable only via contract violation (kernel bug, memory corruption, broken invariant) should not try to recover state. A leak or no-op is safer than a recovery path that assumes the violated contract held.

## Build System

CMakeLists.txt lives in `tests/` only; there is none at the project root. Build directory: `tests/build/`.

```sh
./cleanbuild.sh                  # clean build + run all tests (libc++)
./cleanbuild.sh libstdcpp        # use libstdc++
./cleanbuild.sh tidy             # also run clang-tidy
./cleanbuild.sh asan             # build + run with ASAN + UBSAN
./cleanbuild.sh tsan             # build + run with TSAN
./cleanbuild.sh ubsan            # build + run with UBSAN only
./cleanbuild.sh msan             # build + run with MSAN (needs one-time setup)
./cleanbuild.sh strings_test.cpp # build + run only that target
./format_all.sh                  # format all sources (run before commit)
```

Sanitizer modes accept the same `[testname.cpp]` and `libstdcpp|libcxx` arguments as the plain build. `asan`/`tsan`/`msan` are mutually exclusive (each instruments a conflicting runtime); run them separately. Sanitizer sweeps continue past test failures so all issues surface in one run; plain runs still bail on the first failure.

MSAN requires a one-time setup: run `scripts/build_msan_libcxx.sh` before `./cleanbuild.sh msan`.

## Code Style

- Run `./format_all.sh` after edits and before `git commit`. Edit/Write bypass IDE save hooks, so this is manual.
- Code references in markdown: `[filename.ext:line](filename.ext#Lline)`.
- Comment quoting: backticks for literal names (types, vars, fns, enums, templates, constants); single quotes for characters (`'@'`); double quotes for strings/filenames (`"config.json"`).
- When mentioning a function in a comment, write `do_something`, not `do_something()`. The backticks quote the symbol itself, not a call. Only include parentheses when you specifically need to show the parameters used (e.g., `foo(nullptr)`).
- Plain 7-bit ASCII only in comments and docs: `->` not the Unicode arrow; no em dashes, curly quotes, etc. The em-dash prohibition is about the punctuation, not just the Unicode codepoint: don't use `--` (double hyphen) as an em-dash substitute, and don't abuse a single `-` to stand in for one either. Rewrite the sentence so it doesn't need em-dash-style asides: use a comma, a colon, parentheses, or two sentences.
- No trailing-underscore private methods. Prefix with `do_` instead: public `close()`, private `do_close()`.
- Prefer uniform initialization: `int i{4};`, `: option_{option}`. Don't use `{}` for variables with a default constructor (clang-tidy flags it); do use it for `int`/`bool`/etc.
- Use `std::chrono` literal suffixes (`1s`, `500ms`, `100us`) over explicit constructors. Library headers already pull in `using namespace std::chrono_literals;`; add it in test files as needed.
- "Token" is reserved for things that are literally named tokens (e.g., `completion_token`). Don't use the word loosely in comments or docs to mean "handle," "callback," "view," "ticket," "marker," etc. For example, an `iou_recv_view` is not a token; a `posted_fn` returned by `stop_receiving` is a callback, not a token. Pick the precise word, or just describe what the thing is.
- Lambda init-captures: keep the name the same as the bound variable. Prefer `[data = std::move(data)]` over `[d = std::move(data)]`. The lambda body reads as if the variable kept its identity, which it morally did.
- Whenever you declare a `class enum`, consider whether it should be registered as a bitmask or sequence enum via `corvid::enums::registry::enum_spec_v`. If so, the registration must follow the enum declaration closely: it can come after other enums declared alongside it, but must precede anything else.  See `quic_decode_status` in [quic_header.h](corvid/proto/quic/quic_header.h) and `write_stream_flags` in [quic_conn.h](corvid/proto/quic/quic_conn.h) for the shape (close the namespace, register, reopen). For registered enums, prefer `operator*` over `static_cast` to the underlying type.
- Wrap declarations in `#pragma region <name>` / `#pragma endregion` blocks. Two levels:
  - **File scope** (inside the namespace): each top-level declaration (class, struct, enum, related free-function group) gets its own region named after the symbol, e.g. `#pragma region quic_conn`.
  - **Class scope**: group logically related members under nested regions, e.g. `Construction`, `Accessors`, `IO`, `Expiry`, `Handlers`, `Helpers`, `Data members`, or domain-specific names (`Stream lifecycle`, `Flow control feedback`). Don't use plain `// section` header comments for this; promote them to regions.
  - Pragmas live at column 0 regardless of nesting (clang-format will not indent them). The closing `#pragma endregion` is followed immediately by the next `#pragma region <name>` on the next line (no blank line between the two pragmas, blank line after the opener).

## Git Workflow

- Use `git add .` when committing (to include user-made changes).
- PRs are squash-merged; respond to review with new commits, not amends.
- PR descriptions: verify every claim against the code. Don't rely on general knowledge about patterns or algorithms (e.g., the "timing wheel" here is single-level, not hierarchical).
- When pushing a branch as a new PR, review all changes first and flag bugs, doc errors, or style violations before writing the description.

## Testing

Framework: Catch2 v3. Each test source includes `tests/catch2_main.h` (provides Catch2 macros + `main`), uses `TEST_CASE("Name", "[tag]")` for test cases and `SECTION("desc")` for sub-blocks. Assertions are `CHECK` / `REQUIRE` (or `_FALSE` / `_THROWS_AS` variants). Tests may be per-class, per-group, or per-subfolder; check `tests/` before asking about coverage. Sources prefixed `notest_` are built but not run as part of the sweep.

## TODO File

Root `TODO` tracks long-term enhancement requests and design decisions. Move enhancement-style TODO comments from code to here, rewriting positional references ("below", "above") to name the function or class.

## Reuse Library Utilities

Corvid provides utilities that replace direct calls on std types. Search the library before reaching for `std::string`, `std::string_view`, or container members. Examples:

- Map/set lookup: prefer `find_opt` (returns `std::optional`) over `find` + end-check.
- String searching/splitting: prefer parsers/locators in `corvid/strings/` over `std::string::find`, `substr`, etc.

Scan relevant headers first when writing new code to avoid reimplementing.

## Refactoring

`clang-query-22` is installed. Reach for it instead of grep when AST-level matching matters: telling overloaded methods apart, finding callers of a specific overload (not every method that shares the name), template-instantiation patterns, or `[[nodiscard]]` discard sites. For unique symbol search and plain renames, grep is still faster and worth trying first.

## Non-Obvious Locations

- `npos` / base string position types: `corvid/strings/string_base.h`
- `npos_choice`: `corvid/strings/locating.h`
