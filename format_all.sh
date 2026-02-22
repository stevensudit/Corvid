#!/bin/bash

# Format all C++ source and header files in the project using clang-format.

# Fail fast.
set -e

# Locate clang-format, preferring versioned clang-format-21 if available
if command -v clang-format-21 &> /dev/null; then
  CLANG_FORMAT="clang-format-21"
elif command -v clang-format &> /dev/null; then
  CLANG_FORMAT="clang-format"
else
  echo "Error: clang-format not found. Please install clang-format." >&2
  exit 1
fi

echo "Using $CLANG_FORMAT"
echo "Formatting all .cpp and .h files..."

# Find and format all .cpp files, excluding build directories and CMake files
find . -type f -name "*.cpp" \
  -not -path "*/build/*" \
  -not -path "*/CMakeFiles/*" \
  -print0 | while IFS= read -r -d '' file; do
  echo "Formatting: $file"
  "$CLANG_FORMAT" -i "$file"
done

# Find and format all .h files, excluding build directories and CMake files
find . -type f -name "*.h" \
  -not -path "*/build/*" \
  -not -path "*/CMakeFiles/*" \
  -print0 | while IFS= read -r -d '' file; do
  echo "Formatting: $file"
  "$CLANG_FORMAT" -i "$file"
done

echo "Done formatting all files."
