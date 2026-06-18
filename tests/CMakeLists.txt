cmake_minimum_required(VERSION 3.18)

project(Corvid VERSION 1.0 LANGUAGES CXX)

# Register tests with CTest. cleanbuild.sh and tests/comprehensive.cmake both
# drive `ctest` rather than looping over release_bin/* directly.
enable_testing()

# Emit compile_commands.json into the build dir. Required by the Clang Static
# Analyzer (`./cleanbuild.sh scan` -> analyze-build); also consumed by clangd
# and other tooling, so always-on.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Allow selecting the standard library. Passing -DUSE_LIBSTDCPP=ON to cmake will
# force libstdc++. Otherwise, the default is libc++ unless we're running on
# Codex, where libstdc++ is required.
option(USE_LIBSTDCPP "Use GNU libstdc++ instead of libc++" OFF)
option(USE_CLANG_TIDY "Run clang-tidy analysis" OFF)
option(CORVID_ENABLE_CUDA "Build .cu targets with nvcc (host g++-15, libstdc++)" OFF)
# Default OFF keeps asserts live, so a bare configure and the analysis modes
# (sanitizers, tidy, scan) all retain them. cleanbuild.sh turns this ON only
# for the plain and coverage builds, where the assert cost is not wanted.
option(CORVID_DISABLE_ASSERTS "Define NDEBUG to strip asserts" OFF)
set(TEST_NAME "" CACHE STRING "Build only the named test source file")

# Set the C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On Codex, default to libstdc++ unless explicitly overridden.
if(DEFINED ENV{CODEX_PROXY_CERT} AND NOT USE_LIBSTDCPP)
    message(STATUS "Codex environment detected: using libstdc++")
    set(USE_LIBSTDCPP ON CACHE BOOL "Use GNU libstdc++ instead of libc++" FORCE)
else()
    if(USE_LIBSTDCPP)
        message(STATUS "Using libstdc++ (manual override)")
    else()
        message(STATUS "Using libc++")
    endif()
endif()

# Default libc++ prefix is the system clang-22 install. SANITIZER=msan
# overrides this to the locally built MSAN-instrumented libc++ so that the
# C++ stdlib code MSAN's instrumentation observes is itself instrumented.
set(LIBCXX_PREFIX "/usr/lib/llvm-22")
if(SANITIZER STREQUAL "msan")
    if(USE_LIBSTDCPP)
        message(FATAL_ERROR "SANITIZER=msan requires libc++ (USE_LIBSTDCPP=OFF)")
    endif()
    set(LIBCXX_PREFIX "${CMAKE_SOURCE_DIR}/.local/llvm-msan")
    if(NOT EXISTS "${LIBCXX_PREFIX}/lib/libc++.a")
        message(FATAL_ERROR
            "MSAN libc++ not found at ${LIBCXX_PREFIX}/lib/libc++.a. "
            "Run scripts/build_msan_libcxx.sh first (one-time, ~10 min).")
    endif()
    message(STATUS "Using MSAN libc++ at ${LIBCXX_PREFIX}")
endif()

if(WIN32)
    # Windows builds the portable (and cuda) buckets against the MSVC STL with
    # either clang++ (default) or MSVC cl, mirroring the clang/gcc split on
    # Linux. clang++ drives a GNU-style command line targeting the MSVC ABI: the
    # same driver and warning flags as Linux clang, which also keeps clangd's
    # parsing aligned. cl is the conformance-mode second compiler. (The CUDA
    # bucket also uses clang++, because nvcc's MSVC device frontend has no C++23
    # dialect; see the CUDA section.) The libc++/clang-22 knobs in the Linux
    # branches (-nostdinc++, the libc++ include prefix, -march=native,
    # -fexperimental-library, the libc++ link options) have no MSVC-ABI analog,
    # so none apply here. The Linux network stack is excluded at the
    # source-bucket level. See crossplatform.md.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # clang++ (GNU-like driver, MSVC ABI). Plain -Wall -Wextra -Werror as on
        # Linux (no /clang: escape, unlike the retired clang-cl path); CMake's
        # CMAKE_CXX_STANDARD=23 yields a plain -std=c++23 that clangd parses
        # correctly. -fms-runtime-lib=dll selects the dynamic (/MD) CRT to match
        # the .cu bucket and keep ASAN's dynamic runtime usable; clang++ would
        # otherwise default to the static (/MT) CRT.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -fms-runtime-lib=dll")
    else()
        # MSVC cl, the conformance-mode second compiler. cl has no /std:c++23, so
        # request C++23 as /std:c++latest (overriding CMake's default), which cl
        # accepts. clang++ above needs no such override.
        set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/std:c++latest")
        set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/std:c++latest")
        # /W4 with warnings as errors (cl rejects the /clang: escape).
        # /permissive- turns on conformance mode (proper two-phase template name
        # lookup among other things), without which cl misses the operators that
        # clang finds via using-directive (e.g. the sequence-enum `operator*`).
        #
        # /Zc:preprocessor selects the conformant preprocessor (as clang uses),
        # without which cl corrupts raw string literals passed through macros
        # (e.g. a JSON `\uXXXX` inside a Catch2 CHECK re-forms as an invalid
        # universal-character-name).
        #
        # /wd silences warnings that fire beyond clang's -Wall -Wextra (the level
        # this build mirrors) on correct, idiomatic code rather than real bugs:
        #   4245 - signed/unsigned mismatch passing a literal (deliberate -1 etc.
        #          in wraparound tests).
        #   4267 - narrowing size_t to a smaller integer (an in-range index/count).
        #   4305 - "truncation" using an integer/size as a bool condition.
        #   4310 - cast truncates a constant value (deliberate wraparound tests).
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /permissive- /Zc:preprocessor /W4 /WX /wd4245 /wd4267 /wd4305 /wd4310")
    endif()
    # cleanbuild.ps1 drives this as a Release build: optimization and NDEBUG come
    # from CMAKE_BUILD_TYPE=Release (like Linux's -O3). ide_build.ps1 is the
    # separate debug (/Od) path.
