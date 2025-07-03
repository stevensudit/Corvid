#!/bin/bash

# Fail fast.
set -e

# Set the environment variables to use clang
export CC="/usr/bin/clang-19"
export CXX="/usr/bin/clang++-19"

# Define the build directory (assuming you're using an out-of-source build)
buildDir="tests/release_bin"

rm -f "CMakeCache.txt"
rm -f "cmake_install.cmake"
rm -f "ClangExeProject.sln"
rm -f "build.ninja"
rm -f "CMakeFiles/*"
rm -f ".ninja_deps"
rm -f ".ninja_log"
rm -f "build.ninja"
rm -f "cmake_install.cmake"

# If the build directory exists, delete it to clean the build
if [ -d "$buildDir" ]; then
    echo "Cleaning the build directory at $buildDir"
    rm -rf "$buildDir"
else
    echo "Build directory not found. Creating a new one at $buildDir"
fi

# Create the build directory
mkdir -p "$buildDir"

# Run cmake to configure the project with Ninja (or MinGW Makefiles) and clang
cmake -G "Ninja" tests/

# Run the build (this will compile everything from scratch)
cmake --build tests/ --config Release

# Loop through each file in the release directory
for file in "$buildDir"/*; do
  # Check if the file is an executable and a regular file (not a directory or symlink)
  if [[ -x "$file" && -f "$file" && "$file" != *"CMakeCXXCompilerId"* ]]; then
    echo "$file..."
    "$file"
    echo "."
  fi
done

