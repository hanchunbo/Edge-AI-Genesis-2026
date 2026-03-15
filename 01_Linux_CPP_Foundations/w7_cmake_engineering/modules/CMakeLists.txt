# Copyright 2026 Edge-AI-Genesis
# W7 modules/：C++20/23 具名模块演示 —— CMake 3.28+ 的 CXX_MODULES 配置
#
# 具名模块 vs 传统头文件：
#   #include → 每个翻译单元都重复解析头文件，编译慢、宏污染全局
#   import   → 编译器只解析一次模块接口，生成 BMI，后续直接复用
#
# 环境要求：CMake 3.28+（FILE_SET CXX_MODULES 语法）、GCC 15+（import std;）、Ninja

# CMake < 3.28 不认识 FILE_SET CXX_MODULES，直接报错，版本不够就跳过
if(CMAKE_VERSION VERSION_LESS "3.28")
  message(
    STATUS
    "[W7 模块演示] CMake ${CMAKE_VERSION} < 3.28，跳过（升级到 3.28+ 后自动启用）")
  return()
endif()

# import std; 是 C++23 特性，GCC 14 存在符号冲突 bug，15 才稳定
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION
                                            VERSION_LESS "15")
  message(
    STATUS
    "[W7 模块演示] GCC ${CMAKE_CXX_COMPILER_VERSION} < 15，跳过（import std; 需要 GCC 15+）")
  return()
endif()

# 告诉 CMake 此项目使用 import std;，CMake 会自动预编译标准库模块
set(CMAKE_CXX_MODULE_STD ON)

# ── 模块库 ──────────────────────────────────────────────────────────────────
# add_library 不直接写源文件，源文件通过 target_sources 单独添加，
# 是为了使用 FILE_SET 语法——普通 SOURCES 集合无法声明模块接口文件
add_library(w7_hello_module STATIC)

# FILE_SET CXX_MODULES：CMake 3.28 新增的专用集合类型
#   作用：告诉 CMake "这个 .cppm 是模块接口单元，需要先生成 BMI（预编译模块接口）"
#   CMake + Ninja 通过 dyndep 机制自动保证编译顺序：
#     先编译 hello_module.cppm → 生成 w7.hello.gcm（BMI）
#     再编译 import w7.hello 的消费者文件
#   PUBLIC：模块 BMI 路径对链接此库的目标可见，消费者 import 时 CMake 能找到对应 BMI
target_sources(w7_hello_module PUBLIC FILE_SET CXX_MODULES FILES
                                      hello_module.cppm)

# import std; 是 C++23 的标准库模块语法，必须指定 C++23
# CXX_EXTENSIONS OFF → 用 -std=c++23 而非 -std=gnu++23，避免引入 GCC 私有扩展
set_target_properties(w7_hello_module PROPERTIES CXX_STANDARD 23
                                                  CXX_EXTENSIONS OFF)

# ── 消费者可执行文件 ─────────────────────────────────────────────────────────
add_executable(w7_module_consumer module_consumer.cpp)

# target_link_libraries 让 CMake 知道 consumer 依赖 w7_hello_module，
# 从而保证链接顺序正确，同时传播 FILE_SET 声明的 BMI 路径信息
target_link_libraries(w7_module_consumer PRIVATE w7_hello_module)

# consumer 同样用 C++23（import w7.hello 内部做了 export import std;，需要 C++23 解析）
set_target_properties(w7_module_consumer PROPERTIES CXX_STANDARD 23
                                                     CXX_EXTENSIONS OFF)

# CMake 对 FILE_SET CXX_MODULES 文件自动加 -fmodules，但不传播给消费者 .cpp
# 消费者需要手动指定：
#   -fmodules        → 启用模块解析（import 关键字）
#   -fmodule-mapper  → 告诉 GCC w7.hello → .gcm 文件在哪里（CMake 编译模块时生成）
target_compile_options(
  w7_module_consumer
  PRIVATE
    -fmodules
    -fmodule-mapper=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/w7_hello_module.dir/hello_module.cppm.o.modmap
)
