#!/bin/bash

# Single-file build dispatcher for the VSCode default build task
# (.vscode/tasks.json). Routes by file extension so Ctrl+Shift+B works on
# either kind of source: a .cu file compiles with nvcc (g++-15 host,
# libstdc++); anything else compiles with clang++/libc++, matching the
# historical single-file debug build. The output lands at
# <dir>/debug_bin/<stem>.exe, where launch.json (CodeLLDB) and the "Run
# Executable" task find it. The .exe suffix is used on both platforms (harmless
# on Linux) so one tasks.json/launch.json path template also serves the Windows
# build, where binaries carry .exe.

# Fail fast.
set -e

src="$1"
if [[ -z "$src" ]]; then
  echo "usage: $0 <source-file>" >&2
  exit 2
fi

srcDir="$(dirname "$src")"
stem="$(basename "${src%.*}")"
outDir="$srcDir/debug_bin"
mkdir -p "$outDir"
out="$outDir/$stem.exe"

# Test sources include library headers root-relative ("corvid/...") and the
# shared Catch2 main wrapper by bare name ("catch2_main.h"), so put the repo
# root and tests/ on the include path. Both the .cu and .cpp paths use these.
workspace="$(cd "$(dirname "$0")/.." && pwd)"
incdirs=(-I "$workspace" -I "$workspace/tests")

if [[ "$src" == *.cu ]]; then
  # nvcc rejects gcc newer than 15, so pin the host compiler to g++-15. -g emits
  # host debug info (lldb can step host-side code); -G adds device debug info
  # for cuda-gdb. -arch=native targets the build host's GPU. .cu tests use
  # Catch2 (via catch2_main.h) like the rest of the suite; .cu is libstdc++, so
  # link the system (apt) Catch2, matching the CMake build. It supplies main()
  # via Catch::Session, so only libCatch2 is needed (not libCatch2Main).
  #
  # Auto-link cuBLAS for sources that use it, matching `nvcc ... -lcublas` and
  # the CMake build's CUDA::cublas opt-in.
  cublas=()
  if grep -q cublas "$src"; then
    cublas=(-lcublas)
  fi
  exec nvcc \
    -ccbin /usr/bin/g++-15 \
    -std=c++23 \
    -arch=native \
    -g -G -O0 \
    "${incdirs[@]}" \
    "$src" \
    /usr/lib/libCatch2.a \
    "${cublas[@]}" \
    -o "$out"
fi

# .cpp / .cc / default: clang++ with libc++, mirroring the previous clang-build
# task verbatim.
fc="$workspace/tests/.fetchcontent"
loc="$workspace/tests/.local"

exec /usr/bin/clang++-22 \
  -std=c++23 \
  -stdlib=libc++ \
  -fexperimental-library \
  -march=native \
  -g \
  -O0 \
  -DDEBUG \
  -D_LIBCPP_NO_ABI_TAG \
  -Wall \
  -Wextra \
  -Werror \
  -Wno-unused-variable \
  "${incdirs[@]}" \
  -isystem "$fc/catch2-src/src" \
  -isystem "$fc/catch2-build/generated-includes" \
  -isystem "$loc/ngtcp2/include" \
  -isystem "$loc/nghttp3/include" \
  -isystem "$loc/openssl/include" \
  "$src" \
  "$fc/catch2-build/src/libCatch2.a" \
  "$loc/ngtcp2/lib/libngtcp2_crypto_ossl.a" \
  "$loc/ngtcp2/lib/libngtcp2.a" \
  "$loc/nghttp3/lib/libnghttp3.a" \
  "$loc/openssl/lib64/libssl.a" \
  "$loc/openssl/lib64/libcrypto.a" \
  -lc++experimental \
  -luring \
  -ldl \
  -o "$out"