elseif(USE_LIBSTDCPP)
    # Rely on whichever clang is available and the default libstdc++ install.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -Werror -std=c++23")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # At -O3, gcc raises false-positive -Wmaybe-uninitialized and
        # -Warray-bounds from inside libstdc++ and Catch2 template
        # instantiations, not from Corvid code, so they cannot be suppressed at
        # the source. Keep them as warnings but do not fail the build on them.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=maybe-uninitialized -Wno-error=array-bounds")
    endif()
else()
    # Use Clang 22 and avoid pulling in GCC.
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++-22")
    set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-linux-gnu")
    # `-fexperimental-library` unlocks libc++'s tzdb / utc_clock / zoned_time
    # headers (gated behind `_LIBCPP_HAS_EXPERIMENTAL_TZDB`); the matching
    # runtime lives in libc++experimental, which must come before libc++ in
    # the link order because it references symbols from libc++.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -Werror -nostdinc++ -isystem ${LIBCXX_PREFIX}/include/c++/v1 -std=c++23 -fexperimental-library")
    # These are libc++/clang link flags. We add them with add_link_options
    # guarded by $<LINK_LANGUAGE:CXX> rather than the global
    # CMAKE_EXE_LINKER_FLAGS so that only CXX-linked targets see them. CUDA
    # (.cu) executables are linked by g++-15 (the nvcc host compiler) and must
    # stay on libstdc++; g++-15 also rejects -stdlib=libc++ / --rtlib=compiler-rt
    # outright. The genex keeps these off that link line. -lc++experimental must
    # precede -lc++ (it references libc++ symbols). The rpath lets binaries find
    # the libc++/libunwind .so files at the chosen prefix without
    # LD_LIBRARY_PATH: harmless for the system path, essential for
    # SANITIZER=msan where LIBCXX_PREFIX is outside the ld.so config.
    add_link_options(
        "$<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>"
        "$<$<LINK_LANGUAGE:CXX>:--rtlib=compiler-rt>"
        "$<$<LINK_LANGUAGE:CXX>:-fuse-ld=lld>"
        "$<$<LINK_LANGUAGE:CXX>:-L${LIBCXX_PREFIX}/lib>"
        "$<$<LINK_LANGUAGE:CXX>:-lc++experimental>"
        "$<$<LINK_LANGUAGE:CXX>:-lc++>"
        "$<$<LINK_LANGUAGE:CXX>:-lc++abi>"
        "$<$<LINK_LANGUAGE:CXX>:-Wl,-rpath,${LIBCXX_PREFIX}/lib>")
endif()

