# Copyright 2026 Edge-AI-Genesis
# W7: 现代 CMake 工程化演示 —— lib/app/tests 三层结构 + C++20 模块支持

# 核心库：演示 add_library + PUBLIC/PRIVATE/INTERFACE 权限
add_subdirectory(lib)

# 可执行程序：演示 target_link_libraries 自动继承 PUBLIC include 路径
add_subdirectory(app)

# 单元测试：演示 GTest 集成（FetchContent 已在根目录配置）
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# C++20 具名模块（实验性）：演示 CMake 3.28+ 的 CXX_MODULES 配置
add_subdirectory(modules)
