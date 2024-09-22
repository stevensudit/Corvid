cmake_minimum_required(VERSION 3.10)

# Set the project name
project(Corvid)

# Use Clang as the compiler and enable libc++
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -nostdinc++ -isystem C:/Program Files/LLVM/include/c++/v1")
    set(CMAKE_LINK_FLAGS "${CMAKE_LINK_FLAGS} -stdlib=libc++ -LC:/Program Files/LLVM/lib -lc++ -lc++abi")
endif()

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find all .cpp files in the current directory
file(GLOB SOURCES "*.cpp")

# Add the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Custom target to clean the executable
# cmake --build . --target clean-all
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/${PROJECT_NAME}
    COMMENT "Cleaning up generated files"
)
