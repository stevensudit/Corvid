#!/bin/bash

# Fail fast.
set -e

# Side effect on first run after a fresh clone: CMake regenerates `.clangd`
# at the project root from `.clangd.in` via `configure_file`. The generated
# file is gitignored because it bakes in absolute dep paths (ngtcp2, openssl)
# tied to this checkout. IDE clangd won't resolve those headers correctly
# until this script (or a bare `cmake -S tests -B tests/build`) has run once.

# Choose the compiler and standard library. Optionally pass a test source
# filename first to build and run a matching unit test, then pass "clang"
# (default) or "gcc" to pick the compiler. The standard library defaults to
# match the compiler (clang -> libc++, gcc -> libstdc++); pass "libstdcpp" or
# "libcxx" to override, except gcc + libc++ is rejected because the libc++ path
# is clang-only. Add "tidy" to run clang-tidy during the
# build. Add a sanitizer mode ("asan" [which includes ubsan], "tsan", "ubsan",
# or "msan") to instrument the build with the corresponding LLVM sanitizer.
# The build is incremental when the configuration is unchanged; add "clean" to
# force a wipe, fresh configure, and full recompile.
# Add "coverage" to build with source-based coverage instrumentation and run
# llvm-profdata/llvm-cov after tests pass; mutually exclusive with sanitizers
# and tidy. Add "scan" to skip the build and run the Clang Static Analyzer
# (`analyze-build`) against the compile_commands.json instead; mutually
# exclusive with everything else. Pass "reconfigure" to do a configure-only
# full refresh of tests/build (regenerates compile_commands.json for clangd)
# without building or running anything; also standalone. The default is clang
# with `libcxx`, no tidy, no sanitizer, no coverage, no scan.
#
# CUDA: when nvcc and g++-15 are installed and the mode is plain (no sanitizer,
# coverage, or scan), the *.cu sources in tests/ build alongside the *.cpp
# suite as their own nvcc/libstdc++ executables, regardless of the compiler and
# standard library chosen for the C++ files. Pass a *.cu filename to build and
# run just one.
#
# Pass "clean-all" to delete the buildable outputs and stop: the release tree
# (tests/build), the IDE debug tree (tests/build-debug), and any legacy
# in-source strays at the repo root. Standalone; builds nothing. The dependency
# caches (tests/.fetchcontent, tests/.fetchcontent-debug, tests/.local) are
# expensive to rebuild and are preserved, same as "clean".
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
compiler=""
use_tidy=false
sanitizer=""
use_coverage=false
use_scan=false
use_reconfigure=false
use_clean=false
test_name=""
target_name=""

usage="Usage: $0 [all | reconfigure | clean-all | [testname.cpp|testname.cu] [clang|gcc] [libstdcpp|libcxx] [clean] [tidy] [asan|tsan|ubsan|msan] [coverage] [scan]]"

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

# "clean-all" deletes the buildable outputs and stops: both build trees and
# any legacy in-source strays at the repo root. The dependency caches
# (tests/.fetchcontent, tests/.fetchcontent-debug, tests/.local) survive,
# same as "clean". Note clangd loses compile_commands.json until the next
# configure; "reconfigure" restores it without building.
if [[ "${1:-}" == "clean-all" ]]; then
  if [[ $# -gt 1 ]]; then
    echo "$0: 'clean-all' takes no other arguments" >&2
    echo "$usage" >&2
    exit 1
  fi
  rm -f CMakeCache.txt cmake_install.cmake ClangExeProject.sln build.ninja
  rm -rf CMakeFiles .ninja_deps .ninja_log
  rm -rf tests/build tests/build-debug
  echo "Removed tests/build and tests/build-debug (dependency caches preserved)."
  exit 0
fi

if [[ $# -gt 0 && "$1" != "libstdcpp" && "$1" != "libcxx" \
      && "$1" != "clang" && "$1" != "gcc" \
      && "$1" != "tidy" && "$1" != "--tidy" \
      && "$1" != "asan" && "$1" != "tsan" && "$1" != "ubsan" \
      && "$1" != "msan" && "$1" != "coverage" && "$1" != "scan" \
      && "$1" != "clean" && "$1" != "reconfigure" ]]; then
  if [[ "$1" == *.cpp || "$1" == *.cu ]]; then
    test_name="$1"
    target_name="${test_name%.*}"
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
    clang|gcc)
      compiler="$arg"
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
    reconfigure)
      use_reconfigure=true
      ;;
    clean)
      use_clean=true
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
  if [[ -n "$sanitizer" ]] || $use_tidy || $use_coverage || $use_clean || [[ -n "$test_name" ]]; then
    echo "$0: 'scan' takes no other arguments (configure-only static analysis)" >&2
    exit 1
  fi
fi

