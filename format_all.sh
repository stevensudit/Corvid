#!/bin/bash

# Format all C++ source and header files in the project using clang-format.

# Fail fast.
set -e

CLANG_FORMAT="/usr/bin/clang-format-19"

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
