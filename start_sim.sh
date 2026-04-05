#!/bin/bash

# Build and run the CorvidSim server.
#
# Safe to run from a fresh clone: installs npm dependencies, builds the
# TypeScript frontend, compiles the C++ server with full optimizations, and
# launches it on http://localhost:8080/.
#
# Subsequent runs skip unchanged work: cmake and npm only rebuild what has
# changed. The C++ binary goes to tests/build_sim/ (separate from the main
# test build directory) so this script does not disturb normal dev builds.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

# C++ build --------------------------------------------------------------

export CC="/usr/bin/clang-22"
export CXX="/usr/bin/clang++-22"

BUILD_DIR="tests/build_sim"

echo "--- Configuring C++ build ---"
cmake -S tests -B "$BUILD_DIR" -G Ninja \
    -DUSE_LIBSTDCPP=OFF \
    -DTEST_NAME=corvid_sim.cpp \
    --log-level=WARNING

echo "--- Building corvid_sim ---"
cmake --build "$BUILD_DIR" --config Release --target corvid_sim

# Frontend build ---------------------------------------------------------

WEB_DIR="corvid/sim/web"

echo "--- Installing npm dependencies ---"
npm --prefix "$WEB_DIR" install

echo "--- Building frontend ---"
npm --prefix "$WEB_DIR" run build

# Launch -----------------------------------------------------------------

BINARY="$BUILD_DIR/release_bin/corvid_sim"
echo "--- Starting server ---"
exec "$BINARY"
