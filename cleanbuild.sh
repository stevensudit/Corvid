#!/bin/bash

# Fail fast.
set -e

# Choose which standard library to use. Optionally pass a test source filename
# first to build and run a matching unit test, then pass "libstdcpp" or
# "libcxx" to override the default. Add "tidy" to run clang-tidy during the
# build. The default is `libcxx`.

choice=""
use_tidy=false
test_name=""
target_name=""

if [[ $# -gt 0 && "$1" != "libstdcpp" && "$1" != "libcxx" && "$1" != "tidy" && "$1" != "--tidy" ]]; then
  if [[ "$1" == *.cpp ]]; then
    test_name="$1"
    target_name="${test_name%.cpp}"
    shift
  else
    echo "Usage: $0 [testname.cpp] [libstdcpp|libcxx] [tidy]" >&2
    exit 1
  fi
fi

for arg in "$@"; do
  case "$arg" in
    libstdcpp|libcxx)
      choice="$arg"
      ;;
    tidy|--tidy)
      use_tidy=true
      ;;
    *)
      echo "Usage: $0 [testname.cpp] [libstdcpp|libcxx] [tidy]" >&2
      exit 1
      ;;
  esac
done

if [[ -z "$choice" ]]; then
  choice="libcxx"
fi

if [[ "$choice" == "libstdcpp" ]]; then
  echo "Using libstdc++"
  export CC="$(command -v clang)"
  export CXX="$(command -v clang++)"
  LIBSTD_OPTION="-DUSE_LIBSTDCPP=ON"
else
  echo "Using libc++"
  export CC="/usr/bin/clang-22"
  export CXX="/usr/bin/clang++-22"
  LIBSTD_OPTION="-DUSE_LIBSTDCPP=OFF"
fi

if $use_tidy; then
  TIDY_OPTION="-DUSE_CLANG_TIDY=ON"
else
  TIDY_OPTION=""
fi

if [[ -n "$test_name" ]]; then
  TEST_NAME_OPTION="-DTEST_NAME=$test_name"
else
  TEST_NAME_OPTION=""
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
cmake -S tests -B "$buildRoot" -G "Ninja" $LIBSTD_OPTION $TIDY_OPTION $TEST_NAME_OPTION

# Run the build (this will compile everything from scratch)
if $use_tidy; then
  tidyLogFile="$buildRoot/tidy.log"
  if [[ -n "$target_name" ]]; then
    cmake --build "$buildRoot" --config Release --target "$target_name" 2>&1 | tee "$tidyLogFile"
  else
    cmake --build "$buildRoot" --config Release 2>&1 | tee "$tidyLogFile"
  fi
else
  if [[ -n "$target_name" ]]; then
    cmake --build "$buildRoot" --config Release --target "$target_name"
  else
    cmake --build "$buildRoot" --config Release
  fi
fi

# Loop through each file in the release directory
for file in "$buildDir"/*; do
  # Check if the file is an executable and a regular file (not a directory or symlink)
  if [[ -x "$file" && -f "$file" && "$file" != *"CMakeCXXCompilerId"* ]]; then
    echo "$file..."
    "$file" -testonly
    echo "."
  fi
done
