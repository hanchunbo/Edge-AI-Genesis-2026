cmake_minimum_required(VERSION 3.16)
project(MoveSemanticsDemo LANGUAGES CXX)

# ============================================================================
# C++20 配置
# ============================================================================
# 本项目使用 C++20 特性：
# - std::span (零拷贝视图)
# - std::format (类型安全格式化)
# - Concepts (编译期类型约束)
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 强制使用 GCC 13
set(CMAKE_CXX_COMPILER "g++-13")

# 编译选项
add_compile_options(
    -Wall       # 开启所有警告
    -Wextra     # 额外警告
    -Wpedantic  # 严格标准警告
    -O2         # 优化等级（用于准确的性能测试）
)

# 可执行文件
add_executable(benchmark_move_vs_copy benchmark_move_vs_copy.cpp)
