#!/bin/bash
# Build an MSAN-instrumented libc++/libc++abi/libunwind for use with
# `./cleanbuild.sh msan`. One-time, slow (~10 min on a fast box, longer on
# WSL).
#
# Installs to tests/.local/llvm-msan/ (gitignored). Idempotent: skips the
# clone and build if the install already exists. Delete that directory to
# force a fresh build.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_PREFIX="$REPO_ROOT/tests/.local/llvm-msan"
SRC_DIR="$REPO_ROOT/tests/.local/llvm-project-src"
BUILD_DIR="$REPO_ROOT/tests/.local/llvm-msan-build"
IGNORELIST="$REPO_ROOT/scripts/msan-libcxx-ignorelist.txt"

if [[ -f "$INSTALL_PREFIX/lib/libc++.a" \
   && -f "$INSTALL_PREFIX/lib/libc++abi.a" \
   && -f "$INSTALL_PREFIX/lib/libunwind.a" ]]; then
    echo "MSAN libc++ already installed at $INSTALL_PREFIX"
    echo "Delete that directory to force a rebuild (required after editing"
    echo "$IGNORELIST since it affects libc++ instrumentation)."
    exit 0
fi

mkdir -p "$REPO_ROOT/tests/.local"

if [[ ! -d "$SRC_DIR/.git" ]]; then
    echo "Cloning llvm-project (shallow) ..."
    git clone --depth=1 https://github.com/llvm/llvm-project.git "$SRC_DIR"
else
    echo "llvm-project source already present at $SRC_DIR"
fi

# Patch libunwind's CMakeLists.txt so LIBUNWIND_ADDITIONAL_COMPILE_FLAGS is
# applied PRIVATE rather than PUBLIC. The default PUBLIC propagation causes
# our -fno-sanitize=memory (added to keep libunwind from MSAN-recursing in
# the reporter) to leak into libcxx via target dependencies, silently
# disabling MSAN for libc++.so. Idempotent: only patches if not already done.
LIBUNWIND_CMAKE="$SRC_DIR/libunwind/src/CMakeLists.txt"
# Fixed-string matching (`grep -F`): the pattern contains `${...}` which some
# grep implementations (notably `ugrep` masquerading as `grep`) interpret as
# regex metacharacters. We want literal matching.
if grep -qF 'PUBLIC "${LIBUNWIND_ADDITIONAL_COMPILE_FLAGS}"' "$LIBUNWIND_CMAKE"; then
    echo "Patching libunwind CMakeLists.txt: PUBLIC -> PRIVATE for ADDITIONAL flags"
    sed -i 's|PUBLIC "${LIBUNWIND_ADDITIONAL_COMPILE_FLAGS}"|PRIVATE "${LIBUNWIND_ADDITIONAL_COMPILE_FLAGS}"|g' "$LIBUNWIND_CMAKE"
elif grep -qF 'PRIVATE "${LIBUNWIND_ADDITIONAL_COMPILE_FLAGS}"' "$LIBUNWIND_CMAKE"; then
    echo "libunwind CMakeLists.txt already patched (PRIVATE); skipping"
else
    echo "ERROR: libunwind CMakeLists.txt patch pattern not found." >&2
    echo "Expected either 'PUBLIC \"\${LIBUNWIND_ADDITIONAL_COMPILE_FLAGS}\"'" >&2
    echo "(unpatched) or 'PRIVATE ...' (already patched) in:" >&2
    echo "  $LIBUNWIND_CMAKE" >&2
    echo "Upstream libunwind may have refactored. Without this patch, the" >&2
    echo "-fno-sanitize=memory flag leaks PUBLIC into libcxx and silently" >&2
    echo "disables MSAN. Inspect the file and update this script." >&2
    exit 1
fi

# ccache speeds up repeat rebuilds. The ignorelist's path is on the command
# line, but its *content* is not part of ccache's hash by default, so list it
# in CCACHE_EXTRAFILES to ensure ignorelist edits invalidate stale entries.
CCACHE_LAUNCHER_FLAGS=()
if command -v ccache >/dev/null 2>&1; then
    CCACHE_LAUNCHER_FLAGS=(
        -DCMAKE_C_COMPILER_LAUNCHER=ccache
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
    export CCACHE_EXTRAFILES="$IGNORELIST"
    echo "ccache enabled; CCACHE_EXTRAFILES=$CCACHE_EXTRAFILES"
fi

echo "Configuring libc++ build with MSAN instrumentation ..."
cmake -G Ninja -S "$SRC_DIR/runtimes" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/clang-22 \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 \
    "${CCACHE_LAUNCHER_FLAGS[@]}" \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
    -DLLVM_USE_SANITIZER=MemoryWithOrigins \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DLIBCXX_INCLUDE_TESTS=OFF \
    -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
    -DLIBCXX_ADDITIONAL_COMPILE_FLAGS="-fsanitize-ignorelist=$IGNORELIST" \
    -DLIBCXXABI_INCLUDE_TESTS=OFF \
    -DLIBCXXABI_ADDITIONAL_COMPILE_FLAGS="-fsanitize-ignorelist=$IGNORELIST" \
    -DLIBUNWIND_INCLUDE_TESTS=OFF \
    -DLIBUNWIND_ADDITIONAL_COMPILE_FLAGS="-fno-sanitize=memory" \
    -DLIBUNWIND_ADDITIONAL_C_FLAGS="-fno-sanitize=memory"

echo "Building libc++/libc++abi/libunwind (slow) ..."
ninja -C "$BUILD_DIR" cxx cxxabi unwind

echo "Installing to $INSTALL_PREFIX ..."
ninja -C "$BUILD_DIR" install-cxx install-cxxabi install-unwind

echo ""
echo "Done. MSAN libc++ installed at $INSTALL_PREFIX"
echo "To save disk: rm -rf $BUILD_DIR $SRC_DIR"
