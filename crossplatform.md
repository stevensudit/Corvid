# Cross-Platform Plan

Status: PLAN (in progress). Once the work lands, this file is rewritten as the
record of how cross-platform capability is maintained.

Goal: restore Windows builds without losing Linux. The concrete motivation is
CUDA development on native Windows (WSL CUDA is painful), so the CUDA tests and
the portable library core must build and debug under native Windows. The
toolchain there is split: the portable suite builds with clang / clang-cl
(native clang on Windows, which also unlocks sanitizers and keeps clangd
aligned), and CUDA uses nvcc with `cl` as host (nvcc's required host compiler on
Windows). The Linux-only networking stack (epoll, io_uring, QUIC, HTTP/3) is
never expected to compile on Windows; it is simply excluded from the Windows
build, not ported.

This is not a port to a lowest-common-denominator subset. Linux keeps every
capability it has today. We partition the tree so the build system can select
what each platform compiles. Execution order: the portable Windows port (a
well-defined, self-contained side goal) goes first; CUDA on Windows, the deeper
motivation, follows.

---

## 1. Current state (why it does not build on Windows)

- `tests/` is a flat directory of about 80 sources. `tests/CMakeLists.txt`
  globs `*.cpp` (and `*.cu`) and turns each into its own executable. There is
  no way to say "this test is Linux-only" except the ad hoc filename prefixes
  `notest_` (build but do not register with CTest) and `iou_` (io_uring,
  gets a `RESOURCE_LOCK`).
- Every `.cpp` target unconditionally links the whole Linux network stack:
  `pkg_check_modules(LIBURING REQUIRED ...)`, plus OpenSSL 3.5 / ngtcp2 /
  nghttp3 built via `ExternalProject`. None of that exists or is wanted on
  Windows. (Cleaning this up also speeds the Linux build: portable tests stop
  dragging in the QUIC stack.)
- The CMake compile/link lines are clang-22 + libc++ specific: `-nostdinc++`,
  an explicit libc++ include prefix, `--rtlib=compiler-rt`, `-fuse-ld=lld`,
  `-fexperimental-library`, `-march=native`. None apply to MSVC.
- All 170 test includes use the relative form `#include "../corvid/..."`, which
  is tied to a test living exactly one directory below the repo root.
- One genuinely portable-looking header reaches into POSIX:
  `corvid/infra/log.h` includes `<sys/syscall.h>` and `<unistd.h>` (thread id
  via `gettid`, tty/`write`). This makes the whole `corvid/infra.h` umbrella
  non-portable today, and is the one contamination point to fix (Phase 2).
  (`corvid/sim/sim_game.h` also reaches POSIX, but correctly: sim_game is
  wedded to epoll, so it belongs in the linux bucket, not a thing to port.)
- The F5 / debug path is Linux-only: `.vscode/tasks.json` runs the bash script
  `scripts/ide_build.sh`, and `.vscode/launch.json` debugs with CodeLLDB.
- `cleanbuild.ps1` is stale. It predates the divergence: it uses clang (not
  MSVC), a `build/` dir (not `tests/build/`), knows nothing about the test
  layout, CUDA, sanitizers, single-file builds, or the QUIC deps. A literal
  port of today's `cleanbuild.sh` would be wrong: most of its modes (libc++ vs
  libstdc++, msan/tsan, `analyze-build` scan, llvm-cov coverage, compiler-rt,
  lld) have no MSVC analog.

---

## 2. Target architecture

Three source buckets, selected by platform at configure time:

| Bucket   | Builds on            | Depends on                          |
|----------|----------------------|-------------------------------------|
| portable | every platform       | std C++23 only, portable corvid hdrs|
| linux    | Linux only           | epoll / io_uring / sockets / QUIC   |
| cuda     | wherever nvcc + host | CUDA toolkit (+ Catch2)             |

CUDA is its own bucket, not a sub-case of linux: building the `.cu` tests on
Windows (host compiler `cl`) is the entire point.

