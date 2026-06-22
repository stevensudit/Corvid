# Cross-Platform Build

Status: Windows port complete, including CUDA, device tooling, and lint, and the
Windows-only CUDA cell (CUDA plus Direct3D 11 interop) is now built. The portable
suite builds and passes on Linux (clang), native Windows (clang++, the default),
and native Windows (MSVC cl): 40 of 40 portable tests per Windows compiler. The
cross-platform CUDA bucket builds and runs natively on Windows under clang++ (4 of
4 registered `.cu` pass on the GPU, plus the cuBLAS tutorials); device correctness
runs via `./cleanbuild.ps1 cudacheck` (compute-sanitizer) and device debugging via
Nsight Visual Studio Edition, and `./cleanbuild.ps1 tidy` is clean. The full Linux
suite (portable plus linux buckets, plus CUDA under nvcc) stays green. Two
Windows-only buckets now hold the new GPU work: `tests/windows/` (plain-C++ tests
for the SDL3 and D3D11 wrappers) and `tests/cuda/windows/` (the CUDA-D3D11 interop
test plus the fractal, raymarch, and voxel viewers). These grow a new graphics
substrate under `corvid/sdl/` and `corvid/cuda/windows/`, plus a Dear ImGui live
tuning panel; see section 11. This document records how cross-platform capability
is structured and maintained; update it when the structure changes.

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

Five buckets, selected by platform at configure time:

| Bucket       | Builds on         | Depends on                                 |
|--------------|-------------------|--------------------------------------------|
| portable     | every platform    | std C++23 and portable corvid headers      |
| linux        | Linux only        | epoll, io_uring, sockets, QUIC             |
| cuda         | Linux and Windows | CUDA toolkit (plus Catch2)                 |
| windows      | Windows only      | SDL3 (Windows SDK)                         |
| cuda/windows | Windows only      | CUDA toolkit + D3D11/DXGI + SDL3 (+ ImGui) |

Tests live under `tests/portable/`, `tests/linux/`, `tests/cuda/`,
`tests/windows/`, and `tests/cuda/windows/`.
`tests/CMakeLists.txt` globs each bucket separately. The linux bucket and its
liburing / OpenSSL / ngtcp2 / nghttp3 dependencies sit behind
`if(CMAKE_SYSTEM_NAME STREQUAL "Linux")`, so Windows configures none of them.
This also speeds the Linux build: portable tests no longer drag in the QUIC
stack. CUDA is its own bucket, not a sub-case of linux, because building the
`.cu` tests on Windows is the whole point.

