#!/bin/bash

# Format all C++ source and header files in the project using clang-format.

# Fail fast.
set -e

# Locate clang-format, preferring versioned clang-format-22 if available
if command -v clang-format-22 &> /dev/null; then
  CLANG_FORMAT="clang-format-22"
elif command -v clang-format &> /dev/null; then
  CLANG_FORMAT="clang-format"
else
  echo "Error: clang-format not found. Please install clang-format." >&2
  exit 1
fi

echo "Using $CLANG_FORMAT"
echo "Formatting all .cpp and .h files..."

# Find and format all .cpp and .h files, excluding build directories, CMake
# files, and the .local sandbox (used for MSAN-instrumented LLVM source).
find . -type f \( -name "*.cpp" -o -name "*.h" \) \
  -not -path "*/build/*" \
  -not -path "*/CMakeFiles/*" \
  -not -path "*/.local/*" \
  -print0 | while IFS= read -r -d '' file; do
  echo "Formatting: $file"
  "$CLANG_FORMAT" -i "$file"
done

echo "Done formatting all files."
