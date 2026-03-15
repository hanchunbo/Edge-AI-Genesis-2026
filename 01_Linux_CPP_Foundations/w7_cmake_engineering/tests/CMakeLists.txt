# Copyright 2026 Edge-AI-Genesis
# W7 tests/：GTest 集成演示 —— FetchContent 已在根目录配置，这里直接使用
#
# 这个文件和 app/CMakeLists.txt 结构几乎一样，区别是：
#   1. 额外链接了 GTest::gtest_main（测试框架，提供 main() 和 EXPECT_* 断言）
#   2. 注册的是测试目标而不是普通程序

# 编译测试可执行文件，源文件是 tensor_utils_test.cpp
add_executable(w7_tensor_utils_test tensor_utils_test.cpp)

# target_link_libraries(目标名  可见性  依赖1  依赖2  ...)
# 可以同时链接多个库，空格分隔即可
#
# w7_tensor_utils：被测试的库，同时自动继承它的 PUBLIC include 路径
#   → tensor_utils_test.cpp 里 #include "tensor_utils.hpp" 能直接找到
#
# GTest::gtest_main：Google Test 框架目标
#   提供两样东西：
#     1. main() 函数（运行所有 TEST() 测试用例）
#     2. EXPECT_EQ / ASSERT_TRUE 等断言宏
#   GTest::gtest_main 是命名空间形式（GTest:: 前缀），说明它是通过
#   FetchContent 或 find_package 引入的现代 CMake 目标，不是老式的库名字符串
target_link_libraries(
  w7_tensor_utils_test PRIVATE w7_tensor_utils GTest::gtest_main)

# 注册 CTest 测试
# 命名格式 W7_XxxTest 是本项目约定，方便用 ctest -R "W7_" 统一跑所有 W7 测试
add_test(NAME W7_TensorUtilsTest COMMAND w7_tensor_utils_test)
