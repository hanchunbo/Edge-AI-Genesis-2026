# Copyright 2026 Edge-AI-Genesis
# Top-level CMake configuration for Edge-AI-Genesis-2026

cmake_minimum_required(VERSION 3.16)

# 编译器设置（必须在 project() 之前）
set(CMAKE_CXX_COMPILER "g++-13" CACHE STRING "C++ compiler")

project(EdgeAIGenesis2026 LANGUAGES CXX)

# =============================================================================
# 公共默认配置
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 公共编译警告
add_compile_options(
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wextra>
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wpedantic>
)

# =============================================================================
# 可选 Sanitizer 支持
# =============================================================================
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -g)
  add_link_options(-fsanitize=thread)
endif()

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -g)
  add_link_options(-fsanitize=address)
endif()

# =============================================================================
# Google Test (FetchContent)
# =============================================================================
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Prevent GTest from overriding compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(CTest)

# =============================================================================
# 子模块
# =============================================================================
add_subdirectory(01_Linux_CPP_Foundations/w1_memory_safety)
add_subdirectory(01_Linux_CPP_Foundations/w2_move_semantics)
add_subdirectory(01_Linux_CPP_Foundations/w3_filesystem)
add_subdirectory(01_Linux_CPP_Foundations/w4_threading)
add_subdirectory(01_Linux_CPP_Foundations/w5_thread_pool)
add_subdirectory(01_Linux_CPP_Foundations/w6_mmap_loader)
