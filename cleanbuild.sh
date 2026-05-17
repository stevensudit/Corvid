#!/bin/bash

# Fail fast.
set -e

# Choose which standard library to use. Optionally pass a test source filename
# first to build and run a matching unit test, then pass "libstdcpp" or
# "libcxx" to override the default. Add "tidy" to run clang-tidy during the
# build. Add a sanitizer mode ("asan", "tsan", or "ubsan") to instrument the
# build with the corresponding LLVM sanitizer. The default is `libcxx`, no
# tidy, no sanitizer.

choice=""
use_tidy=false
sanitizer=""
test_name=""
target_name=""

usage="Usage: $0 [testname.cpp] [libstdcpp|libcxx] [tidy] [asan|tsan|ubsan|msan]"

if [[ $# -gt 0 && "$1" != "libstdcpp" && "$1" != "libcxx" \
      && "$1" != "tidy" && "$1" != "--tidy" \
      && "$1" != "asan" && "$1" != "tsan" && "$1" != "ubsan" \
      && "$1" != "msan" ]]; then
  if [[ "$1" == *.cpp ]]; then
    test_name="$1"
    target_name="${test_name%.cpp}"
    shift
  else
    echo "$usage" >&2
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
    asan|tsan|ubsan|msan)
      sanitizer="$arg"
      ;;
    *)
      echo "$usage" >&2
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

if [[ -n "$sanitizer" ]]; then
  echo "Sanitizer: $sanitizer"
  SAN_OPTION="-DSANITIZER=$sanitizer"
else
  SAN_OPTION=""
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
cmake -S tests -B "$buildRoot" -G "Ninja" $LIBSTD_OPTION $TIDY_OPTION $TEST_NAME_OPTION $SAN_OPTION

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

# Loop through each file in the release directory. Sources prefixed with
# `notest_` (e.g. `notest_corvid_sim.cpp`) are built like tests but skipped
# here -- typically long-lived servers or demos that should only run when
# invoked directly (F5).
#
# In sanitizer modes we keep going past failures so the whole sweep reports
# every issue rather than bailing on the first one. Plain runs still bail
# on the first failure to match the legacy behavior.
sweep_failed=0
failed_tests=()
for file in "$buildDir"/*; do
  # Check if the file is an executable and a regular file (not a directory or symlink)
  if [[ -x "$file" && -f "$file" && "$file" != *"CMakeCXXCompilerId"* ]]; then
    base="${file##*/}"
    if [[ "$base" == notest_* ]]; then continue; fi
    echo "$file..."
    if [[ -n "$sanitizer" ]]; then
      if ! "$file"; then
        echo "[FAIL] $base (sanitizer=$sanitizer, exit non-zero)"
        sweep_failed=$((sweep_failed + 1))
        failed_tests+=("$base")
      fi
    else
      "$file"
    fi
    echo "."
  fi
done

if [[ -n "$sanitizer" && "$sweep_failed" -gt 0 ]]; then
  echo ""
  echo "$sweep_failed test executable(s) failed under $sanitizer:"
  for name in "${failed_tests[@]}"; do
    echo "  - $name"
  done
  exit 1
fi