These are the populated cells of a two-axis space: platform (portable /
Linux-only / Windows-only) crossed with whether a target needs the CUDA toolkit.
The cross-platform `cuda` bucket stays platform-neutral (it builds on Linux and
Windows alike); the two Windows-only cells are the newest. `windows` holds
plain-C++ tests (the SDL3 and D3D11 wrappers) that need no CUDA toolchain, and
`cuda/windows` holds CUDA targets whose Windows graphics dependencies (Direct3D
interop via `cudaGraphicsD3D11*`, the Windows SDK's d3d11/dxgi) do not exist on
Linux. Section 11 covers them.

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

| Platform | Portable suite          | CUDA bucket    |
|----------|-------------------------|----------------|
| Linux    | clang + libc++          | nvcc + g++-15  |
| Windows  | clang++ (default), cl   | clang++        |

On Windows the portable suite builds with clang++ by default: the same GNU-style
LLVM driver used on Linux, but targeting the MSVC ABI against the MSVC STL (no
libc++). It keeps clangd's flags aligned and carries clang's sanitizers. MSVC cl
is supported as a second compiler, mirroring the clang/gcc choice on Linux; cl
is genuinely different from clang and catches real conformance divergences (see
section 6).

The CUDA bucket on Windows also builds with clang++ (clang's CUDA frontend),
*not* nvcc. This is forced, not a preference: nvcc 13.3's MSVC device frontend
(`cudafe++`/`cicc`) has no C++23 dialect. Its highest `--ms_c++NN` is
`--ms_c++20`; asked for `-std=c++23` with an MSVC host it prints "not supported
with the configured host compiler" and silently falls back to `--ms_c++14`
(confirmed via `nvcc --dryrun`). Corvid is a C++23 library (it uses
`std::is_scoped_enum_v`, `std::forward_like`, `std::range_format`, deducing
this), so the `.cu` tests cannot compile under nvcc on Windows. clang's unified
C++23 frontend compiles host and device together and has no such gap. This is a
host-specific nvcc limitation: on Linux, nvcc + g++-15 builds the same `.cu` at
C++23 fine (the EDG GNU dialect does support C++23), so Linux keeps nvcc.

Two consequences of the clang++/CUDA choice, both enforced by `cleanbuild.ps1`:

- The whole Windows build unifies on one clang driver. CMake selects the
  Windows-MSVC platform from a clang-cl C++ compiler and then forces MSVC
  conventions (`/machine`, `/Fo`, `MSVC_RUNTIME_LIBRARY`) onto the GNU-driver
  clang++ CUDA compiler, which rejects them. Using clang++ for the `.cpp` suite
  too makes the platform uniformly GNU-like and those conflicts vanish. This is
  why clang-cl was retired in favor of clang++.
- CUDA rides only with the default clang++ build, never with cl: CMake forbids
  mixing a cl (MSVC-frontend) host with a clang++ (GNU-frontend) CUDA compiler
  ("mixed frontend variants ... not supported"). `./cleanbuild.ps1 cl` therefore
  builds the portable suite only; CUDA development happens under the default
  clang++.

clang++ defaults to the static (`/MT`) CRT; the Windows build forces the dynamic
(`/MD`) one with `-fms-runtime-lib=dll` on both the `.cpp` and `.cu` flags, so
the suite, the FetchContent Catch2, and the CUDA objects share one CRT and ASAN
keeps its dynamic runtime. `CMAKE_CUDA_ARCHITECTURES` is passed explicitly
(`cleanbuild.ps1` resolves the GPU's compute capability from `nvidia-smi`, e.g.
8.9 -> sm_89): clang-CUDA's `native` arch detection does not work on Windows and
silently produces a binary whose kernels never run.

Versions used during bring-up: clang++ and clang-format from LLVM 22.1.7; MSVC
STL 14.51 and Windows SDK via VS 2026 (MSVC `_MSC_VER` 1951); CMake 4.3, Ninja,
nvcc 13.3 (toolkit headers/libs and `ptxas`; the compiler driver is clang++).

## 4. Building

Linux (`./cleanbuild.sh`) is unchanged. The full menu (libc++/libstdc++,
asan/tsan/ubsan/msan, scan, coverage, single-file) is documented in the
build-and-test skill.

Windows (`./cleanbuild.ps1`) does a fresh Release configure, build, and ctest:

- `./cleanbuild.ps1`: clang++, portable suite plus CUDA (when the toolkit and a
  GPU are present).
- `./cleanbuild.ps1 <name>_test.cpp` or `<name>_test.cu`: one test (via
  `-DTEST_NAME=`). A `.cu` needs the default clang++ in a plain mode.
- `./cleanbuild.ps1 cl`: portable suite via MSVC cl (no CUDA; see section 3).
- `./cleanbuild.ps1 asan`: ASAN (which carries UBSAN), clang++ only.
- `./cleanbuild.ps1 tidy`: run clang-tidy (`USE_CLANG_TIDY=ON`) during the build,
  then a grouped warning summary, mirroring the Linux tidy run. clang++ only
  (clang-tidy is the clang analyzer; cl has no analog). It keeps asserts live so
  assert facts reach clang-analyzer: the build type stays Release (an empty one
  makes the GNU-clang platform pull the debug CRT, which then will not link the
  /MD Catch2), but the default `-DNDEBUG` is dropped via a `CMAKE_CXX_FLAGS_RELEASE`
  override. LLVM is not on PATH, so the clang-tidy path is passed explicitly.

The script enters a VS Developer shell for INCLUDE/LIB (skipped if already inside
one), configures Ninja with the chosen compiler, builds with `-k 0`, and runs
ctest. CUDA auto-enables when `nvcc` is on PATH and the mode is the default
clang++ plain one: it passes `CORVID_ENABLE_CUDA=ON`, `CMAKE_CUDA_COMPILER`
pointing at `clang++`, and an explicit `CMAKE_CUDA_ARCHITECTURES` from
`nvidia-smi`. It uses `cmake --fresh` rather than wiping `tests/build`, because
clangd holds an open handle on `compile_commands.json` there. The other Linux
cleanbuild modes (libc++/libstdc++ choice, msan/tsan, analyze-build scan,
llvm-cov coverage, compiler-rt/lld swaps) have no MSVC analog and are absent.

Compiler flags are set in the `WIN32` branch of `tests/CMakeLists.txt`:

- clang++: `-Wall -Wextra -Werror -fms-runtime-lib=dll`, plus the plain
  `-std=c++23` CMake emits from `CMAKE_CXX_STANDARD=23` (which clangd parses
  correctly, unlike the clang-cl `/std` form the retired path had to work
  around). `-fms-runtime-lib=dll` selects the `/MD` CRT (see section 3).
- cl: `/std:c++latest` (cl has no `/std:c++23`), then `/EHsc /permissive-
  /Zc:preprocessor /W4 /WX /wd4245 /wd4267 /wd4305 /wd4310`. `/permissive-`
  turns on conformant two-phase name lookup; `/Zc:preprocessor` selects the
  conformant preprocessor, without which cl corrupts raw string literals passed
  through Catch2 macros. The four `/wd` codes silence MSVC-only warnings that
  fire on correct, intentional code.

The CUDA flags live in the `CORVID_ENABLE_CUDA` block. On Windows the warning
set matches the `.cpp` suite: `-Wall -Wextra -Werror`, with
`-Wno-missing-designated-field-initializers` dropping the one `-Wextra` check
that fires on the deliberate partial designated-init idiom for C DESC structs
(the positional `-Wmissing-field-initializers` stays on, like the cl `/wd`
codes). Plus `-std=c++23 -fms-runtime-lib=dll -Wno-unknown-cuda-version` (the
last silences the note that CUDA 13.3 is newer than clang 22's last fully
supported toolkit) and `-gline-tables-only` (section 8). On Linux the nvcc form
is `-std=c++23 -O3 -lineinfo`; raising its host warnings to `-Wextra` (via nvcc
`-Xcompiler`) is a separate follow-up.

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

clang++ on Windows uses clang's front-end with the MSVC STL, so `std::format`,
ranges, and chrono behavior follow the MSVC STL version, not libc++. (These
notes predate the clang-cl -> clang++ switch; they are properties of the MSVC
STL and apply unchanged to clang++.) The recurring fixes, all guarded so Linux
is byte-for-byte unaffected:

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

clang-tidy findings differ by standard library too, so `./cleanbuild.ps1 tidy`
(MSVC STL) surfaces two checks that the Linux libc++ run does not. Both are
suppressed with targeted NOLINT comments at the sites (not a global exclusion),
so the run is clean on both:

- `bugprone-std-namespace-modification` flags the library's legal partial
  `std::formatter` / `std::format_kind` specializations. The check exempts full
  specializations but not partial ones; libc++ hides `std` in the inline
  namespace `std::__1`, which the check ignores, so only the MSVC STL and
  libstdc++ flag them. (Confirmed by an A/B with clang-tidy 22.1.8 on Ubuntu:
  flagged under libstdc++, silent under libc++.)
- `bugprone-exception-escape` flags the intentional `noexcept` on
  `log::terminate` and `entity_registry::clear` (the MSVC STL's `scoped_lock` /
  `shrink_to_fit` analyze as throwing where both Linux stdlibs do not). These
  are noexcept-by-policy, so the escape is intended.

## 7. Sanitizers on Windows

clang++ carries clang's sanitizers. `./cleanbuild.ps1 asan` builds with
`-fsanitize=address,undefined` (ASAN carries UBSAN). Standalone UBSAN is not a
separate Windows mode: LLVM ships only a static-CRT UBSAN runtime, which clashes
with the `/MD` build, and ASAN's dynamic runtime resolves the UBSAN handlers, so
the combined mode is the supported path. tsan and msan have no Windows support
here. Because clang++ drives the link, the `-fsanitize=address,undefined` on the
link line is honored and clang++ pulls in the dynamic asan runtime + thunk
itself, so the `if(SANITIZER AND WIN32)` block now only fails-fast on unsupported
configurations (cl, or a non-asan sanitizer); the retired clang-cl path had to
add the `clang_rt` libs and `/DEBUG`/`/OPT:NOICF` by hand, because CMake linked
clang-cl through lld-link directly, bypassing the driver.

One toolchain limitation is worth recording: any C++ rethrow of an in-flight
exception crashes under ASAN on Windows (clang 22.1.7 with dynamic
VCRUNTIME140). The single library rethrow site is exercised by one test, which
is guarded out under ASAN-on-Windows (via `__has_feature(address_sanitizer)`).

ASAN instruments host code; the device-side analog is `./cleanbuild.ps1
cudacheck`. It builds the cuda bucket and runs each registered `.cu` test under
NVIDIA `compute-sanitizer` across its four tools (memcheck, racecheck, synccheck,
initcheck) with `--error-exitcode 1`. The CUDA build's `-gline-tables-only` (the
clang analog of nvcc `-lineinfo`) maps a fault to its `.cu` source line. A test
that launches no kernel and touches no device memory (host-only, or device
functions exercised on the host) makes no instrumentable CUDA call;
compute-sanitizer reports that and cudacheck records it as a skip, not a fault.

## 8. IDE, F5, and clangd

- Editor: clangd everywhere, IntelliSense disabled. The root `.clangd` is
  generated at configure time, from `.clangd.win.in` on Windows (simpler: no
  ngtcp2 / openssl / liburing) and `.clangd.in` on Linux. Restart the clangd
  server after a reconfigure to pick up changes.
- Single-file IDE build: `scripts/ide_build.sh` (Linux) and
  `scripts/ide_build.ps1` (Windows) are thin wrappers over a dedicated debug
  CMake tree at `tests/build-debug` (its own `tests/.fetchcontent-debug` Catch2
  cache, configured `-DIDE_DEBUG=ON`); each builds one `--target` to
  `tests/build-debug/debug_bin/<stem>.exe`. CMake stays the single source of
  truth for include paths and link libraries, so the IDE build and `cleanbuild`
  cannot diverge on dependencies (the scripts name no flags or `-l` libs of
  their own). The `IDE_DEBUG` knob (in `tests/CMakeLists.txt`) switches the
  baked-in optimization to `-O0 -g` (a PDB on Windows, where the `/MD` CRT stays
  put so the debug exe still links the Catch2 in this tree), keeps asserts live,
  forces a `.exe` suffix so one path template serves both platforms, and routes
  output to `debug_bin/`. The first build pays a one-time configure plus Catch2
  build; later builds reuse the configured tree and relink just the named
  target. A `.cu` builds the same way (clang++ on Windows, nvcc/g++-15 on Linux)
  and also produces a PDB.
- The `.cu` clangd block in `.clangd.win.in` puts clangd into `-xcuda` mode with
  the toolkit path (substituted from nvcc at configure time), so `.cu`/`.cuh`
  parse without flagging `__global__` or `<<<...>>>`.
- `.vscode/tasks.json` and `launch.json` are OS-scoped: the build task picks the
  platform script, and launch.json carries both a Linux (CodeLLDB) and a Windows
  (cppvsdbg, since clang++ emits a PDB) debug config. The run task and both debug
  configs point at the fixed `tests/build-debug/debug_bin/<stem>.exe` the scripts
  produce (one cross-platform template, hence the forced `.exe` suffix). One
  committed `.vscode` serves a native-Windows checkout and a Remote-WSL checkout
  of the same repo.
- Device-side `.cu` debugging on Windows is NVIDIA Nsight Visual Studio Edition
  (full Visual Studio), not VSCode: Nsight VSCode Edition drives `cuda-gdb`,
  which is Linux/WSL-only. clang++ `-g -O0` emits the same NVIDIA device-debug
  sections (`.nv_debug_line_sass`, `.nv_debug_info_reg_sass`, DWARF) that nvcc
  `-G` does (verified with `cuobjdump --dump-elf`), so the
  `tests/build-debug/debug_bin/<stem>.exe` that `ide_build.ps1` produces for a
  `.cu` is device-debuggable: open it
  in Visual Studio and use Nsight's "Start CUDA Debugging". Host-side `.cu`
  debugging stays on cppvsdbg via the PDB, as for `.cpp`.

## 9. Decisions (resolved)

- Partition by subdirectory bucket.
- Root-relative `corvid/...` includes with the repo root on the include path.
- Sequencing: portable Windows port first, CUDA on Windows second.
- Keep the `notest_` prefix. (Retiring `iou_` for a `linux/io_uring/` subdir was
  considered but deferred; the `RESOURCE_LOCK` still keys on the filename
  prefix.)
- clangd everywhere, no IntelliSense.
- Toolchain: clang++ (default) and cl for the portable suite; clang++ for CUDA.
  clang-cl was the original default but was retired: nvcc 13.3 cannot build the
  C++23 `.cu` bucket with an MSVC host, so CUDA uses clang's CUDA frontend, and
  CMake will not mix a clang-cl (MSVC-frontend) host with a clang++ (GNU-frontend)
  CUDA compiler. Unifying the `.cpp` suite on clang++ too makes the platform
  uniformly GNU-like (see section 3). cl stays as the genuinely-different MSVC
  conformance second compiler.
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

## 11. Windows-only CUDA cell

The newest bucket cell is Windows-only CUDA targets. These are `.cu` executables
that depend on Windows-specific GPU-adjacent APIs (Direct3D interop via
`cudaGraphicsD3D11*` plus the Windows SDK's d3d11/dxgi, or the NVIDIA Video Codec
SDK's NVENC/NVDEC), so they cannot build on Linux. They do not fit the existing
`cuda` bucket, which builds on both platforms and assumes no platform-specific
dependencies. The D3D11-interop side is now built; NVENC/NVDEC remain future.

Shape of the cell (all of the open items below are resolved):

- Sources live in a Windows-only CUDA sub-area, globbed only when `WIN32` and the
  CUDA toolkit are both present, the way the linux bucket gates its Linux-only
  `.cpp`. Leading candidate `tests/cuda/windows/`, keeping them under the CUDA
  bucket (reusing the clang++ `.cu` toolchain and Catch2 wiring) with Windows as a
  sub-condition; the alternative is a `tests/windows/` platform bucket symmetric
  to `tests/linux/`.
- The library headers for these features are genuinely Windows-only (they pull in
  `<d3d11.h>` / `nvEncodeAPI.h`), so they belong in a Windows-only location (for
  example `corvid/cuda/windows/`) rather than `#ifdef _WIN32` guards threaded
  through otherwise-portable headers, following the precedent of the
  genuinely-Linux `owner_thread_dispatcher` staying in the linux bucket.
- Each target links the Windows libs it needs. Direct3D interop needs only the
  Windows SDK (d3d11.lib, dxgi.lib), already present; NVENC/NVDEC needs the NVIDIA
  Video Codec SDK, a separate download and so a new external dependency to wire up
  (analogous to the Linux ngtcp2 / openssl prebuilds).

Decided (first target, 2026-06-18). The three open items are resolved: CUDA
graphics targets live in `tests/cuda/windows/` (globbed only on `WIN32`, under
the CUDA bucket), while Windows-only plain-C++ tests (such as the SDL wrapper
tests) live in a separate `tests/windows/` bucket that needs no CUDA toolchain,
so both of the section 11 candidate subdirs exist, split by whether CUDA is involved;
the genuinely Windows-only library headers (D3D11, CUDA-D3D interop) live in
`corvid/cuda/windows/`; and the first target needs only D3D interop (Windows
SDK), not the Video Codec SDK. That first
target is a CUDA-driven fractal viewer: a kernel writes a texture that Direct3D
presents via `cudaGraphicsD3D11*` interop, so the frame never crosses PCIe.
D3D11, not D3D12, because the present is a single full-screen texture and is
never the bottleneck; D3D11's map/unmap gives zero-copy interop without D3D12's
external-memory and fence ceremony. The app owns its own `ID3D11Device` plus a
flip-model swapchain (uncapped present); SDL3 supplies only the window, input,
and event loop, with the `HWND` pulled from it.

SDL3 is a new external dependency, brought in as a prebuilt drop in
`tests/.local/sdl3/` (the official VC binaries, fetched by
`scripts/fetch_sdl3.ps1`), checked at configure time with a `FATAL_ERROR` hint
like the Linux OpenSSL prebuild. Prebuilt rather than FetchContent-from-source
because SDL exposes a C API: there is no C++ ABI to match against the stdlib
(the reason Catch2 must be source-built), so an MSVC-built SDL links cleanly from
clang++, and a C DLL's CRT is independent of the app's, so one `SDL3.dll` serves
both the Release and debug build paths. The shared DLL is staged next to each
windows-cell executable. The wrappers this viewer brings into existence are the
reusable substrate for later, larger GPU work, and they split by portability:
the SDL3 window/input/event wrappers live in `corvid/sdl/` (SDL is
cross-platform, a portable peer to `corvid/cuda/`, not part of the Windows-only
cell), while the genuinely Windows-only D3D11 and CUDA-D3D interop wrappers live
in `corvid/cuda/windows/`.

The cell has since grown well past that first viewer. Two more `notest_` viewers
followed the same interop pattern: an SDF raymarch viewer
(`tests/cuda/windows/notest_raymarch_viewer.cu`) and a CUDA voxel viewer
(`notest_voxel_viewer.cu`) that ray-marches a voxel world (terrain, per-voxel
materials, a filtered color grid, and a free-SDF avatar) per pixel on the GPU,
each frame staying in VRAM via the same `cudaGraphicsD3D11*` interop. The
reusable headers they grew sit in `corvid/cuda/` (`vec`, `camera`, `radians`,
`sdf`, `raycast`, `density_field`, `material_volume`, `strata`, `terrain`,
`voxel_render`, `render_config`, `avatar`) and `corvid/cuda/windows/game/`
(`avatar_tuning`, `config_panel`), on top of a generalized CUDA RAII layer
(`cuda_handle` and the `cuda_array_3d` / `cuda_texture` / `cuda_surface` /
`cuda_volume` owners). The one registered Windows-only CUDA test,
`tests/cuda/windows/cuda_d3d11_interop_test.cu`, covers the interop path; the
viewers are `notest_` GUI apps, not CTest targets.

Dear ImGui is a second new external dependency, for the viewers' live tuning
panel. Unlike SDL3 it ships no prebuilt binaries and no CMakeLists, so it is
brought in by FetchContent (source only) and compiled into a small static library
from the core plus its SDL3 and D3D11 backends (`corvid_require_imgui` in
`tests/CMakeLists.txt`, gated like `corvid_require_sdl3` on a source actually
pulling in an ImGui header). The vendored sources are not clean under the
project's `-Wall -Wextra -Werror`, so the lib builds with `-w`; because `-w` does
not reach clang-tidy, the global `CMAKE_CXX_CLANG_TIDY` is also cleared on that
target so `./cleanbuild.ps1 tidy` stays clean. The overlay itself is wrapped
RAII-style in `corvid/cuda/windows/imgui_overlay.h`.

Resolved this round (detail in sections 7 and 8): device correctness via
`./cleanbuild.ps1 cudacheck` (compute-sanitizer) and device debugging via Nsight
Visual Studio Edition, with the CUDA build's `-gline-tables-only` giving both
source attribution; `./cleanbuild.ps1 tidy` brought clean (the MSVC-STL-only
findings carry NOLINTs, section 6); and `cuda-gdb` ruled out (Windows has none;
the project left WSL on purpose). One thing verified only structurally, not
interactively: that Nsight VSE sets a live device breakpoint in a clang-built
kernel. The debug info is provably present and in NVIDIA's format, but the GUI
session itself was not driven from the build harness.

Implemented: window resize, multi-monitor, and device-lost recovery
(2026-06-20). Resize is a cross-layer rebuild, not a swapchain-local change.
The swapchain part is small: `d3d11_swapchain::resize()` calls
`IDXGISwapChain::ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0)` to re-read the
client area, then re-acquires the back buffer via `GetBuffer(0)` and re-reads
the stored width/height. The real work is the CUDA interop: the kernel writes a
separate `render_texture` whose live `w x h` region is copied into the back
buffer, and `cuda_d3d11_resource` is bound to that texture, so a naive resize
would recreate the texture, unregister and re-register the CUDA resource, and
recompute the kernel grid on every resize event.

The optimization treats the render texture like a `std::vector`'s backing
store: a grow-only capacity, decoupled from the live size. Two sizes are kept
distinct. The live `w x h` is the actual client pixel size; it drives the
swapchain back buffer (exact, via `ResizeBuffers`), the kernel launch grid, and
the copy-out box. The capacity is the allocated size of the CUDA-registered
`render_texture`, held at or above the live size, each axis rounded up to a
256-pixel quantum. A resize that still fits the current capacity does no
reallocation and no CUDA re-registration; it changes only the launch grid and
the copy box. Only a resize that exceeds the capacity grows it (recreate the
texture, re-register CUDA, recompute the grid), and the capacity never shrinks.
The copy-out is `CopySubresourceRegion` of the live box from the
possibly-larger texture into the exact-size back buffer, so the slack is never
read.

This beats a fixed-maximum preallocation on all three goals. It is correct with
no precomputed ceiling: a window spanning two monitors (wider than either), a
monitor changing resolution, and hot-plugging a larger display are all just
"the live size grew," handled by the same growth path rather than a
special-case fallback. It is not wasteful: capacity tracks the largest size
actually used this session, not the theoretical multi-monitor maximum, so a
small window holds a small texture. And it is efficient on the common path:
shrinking, and dragging across same-or-smaller monitors, reallocate nothing,
while a grow-drag reallocates only when it crosses a 256-pixel boundary
(without the quantum it would reallocate every frame it grows). The 256-pixel
slack is a few MB at 4K, negligible against the saving. Because there is no cap
to precompute, multi-monitor support needs no `SDL_GetDisplays` query at all;
the viewer reacts only to `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`.

The separate `render_texture` is what makes this pay off. `ResizeBuffers`
re-creates the swapchain back buffers on every resize regardless, so binding
CUDA to the back buffer would force a re-registration every resize. The owned
texture lets the CUDA registration outlive resizes; grow-only capacity then
makes the (rare) growth the only time that registration churns.

Device-lost recovery shares this teardown-and-rebuild machinery. `Present` and
`ResizeBuffers` can return `DXGI_ERROR_DEVICE_REMOVED` /
`DXGI_ERROR_DEVICE_RESET` (`d3d11_swapchain::is_device_lost` classifies the
`hr_status`); on that the viewer recreates everything downstream of the lost
device (the device, swapchain, render texture, and CUDA registration), resetting
the grow-only capacity, then continues. The rebuild path is therefore written
as "(re)create from the current size," not "resize." The swapchain rebuild is
`d3d11_swapchain::reset(device, hwnd)`, which releases the old swapchain and
flushes D3D11's deferred destruction (`ClearState` + `Flush`) before creating
the new one, because DXGI binds only one flip-model swapchain to an `HWND` at a
time (a plain move-assignment would build the replacement before releasing the
original, and the deferred teardown would not have freed the `HWND` yet). With
that sequence owned by the wrapper, the device and swapchain are held as plain
values; no `std::optional` empty state is needed.

Gotchas handled in the implementation: `ResizeBuffers` fails unless all
outstanding back-buffer references are released first, so `resize()` resets
`back_buffer_` before the call and re-acquires after, and any caller-held view
counts too; a minimized window has a 0x0 client area, on which both
`ResizeBuffers` and rendering are skipped; edge-drag fires a storm of resize
events, so the latest pixel size is coalesced into one rebuild before the next
frame; and the swapchain wants pixels, so the size comes from SDL's pixel-size
events (`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`), not the logical window size,
which differ under DPI scaling. The window is created resizable, and
`sdl_event` exposes the pixel-size-changed event.

Known SDL limitation, cross-DPI resize (investigated 2026-06-20, accepted as
upstream). Dragging a window that straddles two monitors of different scale (for
example a 150% portrait display next to a 100% landscape one) can momentarily
collapse the window's client height to 0 (it first jumps larger, then
collapses). This is SDL's own `WM_DPICHANGED` handling: SDL applies Windows'
suggested rectangle via `SetWindowPos`, which scales the window by the DPI ratio
and, interacting with the in-flight modal resize drag, drives the height to 0.
It is in SDL, not in this code: `d3d11_swapchain::resize()` only follows the
window size, and a logging build confirmed the minimum size (below) is honored
for an ordinary drag at constant DPI but is bypassed on the DPI-transition
resize. SDL3 has no hint or window flag to opt out (the SDL2
`SDL_HINT_WINDOWS_DPI_SCALING` / `_DPI_AWARENESS` knobs were removed and not
replaced), `SDL_WINDOW_HIGH_PIXEL_DENSITY` does not affect this path (tried),
and resizing the window back during the event is reported to flash and revert,
so there is no clean fix. The consequence is contained rather than cured, by two
guards that are correct in their own right:

- A minimum window size (`sdl_window::set_minimum_size`, 640x480 in the viewer)
  keeps an ordinary drag from shrinking the window to nothing. It is honored on
  the normal path; the cross-DPI path ignores it, hence the residual collapse.
- The render loop skips all GPU work (kernel, copy, present) on any zero-area
  window, queried each frame via `SDL_GetWindowSizeInPixels`. So whether the
  window is collapsed by this SDL bug or simply minimized, no kernel runs and
  nothing is presented to a dead surface, and the frame resumes the moment the
  window has area again. The collapse is then a transient cosmetic glitch under
  a narrow condition, with no wasted GPU work and no incorrect rendering.

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
