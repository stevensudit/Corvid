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

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OPENSSL_VERSION="${OPENSSL_VERSION:-openssl-3.5.6}"
INSTALL_PREFIX="$REPO_ROOT/tests/.local/openssl"
SRC_DIR="$REPO_ROOT/tests/.local/openssl-src"
BUILD_DIR="$REPO_ROOT/tests/.local/openssl-build"

if [[ -f "$INSTALL_PREFIX/lib64/libssl.a" \
   && -f "$INSTALL_PREFIX/lib64/libcrypto.a" \
   && -f "$INSTALL_PREFIX/include/openssl/quic.h" ]]; then
    echo "OpenSSL already installed at $INSTALL_PREFIX"
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

echo "Configuring OpenSSL $OPENSSL_VERSION (static, no-tests, no-docs) ..."
"$SRC_DIR/Configure" \
    --prefix="$INSTALL_PREFIX" \
    --openssldir="$INSTALL_PREFIX/etc/ssl" \
    --libdir=lib64 \
    no-shared \
    no-tests \
    no-docs \
    linux-x86_64

echo "Building OpenSSL (parallel) ..."
make -j"$(nproc)"

# install_sw installs libraries, headers, and pkgconfig files but skips man
# pages and HTML docs (which we disabled anyway). Saves a chunk of time.
echo "Installing to $INSTALL_PREFIX ..."
make install_sw

echo ""
echo "Done. OpenSSL installed at $INSTALL_PREFIX"
echo "To save disk: rm -rf $BUILD_DIR $SRC_DIR"
