cmake_minimum_required(VERSION 3.16)

project(Corvid VERSION 1.0 LANGUAGES CXX)

# Set the C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer clang++, falling back to the compiler configured in the environment.
find_program(CLANGXX clang++ clang++-19)
if (CLANGXX)
  set(CMAKE_CXX_COMPILER "${CLANGXX}" CACHE FILEPATH "C++ compiler" FORCE)
endif()

# Compilation and linking flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra -Werror -std=c++23")

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
