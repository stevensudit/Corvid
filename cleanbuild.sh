#!/bin/bash

# Fail fast.
set -e

# Side effect on first run after a fresh clone: CMake regenerates `.clangd`
# at the project root from `.clangd.in` via `configure_file`. The generated
# file is gitignored because it bakes in absolute dep paths (ngtcp2, openssl)
# tied to this checkout. IDE clangd won't resolve those headers correctly
# until this script (or a bare `cmake -S tests -B tests/build`) has run once.

# Choose which standard library to use. Optionally pass a test source filename
# first to build and run a matching unit test, then pass "libstdcpp" or
# "libcxx" to override the default. Add "tidy" to run clang-tidy during the
# build. Add a sanitizer mode ("asan" [which includes ubsan], "tsan", "ubsan",
# or "msan") to instrument the build with the corresponding LLVM sanitizer.
# Add "coverage" to build with source-based coverage instrumentation and run
# llvm-profdata/llvm-cov after tests pass; mutually exclusive with sanitizers
# and tidy. Add "scan" to skip the build and run the Clang Static Analyzer
# (`analyze-build`) against the compile_commands.json instead; mutually
# exclusive with everything else. The default is `libcxx`, no tidy, no
# sanitizer, no coverage, no scan.
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
use_coverage=false
use_scan=false
test_name=""
target_name=""

usage="Usage: $0 [all | [testname.cpp] [libstdcpp|libcxx] [tidy] [asan|tsan|ubsan|msan] [coverage] [scan]]"

# Enforce the core/utils band layering before any build (fast, static, and
# build-independent). See corvid/deps.md.
"$(dirname "$0")/scripts/check_layering.sh" || exit 1

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
      && "$1" != "msan" && "$1" != "coverage" && "$1" != "scan" ]]; then
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
    coverage)
      use_coverage=true
      ;;
    scan)
      use_scan=true
      ;;
    *)
      echo "$usage" >&2
      exit 1
      ;;
  esac
done

# Coverage instruments the same compile/link lines a sanitizer would, so
# combining them produces broken runtimes (and clang-tidy adds build-time
# analysis with no coverage payoff). Reject the combination up front rather
# than waiting for cmake/cl to complain in a less obvious way.
if $use_coverage; then
  if [[ -n "$sanitizer" ]]; then
    echo "$0: 'coverage' cannot be combined with sanitizer '$sanitizer'" >&2
    exit 1
  fi
  if $use_tidy; then
    echo "$0: 'coverage' cannot be combined with 'tidy'" >&2
    exit 1
  fi
fi

# Scan is configure-only: it consumes compile_commands.json and re-invokes
# the analyzer on every TU itself, so combining with anything that affects
# the build (sanitizers, tidy, coverage, single-test filter) is meaningless.
if $use_scan; then
  if [[ -n "$sanitizer" ]] || $use_tidy || $use_coverage || [[ -n "$test_name" ]]; then
    echo "$0: 'scan' takes no other arguments (configure-only static analysis)" >&2
    exit 1
  fi
fi

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

if $use_coverage; then
  echo "Coverage: source-based instrumentation"
  COV_OPTION="-DCOVERAGE=ON"
else
  COV_OPTION=""
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
cmake -S tests -B "$buildRoot" -G "Ninja" $LIBSTD_OPTION $TIDY_OPTION $TEST_NAME_OPTION $SAN_OPTION $COV_OPTION

