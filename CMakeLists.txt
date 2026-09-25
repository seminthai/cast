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

# Configure advanced compiler options based on the toolchain
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /W4          # High warning level
        /O2          # Maximize speed optimization
        /Ot          # Favor fast code over small code size
        /Oi          # Enable intrinsic functions generation
        # ⚡ AVX2 vector extensions for hardware SIMD speedup on binary streams
        /arch:AVX2   
        /permissive- # Enforce strict C++ standard conformance
        /D_CRT_SECURE_NO_WARNINGS # Suppress legacy MSVC security warnings
    )
    
    # 💎 Enable Link-Time Optimization (LTO / GL) for cross-module optimization
    set_target_properties(${PROJECT_NAME} PROPERTIES 
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    )
else()
    # Fallback flags for GCC/Clang environments
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O3 -march=native)
endif()