# reconfigure is a configure-only full refresh of tests/build (for clangd's
# compile_commands.json); it builds nothing and takes no other mode.
if $use_reconfigure; then
  if [[ -n "$sanitizer" ]] || $use_tidy || $use_coverage || $use_scan || $use_clean || [[ -n "$test_name" ]]; then
    echo "$0: 'reconfigure' takes no other arguments (configure-only refresh)" >&2
    exit 1
  fi
fi

# CUDA (.cu) targets build automatically in plain modes when the toolchain is
# present, independently of the compiler/stdlib chosen for the .cpp files: each
# .cu is its own single-source executable (nvcc, g++-15 host, libstdc++) and
# shares no link line with a .cpp binary. nvcc rejects gcc newer than 15, so we
# pin its host compiler to g++-15 and target the build host's GPU (native).
# Skip CUDA under sanitizers/coverage/scan: clang's instrumentation does not
# apply to nvcc, and those flags would break the g++-15-driven CUDA link.
CUDA_OPTION=""
if [[ -z "$sanitizer" ]] && ! $use_coverage && ! $use_scan; then
  if command -v nvcc >/dev/null 2>&1 && command -v g++-15 >/dev/null 2>&1; then
    CUDA_OPTION="-DCORVID_ENABLE_CUDA=ON -DCMAKE_CUDA_HOST_COMPILER=$(command -v g++-15) -DCMAKE_CUDA_ARCHITECTURES=native"
  fi
fi

# A requested .cu source needs that toolchain and a plain mode; fail clearly
# rather than configuring a build that silently produces no target.
if [[ "$test_name" == *.cu && -z "$CUDA_OPTION" ]]; then
  echo "$0: '$test_name' needs CUDA (nvcc + g++-15) in a plain build mode (no sanitizer/coverage/scan)" >&2
  exit 1
fi

# Default the compiler to clang; gcc is opt-in.
if [[ -z "$compiler" ]]; then
  compiler="clang"
fi

# The standard library defaults to match the compiler: clang pairs with
# libc++, gcc with libstdc++. An explicit libstdcpp/libcxx arg overrides this.
if [[ -z "$choice" ]]; then
  if [[ "$compiler" == "gcc" ]]; then
    choice="libstdcpp"
  else
    choice="libcxx"
  fi
fi

# The libc++ path below is clang-only (it hardcodes -nostdinc++, the libc++
# include prefix, compiler-rt, and lld), so gcc + libc++ cannot work here.
if [[ "$compiler" == "gcc" && "$choice" == "libcxx" ]]; then
  echo "$0: gcc cannot build against libc++; use 'gcc libstdcpp'" >&2
  exit 1
fi

if [[ "$choice" == "libstdcpp" ]]; then
  LIBSTD_OPTION="-DUSE_LIBSTDCPP=ON"
  if [[ "$compiler" == "gcc" ]]; then
    echo "Using gcc with libstdc++"
    export CC="$(command -v gcc)"
    export CXX="$(command -v g++)"
  else
    echo "Using clang with libstdc++"
    export CC="$(command -v clang)"
    export CXX="$(command -v clang++)"
  fi
else
  echo "Using clang with libc++"
  export CC="/usr/bin/clang-22"
  export CXX="/usr/bin/clang++-22"
  LIBSTD_OPTION="-DUSE_LIBSTDCPP=OFF"
fi

if $use_tidy; then
  TIDY_OPTION="-DUSE_CLANG_TIDY=ON"
else
  TIDY_OPTION=""
fi

# A named test scopes the build (--target) and the run (ctest -R), not the
# configure: the full configure is a strict superset (TEST_NAME only prunes
# targets), so leaving it out of the configure args lets single-test and
# full-suite runs share one signature and one object cache, and keeps
# compile_commands.json complete for clangd. Validate the filename here so a
# typo gets a clear error rather than ninja's "unknown target".
if [[ -n "$test_name" ]]; then
  found=false
  for bucket in portable linux cuda; do
    [[ -f "tests/$bucket/$test_name" ]] && found=true
  done
  if ! $found; then
    echo "$0: test source '$test_name' not found under tests/{portable,linux,cuda}/" >&2
    exit 1
  fi
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

# Strip asserts (define NDEBUG) for the plain and coverage builds. Keep them
# live for the analysis modes -- sanitizers, tidy, and scan (CSA) -- where the
# assert checks, and the facts they hand the static analyzer, are the point.
if ! $use_tidy && ! $use_scan && [[ -z "$sanitizer" ]]; then
  NDEBUG_OPTION="-DCORVID_DISABLE_ASSERTS=ON"
else
  NDEBUG_OPTION=""
fi

# Define the build directory (assuming you're using an out-of-source build)
buildRoot="tests/build"
buildDir="$buildRoot/release_bin"
sigFile="$buildRoot/.cleanbuild-config"

