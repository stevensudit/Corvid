#!/bin/bash
# Build OpenSSL 3.5.x (LTS) for use with `./cleanbuild.sh quic`. Required
# because Ubuntu Noble ships OpenSSL 3.0.13 and ngtcp2's QUIC API path needs
# the 3.5+ native QUIC API. One-time, slow (~5 min on a fast box, longer on
# WSL).
#
# Installs to tests/.local/openssl/ (gitignored). Static-only (no-shared) so
# downstream targets link statically and no LD_LIBRARY_PATH is needed.
# Idempotent: skips the clone and build if the install already exists. Delete
# that directory to force a fresh build.
#
# Pass `msan` to build an MSAN-instrumented variant into
# tests/.local/openssl-msan/ for `./cleanbuild.sh msan`. The plain and MSAN
# installs coexist (cleanbuild's `all` runs both flavors), so this is a
# separate one-time build, not a replacement. Two things make the MSAN variant
# differ from the plain one:
#   - clang (not the default compiler) with `-fsanitize=memory`, so MSAN sees
#     OpenSSL's stores and stops treating its heap as poisoned. Without an
#     instrumented OpenSSL, its internal reads (lhash, config parsing, the many
#     `memcmp` sites) flood every QUIC test with false positives.
#   - `no-asm`: OpenSSL's crypto uses hand-written assembly that MSAN cannot
#     instrument. Left enabled, those routines write through buffers MSAN never
#     observes, reproducing inside OpenSSL the very false positives this build
#     exists to eliminate. `no-asm` forces the C implementations.

set -e

FLAVOR="${1:-plain}"
if [[ "$FLAVOR" != "plain" && "$FLAVOR" != "msan" ]]; then
    echo "Usage: $0 [plain|msan]" >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OPENSSL_VERSION="${OPENSSL_VERSION:-openssl-3.5.6}"
# Both flavors build from the same checkout; only the build and install dirs
# differ, so the two installs coexist.
SRC_DIR="$REPO_ROOT/tests/.local/openssl-src"
if [[ "$FLAVOR" == "msan" ]]; then
    INSTALL_PREFIX="$REPO_ROOT/tests/.local/openssl-msan"
    BUILD_DIR="$REPO_ROOT/tests/.local/openssl-msan-build"
else
    INSTALL_PREFIX="$REPO_ROOT/tests/.local/openssl"
    BUILD_DIR="$REPO_ROOT/tests/.local/openssl-build"
fi

if [[ -f "$INSTALL_PREFIX/lib64/libssl.a" \
   && -f "$INSTALL_PREFIX/lib64/libcrypto.a" \
   && -f "$INSTALL_PREFIX/include/openssl/quic.h" ]]; then
    echo "OpenSSL ($FLAVOR) already installed at $INSTALL_PREFIX"
    echo "Delete that directory to force a rebuild."
    exit 0
fi

mkdir -p "$REPO_ROOT/tests/.local"

if [[ ! -d "$SRC_DIR/.git" ]]; then
    echo "Cloning OpenSSL $OPENSSL_VERSION (shallow) ..."
    git clone --depth=1 --branch "$OPENSSL_VERSION" \
        https://github.com/openssl/openssl.git "$SRC_DIR"
else
    echo "OpenSSL source already present at $SRC_DIR"
    # Verify the existing checkout matches the requested tag, since a stale
    # checkout would silently produce the wrong version's libraries.
    actual_tag="$(git -C "$SRC_DIR" describe --tags --exact-match 2>/dev/null || true)"
    if [[ "$actual_tag" != "$OPENSSL_VERSION" ]]; then
        echo "ERROR: existing OpenSSL checkout is at '$actual_tag', expected '$OPENSSL_VERSION'." >&2
        echo "Delete $SRC_DIR and rerun this script." >&2
        exit 1
    fi
fi

# OpenSSL builds out-of-tree via a copy of Configure into BUILD_DIR. The
# `--prefix=...` lands the install under tests/.local/openssl/ where the CMake
# build will look for it.
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

config_args=(
    --prefix="$INSTALL_PREFIX"
    --openssldir="$INSTALL_PREFIX/etc/ssl"
    --libdir=lib64
    no-shared
    no-tests
    no-docs
)
if [[ "$FLAVOR" == "msan" ]]; then
    # MSAN cflags must match the test-side flags in tests/CMakeLists.txt: plain
    # `-fsanitize=memory` (no origin tracking, which overflows the stack in our
    # templated TUs) plus frame pointers and `-g` for readable traces. `no-asm`
    # is the load-bearing flag (see header comment). Trailing flag arguments
    # are appended to OpenSSL's CFLAGS by Configure.
    export CC=clang-22
    config_args+=(
        no-asm
        linux-x86_64
        -fsanitize=memory
        -fno-omit-frame-pointer
        -g
    )
    echo "Configuring OpenSSL $OPENSSL_VERSION (MSAN, no-asm, static) ..."
else
    config_args+=(linux-x86_64)
    echo "Configuring OpenSSL $OPENSSL_VERSION (static, no-tests, no-docs) ..."
fi
"$SRC_DIR/Configure" "${config_args[@]}"

if [[ "$FLAVOR" == "msan" ]]; then
    # Build and install only the libraries and headers. The `openssl` app would
    # link the MSAN runtime (extra flags, extra time) and we never run it.
    echo "Building OpenSSL libraries (MSAN, parallel) ..."
    make -j"$(nproc)" build_libs
    echo "Installing libraries + headers to $INSTALL_PREFIX ..."
    make install_dev
else
    echo "Building OpenSSL (parallel) ..."
    make -j"$(nproc)"
    # install_sw installs libraries, headers, and pkgconfig files but skips man
    # pages and HTML docs (which we disabled anyway). Saves a chunk of time.
    echo "Installing to $INSTALL_PREFIX ..."
    make install_sw
fi

echo ""
echo "Done. OpenSSL ($FLAVOR) installed at $INSTALL_PREFIX"
echo "To save disk: rm -rf $BUILD_DIR"
