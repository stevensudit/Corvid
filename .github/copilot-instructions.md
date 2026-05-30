# Claude Code Rules for Corvid Project

## Build System
- Always use CMake to build files, never direct calls to clang or g++ (unless you are specifically debugging issues related to build flags, such as release/debug or optimization level)
- **IMPORTANT**: CMakeLists.txt lives in `tests/` only — there is none at the project root
  - The build directory is at `tests/build/`
  - Build a single target: `cd tests/build && ninja <target>`
  - Or equivalently: `cd tests && cmake --build build --target <target>`
  - If you're not sure what directory you're in, check with `pwd` first
- When creating new test files, they will be automatically picked up by CMakeLists.txt (it globs *.cpp files)
- **cleanbuild.sh**: Run `./cleanbuild.sh` from project root for a clean build and execution of all unit tests
  - Use this to check for regressions globally after broad changes
  - Run before commits are ready to be pushed into a PR
  - Optional arguments: `libstdcpp` or `libcxx` to choose standard library; `tidy` to run clang-tidy; `asan`/`tsan`/`ubsan`/`msan` for sanitizer sweeps (mutually exclusive); `coverage` for instrumented coverage; a `<testname>.cpp` to build and run only that target

## Code Style
- Avoid using emojis in code or comments unless explicitly requested
- Follow existing code patterns in the codebase
- When referencing code locations, always use markdown link format: `[filename.ext:line](filename.ext#Lline)` to make them clickable in the IDE
- **format_all.sh**: Run `./format_all.sh` from project root to format all source files with clang-format
  - MUST run before committing changes to ensure consistent formatting
  - Claude's Edit/Write tools bypass IDE save hooks (like format-on-save), so formatting must be applied manually
  - Run after making code changes but before `git commit`

## Git Operations
- When the user asks to commit changes, use `git add .` to stage all changes (including user-made modifications)
- This ensures any additional changes the user made are included in the commit
- Use `git switch` instead of `git checkout` for switching branches (safer and clearer intent)
- **Squash-merge workflow**: PRs are squash-merged, so updates in response to review should be additional commits, not amended ones
  - Separate commits make it easier to see what changed in response to feedback
  - Avoids force-push, which can disrupt reviewers
  - All commits are squashed into one on merge anyway

## Testing
- Test executables are built to: tests/build/release_bin/
- The test framework is Catch2 v3
- Each test source includes `tests/catch2_main.h` (provides the Catch2 macros plus `main`)
- Tests use `TEST_CASE("Name", "[tag]")` with `SECTION("desc")` sub-blocks; assertions are `CHECK` / `REQUIRE` (and `_FALSE` / `_THROWS_AS` variants)
- Test files follow the pattern `*_test.cpp` or `*_tests.cpp`; sources prefixed `notest_` are built but not run as part of the sweep

## Project Structure
- Headers: corvid/**/*.h
- Tests: tests/*_test.cpp and tests/*_tests.cpp
- String utilities: corvid/strings/
- Test framework: Catch2 v3, included via tests/catch2_main.h
- **TODO file**: The project uses a TODO file at the root level to track enhancement requests and design decisions
  - When encountering TODO comments in code that represent enhancement requests (not immediate implementation tasks), move them to the TODO file
  - Keep TODO comments in code only for items that should be addressed soon as part of ongoing work
  - When moving TODO comments with positional references (like "below", "above"), rewrite them to be absolute (e.g., name the specific function/class, not line numbers which are brittle)

## Specific Notes
- The project uses C++23 standard
- Prefer libc++ (Clang's standard library) over libstdc++
- Base string types position and npos are defined in corvid/strings/string_base.h, while npos_choice is defined in corvid/strings/locating.h
