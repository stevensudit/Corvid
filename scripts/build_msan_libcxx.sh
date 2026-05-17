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

if [[ -f "$INSTALL_PREFIX/lib/libc++.a" ]]; then
    echo "MSAN libc++ already installed at $INSTALL_PREFIX"
    echo "Delete that directory to force a rebuild."
    exit 0
fi

mkdir -p "$REPO_ROOT/tests/.local"

if [[ ! -d "$SRC_DIR/.git" ]]; then
    echo "Cloning llvm-project (shallow) ..."
    git clone --depth=1 https://github.com/llvm/llvm-project.git "$SRC_DIR"
else
    echo "llvm-project source already present at $SRC_DIR"
fi

echo "Configuring libc++ build with MSAN instrumentation ..."
cmake -G Ninja -S "$SRC_DIR/runtimes" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/clang-22 \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
    -DLLVM_USE_SANITIZER=MemoryWithOrigins \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DLIBCXX_INCLUDE_TESTS=OFF \
    -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
    -DLIBCXXABI_INCLUDE_TESTS=OFF \
    -DLIBUNWIND_INCLUDE_TESTS=OFF

echo "Building libc++/libc++abi/libunwind (slow) ..."
ninja -C "$BUILD_DIR" cxx cxxabi unwind

echo "Installing to $INSTALL_PREFIX ..."
ninja -C "$BUILD_DIR" install-cxx install-cxxabi install-unwind

echo ""
echo "Done. MSAN libc++ installed at $INSTALL_PREFIX"
echo "To save disk: rm -rf $BUILD_DIR $SRC_DIR"
