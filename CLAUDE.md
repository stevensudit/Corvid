# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

CMakeLists.txt lives in `tests/` only — there is none at the project root. Build directory: `tests/build/`.

```sh
# Clean build + run all tests
./cleanbuild.sh                        # default (libc++)
./cleanbuild.sh libstdcpp              # use libstdc++ instead
./cleanbuild.sh tidy                   # also run clang-tidy

# Format all source files (required before committing)
./format_all.sh
```

## Code Style

- Run `./format_all.sh` after code changes and before `git commit`. Claude's Edit/Write tools bypass IDE save hooks, so this must be done manually.
- When referencing code locations, use markdown link format: `[filename.ext:line](filename.ext#Lline)`

## Git Workflow

- Use `git add .` when committing (to include any user-made changes alongside Claude's)
- Use `git switch` instead of `git checkout`
- PRs are squash-merged — respond to review with additional commits, not amends

## Testing

Framework: custom minitest (`tests/minitest.h`). All classes have test coverage in `tests/` — tests may be per-class, grouped by related classes, or cover an entire subfolder (e.g., all ECS headers tested together). Check `tests/` before asking about coverage.

## TODO File

The root `TODO` file tracks enhancement requests and design decisions. When encountering TODO comments in code that represent enhancements (not immediate work), move them there. Rewrite positional references ("below", "above") to name the specific function or class.

Corvid is a **header-only C++23 library** (no external dependencies beyond libc++). All headers live under `corvid/`.

## Non-Obvious Locations

- `npos` / base string position types: `corvid/strings/string_base.h`
- `npos_choice`: `corvid/strings/locating.h`
