# Copyright 2026 Edge-AI-Genesis
# W7 lib/：核心演示 —— add_library + INTERFACE/PRIVATE/PUBLIC 三种权限

add_library(w7_tensor_utils STATIC tensor_utils.cpp)

# =============================================================================
# target_include_directories 权限语义演示
# =============================================================================
#
#   PUBLIC    ：本库编译时 + 所有链接本库的目标（app/tests）都自动获得此路径
#   PRIVATE   ：仅本库自身编译时可见，消费者不继承
#   INTERFACE ：本库编译时不需要，但消费者需要（纯头文件库 header-only 的标准写法）
#
# 这里 tensor_utils.hpp 既是本库实现需要的，也是消费者需要 include 的
# → 用 PUBLIC，消费者只写 target_link_libraries 就能自动找到头文件，无需手动指定路径
#
# Generator Expression $<BUILD_INTERFACE:...>：
#   构建阶段 → 指向源码树中的真实目录
#   install 阶段 → 切换为安装后的路径（本项目暂不配置 install）
target_include_directories(
  w7_tensor_utils
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# PRIVATE 编译选项：只影响 tensor_utils.cpp 的编译，不传染给 app/tests
# 如果改为 PUBLIC/INTERFACE，app 和 tests 也会被加上 -O2，可能掩盖调试信息
target_compile_options(w7_tensor_utils PRIVATE -O2)

# INTERFACE 定义演示：本库自身编译时 *不* 定义此宏，但 app/tests 会自动获得
# 现实中用于让消费者知道库的版本，而无需本库头文件显式导出
target_compile_definitions(w7_tensor_utils
                           INTERFACE W7_TENSOR_UTILS_AVAILABLE=1)
