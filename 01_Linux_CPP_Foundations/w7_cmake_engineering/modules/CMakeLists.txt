# Copyright 2026 Edge-AI-Genesis
# W7 modules/：C++20 具名模块演示 —— CMake 3.28+ 的 CXX_MODULES 配置
#
# 知识点：
#   CMake 3.28 起通过 FILE_SET CXX_MODULES 声明模块源文件
#   构建系统自动处理模块依赖扫描（BMI 预编译模块接口文件生成）
#   GCC 13 对具名模块的支持仍为实验性特性

if(CMAKE_VERSION VERSION_LESS "3.28")
  message(
    STATUS
      "[W7 模块演示] CMake ${CMAKE_VERSION} < 3.28，跳过 C++20 具名模块配置"
      "（升级到 CMake 3.28+ 后此目录会自动启用）")
  return()
endif()

# 检查 GCC 是否支持具名模块（GCC 14+ 支持更完整）
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION
                                            VERSION_LESS "14")
  message(
    STATUS
      "[W7 模块演示] GCC ${CMAKE_CXX_COMPILER_VERSION} 对具名模块支持有限，"
      "仅作语法演示，如遇编译错误属预期行为")
endif()

# add_library 而非 add_executable：模块通常作为库被消费者 import
add_library(w7_hello_module STATIC)

# FILE_SET CXX_MODULES：CMake 3.28 新增的声明方式
# 告诉 CMake 这是模块接口单元（.cppm），需要先生成 BMI 再编译消费者
target_sources(w7_hello_module PUBLIC FILE_SET CXX_MODULES FILES
                                      hello_module.cppm)

set_target_properties(w7_hello_module PROPERTIES CXX_STANDARD 20
                                                  CXX_EXTENSIONS OFF)

# consumer 可执行文件：演示 import 具名模块的完整调用链
# GCC 13 对具名模块的 consumer 支持不稳定，仅在 GCC 14+ 下编译
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION
                                            VERSION_LESS "14")
  message(
    STATUS
      "[W7 模块演示] GCC ${CMAKE_CXX_COMPILER_VERSION} 跳过 consumer 编译"
      "（GCC 14+ 才有完整的具名模块 consumer 支持）")
else()
  add_executable(w7_module_consumer module_consumer.cpp)
  target_link_libraries(w7_module_consumer PRIVATE w7_hello_module)
  set_target_properties(w7_module_consumer PROPERTIES CXX_STANDARD 20
                                                       CXX_EXTENSIONS OFF)
endif()
