cmake_minimum_required(VERSION 3.16)

project(Corvid VERSION 1.0 LANGUAGES CXX)

# Set the C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use Clang 19 and avoid pulling in GCC
set(CMAKE_CXX_COMPILER "/usr/bin/clang++-19")
set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-linux-gnu")

# Compilation and linking flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra -Werror -nostdinc++ -isystem /usr/include/c++/v1 -std=c++23")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ --rtlib=compiler-rt -fuse-ld=lld")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/usr/lib/llvm-19/lib -lc++ -lc++abi")

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/release_bin")

# Source files
file(GLOB_RECURSE SOURCES "*.cpp")

# Loop through each source file and create a separate executable
foreach(SOURCE_FILE ${SOURCES})
    # Get the base name (e.g., test1 from test1.cpp)
    get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE)

    # Add executable for each source file
    add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

    # Link libc++ and libc++abi explicitly
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE c++ c++abi)
endforeach()
