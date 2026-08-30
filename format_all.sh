#!/bin/bash

# Format all C++ source and header files in the project using clang-format.

# Fail fast.
set -e

# clang-format is an update-alternatives link to the version the Dockerfile
# installs, so no versioned name is needed here.
if command -v clang-format &> /dev/null; then
  CLANG_FORMAT="clang-format"
else
  echo "Error: clang-format not found. Please install clang-format." >&2
  exit 1
fi

echo "Using $CLANG_FORMAT"
echo "Formatting all .cpp, .h, .cu, and .cuh files..."

# Find and format all .cpp, .h, .cu, and .cuh files, excluding build
# directories, CMake files, and the .local sandbox (used for MSAN-instrumented
# LLVM source).
find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.cu" -o -name "*.cuh" \) \
  -not -path "*/build/*" \
  -not -path "*/CMakeFiles/*" \
  -not -path "*/.fetchcontent*/*" \
  -not -path "*/.local/*" \
  -print0 | while IFS= read -r -d '' file; do
  echo "Formatting: $file"
  if [[ "$file" == *.h ]]; then
    # clang-format guesses a .h file's language, and a C++26 reflection
    # splice (`[: ... :]`) reads to it as an Objective-C message send, which
    # the repo style does not cover, so it silently falls back to its default
    # style. Formatting through stdin under an assumed .cpp name pins C++.
    # The style file is still found from the assumed name's directory.
    tmp="$(mktemp)"
    "$CLANG_FORMAT" --style=file --assume-filename="${file%.h}.cpp" \
      < "$file" > "$tmp" && cat "$tmp" > "$file"
    rm -f "$tmp"
  else
    "$CLANG_FORMAT" -i "$file"
  fi
done

echo "Done formatting all files."
