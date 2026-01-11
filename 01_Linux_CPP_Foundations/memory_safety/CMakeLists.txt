cmake_minimum_required(VERSION 3.16)
project(SafeTensorDemo LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
add_compile_options(
    -Wall       # 开启所有警告
    -Wextra     # 额外警告
    -Wpedantic  # 严格标准警告
    -g          # 调试信息（用于 Valgrind）
)

# 可执行文件
add_executable(safe_tensor_demo main.cpp)
