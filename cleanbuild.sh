#!/bin/bash

# Fail fast.
set -e

# Choose which standard library to use. Optionally pass a test source filename
# first to build and run a matching unit test, then pass "libstdcpp" or
# "libcxx" to override the default. Add "tidy" to run clang-tidy during the
# build. Add a sanitizer mode ("asan" [which includes ubsan], "tsan", "ubsan",
# or "msan") to instrument the build with the corresponding LLVM sanitizer.
# The default is `libcxx`, no tidy, no sanitizer.
#
# This script builds and runs one configuration at a time. To exercise every
# configuration (plain, asan, tsan, msan, tidy) in sequence, pass "all":
#
#   ./cleanbuild.sh all
#
# That dispatches to the CTest dashboard driver at tests/comprehensive.cmake.
# Each config lands in its own tests/build/comprehensive/<name>/ dir, and
# the run exits non-zero if any config fails configure, build, or test.

choice=""
use_tidy=false
sanitizer=""
test_name=""
target_name=""

usage="Usage: $0 [all | [testname.cpp] [libstdcpp|libcxx] [tidy] [asan|tsan|ubsan|msan]]"

# "all" short-circuits to the comprehensive multi-config sweep and doesn't
# compose with the per-config options.
if [[ "${1:-}" == "all" ]]; then
  if [[ $# -gt 1 ]]; then
    echo "$0: 'all' takes no other arguments" >&2
    echo "$usage" >&2
    exit 1
  fi
  ctest -V -S tests/comprehensive.cmake || rv=$?
  rv=${rv:-0}

  # Auto-open any non-empty warnings.txt files (typically just tidy) in
  # VSCode so the punch list is immediately at hand. Falls through with a
  # paths-only message if `code` isn't on PATH.
  warn_files=()
  for f in tests/build/comprehensive/*/warnings.txt; do
    [[ -s "$f" ]] && warn_files+=("$f")
  done
  if [[ ${#warn_files[@]} -gt 0 ]]; then
    if command -v code >/dev/null 2>&1; then
      echo "Opening in VSCode: ${warn_files[*]}"
      code "${warn_files[@]}"
    else
      echo "Warnings written to:"
      printf '  %s\n' "${warn_files[@]}"
    fi
  fi

  exit "$rv"
fi

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
  # When building under MSAN, the ignorelist content affects codegen but is
  # not part of ccache's default hash (only its path is, via the command
  # line). Without CCACHE_EXTRAFILES, editing the ignorelist between two
  # cleanbuild runs can yield stale .o files.
  if [[ "$sanitizer" == "msan" ]]; then
    export CCACHE_EXTRAFILES="$(pwd)/scripts/msan-libcxx-ignorelist.txt"
  fi
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

# Run the registered CTest suite. `notest_*` sources are built but not
# registered, so ctest naturally skips them.
#
# In sanitizer modes we keep going past failures so the whole sweep reports
# every issue rather than bailing on the first one. Plain runs still bail
# on the first failure to match the legacy behavior. Tests are independent
# (no fixed ports, no shared files), so we run in parallel by default; set
# CTEST_PARALLEL_LEVEL to override.
ctest_args=(--output-on-failure --test-dir "$buildRoot")
if [[ -z "${CTEST_PARALLEL_LEVEL:-}" ]]; then
  ctest_args+=(-j"$(nproc)")
fi
if [[ -z "$sanitizer" ]]; then
  ctest_args+=(--stop-on-failure)
fi

ctest "${ctest_args[@]}"