# Scan mode: hand the compile database to the Clang Static Analyzer and
# stop. We don't need ninja artifacts -- analyze-build re-invokes clang with
# --analyze on each TU it finds in compile_commands.json. --analyze-headers
# is on because this is a header-only library; without it, header functions
# not directly called from the matching .cpp would be skipped. --status-bugs
# is intentionally off: first runs against C++23 template code surface
# false positives we want to triage, not fail the script on.
#
# Note: CSA findings here largely duplicate `cleanbuild.sh tidy` because
# clang-tidy's `clang-analyzer-*` family wraps the same analyzer. CSA does
# NOT honor `// NOLINT` (that's a clang-tidy directive), so the noise we've
# already silenced in tidy reappears here. We accept the overlap rather
# than annotate every site twice with `[[clang::suppress]]`; the scan mode
# stays for the rare cross-procedural finding tidy might miss.
if $use_scan; then
  scanReports="$buildRoot/csa-reports"
  rm -rf "$scanReports"
  echo
  echo "Running Clang Static Analyzer (analyze-build) ..."
  analyze-build-22 \
    --cdb "$buildRoot/compile_commands.json" \
    --use-analyzer /usr/bin/clang-22 \
    --exclude tests/.fetchcontent \
    --analyze-headers \
    --output "$scanReports"
  echo
  # analyze-build creates a timestamped subdir under --output and only writes
  # files there when at least one finding exists. No subdir == clean run.
  shopt -s nullglob
  scanRun=("$scanReports"/scan-build-*)
  shopt -u nullglob
  if [[ ${#scanRun[@]} -gt 0 && -f "${scanRun[0]}/index.html" ]]; then
    shopt -s nullglob
    findings=("${scanRun[0]}"/report-*.html)
    shopt -u nullglob
    echo "Scan findings: ${#findings[@]}"
    echo "Report: ${scanRun[0]}/index.html"
    if command -v code >/dev/null 2>&1; then
      code "${scanRun[0]}/index.html"
    fi
  else
    echo "Scan clean: no findings."
  fi
  exit 0
fi

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

# Coverage: each test process writes its own raw profile to
# tests/build/coverage/profraw/<pid>.profraw. %p expands to the PID so
# parallel ctest workers don't collide. Wipe the directory first so a
# previous run's profraws don't leak into the merge.
if $use_coverage; then
  covDir="$buildRoot/coverage"
  rm -rf "$covDir"
  mkdir -p "$covDir/profraw"
  export LLVM_PROFILE_FILE="$(pwd)/$covDir/profraw/%p.profraw"
fi

# Don't let set -e abort on a test failure: we still want the tidy summary
# below to print. Capture the status and propagate it after summarizing.
ctest_status=0
ctest "${ctest_args[@]}" || ctest_status=$?

# In tidy mode, clang-tidy warnings stream through the build phase but get
# buried under the ctest run that follows. Summarize them at the very end so
# the bottom-line state is visible. Filter out diagnostics in
# .fetchcontent/ or .local/ (FetchContent'd Catch2, prebuilt OpenSSL, ngtcp2
# ExternalProject sources) so only Corvid-owned issues appear.
if $use_tidy; then
  echo
  echo "==================== clang-tidy summary ===================="
  warn_lines=$(grep -E ': warning: .*\[[A-Za-z][A-Za-z0-9._-]*\]' "$tidyLogFile" \
               | grep -v -F '.fetchcontent/' \
               | grep -v -F '.local/' \
               || true)
  if [[ -n "$warn_lines" ]]; then
    warn_count=$(printf '%s\n' "$warn_lines" | wc -l)
    echo "$warn_count warning(s):"
    echo
    printf '%s\n' "$warn_lines"
    echo
    echo "By check:"
    printf '%s\n' "$warn_lines" \
        | grep -oE '\[[A-Za-z][A-Za-z0-9._-]*\]$' \
        | sort | uniq -c | sort -rn \
        | sed 's/^/  /'
    echo
    echo "Full log: $tidyLogFile"
  else
    echo "clean: no warnings"
  fi
  echo "============================================================"
fi

if [[ $ctest_status -ne 0 ]]; then
  exit $ctest_status
fi

if $use_coverage; then
  echo
  echo "Merging coverage profiles ..."
  shopt -s nullglob
  profraws=("$covDir"/profraw/*.profraw)
  shopt -u nullglob
  if [[ ${#profraws[@]} -eq 0 ]]; then
    echo "No .profraw files found in $covDir/profraw/ -- did any test binaries run?" >&2
    exit 1
  fi
  merged="$covDir/merged.profdata"
  llvm-profdata-22 merge -sparse "${profraws[@]}" -o "$merged"

  # Collect every test binary (skip notest_* since they don't run). Pass the
  # first as the positional binary and the rest via -object= so llvm-cov sees
  # all counters.
  shopt -s nullglob
  bins=()
  for f in "$buildDir"/*; do
    base="$(basename "$f")"
    [[ "$base" == notest_* ]] && continue
    [[ -x "$f" && -f "$f" ]] && bins+=("$f")
  done
  shopt -u nullglob
  if [[ ${#bins[@]} -eq 0 ]]; then
    echo "No test binaries found in $buildDir/" >&2
    exit 1
  fi
  obj_args=()
  for b in "${bins[@]:1}"; do obj_args+=(-object "$b"); done

  # Scope the report to the library headers under corvid/. Test sources,
  # Catch2, and libc++ would otherwise dominate the output.
  cov_common=(
    "${bins[0]}" "${obj_args[@]}"
    --instr-profile="$merged"
    --ignore-filename-regex='(tests/|.fetchcontent/|/usr/|/llvm-msan/)'
  )

  echo
  echo "Coverage report (scope: corvid/ headers):"
  llvm-cov-22 report "${cov_common[@]}"

  htmlDir="$covDir/html"
  llvm-cov-22 show "${cov_common[@]}" \
    -format=html -output-dir="$htmlDir" \
    -show-line-counts-or-regions -show-branches=count
  echo
  echo "HTML coverage report: $htmlDir/index.html"
fi
