#!/bin/bash

# Fail fast.
set -e

# Choose which standard library to use. Pass "libstdcpp" or "libcxx" as the first
# argument to override the default. Add "tidy" to run clang-tidy during the
# build. Outside of Codex, the default is libc++.

choice=""
use_tidy=false

for arg in "$@"; do
  case "$arg" in
    libstdcpp|libcxx)
      choice="$arg"
      ;;
    tidy|--tidy)
      use_tidy=true
      ;;
    *)
      echo "Usage: $0 [libstdcpp|libcxx] [tidy]" >&2
      exit 1
      ;;
  esac
done

if [[ -z "$choice" ]]; then
  if [[ -n "$CODEX_PROXY_CERT" ]]; then
    choice="libstdcpp"
  else
    choice="libcxx"
  fi
fi

if [[ "$choice" == "libstdcpp" ]]; then
  echo "Using libstdc++"
  export CC="$(command -v clang)"
  export CXX="$(command -v clang++)"
  LIBSTD_OPTION="-DUSE_LIBSTDCPP=ON"
else
  echo "Using libc++"
  export CC="/usr/bin/clang-19"
  export CXX="/usr/bin/clang++-19"
  LIBSTD_OPTION="-DUSE_LIBSTDCPP=OFF"
fi

if $use_tidy; then
  TIDY_OPTION="-DUSE_CLANG_TIDY=ON"
else
  TIDY_OPTION=""
fi

# Define the build directory (assuming you're using an out-of-source build)
buildRoot="tests/build"
buildDir="$buildRoot/release_bin"

rm -f CMakeCache.txt cmake_install.cmake ClangExeProject.sln build.ninja
rm -rf CMakeFiles .ninja_deps .ninja_log

# If the release directory exists, delete it to clean the build
if [ -d "$buildDir" ]; then
    echo "Cleaning the build directory at $buildDir"
    rm -rf "$buildDir"
else
    echo "Build directory not found. Creating a new one at $buildDir"
fi

# Remove and recreate the CMake build directory
rm -rf "$buildRoot"
mkdir -p "$buildRoot" "$buildDir"

# Run cmake to configure the project with Ninja (or MinGW Makefiles) and clang
cmake -S tests -B "$buildRoot" -G "Ninja" $LIBSTD_OPTION $TIDY_OPTION

# Run the build (this will compile everything from scratch)
if $use_tidy; then
  tidyLogFile="$buildRoot/tidy.log"
  cmake --build "$buildRoot" --config Release 2>&1 | tee "$tidyLogFile"
else
  cmake --build "$buildRoot" --config Release
fi

# Loop through each file in the release directory
for file in "$buildDir"/*; do
  # Check if the file is an executable and a regular file (not a directory or symlink)
  if [[ -x "$file" && -f "$file" && "$file" != *"CMakeCXXCompilerId"* ]]; then
    echo "$file..."
    "$file"
    echo "."
  fi
done
