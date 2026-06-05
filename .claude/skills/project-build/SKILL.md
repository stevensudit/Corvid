---
name: project-build
description: How to build, test, format, and sanitize the Corvid project via ./cleanbuild.sh and ./format_all.sh. Use whenever building, running tests, verifying a change, formatting before commit, or running clang-tidy / ASAN / TSAN / UBSAN / MSAN in this repo.
---

# Project build

Corvid builds and runs its tests through `./cleanbuild.sh`, and formats through
`./format_all.sh`. Never hand-run cmake or ninja in `tests/build/`: that
directory goes stale and breaks the FetchContent generator. `CMakeLists.txt`
lives in `tests/` only; there is no root one. Build output lands in
`tests/build/`.

## Commands

```sh
./cleanbuild.sh                  # clean build + run all tests (libc++)
./cleanbuild.sh libstdcpp        # use libstdc++
./cleanbuild.sh tidy             # also run clang-tidy
./cleanbuild.sh asan             # build + run with ASAN + UBSAN
./cleanbuild.sh tsan             # build + run with TSAN
./cleanbuild.sh ubsan            # build + run with UBSAN only
./cleanbuild.sh msan             # build + run with MSAN (needs one-time setup)
./cleanbuild.sh strings_test.cpp # build + run only that target
./format_all.sh                  # format all sources (run before commit)
```

## Arguments and sanitizer behavior

Sanitizer modes accept the same `[testname.cpp]` and `libstdcpp|libcxx`
arguments as the plain build. `asan`/`tsan`/`msan` are mutually exclusive (each
instruments a conflicting runtime); run them separately. Sanitizer sweeps
continue past test failures so all issues surface in one run; plain runs still
bail on the first failure.

## MSAN one-time setup

MSAN requires a one-time setup: run `scripts/build_msan_libcxx.sh` and
`scripts/build_openssl_quic.sh msan` before `./cleanbuild.sh msan`. Both build
instrumented dependencies (libc++ and OpenSSL/ngtcp2) so the QUIC tests don't
drown in false positives from uninstrumented library internals.
