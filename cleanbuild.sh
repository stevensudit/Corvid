#!/bin/bash

# Set the environment variables to use clang
export CC="/usr/bin/clang-19"
export CXX="/usr/bin/clang++-19"

# Define the build directory (assuming you're using an out-of-source build)
buildDir="tests/release_bin"

rm "CMakeCache.txt"
rm "cmale_install.cmake"
rm "ClangExeProject.sln"
rm "build.ninja"
rm "CMakeFiles/*"
rm ".ninja_deps"
rm ".ninja_log"
rm "build.ninja"
rm "cmake_install.cmake"

# If the build directory exists, delete it to clean the build
if [ -d "$buildDir" ]; then
    echo "Cleaning the build directory..."
    rm -rf "$buildDir"
else
    echo "Build directory not found. Creating a new one."
fi

# Create the build directory
mkdir -p "$buildDir"

# Navigate to the build directory
cd "$buildDir" || exit

# Run cmake to configure the project with Ninja (or MinGW Makefiles) and clang
cmake -G "Ninja" ..

# Run the build (this will compile everything from scratch)
cmake --build .. --config Release

# Navigate back to the original directory after building
cd ..
