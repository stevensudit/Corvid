# Cross-Platform Build

Status: portable Windows port complete. The portable test suite builds and
passes on Linux (clang), native Windows (clang-cl), and native Windows (MSVC
cl): 39 of 39 portable tests on each Windows compiler, and the full Linux suite
(portable plus linux buckets) stays green. CUDA on Windows is the remaining
piece (see "Remaining work"). This document records how cross-platform
capability is structured and maintained; update it when the structure changes.

## 1. Goal and shape

Corvid keeps every Linux capability it has. The Windows port does not reduce the
library to a lowest-common-denominator subset. Instead the tree is partitioned
into buckets and the build system selects what each platform compiles. The
concrete motivation is CUDA development on native Windows (WSL CUDA is painful),
so the portable library core and the CUDA tests must build and debug under
native Windows. The Linux-only networking stack (epoll, io_uring, QUIC, HTTP/3)
is never expected to compile on Windows; it is excluded from the Windows build,
not ported. No IOCP port is planned.

## 2. Source buckets

Three buckets, selected by platform at configure time:

| Bucket   | Builds on            | Depends on                            |
|----------|----------------------|---------------------------------------|
| portable | every platform       | std C++23 and portable corvid headers |
| linux    | Linux only           | epoll, io_uring, sockets, QUIC        |
| cuda     | wherever nvcc + host | CUDA toolkit (plus Catch2)            |

Tests live under `tests/portable/`, `tests/linux/`, and `tests/cuda/`.
`tests/CMakeLists.txt` globs each bucket separately. The linux bucket and its
liburing / OpenSSL / ngtcp2 / nghttp3 dependencies sit behind
`if(CMAKE_SYSTEM_NAME STREQUAL "Linux")`, so Windows configures none of them.
This also speeds the Linux build: portable tests no longer drag in the QUIC
stack. CUDA is its own bucket, not a sub-case of linux, because building the
`.cu` tests on Windows is the whole point.

The `notest_` filename prefix means "build the executable but do not register a
CTest" (servers, demos, tutorials); it is orthogonal to bucket. The io_uring
tests carry a `RESOURCE_LOCK` (keyed on the `iou_` / `quic_dgram_` filename
prefix) to serialize their memlock pressure.

### Include style

Test sources include library headers root-relative (`corvid/...`), with the repo
root on the include path. A test's includes therefore do not depend on how deep
the file sits, so moving a test between buckets is free. The shared
`tests/catch2_main.h` is included by bare name.

## 3. Toolchains

| Platform | Portable suite          | CUDA bucket           |
|----------|-------------------------|-----------------------|
| Linux    | clang + libc++          | nvcc + g++-15         |
| Windows  | clang-cl (default), cl  | nvcc + cl host (TODO) |

