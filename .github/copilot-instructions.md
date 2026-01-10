# Corvid Project Guidelines

## Build System

- Always use CMake to build files, never direct calls to clang or g++
- Build commands should be run from the tests/ directory: `cd tests && cmake --build build --target <target>`
- When creating new test files, they will be automatically picked up by CMakeLists.txt (it globs *.cpp files)
- **cleanbuild.sh**: Run `./cleanbuild.sh` from project root for a clean build and execution of all unit tests
  - Use this to check for regressions globally after broad changes
  - Run before commits are ready to be pushed into a PR
  - Optional arguments: `libstdcpp` or `libcxx` to choose standard library, `tidy` to run clang-tidy

## Code Style

- Avoid using emojis in code or comments unless explicitly requested
- Follow existing code patterns in the codebase
- **format_all.sh**: Run `./format_all.sh` from project root to format all source files with clang-format
  - Run before committing changes to ensure consistent formatting

## Testing

- Test executables are built to: tests/build/release_bin/
- The test framework is minitest (custom, minimal test framework)
- Tests use EXPECT_EQ, EXPECT_TRUE, etc. macros
- Test files follow the pattern: *_test.cpp

## Project Structure

- Headers: corvid/**/*.h
- Tests: tests/*_test.cpp
- String utilities: corvid/strings/
- Test framework: tests/minitest.h

## Technical Notes

- The project uses C++23 standard
- Prefer libc++ (Clang's standard library) over libstdc++
- Base string types position and npos are defined in corvid/strings/string_base.h, while npos_choice is defined in corvid/strings/locating.h
