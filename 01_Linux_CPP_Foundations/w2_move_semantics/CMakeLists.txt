cmake_minimum_required(VERSION 3.16)
project(MoveSemanticsDemo LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
add_compile_options(
    -Wall       # 开启所有警告
    -Wextra     # 额外警告
    -Wpedantic  # 严格标准警告
    -O2         # 优化等级（用于准确的性能测试）
)

# 可执行文件
add_executable(benchmark_move_vs_copy benchmark_move_vs_copy.cpp)
