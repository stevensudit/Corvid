cmake_minimum_required(VERSION 3.16)

project(Corvid VERSION 1.0 LANGUAGES CXX)

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

if(USE_LIBSTDCPP)
    # Rely on whichever clang is available and the default libstdc++ install.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -Werror -std=c++23")
else()
    # Use Clang 22 and avoid pulling in GCC.
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++-22")
    set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-linux-gnu")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -Werror -nostdinc++ -isystem /usr/lib/llvm-22/include/c++/v1 -std=c++23")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ --rtlib=compiler-rt -fuse-ld=lld")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/usr/lib/llvm-22/lib -lc++ -lc++abi")
endif()

# Use ccache as a compiler launcher when available so that cleanbuild's
# `rm -rf tests/build/` doesn't trigger a full Catch2 (and Corvid) recompile
# every run. Cache hits return cached `.o` files in milliseconds.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
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
