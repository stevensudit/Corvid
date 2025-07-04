cmake_minimum_required(VERSION 3.16)

project(Corvid VERSION 1.0 LANGUAGES CXX)

# Allow selecting the standard library. Passing -DUSE_LIBSTDCPP=ON to cmake will
# force libstdc++. Otherwise, the default is libc++ unless we're running on
# Codex, where libstdc++ is required.
option(USE_LIBSTDCPP "Use GNU libstdc++ instead of libc++" OFF)
option(USE_CLANG_TIDY "Run clang-tidy analysis" OFF)

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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra -Werror -std=c++23")
else()
    # Use Clang 19 and avoid pulling in GCC.
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++-19")
    set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-linux-gnu")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra -Werror -nostdinc++ -isystem /usr/include/c++/v1 -std=c++23")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ --rtlib=compiler-rt -fuse-ld=lld")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/usr/lib/llvm-19/lib -lc++ -lc++abi")
endif()

if(USE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-19 clang-tidy-20)
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

# Loop through each source file and create a separate executable
foreach(SOURCE_FILE ${SOURCES})
    # Get the base name (e.g., test1 from test1.cpp)
    get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE)

    # Add executable for each source file
    add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

    if(NOT USE_LIBSTDCPP)
        # Link libc++ and libc++abi explicitly
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE c++ c++abi)
    endif()
endforeach()