Windows toolchain split: the portable bucket builds with clang / clang-cl
(native clang on Windows, which also enables clang's sanitizers and keeps
clangd's flags aligned); the cuda bucket builds with nvcc + `cl` host. So `cl`
is used only where nvcc forces it. This mirrors Linux, where the `.cpp` suite
uses clang and the `.cu` bucket uses nvcc + g++-15.

Library header portability (verified by transitive include analysis):

- Portable umbrellas: `containers.h`, `controllers.h`, `ecs.h`, `enums.h`,
  `lang.h`, `math.h`, `meta.h`, `strings.h`.
- `infra.h`: portable after the `infra/log.h` guard (Phase 2); the rest of infra
  (clocks, exception_firewalls, scope_exit) is already OS-clean. infra is a
  cross-platform target.
- Linux umbrellas: `filesys.h`, `proto.h`. Also Linux: `sim/sim_game.h` (epoll)
  and anything that includes it.
- `concurrency/*` is mostly portable: the std-based primitives (`sync_lock`,
  `notifiable`, `tombstone`, `timers`, `timeouts`, `timing_wheel`, `timer_fuse`,
  `idle_timeout`, `timeout_sweeper`, `jthread_stoppable_sleep`) are portable.
  Three of those need a soft guard (`log.h` reach, or the cosmetic
  `pthread_setname` in `jthread_stoppable_sleep`). The one genuinely Linux
  member is `owner_thread_dispatcher` (eventfd). The `concurrency.h` umbrella
  includes it, so the umbrella as-written is Linux; portable code includes the
  specific portable headers instead. `containers.h` reaching
  `concurrency/sync_lock.h` via `containers/utils/intern.h` is harmless
  (sync_lock is OS-clean), so the containers umbrella stays portable.
- Portable subtree despite living under `proto/`: the `proto/misc/*` parsers
  (json_parser, base-64, sha-1, utf8-checker, terminated_text_parser,
  http_head_codec) are OS-clean (json_parser after the log.h fix). A test that
  wants one must include it directly, not via the `proto.h` umbrella.
- `cuda/*.cuh`: portable (CUDA runtime is cross-platform).

### Proposed test tree

```
tests/
  catch2_main.h            # shared, stays at tests root
  CMakeLists.txt
  comprehensive.cmake
  portable/                # built on every platform
  linux/                   # built only when CMAKE_SYSTEM_NAME == Linux
  cuda/                    # built only when CORVID_ENABLE_CUDA (Linux or Windows)
```

Notes on the prefix conventions:

- `notest_` stays. It encodes "build an executable but do not register it as a
  CTest" (servers, demos, tutorials), which is orthogonal to platform. A
  `notest_` source can live in any bucket.
- `iou_` can be retired in favor of the `linux/io_uring/` path: the
  `RESOURCE_LOCK` that serializes io_uring memlock pressure keys off the
  subdirectory instead of the filename. (Decision D below.)

### Include style

Switch test sources from `../corvid/...` to root-relative `corvid/...` and add
the repo root to the test include path (CMake `target_include_directories` and
both `ide_build` scripts). After this, a test's includes no longer depend on
how deep it sits, so moving files between buckets is free. The conversion is a
mechanical sweep (`../corvid/` -> `corvid/`) verifiable by grepping for any
remaining `../corvid`.

---

## 3. Phased plan

Phases 1 and 2 happen on Linux, change no behavior, and are fully verifiable on
the current machine. They are the prerequisite the question "what do we change
first under Linux" asks for. Phases 3 to 5 require a Windows machine.

### Phase 1 - Partition the tree (Linux, no behavior change) [DONE]

1. Classify every test into portable / linux / cuda by transitive-include
   analysis (not by guesswork): for each source, resolve which `corvid/` headers
   it pulls in and flag any that reach an OS header. Anything that reaches
   epoll/io_uring/socket/QUIC headers is `linux/`; `.cu` is `cuda/`; the rest is
   `portable/`. See the proposed mapping in section 6 (to be confirmed by this
   analysis, especially the borderline concurrency-timer and quic/http3-header
   tests).
2. Create `tests/portable|linux|cuda/`, `git mv` sources in, sweep includes to
   root-relative, add repo root to the include path.
3. Refactor `tests/CMakeLists.txt`:
   - Per-bucket globs instead of one `*.cpp` glob.
   - Move the liburing / OpenSSL / ngtcp2 / nghttp3 discovery and the
     `ExternalProject` blocks inside a `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")`
     guard; link the QUIC stack only into `linux/` targets.
   - Preserve the io_uring `RESOURCE_LOCK` (kept keyed on the
     `iou_`/`quic_dgram_` filename prefix; the `linux/io_uring/` subdir of
     decision D is deferred to a follow-up to keep this pass
     behavior-preserving), the `notest_` opt-out, and the CUDA loop unchanged.
4. Update the two path-aware helpers: `tests/comprehensive.cmake` and
   `scripts/check_layering.sh` (and any doc that names a moved test).

Verify: `./cleanbuild.sh` is all-green; `./cleanbuild.sh all` (the sanitizer
sweep) is still green; single-file (`./cleanbuild.sh strings_test.cpp`) still
works; CUDA targets still build and run. Same test outcomes, only reorganized.

### Phase 2 - Portable-core header hygiene (Linux, compiles both ways) [DONE]

1. Guard the two cosmetic "soft" headers for Windows. Both do only thread-label
   work: `infra/log.h` uses `pthread_getname_np` + `syscall(SYS_gettid)`;
   `concurrency/jthread_stoppable_sleep.h` uses `pthread_setname_np`. Behind
   `#ifdef _WIN32`, swap in `GetCurrentThreadId` / `SetThreadDescription` (or
   drop the name). Linux path unchanged. These are the only library headers the
   portable bucket needs ported; `owner_thread_dispatcher` (eventfd) and
   sim_game stay Linux.
2. Narrow five tests that pull a whole umbrella but only exercise a portable
   component, so they move to the portable bucket:
   - json_parser:  `proto.h`       -> `proto/misc/json_parser.h`
   - utf8_checker: `proto.h`       -> `proto/misc/utf8-checker.h`
   - notifiable:   `concurrency.h` -> `concurrency/notifiable.h`
   - tombstone:    `concurrency.h` -> `concurrency/tombstone.h`
   - timerfuse:    `concurrency.h` -> `concurrency/timer_fuse.h`

   (http_header_block keeps the `proto.h` umbrella and stays Linux by choice;
   owner_thread_dispatcher stays Linux.)
3. Add a tiny compile-only smoke TU that includes every portable umbrella and
   asserts (by building cleanly) that the portable core needs no OS headers.

Verify on Linux: builds unchanged; the guarded `#else` branches at least parse.
Real proof comes in Phase 3 under clang-cl (and Phase 4 under `cl` for CUDA).

### Phase 3 - Windows portable bring-up with clang (the lead side goal)

The portable-suite port goes first: it is well-defined and self-contained, and
building it with native clang (clang-cl) on Windows also unlocks clang's
sanitizers and keeps clangd's flags aligned. CUDA (which forces the `cl` host)
follows in Phase 4. Command-line build first; the IDE / F5 wiring is Phase 5.

1. CMake non-Linux path: configure with clang-cl, `-std=c++23` (or
   `/std:c++latest`). The `linux` bucket and its liburing / OpenSSL / ngtcp2 /
   nghttp3 ExternalProjects stay behind the `if(CMAKE_SYSTEM_NAME ...)` guard, so
   Windows configures none of them. Obtain Catch2 from vcpkg or FetchContent
   built with the same compiler.
2. cleanbuild.ps1 rewrite (the Windows build driver): clean `tests/build`,
   configure (clang-cl, portable bucket; cuda added in Phase 4), build, run
   `ctest`, single-file filter (`-DTEST_NAME=`). Add `asan` / `ubsan` modes
   (clang's `-fsanitize=` works on Windows). Deliberately drop the
   libc++/libstdc++ choice, msan, tsan, `scan` (analyze-build), llvm-cov
   coverage, and compiler-rt/lld swaps - record the asymmetry as a decision, not
   an omission. A literal port of `cleanbuild.sh` would be wrong.
3. Portable smoke: build one portable test (for example `strings_test`) under
   clang-cl. Go/no-go for the suite - surfaces any C++23 conformance or MSVC-STL
   gaps in the heavy template / `std::format` / ranges code.
4. Iterate the portable suite green, fixing gaps in headers behind guards so the
   Linux build stays untouched. Run the sanitizer modes to validate.

### Phase 4 - CUDA on Windows (the deeper motivation)

clang is not used for CUDA on Linux today and is not expected to work for it on
Windows either, so CUDA uses nvcc with `cl` as host. This mirrors the Linux
split, where the `.cu` bucket already uses a different toolchain (nvcc + g++-15)
than the `.cpp` suite.

1. CMake CUDA path: `CMAKE_CUDA_HOST_COMPILER` defaults to `cl`; reuse the
   existing `CORVID_ENABLE_CUDA` loop. Auto-enable when nvcc + `cl` are present,
   mirroring the Linux auto-detect.
2. Catch2 for `.cu`: the cuda bucket links an MSVC-ABI Catch2. If the portable
   suite used clang-cl (MSVC ABI), one Catch2 can serve both; confirm ABI
   compatibility, else build a `cl` Catch2 for the cuda bucket.
3. Build and run a `cuda/` test under nvcc + `cl` - native CUDA without WSL.
4. Extend cleanbuild.ps1 to auto-detect and build the cuda bucket, as
   cleanbuild.sh does on Linux.

### Phase 5 - Windows IDE, F5, and clangd

This is the area with the least certainty: running both dev environments has not
been proven smooth before (see the dual-environment note in Risks). De-risk it
early with a trivial case before wiring the full suite.

1. clangd everywhere (decided - no IntelliSense): generate the Windows `.clangd`
   from the Windows `compile_commands.json`. On Windows the portable build pulls
   far fewer deps (no ngtcp2 / openssl / liburing), so its config is simpler than
   Linux. Keep IntelliSense disabled as today.
2. `tasks.json`: OS-scoped `build` task - `linux.command` keeps
   `scripts/ide_build.sh`, `windows.command` calls a new `scripts/ide_build.ps1`.
   Same for the run task. The single committed file works whether the folder is
   opened natively on Windows or via Remote-WSL on Linux.
3. `scripts/ide_build.ps1`: the Windows single-file builder. `.cpp` -> clang-cl;
   `.cu` -> nvcc with `-ccbin cl`; output `debug_bin\<stem>.exe`. Mirror the
   Catch2 link and the root include path.
4. `launch.json`: add a Windows debug configuration pointing at
   `${fileDirname}/debug_bin/${fileBasenameNoExtension}.exe`, alongside the
   existing Linux CodeLLDB config. Pick the debugger that matches the producing
   compiler (CodeLLDB / lldb for clang-built portable binaries; `cppvsdbg` for
   `cl`-linked CUDA binaries).
5. Prove the dual-env setup before widening: open the Windows checkout natively
   and the WSL checkout via Remote-WSL, and confirm F5 builds-and-debugs one
   portable test on each from the same committed `.vscode`. Only then wire the
   rest.

---

## 4. Risks and unknowns

- C++23 conformance on Windows is the biggest unknown. clang-cl gives clang's
  language front-end but still uses Microsoft's STL headers, so `std::format`,
  ranges, and chrono coverage depend on the MSVC STL version, not libc++. Phase
  3 step 3 gates the portable port on a single smoke build before further work.
- Dual-environment VSCode (native Windows plus Remote-WSL on one set of
  committed `.vscode` files) has not been proven smooth here. Phase 5 step 5
  de-risks it with a trivial case first. OS-scoped task overrides and
  per-platform launch configs are the mechanism; the two checkouts (the Windows
  `C:\` clone and the WSL clone) share the committed `.vscode` via git.
- The Linux network stack (epoll, io_uring, QUIC) is out of scope for Windows.
  No IOCP port is planned. (Future note, not this effort.)
- clangd everywhere: the generated `.clangd` bakes in absolute, per-checkout dep
  paths, so each platform regenerates its own from its `compile_commands.json`.
  The Windows config is simpler (no ngtcp2 / openssl / liburing).
- File moves touch 170 includes plus any doc/script that names a test path.
  Mechanical, but grep-verify nothing else hardcodes the old flat paths.

---

## 5. Decisions (resolved)

- A. Partition mechanism: subdirectories. Approved.
- B. Include style: root-relative `corvid/...` with the repo root on the include
  path. Approved.
- C. Sequencing: the portable Windows port goes first (well-defined side goal),
  then CUDA on Windows (the deeper motivation). Changed from the initial
  CUDA-first proposal.
- D. Prefixes: keep `notest_`; retire `iou_` in favor of the `linux/io_uring/`
  subdir for the `RESOURCE_LOCK`. Approved.
- E. Editor: clangd everywhere, no IntelliSense. Decided.
- F. Toolchain: Windows uses clang / clang-cl for the portable suite (which also
  enables clang's sanitizers) and nvcc + `cl` host for CUDA. `cl` only where
  nvcc forces it. Native clang on Windows is a wanted capability in its own
  right, not merely a fallback.
- G. The 3 `proto.h`-umbrella parser tests: json_parser -> portable (narrow
  include); utf8_checker -> portable (narrow; low value but cheap, since UTF-8
  checking is only used by the Linux-only WebSocket code); http_header_block ->
  stays Linux (Linux-only use, no general-purpose value). Decided.
- H. Concurrency tests: the std-based primitives are portable (narrow off the
  `concurrency.h` umbrella where needed); only `owner_thread_dispatcher`
  (eventfd, not built on std) stays Linux. Decided.

---

## 6. Test classification (verified by transitive include analysis)

76 test sources: 9 cuda, 39 portable, 28 Linux. Generated by walking each test's
transitive corvid includes plus its own direct system includes, flagging any
that reach an OS / external-dep system header. Two cosmetic "soft" headers,
`infra/log.h` and `concurrency/jthread_stoppable_sleep.h`, do only thread-label
work (`pthread_get/setname`, `syscall(SYS_gettid)`) and get a Windows guard in
Phase 2.

CUDA (9): cuda_device, cuda_saxpy, cuda_status, notest_cuda_matmul,
notest_cuda_matmul_tiled, notest_cuda_sapxy_parallel, notest_cuda_tutor_1/2/3.

Portable now, no change (19): charconv_wrapper, controllers, cstring_view,
enum_formatter, fixed_bitset, fixed_string, formatting, id_container,
infra_clocks, infra_scope_exit, lang, math, meta, opt_string_view,
string_view_wrapper, strings, tagged_string_view, targeting, timers.

Portable after the soft guards (15): bitmask_enum, sequence_enum, containers,
circular_buffer, find_opt, intern_table, interval, optional_ptr, ecs,
entity_registry, object_pool, infra_log, idle_timeout, timeout_sweeper (these
reach only `log.h`), plus timing_wheel (reaches only
`jthread_stoppable_sleep`).

Portable after narrowing an over-broad include (5): each includes a whole
umbrella but only exercises a portable component -
  - json_parser:  `proto.h`       -> `proto/misc/json_parser.h` (after log.h)
  - utf8_checker: `proto.h`       -> `proto/misc/utf8-checker.h` (clean)
  - notifiable:   `concurrency.h` -> `concurrency/notifiable.h` (clean)
  - tombstone:    `concurrency.h` -> `concurrency/tombstone.h` (clean)
  - timerfuse:    `concurrency.h` -> `concurrency/timer_fuse.h` (after soft guard)

Linux (28): epoll_tests, netsocket_tests, osfile_tests, proto_test,
websocket_tests, iov_queue, iou_buf_pool, iou_dgram_router, iou_loop,
iou_provided_buf_pool, iou_stream_conn, quic_conn, quic_dgram_echo,
quic_dgram_http3, quic_dgram_router, quic_header, quic_smoke, http3_conn,
http3_header, http3_server_stream, http3_stream, http_server_tests,
nghttp3_smoke, notest_corvid_sim, notest_http3_client, sim_world,
http_header_block (Linux-only use, kept Linux by choice), and
owner_thread_dispatcher (eventfd, the one concurrency primitive not built on
std).

---

## 7. Developer environment and agent onboarding

Independent of the build phases, and a priority in its own right: the Windows
checkout needs a working Claude Code instance with the same context this one
has. This can proceed early and in parallel.

What travels for free vs. what does not:

- The project `CLAUDE.md` is checked in, so `git pull` brings it to the Windows
  checkout automatically. No action needed.
- The global `~/.claude/CLAUDE.md` (user-level instructions) and the auto-memory
  folder are NOT in the repo and do not travel. These are what the fresh Windows
  instance lacks.

Plan:

1. Authenticate Claude Code on Windows (a separate login from the WSL instance).
2. Share one memory store across both platforms. Caveat: Claude Code derives the
   per-project memory directory from a hash of the project path, which differs
   per platform (the WSL path vs. the `C:\` path), so the two instances look in
   different directory names by default. Sharing therefore requires pointing
   both at one physical folder explicitly, via a symlink/junction on each side -
   it is not automatic.
3. Recommended layout (matches the "canonical on the Windows partition" idea):
   keep the single physical store on NTFS, for example
   `C:\code\Corvid\.claude-shared\memory`. On Windows, junction the per-project
   memory dir to it (`mklink /J`, no admin needed). On WSL, symlink the
   per-project memory dir to the same folder via `/mnt/c/...`. WSL reads/writes
   small text files there fine. Do the same (junction/symlink, or just copy) for
   the global `CLAUDE.md`.
4. Concurrency: edit memories from one machine at a time to avoid write races
   (you are at one keyboard anyway).

The Linux-side symlink can be set up from this checkout once the canonical path
is chosen; the Windows-side junction and the auth step happen on the Windows
box. The MEMORY.md index and the individual memory files all live under the
shared folder, so both instances see the same recall set.
