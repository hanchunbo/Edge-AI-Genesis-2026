# Copyright 2026 Edge-AI-Genesis
# W7: 现代 CMake 工程化演示 —— lib/app/tests 三层结构 + C++20 模块支持
#
# 这是 w7 的"总目录"CMakeLists.txt。
# 它本身不编译任何代码，只负责告诉 CMake "去哪些子目录里找代码"。
# 根目录的 CMakeLists.txt 通过 add_subdirectory(w7_cmake_engineering) 进入这里。

# add_subdirectory(子目录路径)
# → CMake 会进入该子目录，执行里面的 CMakeLists.txt
# → 顺序很重要：lib 必须在 app 前面，因为 app 要用 lib 里定义的库
#   如果反过来写，CMake 会报错说找不到 w7_tensor_utils 目标

# 第一步：进入 lib/，定义 w7_tensor_utils 静态库
add_subdirectory(lib)

# 第二步：进入 app/，定义 w7_app 可执行文件（它会链接上面定义的库）
add_subdirectory(app)

# BUILD_TESTING 是 CMake 内置的开关变量（默认为 ON）
# 用 cmake -DBUILD_TESTING=OFF 可以关掉，这样就不会去下载/编译 GTest
# → 在嵌入式/资源受限环境中常用，减少构建时间
if(BUILD_TESTING)
  # 第三步：进入 tests/，定义单元测试可执行文件
  add_subdirectory(tests)
endif()

# 第四步：进入 modules/，演示 C++20 具名模块（CMake 3.28+ 才支持）
add_subdirectory(modules)