On Windows the portable suite builds with clang-cl by default. Native clang on
Windows is a wanted capability in its own right (it also enables clang's
sanitizers and keeps clangd's flags aligned), not merely a fallback. MSVC cl is
supported as a second compiler, mirroring the clang/gcc choice on Linux. CUDA
uses nvcc with cl as host because nvcc requires cl on Windows; this mirrors
Linux, where the `.cu` bucket already uses nvcc + g++-15 rather than the `.cpp`
suite's clang.

Versions used during bring-up: clang-cl and clang-format from LLVM 22.1.7; MSVC
STL 14.4x and Windows SDK 10.0.26100 via VS 2022 Community; CMake 4.3, Ninja,
nvcc 13.3.

## 4. Building

Linux (`./cleanbuild.sh`) is unchanged. The full menu (libc++/libstdc++,
asan/tsan/ubsan/msan, scan, coverage, single-file) is documented in the
build-and-test skill.

Windows (`./cleanbuild.ps1`) does a fresh Release configure, build, and ctest:

- `./cleanbuild.ps1`: clang-cl, whole portable suite.
- `./cleanbuild.ps1 <name>_test.cpp`: one test (via `-DTEST_NAME=`).
- `./cleanbuild.ps1 cl`: build with MSVC cl instead of clang-cl.
- `./cleanbuild.ps1 asan`: ASAN (which carries UBSAN), clang-cl only.

The script enters a VS Developer shell for INCLUDE/LIB (skipped if already inside
one), configures Ninja with the chosen compiler, builds with `-k 0`, and runs
ctest. It uses `cmake --fresh` rather than wiping `tests/build`, because clangd
holds an open handle on `compile_commands.json` there. The Linux-only cleanbuild
modes (libc++/libstdc++ choice, msan/tsan, analyze-build scan, llvm-cov
coverage, compiler-rt/lld swaps) have no MSVC analog and are deliberately absent.

Compiler flags are set in the `WIN32` branch of `tests/CMakeLists.txt`:

- C++23 is requested as `/std:c++latest`, overriding CMake's clang-cl default
  `-clang:-std=c++23`, which clangd mis-parses and drops.
- clang-cl: `/EHsc /clang:-Wall /clang:-Wextra /clang:-Werror`. Bare `-Wall` and
  `-Wextra` under clang-cl alias to MSVC's `/Wall` (close to `-Weverything`), so
  the real clang warnings are passed through the `/clang:` escape.
- cl: `/EHsc /permissive- /Zc:preprocessor /W4 /WX /wd4245 /wd4267 /wd4305
  /wd4310`. `/permissive-` turns on conformant two-phase name lookup;
  `/Zc:preprocessor` selects the conformant preprocessor, without which cl
  corrupts raw string literals passed through Catch2 macros. The four `/wd`
  codes silence MSVC-only warnings that fire on correct, intentional code.

## 5. Header portability conventions

The library stays single-source: portability is handled with guards, not
parallel files. When adding code the portable bucket will compile, follow these:

- POSIX reach is guarded, not assumed. Two "soft" headers do thread-label work
  behind a `#ifdef _WIN32` guard: `infra/log.h` (Linux uses
  `pthread_getname_np` + `syscall(SYS_gettid)`, Windows uses
  `std::this_thread::get_id`) and `concurrency/jthread_stoppable_sleep.h` (Linux
  uses `pthread_setname_np`, Windows is a no-op). The one genuinely Linux
  concurrency primitive, `owner_thread_dispatcher` (eventfd), stays in the linux
  bucket, as does `sim/sim_game.h` (epoll).
- Empty-base and member elision: use `CORVID_NO_UNIQUE_ADDRESS` (from
  `corvid/meta/crossplatform.h`), not the raw `[[no_unique_address]]`. MSVC
  silently ignores the standard attribute and needs `[[msvc::no_unique_address]]`;
  the macro picks the right spelling. The raw attribute being ignored on MSVC is
  a real layout bug, not cosmetic.
- Diagnostic pragmas: use the `PRAGMA_DIAG` / `PRAGMA_IGNORED` family from
  `crossplatform.h` to silence a known-safe warning at its single call site. Do
  not reach for a blanket `/D_CRT_SECURE_NO_WARNINGS`, which would hide future
  unsafe instances.

## 6. Windows conformance notes

clang-cl uses clang's front-end with the MSVC STL, so `std::format`, ranges, and
chrono behavior follow the MSVC STL version, not libc++. The recurring fixes,
all guarded so Linux is byte-for-byte unaffected:

- Parse-context iterators are checked class types, not `const char*`. Build a
  `basic_string_view` from the iterator pair `{ctx.begin(), ctx.end()}`, not
  from pointer plus size, and compare a `from_chars` result against `sv.data() +
  sv.size()`, not `sv.end()`. Applied in `formatting.h`, `enum_formatter.h`,
  `enum_registry.h`.
- Shifts in bit code use the value's own unsigned type, for example
  `std::underlying_type_t<E>{1} << n`, not a plain `int` `1 << n`. The latter is
  undefined once the count reaches the int width, and MSVC flags it (C4334).

MSVC cl, the conformance-mode second compiler, needed a few more, each isolated
with a minimal repro:

- The sequence-enum `operator*` (underlying-value deref) is not found by cl's
  two-phase lookup inside template bodies. A using-declaration
  `using corvid::enums::sequence::ops::operator*;` in the consuming namespace
  (see `corvid/ecs/entity_ids.h`) covers the ordinary cases. It does not reach a
  dependent enum non-type parameter used in a template-argument context, for
  example `tuple_element_t<*SID, ...>`; there, spell the underlying value as
  `std::to_underlying(SID)`.
- A deducing-this member (`this auto& self`) called with an explicit template
  parameter pack fails cl substitution only when its trailing return type names
  `decltype(self)`. Name the object-parameter template (`this Self& self`) and
  write the return type in terms of `Self`. Members that do not return
  `decltype(self)` (void, a fixed type) need no change.
- A non-type template parameter must not share a name with a data member of the
  same type: cl resolves the bare name to the member. Rename the parameter.
- An inline friend of a nested class cannot reach a sibling nested class's
  private members under cl, even where the nested class's own member functions
  can. Use a public accessor instead.

## 7. Sanitizers on Windows

clang-cl carries clang's sanitizers. `./cleanbuild.ps1 asan` builds with
`-fsanitize=address,undefined` (ASAN carries UBSAN). Standalone UBSAN is not a
separate Windows mode: LLVM ships only a static-CRT UBSAN runtime, which clashes
with the `/MD` build, and ASAN's dynamic runtime resolves the UBSAN handlers, so
the combined mode is the supported path. tsan and msan have no Windows support
here. CMake links clang-cl targets through lld-link, which ignores `-fsanitize`
on the link line, so the `if(SANITIZER AND WIN32)` block adds the asan runtime
libs explicitly.

One toolchain limitation is worth recording: any C++ rethrow of an in-flight
exception crashes under ASAN on Windows (clang-cl 22.1.7 with dynamic
VCRUNTIME140). The single library rethrow site is exercised by one test, which
is guarded out under ASAN-on-Windows.

## 8. IDE, F5, and clangd

- Editor: clangd everywhere, IntelliSense disabled. The root `.clangd` is
  generated at configure time, from `.clangd.win.in` on Windows (simpler: no
  ngtcp2 / openssl / liburing) and `.clangd.in` on Linux. Restart the clangd
  server after a reconfigure to pick up changes.
- Single-file IDE build: `scripts/ide_build.sh` (Linux) and
  `scripts/ide_build.ps1` (Windows), both emitting `debug_bin/<stem>.exe`. The
  Windows builder compiles `.cpp` with clang-cl; `.cu` is Phase 4 and currently
  errors out.
- `.vscode/tasks.json` and `launch.json` are OS-scoped: the build and run tasks
  pick the platform script, and launch.json carries both a Linux (CodeLLDB) and
  a Windows (cppvsdbg, since clang-cl emits a PDB) debug config. One committed
  `.vscode` serves a native-Windows checkout and a Remote-WSL checkout of the
  same repo.

## 9. Decisions (resolved)

- Partition by subdirectory bucket.
- Root-relative `corvid/...` includes with the repo root on the include path.
- Sequencing: portable Windows port first, CUDA on Windows second.
- Keep the `notest_` prefix. (Retiring `iou_` for a `linux/io_uring/` subdir was
  considered but deferred; the `RESOURCE_LOCK` still keys on the filename
  prefix.)
- clangd everywhere, no IntelliSense.
- Toolchain: clang-cl (default) and cl for the portable suite; nvcc + cl host
  for CUDA. cl only where nvcc forces it; native clang on Windows is wanted in
  its own right.
- The three `proto.h`-umbrella parser tests: json_parser and utf8_checker move
  to portable with narrowed includes; http_header_block stays Linux.
- Concurrency: the std-based primitives are portable; only
  `owner_thread_dispatcher` (eventfd) stays Linux.

## 10. Test classification

Counts at the time of the port: 9 cuda, 39 portable, 28 Linux. Buckets were
chosen by transitive-include analysis: a source reaching an epoll / io_uring /
socket / QUIC header is linux, a `.cu` source is cuda, and the rest are
portable. Five tests were narrowed from an umbrella include to a specific
portable header so they could move to portable (json_parser, utf8_checker,
notifiable, tombstone, timerfuse). The library-header bucketing is summarized in
section 5; the per-test move list lives in the git history for the partition
commit.

## 11. Remaining work

CUDA on Windows. Add the CMake CUDA path with `CMAKE_CUDA_HOST_COMPILER`
defaulting to cl, reuse the `CORVID_ENABLE_CUDA` loop, auto-enable when nvcc and
cl are both present, link an MSVC-ABI Catch2 for the `.cu` bucket, and teach
`ide_build.ps1` to build `.cu` with `nvcc -ccbin cl`. Build and run one `cuda/`
test under nvcc + cl to confirm native CUDA without WSL.

## 12. Shared developer context across Windows and WSL

The project `CLAUDE.md` is checked in and travels with `git pull`. The global
`~/.claude/CLAUDE.md` and the auto-memory folder do not. To share one memory
store, keep the physical store on NTFS (for example
`C:\code\Corvid\.claude-shared\memory`), junction the Windows per-project memory
directory to it (`mklink /J`, no admin needed), and symlink the WSL per-project
memory directory to the same folder via `/mnt/c/...`. The per-project memory
directory name is derived from a hash of the project path, which differs between
the WSL and `C:\` paths, so the sharing is not automatic. Edit memories from one
machine at a time.
