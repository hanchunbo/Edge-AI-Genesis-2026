# Copyright 2026 Edge-AI-Genesis
# W7 tests/：GTest 集成演示 —— FetchContent 已在根目录配置，这里直接使用

add_executable(w7_tensor_utils_test tensor_utils_test.cpp)

# 同样无需 target_include_directories，PUBLIC 路径通过 w7_tensor_utils 自动继承
target_link_libraries(
  w7_tensor_utils_test PRIVATE w7_tensor_utils GTest::gtest_main)

add_test(NAME W7_TensorUtilsTest COMMAND w7_tensor_utils_test)