# Use ccache as a compiler launcher when available so that cleanbuild's
# `rm -rf tests/build/` doesn't trigger a full Catch2 (and Corvid) recompile
# every run. Cache hits return cached `.o` files in milliseconds.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    # Cache nvcc too, so a clean rebuild of an unchanged .cu restores from
    # cache instead of recompiling. ccache hashes -arch=native verbatim, so the
    # cache is only valid while the build host's GPU is unchanged -- true here.
    set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Optional coverage mode. Selected via -DCOVERAGE=ON (or `./cleanbuild.sh
# coverage`). Builds with `-fprofile-instr-generate -fcoverage-mapping` so
# every test binary writes raw profiles at runtime; cleanbuild.sh then merges
# them with `llvm-profdata` and produces a report with `llvm-cov`.
# Mutually exclusive with sanitizers (they instrument conflicting runtimes)
# and with clang-tidy (build-time analyzer adds nothing here).
option(COVERAGE "Build with source-based coverage instrumentation" OFF)
if(COVERAGE)
    if(SANITIZER)
        message(FATAL_ERROR "COVERAGE=ON cannot be combined with SANITIZER=${SANITIZER}")
    endif()
    if(USE_CLANG_TIDY)
        message(FATAL_ERROR "COVERAGE=ON cannot be combined with USE_CLANG_TIDY=ON")
    endif()
    if(USE_LIBSTDCPP)
        message(FATAL_ERROR "COVERAGE=ON requires libc++ (USE_LIBSTDCPP=OFF) for matching llvm-cov tooling")
    endif()
    # Coverage needs a low-but-not-zero optimization level so the source-to-
    # counter mapping stays close to source: aggressive inlining, unrolling,
    # and dead-code elimination at -O3 silently merge or zero out counters
    # for header-only template code. Strip the release flags and replace
    # with -O1 -g. See the discussion in TODO item 2 for context.
    string(REGEX REPLACE "-O3"            "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "-march=native"  "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(_cov_flags "-O1 -g -fprofile-instr-generate -fcoverage-mapping")
    message(STATUS "Coverage enabled: ${_cov_flags}")
    set(CMAKE_CXX_FLAGS        "${CMAKE_CXX_FLAGS} ${_cov_flags}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_cov_flags}")
endif()

# Optional sanitizer mode. Selected via -DSANITIZER=asan|tsan|ubsan (or via
# `./cleanbuild.sh asan` etc.). Sanitizers need the same -fsanitize flags on
# both compile and link lines, plus -fno-omit-frame-pointer for usable stack
# traces and -g so the traces resolve to source locations. The flags propagate
# into the FetchContent Catch2 build so its objects match.
set(SANITIZER "" CACHE STRING
    "Enable sanitizer mode: asan, tsan, ubsan, or msan")
set(_extra_san_flags "")
if(SANITIZER)
    if(SANITIZER STREQUAL "asan")
        # ASAN + UBSAN: compatible and complementary; this is the common combo.
        set(_san "address,undefined")
    elseif(SANITIZER STREQUAL "tsan")
        # TSAN is incompatible with ASAN; runs in its own build.
        set(_san "thread")
    elseif(SANITIZER STREQUAL "ubsan")
        set(_san "undefined")
    elseif(SANITIZER STREQUAL "msan")
        # MSAN needs the MSAN-instrumented libc++ already wired in above.
        # NOTE: origin tracking (-fsanitize-memory-track-origins) was tried
        # but caused stack overflows in heavily templated tests under our
        # compile flags. Plain MSAN still reports uninitialized reads; the
        # origin pinpoint is the loss.
        set(_san "memory")
        # Apply the same ignorelist used for the libc++ MSAN rebuild so that
        # libc++ template instantiations in user TUs (Catch2 reporters,
        # iostream uses in test code) are not instrumented either.
        set(_ignorelist "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/msan-libcxx-ignorelist.txt")
        if(NOT EXISTS "${_ignorelist}")
            message(FATAL_ERROR "MSAN ignorelist not found at ${_ignorelist}")
        endif()
        set(_extra_san_flags "-fsanitize-ignorelist=${_ignorelist}")
    else()
        message(FATAL_ERROR
            "Unknown SANITIZER=${SANITIZER} (expected asan|tsan|ubsan|msan)")
    endif()
    message(STATUS "Sanitizer enabled: ${SANITIZER} (-fsanitize=${_san})")
    set(_san_flags "-fsanitize=${_san} ${_extra_san_flags} -fno-omit-frame-pointer -g")
    # MSAN doesn't play nicely with -march=native: AVX intrinsics can read
    # past initialized data in ways MSAN can't model, producing crashes
    # rather than diagnostic reports. Strip it for MSAN runs.
    if(SANITIZER STREQUAL "msan")
        string(REGEX REPLACE "-march=native" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_san_flags}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_san_flags}")
endif()

# Windows sanitizer guards. clang++ drives the link, so the
# -fsanitize=address,undefined set above is honored on the link line and clang++
# pulls in the dynamic asan runtime + thunk itself. (The retired clang-cl path
# had to add those clang_rt libs by hand, because CMake linked it through
# lld-link directly, bypassing the driver.) -g (set above) gives the CodeView
# debug info asan symbolizes from.
if(SANITIZER AND WIN32)
    # Sanitizers on Windows require clang++ (the LLVM compiler-rt runtimes); MSVC
    # cl has no compatible runtime, so fail fast rather than chase a nonexistent
    # clang-runtime path. Same compiler-ID split as the top WIN32 block.
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "On Windows, sanitizer builds require clang++, not MSVC cl. "
            "Use ./cleanbuild.ps1 asan.")
    endif()
    # Only ASAN is supported on Windows, and it carries UBSAN with it
    # (-fsanitize=address,undefined; the dynamic asan runtime also resolves the
    # __ubsan_* handlers). Standalone UBSAN cannot link here: LLVM ships only a
    # static-CRT ubsan runtime (clang_rt.ubsan_standalone*-x86_64.lib is
    # MT_StaticRelease, no _dynamic variant) that clashes with this /MD build
    # (lld-link /failifmismatch). tsan/msan are Linux-only too. Fail fast with
    # the reason rather than the cryptic linker error.
    if(NOT SANITIZER STREQUAL "asan")
        message(FATAL_ERROR
            "On Windows only SANITIZER=asan is supported; it already includes "
            "UBSAN. SANITIZER=${SANITIZER} has no /MD-compatible runtime here.")
    endif()
endif()

# Strip asserts via NDEBUG when requested. Applied as a compile definition (not
# a CXX flag) so the .cu targets, compiled by nvcc, pick it up too.
if(CORVID_DISABLE_ASSERTS)
    add_compile_definitions(NDEBUG)
    message(STATUS "Asserts disabled (NDEBUG)")
else()
    message(STATUS "Asserts live (NDEBUG not set)")
endif()

# External dependencies for the linux bucket (io_uring / QUIC / HTTP/3). None of
# this exists or is wanted on Windows, where only the portable and cuda buckets
# build, so it is all guarded behind a Linux check. See crossplatform.md.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
# External dependencies: requires apt install liburing-dev pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)
endif()

# Catch2 v3. Under libstdc++, prefer a system install (apt install catch2),
# since Ubuntu's package is built against libstdc++ and links cleanly. Under
# libc++ we must build Catch2 from source against the same standard library
# (mixing the two produces unresolved std::__cxx11 / std::__1 symbols at link
# time), so we always go through FetchContent there.
set(_have_catch2 FALSE)
if(USE_LIBSTDCPP)
    find_package(Catch2 3 QUIET)
    if(Catch2_FOUND)
        message(STATUS "Using system Catch2 ${Catch2_VERSION} (libstdc++)")
        set(_have_catch2 TRUE)
    endif()