# Signature of everything that determines the CMake configuration. When the
# existing build dir already carries a matching signature, the configuration
# cannot have changed, so we skip the wipe and the cold reconfigure (~19s of
# nvcc/find_package probing that would only reproduce identical build files)
# and let ninja rebuild incrementally. A changed option, a first run, or an
# explicit "clean" falls through to the wipe-and-reconfigure path. Some modes
# always take that path: tidy because clang-tidy runs as part of compilation,
# so an incremental no-op build would analyze nothing and report a falsely
# clean summary; msan because the ignorelist affects codegen but is not a
# ninja dependency, so an incremental build would keep stale objects after an
# ignorelist edit (CCACHE_EXTRAFILES only fixes ccache hits on recompiles, not
# skipped recompiles); scan and reconfigure because they are configure-only.
# Editing CMakeLists.txt still reconfigures: `cmake --build` re-runs CMake
# itself when the lists file is newer than the cache.
configSig="$LIBSTD_OPTION|$TIDY_OPTION|$SAN_OPTION|$COV_OPTION|$CUDA_OPTION|$NDEBUG_OPTION|CC=$CC|CXX=$CXX"

reuse=false
if ! $use_clean && ! $use_tidy && ! $use_scan && ! $use_reconfigure &&
  [[ "$sanitizer" != "msan" ]] &&
  [[ -f "$sigFile" && "$(cat "$sigFile")" == "$configSig" ]]; then
  reuse=true
fi

if $reuse; then
  if [[ -n "$test_name" ]]; then
    echo "Reusing configured build dir for $test_name (config unchanged); skipping wipe and reconfigure."
    # A named test still gets a fresh executable: drop just this target's
    # binary so it relinks, leaving the cached objects and configuration in
    # place.
    rm -f "$buildDir/$target_name"
  else
    echo "Reusing configured build dir (config unchanged); incremental build."
  fi
else
  # Clean any stray artifacts from a legacy in-source build at the repo root.
  rm -f CMakeCache.txt cmake_install.cmake ClangExeProject.sln build.ninja
  rm -rf CMakeFiles .ninja_deps .ninja_log

  # Remove and recreate the CMake build directory, then configure from scratch.
  if [ -d "$buildDir" ]; then
    echo "Cleaning the build directory at $buildDir"
  else
    echo "Build directory not found. Creating a new one at $buildDir"
  fi
  rm -rf "$buildRoot"
  mkdir -p "$buildRoot" "$buildDir"

  # Run cmake to configure the project with Ninja and the selected compiler.
  cmake -S tests -B "$buildRoot" -G "Ninja" $LIBSTD_OPTION $TIDY_OPTION $SAN_OPTION $COV_OPTION $CUDA_OPTION $NDEBUG_OPTION

  # Record the signature so the next matching single-file run can reuse this.
  echo "$configSig" >"$sigFile"
fi

# reconfigure stops here: the configure above already regenerated a full
# compile_commands.json, which is all clangd needs. No build, no ctest.
if $use_reconfigure; then
  echo "Reconfigured $buildRoot; compile_commands.json refreshed for clangd."
  exit 0
fi

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

# Run the build: everything from scratch after a wipe, incremental on reuse.
if $use_tidy; then
  tidyLogFile="$(pwd)/$buildRoot/tidy.log"
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
# registered, so ctest naturally skips them -- except a single `notest_*` file
# requested by name, which is run directly below.
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
# A named test runs alone: the unfiltered configure registers every test, so
# scope ctest to the one target.
if [[ -n "$target_name" ]]; then
  ctest_args+=(-R "^${target_name}$")
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
if [[ -n "$target_name" && "$target_name" == notest_* ]]; then
  # A `notest_*` source opts out of CTest, so nothing is registered to run.
  # A build-everything run skips it (the point of the prefix), but an explicit
  # single-file request means "build AND run this one", so run it directly.
  notest_bin="$buildDir/$target_name"
  if [[ -x "$notest_bin" ]]; then
    echo "Running $notest_bin"
    "$notest_bin" || ctest_status=$?
  else
    echo "$0: built binary not found: $notest_bin" >&2
    ctest_status=1
  fi
else
  ctest "${ctest_args[@]}" || ctest_status=$?
fi

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
  else
    echo "clean: no warnings"
  fi
  echo
  echo "Full log: $tidyLogFile"
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
  # all counters. A named test scopes the report to its own binary: on a
  # reused tree, binaries from earlier runs may exist but ran nothing, and
  # their zero counters would dilute the report.
  bins=()
  if [[ -n "$target_name" ]]; then
    bins=("$buildDir/$target_name")
  else
    shopt -s nullglob
    for f in "$buildDir"/*; do
      base="$(basename "$f")"
      [[ "$base" == notest_* ]] && continue
      [[ -x "$f" && -f "$f" ]] && bins+=("$f")
    done
    shopt -u nullglob
  fi
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
