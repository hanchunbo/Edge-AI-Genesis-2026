cmake_minimum_required(VERSION 3.16)
project(SafeTensorDemo LANGUAGES CXX)

# ============================================================================
# C++20/23 配置 (需要 GCC 13+)
# ============================================================================
# 本项目使用 C++20 作为基准标准，部分特性依赖 C++23：
# - std::expected (C++23)
# - std::format (C++20)
# - Concepts (C++20)
# - std::span (C++20)
#
# 如需 std::expected，请使用 GCC 13+ 并启用 C++23
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 强制使用 GCC 13（支持 C++23 特性）
set(CMAKE_CXX_COMPILER "g++-13")

# 编译选项
add_compile_options(
    -Wall       # 开启所有警告
    -Wextra     # 额外警告
    -Wpedantic  # 严格标准警告
    -g          # 调试信息（用于 Valgrind）
)

# 可执行文件
add_executable(safe_tensor_demo main.cpp)
