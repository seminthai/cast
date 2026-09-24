cmake_minimum_required(VERSION 3.15)
project(cast VERSION 1.0 LANGUAGES CXX)

# Enable C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find and configure OpenMP for multi-threaded workloads
find_package(OpenMP REQUIRED)

# List of source files located in the src/ directory
set(SOURCES
    src/main.cpp
    src/Pipeline.cpp
    src/BitStream.cpp
)

# Define the executable target
add_executable(${PROJECT_NAME} ${SOURCES})

# Link OpenMP libraries to the target
target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)

# Configure compiler warnings based on the toolchain
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()
