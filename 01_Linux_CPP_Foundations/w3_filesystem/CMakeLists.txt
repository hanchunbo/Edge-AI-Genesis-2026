# Copyright 2026 Edge-AI-Genesis-2026
# CMake configuration for W3: Filesystem Model Scanner (C++20)

cmake_minimum_required(VERSION 3.16)
project(ModelScanner LANGUAGES CXX)

# ============================================================================
# C++20 配置
# ============================================================================
# 本项目使用 C++20 特性：
# - std::format (替代 iostream 字符串拼接)
# - std::string_view (零拷贝字符串视图)
# - Concepts (类型约束) [可选]
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 强制使用 GCC 13
set(CMAKE_CXX_COMPILER "g++-13")

# 编译选项
add_compile_options(-Wall -Wextra -Wpedantic)

# 添加可执行文件
add_executable(model_scanner model_scanner.cpp)
