# Copyright 2026 Edge-AI-Genesis-2026
# CMake configuration for W3: Filesystem Model Scanner

cmake_minimum_required(VERSION 3.14)
project(ModelScanner LANGUAGES CXX)

# 要求 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
add_compile_options(-Wall -Wextra -Wpedantic)

# 添加可执行文件
add_executable(model_scanner model_scanner.cpp)

# 在某些旧版本 GCC 上可能需要链接 stdc++fs
# GCC 9+ 和 Clang 9+ 已将 filesystem 内置，无需额外链接
# 但为了兼容性，我们检测并添加
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(model_scanner PRIVATE stdc++fs)
endif()
