---
name: build-and-test
description: How to build, run, format, sanitize, and write tests for the Corvid project via ./cleanbuild.sh and ./format_all.sh. Use whenever building, running tests, verifying a change, formatting before commit, running clang-tidy / ASAN / TSAN / UBSAN / MSAN, or writing a Catch2 test in this repo.
---

# Build and test

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

## Writing tests

Framework: Catch2 v3. Each test source includes `tests/catch2_main.h` (provides
Catch2 macros + `main`), uses `TEST_CASE("Name", "[tag]")` for test cases and
`SECTION("desc")` for sub-blocks. Assertions are `CHECK` / `REQUIRE` (or
`_FALSE` / `_THROWS_AS` variants). Tests may be per-class, per-group, or
per-subfolder; check `tests/` before asking about coverage. Sources prefixed
`notest_` are built but not run as part of the sweep.

Always wrap the `TEST_CASE`s in a file with a
`// NOLINTBEGIN(readability-function-cognitive-complexity)` line before the
first one and a matching
`// NOLINTEND(readability-function-cognitive-complexity)` after the last. The
`CHECK` / `REQUIRE` macros expand into enough branching to trip the clang-tidy
cognitive-complexity check, so every test file suppresses it this way.
