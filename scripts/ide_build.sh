#!/bin/bash

# Single-file IDE build for the VSCode default build task (.vscode/tasks.json).
# Drives a dedicated debug CMake tree (tests/build-debug) rather than invoking
# the compiler directly, so CMake stays the single source of truth for include
# paths AND link libraries; this script names none of its own. The requested
# target builds to tests/build-debug/debug_bin/<stem>.exe, where launch.json
# (CodeLLDB) and the "Run Executable" task look. The Linux counterpart of
# ide_build.ps1. See crossplatform.md.

# Fail fast.
set -e

src="$1"
if [[ -z "$src" ]]; then
  echo "usage: $0 <source-file>" >&2
  exit 2
fi
if [[ ! -f "$src" ]]; then
  echo "$0: source not found: $src" >&2
  exit 2
fi

repo="$(cd "$(dirname "$0")/.." && pwd)"
srcRoot="$repo/tests"
bldDir="$repo/tests/build-debug"
fcDebug="$repo/tests/.fetchcontent-debug"
stem="$(basename "${src%.*}")"

# clang/libc++ by default, matching cleanbuild.sh's default configuration.
export CC="/usr/bin/clang-22"
export CXX="/usr/bin/clang++-22"

# CUDA (.cu) targets compile with nvcc (g++-15 host); mirror cleanbuild.sh.
# Enabling CUDA whenever the toolchain is present keeps the configure signature
# stable across .cpp and .cu files, so switching files never forces a
# reconfigure.
cudaArgs=()
if command -v nvcc >/dev/null 2>&1 && command -v g++-15 >/dev/null 2>&1; then
  cudaArgs=(-DCORVID_ENABLE_CUDA=ON
    -DCMAKE_CUDA_HOST_COMPILER="$(command -v g++-15)"
    -DCMAKE_CUDA_ARCHITECTURES=native)
elif [[ "$src" == *.cu ]]; then
  echo "$0: '$src' is a .cu file but CUDA (nvcc + g++-15) was not found" >&2
  exit 1
fi

# Configure the debug tree: libc++ like cleanbuild's default, IDE_DEBUG flipping
# the flags to -O0 -g (asserts live). Its own FetchContent base keeps it from
# fighting cleanbuild's release tree over the shared Catch2 build. No TEST_NAME:
# all targets configure once, then a single --target builds on demand. Reuse the
# configured tree when the signature is unchanged, as cleanbuild does, so only
# the first build pays the configure cost.
cfg=(-S "$srcRoot" -B "$bldDir" -G Ninja -DUSE_LIBSTDCPP=OFF
  -DIDE_DEBUG=ON -DFETCHCONTENT_BASE_DIR="$fcDebug" "${cudaArgs[@]}")
sigFile="$bldDir/.ide-build-config"
configSig="$(IFS='|'; echo "${cfg[*]}")|CC=$CC|CXX=$CXX"
if [[ ! -f "$sigFile" || ! -f "$bldDir/CMakeCache.txt" \
      || "$(cat "$sigFile")" != "$configSig" ]]; then
  cmake --fresh "${cfg[@]}"
  echo "$configSig" >"$sigFile"
fi

# Build just the requested target. CMake supplies its flags and link libraries;
# debug info lands next to the exe in debug_bin/ on its own.
cmake --build "$bldDir" --target "$stem"