endif()
if(NOT _have_catch2)
    # Persist the FetchContent cache outside the build dir so cleanbuild.sh's
    # `rm -rf tests/build/` doesn't trigger a full Catch2 rebuild every run.
    # Skip update checks once the source is populated for faster reconfigures.
    set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/.fetchcontent"
        CACHE PATH "FetchContent cache (persisted across builds)")
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    message(STATUS "Fetching Catch2 v3.6.0 via FetchContent (cache: ${FETCHCONTENT_BASE_DIR})")
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.6.0
    )
    FetchContent_MakeAvailable(Catch2)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
# QUIC dependency: ngtcp2 (ExternalProject) linked against a preflight-built
# OpenSSL 3.5+ (the LTS line; needed for the native QUIC API, which Ubuntu
# Noble's 3.0.13 lacks). Linked into every test target -- the static linker
# drops unreferenced object files, so tests that do not include any QUIC
# headers pay essentially nothing.
#
# ExternalProject_Add rather than FetchContent because ngtcp2's CMakeLists
# runs an autoconf-style probe sweep (`check_function_exists`,
# `check_include_files`, ...) and the answers live in our parent
# `CMakeCache.txt`, which `cleanbuild.sh` wipes every run. ExternalProject
# runs the sub-build as a separate CMake invocation with its own persistent
# cache under `tests/.local/ngtcp2-external/`, so the probes execute exactly
# once across cleanbuilds. It also isolates target names (the nghttp3
# `check`-target collision goes away when we add it later) and prevents
# `CMAKE_BUILD_TYPE` from leaking back into our compile lines.
# Under MSAN, OpenSSL and ngtcp2 must themselves be instrumented; an
# uninstrumented static OpenSSL floods every QUIC test with use-of-uninit
# false positives (its internal lhash/config/memcmp reads). The instrumented
# build installs to a separate prefix so it coexists with the plain one.
if(SANITIZER STREQUAL "msan")
    set(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.local/openssl-msan")
    set(_openssl_build_hint "scripts/build_openssl_quic.sh msan")
else()
    set(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.local/openssl")
    set(_openssl_build_hint "scripts/build_openssl_quic.sh")
endif()
if(NOT EXISTS "${OPENSSL_ROOT_DIR}/lib64/libssl.a")
    message(FATAL_ERROR
        "OpenSSL 3.5+ not found at ${OPENSSL_ROOT_DIR}. "
        "Run ${_openssl_build_hint} first (one-time, ~5 min).")
endif()
set(OPENSSL_USE_STATIC_LIBS ON)
find_package(OpenSSL 3.5 REQUIRED COMPONENTS SSL Crypto)
message(STATUS "Using OpenSSL ${OPENSSL_VERSION} at ${OPENSSL_ROOT_DIR}")

include(ExternalProject)

# A `-msan` suffix keeps the instrumented ngtcp2 (built with MSAN cflags
# against the instrumented OpenSSL) from clobbering the plain install, mirroring
# the OpenSSL split above.
if(SANITIZER STREQUAL "msan")
    set(_ngtcp2_suffix "-msan")
else()
    set(_ngtcp2_suffix "")
endif()
set(NGTCP2_PREFIX_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/.local/ngtcp2-external${_ngtcp2_suffix}")
set(NGTCP2_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.local/ngtcp2${_ngtcp2_suffix}")
set(NGTCP2_LIB_DIR     "${NGTCP2_INSTALL_DIR}/lib")
set(NGTCP2_INCLUDE_DIR "${NGTCP2_INSTALL_DIR}/include")

# Forward MSAN instrumentation into ngtcp2's sub-build: clang plus the same
# plain `-fsanitize=memory` the test side uses (origin tracking overflows the
# stack in our templated TUs). Without this, ngtcp2's own writes stay invisible
# to MSAN and its output poisons our QUIC code paths.
set(_ngtcp2_san_args "")
if(SANITIZER STREQUAL "msan")
    set(_ngtcp2_san_args
        "-DCMAKE_C_COMPILER=clang-22"
        "-DCMAKE_C_FLAGS=-fsanitize=memory -fno-omit-frame-pointer -g")
endif()

# `BUILD_BYPRODUCTS` lists the artifacts the sub-build produces. Ninja
# requires this to track downstream dependencies on imported targets that
# reference these files. `CMAKE_INSTALL_LIBDIR=lib` forces a flat `lib/`
# rather than the `lib64/` that GNUInstallDirs picks on some distros, so
# the IMPORTED_LOCATION paths below are stable.
ExternalProject_Add(ngtcp2_external
    PREFIX            "${NGTCP2_PREFIX_DIR}"
    GIT_REPOSITORY    https://github.com/ngtcp2/ngtcp2.git
    GIT_TAG           v1.22.1
    GIT_SHALLOW       TRUE
    UPDATE_DISCONNECTED TRUE
    INSTALL_DIR       "${NGTCP2_INSTALL_DIR}"
    CMAKE_GENERATOR   "Ninja"
    # Route ngtcp2's configure output to a log file rather than the console.
    # Its crypto-backend probe warns that quictls and LibreSSL are disabled
    # (benign: we build the OpenSSL backend), which otherwise clutters every
    # clean build and inflates the CTest dashboard warning count. The log is
    # still printed if configure fails.
    LOG_CONFIGURE 1
    LOG_OUTPUT_ON_FAILURE 1
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${NGTCP2_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
        ${_ngtcp2_san_args}
        -DENABLE_OPENSSL=ON
        -DENABLE_STATIC_LIB=ON
        -DENABLE_SHARED_LIB=OFF
        -DENABLE_LIB_ONLY=ON
        -DENABLE_WERROR=OFF
        -DBUILD_TESTING=OFF
    BUILD_BYPRODUCTS
        "${NGTCP2_LIB_DIR}/libngtcp2.a"
        "${NGTCP2_LIB_DIR}/libngtcp2_crypto_ossl.a"
)

# The install dir's include subdir must exist at configure time, otherwise
# CMake errors on the IMPORTED_LOCATION / INTERFACE_INCLUDE_DIRECTORIES
# property below. Create it eagerly; ngtcp2_external will populate it on
# first build.
file(MAKE_DIRECTORY "${NGTCP2_INCLUDE_DIR}")

add_library(ngtcp2_static STATIC IMPORTED GLOBAL)
set_target_properties(ngtcp2_static PROPERTIES
    IMPORTED_LOCATION             "${NGTCP2_LIB_DIR}/libngtcp2.a"
    INTERFACE_INCLUDE_DIRECTORIES "${NGTCP2_INCLUDE_DIR}")
add_dependencies(ngtcp2_static ngtcp2_external)

add_library(ngtcp2_crypto_ossl_static STATIC IMPORTED GLOBAL)
set_target_properties(ngtcp2_crypto_ossl_static PROPERTIES
    IMPORTED_LOCATION             "${NGTCP2_LIB_DIR}/libngtcp2_crypto_ossl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${NGTCP2_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES      "ngtcp2_static;OpenSSL::SSL;OpenSSL::Crypto")
add_dependencies(ngtcp2_crypto_ossl_static ngtcp2_external)

# HTTP/3 dependency: nghttp3 (ExternalProject), the same pattern as ngtcp2
# above. nghttp3 is crypto-agnostic -- it implements HTTP/3 framing and QPACK
# and has no OpenSSL dependency -- so its config is ngtcp2's minus the crypto
# knobs. ExternalProject (rather than FetchContent) for the same reasons: it
# runs the autoconf-style probe sweep in an isolated cache that survives
# `cleanbuild.sh`'s build-dir wipe, and it sidesteps nghttp3's top-level
# `check` target colliding with ngtcp2's (each sub-build is its own CMake
# invocation, so target names never share a namespace).
if(SANITIZER STREQUAL "msan")
    set(_nghttp3_suffix "-msan")
else()
    set(_nghttp3_suffix "")
endif()
set(NGHTTP3_PREFIX_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/.local/nghttp3-external${_nghttp3_suffix}")
set(NGHTTP3_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.local/nghttp3${_nghttp3_suffix}")
set(NGHTTP3_LIB_DIR     "${NGHTTP3_INSTALL_DIR}/lib")
set(NGHTTP3_INCLUDE_DIR "${NGHTTP3_INSTALL_DIR}/include")

# Forward MSAN instrumentation into nghttp3's sub-build, mirroring ngtcp2:
# clang plus the same plain `-fsanitize=memory` (origin tracking overflows the
# stack in our templated TUs). Without this, nghttp3's own writes stay
# invisible to MSAN and poison our HTTP/3 code paths with false positives.
set(_nghttp3_san_args "")
if(SANITIZER STREQUAL "msan")
    set(_nghttp3_san_args
        "-DCMAKE_C_COMPILER=clang-22"
        "-DCMAKE_C_FLAGS=-fsanitize=memory -fno-omit-frame-pointer -g")
endif()

ExternalProject_Add(nghttp3_external
    PREFIX            "${NGHTTP3_PREFIX_DIR}"
    GIT_REPOSITORY    https://github.com/ngtcp2/nghttp3.git
    GIT_TAG           v1.15.0
    GIT_SHALLOW       TRUE
    UPDATE_DISCONNECTED TRUE
    INSTALL_DIR       "${NGHTTP3_INSTALL_DIR}"
    CMAKE_GENERATOR   "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${NGHTTP3_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        ${_nghttp3_san_args}
        -DENABLE_STATIC_LIB=ON
        -DENABLE_SHARED_LIB=OFF
        -DENABLE_LIB_ONLY=ON
        -DENABLE_WERROR=OFF
        -DBUILD_TESTING=OFF
    BUILD_BYPRODUCTS
        "${NGHTTP3_LIB_DIR}/libnghttp3.a"
)

# As with ngtcp2: the include dir must exist at configure time for the
# IMPORTED target properties below. nghttp3_external populates it on first
# build.
file(MAKE_DIRECTORY "${NGHTTP3_INCLUDE_DIR}")

add_library(nghttp3_static STATIC IMPORTED GLOBAL)
set_target_properties(nghttp3_static PROPERTIES
    IMPORTED_LOCATION             "${NGHTTP3_LIB_DIR}/libnghttp3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${NGHTTP3_INCLUDE_DIR}")
add_dependencies(nghttp3_static nghttp3_external)
endif() # CMAKE_SYSTEM_NAME STREQUAL "Linux" (io_uring / QUIC / HTTP/3 deps)

if(USE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-22)
    if(CLANG_TIDY_EXE)
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
        # Hardcode disabling StackAddressEscape explicitly
        set(CMAKE_CXX_CLANG_TIDY
            "${CLANG_TIDY_EXE}"
        )
    else()
        message(WARNING "clang-tidy requested but not found")
    endif()
endif()

# CUDA (.cu) targets. The cleanbuild driver sets CORVID_ENABLE_CUDA=ON (and
# passes the CUDA compiler + architecture) whenever the toolkit is present and
# the build is a plain one (no sanitizer/coverage/scan). The CUDA compiler
# differs by platform: nvcc with a g++-15 host on Linux (nvcc rejects gcc newer
# than 15), and clang (clang++) on Windows, because nvcc 13.3's MSVC device
# frontend has no C++23 dialect (see the flag block below). The .cu targets are
# independent single-source executables that share no link line with a .cpp
# binary, so they coexist with whatever compiler/stdlib the .cpp suite uses; the
# libc++ link flags above are CXX-only and never reach this link line.
if(CORVID_ENABLE_CUDA)
    enable_language(CUDA)
    if(NOT CMAKE_CUDA_ARCHITECTURES)
        # `native` resolves to the build host's GPU (sm_89 on an RTX 4090) under
        # nvcc on Linux. On Windows the cleanbuild driver passes an explicit
        # sm_NN: clang-CUDA's `native` detection does not work there.
        set(CMAKE_CUDA_ARCHITECTURES native)
    endif()
    # CMake 3.28 cannot map CUDA_STANDARD 23 (it rejects the dialect as
    # unsupported by nvcc), though nvcc 13.3 accepts -std=c++23. Clear CMake's
    # default standard flag and pass c++23 ourselves so the compiler command
    # carries a single -std (no "incompatible redefinition for option 'std'").
    set(CMAKE_CUDA_STANDARD_DEFAULT "")
    if(WIN32)
        # Windows compiles the CUDA bucket with clang (clang++), not nvcc: nvcc
        # 13.3's MSVC device frontend has no C++23 dialect (its -std=c++23 maps
        # to --ms_c++14 and is dropped), so it cannot build Corvid's C++23
        # headers, whereas clang's unified C++23 frontend compiles host and
        # device together. -fms-runtime-lib=dll picks the dynamic (/MD) CRT to
        # match the FetchContent Catch2 (clang defaults to the static /MT one);
        # -Wno-unknown-cuda-version silences the note that CUDA 13.3 is newer
        # than clang 22's last fully supported toolkit (codegen is unaffected).
        # -gline-tables-only is the clang analog of the Linux side's -lineinfo:
        # it embeds the SASS<->source line mapping (.nv_debug_line_sass) that
        # compute-sanitizer (./cleanbuild.ps1 cudacheck) and Nsight attribute
        # results from, while keeping device optimization intact. Release
        # supplies the optimization (-O3 -DNDEBUG). See crossplatform.md.
        string(APPEND CMAKE_CUDA_FLAGS
            " -std=c++23 -fms-runtime-lib=dll -Wno-unknown-cuda-version -gline-tables-only")
    else()
        # Linux uses nvcc (g++-15 host). -lineinfo adds source-line correlation
        # to the optimized SASS so Nsight (and compute-sanitizer) can attribute
        # results to source lines; unlike -G it leaves device optimization
        # intact, so the runtime cost is negligible.
        string(APPEND CMAKE_CUDA_FLAGS " -std=c++23 -O3 -lineinfo")
    endif()
    message(STATUS
        "CUDA enabled: ${CMAKE_CUDA_COMPILER} (arch ${CMAKE_CUDA_ARCHITECTURES})")
endif()

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/release_bin")

# Test sources are partitioned into platform buckets: portable/ builds on every
# platform, linux/ only on Linux, cuda/ only when CORVID_ENABLE_CUDA (handled in
# the CUDA section). .cpp builds the CXX suite below; .cu is routed to the CUDA
# loop further down. See crossplatform.md.
file(GLOB PORTABLE_SOURCES "portable/*.cpp")
set(SOURCES ${PORTABLE_SOURCES})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    file(GLOB LINUX_SOURCES "linux/*.cpp")
    list(APPEND SOURCES ${LINUX_SOURCES})
endif()

if(TEST_NAME)
    # Locate the single requested source in whichever bucket holds it.
    set(FILTERED_SOURCE "")
    set(_buckets portable linux cuda)
    if(WIN32)
        # Windows-only CUDA cell (D3D/SDL3 targets); see crossplatform.md s11.
        list(APPEND _buckets cuda/windows)
    endif()
    foreach(_bucket ${_buckets})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_bucket}/${TEST_NAME}")
            set(FILTERED_SOURCE
                "${CMAKE_CURRENT_SOURCE_DIR}/${_bucket}/${TEST_NAME}")
        endif()
    endforeach()
    if(NOT FILTERED_SOURCE)
        message(FATAL_ERROR
            "Requested test source not found in any bucket: ${TEST_NAME}")
    endif()

    # Route the single requested source to its language. A .cu filter leaves
    # SOURCES empty (the CXX loop builds nothing) and hands the file to the
    # CUDA loop; a .cpp filter does the reverse.
    if(TEST_NAME MATCHES "\\.cu$")
        if(NOT CORVID_ENABLE_CUDA)
            message(FATAL_ERROR
                "Requested .cu source '${TEST_NAME}' but CUDA is not enabled")
        endif()
        set(SOURCES "")
        set(CUDA_TEST_NAME_SOURCE "${FILTERED_SOURCE}")
    elseif(FILTERED_SOURCE MATCHES "/linux/")
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
            message(FATAL_ERROR
                "Requested Linux-only source '${TEST_NAME}' on ${CMAKE_SYSTEM_NAME}")
        endif()
        set(SOURCES "${FILTERED_SOURCE}")
    else()
        set(SOURCES "${FILTERED_SOURCE}")
    endif()
    message(STATUS "Building only test source: ${TEST_NAME}")
endif()

# Loop through each source file and create a separate executable
foreach(SOURCE_FILE ${SOURCES})
    # Get the base name (e.g., test1 from test1.cpp)
    get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE)

    add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

    # Test sources include library headers root-relative ("corvid/...") and the
    # shared Catch2 main wrapper by bare name ("catch2_main.h"), so put the repo
    # root (parent of tests/) and tests/ itself on the include path. This keeps
    # includes independent of which bucket subdir a test lives in.
    target_include_directories(${EXECUTABLE_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}")

    # Every test file uses Catch2 except `notest_*` programs (long-lived
    # servers/demos that opt out via filename convention).
    if(NOT ${EXECUTABLE_NAME} MATCHES "^notest_")
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE Catch2::Catch2)
        add_test(NAME ${EXECUTABLE_NAME} COMMAND ${EXECUTABLE_NAME})
        # io_uring tests share the user's RLIMIT_MEMLOCK budget via
        # `io_uring_register_buffers`; running several in parallel exceeds
        # the limit and registrations fail with ENOMEM. Serialize them
        # against each other with RESOURCE_LOCK (they can still run in
        # parallel with non-iou tests). The label is for `ctest -L iou`.
        # `quic_dgram_*` tests sit on top of `iou_dgram_router`, so they
        # share the same constraints.
        if(${EXECUTABLE_NAME} MATCHES "^iou_|^quic_dgram_")
            set_tests_properties(${EXECUTABLE_NAME} PROPERTIES
                LABELS "iou"
                RESOURCE_LOCK "iou_memlock")
        endif()
    endif()

    # Linux-bucket targets link liburing, the QUIC stack, and the HTTP/3 stack.
    # The static linker drops archive members no symbol references, so a linux
    # target that touches none of these headers pays no measurable size or
    # link-time cost. The crypto target's INTERFACE_LINK_LIBRARIES carries
    # ngtcp2_static + OpenSSL transitively; nghttp3_static is standalone (no
    # crypto dependency). Portable-bucket targets link none of this (and on
    # non-Linux platforms these imported targets do not exist).
    if(SOURCE_FILE MATCHES "/linux/")
        target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIBURING_INCLUDE_DIRS})
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE
            ${LIBURING_LIBRARIES} ngtcp2_crypto_ossl_static nghttp3_static)
    endif()

    if(NOT USE_LIBSTDCPP AND NOT WIN32)
        # Link libc++ and libc++abi explicitly
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE c++ c++abi)
    endif()
endforeach()

# CUDA (.cu) test executables, the same single-source-per-executable model as
# the .cpp suite above but compiled by the CUDA compiler (nvcc + g++-15 host on
# Linux, clang++ on Windows). They use Catch2 like the rest of the suite; the
# Catch2 it links
# differs by platform (see the CUDA_CATCH2_TARGET split below). notest_* opt out
# of Catch2 and CTest, as in the .cpp loop. A .cu file reaches its headers
# root-relative ("corvid/cuda/...") and Catch2 by bare name ("catch2_main.h");
# the include path below adds the repo root and tests/.
if(CORVID_ENABLE_CUDA)
    if(DEFINED CUDA_TEST_NAME_SOURCE)
        set(CUDA_SOURCES "${CUDA_TEST_NAME_SOURCE}")
    elseif(TEST_NAME)
        set(CUDA_SOURCES "") # a .cpp single-file filter builds no .cu targets
    else()
        file(GLOB CUDA_SOURCES "cuda/*.cu")
        if(WIN32)
            # Windows-only CUDA cell: D3D/SDL3 targets that can't build on Linux
            # (see crossplatform.md s11). Globbed only here, under WIN32.
            file(GLOB CUDA_WINDOWS_SOURCES "cuda/windows/*.cu")
            list(APPEND CUDA_SOURCES ${CUDA_WINDOWS_SOURCES})
        endif()
    endif()

    if(CUDA_SOURCES)
        # cuBLAS ships with the CUDA toolkit; FindCUDAToolkit exposes it as the
        # CUDA::cublas imported target (include dirs + link). A .cu target opts
        # in below by referencing cublas in its source.
        find_package(CUDAToolkit REQUIRED)
        if(WIN32)
            # clang++ targets the MSVC ABI on Windows, so the .cu objects match
            # the clang-cl/cl .cpp suite's FetchContent Catch2 (also MSVC ABI).
            # Reuse it directly. The Linux split below (a separate system
            # Catch2) exists only because the .cpp suite there may be libc++
            # while the .cu targets are libstdc++; no such ABI divergence here.
            set(CUDA_CATCH2_TARGET Catch2::Catch2)
        else()
            # On Linux the .cu targets are libstdc++ (nvcc + g++-15 host), so
            # they need the system (apt) libstdc++ Catch2 rather than the libc++
            # FetchContent build the .cpp suite may be using (mixing ABIs gives
            # unresolved std::__1 / std::__cxx11 symbols at link).
            find_path(CUDA_CATCH2_INCLUDE_DIR catch2/catch_test_macros.hpp)
            find_library(CUDA_CATCH2_LIBRARY NAMES Catch2)
            if(NOT CUDA_CATCH2_INCLUDE_DIR OR NOT CUDA_CATCH2_LIBRARY)
                message(FATAL_ERROR
                    "CUDA tests use Catch2 but the system libstdc++ Catch2 was "
                    "not found (apt install catch2).")
            endif()
            # The catch2_main.h that each test includes supplies main() via
            # Catch::Session, so only libCatch2 is needed (not libCatch2Main).
            add_library(cuda_catch2 UNKNOWN IMPORTED)
            set_target_properties(cuda_catch2 PROPERTIES
                IMPORTED_LOCATION "${CUDA_CATCH2_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${CUDA_CATCH2_INCLUDE_DIR}")
            set(CUDA_CATCH2_TARGET cuda_catch2)
        endif()

        # Windows-only CUDA cell needs SDL3 (window/input/events). It is a
        # prebuilt drop in tests/.local/sdl3 (run scripts/fetch_sdl3.ps1),
        # mirroring the Linux OpenSSL prebuild: prebuilt rather than
        # FetchContent because SDL is a C API with no C++ ABI to match. Required
        # only when a windows-cell target is actually in the build, so the
        # portable and plain-cuda builds do not depend on it. See
        # crossplatform.md s11.
        set(_have_win_cuda FALSE)
        foreach(_s ${CUDA_SOURCES})
            if(_s MATCHES "/cuda/windows/")
                set(_have_win_cuda TRUE)
            endif()
        endforeach()
        if(_have_win_cuda)
            set(SDL3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.local/sdl3")
            if(NOT EXISTS "${SDL3_ROOT}/cmake/SDL3Config.cmake")
                message(FATAL_ERROR
                    "SDL3 not found at ${SDL3_ROOT}. "
                    "Run scripts/fetch_sdl3.ps1 first (one-time).")
            endif()
            find_package(SDL3 CONFIG REQUIRED
                PATHS "${SDL3_ROOT}/cmake" NO_DEFAULT_PATH)
            message(STATUS "Using SDL3 ${SDL3_VERSION} at ${SDL3_ROOT}")
        endif()
    endif()

    foreach(SOURCE_FILE ${CUDA_SOURCES})
        get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE)
        add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})
        # Repo root (parent of tests/) for "corvid/..." and tests/ for the
        # bare-name "catch2_main.h", as in the CXX loop above.
        target_include_directories(${EXECUTABLE_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}")
        # Auto-link cuBLAS for sources that use it, matching `nvcc ... -lcublas`.
        file(STRINGS ${SOURCE_FILE} CUDA_USES_CUBLAS REGEX "cublas")
        if(CUDA_USES_CUBLAS)
            target_link_libraries(${EXECUTABLE_NAME} PRIVATE CUDA::cublas)
        endif()
        # Windows-only CUDA cell links SDL3 and stages SDL3.dll next to the exe.
        # (The Windows SDK graphics libs d3d11/dxgi join here at the D3D
        # milestone.) See crossplatform.md s11.
        if(SOURCE_FILE MATCHES "/cuda/windows/")
            target_link_libraries(${EXECUTABLE_NAME} PRIVATE SDL3::SDL3)
            add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:SDL3::SDL3>"
                    "$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>")
        endif()
        if(NOT ${EXECUTABLE_NAME} MATCHES "^notest_")
            target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${CUDA_CATCH2_TARGET})
            add_test(NAME ${EXECUTABLE_NAME} COMMAND ${EXECUTABLE_NAME})
        endif()
    endforeach()
