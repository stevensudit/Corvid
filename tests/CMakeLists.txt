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
# overrides this to the locally-built MSAN-instrumented libc++ so that the
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

if(USE_LIBSTDCPP)
    # Rely on whichever clang is available and the default libstdc++ install.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -Werror -std=c++23")
else()
    # Use Clang 22 and avoid pulling in GCC.
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++-22")
    set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-linux-gnu")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -Werror -nostdinc++ -isystem ${LIBCXX_PREFIX}/include/c++/v1 -std=c++23")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ --rtlib=compiler-rt -fuse-ld=lld")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${LIBCXX_PREFIX}/lib -lc++ -lc++abi")
    # Embed an rpath so binaries find libc++/libunwind .so files at the chosen
    # prefix without requiring LD_LIBRARY_PATH. Harmless for the system path,
    # essential for SANITIZER=msan where LIBCXX_PREFIX is outside ld.so config.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${LIBCXX_PREFIX}/lib")
endif()

# Use ccache as a compiler launcher when available so that cleanbuild's
# `rm -rf tests/build/` doesn't trigger a full Catch2 (and Corvid) recompile
# every run. Cache hits return cached `.o` files in milliseconds.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
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

# External dependencies: requires apt install liburing-dev pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

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

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/release_bin")

# Source files (only in this directory)
file(GLOB SOURCES "*.cpp")

if(TEST_NAME)
    set(FILTERED_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_NAME}")
    if(NOT EXISTS "${FILTERED_SOURCE}")
        message(FATAL_ERROR "Requested test source not found: ${TEST_NAME}")
    endif()

    set(SOURCES "${FILTERED_SOURCE}")
    message(STATUS "Building only test source: ${TEST_NAME}")
endif()

# Loop through each source file and create a separate executable
foreach(SOURCE_FILE ${SOURCES})
    # Get the base name (e.g., test1 from test1.cpp)
    get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE)

    add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

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
        if(${EXECUTABLE_NAME} MATCHES "^iou_")
            set_tests_properties(${EXECUTABLE_NAME} PROPERTIES
                LABELS "iou"
                RESOURCE_LOCK "iou_memlock")
        endif()
    endif()

    if(${EXECUTABLE_NAME} MATCHES "^iou_")
        target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIBURING_INCLUDE_DIRS})
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${LIBURING_LIBRARIES})
    endif()

    if(NOT USE_LIBSTDCPP)
        # Link libc++ and libc++abi explicitly
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE c++ c++abi)
    endif()
endforeach()