endif()

# Generate `.clangd` at the project root from `.clangd.in`. The dep paths
# baked in (ngtcp2, openssl) are absolute and tied to this checkout, so the
# generated file is gitignored; edit `.clangd.in` and re-configure to update.
# `CMAKE_SOURCE_DIR` is this `tests/` directory (the only CMake project in
# the repo), so the .. step is the actual project root.
get_filename_component(PROJECT_ROOT_DIR "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
if(WIN32)
    # Windows uses clang++ against the MSVC STL and builds only the portable
    # (and cuda) buckets, so its clangd config is far simpler than Linux: no
    # libc++ flags, no ngtcp2/openssl/liburing isystem paths. Generated from a
    # separate template. See crossplatform.md.
    #
    # The .cu/.cuh clangd block points clang's CUDA frontend at the toolkit.
    # Derive it from nvcc (independent of CORVID_ENABLE_CUDA, so .cu editing
    # works even in a cl/portable configure) and substitute the whole flag list,
    # so a box without nvcc just drops --cuda-path rather than emitting an empty
    # one. @ONLY substitution; see .clangd.win.in.
    find_program(_corvid_nvcc nvcc)
    if(_corvid_nvcc)
        get_filename_component(_corvid_cuda_bin "${_corvid_nvcc}" DIRECTORY)
        get_filename_component(_corvid_cuda_root "${_corvid_cuda_bin}/.." ABSOLUTE)
        set(CORVID_CLANGD_CUDA_FLAGS
            "-xcuda, --cuda-path=${_corvid_cuda_root}, --no-cuda-version-check, -std=c++23")
    else()
        set(CORVID_CLANGD_CUDA_FLAGS "-xcuda, --no-cuda-version-check, -std=c++23")
    endif()
    configure_file(
        "${PROJECT_ROOT_DIR}/.clangd.win.in"
        "${PROJECT_ROOT_DIR}/.clangd"
        @ONLY)
else()
    configure_file(
        "${PROJECT_ROOT_DIR}/.clangd.in"
        "${PROJECT_ROOT_DIR}/.clangd"
        @ONLY)
endif()
